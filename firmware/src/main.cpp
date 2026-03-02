// ============================================================================
// main.cpp — Claude Pet Firmware
// Xteink X4 (ESP32-C3, GDEQ0426T82 800x480 e-ink)
// Animated pixel-art companion with serial command protocol
// ============================================================================

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <SPI.h>
#include <PacketSerial.h>
#include "sprites.h"

// ============================================================================
// Hardware Pin Definitions
// ============================================================================
#define EPD_SCLK  8
#define EPD_MOSI  10
#define EPD_CS    21
#define EPD_DC    4
#define EPD_RST   5
#define EPD_BUSY  6

#define BTN_POWER 3
#define BTN_ADC1  1
#define BTN_ADC2  2

// ============================================================================
// Display Configuration
// ============================================================================
#define DISP_W    800
#define DISP_H    480

// Sprite display parameters
#define SPRITE_X       100   // Sprite position on screen
#define SPRITE_Y       120
#define SPRITE_SCALE   2     // 64x64 drawn at 128x128
#define SPRITE_DRAW_W  (SPRITE_W * SPRITE_SCALE)
#define SPRITE_DRAW_H  (SPRITE_H * SPRITE_SCALE)

// Text layout positions
#define STATUS_TEXT_X  280
#define STATUS_TEXT_Y  160
#define DETAIL_TEXT_X  280
#define DETAIL_TEXT_Y  220

// ============================================================================
// Serial Protocol Constants
// ============================================================================
// Host -> Device commands
#define CMD_CLEAR        0x01
#define CMD_SPRITE       0x02
#define CMD_TEXT         0x03
#define CMD_BITMAP_FULL  0x04
#define CMD_BITMAP_REGION 0x05
#define CMD_REFRESH      0x06
#define CMD_SET_STATE    0x07
#define CMD_PING         0x08

// Device -> Host responses
#define RSP_ACK          0x81
#define RSP_BUTTON       0x82
#define RSP_PONG         0x83
#define RSP_BATTERY      0x84
#define RSP_ERROR        0x85

// Error codes
#define ERR_UNKNOWN_CMD  0x01
#define ERR_BAD_CRC      0x02
#define ERR_BAD_LENGTH   0x03
#define ERR_BAD_PARAM    0x04

// Timeouts
#define SERIAL_TIMEOUT_MS    60000  // 60s no serial -> auto IDLE
#define DEBOUNCE_MS          50
#define POWER_HOLD_SLEEP_MS  2000   // Hold power 2s for sleep
#define FULL_REFRESH_INTERVAL 60    // Full refresh every N partial refreshes (~30s at idle)

// ============================================================================
// Forward Declarations
// ============================================================================
// Display
void initDisplay(bool fullRefresh);
void showBootSplash();
void clearScreen();
void drawSpriteToDisplay(uint8_t spriteId, uint8_t frame, int16_t x, int16_t y);
void drawScaledXBM(const uint8_t* bitmap, int16_t x, int16_t y, int16_t w, int16_t h, int scale);
void drawText(int16_t x, int16_t y, uint8_t size, const char* text);
void drawBitmapFull(const uint8_t* data);
void drawBitmapRegion(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* data);
void doRefresh(bool full);

// Animation
void updateAnimation();
void setState(AnimState newState);
void drawCurrentFrame();

// Serial protocol
void onPacketReceived(const uint8_t* buffer, size_t size);
void sendResponse(uint8_t cmd, const uint8_t* payload, uint16_t len);
void sendAck(uint8_t forCmd);
void sendError(uint8_t code, const char* msg);
void sendButton(uint8_t buttonId);
void sendPong();
void sendBattery();
uint16_t crc16(const uint8_t* data, size_t len);

// Buttons
void pollButtons();
void checkPowerButton();
void checkAdcButtons();

// Power
void enterSleep();

// ============================================================================
// Global State
// ============================================================================

// Display
GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

// Serial
PacketSerial packetSerial;

// Animation state machine
AnimState   currentState       = STATE_IDLE;
uint8_t     currentFrame       = 0;
uint32_t    frameTimer         = 0;
uint16_t    partialRefreshCount = 0;
bool        animationDirty     = true; // Need to redraw sprite

// Status/detail text displayed next to sprite
char statusText[64]  = "Hello!";
char detailText[128] = "Waiting for host...";
bool textDirty       = false;

// Serial tracking
uint32_t lastSerialDataMs = 0;
bool     hostConnected    = false;

// Button state
bool     powerBtnPressed     = false;
uint32_t powerBtnPressTime   = 0;
int      lastAdc1Value       = 0;
int      lastAdc2Value       = 0;
uint32_t lastAdc1ChangeMs    = 0;
uint32_t lastAdc2ChangeMs    = 0;

// Display state
bool displayInited = false;
bool sleeping      = false;
uint32_t bootTime  = 0;   // millis() at boot, used to ignore button during startup

// ============================================================================
// CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
// ============================================================================
uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }
    return crc;
}

// ============================================================================
// Display Functions
// ============================================================================
void initDisplay(bool fullRefresh) {
    display.init(115200, fullRefresh, 2, false, SPI, spiSettings);
    display.setRotation(0);
    displayInited = true;
}

void showBootSplash() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Title: "Claude Pet" centered
        display.setFont(&FreeMonoBold18pt7b);
        display.setTextColor(GxEPD_BLACK);

        // Approximate centering for "Claude Pet"
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds("Claude Pet", 0, 0, &tbx, &tby, &tbw, &tbh);
        int16_t titleX = (DISP_W - tbw) / 2 - tbx;
        int16_t titleY = DISP_H / 2 - 20;
        display.setCursor(titleX, titleY);
        display.print("Claude Pet");

        // Subtitle
        display.setFont(&FreeMono9pt7b);
        display.getTextBounds("Your animated companion", 0, 0, &tbx, &tby, &tbw, &tbh);
        int16_t subX = (DISP_W - tbw) / 2 - tbx;
        display.setCursor(subX, titleY + 40);
        display.print("Your animated companion");

        // Version
        display.setCursor(subX + 20, titleY + 65);
        display.print("v1.0.0");

        // Draw a simple orb in the center above text
        int orbCx = DISP_W / 2;
        int orbCy = titleY - 80;
        // Filled circle with outline
        for (int r = 30; r >= 28; r--) {
            for (int a = 0; a < 360; a++) {
                float rad = a * 3.14159f / 180.0f;
                int px = orbCx + (int)(r * cosf(rad));
                int py = orbCy + (int)(r * sinf(rad));
                display.drawPixel(px, py, GxEPD_BLACK);
            }
        }
        // Eyes
        display.fillRect(orbCx - 8, orbCy - 4, 4, 4, GxEPD_BLACK);
        display.fillRect(orbCx + 5, orbCy - 4, 4, 4, GxEPD_BLACK);
        // Smile
        for (int x = -5; x <= 5; x++) {
            int sy = orbCy + 8 + (x * x) / 8;
            display.drawPixel(orbCx + x, sy, GxEPD_BLACK);
        }

    } while (display.nextPage());
}

void clearScreen() {
    initDisplay(false);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());
}

void drawScaledXBM(const uint8_t* bitmap, int16_t x, int16_t y,
                    int16_t w, int16_t h, int scale) {
    // XBM format: LSB-first, each byte = 8 horizontal pixels
    int bytesPerRow = w / 8;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int byteIdx = row * bytesPerRow + (col / 8);
            int bitIdx = col % 8;
            bool pixel = (bitmap[byteIdx] >> bitIdx) & 1;
            if (pixel) {
                // Draw scaled pixel (scale x scale block)
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        display.drawPixel(
                            x + col * scale + sx,
                            y + row * scale + sy,
                            GxEPD_BLACK
                        );
                    }
                }
            }
        }
    }
}

void drawSpriteToDisplay(uint8_t spriteId, uint8_t frame, int16_t x, int16_t y) {
    if (spriteId >= STATE_COUNT) return;

    const AnimationDef& anim = animations[spriteId];
    if (frame >= anim.frameCount) return;

    const uint8_t* frameData = anim.frames[frame];
    if (!frameData) return;

    drawScaledXBM(frameData, x, y, SPRITE_W, SPRITE_H, SPRITE_SCALE);
}

void drawText(int16_t x, int16_t y, uint8_t size, const char* text) {
    initDisplay(false);

    // Calculate approximate text bounds for partial window
    // Use generous region to avoid clipping
    int16_t tw = strlen(text) * (size <= 1 ? 10 : (size == 2 ? 18 : 24));
    int16_t th = (size <= 1 ? 16 : (size == 2 ? 28 : 36));

    // Align x to multiple of 8
    int16_t px = (x / 8) * 8;
    int16_t pw = ((tw + x - px + 8) / 8) * 8;
    if (px + pw > DISP_W) pw = ((DISP_W - px) / 8) * 8;
    if (pw < 8) pw = 8;

    display.setPartialWindow(px, y - th, pw, th + 4);
    display.firstPage();
    do {
        display.fillRect(px, y - th, pw, th + 4, GxEPD_WHITE);

        switch (size) {
            case 0: display.setFont(&FreeMono9pt7b); break;
            case 1: display.setFont(&FreeMono12pt7b); break;
            case 2: display.setFont(&FreeMonoBold12pt7b); break;
            case 3: display.setFont(&FreeMonoBold18pt7b); break;
            default: display.setFont(&FreeMono9pt7b); break;
        }
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(x, y);
        display.print(text);
    } while (display.nextPage());
}

void drawBitmapFull(const uint8_t* data) {
    initDisplay(false);
    display.setFullWindow();
    display.firstPage();
    do {
        // data is 48000 bytes = 800*480/8, row-major, MSB-first
        display.drawBitmap(0, 0, data, DISP_W, DISP_H, GxEPD_BLACK, GxEPD_WHITE);
    } while (display.nextPage());
}

void drawBitmapRegion(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* data) {
    initDisplay(false);

    // Align x to 8
    int16_t ax = (x / 8) * 8;
    int16_t aw = ((w + x - ax + 7) / 8) * 8;
    if (aw < 8) aw = 8;

    display.setPartialWindow(ax, y, aw, h);
    display.firstPage();
    do {
        display.fillRect(ax, y, aw, h, GxEPD_WHITE);
        display.drawBitmap(x, y, data, w, h, GxEPD_BLACK, GxEPD_WHITE);
    } while (display.nextPage());
}

void doRefresh(bool full) {
    if (full) {
        initDisplay(true);
        // Redraw everything
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);

            // Redraw current sprite
            const AnimationDef& anim = animations[currentState];
            if (currentFrame < anim.frameCount && anim.frames[currentFrame]) {
                drawScaledXBM(anim.frames[currentFrame],
                              SPRITE_X, SPRITE_Y, SPRITE_W, SPRITE_H, SPRITE_SCALE);
            }

            // Redraw text
            display.setFont(&FreeMonoBold12pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(STATUS_TEXT_X, STATUS_TEXT_Y);
            display.print(statusText);

            display.setFont(&FreeMono9pt7b);
            display.setCursor(DETAIL_TEXT_X, DETAIL_TEXT_Y);
            display.print(detailText);
        } while (display.nextPage());

        partialRefreshCount = 0;
    } else {
        // Partial refresh handled by caller (e.g., animation update)
        // Just trigger a display update of the current partial window
        initDisplay(false);
    }
}

// ============================================================================
// Animation State Machine
// ============================================================================
void setState(AnimState newState) {
    if (newState >= STATE_COUNT) return;
    if (newState == currentState && newState != STATE_SUCCESS) return;

    currentState = newState;
    currentFrame = 0;
    frameTimer = millis();
    animationDirty = true;

    // Update status text for built-in states
    const char* stateNames[] = {
        "Idle", "Thinking...", "Coding", "Running",
        "Waiting", "Success!", "Error", "Sleeping"
    };
    if (newState < STATE_COUNT) {
        strncpy(statusText, stateNames[newState], sizeof(statusText) - 1);
        statusText[sizeof(statusText) - 1] = '\0';
        textDirty = true;
    }
}

void drawCurrentFrame() {
    if (!displayInited) {
        initDisplay(false);
    }

    const AnimationDef& anim = animations[currentState];
    if (currentFrame >= anim.frameCount) return;
    if (!anim.frames[currentFrame]) return;

    bool needFullRefresh = (partialRefreshCount >= FULL_REFRESH_INTERVAL);

    if (needFullRefresh) {
        // Full refresh: redraw everything
        initDisplay(true);
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);

            // Draw sprite
            drawScaledXBM(anim.frames[currentFrame],
                          SPRITE_X, SPRITE_Y, SPRITE_W, SPRITE_H, SPRITE_SCALE);

            // Draw status text
            display.setFont(&FreeMonoBold12pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(STATUS_TEXT_X, STATUS_TEXT_Y);
            display.print(statusText);

            // Draw detail text
            display.setFont(&FreeMono9pt7b);
            display.setCursor(DETAIL_TEXT_X, DETAIL_TEXT_Y);
            display.print(detailText);
        } while (display.nextPage());

        partialRefreshCount = 0;
        textDirty = false;
    } else {
        initDisplay(false);

        // Partial refresh: sprite region only (aligned to 8px boundary)
        int16_t px = (SPRITE_X / 8) * 8;
        int16_t pw = ((SPRITE_DRAW_W + SPRITE_X - px + 7) / 8) * 8;

        display.setPartialWindow(px, SPRITE_Y, pw, SPRITE_DRAW_H);
        display.firstPage();
        do {
            display.fillRect(px, SPRITE_Y, pw, SPRITE_DRAW_H, GxEPD_WHITE);
            drawScaledXBM(anim.frames[currentFrame],
                          SPRITE_X, SPRITE_Y, SPRITE_W, SPRITE_H, SPRITE_SCALE);
        } while (display.nextPage());

        partialRefreshCount++;

        // Update text area if dirty (separate partial refresh)
        if (textDirty) {
            int16_t textPx = (STATUS_TEXT_X / 8) * 8;
            int16_t textPw = ((DISP_W - textPx) / 8) * 8;
            int16_t textPy = STATUS_TEXT_Y - 20;
            int16_t textPh = DETAIL_TEXT_Y - textPy + 30;

            display.setPartialWindow(textPx, textPy, textPw, textPh);
            display.firstPage();
            do {
                display.fillRect(textPx, textPy, textPw, textPh, GxEPD_WHITE);

                display.setFont(&FreeMonoBold12pt7b);
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(STATUS_TEXT_X, STATUS_TEXT_Y);
                display.print(statusText);

                display.setFont(&FreeMono9pt7b);
                display.setCursor(DETAIL_TEXT_X, DETAIL_TEXT_Y);
                display.print(detailText);
            } while (display.nextPage());

            textDirty = false;
            partialRefreshCount++;
        }
    }

    animationDirty = false;
}

void updateAnimation() {
    if (sleeping) return;

    uint32_t now = millis();
    const AnimationDef& anim = animations[currentState];

    if (now - frameTimer >= anim.frameDurationMs) {
        frameTimer = now;

        uint8_t nextFrame = currentFrame + 1;

        if (nextFrame >= anim.frameCount) {
            if (anim.looping) {
                nextFrame = 0;
            } else {
                // Non-looping (SUCCESS): transition to IDLE
                setState(STATE_IDLE);
                return;
            }
        }

        currentFrame = nextFrame;
        animationDirty = true;
    }

    if (animationDirty) {
        drawCurrentFrame();
    }
}

// ============================================================================
// Serial Protocol
// ============================================================================

// Send a response packet: [CMD][LEN_LO][LEN_HI][PAYLOAD...][CRC_LO][CRC_HI]
void sendResponse(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    // Build packet: CMD(1) + LENGTH(2) + PAYLOAD(len) + CRC16(2)
    uint16_t packetLen = 1 + 2 + len + 2;
    uint8_t packet[packetLen];

    packet[0] = cmd;
    packet[1] = len & 0xFF;
    packet[2] = (len >> 8) & 0xFF;
    if (len > 0 && payload) {
        memcpy(&packet[3], payload, len);
    }

    // CRC over everything except the CRC field itself
    uint16_t crc = crc16(packet, 3 + len);
    packet[3 + len] = crc & 0xFF;
    packet[3 + len + 1] = (crc >> 8) & 0xFF;

    // Send via PacketSerial (COBS-encoded)
    packetSerial.send(packet, packetLen);
}

void sendAck(uint8_t forCmd) {
    sendResponse(RSP_ACK, &forCmd, 1);
}

void sendError(uint8_t code, const char* msg) {
    size_t msgLen = strlen(msg);
    uint16_t payloadLen = 1 + msgLen;
    uint8_t payload[payloadLen];
    payload[0] = code;
    memcpy(&payload[1], msg, msgLen);
    sendResponse(RSP_ERROR, payload, payloadLen);
}

void sendButton(uint8_t buttonId) {
    sendResponse(RSP_BUTTON, &buttonId, 1);
}

void sendPong() {
    sendResponse(RSP_PONG, nullptr, 0);
}

void sendBattery() {
    // Placeholder: report 4200mV (full), not charging
    uint8_t payload[3];
    uint16_t mv = 4200;
    payload[0] = mv & 0xFF;
    payload[1] = (mv >> 8) & 0xFF;
    payload[2] = 0; // not charging
    sendResponse(RSP_BATTERY, payload, 3);
}

void onPacketReceived(const uint8_t* buffer, size_t size) {
    lastSerialDataMs = millis();
    hostConnected = true;

    // Minimum packet: CMD(1) + LENGTH(2) + CRC(2) = 5 bytes
    if (size < 5) {
        sendError(ERR_BAD_LENGTH, "packet too short");
        return;
    }

    uint8_t cmd = buffer[0];
    uint16_t payloadLen = buffer[1] | ((uint16_t)buffer[2] << 8);

    // Verify total size: 1 + 2 + payloadLen + 2
    if (size != (size_t)(5 + payloadLen)) {
        sendError(ERR_BAD_LENGTH, "length mismatch");
        return;
    }

    // Verify CRC (over everything except last 2 bytes)
    uint16_t receivedCrc = buffer[size - 2] | ((uint16_t)buffer[size - 1] << 8);
    uint16_t computedCrc = crc16(buffer, size - 2);
    if (receivedCrc != computedCrc) {
        sendError(ERR_BAD_CRC, "CRC mismatch");
        return;
    }

    const uint8_t* payload = &buffer[3];

    switch (cmd) {
        case CMD_CLEAR:
            clearScreen();
            sendAck(cmd);
            break;

        case CMD_SPRITE: {
            // Payload: sprite_id(1) frame(1) x(2) y(2) = 6 bytes
            if (payloadLen < 6) {
                sendError(ERR_BAD_PARAM, "SPRITE: need 6 bytes");
                return;
            }
            uint8_t sprId = payload[0];
            uint8_t frame = payload[1];
            int16_t x = (int16_t)(payload[2] | (payload[3] << 8));
            int16_t y = (int16_t)(payload[4] | (payload[5] << 8));

            initDisplay(false);
            int16_t px = (x / 8) * 8;
            int16_t pw = ((SPRITE_DRAW_W + x - px + 7) / 8) * 8;

            display.setPartialWindow(px, y, pw, SPRITE_DRAW_H);
            display.firstPage();
            do {
                display.fillRect(px, y, pw, SPRITE_DRAW_H, GxEPD_WHITE);
                drawSpriteToDisplay(sprId, frame, x, y);
            } while (display.nextPage());

            partialRefreshCount++;
            sendAck(cmd);
            break;
        }

        case CMD_TEXT: {
            // Payload: x(2) y(2) size(1) text(null-terminated)
            if (payloadLen < 6) {
                sendError(ERR_BAD_PARAM, "TEXT: need x,y,size,text");
                return;
            }
            int16_t x = (int16_t)(payload[0] | (payload[1] << 8));
            int16_t y = (int16_t)(payload[2] | (payload[3] << 8));
            uint8_t textSize = payload[4];
            const char* text = (const char*)&payload[5];

            drawText(x, y, textSize, text);
            partialRefreshCount++;
            sendAck(cmd);
            break;
        }

        case CMD_BITMAP_FULL: {
            // Payload: 48000 bytes (800*480/8)
            if (payloadLen != 48000) {
                sendError(ERR_BAD_PARAM, "BITMAP_FULL: need 48000 bytes");
                return;
            }
            drawBitmapFull(payload);
            sendAck(cmd);
            break;
        }

        case CMD_BITMAP_REGION: {
            // Payload: x(2) y(2) w(2) h(2) data(w*h/8)
            if (payloadLen < 8) {
                sendError(ERR_BAD_PARAM, "BITMAP_REGION: need header");
                return;
            }
            int16_t x = (int16_t)(payload[0] | (payload[1] << 8));
            int16_t y = (int16_t)(payload[2] | (payload[3] << 8));
            int16_t w = (int16_t)(payload[4] | (payload[5] << 8));
            int16_t h = (int16_t)(payload[6] | (payload[7] << 8));
            uint32_t expectedBytes = ((uint32_t)w * h + 7) / 8;

            if (payloadLen != 8 + expectedBytes) {
                sendError(ERR_BAD_PARAM, "BITMAP_REGION: data size mismatch");
                return;
            }
            drawBitmapRegion(x, y, w, h, &payload[8]);
            partialRefreshCount++;
            sendAck(cmd);
            break;
        }

        case CMD_REFRESH: {
            // Payload: mode(1) — 0=partial, 1=full
            bool full = (payloadLen > 0 && payload[0] == 1);
            doRefresh(full);
            sendAck(cmd);
            break;
        }

        case CMD_SET_STATE: {
            // Payload: state(1), optional: status_text, detail_text
            if (payloadLen < 1) {
                sendError(ERR_BAD_PARAM, "SET_STATE: need state byte");
                return;
            }
            uint8_t newState = payload[0];
            if (newState >= STATE_COUNT) {
                sendError(ERR_BAD_PARAM, "SET_STATE: invalid state");
                return;
            }
            setState((AnimState)newState);

            // Optional: if payload has more, parse status and detail text
            // Format: state(1) [status_text\0 [detail_text\0]]
            if (payloadLen > 1) {
                const char* p = (const char*)&payload[1];
                size_t remaining = payloadLen - 1;

                // Find first null terminator for status text
                size_t sLen = strnlen(p, remaining);
                if (sLen < remaining) {
                    strncpy(statusText, p, sizeof(statusText) - 1);
                    statusText[sizeof(statusText) - 1] = '\0';
                    textDirty = true;

                    // Advance past null
                    p += sLen + 1;
                    remaining -= sLen + 1;

                    if (remaining > 0) {
                        size_t dLen = strnlen(p, remaining);
                        strncpy(detailText, p, sizeof(detailText) - 1);
                        detailText[sizeof(detailText) - 1] = '\0';
                        (void)dLen;
                    }
                }
            }

            sendAck(cmd);
            break;
        }

        case CMD_PING:
            sendPong();
            break;

        default:
            sendError(ERR_UNKNOWN_CMD, "unknown command");
            break;
    }
}

// ============================================================================
// Button Handling
// ============================================================================

void checkPowerButton() {
    // Ignore button for first 5 seconds after boot to avoid phantom presses
    if (millis() - bootTime < 5000) return;

    bool pressed = (digitalRead(BTN_POWER) == LOW);

    if (pressed && !powerBtnPressed) {
        // Rising edge (pressed)
        powerBtnPressed = true;
        powerBtnPressTime = millis();
    } else if (!pressed && powerBtnPressed) {
        // Falling edge (released)
        uint32_t holdTime = millis() - powerBtnPressTime;
        powerBtnPressed = false;

        if (holdTime >= POWER_HOLD_SLEEP_MS) {
            // Long press -> sleep
            enterSleep();
        } else if (holdTime >= DEBOUNCE_MS) {
            // Short press -> send button event
            sendButton(0); // button 0 = power
        }
    } else if (pressed && powerBtnPressed) {
        // Check for long hold while still pressed
        if (millis() - powerBtnPressTime >= POWER_HOLD_SLEEP_MS) {
            // Enter sleep immediately on long hold
            powerBtnPressed = false;
            enterSleep();
        }
    }
}

// ADC button thresholds (resistor ladder)
// These are approximate and may need calibration
#define ADC_THRESHOLD_NONE   3800
#define ADC_THRESHOLD_BTN_A  2000   // ~1.5V
#define ADC_THRESHOLD_BTN_B  1000   // ~0.8V
#define ADC_HYSTERESIS       200

static uint8_t classifyAdcButton(int value) {
    if (value < ADC_THRESHOLD_BTN_B + ADC_HYSTERESIS) return 2; // Button B
    if (value < ADC_THRESHOLD_BTN_A + ADC_HYSTERESIS) return 1; // Button A
    return 0; // No press
}

void checkAdcButtons() {
    uint32_t now = millis();

    // ADC button 1 (GPIO 1)
    int adc1 = analogRead(BTN_ADC1);
    uint8_t btn1 = classifyAdcButton(adc1);
    uint8_t prevBtn1 = classifyAdcButton(lastAdc1Value);

    if (btn1 != prevBtn1 && (now - lastAdc1ChangeMs) >= DEBOUNCE_MS) {
        if (btn1 > 0) {
            sendButton(btn1); // Button 1 or 2
        }
        lastAdc1Value = adc1;
        lastAdc1ChangeMs = now;
    }

    // ADC button 2 (GPIO 2)
    int adc2 = analogRead(BTN_ADC2);
    uint8_t btn2 = classifyAdcButton(adc2);
    uint8_t prevBtn2 = classifyAdcButton(lastAdc2Value);

    if (btn2 != prevBtn2 && (now - lastAdc2ChangeMs) >= DEBOUNCE_MS) {
        if (btn2 > 0) {
            sendButton(btn2 + 2); // Button 3 or 4
        }
        lastAdc2Value = adc2;
        lastAdc2ChangeMs = now;
    }
}

void pollButtons() {
    checkPowerButton();
    checkAdcButtons();
}

// ============================================================================
// Power Management
// ============================================================================

void enterSleep() {
    sleeping = true;

    // Show sleep screen
    setState(STATE_SLEEPING);
    drawCurrentFrame();

    // Draw sleep message
    initDisplay(false);
    int16_t textPx = (STATUS_TEXT_X / 8) * 8;
    int16_t textPw = ((DISP_W - textPx) / 8) * 8;
    display.setPartialWindow(textPx, STATUS_TEXT_Y - 20, textPw, 60);
    display.firstPage();
    do {
        display.fillRect(textPx, STATUS_TEXT_Y - 20, textPw, 60, GxEPD_WHITE);
        display.setFont(&FreeMonoBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(STATUS_TEXT_X, STATUS_TEXT_Y);
        display.print("Sleeping...");
        display.setFont(&FreeMono9pt7b);
        display.setCursor(DETAIL_TEXT_X, DETAIL_TEXT_Y);
        display.print("Press power to wake");
    } while (display.nextPage());

    display.hibernate();

    // Wait for power button release
    while (digitalRead(BTN_POWER) == LOW) {
        delay(10);
    }
    delay(200);

    // Wait for power button press to wake
    while (digitalRead(BTN_POWER) == HIGH) {
        delay(50);
    }
    // Debounce
    delay(DEBOUNCE_MS);
    while (digitalRead(BTN_POWER) == LOW) {
        delay(10);
    }
    delay(200);

    // Wake up
    sleeping = false;
    initDisplay(true);
    setState(STATE_IDLE);
    strncpy(statusText, "Hello!", sizeof(statusText) - 1);
    strncpy(detailText, "Waking up...", sizeof(detailText) - 1);
    textDirty = true;
    drawCurrentFrame();
    partialRefreshCount = 0;
}

// ============================================================================
// USB Disconnect Detection
// ============================================================================

void checkSerialTimeout() {
    if (!hostConnected) return;

    uint32_t now = millis();
    if (now - lastSerialDataMs >= SERIAL_TIMEOUT_MS) {
        // No serial data for 60s — assume host disconnected
        hostConnected = false;
        setState(STATE_IDLE);
        strncpy(detailText, "Host disconnected", sizeof(detailText) - 1);
        textDirty = true;
    }
}

// ============================================================================
// Setup & Main Loop
// ============================================================================

void setup() {
    // Init serial (USB CDC)
    Serial.begin(115200);
    delay(500); // Let USB enumerate

    // Configure button pins
    pinMode(BTN_POWER, INPUT_PULLUP);
    // ADC pins don't need pinMode for analogRead

    // Init SPI for display
    SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);

    // Init display (full refresh on boot)
    initDisplay(true);

    // Generate sprite data
    initAnimations();

    // Show boot splash
    showBootSplash();
    delay(2000);

    // Init PacketSerial (COBS over Serial)
    packetSerial.setStream(&Serial);
    packetSerial.setPacketHandler(&onPacketReceived);

    // Initial state
    bootTime = millis();
    lastSerialDataMs = millis();
    setState(STATE_IDLE);
    strncpy(detailText, "Waiting for host...", sizeof(detailText) - 1);
    textDirty = true;

    // Draw initial frame (full refresh to start clean)
    partialRefreshCount = FULL_REFRESH_INTERVAL; // Force full refresh
    drawCurrentFrame();

    Serial.println("Claude Pet v1.0.0 ready");
}

void loop() {
    // Process incoming serial packets
    packetSerial.update();

    // Update animation (advance frames, redraw if needed)
    updateAnimation();

    // Poll buttons
    pollButtons();

    // Check for serial timeout (host disconnect)
    checkSerialTimeout();

    // Small yield to avoid starving the watchdog (even though it's disabled,
    // good practice for cooperative multitasking)
    delay(1);
}
