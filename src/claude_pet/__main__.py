"""CLI entry point for the claude-pet daemon.

Usage:
    python -m claude_pet             # run in foreground
    python -m claude_pet --daemon    # run in background
    python -m claude_pet --stop      # stop background daemon
    python -m claude_pet --status    # check if daemon + device are up
    python -m claude_pet --install-hooks  # wire into Claude Code settings
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import signal
import sys
from pathlib import Path

from . import __version__

RUN_DIR = Path.home() / ".claude-pet"
PID_FILE = RUN_DIR / "daemon.pid"
SOCK_PATH = RUN_DIR / "state.sock"

CLAUDE_SETTINGS = Path.home() / ".claude" / "settings.json"

# The hook script that Claude Code will execute.  It sends a JSON line
# to the daemon's Unix socket.
HOOK_SCRIPT = r"""#!/usr/bin/env python3
\"\"\"Claude Code hook -> claude-pet daemon bridge.\"\"\"
import json, os, socket, sys

SOCK = os.path.expanduser("~/.claude-pet/state.sock")

def main():
    hook = os.environ.get("CLAUDE_HOOK", "")
    detail = os.environ.get("CLAUDE_HOOK_DETAIL", "")
    msg = json.dumps({"hook": hook, "detail": detail or None}) + "\n"

    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(0.5)
        s.connect(SOCK)
        s.sendall(msg.encode())
        s.recv(256)  # wait for ack
        s.close()
    except Exception:
        pass  # daemon not running — fail silently, never block Claude

if __name__ == "__main__":
    main()
"""


def setup_logging(verbose: bool = False) -> None:
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        stream=sys.stderr,
    )


# ------------------------------------------------------------------
# Daemon management
# ------------------------------------------------------------------

def _write_pid() -> None:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    PID_FILE.write_text(str(os.getpid()))


def _read_pid() -> int | None:
    try:
        return int(PID_FILE.read_text().strip())
    except (FileNotFoundError, ValueError):
        return None


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def cmd_stop() -> int:
    """Stop a running background daemon."""
    pid = _read_pid()
    if pid is None:
        print("No PID file found — daemon not running?", file=sys.stderr)
        return 1
    if not _pid_alive(pid):
        print(f"PID {pid} is not running. Cleaning up.", file=sys.stderr)
        PID_FILE.unlink(missing_ok=True)
        return 0
    os.kill(pid, signal.SIGTERM)
    print(f"Sent SIGTERM to PID {pid}", file=sys.stderr)
    PID_FILE.unlink(missing_ok=True)
    return 0


def cmd_status() -> int:
    """Print daemon and device status."""
    pid = _read_pid()
    if pid and _pid_alive(pid):
        print(f"Daemon running (PID {pid})")
    else:
        print("Daemon not running")

    if SOCK_PATH.exists():
        print(f"Socket exists: {SOCK_PATH}")
    else:
        print("Socket not found")

    # Quick device scan
    try:
        from .serial_conn import find_x4_port

        port = find_x4_port()
        if port:
            print(f"X4 device found at {port}")
        else:
            print("X4 device not found")
    except Exception as exc:
        print(f"Device scan failed: {exc}")

    return 0


def cmd_daemonize() -> None:
    """Fork into the background and run the daemon."""
    # Double-fork to detach from terminal
    pid = os.fork()
    if pid > 0:
        # Parent exits
        print(f"Daemon started (PID {pid})", file=sys.stderr)
        sys.exit(0)

    # Child: new session
    os.setsid()

    # Second fork
    pid2 = os.fork()
    if pid2 > 0:
        sys.exit(0)

    # Grandchild: redirect stdio
    sys.stdin.close()
    devnull = open(os.devnull, "w")
    sys.stdout = devnull
    sys.stderr = devnull

    _write_pid()

    from .daemon import Daemon

    Daemon().run()


def cmd_foreground() -> None:
    """Run the daemon in the foreground."""
    _write_pid()
    try:
        from .daemon import Daemon

        Daemon().run()
    finally:
        PID_FILE.unlink(missing_ok=True)


# ------------------------------------------------------------------
# Hook installation
# ------------------------------------------------------------------

def cmd_install_hooks() -> int:
    """Merge claude-pet hooks into ~/.claude/settings.json."""
    hook_dir = RUN_DIR / "hooks"
    hook_dir.mkdir(parents=True, exist_ok=True)

    # Write the hook script
    hook_script_path = hook_dir / "notify.py"
    hook_script_path.write_text(HOOK_SCRIPT)
    hook_script_path.chmod(0o755)

    # Build the hook entries Claude Code expects
    hook_command = f"python3 {hook_script_path}"
    hooks_config = {
        "hooks": {
            "PreToolUse": [
                {
                    "type": "command",
                    "command": hook_command,
                }
            ],
            "PostToolUse": [
                {
                    "type": "command",
                    "command": hook_command,
                }
            ],
            "Notification": [
                {
                    "type": "command",
                    "command": hook_command,
                }
            ],
            "Stop": [
                {
                    "type": "command",
                    "command": hook_command,
                }
            ],
        }
    }

    # Merge into existing settings
    CLAUDE_SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    if CLAUDE_SETTINGS.exists():
        try:
            settings = json.loads(CLAUDE_SETTINGS.read_text())
        except (json.JSONDecodeError, OSError):
            settings = {}
    else:
        settings = {}

    existing_hooks = settings.get("hooks", {})
    for hook_name, hook_list in hooks_config["hooks"].items():
        if hook_name not in existing_hooks:
            existing_hooks[hook_name] = []
        # Avoid duplicates: check if our command is already there
        existing_cmds = {
            h.get("command") for h in existing_hooks[hook_name] if isinstance(h, dict)
        }
        for hook_entry in hook_list:
            if hook_entry["command"] not in existing_cmds:
                existing_hooks[hook_name].append(hook_entry)

    settings["hooks"] = existing_hooks
    CLAUDE_SETTINGS.write_text(json.dumps(settings, indent=2) + "\n")

    print(f"Hook script written to {hook_script_path}")
    print(f"Claude Code settings updated: {CLAUDE_SETTINGS}")
    return 0


# ------------------------------------------------------------------
# Argument parsing
# ------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        prog="claude-petd",
        description="Claude Pet e-ink companion daemon",
    )
    parser.add_argument(
        "--version", action="version", version=f"%(prog)s {__version__}"
    )
    parser.add_argument(
        "--daemon", "-d", action="store_true", help="Run in the background"
    )
    parser.add_argument(
        "--stop", action="store_true", help="Stop the background daemon"
    )
    parser.add_argument(
        "--status", action="store_true", help="Show daemon and device status"
    )
    parser.add_argument(
        "--install-hooks",
        action="store_true",
        help="Install Claude Code hook integration",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="Enable debug logging"
    )

    args = parser.parse_args()
    setup_logging(args.verbose)

    if args.stop:
        sys.exit(cmd_stop())
    elif args.status:
        sys.exit(cmd_status())
    elif args.install_hooks:
        sys.exit(cmd_install_hooks())
    elif args.daemon:
        cmd_daemonize()
    else:
        cmd_foreground()


if __name__ == "__main__":
    main()
