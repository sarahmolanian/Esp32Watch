#ifndef KEYBOARDESP_H
#define KEYBOARDESP_H

#include <Arduino.h>
#include "oled_driver.h"
#include "font.h"

// --- Pin config ---
#define BTN_DOWN    6
#define BTN_UP   7
#define BTN_SELECT  10

// --- Special key tokens ---
#define K_CAP  '\x01'
#define K_NUM  '\x02'
#define K_DEL  '\x03'
#define K_OK   '\x04'
#define K_PAD  '\x05'

// --- Grid ---
#define KEY_COLS  6
#define KEY_ROWS  5
#define KEY_TOTAL (KEY_COLS * KEY_ROWS)

// --- Display geometry ---
#define LABEL_Y  0
#define INPUT_Y  8
#define DIV_Y    16
#define KB_Y     17
#define CELL_H   9
#define CELL_W   21
#define KB_X     1

// --- Keyboard state struct ---
// All state lives here so multiple instances are possible
// and the caller can read the result directly.
struct KeyboardState {
  char    password[32];
  uint8_t pw_len;
  int     cur_pos;
  uint8_t cur_page;    // 0 = alpha, 1 = num/sym
  bool    caps_on;
  bool    confirmed;   // set true when user presses OK
};

// --- Public API ---

// Call once in setup()
void keyboard_init(KeyboardState* kb);

// Call every loop() — handles buttons, blink, redraws
// Returns true the moment the user confirms (OK pressed)
bool keyboard_update(KeyboardState* kb);

// Force a redraw (useful if your app switches screens and comes back)
void keyboard_draw(KeyboardState* kb);

// Reset state (clears password, goes back to page 0)
void keyboard_reset(KeyboardState* kb);

#endif