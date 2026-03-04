// ============================================================================
// main.cpp — Claude Pet Firmware (Split-Pane UI)
// Xteink X4 (ESP32-C3, GDEQ0426T82 800x480 e-ink)
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

// Split-pane layout
#define LEFT_PANE_W       400
#define DIVIDER_X         400
#define RIGHT_PANE_X      408

// Left pane: session list
#define LIST_ROW_H         53
#define LIST_VISIBLE        9
#define LIST_TEXT_X         8
#define LIST_TEXT_BASELINE  34
#define LIST_ICON_X       368
#define LIST_ICON_Y_OFF    14

// Right pane: detail view
#define MASCOT_SCALE        3
#define MASCOT_DRAW_W      (SPRITE_W * MASCOT_SCALE)
#define MASCOT_DRAW_H      (SPRITE_H * MASCOT_SCALE)
#define MASCOT_X           (RIGHT_PANE_X + (DISP_W - RIGHT_PANE_X - MASCOT_DRAW_W) / 2)
#define MASCOT_Y            16
#define DETAIL_NAME_X       416
#define DETAIL_NAME_Y       228
#define DETAIL_STATE_X      416
#define DETAIL_STATE_Y      260
#define DETAIL_INFO_X       416
#define DETAIL_INFO_Y       296
#define DETAIL_SEP_Y        326
#define DETAIL_SUMMARY_X    416
#define DETAIL_SUMMARY_Y    356
#define DETAIL_TOKENS_X     416
#define DETAIL_TOKENS_Y     386

// ============================================================================
// Serial Protocol Constants
// ============================================================================
#define CMD_CLEAR        0x01
#define CMD_SPRITE       0x02
#define CMD_TEXT         0x03
#define CMD_BITMAP_FULL  0x04
#define CMD_BITMAP_REGION 0x05
#define CMD_REFRESH      0x06
#define CMD_SET_STATE    0x07
#define CMD_PING         0x08
#define CMD_SESSION_LIST 0x09

#define RSP_ACK          0x81
#define RSP_BUTTON       0x82
#define RSP_PONG         0x83
#define RSP_BATTERY      0x84
#define RSP_ERROR        0x85

#define ERR_UNKNOWN_CMD  0x01
#define ERR_BAD_CRC      0x02
#define ERR_BAD_LENGTH   0x03
#define ERR_BAD_PARAM    0x04

#define SERIAL_TIMEOUT_MS    60000
#define DEBOUNCE_MS          50
#define POWER_HOLD_SLEEP_MS  2000

// ============================================================================
// Session data
// ============================================================================
#define MAX_SESSIONS      16
#define MAX_SESSION_NAME  25
#define MAX_DETAIL_TEXT   40
#define MAX_SUMMARY_TEXT  60

struct SessionEntry {
    uint8_t  state;
    char     name[MAX_SESSION_NAME + 1];
    char     detail[MAX_DETAIL_TEXT + 1];
    uint32_t turnTokens;
    char     summary[MAX_SUMMARY_TEXT + 1];
};

// ============================================================================
// Forward Declarations
// ============================================================================
void initDisplay();
void showBootSplash();
void drawSplitPane();
void drawRow(uint8_t visibleIdx);
void drawScaledXBM(const uint8_t* bitmap, int16_t x, int16_t y,
                    int16_t w, int16_t h, int scale);
void drawIcon(int16_t x, int16_t y, uint8_t stateId, bool inverted);
void moveSelection(int8_t dir);

void onPacketReceived(const uint8_t* buffer, size_t size);
void sendResponse(uint8_t cmd, const uint8_t* payload, uint16_t len);
void sendAck(uint8_t forCmd);
void sendError(uint8_t code, const char* msg);
void sendButton(uint8_t buttonId);
void sendPong();
void sendBattery();
uint16_t crc16(const uint8_t* data, size_t len);

void pollButtons();
void checkPowerButton();
void checkAdcButtons();
void enterSleep();
void checkSerialTimeout();

// ============================================================================
// Global State
// ============================================================================
GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);
// Default 256-byte buffer is too small for SESSION_LIST with summaries.
// Max packet: 16 entries * 133 bytes + overhead ≈ 2.1 KB
PacketSerial_<COBS, 0, 2560> packetSerial;

// Sessions
SessionEntry sessionList[MAX_SESSIONS];
uint8_t sessionCount   = 0;
uint8_t selectedIndex  = 0;
uint8_t scrollOffset   = 0;
bool    screenDirty    = true;
bool    rightPaneDirty = false;

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
uint32_t bootTime  = 0;

// Right pane deferral — wait for user to stop scrolling before full redraw
#define RIGHT_PANE_DEFER_MS  400
uint32_t lastMoveSelectionMs = 0;

// Hourly full refresh to clear e-ink ghosting
#define FULL_REFRESH_INTERVAL_MS 3600000UL
uint32_t lastFullRefreshMs = 0;
bool needsFullRefresh = false;

// ============================================================================
// State label lookup
// ============================================================================
static const char* stateLabel(uint8_t state) {
    static const char* labels[] = {
        "Idle", "Thinking...", "Coding", "Running",
        "Waiting", "Done!", "Error", "Sleeping"
    };
    if (state < STATE_COUNT) return labels[state];
    return "?";
}

// ============================================================================
// CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
// ============================================================================
uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else              crc = crc << 1;
        }
    }
    return crc;
}

// ============================================================================
// Display Functions
// ============================================================================
void initDisplay() {
    if (displayInited) return;
    // SSD1677 needs true on first init or partial updates won't work
    display.init(0, true, 2, false, SPI, spiSettings);
    display.setRotation(0);
    displayInited = true;
}

void showBootSplash() {
    display.setPartialWindow(0, 0, DISP_W, DISP_H);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeMonoBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        int16_t tbx, tby; uint16_t tbw, tbh;
        display.getTextBounds("Claude Pet", 0, 0, &tbx, &tby, &tbw, &tbh);
        display.setCursor((DISP_W - tbw) / 2 - tbx, DISP_H / 2);
        display.print("Claude Pet");
    } while (display.nextPage());
}

void drawScaledXBM(const uint8_t* bitmap, int16_t x, int16_t y,
                    int16_t w, int16_t h, int scale) {
    int bytesPerRow = w / 8;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int byteIdx = row * bytesPerRow + (col / 8);
            int bitIdx = col % 8;
            if ((bitmap[byteIdx] >> bitIdx) & 1) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        display.drawPixel(x + col * scale + sx,
                                          y + row * scale + sy, GxEPD_BLACK);
            }
        }
    }
}

void drawIcon(int16_t x, int16_t y, uint8_t stateId, bool inverted) {
    if (stateId >= STATE_COUNT) return;
    const uint8_t* icon = iconPool[stateId];
    uint16_t fg = inverted ? GxEPD_WHITE : GxEPD_BLACK;
    for (int row = 0; row < ICON_H; row++) {
        for (int col = 0; col < ICON_W; col++) {
            int byteIdx = row * ICON_BPR + (col / 8);
            int bitIdx = col % 8;
            if ((icon[byteIdx] >> bitIdx) & 1) {
                display.drawPixel(x + col, y + row, fg);
            }
        }
    }
}

// Format token count: "847 tok", "1.2k tok", "24.5k tok", "2.1M tok"
static void formatTokens(uint32_t tokens, char* buf, size_t bufLen) {
    if (tokens == 0) {
        buf[0] = '\0';
        return;
    }
    if (tokens < 1000) {
        snprintf(buf, bufLen, "Turn: %lu tok", (unsigned long)tokens);
    } else if (tokens < 100000) {
        snprintf(buf, bufLen, "Turn: %.1fk tok", tokens / 1000.0);
    } else if (tokens < 1000000) {
        snprintf(buf, bufLen, "Turn: %luk tok", (unsigned long)(tokens / 1000));
    } else {
        snprintf(buf, bufLen, "Turn: %.1fM tok", tokens / 1000000.0);
    }
}

void drawSplitPane() {
    if (sleeping) return;
    initDisplay();

    display.setPartialWindow(0, 0, DISP_W, DISP_H);

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // ---- LEFT PANE: Session List ----
        if (sessionCount == 0) {
            display.setFont(&FreeMono9pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(LIST_TEXT_X, DISP_H / 2);
            display.print("No sessions");
        } else {
            for (uint8_t i = 0; i < LIST_VISIBLE && (scrollOffset + i) < sessionCount; i++) {
                uint8_t idx = scrollOffset + i;
                int16_t rowY = i * LIST_ROW_H;
                bool selected = (idx == selectedIndex);

                if (selected) {
                    display.fillRect(0, rowY, LEFT_PANE_W, LIST_ROW_H, GxEPD_BLACK);
                }

                if (!selected && i > 0) {
                    display.drawFastHLine(0, rowY, LEFT_PANE_W, GxEPD_BLACK);
                }

                display.setFont(&FreeMono9pt7b);
                display.setTextColor(selected ? GxEPD_WHITE : GxEPD_BLACK);
                display.setCursor(LIST_TEXT_X, rowY + LIST_TEXT_BASELINE);
                display.print(sessionList[idx].name);

                drawIcon(LIST_ICON_X, rowY + LIST_ICON_Y_OFF, sessionList[idx].state, selected);
            }

            // Scroll indicators
            if (scrollOffset > 0) {
                display.setFont(&FreeMono9pt7b);
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(LEFT_PANE_W - 20, 15);
                display.print("^");
            }
            if (scrollOffset + LIST_VISIBLE < sessionCount) {
                display.setFont(&FreeMono9pt7b);
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(LEFT_PANE_W - 20, DISP_H - 5);
                display.print("v");
            }
        }

        // ---- DIVIDER ----
        display.fillRect(DIVIDER_X, 0, 2, DISP_H, GxEPD_BLACK);

        // ---- RIGHT PANE: Detail View ----
        uint8_t detailState = STATE_IDLE;
        const char* detailName = "No session";
        const char* detailStateLabel = "Idle";
        const char* detailInfo = "";

        if (sessionCount > 0 && selectedIndex < sessionCount) {
            const SessionEntry& sel = sessionList[selectedIndex];
            detailState = sel.state;
            detailName = sel.name;
            detailStateLabel = stateLabel(sel.state);
            detailInfo = sel.detail;
        }

        // Mascot (3x scale = 192x192)
        const uint8_t* mascot = stateSprite[detailState];
        if (mascot) {
            drawScaledXBM(mascot, MASCOT_X, MASCOT_Y,
                          SPRITE_W, SPRITE_H, MASCOT_SCALE);
        }

        // Session name
        display.setFont(&FreeMonoBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(DETAIL_NAME_X, DETAIL_NAME_Y);
        display.print(detailName);

        // State label
        display.setFont(&FreeMono12pt7b);
        display.setCursor(DETAIL_STATE_X, DETAIL_STATE_Y);
        display.print(detailStateLabel);

        // Detail/info text
        display.setFont(&FreeMono9pt7b);
        display.setCursor(DETAIL_INFO_X, DETAIL_INFO_Y);
        display.print(detailInfo);

        // Separator, summary, and token count (only if session selected)
        if (sessionCount > 0 && selectedIndex < sessionCount) {
            const SessionEntry& sel = sessionList[selectedIndex];

            // Thin separator line
            display.drawFastHLine(DETAIL_SUMMARY_X, DETAIL_SEP_Y, DISP_W - DETAIL_SUMMARY_X - 16, GxEPD_BLACK);

            // Tool summary
            if (sel.summary[0] != '\0') {
                display.setFont(&FreeMono9pt7b);
                display.setCursor(DETAIL_SUMMARY_X, DETAIL_SUMMARY_Y);
                display.print(sel.summary);
            }

            // Turn token count
            char tokBuf[32];
            formatTokens(sel.turnTokens, tokBuf, sizeof(tokBuf));
            if (tokBuf[0] != '\0') {
                display.setFont(&FreeMono9pt7b);
                display.setCursor(DETAIL_TOKENS_X, DETAIL_TOKENS_Y);
                display.print(tokBuf);
            }
        }

    } while (display.nextPage());

    screenDirty = false;
    rightPaneDirty = false;
    lastFullRefreshMs = millis();
}

// Row-band partial update: redraws a single visible list row (full-width, LIST_ROW_H tall)
void drawRow(uint8_t visibleIdx) {
    if (sleeping) return;
    uint8_t idx = scrollOffset + visibleIdx;
    if (idx >= sessionCount) return;
    initDisplay();

    int16_t rowY = visibleIdx * LIST_ROW_H;
    bool selected = (idx == selectedIndex);

    // Full-width strip to avoid SSD1677 hang on non-full-width full-height windows
    // Height = LIST_ROW_H (53px), so refresh is ~60-100ms instead of ~600ms
    display.setPartialWindow(0, rowY, DISP_W, LIST_ROW_H);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Left pane: row content
        if (selected) {
            display.fillRect(0, rowY, LEFT_PANE_W, LIST_ROW_H, GxEPD_BLACK);
        }
        if (!selected && visibleIdx > 0) {
            display.drawFastHLine(0, rowY, LEFT_PANE_W, GxEPD_BLACK);
        }

        display.setFont(&FreeMono9pt7b);
        display.setTextColor(selected ? GxEPD_WHITE : GxEPD_BLACK);
        display.setCursor(LIST_TEXT_X, rowY + LIST_TEXT_BASELINE);
        display.print(sessionList[idx].name);

        drawIcon(LIST_ICON_X, rowY + LIST_ICON_Y_OFF, sessionList[idx].state, selected);

        // Divider
        display.fillRect(DIVIDER_X, rowY, 2, LIST_ROW_H, GxEPD_BLACK);

        // Right pane: whatever falls in this strip (mascot/text/nothing)
        uint8_t detailState = STATE_IDLE;
        const char* detailName = "No session";
        const char* detailStateLabel = "Idle";
        const char* detailInfo = "";

        if (sessionCount > 0 && selectedIndex < sessionCount) {
            const SessionEntry& sel = sessionList[selectedIndex];
            detailState = sel.state;
            detailName = sel.name;
            detailStateLabel = stateLabel(sel.state);
            detailInfo = sel.detail;
        }

        // Mascot (only rows that intersect this strip)
        const uint8_t* mascot = stateSprite[detailState];
        if (mascot && rowY < MASCOT_Y + MASCOT_DRAW_H && rowY + LIST_ROW_H > MASCOT_Y) {
            drawScaledXBM(mascot, MASCOT_X, MASCOT_Y,
                          SPRITE_W, SPRITE_H, MASCOT_SCALE);
        }

        // Text labels (only if they fall in this strip)
        if (rowY <= DETAIL_NAME_Y && rowY + LIST_ROW_H > DETAIL_NAME_Y - 20) {
            display.setFont(&FreeMonoBold12pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(DETAIL_NAME_X, DETAIL_NAME_Y);
            display.print(detailName);
        }
        if (rowY <= DETAIL_STATE_Y && rowY + LIST_ROW_H > DETAIL_STATE_Y - 16) {
            display.setFont(&FreeMono12pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(DETAIL_STATE_X, DETAIL_STATE_Y);
            display.print(detailStateLabel);
        }
        if (rowY <= DETAIL_INFO_Y && rowY + LIST_ROW_H > DETAIL_INFO_Y - 14) {
            display.setFont(&FreeMono9pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(DETAIL_INFO_X, DETAIL_INFO_Y);
            display.print(detailInfo);
        }

        // Separator line
        if (rowY <= DETAIL_SEP_Y && rowY + LIST_ROW_H > DETAIL_SEP_Y) {
            display.drawFastHLine(DETAIL_SUMMARY_X, DETAIL_SEP_Y, DISP_W - DETAIL_SUMMARY_X - 16, GxEPD_BLACK);
        }

        // Summary text
        if (sessionCount > 0 && selectedIndex < sessionCount) {
            const SessionEntry& sel = sessionList[selectedIndex];
            if (sel.summary[0] != '\0' && rowY <= DETAIL_SUMMARY_Y && rowY + LIST_ROW_H > DETAIL_SUMMARY_Y - 14) {
                display.setFont(&FreeMono9pt7b);
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(DETAIL_SUMMARY_X, DETAIL_SUMMARY_Y);
                display.print(sel.summary);
            }

            // Token count
            char tokBuf[32];
            formatTokens(sel.turnTokens, tokBuf, sizeof(tokBuf));
            if (tokBuf[0] != '\0' && rowY <= DETAIL_TOKENS_Y && rowY + LIST_ROW_H > DETAIL_TOKENS_Y - 14) {
                display.setFont(&FreeMono9pt7b);
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(DETAIL_TOKENS_X, DETAIL_TOKENS_Y);
                display.print(tokBuf);
            }
        }

    } while (display.nextPage());
}

void moveSelection(int8_t dir) {
    if (sessionCount == 0) return;
    int8_t newIdx = (int8_t)selectedIndex + dir;
    if (newIdx < 0) newIdx = 0;
    if (newIdx >= (int8_t)sessionCount) newIdx = sessionCount - 1;
    if ((uint8_t)newIdx == selectedIndex) return;

    uint8_t oldIdx = selectedIndex;
    selectedIndex = (uint8_t)newIdx;

    uint8_t oldScroll = scrollOffset;
    if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
    } else if (selectedIndex >= scrollOffset + LIST_VISIBLE) {
        scrollOffset = selectedIndex - LIST_VISIBLE + 1;
    }

    if (scrollOffset != oldScroll) {
        // Scroll position changed — full redraw needed
        screenDirty = true;
    } else {
        // Selection moved within visible window — fast row-band update
        drawRow(oldIdx - scrollOffset);
        drawRow(selectedIndex - scrollOffset);
        rightPaneDirty = true;
        lastMoveSelectionMs = millis();
    }
}

// ============================================================================
// Serial Protocol
// ============================================================================
void sendResponse(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    uint16_t packetLen = 1 + 2 + len + 2;
    uint8_t packet[packetLen];
    packet[0] = cmd;
    packet[1] = len & 0xFF;
    packet[2] = (len >> 8) & 0xFF;
    if (len > 0 && payload) memcpy(&packet[3], payload, len);
    uint16_t crc = crc16(packet, 3 + len);
    packet[3 + len]     = crc & 0xFF;
    packet[3 + len + 1] = (crc >> 8) & 0xFF;
    packetSerial.send(packet, packetLen);
}

void sendAck(uint8_t forCmd) { sendResponse(RSP_ACK, &forCmd, 1); }

void sendError(uint8_t code, const char* msg) {
    size_t msgLen = strlen(msg);
    uint16_t payloadLen = 1 + msgLen;
    uint8_t payload[payloadLen];
    payload[0] = code;
    memcpy(&payload[1], msg, msgLen);
    sendResponse(RSP_ERROR, payload, payloadLen);
}

void sendButton(uint8_t buttonId) { sendResponse(RSP_BUTTON, &buttonId, 1); }
void sendPong() { sendResponse(RSP_PONG, nullptr, 0); }

void sendBattery() {
    uint8_t payload[3];
    uint16_t mv = 4200;
    payload[0] = mv & 0xFF;
    payload[1] = (mv >> 8) & 0xFF;
    payload[2] = 0;
    sendResponse(RSP_BATTERY, payload, 3);
}

void onPacketReceived(const uint8_t* buffer, size_t size) {
    lastSerialDataMs = millis();
    hostConnected = true;

    if (size < 5) { sendError(ERR_BAD_LENGTH, "packet too short"); return; }

    uint8_t cmd = buffer[0];
    uint16_t payloadLen = buffer[1] | ((uint16_t)buffer[2] << 8);
    if (size != (size_t)(5 + payloadLen)) {
        sendError(ERR_BAD_LENGTH, "length mismatch"); return;
    }

    uint16_t receivedCrc = buffer[size - 2] | ((uint16_t)buffer[size - 1] << 8);
    uint16_t computedCrc = crc16(buffer, size - 2);
    if (receivedCrc != computedCrc) {
        sendError(ERR_BAD_CRC, "CRC mismatch"); return;
    }

    const uint8_t* payload = &buffer[3];

    switch (cmd) {
        case CMD_CLEAR:
            initDisplay();
            display.setPartialWindow(0, 0, DISP_W, DISP_H);
            display.firstPage();
            do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());
            sendAck(cmd);
            break;

        case CMD_SPRITE: {
            if (payloadLen < 6) { sendError(ERR_BAD_PARAM, "SPRITE: need 6 bytes"); return; }
            uint8_t sprId = payload[0];
            int16_t x = (int16_t)(payload[2] | (payload[3] << 8));
            int16_t y = (int16_t)(payload[4] | (payload[5] << 8));
            if (sprId >= STATE_COUNT || !stateSprite[sprId]) {
                sendError(ERR_BAD_PARAM, "invalid sprite"); return;
            }
            initDisplay();
            const int cmdScale = 2;
            int16_t drawW = SPRITE_W * cmdScale;
            int16_t drawH = SPRITE_H * cmdScale;
            int16_t px = (x / 8) * 8;
            int16_t pw = ((drawW + x - px + 7) / 8) * 8;
            display.setPartialWindow(px, y, pw, drawH);
            display.firstPage();
            do {
                display.fillRect(px, y, pw, drawH, GxEPD_WHITE);
                drawScaledXBM(stateSprite[sprId], x, y, SPRITE_W, SPRITE_H, cmdScale);
            } while (display.nextPage());
            sendAck(cmd);
            break;
        }

        case CMD_TEXT: {
            if (payloadLen < 6) { sendError(ERR_BAD_PARAM, "TEXT: need x,y,size,text"); return; }
            int16_t x = (int16_t)(payload[0] | (payload[1] << 8));
            int16_t y = (int16_t)(payload[2] | (payload[3] << 8));
            uint8_t textSize = payload[4];
            const char* text = (const char*)&payload[5];
            initDisplay();
            int16_t tw = strlen(text) * (textSize <= 1 ? 10 : (textSize == 2 ? 18 : 24));
            int16_t th = (textSize <= 1 ? 16 : (textSize == 2 ? 28 : 36));
            int16_t px = (x / 8) * 8;
            int16_t pw = ((tw + x - px + 8) / 8) * 8;
            if (px + pw > DISP_W) pw = ((DISP_W - px) / 8) * 8;
            if (pw < 8) pw = 8;
            display.setPartialWindow(px, y - th, pw, th + 4);
            display.firstPage();
            do {
                display.fillRect(px, y - th, pw, th + 4, GxEPD_WHITE);
                switch (textSize) {
                    case 0: display.setFont(&FreeMono9pt7b); break;
                    case 1: display.setFont(&FreeMono12pt7b); break;
                    case 2: display.setFont(&FreeMonoBold12pt7b); break;
                    default: display.setFont(&FreeMonoBold18pt7b); break;
                }
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(x, y);
                display.print(text);
            } while (display.nextPage());
            sendAck(cmd);
            break;
        }

        case CMD_BITMAP_FULL: {
            if (payloadLen != 48000) { sendError(ERR_BAD_PARAM, "need 48000 bytes"); return; }
            initDisplay();
            display.setPartialWindow(0, 0, DISP_W, DISP_H);
            display.firstPage();
            do {
                display.drawBitmap(0, 0, payload, DISP_W, DISP_H, GxEPD_BLACK, GxEPD_WHITE);
            } while (display.nextPage());
            sendAck(cmd);
            break;
        }

        case CMD_BITMAP_REGION: {
            if (payloadLen < 8) { sendError(ERR_BAD_PARAM, "need header"); return; }
            int16_t x = (int16_t)(payload[0] | (payload[1] << 8));
            int16_t y = (int16_t)(payload[2] | (payload[3] << 8));
            int16_t w = (int16_t)(payload[4] | (payload[5] << 8));
            int16_t h = (int16_t)(payload[6] | (payload[7] << 8));
            uint32_t expectedBytes = ((uint32_t)w * h + 7) / 8;
            if (payloadLen != 8 + expectedBytes) {
                sendError(ERR_BAD_PARAM, "data size mismatch"); return;
            }
            initDisplay();
            int16_t ax = (x / 8) * 8;
            int16_t aw = ((w + x - ax + 7) / 8) * 8;
            if (aw < 8) aw = 8;
            display.setPartialWindow(ax, y, aw, h);
            display.firstPage();
            do {
                display.fillRect(ax, y, aw, h, GxEPD_WHITE);
                display.drawBitmap(x, y, &payload[8], w, h, GxEPD_BLACK, GxEPD_WHITE);
            } while (display.nextPage());
            sendAck(cmd);
            break;
        }

        case CMD_REFRESH: {
            bool full = (payloadLen > 0 && payload[0] == 1);
            if (full) needsFullRefresh = true;
            screenDirty = true;
            sendAck(cmd);
            break;
        }

        case CMD_SET_STATE: {
            if (payloadLen < 1) { sendError(ERR_BAD_PARAM, "need state byte"); return; }
            // Display is driven by SESSION_LIST; just ACK
            sendAck(cmd);
            break;
        }

        case CMD_SESSION_LIST: {
            // Wire format: count(1) + selected_idx(1)
            //   + [state(1) + name_len(1) + name + detail_len(1) + detail + tokens(4)]...
            if (payloadLen < 2) {
                sendError(ERR_BAD_PARAM, "SESSION_LIST: need count+sel");
                return;
            }
            uint8_t count = payload[0];
            uint8_t selIdx = payload[1];
            if (count > MAX_SESSIONS) count = MAX_SESSIONS;

            size_t off = 2;
            uint8_t parsed = 0;
            for (uint8_t i = 0; i < count && off < payloadLen; i++) {
                if (off + 2 > payloadLen) break;
                uint8_t st = payload[off];
                uint8_t nameLen = payload[off + 1];
                off += 2;

                if (off + nameLen > payloadLen) break;
                if (nameLen > MAX_SESSION_NAME) nameLen = MAX_SESSION_NAME;

                sessionList[i].state = st;
                memcpy(sessionList[i].name, &payload[off], nameLen);
                sessionList[i].name[nameLen] = '\0';
                off += nameLen;

                // Detail text
                if (off < payloadLen) {
                    uint8_t detailLen = payload[off];
                    off++;
                    if (off + detailLen > payloadLen) detailLen = payloadLen - off;
                    if (detailLen > MAX_DETAIL_TEXT) detailLen = MAX_DETAIL_TEXT;
                    memcpy(sessionList[i].detail, &payload[off], detailLen);
                    sessionList[i].detail[detailLen] = '\0';
                    off += detailLen;
                } else {
                    sessionList[i].detail[0] = '\0';
                }

                // Parse tokens field (4 bytes, uint32 LE)
                if (off + 4 <= payloadLen) {
                    sessionList[i].turnTokens = payload[off]
                        | ((uint32_t)payload[off+1] << 8)
                        | ((uint32_t)payload[off+2] << 16)
                        | ((uint32_t)payload[off+3] << 24);
                    off += 4;
                } else {
                    sessionList[i].turnTokens = 0;
                }

                // Parse summary field (len + data)
                if (off < payloadLen) {
                    uint8_t summaryLen = payload[off];
                    off++;
                    if (off + summaryLen > payloadLen) summaryLen = payloadLen - off;
                    if (summaryLen > MAX_SUMMARY_TEXT) summaryLen = MAX_SUMMARY_TEXT;
                    memcpy(sessionList[i].summary, &payload[off], summaryLen);
                    sessionList[i].summary[summaryLen] = '\0';
                    off += summaryLen;
                } else {
                    sessionList[i].summary[0] = '\0';
                }

                parsed++;
            }

            sessionCount = parsed;
            if (selIdx < sessionCount) selectedIndex = selIdx;

            // Clamp scroll
            if (sessionCount <= LIST_VISIBLE) {
                scrollOffset = 0;
            } else if (scrollOffset > sessionCount - LIST_VISIBLE) {
                scrollOffset = sessionCount - LIST_VISIBLE;
            }

            // Keep selected in view
            if (selectedIndex < scrollOffset) {
                scrollOffset = selectedIndex;
            } else if (selectedIndex >= scrollOffset + LIST_VISIBLE) {
                scrollOffset = selectedIndex - LIST_VISIBLE + 1;
            }

            screenDirty = true;
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
    if (millis() - bootTime < 5000) return;
    bool pressed = (digitalRead(BTN_POWER) == LOW);

    if (pressed && !powerBtnPressed) {
        powerBtnPressed = true;
        powerBtnPressTime = millis();
    } else if (!pressed && powerBtnPressed) {
        uint32_t holdTime = millis() - powerBtnPressTime;
        powerBtnPressed = false;
        if (holdTime >= POWER_HOLD_SLEEP_MS) {
            enterSleep();
        } else if (holdTime >= DEBOUNCE_MS) {
            sendButton(0);
        }
    } else if (pressed && powerBtnPressed) {
        if (millis() - powerBtnPressTime >= POWER_HOLD_SLEEP_MS) {
            powerBtnPressed = false;
            enterSleep();
        }
    }
}

// G1 has 4 buttons across 2 rockers (resistor ladder):
//   Rocker 1 (back/go): back ~3534, go ~2702 → scroll DOWN
//   Rocker 2 (up/down): up ~1500, down ~5   → scroll UP
// G2 has 2 buttons on 1 rocker:
//   Rocker 3 (left/right): left ~2240, right ~5

#define ADC_NO_PRESS  3800

// G1: 4 levels — returns 0 (none), 1 (back), 2 (go), 3 (up), 4 (down)
static uint8_t classifyG1(int value) {
    if (value > ADC_NO_PRESS) return 0;
    if (value > 3100) return 1;   // back ~3534
    if (value > 2100) return 2;   // go   ~2702
    if (value > 750)  return 3;   // up   ~1500
    return 4;                      // down ~5
}

// G2: 2 levels — returns 0 (none), 1 (left), 2 (right)
static uint8_t classifyG2(int value) {
    if (value > ADC_NO_PRESS) return 0;
    if (value > 1100) return 1;   // left  ~2240
    return 2;                      // right ~5
}

#define REPEAT_DELAY_MS  500
#define REPEAT_RATE_MS   200

static uint32_t adc1RepeatMs = 0;
static uint32_t adc2RepeatMs = 0;

// G1 rocker direction: rocker 1 (btn 1,2) = DOWN, rocker 2 (btn 3,4) = UP
static int8_t g1Direction(uint8_t btn) {
    if (btn <= 2) return 1;   // rocker 1 (back/go) → scroll DOWN
    return -1;                 // rocker 2 (up/down) → scroll UP
}

void checkAdcButtons() {
    uint32_t now = millis();

    // G1: 4 buttons across 2 rockers
    int adc1 = analogRead(BTN_ADC1);
    uint8_t btn1 = classifyG1(adc1);
    uint8_t prevBtn1 = classifyG1(lastAdc1Value);

    if (btn1 != prevBtn1 && (now - lastAdc1ChangeMs) >= DEBOUNCE_MS) {
        if (btn1 > 0) {
            sendButton(btn1);
            moveSelection(g1Direction(btn1));
            adc1RepeatMs = now + REPEAT_DELAY_MS;
        }
        lastAdc1Value = adc1;
        lastAdc1ChangeMs = now;
    } else if (btn1 > 0 && prevBtn1 == btn1 && now >= adc1RepeatMs) {
        moveSelection(g1Direction(btn1));
        adc1RepeatMs = now + REPEAT_RATE_MS;
    }

    // G2: 2 buttons on 1 rocker
    int adc2 = analogRead(BTN_ADC2);
    uint8_t btn2 = classifyG2(adc2);
    uint8_t prevBtn2 = classifyG2(lastAdc2Value);

    if (btn2 != prevBtn2 && (now - lastAdc2ChangeMs) >= DEBOUNCE_MS) {
        if (btn2 > 0) {
            sendButton(btn2 + 4);
            moveSelection(btn2 == 1 ? -1 : 1);
            adc2RepeatMs = now + REPEAT_DELAY_MS;
        }
        lastAdc2Value = adc2;
        lastAdc2ChangeMs = now;
    } else if (btn2 > 0 && prevBtn2 == btn2 && now >= adc2RepeatMs) {
        moveSelection(btn2 == 1 ? -1 : 1);
        adc2RepeatMs = now + REPEAT_RATE_MS;
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
    initDisplay();
    display.setPartialWindow(0, 0, DISP_W, DISP_H);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        const uint8_t* mascot = stateSprite[STATE_SLEEPING];
        if (mascot) {
            int cx = (DISP_W - MASCOT_DRAW_W) / 2;
            drawScaledXBM(mascot, cx, 80, SPRITE_W, SPRITE_H, MASCOT_SCALE);
        }
        display.setFont(&FreeMonoBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        int16_t tbx, tby; uint16_t tbw, tbh;
        display.getTextBounds("Sleeping...", 0, 0, &tbx, &tby, &tbw, &tbh);
        display.setCursor((DISP_W - tbw) / 2 - tbx, 320);
        display.print("Sleeping...");
        display.setFont(&FreeMono9pt7b);
        display.getTextBounds("Press power to wake", 0, 0, &tbx, &tby, &tbw, &tbh);
        display.setCursor((DISP_W - tbw) / 2 - tbx, 360);
        display.print("Press power to wake");
    } while (display.nextPage());

    display.hibernate();

    while (digitalRead(BTN_POWER) == LOW) delay(10);
    delay(200);
    while (digitalRead(BTN_POWER) == HIGH) delay(50);
    delay(DEBOUNCE_MS);
    while (digitalRead(BTN_POWER) == LOW) delay(10);
    delay(200);

    sleeping = false;
    displayInited = false;
    initDisplay();
    screenDirty = true;
}

// ============================================================================
// Serial Timeout Detection
// ============================================================================
void checkSerialTimeout() {
    if (!hostConnected) return;
    if (millis() - lastSerialDataMs >= SERIAL_TIMEOUT_MS) {
        hostConnected = false;
        sessionCount = 0;
        screenDirty = true;
    }
}

// ============================================================================
// Setup & Main Loop
// ============================================================================
void setup() {
    Serial.setRxBufferSize(4096);  // Default 256 too small for SESSION_LIST packets
    Serial.begin(115200);
    delay(500);

    pinMode(BTN_POWER, INPUT_PULLUP);
    SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);
    initDisplay();
    initAnimations();

    showBootSplash();
    delay(2000);

    // Flush any stale data that arrived during boot
    while (Serial.available()) Serial.read();

    packetSerial.setStream(&Serial);
    packetSerial.setPacketHandler(&onPacketReceived);

    bootTime = millis();
    lastSerialDataMs = millis();
    lastFullRefreshMs = millis();

    // Draw initial split-pane (empty state)
    drawSplitPane();
}

void loop() {
    packetSerial.update();
    pollButtons();

    if (screenDirty) {
        drawSplitPane();
        pollButtons();
        packetSerial.update();
    } else if (rightPaneDirty && (millis() - lastMoveSelectionMs >= RIGHT_PANE_DEFER_MS)) {
        // Deferred right pane update — only fires after user stops scrolling
        drawSplitPane();
        pollButtons();
        packetSerial.update();
    }

    checkSerialTimeout();
    delay(1);
}
