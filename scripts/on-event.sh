#!/bin/bash
# Sends hook event to the claude-pet daemon via Unix socket.
# Usage: on-event.sh <hook_name> [detail]
# Fails silently if daemon is not running (hooks must not block Claude Code).

SOCKET="$HOME/.claude-pet/state.sock"
HOOK="$1"
DETAIL="${2:-}"

# Non-blocking write to Unix socket. Timeout 100ms.
echo "{\"hook\":\"$HOOK\",\"detail\":\"$DETAIL\"}" | \
    socat -t0.1 - UNIX-CONNECT:"$SOCKET" 2>/dev/null || true
