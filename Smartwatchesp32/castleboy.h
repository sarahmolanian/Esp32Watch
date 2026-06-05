#ifndef CASTLEBOY_H
#define CASTLEBOY_H

#include <Arduino.h>
#include <pgmspace.h>
#include "oled_driver.h"
#include "castleboy_assets/assets.h"

// Everything is inside namespace CB to avoid clashes with other
// headers in the smartwatch project (e.g. entities.h, map.h).
namespace CB {

// ---- Pins ----
#define BTN_UP     6
#define BTN_DOWN   7
#define BTN_SELECT 10   // A button: jump / confirm
#define BTN_BACK   3   // B button: sword attack
// Combos: UP+BACK = throw knife / menu up
//         DOWN+BACK = duck / menu down

#define FPS      30
#define SCREEN_W 128
#define SCREEN_H 64

// ---- Game constants ----
#define STAGE_MAX 3
#define LEVEL_PER_STAGE 4
#define ENTITY_MAX 32
#define F_PRECISION 1000

#define GAME_STARTING_LIFE   5
#define GAME_STARTING_TIME   7500
#define GAME_EXTRA_TIME      450
#define BOSS_MAX_HP          12
#define CAMERA_LEFT_BUFFER   24
#define CAMERA_RIGHT_BUFFER  86
#define SCORE_PER_SECOND     5
#define SCORE_PER_CANDLE     10
#define SCORE_PER_MONSTER    25
#define SCORE_PER_COIN       100
#define SCORE_PER_KNIFE      200
#define SCORE_PER_LIFE       1000

#define PLAYER_JUMP_GRAVITY_F     380
#define PLAYER_FALL_GRAVITY_F     380
#define PLAYER_JUMP_FORCE_F       5500
#define PLAYER_LEVITATE_DURATION  3
#define PLAYER_KNOCKBACK_DURATION 24
#define PLAYER_KNOCKBACK_FAST     18
#define PLAYER_INVINCIBLE_DURATION 120
#define PLAYER_SPEED_NORMAL       1
#define PLAYER_SPEED_DUCK         2
#define PLAYER_SPEED_KNOCKBACK_NORMAL 1
#define PLAYER_SPEED_KNOCKBACK_FAST   1
#define PLAYER_ATTACK_TOTAL_DURATION  14
#define PLAYER_ATTACK_CHARGE          8
#define PLAYER_MAX_HP             5

#define ENTITY_FALLING_PLATFORM_DURATION 40
#define ENTITY_FALLING_PLATFORM_WARNING  12
#define BOSS_KNIGHT_WALK_INTERVAL 22
#define BOSS_HARPY_WALK_INTERVAL  16
#define ENTITY_BIRD_WALK_INTERVAL 10

// ---- States ----
#define STATE_TITLE          0
#define STATE_STAGE_INTRO    1
#define STATE_PLAY           2
#define STATE_GAME_OVER      3
#define STATE_GAME_FINISHED  4
#define STATE_LEVEL_FINISHED 5
#define STATE_STAGE_FINISHED 6
#define STATE_PLAYER_DIED    7
#define STATE_HELP           8

// ---- Tile data ----
#define TILE_DATA_EMPTY  0
#define TILE_DATA_PROP   1
#define TILE_DATA_MISC   2
#define TILE_DATA_MAIN   3

#define TILE_WALL              0
#define TILE_WALL_ALT          1
#define TILE_SOLID_END         2
#define TILE_SOLID_END_ALT     3
#define TILE_GROUND_START      4
#define TILE_GROUND            5
#define TILE_GROUND_START_ALT  6
#define TILE_GROUND_ALT        7
#define TILE_GRAVE             8
#define TILE_CHAIN             9
#define TILE_WINDOW            10

#define PICKUP_COIN_VALUE  180
#define PICKUP_KNIFE_VALUE 3

#define TILE_WIDTH       8
#define TILE_HEIGHT      8
#define HALF_TILE_WIDTH  4
#define HALF_TILE_HEIGHT 4

#define ALIGN_LEFT   0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT  2

// ---- Button masks ----
#define LEFT_BUTTON  0x01
#define RIGHT_BUTTON 0x02
#define UP_BUTTON    0x04
#define DOWN_BUTTON  0x08
#define A_BUTTON     0x10
#define B_BUTTON     0x20

// ---- Note constants (values unused — sound is no-op) ----
#define NOTE_C2   65
#define NOTE_G1   49
#define NOTE_G2   98
#define NOTE_GS2  104
#define NOTE_C3   131
#define NOTE_GS3  208
#define NOTE_G3   196
#define NOTE_CS3H 139
#define NOTE_C4   262
#define NOTE_G4   392
#define NOTE_GS4  415
#define NOTE_C5   523
#define NOTE_CS5  554
#define NOTE_GS5  831
#define NOTE_E6   1319
#define NOTE_CS6  1109
#define NOTE_CS7  2217
#define TONES_REPEAT 0xFFFF
#define TONES_END    0

// ---- Structs ----
struct Vec { int16_t x; int8_t y; };
struct Box { uint8_t x, y, width, height; };

struct Entity {
  uint8_t type;
  Vec     pos;
  uint8_t hp;
  uint8_t state;
  uint8_t frame;
  uint8_t counter;
};

// ---- Globals ----
extern uint8_t  mainState;
extern uint8_t  flashCounter;
extern uint32_t frameCount;

// ---- Input ----
namespace Input {
  void init();
  void poll();
  bool pressed(uint8_t mask);
  bool justPressed(uint8_t mask);
  bool justReleased(uint8_t mask);
  bool everyXFrames(uint8_t x);
}

// ---- Sound: all no-ops (no buzzer) ----
namespace Sound {
  inline void init()                                                              {}
  inline void tone1(uint16_t, uint16_t)                                          {}
  inline void tone2(uint16_t, uint16_t, uint16_t, uint16_t)                     {}
  inline void tone3(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t) {}
  inline void tones(const uint16_t*)                                             {}
  inline void update()                                                           {}
  inline bool enabled()        { return false; }
  inline void setEnabled(bool) {}
  inline void saveOnOff()      {}
}

// ---- Display ----
namespace Display {
  void clear();
  void display();
  void fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color);
  void drawPixel(int16_t x, int16_t y, uint8_t color);
  void drawOverwrite(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame);
  void drawSelfMasked(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame);
  void drawPlusMask(int16_t x, int16_t y, const uint8_t* bitmap, uint8_t frame);
  void drawNumber(int16_t x, int16_t y, uint16_t value, uint8_t align);
}

// ---- Util ----
namespace Util {
  void toggle(uint8_t& flags, uint8_t mask);
  bool collideRect(int16_t x1, int8_t y1, uint8_t w1, uint8_t h1,
                   int16_t x2, int8_t y2, uint8_t w2, uint8_t h2);
}

// ---- Map ----
namespace Map {
  extern uint8_t  width;
  extern bool     showBackground;
  extern Entity*  boss;
  void init(const uint8_t* source);
  bool collide(int16_t x, int8_t y, const Box& hitbox);
  void draw();
}

// ---- Entities ----
namespace Entities {
  void    init();
  Entity* add(uint8_t type, int16_t x, int8_t y);
  void    update();
  bool    damage(int16_t x, int8_t y, uint8_t width, uint8_t height, uint8_t value);
  bool    moveCollide(int16_t x, int8_t y, int8_t offsetX, int8_t offsetY, const Box& hitbox);
  Entity* checkPlayer(int16_t x, int8_t y, uint8_t width, uint8_t height);
  void    draw();
}

// ---- Player ----
namespace Player {
  extern Vec     pos;
  extern uint8_t hp;
  extern bool    alive;
  extern uint8_t knifeCount;
  void init(int16_t x, int8_t y);
  void update();
  void draw();
}

// ---- Game ----
namespace Game {
  extern int16_t  cameraX;
  extern uint8_t  life;
  extern uint16_t timeLeft;
  extern uint16_t score;
  void reset();
  void play();
  void loop();
  bool moveY(Vec& pos, int8_t dy, const Box& hitbox, bool collideToEntity = false);
}

// ---- Menu ----
namespace Menu {
  void showTitle();
  void notifyPlayerDied();
  void notifyLevelFinished();
  void loop();
}

} // namespace CB

// Top-level entry points (outside CB — .ino calls these directly)
void castleboy_setup();
bool castleboy_loop(); // returns false when player holds BACK to exit

#endif // CASTLEBOY_H
