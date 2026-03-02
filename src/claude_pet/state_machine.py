"""Map Claude Code hook events to pet display states.

State IDs match the firmware's state enum so SET_STATE can send them
directly.
"""

from __future__ import annotations

import re
from enum import IntEnum
from typing import Optional


class State(IntEnum):
    IDLE = 0x00
    THINKING = 0x01
    CODING = 0x02
    RUNNING = 0x03
    WAITING = 0x04
    SUCCESS = 0x05
    ERROR = 0x06
    SLEEPING = 0x07


# Each key is (hook_name, detail_pattern | None).
# detail_pattern is matched with re.fullmatch if not None.
# Entries are checked top-to-bottom; first match wins.

_STATE_MAP: list[tuple[str, Optional[str], State]] = [
    ("SessionStart",     None,          State.IDLE),
    ("UserPromptSubmit", None,          State.THINKING),
    ("PreToolUse",       r"Edit|Write", State.CODING),
    ("PreToolUse",       r"Bash",       State.RUNNING),
    ("PreToolUse",       r"Task",       State.THINKING),
    ("PreToolUse",       None,          State.THINKING),  # fallback for other tools
    ("PostToolUse",      None,          State.IDLE),
    ("Stop",             None,          State.SLEEPING),
    ("Notification",     None,          State.WAITING),
]


def resolve_state(hook: str, detail: Optional[str] = None) -> Optional[State]:
    """Return the pet State for a given hook event, or None if unmapped."""
    for map_hook, map_pattern, state in _STATE_MAP:
        if hook != map_hook:
            continue
        if map_pattern is None:
            # Matches any detail (including None)
            return state
        if detail is not None and re.fullmatch(map_pattern, detail):
            return state
    return None


# Human-readable labels shown on the e-ink display alongside the sprite.
STATE_LABELS: dict[State, str] = {
    State.IDLE:     "idle",
    State.THINKING: "thinking...",
    State.CODING:   "coding",
    State.RUNNING:  "running",
    State.WAITING:  "waiting for you",
    State.ERROR:    "error",
    State.SUCCESS:  "done!",
    State.SLEEPING: "zzz",
}
