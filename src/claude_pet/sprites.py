"""Sprite ID constants and metadata matching the firmware sprite sheet.

These IDs correspond to the sprite table baked into the X4 firmware's
flash.  Frame counts and durations are used by the daemon to drive
animation over the serial protocol (SPRITE command 0x02).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from .state_machine import State


@dataclass(frozen=True)
class SpriteInfo:
    """Metadata for a single sprite strip."""

    id: int
    name: str
    frames: int
    frame_ms: int  # milliseconds per frame


# Sprite table — IDs must match firmware
IDLE_BLINK = SpriteInfo(id=0, name="idle_blink", frames=6, frame_ms=500)
THINKING = SpriteInfo(id=1, name="thinking", frames=4, frame_ms=400)
CODING = SpriteInfo(id=2, name="coding", frames=4, frame_ms=300)
RUNNING = SpriteInfo(id=3, name="running", frames=4, frame_ms=250)
WAITING = SpriteInfo(id=4, name="waiting", frames=4, frame_ms=600)
SUCCESS = SpriteInfo(id=5, name="success", frames=6, frame_ms=300)
ERROR = SpriteInfo(id=6, name="error", frames=4, frame_ms=400)
SLEEPING = SpriteInfo(id=7, name="sleeping", frames=4, frame_ms=800)

ALL_SPRITES: list[SpriteInfo] = [
    IDLE_BLINK,
    THINKING,
    CODING,
    RUNNING,
    WAITING,
    ERROR,
    SUCCESS,
    SLEEPING,
]

# Map pet states to their default sprite
STATE_SPRITE: dict[State, SpriteInfo] = {
    State.IDLE:     IDLE_BLINK,
    State.THINKING: THINKING,
    State.CODING:   CODING,
    State.RUNNING:  RUNNING,
    State.WAITING:  WAITING,
    State.ERROR:    ERROR,
    State.SUCCESS:  SUCCESS,
    State.SLEEPING: SLEEPING,
}
