# Operations & Troubleshooting

Day-to-day operations, deployment, and troubleshooting for the Claude Pet system.

## Deployment

### First-Time Setup

```bash
# 1. Install Python daemon
pip3 install --user .

# 2. Install Claude Code hooks into ~/.claude/settings.json
claude-petd --install-hooks

# 3. Flash firmware (see Firmware Flashing below)

# 4. Start daemon
claude-petd --daemon
```

### Python Daemon

The daemon is a pure-Python package. After any code change to `src/claude_pet/`:

```bash
# Reinstall (required after code changes)
pip3 install --user --force-reinstall --no-deps .

# Restart
claude-petd --stop
claude-petd --daemon
```

Use `--verbose` for debug logging when troubleshooting:

```bash
claude-petd --stop
claude-petd --verbose   # foreground, full logs to terminal
```

### Firmware Flashing

**The daemon holds the serial port.** You must stop it before flashing.

```bash
# 1. Stop daemon
claude-petd --stop

# 2. Build and flash
cd firmware/
pio run -e xteink_x4 --target upload

# 3. Restart daemon
claude-petd --daemon
```

If the upload port isn't found, check which port the device is on:

```bash
ls /dev/cu.usbmodem*
```

Then pass it explicitly:

```bash
pio run -e xteink_x4 --target upload --upload-port /dev/cu.usbmodem1101
```

### Verifying Everything Works

```bash
# Check daemon and device status
claude-petd --status

# Fire test hook events at the running daemon
./scripts/test-hooks.sh

# Watch daemon logs in real time (run in foreground)
claude-petd --stop && claude-petd --verbose
```

## Troubleshooting

### Daemon Won't Start

**"Address already in use" on socket:**
```bash
# Old socket file left behind from a crash
rm ~/.claude-pet/state.sock
claude-petd --daemon
```

**"No PID file found" but daemon seems running:**
```bash
# Find and kill orphan processes
pgrep -f claude-petd
kill <pid>
claude-petd --daemon
```

### Device Not Detected

**Symptoms:** `claude-petd --status` says no device, logs show no serial connection.

1. Check the device is plugged in and powered on (hold power button on GPIO 3)
2. Verify it enumerates on USB:
   ```bash
   ls /dev/cu.usbmodem*
   ```
3. If no port appears, unplug and replug the USB cable
4. If the device was flashed with bad firmware, it may be boot-looping but the USB port should still appear (ROM bootloader is unbrickable)

**Port appears but daemon can't connect:**
- Another process may hold the port (e.g. PlatformIO serial monitor, `screen`, another daemon instance)
- Kill the other process or close the serial monitor

### Display Not Updating

**Display is blank / shows old content:**
1. Check daemon is running: `claude-petd --status`
2. Check device is connected: look for "Device connected" in daemon logs
3. Send a test event: `./scripts/test-hooks.sh`
4. If the display shows stale content after a firmware flash, unplug and replug — the display needs a full refresh on boot

**Partial updates not working (display shows nothing after init):**
- The first `display.init()` call MUST pass `true` for `initial_refresh`. If firmware was changed to pass `false`, partial updates silently fail on the SSD1677 controller.

**Session names not updating after rename:**
- The daemon polls `~/.claude/session-index.json` every 10 seconds
- Max delay from rename to display: ~15 seconds (10s poll + 5s send interval)
- Check logs for "Session renamed:" messages

### Boot Loop After Firmware Flash

**Symptoms:** Device repeatedly resets, serial output shows `rst:0x3 (RTC_SW_SYS_RST)`.

**Common causes:**
| Cause | Fix |
|-------|-----|
| Wrong board (`devkitc-02`) | Change to `esp32-c3-devkitm-1` in `platformio.ini` |
| Missing `board_upload.*` settings | Add all three: `flash_size`, `maximum_size`, `offset_address` |
| Missing `flash_mode = dio` | Add `board_build.flash_mode = dio` |
| Watchdog timeout during e-ink init | Add `-DCONFIG_ESP_TASK_WDT_INIT=0` to `build_flags` |

**Recovery:**
```bash
# 1. Erase flash completely
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \
    --chip esp32c3 \
    --port /dev/cu.usbmodem1101 \
    erase_flash

# 2. Reflash with corrected platformio.ini
cd firmware/
pio run -e xteink_x4 --target upload
```

The ESP32-C3 ROM bootloader **cannot be bricked** — you can always recover.

### Flash Interrupted / USB Unresponsive

If a flash is interrupted mid-write, the USB CDC interface may become unresponsive.

**Fix:** Unplug the USB cable, wait 2 seconds, replug. The ROM bootloader will re-enumerate the port. Then erase and reflash.

### Hooks Not Firing

**Symptoms:** Daemon is running but no events arrive when using Claude Code.

1. Verify hooks are installed:
   ```bash
   cat ~/.claude/settings.json | grep claude-pet
   ```
2. If not present, reinstall:
   ```bash
   claude-petd --install-hooks
   ```
3. Test the hook script directly:
   ```bash
   echo '{"session_id":"test","hook":"SessionStart"}' | ~/.claude-pet/hooks/on-event.sh
   ```
4. Check that `socat` is installed:
   ```bash
   which socat || brew install socat
   ```

### ADC Buttons Not Responding

- ESP32-C3 ADC defaults to ~750mV range. Firmware must call `analogSetAttenuation(ADC_11db)` for the resistor ladder to work.
- ADC values vary by unit. Expected thresholds (GPIO 1):
  - Back: ~3534, Go: ~2702, Up: ~1500, Down: ~5
- Use serial output to debug raw ADC readings if buttons seem miscalibrated.

### Protocol / Communication Errors

**"CRC mismatch" in daemon logs:**
- Packet corruption on serial. Usually transient — the retry mechanism handles it.
- If persistent, check USB cable quality (use a data cable, not charge-only).

**"No ACK" after sending:**
- Device may be busy with a display refresh (2-3s for full refresh).
- The daemon retries automatically. Only a problem if it persists across multiple attempts.

**After changing protocol code:**
- Both sides must agree on the packet format. If you changed `protocol.py` or `main.cpp`:
  ```bash
  # Reinstall Python side
  pip3 install --user --force-reinstall --no-deps .
  claude-petd --stop && claude-petd --daemon

  # Reflash firmware side
  claude-petd --stop
  cd firmware/ && pio run -e xteink_x4 --target upload
  claude-petd --daemon
  ```
