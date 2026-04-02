/*
 * display.cpp
 * Replaces the original Adafruit_SSD1306-based display layer.
 * All drawing goes into OLED_GRAM[][]; call displayFlush() to push to hardware.
 *
 * OLED_GRAM layout (from oled_driver.h):
 *   OLED_GRAM[col][page]  col = x (0-127), page = y/8 (0-7)
 *   Bit n in a byte = pixel at y = page*8 + n
 *
 * This matches the SSD1306 'horizontal pages' memory model, so all the
 * original bit-math from display.cpp is preserved unchanged.
 */

#include "display.h"
#include "sprites.h"   // bmp_font, gradient, CHAR_* constants
#include "constants.h"
#include "oled_driver.h"

// ── Read F()-string character ─────────────────────────────────────────────────
#define F_char(ifsh, ch) pgm_read_byte(reinterpret_cast<PGM_P>(ifsh) + (ch))

// ── Fast bit read from PROGMEM bytes (left-to-right) ─────────────────────────
const static uint8_t PROGMEM bit_mask[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
#define read_bit(b, n)  ((b) & pgm_read_byte(bit_mask + (n)) ? 1 : 0)

// ── Globals ───────────────────────────────────────────────────────────────────
double   delta         = 1.0;
uint32_t lastFrameTime = 0;
uint8_t  zbuffer[ZBUFFER_SIZE];

// ─────────────────────────────────────────────────────────────────────────────
void setupDisplay() {
    OLED_Init();
    OLED_Clear();
    memset(zbuffer, 0xFF, ZBUFFER_SIZE);
}

// ─────────────────────────────────────────────────────────────────────────────
void fps() {
    while (millis() - lastFrameTime < (uint32_t)FRAME_TIME);
    delta         = (double)(millis() - lastFrameTime) / FRAME_TIME;
    lastFrameTime = millis();
}

double getActualFps() {
    return 1000.0 / (FRAME_TIME * delta);
}

// ─────────────────────────────────────────────────────────────────────────────
// Write a raw byte directly into the page buffer at (x, y) where y is already
// page-aligned (i.e. y must be a multiple of 8).
void drawByte(uint8_t x, uint8_t y, uint8_t b) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    OLED_GRAM[x][y / 8] = b;
}

uint8_t getByte(uint8_t x, uint8_t y) {
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return 0;
    return OLED_GRAM[x][y / 8];
}

// ─────────────────────────────────────────────────────────────────────────────
void drawPixel(int8_t x, int8_t y, bool color, bool raycasterViewport) {
    if (x < 0 || x >= SCREEN_WIDTH) return;
    if (y < 0 || y >= (raycasterViewport ? (int8_t)RENDER_HEIGHT : (int8_t)SCREEN_HEIGHT)) return;

    if (color)
        OLED_GRAM[x][y / 8] |=  (1 << (y & 7));
    else
        OLED_GRAM[x][y / 8] &= ~(1 << (y & 7));
}

// ─────────────────────────────────────────────────────────────────────────────
bool getGradientPixel(uint8_t x, uint8_t y, uint8_t i) {
    if (i == 0) return false;
    if (i >= GRADIENT_COUNT - 1) return true;

    uint8_t index = (uint8_t)max((int)0, min((int)(GRADIENT_COUNT - 1), (int)i))
                    * GRADIENT_WIDTH * GRADIENT_HEIGHT
                    + y * GRADIENT_WIDTH % (GRADIENT_WIDTH * GRADIENT_HEIGHT)
                    + x / GRADIENT_HEIGHT % GRADIENT_WIDTH;

    return read_bit(pgm_read_byte(gradient + index), x % 8);
}

// ─────────────────────────────────────────────────────────────────────────────
void fadeScreen(uint8_t intensity, bool color) {
    for (uint8_t x = 0; x < SCREEN_WIDTH; x++)
        for (uint8_t y = 0; y < SCREEN_HEIGHT; y++)
            if (getGradientPixel(x, y, intensity))
                drawPixel((int8_t)x, (int8_t)y, color, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertical line with dithered intensity — the raycaster's hot path.
// Uses raw byte writes to avoid per-pixel overhead.
void drawVLine(uint8_t x, int8_t start_y, int8_t end_y, uint8_t intensity) {
    int8_t lower_y  = max(min(start_y, end_y), (int8_t)0);
    int8_t higher_y = min(max(start_y, end_y), (int8_t)(RENDER_HEIGHT - 1));

    uint8_t bp, b, c, y;
    for (c = 0; c < RES_DIVIDER; c++) {
        y = (uint8_t)lower_y;
        b = 0;
        while (y <= (uint8_t)higher_y) {
            bp = y % 8;
            b |= getGradientPixel(x + c, y, intensity) << bp;
            if (bp == 7) {
                drawByte(x + c, y, b);
                b = 0;
            }
            y++;
        }
        if (bp != 7) drawByte(x + c, y - 1, b);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void drawSprite(
    int8_t x, int8_t y,
    const uint8_t bitmap[], const uint8_t mask[],
    int16_t w, int16_t h,
    uint8_t sprite, double distance)
{
    uint8_t tw = (double)w / distance;
    uint8_t th = (double)h / distance;
    uint8_t byte_width  = w / 8;
    uint8_t pixel_size  = (uint8_t)max((int)1, (int)(1.0 / distance));
    uint16_t sprite_offset = byte_width * h * sprite;

    int zbuf_idx = min(max((int)x, (int)0), (int)(ZBUFFER_SIZE - 1)) / Z_RES_DIVIDER;
    if (zbuffer[zbuf_idx] < (uint8_t)(distance * DISTANCE_MULTIPLIER)) return;

    for (uint8_t ty = 0; ty < th; ty += pixel_size) {
        if (y + ty < 0 || y + ty >= RENDER_HEIGHT) continue;
        uint8_t sy = (uint8_t)(ty * distance);

        for (uint8_t tx = 0; tx < tw; tx += pixel_size) {
            uint8_t sx = (uint8_t)(tx * distance);
            uint16_t byte_offset = sprite_offset + sy * byte_width + sx / 8;
            if (x + tx < 0 || x + tx >= SCREEN_WIDTH) continue;

            bool maskPixel = read_bit(pgm_read_byte(mask + byte_offset), sx % 8);
            if (maskPixel) {
                bool pixel = read_bit(pgm_read_byte(bitmap + byte_offset), sx % 8);
                for (uint8_t ox = 0; ox < pixel_size; ox++)
                    for (uint8_t oy = 0; oy < pixel_size; oy++)
                        drawPixel((int8_t)(x + tx + ox), (int8_t)(y + ty + oy), pixel, true);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 4×6 bitmap font from sprites.h
void drawChar(int8_t x, int8_t y, char ch) {
    uint8_t c = 0, n, bOffset, b, line = 0;
    while (CHAR_MAP[c] != ch && CHAR_MAP[c] != '\0') c++;
    bOffset = c / 2;
    for (; line < CHAR_HEIGHT; line++) {
        b = pgm_read_byte(bmp_font + (line * bmp_font_width + bOffset));
        for (n = 0; n < CHAR_WIDTH; n++)
            if (read_bit(b, (c % 2 == 0 ? 0 : 4) + n))
                drawPixel(x + n, y + line, 1, false);
    }
}

void drawText(int8_t x, int8_t y, char* txt, uint8_t space) {
    uint8_t pos = (uint8_t)x, i = 0;
    char ch;
    while ((ch = txt[i]) != '\0') {
        drawChar(pos, y, ch);
        i++;
        pos += CHAR_WIDTH + space;
        if (pos > SCREEN_WIDTH) return;
    }
}

void drawText(int8_t x, int8_t y, const __FlashStringHelper* txt_p, uint8_t space) {
    uint8_t pos = (uint8_t)x, i = 0;
    char ch;
    while (pos < SCREEN_WIDTH && (ch = F_char(txt_p, i)) != '\0') {
        drawChar(pos, y, ch);
        i++;
        pos += CHAR_WIDTH + space;
    }
}

void drawText(uint8_t x, uint8_t y, uint8_t num) {
    char buf[4];
    itoa(num, buf, 10);
    drawText((int8_t)x, (int8_t)y, buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Flush OLED_GRAM to hardware via your oled_driver (dirty-page aware)
void displayFlush() {
    OLED_Flush();
}

// ─────────────────────────────────────────────────────────────────────────────
// Software display invert — flip every bit in OLED_GRAM.
// The SSD1309 supports hardware invert (0xA7) but flipping in software is
// equivalent and doesn't require a separate command path.
void displayInvert(bool invert) {
    if (invert)
        OLED_WR_Byte(0xA7, 0);  // OLED_CMD = 0
    else
        OLED_WR_Byte(0xA6, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear only the raycaster viewport rows (pages 0..RENDER_HEIGHT/8-1)
// Equivalent to the original: memset(display_buf, 0, SCREEN_WIDTH*(RENDER_HEIGHT/8))
void displayClearViewport() {
    // RENDER_HEIGHT = 56, so pages 0..6 (7 pages × 128 columns)
    const uint8_t pages = RENDER_HEIGHT / 8;
    for (uint8_t col = 0; col < SCREEN_WIDTH; col++)
        for (uint8_t page = 0; page < pages; page++)
            OLED_GRAM[col][page] = 0;
}
