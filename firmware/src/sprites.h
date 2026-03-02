#pragma once
// ============================================================================
// sprites.h — Placeholder XBM sprite data for Claude Pet
// 64x64 pixel frames (512 bytes each), 1-bit packed, XBM format
// Each sprite is a round "Claude orb" character with expressive features
// ============================================================================

#include <stdint.h>

// ---------------------------------------------------------------------------
// Sprite dimensions
// ---------------------------------------------------------------------------
#define SPRITE_W 64
#define SPRITE_H 64
#define SPRITE_BYTES (SPRITE_W * SPRITE_H / 8) // 512 bytes per frame

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
// Animation definition
// ---------------------------------------------------------------------------
struct AnimationDef {
    AnimState     state;
    uint8_t       frameCount;
    uint16_t      frameDurationMs;
    bool          looping;       // false = play once then -> IDLE
    const uint8_t* const* frames; // array of pointers to frame data
};

// ---------------------------------------------------------------------------
// Helper: programmatic sprite generation at compile time is impractical,
// so we use PROGMEM arrays with hand-crafted byte patterns.
//
// XBM format: each byte represents 8 horizontal pixels, LSB = leftmost.
// 1 = black pixel, 0 = white pixel.
// Row stride = 64/8 = 8 bytes per row, 64 rows = 512 bytes.
//
// The character is a round orb (~48px diameter) centered in the 64x64 frame,
// with various features drawn for each state/frame.
// ---------------------------------------------------------------------------

// Utility: We generate sprites programmatically at startup instead of
// storing 36 * 512 = 18KB of PROGMEM constants. This keeps the binary
// smaller and the sprites more maintainable.

// Runtime sprite buffer — all frames are generated into this pool at boot
// Max frames across all states = 6+4+4+4+4+6+4+4 = 36
#define TOTAL_FRAMES 36
static uint8_t spritePool[TOTAL_FRAMES][SPRITE_BYTES];
static const uint8_t* framePointers[TOTAL_FRAMES];

// Per-state frame pointer arrays
static const uint8_t* idleFrames[6];
static const uint8_t* thinkingFrames[4];
static const uint8_t* codingFrames[4];
static const uint8_t* runningFrames[4];
static const uint8_t* waitingFrames[4];
static const uint8_t* successFrames[6];
static const uint8_t* errorFrames[4];
static const uint8_t* sleepingFrames[4];

// ---------------------------------------------------------------------------
// Pixel-level drawing helpers for sprite generation
// ---------------------------------------------------------------------------
static inline void sprSetPixel(uint8_t* buf, int x, int y) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return;
    // XBM: LSB-first within each byte
    buf[y * 8 + (x / 8)] |= (1 << (x % 8));
}

static inline void sprClearPixel(uint8_t* buf, int x, int y) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return;
    buf[y * 8 + (x / 8)] &= ~(1 << (x % 8));
}

static void sprClear(uint8_t* buf) {
    memset(buf, 0, SPRITE_BYTES);
}

// Draw filled circle (Bresenham midpoint)
static void sprFillCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                sprSetPixel(buf, cx + x, cy + y);
            }
        }
    }
}

// Draw circle outline
static void sprDrawCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int a = 0; a < 360; a++) {
        float rad = a * 3.14159f / 180.0f;
        int x = (int)(cx + r * cosf(rad) + 0.5f);
        int y = (int)(cy + r * sinf(rad) + 0.5f);
        sprSetPixel(buf, x, y);
    }
}

// Draw horizontal line
static void sprHLine(uint8_t* buf, int x0, int x1, int y) {
    for (int x = x0; x <= x1; x++) sprSetPixel(buf, x, y);
}

// Draw vertical line
static void sprVLine(uint8_t* buf, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++) sprSetPixel(buf, x, y);
}

// Draw line (Bresenham)
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

// Draw filled rectangle
static void sprFillRect(uint8_t* buf, int x, int y, int w, int h) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            sprSetPixel(buf, x + dx, y + dy);
}

// Draw rectangle outline
static void sprDrawRect(uint8_t* buf, int x, int y, int w, int h) {
    sprHLine(buf, x, x + w - 1, y);
    sprHLine(buf, x, x + w - 1, y + h - 1);
    sprVLine(buf, x, y, y + h - 1);
    sprVLine(buf, x + w - 1, y, y + h - 1);
}

// Clear a filled circle (white out)
static void sprClearCircle(uint8_t* buf, int cx, int cy, int r) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                sprClearPixel(buf, cx + x, cy + y);
            }
        }
    }
}

// Draw a small 4-point sparkle
static void sprSparkle(uint8_t* buf, int cx, int cy, int size) {
    sprVLine(buf, cx, cy - size, cy + size);
    sprHLine(buf, cx - size, cx + size, cy);
    // Diagonal accents
    if (size > 2) {
        sprSetPixel(buf, cx - 1, cy - 1);
        sprSetPixel(buf, cx + 1, cy - 1);
        sprSetPixel(buf, cx - 1, cy + 1);
        sprSetPixel(buf, cx + 1, cy + 1);
    }
}

// Draw small "Z" letter
static void sprDrawZ(uint8_t* buf, int x, int y, int size) {
    sprHLine(buf, x, x + size, y);
    sprLine(buf, x + size, y, x, y + size);
    sprHLine(buf, x, x + size, y + size);
}

// Draw "?" character
static void sprDrawQuestion(uint8_t* buf, int x, int y) {
    // Top arc of ?
    sprHLine(buf, x + 1, x + 3, y);
    sprSetPixel(buf, x + 4, y + 1);
    sprSetPixel(buf, x, y + 1);
    sprSetPixel(buf, x + 4, y + 2);
    sprSetPixel(buf, x + 3, y + 3);
    sprSetPixel(buf, x + 2, y + 4);
    sprSetPixel(buf, x + 2, y + 5);
    // Dot
    sprSetPixel(buf, x + 2, y + 7);
}

// Draw dot eyes (normal open eyes)
static void sprDotEyes(uint8_t* buf, int cx, int cy) {
    // Left eye: filled 3x3
    sprFillRect(buf, cx - 8, cy - 2, 3, 3);
    // Right eye: filled 3x3
    sprFillRect(buf, cx + 6, cy - 2, 3, 3);
}

// Draw closed eyes (horizontal lines)
static void sprClosedEyes(uint8_t* buf, int cx, int cy) {
    sprHLine(buf, cx - 9, cx - 5, cy);
    sprHLine(buf, cx + 5, cx + 9, cy);
}

// Draw happy curved eyes (arcs)
static void sprHappyEyes(uint8_t* buf, int cx, int cy) {
    // Left eye: upside-down arc
    sprSetPixel(buf, cx - 9, cy + 1);
    sprSetPixel(buf, cx - 8, cy);
    sprSetPixel(buf, cx - 7, cy - 1);
    sprSetPixel(buf, cx - 6, cy);
    sprSetPixel(buf, cx - 5, cy + 1);
    // Right eye
    sprSetPixel(buf, cx + 5, cy + 1);
    sprSetPixel(buf, cx + 6, cy);
    sprSetPixel(buf, cx + 7, cy - 1);
    sprSetPixel(buf, cx + 8, cy);
    sprSetPixel(buf, cx + 9, cy + 1);
}

// Draw X eyes (error state)
static void sprXEyes(uint8_t* buf, int cx, int cy) {
    // Left X
    sprLine(buf, cx - 10, cy - 3, cx - 5, cy + 2);
    sprLine(buf, cx - 5, cy - 3, cx - 10, cy + 2);
    // Right X
    sprLine(buf, cx + 5, cy - 3, cx + 10, cy + 2);
    sprLine(buf, cx + 10, cy - 3, cx + 5, cy + 2);
}

// Draw sparkle-star eyes (success)
static void sprStarEyes(uint8_t* buf, int cx, int cy) {
    sprSparkle(buf, cx - 7, cy, 3);
    sprSparkle(buf, cx + 7, cy, 3);
}

// Draw small mouth (smile)
static void sprSmile(uint8_t* buf, int cx, int cy) {
    sprSetPixel(buf, cx - 4, cy + 6);
    sprSetPixel(buf, cx - 3, cy + 7);
    sprHLine(buf, cx - 2, cx + 2, cy + 8);
    sprSetPixel(buf, cx + 3, cy + 7);
    sprSetPixel(buf, cx + 4, cy + 6);
}

// Draw small frown
static void sprFrown(uint8_t* buf, int cx, int cy) {
    sprSetPixel(buf, cx - 4, cy + 9);
    sprSetPixel(buf, cx - 3, cy + 8);
    sprHLine(buf, cx - 2, cx + 2, cy + 7);
    sprSetPixel(buf, cx + 3, cy + 8);
    sprSetPixel(buf, cx + 4, cy + 9);
}

// Draw the base orb body (the round Claude character)
static void sprDrawBody(uint8_t* buf, int cx, int cy, int r) {
    // Filled body circle
    sprFillCircle(buf, cx, cy, r);
    // Clear inside for features (hollow with thick border)
    sprClearCircle(buf, cx, cy, r - 3);
    // Draw outline again for crisp border
    sprDrawCircle(buf, cx, cy, r);
    sprDrawCircle(buf, cx, cy, r - 1);
}

// Draw a speech bubble outline at position
static void sprBubble(uint8_t* buf, int x, int y, int w, int h) {
    sprDrawRect(buf, x, y, w, h);
    // Small triangle pointer at bottom-left
    sprSetPixel(buf, x + 3, y + h);
    sprSetPixel(buf, x + 2, y + h + 1);
    sprSetPixel(buf, x + 1, y + h + 2);
}

// Draw a sweat drop
static void sprSweatDrop(uint8_t* buf, int x, int y) {
    sprSetPixel(buf, x, y);
    sprSetPixel(buf, x - 1, y + 1);
    sprSetPixel(buf, x + 1, y + 1);
    sprSetPixel(buf, x - 1, y + 2);
    sprSetPixel(buf, x + 1, y + 2);
    sprSetPixel(buf, x, y + 3);
}

// Draw a small spiral
static void sprSpiral(uint8_t* buf, int cx, int cy) {
    sprSetPixel(buf, cx, cy);
    sprSetPixel(buf, cx + 1, cy);
    sprSetPixel(buf, cx + 1, cy - 1);
    sprSetPixel(buf, cx, cy - 1);
    sprSetPixel(buf, cx - 1, cy - 1);
    sprSetPixel(buf, cx - 1, cy);
    sprSetPixel(buf, cx - 1, cy + 1);
    sprSetPixel(buf, cx, cy + 1);
    sprSetPixel(buf, cx + 1, cy + 1);
    sprSetPixel(buf, cx + 2, cy + 1);
    sprSetPixel(buf, cx + 2, cy);
    sprSetPixel(buf, cx + 2, cy - 1);
    sprSetPixel(buf, cx + 2, cy - 2);
    sprSetPixel(buf, cx + 1, cy - 2);
    sprSetPixel(buf, cx, cy - 2);
    sprSetPixel(buf, cx - 1, cy - 2);
    sprSetPixel(buf, cx - 2, cy - 2);
    sprSetPixel(buf, cx - 2, cy - 1);
    sprSetPixel(buf, cx - 2, cy);
    sprSetPixel(buf, cx - 2, cy + 1);
    sprSetPixel(buf, cx - 2, cy + 2);
}

// ---------------------------------------------------------------------------
// Generate all sprite frames at runtime (called once at boot)
// ---------------------------------------------------------------------------
static void generateSprites() {
    int frameIdx = 0;
    const int cx = 32; // Center x of 64x64 sprite
    const int cy = 30; // Center y (slightly above center for feet room)
    const int bodyR = 20; // Body radius

    // ---- IDLE (6 frames, breathing + blink) ----
    for (int f = 0; f < 6; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        // Breathing: body shifts down 0-1-2-1-0 pixels
        int breathOffsets[] = {0, 0, 1, 2, 1, 0};
        int bo = breathOffsets[f];

        sprDrawBody(buf, cx, cy + bo, bodyR);

        // Eyes: blink on frames 4-5
        if (f == 4) {
            // Half-closed
            sprHLine(buf, cx - 9, cx - 5, cy + bo - 1);
            sprHLine(buf, cx - 9, cx - 5, cy + bo);
            sprHLine(buf, cx + 5, cx + 9, cy + bo - 1);
            sprHLine(buf, cx + 5, cx + 9, cy + bo);
        } else if (f == 5) {
            sprClosedEyes(buf, cx, cy + bo);
        } else {
            sprDotEyes(buf, cx, cy + bo);
        }

        sprSmile(buf, cx, cy + bo);

        // Small feet at bottom
        sprFillRect(buf, cx - 8, cy + bo + bodyR + 1, 5, 3);
        sprFillRect(buf, cx + 4, cy + bo + bodyR + 1, 5, 3);

        idleFrames[f] = buf;
        frameIdx++;
    }

    // ---- THINKING (4 frames, sparkle dots rotating) ----
    for (int f = 0; f < 4; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        sprDrawBody(buf, cx, cy, bodyR);
        sprClosedEyes(buf, cx, cy);

        // Small contemplative mouth
        sprHLine(buf, cx - 2, cx + 2, cy + 7);

        // Rotating sparkle positions around head
        int sparklePositions[][2] = {
            {cx - 5, cy - bodyR - 6},  // top-left
            {cx + 8, cy - bodyR - 4},  // top-right
            {cx + 12, cy - bodyR + 2}, // right
            {cx - 12, cy - bodyR + 2}  // left
        };
        // Show 2 sparkles at a time, rotating position each frame
        for (int s = 0; s < 2; s++) {
            int idx = (f + s * 2) % 4;
            sprSparkle(buf, sparklePositions[idx][0], sparklePositions[idx][1], 2);
        }

        // Feet
        sprFillRect(buf, cx - 8, cy + bodyR + 1, 5, 3);
        sprFillRect(buf, cx + 4, cy + bodyR + 1, 5, 3);

        thinkingFrames[f] = buf;
        frameIdx++;
    }

    // ---- CODING (4 frames, looking down, hands on keyboard) ----
    for (int f = 0; f < 4; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        // Body slightly lower (looking down)
        sprDrawBody(buf, cx, cy + 2, bodyR);

        // Eyes looking down
        sprFillRect(buf, cx - 8, cy + 2, 3, 2);
        sprFillRect(buf, cx + 6, cy + 2, 3, 2);

        // Focused mouth
        sprHLine(buf, cx - 1, cx + 1, cy + 9);

        // Keyboard below
        sprDrawRect(buf, cx - 14, cy + bodyR + 5, 28, 8);
        // Keys
        for (int k = 0; k < 5; k++) {
            sprDrawRect(buf, cx - 12 + k * 5, cy + bodyR + 6, 4, 3);
        }
        for (int k = 0; k < 4; k++) {
            sprDrawRect(buf, cx - 10 + k * 5, cy + bodyR + 9, 4, 3);
        }

        // Arms/hands alternating on keyboard
        if (f % 2 == 0) {
            // Left hand pressing
            sprLine(buf, cx - 10, cy + bodyR - 2, cx - 8, cy + bodyR + 6);
            sprLine(buf, cx + 10, cy + bodyR - 2, cx + 6, cy + bodyR + 4);
        } else {
            // Right hand pressing
            sprLine(buf, cx - 10, cy + bodyR - 2, cx - 6, cy + bodyR + 4);
            sprLine(buf, cx + 10, cy + bodyR - 2, cx + 8, cy + bodyR + 6);
        }

        codingFrames[f] = buf;
        frameIdx++;
    }

    // ---- RUNNING (4 frames, running poses with sweat) ----
    for (int f = 0; f < 4; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        // Body bounces up/down
        int bounceY[] = {0, -2, 0, -2};
        int by = bounceY[f];

        sprDrawBody(buf, cx, cy + by, bodyR);

        // Determined eyes
        sprDotEyes(buf, cx, cy + by);

        // Open mouth (effort)
        sprDrawCircle(buf, cx, cy + by + 7, 2);

        // Legs in running poses
        if (f == 0 || f == 2) {
            // Left forward, right back
            sprLine(buf, cx - 5, cy + by + bodyR, cx - 12, cy + by + bodyR + 10);
            sprLine(buf, cx + 5, cy + by + bodyR, cx + 8, cy + by + bodyR + 8);
        } else {
            // Right forward, left back
            sprLine(buf, cx + 5, cy + by + bodyR, cx + 12, cy + by + bodyR + 10);
            sprLine(buf, cx - 5, cy + by + bodyR, cx - 8, cy + by + bodyR + 8);
        }

        // Arms swinging (opposite to legs)
        if (f == 0 || f == 2) {
            sprLine(buf, cx - bodyR, cy + by - 2, cx - bodyR - 6, cy + by + 4);
            sprLine(buf, cx + bodyR, cy + by - 2, cx + bodyR + 4, cy + by - 6);
        } else {
            sprLine(buf, cx - bodyR, cy + by - 2, cx - bodyR - 4, cy + by - 6);
            sprLine(buf, cx + bodyR, cy + by - 2, cx + bodyR + 6, cy + by + 4);
        }

        // Sweat drops
        if (f == 1 || f == 3) {
            sprSweatDrop(buf, cx + bodyR + 3, cy + by - bodyR + 2);
        }
        if (f == 2) {
            sprSweatDrop(buf, cx - bodyR - 3, cy + by - bodyR + 5);
        }

        // Motion lines behind
        sprHLine(buf, cx - bodyR - 10, cx - bodyR - 5, cy + by - 3);
        sprHLine(buf, cx - bodyR - 12, cx - bodyR - 6, cy + by + 2);
        sprHLine(buf, cx - bodyR - 9, cx - bodyR - 4, cy + by + 7);

        runningFrames[f] = buf;
        frameIdx++;
    }

    // ---- WAITING (4 frames, looking at viewer with "?" bubble) ----
    for (int f = 0; f < 4; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        sprDrawBody(buf, cx, cy, bodyR);

        // Big eyes looking forward
        sprFillRect(buf, cx - 9, cy - 3, 4, 4);
        sprFillRect(buf, cx + 6, cy - 3, 4, 4);
        // Eye highlights (clear a pixel in each)
        sprClearPixel(buf, cx - 8, cy - 2);
        sprClearPixel(buf, cx + 7, cy - 2);

        // Slight frown (uncertain)
        sprHLine(buf, cx - 2, cx + 2, cy + 7);

        // Feet
        sprFillRect(buf, cx - 8, cy + bodyR + 1, 5, 3);
        sprFillRect(buf, cx + 4, cy + bodyR + 1, 5, 3);

        // Speech bubble with "?" - bubble floats/bobs
        int bubbleY = (f % 2 == 0) ? -8 : -7;
        sprBubble(buf, cx + 12, cy - bodyR + bubbleY, 12, 11);
        sprDrawQuestion(buf, cx + 15, cy - bodyR + bubbleY + 2);

        waitingFrames[f] = buf;
        frameIdx++;
    }

    // ---- SUCCESS (6 frames, jump with sparkle eyes, play once) ----
    for (int f = 0; f < 6; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        // Jump arc: 0=ground, 1-2=rising, 3=peak, 4-5=landing
        int jumpY[] = {0, -4, -8, -10, -6, -2};
        int jy = jumpY[f];

        sprDrawBody(buf, cx, cy + jy, bodyR);

        // Star eyes!
        sprStarEyes(buf, cx, cy + jy);

        // Big smile
        sprSmile(buf, cx, cy + jy);
        // Extra smile line for big grin
        sprHLine(buf, cx - 3, cx + 3, cy + jy + 7);

        // Arms up in celebration
        sprLine(buf, cx - bodyR, cy + jy - 5, cx - bodyR - 8, cy + jy - 14);
        sprLine(buf, cx + bodyR, cy + jy - 5, cx + bodyR + 8, cy + jy - 14);

        // Sparkles around
        if (f >= 1 && f <= 4) {
            sprSparkle(buf, cx - bodyR - 4, cy + jy - bodyR - 2, 2);
            sprSparkle(buf, cx + bodyR + 4, cy + jy - bodyR - 2, 2);
        }
        if (f >= 2 && f <= 5) {
            sprSparkle(buf, cx, cy + jy - bodyR - 6, 3);
        }

        // Feet (ground shadow when jumping)
        if (jy < -2) {
            // Shadow on ground
            sprHLine(buf, cx - 6, cx + 6, cy + bodyR + 3);
        }
        // Feet
        sprFillRect(buf, cx - 8, cy + jy + bodyR + 1, 5, 3);
        sprFillRect(buf, cx + 4, cy + jy + bodyR + 1, 5, 3);

        successFrames[f] = buf;
        frameIdx++;
    }

    // ---- ERROR (4 frames, X eyes with dizzy spirals) ----
    for (int f = 0; f < 4; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        // Body wobbles left-right
        int wobbleX[] = {-1, 0, 1, 0};
        int wx = wobbleX[f];

        sprDrawBody(buf, cx + wx, cy, bodyR);

        // X eyes
        sprXEyes(buf, cx + wx, cy);

        // Dizzy frown
        sprFrown(buf, cx + wx, cy);

        // Dizzy spirals rotating around head
        int spiralAngle = f * 90;
        float rad1 = spiralAngle * 3.14159f / 180.0f;
        float rad2 = (spiralAngle + 180) * 3.14159f / 180.0f;
        int sx1 = cx + wx + (int)(15 * cosf(rad1));
        int sy1 = cy - 8 + (int)(8 * sinf(rad1));
        int sx2 = cx + wx + (int)(15 * cosf(rad2));
        int sy2 = cy - 8 + (int)(8 * sinf(rad2));
        sprSpiral(buf, sx1, sy1);
        sprSpiral(buf, sx2, sy2);

        // Feet (wobbly)
        sprFillRect(buf, cx + wx - 8, cy + bodyR + 1, 5, 3);
        sprFillRect(buf, cx + wx + 4, cy + bodyR + 1, 5, 3);

        errorFrames[f] = buf;
        frameIdx++;
    }

    // ---- SLEEPING (4 frames, closed eyes with Z bubbles) ----
    for (int f = 0; f < 4; f++) {
        uint8_t* buf = spritePool[frameIdx];
        sprClear(buf);

        sprDrawBody(buf, cx, cy, bodyR);

        // Closed peaceful eyes
        sprClosedEyes(buf, cx, cy);

        // Small peaceful smile
        sprSetPixel(buf, cx - 2, cy + 7);
        sprHLine(buf, cx - 1, cx + 1, cy + 8);
        sprSetPixel(buf, cx + 2, cy + 7);

        // Feet
        sprFillRect(buf, cx - 8, cy + bodyR + 1, 5, 3);
        sprFillRect(buf, cx + 4, cy + bodyR + 1, 5, 3);

        // Z bubbles floating up - different positions per frame
        // Small z, medium Z, large Z floating upward
        int zBaseX = cx + bodyR + 2;
        int zBaseY = cy - bodyR + 5;

        switch (f) {
            case 0:
                sprDrawZ(buf, zBaseX, zBaseY, 3);
                break;
            case 1:
                sprDrawZ(buf, zBaseX + 2, zBaseY - 3, 3);
                sprDrawZ(buf, zBaseX + 5, zBaseY - 9, 4);
                break;
            case 2:
                sprDrawZ(buf, zBaseX + 4, zBaseY - 6, 3);
                sprDrawZ(buf, zBaseX + 7, zBaseY - 13, 4);
                sprDrawZ(buf, zBaseX + 3, zBaseY - 19, 5);
                break;
            case 3:
                sprDrawZ(buf, zBaseX + 6, zBaseY - 9, 4);
                sprDrawZ(buf, zBaseX + 5, zBaseY - 17, 5);
                break;
        }

        sleepingFrames[f] = buf;
        frameIdx++;
    }
}

// ---------------------------------------------------------------------------
// Animation lookup table
// ---------------------------------------------------------------------------
static AnimationDef animations[STATE_COUNT];

static void initAnimations() {
    generateSprites();

    animations[STATE_IDLE] = {
        STATE_IDLE, 6, 500, true,
        idleFrames
    };
    animations[STATE_THINKING] = {
        STATE_THINKING, 4, 400, true,
        thinkingFrames
    };
    animations[STATE_CODING] = {
        STATE_CODING, 4, 300, true,
        codingFrames
    };
    animations[STATE_RUNNING] = {
        STATE_RUNNING, 4, 250, true,
        runningFrames
    };
    animations[STATE_WAITING] = {
        STATE_WAITING, 4, 600, true,
        waitingFrames
    };
    animations[STATE_SUCCESS] = {
        STATE_SUCCESS, 6, 300, false,  // play once
        successFrames
    };
    animations[STATE_ERROR] = {
        STATE_ERROR, 4, 400, true,
        errorFrames
    };
    animations[STATE_SLEEPING] = {
        STATE_SLEEPING, 4, 800, true,
        sleepingFrames
    };
}
