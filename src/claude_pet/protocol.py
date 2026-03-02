"""Binary packet protocol matching the X4 firmware.

Packet format (before COBS):
    [CMD(1)][LENGTH(2, LE)][PAYLOAD(0..4096)][CRC16(2)]

On the wire the raw packet is COBS-encoded and terminated with a 0x00
delimiter byte.

CRC is CRC-16/CCITT (poly 0x1021, init 0xFFFF) computed over
CMD + LENGTH + PAYLOAD.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

import crcmod

# ---------------------------------------------------------------------------
# CRC helper
# ---------------------------------------------------------------------------

_crc16_fn = crcmod.predefined.mkCrcFun("crc-ccitt-false")


def crc16_ccitt(data: bytes) -> int:
    """Return CRC-16/CCITT-FALSE over *data*."""
    return _crc16_fn(data)


# ---------------------------------------------------------------------------
# Host -> Device commands
# ---------------------------------------------------------------------------

class Cmd(IntEnum):
    CLEAR = 0x01
    SPRITE = 0x02
    TEXT = 0x03
    BITMAP_FULL = 0x04
    BITMAP_REGION = 0x05
    REFRESH = 0x06
    SET_STATE = 0x07
    PING = 0x08


# ---------------------------------------------------------------------------
# Device -> Host response codes
# ---------------------------------------------------------------------------

class Resp(IntEnum):
    ACK = 0x81
    BUTTON = 0x82
    PONG = 0x83
    BATTERY = 0x84
    ERROR = 0x85


# ---------------------------------------------------------------------------
# Refresh modes
# ---------------------------------------------------------------------------

class RefreshMode(IntEnum):
    PARTIAL = 0
    FULL = 1


# ---------------------------------------------------------------------------
# Parsed response container
# ---------------------------------------------------------------------------

@dataclass
class Response:
    code: Resp
    payload: bytes

    # Convenience accessors ---------------------------------------------------

    @property
    def ack_cmd(self) -> int:
        """For ACK responses, the echoed command byte."""
        assert self.code == Resp.ACK and len(self.payload) >= 1
        return self.payload[0]

    @property
    def button_id(self) -> int:
        assert self.code == Resp.BUTTON and len(self.payload) >= 1
        return self.payload[0]

    @property
    def battery_mv(self) -> int:
        assert self.code == Resp.BATTERY and len(self.payload) >= 3
        return struct.unpack_from("<H", self.payload, 0)[0]

    @property
    def battery_charging(self) -> bool:
        assert self.code == Resp.BATTERY and len(self.payload) >= 3
        return bool(self.payload[2])

    @property
    def error_code(self) -> int:
        assert self.code == Resp.ERROR and len(self.payload) >= 1
        return self.payload[0]

    @property
    def error_msg(self) -> str:
        assert self.code == Resp.ERROR and len(self.payload) >= 1
        return self.payload[1:].decode("utf-8", errors="replace")


# ---------------------------------------------------------------------------
# Packet construction
# ---------------------------------------------------------------------------

MAX_PAYLOAD = 4096


def build_packet(cmd: int, payload: bytes = b"") -> bytes:
    """Build a raw packet (before COBS encoding).

    Returns bytes: CMD(1) + LENGTH(2, LE) + PAYLOAD + CRC16(2).
    """
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"Payload too large: {len(payload)} > {MAX_PAYLOAD}")

    length = len(payload)
    header = struct.pack("<BH", cmd, length)
    body = header + payload
    crc = crc16_ccitt(body)
    return body + struct.pack("<H", crc)


# ---------------------------------------------------------------------------
# Packet parsing
# ---------------------------------------------------------------------------

class PacketError(Exception):
    """Raised when a received packet is malformed."""


def parse_response(data: bytes) -> Response:
    """Parse a raw packet (after COBS decoding) into a Response.

    Validates CRC and minimum length.
    """
    # Minimum packet: CMD(1) + LENGTH(2) + CRC(2) = 5 bytes
    if len(data) < 5:
        raise PacketError(f"Packet too short: {len(data)} bytes")

    cmd = data[0]
    length = struct.unpack_from("<H", data, 1)[0]

    expected_total = 1 + 2 + length + 2  # CMD + LENGTH + PAYLOAD + CRC
    if len(data) != expected_total:
        raise PacketError(
            f"Length mismatch: header says {length} payload bytes "
            f"({expected_total} total) but got {len(data)} bytes"
        )

    payload = data[3 : 3 + length]
    received_crc = struct.unpack_from("<H", data, 3 + length)[0]
    computed_crc = crc16_ccitt(data[: 3 + length])

    if received_crc != computed_crc:
        raise PacketError(
            f"CRC mismatch: received 0x{received_crc:04X}, "
            f"computed 0x{computed_crc:04X}"
        )

    try:
        resp_code = Resp(cmd)
    except ValueError:
        raise PacketError(f"Unknown response code: 0x{cmd:02X}")

    return Response(code=resp_code, payload=payload)


# ---------------------------------------------------------------------------
# Payload builders for specific commands
# ---------------------------------------------------------------------------

def cmd_clear() -> bytes:
    """Build a CLEAR packet."""
    return build_packet(Cmd.CLEAR)


def cmd_sprite(sprite_id: int, frame: int, x: int, y: int) -> bytes:
    """Build a SPRITE packet."""
    payload = struct.pack("<BBHH", sprite_id, frame, x, y)
    return build_packet(Cmd.SPRITE, payload)


def cmd_text(x: int, y: int, size: int, text: str) -> bytes:
    """Build a TEXT packet.  Text is null-terminated in the payload."""
    text_bytes = text.encode("utf-8") + b"\x00"
    payload = struct.pack("<HHB", x, y, size) + text_bytes
    return build_packet(Cmd.TEXT, payload)


def cmd_bitmap_full(data: bytes) -> bytes:
    """Build a BITMAP_FULL packet (48000 bytes for 800x480 1-bit)."""
    if len(data) != 48000:
        raise ValueError(f"Full bitmap must be 48000 bytes, got {len(data)}")
    return build_packet(Cmd.BITMAP_FULL, data)


def cmd_bitmap_region(
    x: int, y: int, w: int, h: int, data: bytes
) -> bytes:
    """Build a BITMAP_REGION packet."""
    header = struct.pack("<HHHH", x, y, w, h)
    return build_packet(Cmd.BITMAP_REGION, header + data)


def cmd_refresh(mode: RefreshMode = RefreshMode.PARTIAL) -> bytes:
    """Build a REFRESH packet."""
    return build_packet(Cmd.REFRESH, struct.pack("<B", mode))


def cmd_set_state(state_id: int) -> bytes:
    """Build a SET_STATE packet."""
    return build_packet(Cmd.SET_STATE, struct.pack("<B", state_id))


def cmd_ping() -> bytes:
    """Build a PING packet."""
    return build_packet(Cmd.PING)
