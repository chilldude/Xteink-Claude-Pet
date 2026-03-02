#pragma once
// ============================================================================
// sprites.h — Sprite and icon data for Claude Pet split-pane UI
// 64x64 pixel mascot sprites (512 bytes each), 1-bit XBM format
// 24x24 pixel state icons (72 bytes each) for session list
// ============================================================================

#include <stdint.h>

// ---------------------------------------------------------------------------
// Sprite dimensions (mascot)
// ---------------------------------------------------------------------------
#define SPRITE_W 64
#define SPRITE_H 64
#define SPRITE_BYTES (SPRITE_W * SPRITE_H / 8) // 512

// ---------------------------------------------------------------------------
// Icon dimensions (session list state indicators)
// ---------------------------------------------------------------------------
#define ICON_W 24
#define ICON_H 24
#define ICON_BPR (ICON_W / 8)           // 3 bytes per row
#define ICON_BYTES (ICON_BPR * ICON_H)  // 72 bytes per icon

// ---------------------------------------------------------------------------
// Animation states
// ---------------------------------------------------------------------------
enum AnimState : uint8_t {
    STATE_IDLE     = 0,
    STATE_THINKING = 1,
    STATE_CODING   = 2,
    STATE_RUNNING  = 3,
    STATE_WAITING  = 4,
    STATE_SUCCESS  = 5,
    STATE_ERROR    = 6,
    STATE_SLEEPING = 7,
    STATE_COUNT    = 8
};

// ---------------------------------------------------------------------------
// Runtime buffers — one sprite + one icon per state
// ---------------------------------------------------------------------------
static uint8_t spritePool[STATE_COUNT][SPRITE_BYTES];
static const uint8_t* stateSprite[STATE_COUNT];
static uint8_t iconPool[STATE_COUNT][ICON_BYTES];

// ---------------------------------------------------------------------------
// Pixel-level drawing helpers for 64x64 sprites
// ---------------------------------------------------------------------------
static inline void sprSetPixel(uint8_t* buf, int x, int y) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return;
    buf[y * 8 + (x / 8)] |= (1 << (x % 8));
}

static inline void sprClearPixel(uint8_t* buf, int x, int y) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return;
    buf[y * 8 + (x / 8)] &= ~(1 << (x % 8));
}

static void sprClear(uint8_t* buf) {
    memset(buf, 0, SPRITE_BYTES);
}

static void sprFillCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                sprSetPixel(buf, cx + x, cy + y);
}

static void sprDrawCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int a = 0; a < 360; a++) {
        float rad = a * 3.14159f / 180.0f;
        sprSetPixel(buf, (int)(cx + r * cosf(rad) + 0.5f),
                         (int)(cy + r * sinf(rad) + 0.5f));
    }
}

static void sprHLine(uint8_t* buf, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++) sprSetPixel(buf, x, y);
}

static void sprVLine(uint8_t* buf, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++) sprSetPixel(buf, x, y);
}

static void sprLine(uint8_t* buf, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        sprSetPixel(buf, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void sprFillRect(uint8_t* buf, int x, int y, int w, int h) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            sprSetPixel(buf, x + dx, y + dy);
}

static void sprDrawRect(uint8_t* buf, int x, int y, int w, int h) {
    sprHLine(buf, x, x + w - 1, y);
    sprHLine(buf, x, x + w - 1, y + h - 1);
    sprVLine(buf, x, y, y + h - 1);
    sprVLine(buf, x + w - 1, y, y + h - 1);
}

static void sprClearCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                sprClearPixel(buf, cx + x, cy + y);
}

static void sprSparkle(uint8_t* buf, int cx, int cy, int size) {
    sprVLine(buf, cx, cy - size, cy + size);
    sprHLine(buf, cx - size, cx + size, cy);
    if (size > 2) {
        sprSetPixel(buf, cx - 1, cy - 1);
        sprSetPixel(buf, cx + 1, cy - 1);
        sprSetPixel(buf, cx - 1, cy + 1);
        sprSetPixel(buf, cx + 1, cy + 1);
    }
}

// ---------------------------------------------------------------------------
// Mascot feature drawing helpers
// ---------------------------------------------------------------------------
static void sprDrawBody(uint8_t* buf, int cx, int cy, int r) {
    sprFillCircle(buf, cx, cy, r);
    sprClearCircle(buf, cx, cy, r - 3);
    sprDrawCircle(buf, cx, cy, r);
    sprDrawCircle(buf, cx, cy, r - 1);
}

static void sprDotEyes(uint8_t* buf, int cx, int cy) {
    sprFillRect(buf, cx - 8, cy - 2, 3, 3);
    sprFillRect(buf, cx + 6, cy - 2, 3, 3);
}

static void sprClosedEyes(uint8_t* buf, int cx, int cy) {
    sprHLine(buf, cx - 9, cx - 5, cy);
    sprHLine(buf, cx + 5, cx + 9, cy);
}

static void sprXEyes(uint8_t* buf, int cx, int cy) {
    sprLine(buf, cx - 10, cy - 3, cx - 5, cy + 2);
    sprLine(buf, cx - 5, cy - 3, cx - 10, cy + 2);
    sprLine(buf, cx + 5, cy - 3, cx + 10, cy + 2);
    sprLine(buf, cx + 10, cy - 3, cx + 5, cy + 2);
}

static void sprStarEyes(uint8_t* buf, int cx, int cy) {
    sprSparkle(buf, cx - 7, cy, 3);
    sprSparkle(buf, cx + 7, cy, 3);
}

static void sprSmile(uint8_t* buf, int cx, int cy) {
    sprSetPixel(buf, cx - 4, cy + 6);
    sprSetPixel(buf, cx - 3, cy + 7);
    sprHLine(buf, cx - 2, cx + 2, cy + 8);
    sprSetPixel(buf, cx + 3, cy + 7);
    sprSetPixel(buf, cx + 4, cy + 6);
}

static void sprFrown(uint8_t* buf, int cx, int cy) {
    sprSetPixel(buf, cx - 4, cy + 9);
    sprSetPixel(buf, cx - 3, cy + 8);
    sprHLine(buf, cx - 2, cx + 2, cy + 7);
    sprSetPixel(buf, cx + 3, cy + 8);
    sprSetPixel(buf, cx + 4, cy + 9);
}

// ---------------------------------------------------------------------------
// Pixel-level drawing helpers for 24x24 icons
// ---------------------------------------------------------------------------
static inline void icoSet(uint8_t* buf, int x, int y) {
    if (x < 0 || x >= ICON_W || y < 0 || y >= ICON_H) return;
    buf[y * ICON_BPR + (x / 8)] |= (1 << (x % 8));
}

static void icoClear(uint8_t* buf) {
    memset(buf, 0, ICON_BYTES);
}

static void icoHLine(uint8_t* buf, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++) icoSet(buf, x, y);
}

static void icoVLine(uint8_t* buf, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++) icoSet(buf, x, y);
}

static void icoLine(uint8_t* buf, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        icoSet(buf, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void icoFillRect(uint8_t* buf, int x, int y, int w, int h) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            icoSet(buf, x + dx, y + dy);
}

static void icoFillCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                icoSet(buf, cx + x, cy + y);
}

static void icoDrawCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int a = 0; a < 360; a++) {
        float rad = a * 3.14159f / 180.0f;
        icoSet(buf, (int)(cx + r * cosf(rad) + 0.5f),
                     (int)(cy + r * sinf(rad) + 0.5f));
    }
}

// ---------------------------------------------------------------------------
// Icon generators — one per state
// ---------------------------------------------------------------------------

// IDLE: checkmark in circle
static void iconCheckCircle(uint8_t* buf) {
    icoClear(buf);
    icoDrawCircle(buf, 12, 12, 10);
    icoDrawCircle(buf, 12, 12, 9);
    icoLine(buf, 6, 12, 10, 17);
    icoLine(buf, 10, 17, 18, 7);
    icoLine(buf, 7, 12, 11, 17);
    icoLine(buf, 11, 17, 19, 7);
}

// THINKING: loading spinner (radial lines)
static void iconSpinner(uint8_t* buf) {
    icoClear(buf);
    int cx = 12, cy = 12;
    for (int i = 0; i < 8; i++) {
        float angle = i * 3.14159f / 4.0f;
        int x0 = cx + (int)(5 * cosf(angle));
        int y0 = cy + (int)(5 * sinf(angle));
        int x1 = cx + (int)(10 * cosf(angle));
        int y1 = cy + (int)(10 * sinf(angle));
        icoLine(buf, x0, y0, x1, y1);
        if (i < 4) {
            float perp = angle + 3.14159f / 2.0f;
            int px = (int)(cosf(perp));
            int py = (int)(sinf(perp));
            icoLine(buf, x0 + px, y0 + py, x1 + px, y1 + py);
        }
    }
}

// CODING: </> brackets
static void iconCodeBrackets(uint8_t* buf) {
    icoClear(buf);
    icoLine(buf, 8, 4, 2, 12);  icoLine(buf, 2, 12, 8, 20);
    icoLine(buf, 9, 4, 3, 12);  icoLine(buf, 3, 12, 9, 20);
    icoLine(buf, 13, 4, 10, 20); icoLine(buf, 14, 4, 11, 20);
    icoLine(buf, 15, 4, 21, 12); icoLine(buf, 21, 12, 15, 20);
    icoLine(buf, 16, 4, 22, 12); icoLine(buf, 22, 12, 16, 20);
}

// RUNNING: stick-figure running man
static void iconRunningMan(uint8_t* buf) {
    icoClear(buf);
    icoFillCircle(buf, 12, 4, 3);
    icoVLine(buf, 12, 7, 15); icoVLine(buf, 13, 7, 15);
    icoLine(buf, 12, 9, 6, 6);  icoLine(buf, 13, 9, 7, 6);
    icoLine(buf, 13, 9, 19, 12); icoLine(buf, 14, 9, 20, 12);
    icoLine(buf, 12, 15, 6, 22); icoLine(buf, 13, 15, 7, 22);
    icoLine(buf, 13, 15, 20, 20); icoLine(buf, 14, 15, 21, 20);
}

// WAITING: bold "?"
static void iconQuestionMark(uint8_t* buf) {
    icoClear(buf);
    icoHLine(buf, 8, 15, 2); icoHLine(buf, 7, 16, 3);
    icoFillRect(buf, 6, 4, 3, 2);
    icoFillRect(buf, 15, 4, 3, 2);
    icoFillRect(buf, 15, 6, 3, 2);
    icoFillRect(buf, 13, 8, 3, 2);
    icoFillRect(buf, 11, 10, 3, 3);
    icoFillRect(buf, 11, 13, 3, 3);
    icoFillRect(buf, 11, 18, 3, 3);
}

// SUCCESS: bold checkmark
static void iconCheckmark(uint8_t* buf) {
    icoClear(buf);
    icoLine(buf, 3, 12, 9, 19); icoLine(buf, 9, 19, 21, 5);
    icoLine(buf, 4, 12, 10, 19); icoLine(buf, 10, 19, 22, 5);
    icoLine(buf, 5, 12, 11, 19); icoLine(buf, 11, 19, 23, 5);
}

// ERROR: X mark
static void iconXMark(uint8_t* buf) {
    icoClear(buf);
    icoLine(buf, 4, 4, 20, 20); icoLine(buf, 5, 4, 21, 20);
    icoLine(buf, 4, 5, 20, 21);
    icoLine(buf, 20, 4, 4, 20); icoLine(buf, 21, 4, 5, 20);
    icoLine(buf, 20, 5, 4, 21);
}

// SLEEPING: "zzz"
static void iconZzz(uint8_t* buf) {
    icoClear(buf);
    icoHLine(buf, 10, 20, 3); icoHLine(buf, 11, 21, 4);
    icoLine(buf, 20, 4, 10, 12); icoLine(buf, 21, 4, 11, 12);
    icoHLine(buf, 10, 20, 12); icoHLine(buf, 11, 21, 13);
    icoHLine(buf, 5, 12, 8);
    icoLine(buf, 12, 8, 5, 13);
    icoHLine(buf, 5, 12, 13);
    icoHLine(buf, 2, 7, 16);
    icoLine(buf, 7, 16, 2, 19);
    icoHLine(buf, 2, 7, 19);
}

// ---------------------------------------------------------------------------
// Generate all icons
// ---------------------------------------------------------------------------
static void generateIcons() {
    iconCheckCircle(iconPool[STATE_IDLE]);
    iconSpinner(iconPool[STATE_THINKING]);
    iconCodeBrackets(iconPool[STATE_CODING]);
    iconRunningMan(iconPool[STATE_RUNNING]);
    iconQuestionMark(iconPool[STATE_WAITING]);
    iconCheckmark(iconPool[STATE_SUCCESS]);
    iconXMark(iconPool[STATE_ERROR]);
    iconZzz(iconPool[STATE_SLEEPING]);
}

// ---------------------------------------------------------------------------
// Generate mascot sprites (one static frame per state)
// ---------------------------------------------------------------------------
static void generateSprites() {
    const int cx = 32, cy = 30, bodyR = 20;

    // IDLE: normal eyes + smile
    { uint8_t* b = spritePool[STATE_IDLE]; sprClear(b);
      sprDrawBody(b, cx, cy, bodyR); sprDotEyes(b, cx, cy); sprSmile(b, cx, cy);
      sprFillRect(b, cx-8, cy+bodyR+1, 5, 3); sprFillRect(b, cx+4, cy+bodyR+1, 5, 3);
      stateSprite[STATE_IDLE] = b; }

    // THINKING: closed eyes + sparkles
    { uint8_t* b = spritePool[STATE_THINKING]; sprClear(b);
      sprDrawBody(b, cx, cy, bodyR); sprClosedEyes(b, cx, cy);
      sprHLine(b, cx-2, cx+2, cy+7);
      sprSparkle(b, cx-5, cy-bodyR-6, 2); sprSparkle(b, cx+8, cy-bodyR-4, 2);
      sprFillRect(b, cx-8, cy+bodyR+1, 5, 3); sprFillRect(b, cx+4, cy+bodyR+1, 5, 3);
      stateSprite[STATE_THINKING] = b; }

    // CODING: looking down + keyboard
    { uint8_t* b = spritePool[STATE_CODING]; sprClear(b);
      sprDrawBody(b, cx, cy+2, bodyR);
      sprFillRect(b, cx-8, cy+2, 3, 2); sprFillRect(b, cx+6, cy+2, 3, 2);
      sprHLine(b, cx-1, cx+1, cy+9);
      sprDrawRect(b, cx-14, cy+bodyR+5, 28, 8);
      for (int k = 0; k < 5; k++) sprDrawRect(b, cx-12+k*5, cy+bodyR+6, 4, 3);
      for (int k = 0; k < 4; k++) sprDrawRect(b, cx-10+k*5, cy+bodyR+9, 4, 3);
      sprLine(b, cx-10, cy+bodyR-2, cx-8, cy+bodyR+6);
      sprLine(b, cx+10, cy+bodyR-2, cx+6, cy+bodyR+4);
      stateSprite[STATE_CODING] = b; }

    // RUNNING: bouncing + motion lines
    { uint8_t* b = spritePool[STATE_RUNNING]; sprClear(b);
      sprDrawBody(b, cx, cy-2, bodyR); sprDotEyes(b, cx, cy-2);
      sprDrawCircle(b, cx, cy+5, 2);
      sprLine(b, cx-5, cy-2+bodyR, cx-12, cy-2+bodyR+10);
      sprLine(b, cx+5, cy-2+bodyR, cx+8, cy-2+bodyR+8);
      sprLine(b, cx-bodyR, cy-4, cx-bodyR-6, cy+2);
      sprLine(b, cx+bodyR, cy-4, cx+bodyR+4, cy-8);
      sprHLine(b, cx-bodyR-10, cx-bodyR-5, cy-5);
      sprHLine(b, cx-bodyR-12, cx-bodyR-6, cy);
      sprHLine(b, cx-bodyR-9, cx-bodyR-4, cy+5);
      stateSprite[STATE_RUNNING] = b; }

    // WAITING: big eyes + ? bubble
    { uint8_t* b = spritePool[STATE_WAITING]; sprClear(b);
      sprDrawBody(b, cx, cy, bodyR);
      sprFillRect(b, cx-9, cy-3, 4, 4); sprFillRect(b, cx+6, cy-3, 4, 4);
      sprClearPixel(b, cx-8, cy-2); sprClearPixel(b, cx+7, cy-2);
      sprHLine(b, cx-2, cx+2, cy+7);
      sprFillRect(b, cx-8, cy+bodyR+1, 5, 3); sprFillRect(b, cx+4, cy+bodyR+1, 5, 3);
      sprDrawRect(b, cx+12, cy-bodyR-8, 12, 11);
      sprSetPixel(b, cx+15, cy-bodyR+3); sprSetPixel(b, cx+14, cy-bodyR+4);
      sprSetPixel(b, cx+13, cy-bodyR+5);
      sprHLine(b, cx+16, cx+18, cy-bodyR-6);
      sprSetPixel(b, cx+19, cy-bodyR-5); sprSetPixel(b, cx+15, cy-bodyR-5);
      sprSetPixel(b, cx+19, cy-bodyR-4); sprSetPixel(b, cx+18, cy-bodyR-3);
      sprSetPixel(b, cx+17, cy-bodyR-2); sprSetPixel(b, cx+17, cy-bodyR-1);
      sprSetPixel(b, cx+17, cy-bodyR+1);
      stateSprite[STATE_WAITING] = b; }

    // SUCCESS: star eyes + big smile + sparkles
    { uint8_t* b = spritePool[STATE_SUCCESS]; sprClear(b);
      sprDrawBody(b, cx, cy-4, bodyR); sprStarEyes(b, cx, cy-4);
      sprSmile(b, cx, cy-4); sprHLine(b, cx-3, cx+3, cy-4+7);
      sprLine(b, cx-bodyR, cy-9, cx-bodyR-8, cy-18);
      sprLine(b, cx+bodyR, cy-9, cx+bodyR+8, cy-18);
      sprSparkle(b, cx-bodyR-4, cy-4-bodyR-2, 2);
      sprSparkle(b, cx+bodyR+4, cy-4-bodyR-2, 2);
      sprSparkle(b, cx, cy-4-bodyR-6, 3);
      sprFillRect(b, cx-8, cy-4+bodyR+1, 5, 3);
      sprFillRect(b, cx+4, cy-4+bodyR+1, 5, 3);
      stateSprite[STATE_SUCCESS] = b; }

    // ERROR: X eyes + frown
    { uint8_t* b = spritePool[STATE_ERROR]; sprClear(b);
      sprDrawBody(b, cx, cy, bodyR); sprXEyes(b, cx, cy); sprFrown(b, cx, cy);
      sprFillRect(b, cx-8, cy+bodyR+1, 5, 3); sprFillRect(b, cx+4, cy+bodyR+1, 5, 3);
      stateSprite[STATE_ERROR] = b; }

    // SLEEPING: closed eyes + Z bubbles
    { uint8_t* b = spritePool[STATE_SLEEPING]; sprClear(b);
      sprDrawBody(b, cx, cy, bodyR); sprClosedEyes(b, cx, cy);
      sprSetPixel(b, cx-2, cy+7); sprHLine(b, cx-1, cx+1, cy+8);
      sprSetPixel(b, cx+2, cy+7);
      sprFillRect(b, cx-8, cy+bodyR+1, 5, 3); sprFillRect(b, cx+4, cy+bodyR+1, 5, 3);
      int zx = cx+bodyR+2, zy = cy-bodyR+5;
      sprHLine(b, zx, zx+3, zy); sprLine(b, zx+3, zy, zx, zy+3);
      sprHLine(b, zx, zx+3, zy+3);
      sprHLine(b, zx+5, zx+9, zy-7); sprLine(b, zx+9, zy-7, zx+5, zy-3);
      sprHLine(b, zx+5, zx+9, zy-3);
      stateSprite[STATE_SLEEPING] = b; }
}

// ---------------------------------------------------------------------------
// Init: generate sprites and icons at boot
// ---------------------------------------------------------------------------
static void initAnimations() {
    generateSprites();
    generateIcons();
}
