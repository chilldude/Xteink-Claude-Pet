"""Unix domain socket server for receiving Claude Code hook events.

Listens on ``~/.claude-pet/state.sock`` for newline-delimited JSON
messages.  Each message is expected to have at least a ``hook`` field
and optionally a ``detail`` field::

    {"hook": "PreToolUse", "detail": "Edit"}

The server is non-blocking, asyncio-native, and cleans up the socket
file on exit.
"""

from __future__ import annotations

import asyncio
import json
import logging
from pathlib import Path
from typing import Callable, Optional

log = logging.getLogger(__name__)

DEFAULT_SOCK_DIR = Path.home() / ".claude-pet"
DEFAULT_SOCK_PATH = DEFAULT_SOCK_DIR / "state.sock"


class SocketServer:
    """Asyncio-based Unix domain socket listener."""

    def __init__(
        self,
        sock_path: Path = DEFAULT_SOCK_PATH,
        on_event: Optional[Callable[[str, Optional[str], Optional[str], Optional[str], Optional[str], Optional[int]], None]] = None,
    ) -> None:
        self._sock_path = sock_path
        self.on_event = on_event  # callback(hook, detail)
        self._server: Optional[asyncio.AbstractServer] = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    async def listen(self) -> None:
        """Start listening and serve until cancelled."""
        self._sock_path.parent.mkdir(parents=True, exist_ok=True)

        # Remove stale socket file
        if self._sock_path.exists():
            self._sock_path.unlink()

        self._server = await asyncio.start_unix_server(
            self._handle_client, path=str(self._sock_path)
        )
        log.info("Listening on %s", self._sock_path)

        try:
            await self._server.serve_forever()
        except asyncio.CancelledError:
            pass
        finally:
            await self.close()

    async def close(self) -> None:
        """Stop the server and remove the socket file."""
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
            self._server = None

        try:
            self._sock_path.unlink(missing_ok=True)
        except OSError:
            pass
        log.debug("Socket server closed")

    # ------------------------------------------------------------------
    # Client handling
    # ------------------------------------------------------------------

    async def _handle_client(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ) -> None:
        """Process newline-delimited JSON from a single client."""
        peer = "unix-client"
        log.debug("Client connected: %s", peer)

        try:
            while True:
                line = await reader.readline()
                if not line:
                    break  # EOF

                line_str = line.decode("utf-8", errors="replace").strip()
                if not line_str:
                    continue

                try:
                    msg = json.loads(line_str)
                except json.JSONDecodeError as exc:
                    log.debug("Invalid JSON from %s: %s", peer, exc)
                    continue

                hook = msg.get("hook")
                if not hook:
                    log.debug("Message missing 'hook' field: %s", line_str)
                    continue

                detail = msg.get("detail")
                session = msg.get("session")
                session_name = msg.get("session_name")
                detail_text = msg.get("detail_text")
                tokens = msg.get("tokens")
                if isinstance(tokens, int):
                    pass
                else:
                    tokens = None
                log.debug("Event: hook=%s detail=%s session=%s name=%s detail_text=%s tokens=%s",
                          hook, detail, session, session_name, detail_text, tokens)

                if self.on_event is not None:
                    try:
                        self.on_event(hook, detail, session, session_name, detail_text, tokens)
                    except Exception:
                        log.exception("Error in on_event callback")

                # Acknowledge receipt
                try:
                    writer.write(b'{"ok":true}\n')
                    await writer.drain()
                except (ConnectionError, OSError):
                    break

        except (asyncio.CancelledError, ConnectionError):
            pass
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass
            log.debug("Client disconnected: %s", peer)
