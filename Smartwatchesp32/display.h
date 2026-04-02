#ifndef _display_h
#define _display_h

#include <Arduino.h>
#include <pgmspace.h>
#include "constants.h"
#include "oled_driver.h"   // your bit-bang I2C driver

// ── Display setup ─────────────────────────────────────────────────────────────
void setupDisplay();

// ── FPS / timing ─────────────────────────────────────────────────────────────
void   fps();
double getActualFps();

// ── Drawing primitives ────────────────────────────────────────────────────────
bool getGradientPixel(uint8_t x, uint8_t y, uint8_t i);
void fadeScreen(uint8_t intensity, bool color = 0);
void drawByte(uint8_t x, uint8_t y, uint8_t b);
uint8_t getByte(uint8_t x, uint8_t y);
void drawPixel(int8_t x, int8_t y, bool color, bool raycasterViewport = false);
void drawVLine(uint8_t x, int8_t start_y, int8_t end_y, uint8_t intensity);
void drawSprite(int8_t x, int8_t y,
                const uint8_t bitmap[], const uint8_t mask[],
                int16_t w, int16_t h, uint8_t sprite, double distance);

// ── Text ──────────────────────────────────────────────────────────────────────
void drawChar(int8_t x, int8_t y, char ch);
void drawText(int8_t x, int8_t y, char* txt, uint8_t space = 1);
void drawText(int8_t x, int8_t y, const __FlashStringHelper* txt, uint8_t space = 1);
void drawText(uint8_t x, uint8_t y, uint8_t num);

// ── HUD / flush ──────────────────────────────────────────────────────────────
// Call once per frame after all drawing is done.
// invertDisplay() is implemented here as a full software invert of OLED_GRAM.
void displayFlush();
void displayInvert(bool invert);
void displayClearViewport(); // clears only the 3-D portion of OLED_GRAM

// ── Globals used by the game loop ─────────────────────────────────────────────
extern double   delta;
extern uint32_t lastFrameTime;
extern uint8_t  zbuffer[ZBUFFER_SIZE];

// Direct pointer to OLED_GRAM so the game loop can memset the viewport fast
// (equivalent to the original display_buf in OPTIMIZE_SSD1306 mode)
#define display_buf  (&OLED_GRAM[0][0])

#endif // _display_h
