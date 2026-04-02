// ============================================================
// DinoGame.cpp
//
// Strategy: run the original blocking while(true) game loop
// exactly as written. This is the same architecture as Doom in
// your project — dinoGame_run() blocks until the player exits,
// then ui.cpp switches back to UI_GAME. No tick-based state
// machine, no heap allocation, no session objects.
// ============================================================

#include "DinoGame.h"
#include <Preferences.h>

// ============================================================
// Assets — exactly as the original assets.h
// ============================================================
#include "assets/trex-up-1.h"
#include "assets/trex-up-2.h"
#include "assets/trex-up-3.h"
#include "assets/trex-duck-1.h"
#include "assets/trex-duck-2.h"
#include "assets/trex-dead-1-no-outline.h"
#include "assets/trex-dead-2-no-outline.h"
const BitmapMasked trex_up_1  (trex_up_1s_bitmap,              trex_up_1s_mask);
const BitmapMasked trex_up_2  (trex_up_2s_bitmap,              trex_up_2s_mask);
const BitmapMasked trex_up_3  (trex_up_3s_bitmap,              trex_up_3s_mask);
const BitmapMasked trex_duck_1(trex_duck_1s_bitmap,            trex_duck_1s_mask);
const BitmapMasked trex_duck_2(trex_duck_2s_bitmap,            trex_duck_2s_mask);
const BitmapMasked trex_dead_1(trex_dead_1s_no_outline_bitmap, trex_dead_1s_no_outline_mask);
const BitmapMasked trex_dead_2(trex_dead_2s_no_outline_bitmap, trex_dead_2s_no_outline_mask);

#include "assets/ground-1.h"
#include "assets/ground-2.h"
#include "assets/ground-3.h"
#include "assets/ground-4.h"
#include "assets/ground-5.h"
const BitmapMasked ground_1(ground_1_bitmap, nullptr);
const BitmapMasked ground_2(ground_2_bitmap, nullptr);
const BitmapMasked ground_3(ground_3_bitmap, nullptr);
const BitmapMasked ground_4(ground_4_bitmap, nullptr);
const BitmapMasked ground_5(ground_5_bitmap, nullptr);

#include "assets/cacti-big-big.h"
#include "assets/cacti-big-small.h"
#include "assets/cacti-small-big.h"
#include "assets/cacti-small-small-small.h"
const BitmapMasked cacti_2b(cacti_big_big_bitmap,           cacti_big_big_mask);
const BitmapMasked cacti_bs(cacti_big_small_bitmap,         cacti_big_small_mask);
const BitmapMasked cacti_sb(cacti_small_big_bitmap,         cacti_small_big_mask);
const BitmapMasked cacti_3s(cacti_small_small_small_bitmap, cacti_small_small_small_mask);

#include "assets/pterodactyl-1.h"
#include "assets/pterodactyl-2.h"
const BitmapMasked pterodactyl_1(pterodactyl_1_bitmap, pterodactyl_1_mask);
const BitmapMasked pterodactyl_2(pterodactyl_2_bitmap, pterodactyl_2_mask);

static const uint8_t font5x8_digits_data[] PROGMEM = {
  0x3E,0x51,0x49,0x45,0x3E,
  0x00,0x42,0x7F,0x40,0x00,
  0x42,0x61,0x51,0x49,0x46,
  0x21,0x41,0x45,0x4B,0x31,
  0x18,0x14,0x12,0x7F,0x10,
  0x27,0x45,0x45,0x45,0x39,
  0x3C,0x4A,0x49,0x49,0x30,
  0x01,0x71,0x09,0x05,0x03,
  0x36,0x49,0x49,0x49,0x36,
  0x06,0x49,0x49,0x29,0x1E
};
Symbol numbers(font5x8_digits_data, 5, 8, 10);

#include "assets/game-over.h"
const BitmapMasked game_overver_bm(game_over_bitmap, game_over_mask);
#include "assets/restart-icon.h"
const BitmapMasked restart_icon_bm(restart_icon_bitmap, restart_icon_mask);
#include "assets/hearts-5x.h"
const BitmapMasked hearts_5x_bm(hearts_5x_bitmap, nullptr);

static const uint8_t hi_score_bitmap[] PROGMEM = {
  11,8,
  0x7F,0x08,0x08,0x08,0x7F, // H
  0x00,
  0x00,0x41,0x7F,0x41,0x00  // I
};
const BitmapMasked hi_score(hi_score_bitmap, nullptr);

// ============================================================
// Sprite arrays
// ============================================================
const BitmapMasked* const trex_sprites[] = {
  &trex_up_1,&trex_up_2,&trex_up_3,
  &trex_duck_1,&trex_duck_2,
  &trex_dead_1,&trex_dead_2
};
const BitmapMasked* const ground_sprites[] = {
  &ground_1,&ground_2,&ground_3,&ground_4,&ground_5,
  &ground_1,&ground_2,&ground_3
};
const BitmapMasked* const cacti_sprites[] = {
  &cacti_sb,&cacti_sb,
  &cacti_bs,&cacti_bs,
  &cacti_2b,&cacti_2b,
  &cacti_3s,&cacti_3s,
};
const uint8_t cacti_widths[] = {10,20,14,20,14,24,18,24};
const BitmapMasked* const pterodactyl_sprites[] = {&pterodactyl_1,&pterodactyl_2};
const int8_t pterodactyl_y_positions[] = {
  PTERODACTYL_POSITION_Y1,PTERODACTYL_POSITION_Y2,
  PTERODACTYL_POSITION_Y2,PTERODACTYL_POSITION_Y3
};

// ============================================================
// Hi-score (ESP32 NVS Preferences)
// ============================================================
static Preferences prefs;
static uint16_t loadHiScore() {
  prefs.begin("dino",true); uint16_t v=prefs.getUShort("hi",0); prefs.end(); return v;
}
static void saveHiScore(uint16_t v) {
  prefs.begin("dino",false); prefs.putUShort("hi",v); prefs.end();
}

// ============================================================
// Button helpers (private to this file)
// ============================================================
static unsigned long _last[4]={0,0,0,0};

static bool _edge(int pin) {
  uint8_t idx;
  switch(pin){
    case BTN_UP:     idx=0; break;
    case BTN_DOWN:   idx=1; break;
    case BTN_SELECT: idx=2; break;
    case BTN_BACK:   idx=3; break;
    default: return false;
  }
  if(digitalRead(pin)==LOW){
    unsigned long now=millis();
    if(now-_last[idx]>UI_DEBOUNCE_MS){_last[idx]=now;return true;}
  } else {
    _last[idx]=0;
  }
  return false;
}

static bool _jumpEdge() { return _edge(BTN_UP)||_edge(BTN_SELECT); }
static bool _backEdge() { return _edge(BTN_BACK); }
static bool _duckHeld() { return digitalRead(BTN_DOWN)==LOW; }

// Flush the debounce table so a button held before entering
// the game does not register as a press inside it.
static void _flushDebounce() {
  unsigned long now=millis();
  for(uint8_t i=0;i<4;++i) _last[i]=now;
}

// ============================================================
// Display helpers
// ============================================================

// Write the BitCanvas directly to OLED hardware — bypasses OLED_Flush().
//
// Root cause of the treadmill / vertical-shift corruption:
//   OLED_Flush() uses dirty-page detection (OLED_GRAM vs OLED_GRAM_PREV).
//   When the frame-throttle spin or any delay interrupts an I2C transaction
//   mid-stream, the SSD1309's internal column pointer ends up at a wrong
//   offset.  The next Flush() skips "clean" pages without resetting the
//   pointer, so those pages land at the wrong hardware address — producing
//   the shift that persists even after the game exits because OLED_GRAM_PREV
//   still records those pages as already sent correctly.
//
// Fix: send every page unconditionally with explicit column-reset commands
//   (0xB0+page, 0x00, 0x10) so the hardware pointer is always anchored.
//   Also keep OLED_GRAM and OLED_GRAM_PREV in sync so after the game exits
//   the UI's OLED_Flush() correctly detects its own draws as dirty.
static void pushCanvasToOLED(const uint8_t* buf) {
    for (uint8_t page = 0; page < 8; page++)
        for (uint8_t col = 0; col < 128; col++)
            OLED_GRAM[col][page] = buf[page * 128 + col];
    OLED_Refresh();
}
static void renderNumber(BitCanvas& c, Point2Di8 pt, uint16_t n) {
  uint16_t base=10000;
  while(base){
    c.render(numbers.getSprite((n/base)%10,pt));
    base/=10; pt.x+=numbers.getWidth()+1;
  }
}

static void setInverse(bool inv){
  OLED_WR_Byte(inv?0xA7:0xA6,0);
}

// ============================================================
// gameLoop — the original blocking loop, now with BTN_BACK exit
// Returns true  → player pressed BTN_BACK (go to menu)
// Returns false → player finished game-over screen (loop again)
// ============================================================
static bool gameLoop(uint16_t& hiScore) {
  // Stack-allocate the canvas (1024 bytes) — safe because this
  // function is called from runDino() at the top of ui_update(),
  // not from within a deep call chain.
  uint8_t   canvasBuf[LCD_WIDTH * LCD_HEIGHT / 8];
  BitCanvas canvas(canvasBuf, LCD_HEIGHT, LCD_WIDTH);

  SpawnHold spawnHolder;
  TrexPlayer  trex;
  Ground      ground1(-1), ground2(63), ground3(127);
  Cactus      cactus1(spawnHolder), cactus2(spawnHolder);
  Pterodactyl ptero(spawnHolder);
  HeartLive   heart;

  dino_array<SpriteAnimated*,8> sprites{{
    &ground1,&ground2,&ground3,
    &cactus1,&cactus2,
    &ptero,&heart,&trex
  }};
  dino_array<SpriteAnimated*,3> enemies{{&cactus1,&cactus2,&ptero}};

  // HUD layout — all y=0 (top 8-pixel page):
  //  x=0   "HI" label
  //  x=13  hi-score  (5 digits × 6px)
  //  x=57  score     (5 digits × 6px)
  //  x=93  hearts    (up to 5 × 6px)
  const Sprite gameOverSpr(&game_overver_bm, {(int8_t)((128-58)/2), 20});
  const Sprite restartSpr (&restart_icon_bm, {(int8_t)((128-18)/2), 36});
  const Sprite hiSpr      (&hi_score,        {0,  0});
        Sprite heartsSpr  (&hearts_5x_bm,    {93, 0});

  uint32_t prvT    = 0;
  bool     gameOver= false;
  uint16_t score   = 0;
  uint8_t  fps     = TARGET_FPS_START;
  uint8_t  lives   = LIVES_START;
  bool     night   = false;
  setInverse(false);

  while(true) {

    // ---- BTN_BACK: exit to menu immediately ----------------
    if(_backEdge()) {
      if(score>hiScore){ hiScore=score; saveHiScore(hiScore); }
      return true;   // caller switches to UI_GAME
    }

    // ---- Render --------------------------------------------
    canvas.clear();

    // HUD (drawn first — always in top row)
    canvas.render(hiSpr);
    heartsSpr.limitRenderWidthTo = 6*lives+1;
    canvas.render(heartsSpr);
    renderNumber(canvas, {13, 0}, hiScore);
    renderNumber(canvas, {57, 0}, score);

    // Game objects
    for(uint8_t i=0;i<sprites.size();++i)
      canvas.render(*sprites[i]);

    // Game-over overlay
    if(gameOver){
      canvas.render(gameOverSpr);
      canvas.render(restartSpr);
    }

    pushCanvasToOLED(canvasBuf);

    // ---- Game-over: wait for jump to restart ---------------
    if(gameOver) {
      if(score>hiScore){ hiScore=score; saveHiScore(hiScore); }
      // Spin here — same as the original blocking wait.
      // BTN_BACK is also checked so the player can exit from
      // the game-over screen.
      while(true){
        if(_backEdge()) return true;
        if(_jumpEdge()) break;
        delay(10);
      }
      return false;   // restart: caller calls gameLoop() again
    }

    // ---- Collision detection ------------------------------
    if(!trex.isBlinking() &&
       CollisionDetector::check(trex,enemies.data,enemies.size())){
      if(lives){ trex.blink(); --lives; }
      else     { trex.die(); gameOver=true; continue; }
    }
    if(lives<LIVES_MAX && CollisionDetector::check(trex,heart)){
      ++lives; heart.eat();
    }

    // ---- Controls -----------------------------------------
    if(_jumpEdge()) trex.jump();
    trex.duck(_duckHeld());

    // ---- Logic step ---------------------------------------
    for(uint8_t i=0;i<sprites.size();++i)
      sprites[i]->step();

    // ---- Score & difficulty --------------------------------
    if(score<0xFFFE) ++score;
    if(!(score%INCREASE_FPS_EVERY_N_SCORE)&&fps<TARGET_FPS_MAX) ++fps;
    if(!(score%DAY_NIGHT_SWITCH_CYCLES)) setInverse(night=!night);

    // ---- Frame throttle (verbatim from original) -----------
    const uint32_t ft = 1000/fps;
    uint32_t elapsed = millis() - prvT;
    if (elapsed < ft) delay(ft - elapsed);   // blocking wait — identical to original
    prvT = millis();
  }
}

// ============================================================
// Public API
// ============================================================

void dinoGame_run() {
  //Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  //Serial.printf("Free stack: %d\n", uxTaskGetStackHighWaterMark(NULL));
  // Seed RNG
  srand((uint16_t)((esp_random()&0xFF00)|(esp_random()&0x00FF)));

  // Load hi-score
  uint16_t hiScore = loadHiScore();

  // Flush debounce so SELECT held in the menu doesn't jump in-game
  _flushDebounce();

  // Run game sessions until the player presses BTN_BACK
  while(!gameLoop(hiScore));

  // Clean up display for the UI.
  // Re-init the OLED to guarantee the hardware's internal page/column
  // pointer is fully reset — prevents any residual game I2C state from
  // corrupting the UI's subsequent OLED_Flush() calls.
  OLED_Init();
  OLED_BufferClear();
  OLED_Flush();
  setInverse(false);

  // Wait until BTN_BACK is released so ui.cpp doesn't see it
  while(digitalRead(BTN_BACK)==LOW) delay(10);
  delay(100);
}