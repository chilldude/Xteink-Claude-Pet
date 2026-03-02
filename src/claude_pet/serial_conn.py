"""Serial connection manager for the Xteink X4.

Handles auto-discovery by USB VID/PID, COBS framing, CRC validation,
ACK-wait retries, heartbeat PINGs, and graceful disconnect.  All
public methods are async-safe and can be called from the asyncio event
loop.
"""

from __future__ import annotations

import asyncio
import logging
import threading
import time
from typing import Callable, Optional

import serial
import serial.tools.list_ports
from cobs import cobs

from .protocol import (
    Cmd,
    PacketError,
    Resp,
    Response,
    build_packet,
    cmd_ping,
    parse_response,
)

log = logging.getLogger(__name__)

# Xteink X4 USB identifiers (Espressif native USB)
X4_VID = 0x303A
X4_PID = 0x1001

# Timing constants
SCAN_INTERVAL_S = 2.0
ACK_TIMEOUT_S = 0.1
ACK_RETRIES = 3
HEARTBEAT_INTERVAL_S = 5.0
MISSED_PONG_LIMIT = 3
BAUD_RATE = 115200  # USB CDC ignores this, but pyserial needs a value


def find_x4_port() -> Optional[str]:
    """Scan serial ports and return the device path of the first X4 found."""
    for port in serial.tools.list_ports.comports():
        if port.vid == X4_VID and port.pid == X4_PID:
            return port.device
    return None


class SerialConnection:
    """Manages the serial link to a single X4 device.

    Thread-safety: all serial I/O is funnelled through a single
    background thread.  The asyncio layer communicates via queues and
    events.
    """

    def __init__(self) -> None:
        self._ser: Optional[serial.Serial] = None
        self._lock = threading.Lock()
        self._loop: Optional[asyncio.AbstractEventLoop] = None

        # Incoming COBS frame buffer
        self._rx_buf = bytearray()

        # Parsed responses waiting to be consumed
        self._response_queue: asyncio.Queue[Response] = asyncio.Queue()

        # External callback for unsolicited device messages (BUTTON, BATTERY)
        self.on_unsolicited: Optional[Callable[[Response], None]] = None

        # Connection state
        self._connected = asyncio.Event()
        self._device_path: Optional[str] = None

        # Heartbeat bookkeeping
        self._missed_pongs = 0

        # Shutdown flag
        self._closing = False

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def connected(self) -> bool:
        return self._connected.is_set()

    @property
    def device_path(self) -> Optional[str]:
        return self._device_path

    # ------------------------------------------------------------------
    # Connection lifecycle
    # ------------------------------------------------------------------

    async def maintain_connection(self) -> None:
        """Loop forever: discover, connect, read, reconnect on failure."""
        self._loop = asyncio.get_running_loop()
        while not self._closing:
            if not self.connected:
                await self._try_connect()
            if self.connected:
                await self._read_loop()
            # If we fall through, the device was lost.
            await asyncio.sleep(SCAN_INTERVAL_S)

    async def _try_connect(self) -> None:
        """Attempt to find and open the X4."""
        path = await asyncio.get_running_loop().run_in_executor(
            None, find_x4_port
        )
        if path is None:
            return

        try:
            ser = await asyncio.get_running_loop().run_in_executor(
                None, lambda: serial.Serial(path, BAUD_RATE, timeout=0.05)
            )
        except serial.SerialException as exc:
            log.debug("Failed to open %s: %s", path, exc)
            return

        with self._lock:
            self._ser = ser
            self._device_path = path
            self._rx_buf.clear()
            self._missed_pongs = 0

        self._connected.set()
        log.info("Connected to X4 at %s", path)

    def _disconnect(self, reason: str = "unknown") -> None:
        """Close the serial port and reset state."""
        with self._lock:
            if self._ser is not None:
                try:
                    self._ser.close()
                except Exception:
                    pass
                self._ser = None
            self._device_path = None
            self._rx_buf.clear()

        if self._connected.is_set():
            self._connected.clear()
            log.info("Disconnected from X4 (%s)", reason)

    async def close(self) -> None:
        """Shut down gracefully."""
        self._closing = True
        self._disconnect("shutdown")

    # ------------------------------------------------------------------
    # Read loop
    # ------------------------------------------------------------------

    async def _read_loop(self) -> None:
        """Read bytes from serial, COBS-decode frames, dispatch."""
        loop = asyncio.get_running_loop()
        while self.connected and not self._closing:
            try:
                chunk = await loop.run_in_executor(None, self._raw_read)
            except Exception:
                self._disconnect("read error")
                return

            if chunk is None:
                # Port gone
                self._disconnect("port disappeared")
                return

            if not chunk:
                # Timeout, no data -- yield to other tasks
                await asyncio.sleep(0)
                continue

            self._rx_buf.extend(chunk)
            self._process_rx_buffer()

    def _raw_read(self) -> Optional[bytes]:
        """Read available bytes (called in executor thread)."""
        with self._lock:
            if self._ser is None:
                return None
            try:
                # Read up to 4096 bytes with the configured timeout
                data = self._ser.read(4096)
                return data
            except (serial.SerialException, OSError):
                return None

    def _process_rx_buffer(self) -> None:
        """Extract complete COBS frames (delimited by 0x00) from _rx_buf."""
        while True:
            delim = self._rx_buf.find(0x00)
            if delim == -1:
                break

            frame_bytes = bytes(self._rx_buf[:delim])
            del self._rx_buf[: delim + 1]

            if not frame_bytes:
                continue  # empty frame, skip

            try:
                raw = cobs.decode(frame_bytes)
            except cobs.DecodeError as exc:
                log.debug("COBS decode error: %s", exc)
                continue

            try:
                resp = parse_response(raw)
            except PacketError as exc:
                log.debug("Packet parse error: %s", exc)
                continue

            self._dispatch_response(resp)

    def _dispatch_response(self, resp: Response) -> None:
        """Route a parsed response to the right consumer."""
        if resp.code in (Resp.ACK, Resp.PONG):
            # These are replies to commands we sent -- put on the queue
            try:
                self._response_queue.put_nowait(resp)
            except asyncio.QueueFull:
                log.warning("Response queue full, dropping %s", resp.code.name)
        else:
            # Unsolicited: BUTTON, BATTERY, ERROR
            if self.on_unsolicited is not None:
                try:
                    self.on_unsolicited(resp)
                except Exception:
                    log.exception("Error in unsolicited callback")

    # ------------------------------------------------------------------
    # Send with ACK
    # ------------------------------------------------------------------

    async def send(self, raw_packet: bytes, expect_ack: bool = True) -> Optional[Response]:
        """COBS-encode and send *raw_packet*.

        If *expect_ack* is True, waits for an ACK (up to ACK_RETRIES
        attempts).  Returns the ACK Response or None on failure.
        """
        encoded = cobs.encode(raw_packet) + b"\x00"

        for attempt in range(1, ACK_RETRIES + 1):
            ok = await asyncio.get_running_loop().run_in_executor(
                None, self._raw_write, encoded
            )
            if not ok:
                return None

            if not expect_ack:
                return None

            try:
                resp = await asyncio.wait_for(
                    self._drain_for_ack(), timeout=ACK_TIMEOUT_S
                )
                if resp is not None:
                    return resp
            except asyncio.TimeoutError:
                log.debug(
                    "ACK timeout (attempt %d/%d)", attempt, ACK_RETRIES
                )

        log.warning("No ACK after %d retries", ACK_RETRIES)
        return None

    async def _drain_for_ack(self) -> Optional[Response]:
        """Pull from the response queue looking for an ACK or PONG."""
        resp = await self._response_queue.get()
        return resp

    def _raw_write(self, data: bytes) -> bool:
        """Write bytes to serial (called in executor thread)."""
        with self._lock:
            if self._ser is None:
                return False
            try:
                self._ser.write(data)
                self._ser.flush()
                return True
            except (serial.SerialException, OSError):
                self._disconnect("write error")
                return False

    # ------------------------------------------------------------------
    # Heartbeat
    # ------------------------------------------------------------------

    async def heartbeat_loop(self) -> None:
        """Send PINGs periodically; reconnect if device stops responding."""
        while not self._closing:
            if self.connected:
                resp = await self.send(cmd_ping(), expect_ack=True)
                if resp is not None and resp.code == Resp.PONG:
                    self._missed_pongs = 0
                else:
                    self._missed_pongs += 1
                    log.debug(
                        "Missed PONG (%d/%d)",
                        self._missed_pongs,
                        MISSED_PONG_LIMIT,
                    )
                    if self._missed_pongs >= MISSED_PONG_LIMIT:
                        self._disconnect("heartbeat timeout")
            await asyncio.sleep(HEARTBEAT_INTERVAL_S)
