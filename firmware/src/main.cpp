#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <SPI.h>

// Xteink X4 display SPI pins
#define EPD_SCLK 8
#define EPD_MOSI 10
#define EPD_CS   21
#define EPD_DC   4
#define EPD_RST  5
#define EPD_BUSY 6

// Button inputs
#define BTN_POWER 3

// Display dimensions
#define W 800
#define H 480

// GDEQ0426T82: 4.26" 800x480, SSD1677 controller
GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

bool asleep = false;
SPISettings spi_settings(4000000, MSBFIRST, SPI_MODE0);

void initDisplay(bool full) {
    display.init(115200, full, 2, false, SPI, spi_settings);
    display.setRotation(0);
}

void showText(const char* line1, const char* line2) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeMonoBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(220, 220);
        display.print(line1);
        if (line2) {
            display.setFont(nullptr);
            display.setTextSize(2);
            display.setCursor(260, 270);
            display.print(line2);
        }
    } while (display.nextPage());
}

void drawLabel(int16_t x, int16_t y, const char* text) {
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x, y);
    display.print(text);
}

void showAcidTest() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // === Outer border (1px inset so it's fully visible) ===
        display.drawRect(0, 0, W, H, GxEPD_BLACK);
        display.drawRect(1, 1, W - 2, H - 2, GxEPD_BLACK);

        // === Grid: 100px spacing ===
        for (int x = 100; x < W; x += 100) {
            for (int y = 0; y < H; y += 4) {
                display.drawPixel(x, y, GxEPD_BLACK); // dashed vertical
            }
        }
        for (int y = 100; y < H; y += 100) {
            for (int x = 0; x < W; x += 4) {
                display.drawPixel(x, y, GxEPD_BLACK); // dashed horizontal
            }
        }

        // === Center crosshair ===
        int cx = W / 2, cy = H / 2;
        display.drawLine(cx - 30, cy, cx + 30, cy, GxEPD_BLACK);
        display.drawLine(cx, cy - 30, cx, cy + 30, GxEPD_BLACK);
        display.drawCircle(cx, cy, 20, GxEPD_BLACK);

        // === Corner markers (10px triangles) ===
        // Top-left
        display.drawLine(0, 0, 20, 0, GxEPD_BLACK);
        display.drawLine(0, 0, 0, 20, GxEPD_BLACK);
        display.drawLine(0, 0, 20, 20, GxEPD_BLACK);
        // Top-right
        display.drawLine(W-1, 0, W-21, 0, GxEPD_BLACK);
        display.drawLine(W-1, 0, W-1, 20, GxEPD_BLACK);
        display.drawLine(W-1, 0, W-21, 20, GxEPD_BLACK);
        // Bottom-left
        display.drawLine(0, H-1, 20, H-1, GxEPD_BLACK);
        display.drawLine(0, H-1, 0, H-21, GxEPD_BLACK);
        display.drawLine(0, H-1, 20, H-21, GxEPD_BLACK);
        // Bottom-right
        display.drawLine(W-1, H-1, W-21, H-1, GxEPD_BLACK);
        display.drawLine(W-1, H-1, W-1, H-21, GxEPD_BLACK);
        display.drawLine(W-1, H-1, W-21, H-21, GxEPD_BLACK);

        // === Corner coordinate labels ===
        drawLabel(5, 35, "0,0");
        drawLabel(W - 75, 35, "799,0");
        drawLabel(5, H - 10, "0,479");
        drawLabel(W - 95, H - 10, "799,479");

        // === Center label ===
        drawLabel(cx - 35, cy - 35, "400,240");

        // === Title ===
        display.setFont(&FreeMonoBold18pt7b);
        display.setCursor(200, 80);
        display.print("ACID TEST");

        // === Dimension labels ===
        display.setFont(&FreeMono9pt7b);
        display.setCursor(330, 470);
        display.print("800 x 480");

        // === Edge-to-edge bars (prove we touch all 4 edges) ===
        // Top bar: full width, 4px tall at y=0
        display.fillRect(0, 0, W, 4, GxEPD_BLACK);
        // Bottom bar
        display.fillRect(0, H - 4, W, 4, GxEPD_BLACK);
        // Left bar
        display.fillRect(0, 0, 4, H, GxEPD_BLACK);
        // Right bar
        display.fillRect(W - 4, 0, 4, H, GxEPD_BLACK);

        // === Diagonal lines corner to corner ===
        display.drawLine(0, 0, W - 1, H - 1, GxEPD_BLACK);
        display.drawLine(W - 1, 0, 0, H - 1, GxEPD_BLACK);

    } while (display.nextPage());
}

// Bouncing ball animation using partial refresh
int ballX = 100, ballY = 240;
int ballDX = 40, ballDY = 25;
int ballR = 15;

void animateBall() {
    int oldX = ballX, oldY = ballY;

    // Move
    ballX += ballDX;
    ballY += ballDY;

    // Bounce off edges
    if (ballX - ballR <= 4 || ballX + ballR >= W - 4) {
        ballDX = -ballDX;
        ballX += ballDX;
    }
    if (ballY - ballR <= 4 || ballY + ballR >= H - 4) {
        ballDY = -ballDY;
        ballY += ballDY;
    }

    // Partial refresh: erase old, draw new
    // Compute bounding box covering both old and new positions
    int minX = min(oldX, ballX) - ballR - 2;
    int minY = min(oldY, ballY) - ballR - 2;
    int maxX = max(oldX, ballX) + ballR + 2;
    int maxY = max(oldY, ballY) + ballR + 2;

    // Clamp to screen
    minX = max(0, minX);
    minY = max(0, minY);
    maxX = min(W - 1, maxX);
    maxY = min(H - 1, maxY);

    // GxEPD2 requires x to be multiple of 8
    minX = (minX / 8) * 8;
    int regionW = ((maxX - minX + 8) / 8) * 8;
    int regionH = maxY - minY + 1;

    display.setPartialWindow(minX, minY, regionW, regionH);
    display.firstPage();
    do {
        display.fillRect(minX, minY, regionW, regionH, GxEPD_WHITE);
        // Redraw grid lines in this region (dashed)
        for (int gx = ((minX / 100) + 1) * 100; gx < minX + regionW && gx < W; gx += 100) {
            for (int gy = minY; gy < minY + regionH && gy < H; gy += 4) {
                display.drawPixel(gx, gy, GxEPD_BLACK);
            }
        }
        for (int gy = ((minY / 100) + 1) * 100; gy < minY + regionH && gy < H; gy += 100) {
            for (int gx = minX; gx < minX + regionW && gx < W; gx += 4) {
                display.drawPixel(gx, gy, GxEPD_BLACK);
            }
        }
        // Draw ball
        display.fillCircle(ballX, ballY, ballR, GxEPD_BLACK);
        display.drawCircle(ballX, ballY, ballR + 1, GxEPD_BLACK);
    } while (display.nextPage());
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Claude Pet - Acid Test");

    pinMode(BTN_POWER, INPUT_PULLUP);

    SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);
    initDisplay(true);

    // Boot into acid test
    showAcidTest();
    Serial.println("Acid test drawn, starting animation");
}

int partialCount = 0;

void loop() {
    // Power button: toggle sleep
    if (digitalRead(BTN_POWER) == LOW) {
        delay(50);
        if (digitalRead(BTN_POWER) == LOW) {
            asleep = !asleep;
            initDisplay(false);

            if (asleep) {
                showText("Claude is asleep", nullptr);
                Serial.println("Sleeping");
            } else {
                showAcidTest();
                partialCount = 0;
                Serial.println("Awake - acid test");
            }
            display.hibernate();

            while (digitalRead(BTN_POWER) == LOW) delay(10);
            delay(200);
        }
    }

    // Animate ball when awake
    if (!asleep) {
        initDisplay(false);
        animateBall();
        partialCount++;

        // Full refresh every 20 frames to clear ghosting
        if (partialCount >= 20) {
            initDisplay(false);
            showAcidTest();
            partialCount = 0;
            ballX = 100; ballY = 240;
        }

        delay(100);
    }

    delay(20);
}
