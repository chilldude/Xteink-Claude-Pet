# Xteink X4 Hardware Reference

Everything learned from detecting, programming, and flashing the Xteink X4.

## Device Detection

### USB Identification

The X4 uses the ESP32-C3's **native USB** (not an external UART chip like CH340/CP210x). It enumerates as:

| Field | Value |
|-------|-------|
| VID | `0x303A` (12346 decimal) — Espressif |
| PID | `0x1001` (4097 decimal) |
| Product Name | "USB JTAG/serial debug unit" |
| Serial port | `/dev/cu.usbmodem*` (macOS) |

### Detection Commands (macOS)

```bash
# List USB serial devices
ls /dev/cu.usbmodem*

# Full USB device info
ioreg -r -c IOUSBHostDevice -l | grep -E '"USB Product Name"|"idVendor"|"idProduct"'

# Python (with pyserial)
python3 -c "
import serial.tools.list_ports
for p in serial.tools.list_ports.comports():
    if p.vid == 0x303A and p.pid == 0x1001:
        print(f'Found X4 at {p.device}')
"
```

### Gotchas

- The port path (`/dev/cu.usbmodem1101` vs `1201` etc.) changes depending on which physical USB port is used. **Always detect by VID/PID**, never hardcode the path.
- The device may take 1-2 seconds to enumerate after plugging in. If detection fails, wait and retry.
- The X4 can go to deep sleep and disappear from USB. Pressing the power button or unplugging/replugging wakes it.
- PlatformIO's auto-detection may pick up Bluetooth serial ports (e.g. `/dev/cu.Maker4-8600`) instead of the actual USB device. Always specify `upload_port` explicitly.

## Pin Map

### SPI (Display)

| Signal | GPIO | Notes |
|--------|------|-------|
| SCLK | 8 | SPI clock |
| MOSI | 10 | Data to display |
| CS | 21 | Chip select |
| DC | 4 | Data/Command |
| RST | 5 | Hardware reset |
| BUSY | 6 | Display busy (high = busy) |

MISO is not connected — the e-ink display is write-only.

### Buttons

| Input | GPIO | Type |
|-------|------|------|
| ADC buttons | 1, 2 | Resistor ladder (analog) |
| Power button | 3 | Digital, active LOW, use `INPUT_PULLUP` |

## Display

### Panel: GDEQ0426T82

| Property | Value |
|----------|-------|
| Size | 4.26 inches diagonal |
| Resolution | 800 x 480 pixels |
| Color | 1-bit black & white (4-grayscale capable) |
| Controller | SSD1677 |
| PPI | 220 |
| Native orientation | Landscape (800 wide, 480 tall) |

### GxEPD2 Configuration

```cpp
#include <GxEPD2_BW.h>

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(/*CS=*/21, /*DC=*/4, /*RST=*/5, /*BUSY=*/6));
```

### Display Initialization

The ESP32-C3 does not have named SPI buses (no HSPI/VSPI). You must pass custom pins to `SPI.begin()` and then pass the SPI instance to `display.init()`:

```cpp
SPI.begin(/*SCLK=*/8, /*MISO=*/-1, /*MOSI=*/10, /*SS=*/21);
SPISettings spi_settings(4000000, MSBFIRST, SPI_MODE0);
display.init(115200, true, 2, false, SPI, spi_settings);
```

The `display.init()` parameters: `(baud, initial_refresh, reset_duration_ms, pulldown_rst, spi_bus, spi_settings)`. The last two are critical — without them GxEPD2 uses default SPI pins which are wrong for the X4.

### Rotation

- `setRotation(0)` = landscape, native orientation (800x480)
- `setRotation(1)` = portrait (480x800)
- `setRotation(2)` = landscape flipped
- `setRotation(3)` = portrait flipped

The X4's native panel orientation is already landscape. **Use `setRotation(0)`** for standard landscape with the display's native pixel layout.

### Refresh Strategy

- **Full refresh**: ~2-3 seconds. Clears ghosting. Use for boot, major state changes.
- **Partial refresh**: ~300-500ms for small regions. Use for animation, status updates.
- **Ghost clearing**: Do a full refresh every ~5-20 partial refreshes to prevent ghosting buildup.
- `display.hibernate()` after drawing to save power. Call `display.init()` again before next draw.

### Paged Drawing

GxEPD2 uses paged drawing to fit in RAM:

```cpp
display.setFullWindow();
display.firstPage();
do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(100, 100);
    display.print("Hello");
} while (display.nextPage());
```

Your drawing code runs multiple times (once per page). Draw everything inside the loop.

### Partial Window

For partial refresh of a sub-region:

```cpp
// x must be multiple of 8, width must be multiple of 8
display.setPartialWindow(x, y, width, height);
display.firstPage();
do {
    // draw only within the partial window
} while (display.nextPage());
```

## PlatformIO Configuration

### Board Selection

**Use `esp32-c3-devkitm-1`**, not `esp32-c3-devkitc-02`.

The X4 uses the ESP32-C3-MINI-1 module, which matches the DevKitM-1 board definition. Using the wrong board definition causes boot loops due to mismatched flash configuration.

### Critical Settings

```ini
[env:xteink_x4]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino

; 16MB flash — ALL THREE board_upload settings are required
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_upload.offset_address = 0x10000

; Build settings
board_build.flash_mode = dio        ; DIO required for 16MB flash
board_build.flash_size = 16MB
board_build.partitions = default_16MB.csv

build_flags =
    -DARDUINO_USB_MODE=1            ; Native USB mode
    -DARDUINO_USB_CDC_ON_BOOT=1     ; USB CDC serial on boot
    -DCONFIG_ESP_TASK_WDT_INIT=0    ; Disable watchdog (e-ink init is slow)
```

### What Goes Wrong Without These

| Missing Setting | Symptom |
|-----------------|---------|
| Wrong board (`devkitc-02`) | Boot loop (`rst:0x3 RTC_SW_SYS_RST`) |
| Missing `board_upload.*` | Bootloader/partition geometry mismatch, boot loop |
| Missing `flash_mode = dio` | Flash read errors on boot |
| Missing `WDT_INIT=0` | Watchdog reset during slow e-ink display init |
| Missing `USB_CDC_ON_BOOT` | No serial output over USB |

### Recovery from Boot Loop

If you flash bad firmware and the device boot-loops:

1. The device still enumerates on USB (the ROM bootloader is permanent)
2. Erase flash: `python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 erase_flash`
3. Reflash with corrected `platformio.ini`

The ESP32-C3's native USB bootloader lives in ROM and **cannot be bricked** by bad firmware. You can always recover.

## Memory Budget

| Item | Size |
|------|------|
| GxEPD2 framebuffer | 48,000 bytes (800x480 / 8) |
| Firmware + libs | ~62 KB RAM, ~264 KB flash |
| Available RAM | ~240 KB headroom |
| Available flash | ~6.3 MB headroom |

## Serial Communication

The USB CDC serial runs at USB full-speed (12 Mbps). The baud rate parameter (115200) is a legacy formality — actual throughput is determined by USB.

```cpp
Serial.begin(115200);  // USB CDC, baud is ignored
Serial.println("hello from X4");
```

Read from host (Python):
```python
import serial
ser = serial.Serial('/dev/cu.usbmodem1101', 115200, timeout=1)
data = ser.read(4096)
```
