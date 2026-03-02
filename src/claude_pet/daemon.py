"""Main daemon orchestrating serial, socket, and state management.

Runs an asyncio event loop that gathers:
  - serial_conn.maintain_connection()  -- auto-discover & stay connected
  - serial_conn.heartbeat_loop()       -- keep-alive PINGs
  - socket_server.listen()             -- receive hook events

When a hook event arrives via the socket, it is mapped to a pet state
and sent to the device as a SESSION_LIST command showing all active
sessions with their state icons and detail text.
"""

from __future__ import annotations

import asyncio
import logging
import signal
import sys
import time
from typing import Optional

from .protocol import Resp, Response, cmd_session_list
from .serial_conn import SerialConnection
from .socket_server import SocketServer
from .state_machine import State, resolve_state, STATE_LABELS, STATE_PRIORITY

log = logging.getLogger(__name__)


class Daemon:
    """The main claude-pet daemon."""

    # Seconds before a session is considered stale and ignored
    SESSION_STALE_TIMEOUT = 120.0

    # Minimum seconds between session list sends (e-ink refresh takes 2-3s)
    SESSION_LIST_INTERVAL = 5.0

    def __init__(self) -> None:
        self._serial = SerialConnection()
        self._socket = SocketServer(on_event=self._on_hook_event)
        self._display_state: State = State.IDLE  # what's currently shown on device
        self._running = False

        # Per-session state tracking: session_id -> (State, last_seen_timestamp)
        self._sessions: dict[str, tuple[State, float]] = {}

        # Session display names: session_id -> slug/name
        self._session_names: dict[str, str] = {}

        # Per-session detail text: session_id -> detail string
        self._session_details: dict[str, str] = {}

        # Dirty flag for debounced session list sends
        self._session_list_dirty = False

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

        # Initialize async primitives inside the running loop (Python 3.9 compat)
        self._serial._ensure_async_primitives()

        # Wait for connection, then push initial state
        asyncio.create_task(self._resync_on_connect())

        try:
            await asyncio.gather(
                self._serial.maintain_connection(),
                self._serial.heartbeat_loop(),
                self._socket.listen(),
                self._session_list_loop(),
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

    def _on_hook_event(
        self,
        hook: str,
        detail: Optional[str],
        session: Optional[str] = None,
        session_name: Optional[str] = None,
        detail_text: Optional[str] = None,
    ) -> None:
        """Map a hook event to a state transition and push to device.

        Tracks per-session states.  The highest-priority state across all
        active sessions is what gets displayed on the pet.
        """
        session_id = session or "default"
        new_state = resolve_state(hook, detail)
        if new_state is None:
            log.debug("No state mapping for hook=%s detail=%s", hook, detail)
            return

        # Update this session's state, display name, and detail text
        self._sessions[session_id] = (new_state, time.monotonic())
        if session_name:
            self._session_names[session_id] = session_name
        if detail_text:
            self._session_details[session_id] = detail_text

        # Remove sessions that ended (Stop hook)
        if hook == "Stop":
            self._sessions.pop(session_id, None)
            self._session_names.pop(session_id, None)
            self._session_details.pop(session_id, None)

        # Resolve highest-priority state across all active sessions
        winning_state = self._resolve_priority()

        old = self._display_state
        self._display_state = winning_state
        log.info(
            "Display: %s -> %s (session=%s, hook=%s, %d active sessions)",
            old.name, winning_state.name, session_id, hook, len(self._sessions),
        )

        # Mark session list dirty — periodic loop will send it
        self._session_list_dirty = True

    def _resolve_priority(self) -> State:
        """Return the highest-priority state across all non-stale sessions."""
        now = time.monotonic()

        # Prune stale sessions
        stale = [
            sid for sid, (_, ts) in self._sessions.items()
            if now - ts > self.SESSION_STALE_TIMEOUT
        ]
        for sid in stale:
            log.debug("Pruning stale session: %s", sid)
            del self._sessions[sid]
            self._session_details.pop(sid, None)

        if not self._sessions:
            return State.IDLE

        # Pick the state with the highest priority
        return max(
            (state for state, _ in self._sessions.values()),
            key=lambda s: STATE_PRIORITY.get(s, 0),
        )

    async def _session_list_loop(self) -> None:
        """Periodically send the session list if dirty."""
        while self._running:
            if self._session_list_dirty and self._serial.connected:
                self._session_list_dirty = False
                await self._push_session_list()
            await asyncio.sleep(self.SESSION_LIST_INTERVAL)

    async def _push_session_list(self) -> None:
        """Send the full session list to the device."""
        if not self._serial.connected:
            return

        # Prune stale sessions first
        self._resolve_priority()

        # Build list of (state_id, display_name, detail_text) tuples
        entries: list[tuple[int, str, str]] = []
        for sid, (state, _ts) in self._sessions.items():
            name = self._session_names.get(sid, sid[:25])
            detail = self._session_details.get(sid, "")
            entries.append((state.value, name, detail))

        # Compute selected_idx: highest-priority session
        selected_idx = 0
        if entries:
            best_priority = -1
            for i, (state_val, _, _) in enumerate(entries):
                try:
                    st = State(state_val)
                    p = STATE_PRIORITY.get(st, 0)
                except ValueError:
                    p = 0
                if p > best_priority:
                    best_priority = p
                    selected_idx = i

        log.info("Sending session list: %d entries, selected=%d", len(entries), selected_idx)
        await self._serial.send(cmd_session_list(entries, selected_idx))

    # ------------------------------------------------------------------
    # Reconnect sync
    # ------------------------------------------------------------------

    async def _resync_on_connect(self) -> None:
        """Whenever the device (re)connects, push the current state."""
        while self._running:
            # Wait for connection
            await self._serial._connected.wait()
            log.info("Device connected, syncing state: %s", self._display_state.name)
            self._session_list_dirty = True  # will be sent by _session_list_loop
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
