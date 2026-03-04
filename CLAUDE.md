# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run Commands

### Firmware (ESP32-C3)

```bash
# CRITICAL: Kill the daemon before flashing — it holds the serial port
claude-petd --stop  # or: launchctl bootout gui/$(id -u)/com.claude-pet.daemon

cd firmware/
pio run -e xteink_x4                    # Build
pio run -e xteink_x4 --target upload    # Flash

# Restart daemon after flashing
claude-petd --daemon
```

### Python Daemon

```bash
pip install -e .                  # Install package (editable)
claude-petd                       # Run foreground (debug)
claude-petd --verbose             # Run with debug logging
claude-petd --daemon              # Run background (double-fork)
claude-petd --stop                # Stop background daemon
claude-petd --status              # Check daemon + device status
claude-petd --install-hooks       # Install Claude Code hooks into ~/.claude/settings.json
```

### Hook Testing

```bash
./scripts/test-hooks.sh           # Fire all hook event types at running daemon
```

### No tests exist yet.

## Architecture

Three-layer system: **Claude Code hooks** → **Python daemon** → **ESP32 firmware on e-ink display**

### Data Flow

1. Claude Code fires hook events (SessionStart, PreToolUse, PostToolUse, Stop, etc.)
2. `scripts/on-event.sh` receives JSON on stdin, extracts fields, sends JSON to Unix socket via `socat`
3. Python daemon (`claude-petd`) receives events on `~/.claude-pet/state.sock`
4. Daemon maps hooks to states via `state_machine.py`, tracks per-session state
5. Daemon sends `SESSION_LIST` binary packets over USB serial (COBS-encoded, CRC-16 validated)
6. ESP32 firmware parses packets, renders split-pane UI on 800x480 e-ink display

### Python Daemon (`src/claude_pet/`)

- **daemon.py** — asyncio event loop running 5 concurrent tasks: serial connection, heartbeat, socket server, debounced session list sender (5s interval), session-index watcher (10s poll for name changes)
- **serial_conn.py** — auto-discovers device by USB VID `0x303A` / PID `0x1001`, COBS framing, ACK/retry, heartbeat PING/PONG
- **socket_server.py** — Unix domain socket server at `~/.claude-pet/state.sock`, newline-delimited JSON
- **protocol.py** — binary packet protocol: `CMD(1) + LENGTH(2 LE) + PAYLOAD(0..4096) + CRC16(2)`, builders for all commands
- **state_machine.py** — hook→State mapping with priority resolution for multi-session conflicts
- **sprites.py** — sprite ID constants, must match firmware's sprite table in `sprites.h`

### Firmware (`firmware/src/`)

- **main.cpp** — split-pane UI (session list left, mascot+detail right), COBS serial protocol handler, button input via ADC
- **sprites.h** — sprite bitmaps baked into flash, contains `AnimationDef` struct and all frame data

### State IDs (must match between Python and firmware)

IDLE=0x00, THINKING=0x01, CODING=0x02, RUNNING=0x03, WAITING=0x04, SUCCESS=0x05, ERROR=0x06, SLEEPING=0x07

## Hardware Gotchas

- **Board**: must be `esp32-c3-devkitm-1` (NOT devkitc-02 — causes boot loop)
- **Display init**: `display.init(0, true, ...)` — MUST pass `true` on first boot or partial updates silently fail on SSD1677
- **ADC buttons**: ESP32-C3 ADC defaults to ~750mV range. Must call `analogSetAttenuation(ADC_11db)` for the resistor ladder to work
- **Flash config**: must set ALL THREE: `board_upload.flash_size`, `maximum_size`, `offset_address` plus `flash_mode = dio`
- **Watchdog**: must disable with `-DCONFIG_ESP_TASK_WDT_INIT=0` (e-ink init is slow)
- **USB port**: `upload_port` in platformio.ini is hardcoded; port number changes per USB port. Auto-detect by VID/PID instead
- **Partial flash corruption**: if flash is interrupted, device needs unplug/replug to recover (USB CDC becomes unresponsive)

## Dependencies

- **socat** — required by hook bridge script (`brew install socat`)
- **PlatformIO** — firmware build system
- **GxEPD2** — e-ink display library (via PlatformIO lib_deps)
- **PacketSerial** — COBS framing on firmware side
- Python: `pyserial`, `cobs`, `crcmod`

## Runtime Files

- `~/.claude-pet/state.sock` — daemon Unix socket
- `~/.claude-pet/daemon.pid` — PID file
- `~/.claude-pet/hooks/on-event.sh` — installed hook script
- `~/.claude/settings.json` — Claude Code hooks configuration (not in this repo)
- `~/.claude/session-index.json` — read by daemon at startup and polled every 10s for session name changes
