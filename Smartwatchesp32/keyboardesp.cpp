#include "keyboardesp.h"
#include "oled_driver.h"
#include "font.h"

// ============================================================
// Key layouts
// ============================================================
static const char ALPHA_KEYS[KEY_TOTAL] = {
  'A','B','C','D','E','F',
  'G','H','I','J','K','L',
  'M','N','O','P','Q','R',
  'S','T','U','V','W','X',
  'Y','Z',K_CAP,K_NUM,K_DEL,K_OK
};

static const char NUM_KEYS[KEY_TOTAL] = {
  '0','1','2','3','4','5',
  '6','7','8','9','+',' ',
  '-','*','/','=','!','?',
  '@','#','$','%','(',')',
  '.',',',K_CAP,K_NUM,K_DEL,K_OK
};

// ============================================================
// Internal state (blink + debounce)
// ============================================================
static unsigned long s_last_blink  = 0;
static bool          s_blink_on    = true;
static unsigned long s_btn_last[3] = {0, 0, 0};

#define DEBOUNCE_MS 350

// ============================================================
// Internal helpers
// ============================================================
static const char* _currentKeys(KeyboardState* kb) {
  return (kb->cur_page == 0) ? ALPHA_KEYS : NUM_KEYS;
}

static bool _btnPressed(uint8_t pin, uint8_t idx) {
  if (digitalRead(pin) == LOW) {
    unsigned long now = millis();
    if (now - s_btn_last[idx] > DEBOUNCE_MS) {
      s_btn_last[idx] = now;
      return true;
    }
  } else {
    s_btn_last[idx] = 0;
  }
  return false;
}

static void _drawChar(uint8_t x, uint8_t y, char c, bool invert) {
  if (c < 32 || c > 127) return;
  uint8_t idx = c - 32;
  for (uint8_t col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&font5x8[idx][col]);
    for (uint8_t row = 0; row < 8; row++) {
      bool lit = (line >> row) & 1;
      if (invert ? !lit : lit)
        OLED_DrawPoint(x + col, y + row);
      else
        OLED_ClearPoint(x + col, y + row);
    }
  }
}

static void _drawCellLabel(uint8_t px, uint8_t py,
                            char key, bool selected,
                            KeyboardState* kb) {
  bool is_special = (key == K_CAP || key == K_NUM ||
                     key == K_DEL || key == K_OK);

  if (is_special) {
    // Filled box background
    uint8_t fill_x0 = selected ? px             : px + 1;
    uint8_t fill_y0 = selected ? py             : py + 1;
    uint8_t fill_x1 = selected ? px + CELL_W - 1 : px + CELL_W - 2;
    uint8_t fill_y1 = selected ? py + CELL_H     : py + CELL_H - 1;

    for (uint8_t dy = fill_y0; dy < fill_y1; dy++)
      for (uint8_t dx = fill_x0; dx < fill_x1; dx++)
        OLED_DrawPoint(dx, dy);

    // Label text
    const char* label;
    if      (key == K_CAP) label = kb->caps_on       ? "CAP" : "cap";
    else if (key == K_NUM) label = kb->cur_page == 0  ? "NUM" : "ABC";
    else if (key == K_DEL) label = "DEL";
    else                   label = " OK";

    // Draw inverted text inside filled box
    for (uint8_t i = 0; i < 3; i++) {
      if (label[i] == ' ') continue;
      uint8_t cidx = label[i] - 32;
      for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = pgm_read_byte(&font5x8[cidx][col]);
        for (uint8_t row = 0; row < 8; row++) {
          bool lit = (line >> row) & 1;
          if (lit)
            OLED_ClearPoint(px + 1 + i * 6 + col, py + row);
          else
            OLED_DrawPoint(px + 1 + i * 6 + col, py + row);
        }
      }
    }

  } else {
    // Hollow selection box for regular keys
    if (selected) {
      for (uint8_t dx = 0; dx < CELL_W - 1; dx++) {
        OLED_DrawPoint(px + dx, py);
        OLED_DrawPoint(px + dx, py + CELL_H - 1);
      }
      for (uint8_t dy = 0; dy < CELL_H; dy++) {
        OLED_DrawPoint(px,              py + dy);
        OLED_DrawPoint(px + CELL_W - 2, py + dy);
      }
    }

    // Character centered in cell
    char c = key;
    if (kb->cur_page == 0 && !kb->caps_on && c >= 'A' && c <= 'Z')
      c = c + 32;

    _drawChar(px + 8, py, (key == ' ') ? '_' : c, false);
  }
}

// ============================================================
// Public API
// ============================================================
void keyboard_init(KeyboardState* kb) {
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_UP,  INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  keyboard_reset(kb);
}

void keyboard_reset(KeyboardState* kb) {
  memset(kb->password, 0, sizeof(kb->password));
  kb->pw_len    = 0;
  kb->cur_pos   = 0;
  kb->cur_page  = 0;
  kb->caps_on   = true;
  kb->confirmed = false;
}

void keyboard_draw(KeyboardState* kb) {
  OLED_BufferClear();

  // Header
  OLED_ShowString8(KB_X, LABEL_Y,
    kb->cur_page == 0 ? (kb->caps_on ? "ABC" : "abc") : "123");
  OLED_ShowString8(24, LABEL_Y, "Password:");

  // Input text
  kb->password[kb->pw_len] = '\0';
  OLED_ShowString8(KB_X, INPUT_Y, kb->password);

  // Blinking cursor
  uint8_t cx = KB_X + kb->pw_len * 6;
  if (cx < 127 && s_blink_on) {
    for (uint8_t y = INPUT_Y; y < INPUT_Y + 8; y++)
      OLED_DrawPoint(cx, y);
  }

  // Divider
  for (uint8_t x = 0; x < 128; x++)
    OLED_DrawPoint(x, DIV_Y);

  // Keyboard grid
  const char* keys = _currentKeys(kb);
  for (int i = 0; i < KEY_TOTAL; i++) {
    if (keys[i] == K_PAD) continue;
    uint8_t col = i % KEY_COLS;
    uint8_t row = i / KEY_COLS;
    uint8_t px  = KB_X + col * CELL_W;
    uint8_t py  = KB_Y + row * CELL_H;
    _drawCellLabel(px, py, keys[i], (i == kb->cur_pos), kb);
  }

  OLED_Flush();
}

bool keyboard_update(KeyboardState* kb) {
  kb->confirmed = false;

  // Blink cursor every 500ms
  if (millis() - s_last_blink > 500) {
    s_blink_on   = !s_blink_on;
    s_last_blink = millis();
    keyboard_draw(kb);
  }

  // LEFT
  if (_btnPressed(BTN_DOWN, 0)) {
    kb->cur_pos = (kb->cur_pos - 1 + KEY_TOTAL) % KEY_TOTAL;
    while (_currentKeys(kb)[kb->cur_pos] == K_PAD)
      kb->cur_pos = (kb->cur_pos - 1 + KEY_TOTAL) % KEY_TOTAL;
    keyboard_draw(kb);
  }

  // RIGHT
  if (_btnPressed(BTN_UP, 1)) {
    kb->cur_pos = (kb->cur_pos + 1) % KEY_TOTAL;
    while (_currentKeys(kb)[kb->cur_pos] == K_PAD)
      kb->cur_pos = (kb->cur_pos + 1) % KEY_TOTAL;
    keyboard_draw(kb);
  }

  // SELECT
  if (_btnPressed(BTN_SELECT, 2)) {
    char key = _currentKeys(kb)[kb->cur_pos];
    switch (key) {

      case K_OK:
        kb->confirmed = true;
        return true;

      case K_DEL:
        if (kb->pw_len > 0) kb->pw_len--;
        break;

      case K_CAP:
        kb->caps_on = !kb->caps_on;
        break;

      case K_NUM:
        kb->cur_page = (kb->cur_page == 0) ? 1 : 0;
        kb->cur_pos  = 0;
        break;

      case K_PAD:
        break;

      default:
        if (kb->pw_len < 31) {
          char c = key;
          if (kb->cur_page == 0 && !kb->caps_on && c >= 'A' && c <= 'Z')
            c = c + 32;
          kb->password[kb->pw_len++] = c;
        }
        break;
    }
    keyboard_draw(kb);
  }

  return false;
}