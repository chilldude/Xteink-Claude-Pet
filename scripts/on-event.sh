#!/bin/bash
# Sends hook event to the claude-pet daemon via Unix socket.
# Called by Claude Code hooks — receives JSON on stdin.
# Arg $1 is the hook name (passed from the hook command).
# Fails silently if daemon is not running (hooks must not block Claude Code).

SOCKET="$HOME/.claude-pet/state.sock"
HOOK="$1"

# Read JSON from stdin (Claude Code hook input)
INPUT=$(cat)

# Extract fields and build socket message using Python (handles escaping)
python3 -c "
import sys, json

hook = sys.argv[1]
try:
    d = json.loads(sys.argv[2])
except:
    d = {}

detail = d.get('tool_name', '')
session = d.get('session_id', 'unknown') or 'unknown'

# Extract context-aware detail text
detail_text = ''
if hook == 'UserPromptSubmit':
    prompt = d.get('prompt', '')
    detail_text = prompt[:40] if prompt else ''
elif hook in ('PreToolUse', 'PostToolUse'):
    tool = d.get('tool_name', '')
    inp = d.get('tool_input', {}) if isinstance(d.get('tool_input'), dict) else {}
    if tool == 'Bash':
        cmd = inp.get('command', '')
        detail_text = ('$ ' + cmd)[:40] if cmd else tool
    elif tool in ('Edit', 'Write', 'Read'):
        fp = inp.get('file_path', '')
        detail_text = fp[-40:] if fp else tool
    else:
        detail_text = tool
elif hook == 'Stop':
    detail_text = 'Finished'

# Look up session name from Claude session files
import glob, os
session_name = session
for jsonl in glob.glob(os.path.expanduser(f'~/.claude/projects/*/{session}.jsonl')):
    title = slug = ''
    tokens = 0
    try:
        with open(jsonl) as f:
            for line in f:
                try:
                    obj = json.loads(line)
                    if obj.get('type') == 'custom-title':
                        title = obj.get('customTitle', '')
                    if 'slug' in obj:
                        slug = obj['slug']
                    if obj.get('type') == 'assistant' and isinstance(obj.get('message'), dict):
                        usage = obj['message'].get('usage', {})
                        tokens += usage.get('input_tokens', 0) + usage.get('output_tokens', 0)
                except:
                    pass
    except:
        pass
    session_name = title or slug or session
    break

msg = json.dumps({
    'hook': hook,
    'detail': detail,
    'session': session,
    'session_name': session_name,
    'detail_text': detail_text,
    'tokens': tokens,
})
print(msg)
" "$HOOK" "$INPUT" 2>/dev/null | \
    socat -t0.1 - UNIX-CONNECT:"$SOCKET" 2>/dev/null || true
