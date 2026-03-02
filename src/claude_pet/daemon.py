"""Main daemon orchestrating serial, socket, and state management.

Runs an asyncio event loop that gathers:
  - serial_conn.maintain_connection()  -- auto-discover & stay connected
  - serial_conn.heartbeat_loop()       -- keep-alive PINGs
  - socket_server.listen()             -- receive hook events

When a hook event arrives via the socket, it is mapped to a pet state
and sent to the device as SET_STATE + TEXT commands.  On reconnect the
current state is re-pushed.
"""

from __future__ import annotations

import asyncio
import logging
import signal
import sys
from typing import Optional

from .protocol import Resp, Response, cmd_set_state, cmd_text, cmd_clear
from .serial_conn import SerialConnection
from .socket_server import SocketServer
from .state_machine import State, resolve_state, STATE_LABELS

log = logging.getLogger(__name__)

# Display layout constants (for text placement on 800x480)
STATUS_TEXT_X = 50
STATUS_TEXT_Y = 430
STATUS_TEXT_SIZE = 2


class Daemon:
    """The main claude-pet daemon."""

    def __init__(self) -> None:
        self._serial = SerialConnection()
        self._socket = SocketServer(on_event=self._on_hook_event)
        self._current_state: State = State.IDLE
        self._running = False

        # Wire up unsolicited serial messages
        self._serial.on_unsolicited = self._on_device_message

    # ------------------------------------------------------------------
    # Public entry point
    # ------------------------------------------------------------------

    def run(self) -> None:
        """Start the daemon (blocking)."""
        try:
            asyncio.run(self._main())
        except KeyboardInterrupt:
            pass

    async def _main(self) -> None:
        """Async main: set up signals, gather all tasks."""
        self._running = True
        loop = asyncio.get_running_loop()

        # Handle SIGTERM / SIGINT gracefully
        for sig in (signal.SIGTERM, signal.SIGINT):
            loop.add_signal_handler(sig, self._request_shutdown)

        log.info("claude-petd starting")

        # Wait for connection, then push initial state
        asyncio.create_task(self._resync_on_connect())

        try:
            await asyncio.gather(
                self._serial.maintain_connection(),
                self._serial.heartbeat_loop(),
                self._socket.listen(),
            )
        except asyncio.CancelledError:
            pass
        finally:
            await self._shutdown()

    def _request_shutdown(self) -> None:
        """Signal handler: cancel all tasks."""
        log.info("Shutdown requested")
        self._running = False
        for task in asyncio.all_tasks():
            task.cancel()

    async def _shutdown(self) -> None:
        """Clean up resources."""
        await self._serial.close()
        await self._socket.close()
        log.info("claude-petd stopped")

    # ------------------------------------------------------------------
    # Hook event handling (called from socket server)
    # ------------------------------------------------------------------

    def _on_hook_event(self, hook: str, detail: Optional[str]) -> None:
        """Map a hook event to a state transition and push to device."""
        new_state = resolve_state(hook, detail)
        if new_state is None:
            log.debug("No state mapping for hook=%s detail=%s", hook, detail)
            return

        if new_state == self._current_state:
            log.debug("State unchanged: %s", new_state.name)
            return

        old = self._current_state
        self._current_state = new_state
        log.info("State: %s -> %s (hook=%s)", old.name, new_state.name, hook)

        # Fire-and-forget: schedule the send on the event loop
        loop = asyncio.get_event_loop()
        if loop.is_running():
            loop.create_task(self._push_state(new_state))

    async def _push_state(self, state: State) -> None:
        """Send SET_STATE and status text to the device."""
        if not self._serial.connected:
            return

        # Send SET_STATE
        await self._serial.send(cmd_set_state(state))

        # Send status label text
        label = STATE_LABELS.get(state, state.name)
        await self._serial.send(
            cmd_text(STATUS_TEXT_X, STATUS_TEXT_Y, STATUS_TEXT_SIZE, label)
        )

    # ------------------------------------------------------------------
    # Reconnect sync
    # ------------------------------------------------------------------

    async def _resync_on_connect(self) -> None:
        """Whenever the device (re)connects, push the current state."""
        while self._running:
            # Wait for connection
            await self._serial._connected.wait()
            log.info("Device connected, syncing state: %s", self._current_state.name)
            await self._push_state(self._current_state)
            # Now wait for disconnect so we can re-sync next time
            while self._serial.connected and self._running:
                await asyncio.sleep(0.5)

    # ------------------------------------------------------------------
    # Unsolicited device messages
    # ------------------------------------------------------------------

    def _on_device_message(self, resp: Response) -> None:
        """Handle unsolicited messages from the device."""
        if resp.code == Resp.BUTTON:
            log.info("Button pressed: %d", resp.button_id)
        elif resp.code == Resp.BATTERY:
            log.debug(
                "Battery: %d mV, charging=%s",
                resp.battery_mv,
                resp.battery_charging,
            )
        elif resp.code == Resp.ERROR:
            log.warning(
                "Device error %d: %s", resp.error_code, resp.error_msg
            )
