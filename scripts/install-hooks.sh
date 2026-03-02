#!/bin/bash
# Interactive installation script for Claude Code hooks.
# Checks for socat, creates hook directory, copies on-event.sh,
# and merges hook configuration into ~/.claude/settings.json

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

HOOKS_DIR="$HOME/.claude-pet/hooks"
SETTINGS_FILE="$HOME/.claude/settings.json"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Claude Code Hooks Installation"
echo "==============================="
echo ""

# Check if socat is installed
if ! command -v socat &> /dev/null; then
    echo -e "${YELLOW}socat is not installed.${NC}"
    echo "socat is required to send events to the claude-pet daemon."
    echo ""
    echo "Install socat? (y/n)"
    read -r install_socat

    if [[ "$install_socat" =~ ^[Yy]$ ]]; then
        echo "Installing socat..."
        if command -v brew &> /dev/null; then
            brew install socat
            echo -e "${GREEN}socat installed successfully${NC}"
        else
            echo -e "${RED}Error: Homebrew not found. Please install socat manually.${NC}"
            exit 1
        fi
    else
        echo -e "${RED}socat is required. Aborting installation.${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✓ socat is installed${NC}"
fi

# Create hooks directory
echo "Creating hooks directory..."
mkdir -p "$HOOKS_DIR"
echo -e "${GREEN}✓ Created $HOOKS_DIR${NC}"

# Copy on-event.sh
echo "Installing on-event.sh..."
cp "$SCRIPT_DIR/on-event.sh" "$HOOKS_DIR/on-event.sh"
chmod +x "$HOOKS_DIR/on-event.sh"
echo -e "${GREEN}✓ Installed $HOOKS_DIR/on-event.sh (executable)${NC}"

# Hook configuration
HOOKS_CONFIG='{
  "hooks": {
    "SessionStart": [{"type": "command", "command": "~/.claude-pet/hooks/on-event.sh SessionStart"}],
    "UserPromptSubmit": [{"type": "command", "command": "~/.claude-pet/hooks/on-event.sh UserPromptSubmit \"$PROMPT\""}],
    "PreToolUse": [{"matcher": "Edit|Write|Bash|Task|Read|Grep|Glob", "type": "command", "command": "~/.claude-pet/hooks/on-event.sh PreToolUse \"$TOOL_NAME\""}],
    "PostToolUse": [{"type": "command", "command": "~/.claude-pet/hooks/on-event.sh PostToolUse \"$TOOL_NAME\""}],
    "Stop": [{"type": "command", "command": "~/.claude-pet/hooks/on-event.sh Stop"}],
    "Notification": [{"type": "command", "command": "~/.claude-pet/hooks/on-event.sh Notification"}]
  }
}'

# Handle settings.json
echo "Configuring Claude Code settings..."

if [ -f "$SETTINGS_FILE" ]; then
    echo "Merging hooks into existing settings.json..."

    # Use Python to merge JSON
    python3 << PYTHON_SCRIPT
import json
import sys

settings_file = "$SETTINGS_FILE"
hooks_config = $HOOKS_CONFIG

# Read existing settings
with open(settings_file, 'r') as f:
    settings = json.load(f)

# Initialize hooks key if it doesn't exist
if 'hooks' not in settings:
    settings['hooks'] = {}

# Merge hook configuration - append to arrays instead of replacing
for hook_name, hook_list in hooks_config['hooks'].items():
    if hook_name not in settings['hooks']:
        settings['hooks'][hook_name] = hook_list
    else:
        # Append new hooks to existing array
        settings['hooks'][hook_name].extend(hook_list)

# Write back settings
with open(settings_file, 'w') as f:
    json.dump(settings, f, indent=2)

print("✓ Hooks merged into " + settings_file)
PYTHON_SCRIPT

else
    echo "Creating new settings.json with hooks configuration..."

    mkdir -p "$(dirname "$SETTINGS_FILE")"
    python3 << PYTHON_SCRIPT
import json
import sys

settings_file = "$SETTINGS_FILE"
hooks_config = $HOOKS_CONFIG

with open(settings_file, 'w') as f:
    json.dump(hooks_config, f, indent=2)

print("✓ Created " + settings_file)
PYTHON_SCRIPT
fi

echo ""
echo -e "${GREEN}Installation complete!${NC}"
echo ""
echo "Next steps:"
echo "1. Make sure the claude-pet daemon is running"
echo "2. Test the hooks with: ./scripts/test-hooks.sh"
echo ""
echo "Hook configuration added to: $SETTINGS_FILE"
