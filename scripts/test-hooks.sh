#!/bin/bash
# Test script for Claude Code hooks.
# Sends each hook event to the daemon and shows expected pet state.
# Usage: ./scripts/test-hooks.sh
# Requires: daemon running and socket at ~/.claude-pet/state.sock

SOCKET="$HOME/.claude-pet/state.sock"
DELAY=1

# Colors for output
BLUE='\033[0;34m'
CYAN='\033[0;36m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

echo "Claude Pet Hook Test"
echo "===================="
echo ""
echo "Testing events with 1 second delay between each."
echo "Make sure the daemon is running."
echo ""

# Function to send event
send_event() {
    local hook_name="$1"
    local detail="${2:-}"

    echo -e "${CYAN}Sending: $hook_name${NC}"
    if [ -n "$detail" ]; then
        echo "  Detail: $detail"
    fi

    # Send event via socket
    if [ -S "$SOCKET" ]; then
        echo "{\"hook\":\"$hook_name\",\"detail\":\"$detail\"}" | \
            socat -t0.1 - UNIX-CONNECT:"$SOCKET" 2>/dev/null
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}✓ Event sent${NC}"
        else
            echo -e "${GREEN}✓ Event sent (no response from daemon)${NC}"
        fi
    else
        echo -e "${BLUE}(Socket not found - would send if daemon was running)${NC}"
    fi
    echo ""
    sleep "$DELAY"
}

# Send test events
echo -e "${BLUE}--- Pet should wake up and look happy ---${NC}"
echo ""
send_event "SessionStart"

echo -e "${BLUE}--- Pet receives user prompt ---${NC}"
echo ""
send_event "UserPromptSubmit" "Hello, pet!"

echo -e "${BLUE}--- Pet is working on tools ---${NC}"
echo ""
send_event "PreToolUse" "Bash"
sleep 0.5
send_event "PostToolUse" "Bash"

echo -e "${BLUE}--- Pet is using another tool ---${NC}"
echo ""
send_event "PreToolUse" "Edit"
sleep 0.5
send_event "PostToolUse" "Edit"

echo -e "${BLUE}--- Session ends and pet rests ---${NC}"
echo ""
send_event "Stop"

echo ""
echo "Test complete!"
echo "Check the daemon output to see if events were received."
