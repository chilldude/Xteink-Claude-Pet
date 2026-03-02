# Claude Pet

An animated pixel-art companion that lives on an Xteink X4 e-ink display, reflecting Claude Code's real-time activity state.

## Hardware

**Xteink X4** — a 4.26" e-ink device with an ESP32-C3 microcontroller.

| Spec | Value |
|------|-------|
| MCU | ESP32-C3 (RISC-V, 160 MHz) |
| Display | GDEQ0426T82, 800x480, 1-bit B&W (SSD1677 controller) |
| Resolution | 220 PPI |
| Flash | 16 MB SPI |
| RAM | ~328 KB |
| USB | Native CDC (VID `0x303A`, PID `0x1001`) |
| Battery | 650 mAh |

## Quick Start

### Prerequisites

```bash
pip install platformio
```

### Detect the device

Plug the X4 in via USB-C. It enumerates as an Espressif USB JTAG/serial device:

```bash
# Check it's connected
ls /dev/cu.usbmodem*
# Should show e.g. /dev/cu.usbmodem1101

# Verify VID/PID
ioreg -r -c IOUSBHostDevice -l | grep -E '"USB Product Name"|"idVendor"|"idProduct"'
# idVendor = 12346 (0x303A = Espressif)
# idProduct = 4097 (0x1001)
```

The port number (`1101`, `1201`, etc.) changes depending on which USB port you use. Detection should always be done by VID/PID, not by hardcoded port path.

### Build and flash

```bash
cd firmware/

# Build
pio run -e xteink_x4

# Flash (device must be connected)
pio run -e xteink_x4 --target upload

# Serial monitor (for debug output)
pio device monitor --port /dev/cu.usbmodem1101 --baud 115200
```

### If flashing fails

If the device doesn't respond to `esptool`:

1. **Enter bootloader mode**: hold power button, plug in USB, release after 2 seconds
2. **Erase flash first** (clears bad partition tables):
   ```bash
   python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \
     --chip esp32c3 --port /dev/cu.usbmodem1101 erase_flash
   ```
3. Then retry `pio run --target upload`

## Current State

Phase 1 hello world + acid test. The firmware draws a test pattern on boot (border, grid, diagonals, bouncing ball animation) and the power button toggles sleep mode.

## Project Structure

```
firmware/
├── platformio.ini       # PlatformIO config for Xteink X4
└── src/
    └── main.cpp         # Firmware source
```
