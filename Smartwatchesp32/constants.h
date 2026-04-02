#ifndef _constants_h
#define _constants_h

// ── Button pins (your ESP32-C3 setup) ────────────────────────────────────────
// 4 buttons: UP=forward, DOWN=backward, SELECT=fire, BACK=rotate-left
// Holding SELECT+BACK together exits to intro (replaces old left+right)
#define K_UP        6
#define K_DOWN      7
#define K_FIRE      10
#define K_LEFT      20    // BACK button → rotate left
// Rotate right is derived: if not left and player is strafing, handled in loop
// We don't have a dedicated right-rotate button, so we use a second combo:
// UP + FIRE held briefly → rotate right  (see input.cpp)
// Or simply: no dedicated right. You can wire a 5th button to any free GPIO
// and set K_RIGHT to that pin. For now K_RIGHT = K_LEFT to compile cleanly;
// the game will only rotate left unless you define a real pin.
#define K_RIGHT     20    // Change to your 5th button GPIO if you have one

#define USE_INPUT_PULLUP  // buttons wired to GND

// ── GFX settings ─────────────────────────────────────────────────────────────
#define OPTIMIZE_SSD1306          // enables direct OLED_GRAM buffer writes

#define FRAME_TIME          66.666666   // ~15 fps target (ms per frame)
#define RES_DIVIDER         2           // horizontal raycaster resolution divisor
#define Z_RES_DIVIDER       2           // zbuffer resolution divisor
#define DISTANCE_MULTIPLIER 20
#define MAX_RENDER_DEPTH    12
#define MAX_SPRITE_DEPTH    8

#define ZBUFFER_SIZE        (SCREEN_WIDTH / Z_RES_DIVIDER)

// ── Level dimensions ─────────────────────────────────────────────────────────
#define LEVEL_WIDTH_BASE    6
#define LEVEL_WIDTH         (1 << LEVEL_WIDTH_BASE)
#define LEVEL_HEIGHT        57
#define LEVEL_SIZE          (LEVEL_WIDTH / 2 * LEVEL_HEIGHT)

// ── Scene IDs ─────────────────────────────────────────────────────────────────
#define INTRO               0
#define GAME_PLAY           1

// ── Gameplay tuning ───────────────────────────────────────────────────────────
#define GUN_TARGET_POS      18
#define GUN_SHOT_POS        (GUN_TARGET_POS + 4)

#define ROT_SPEED           .12
#define MOV_SPEED           .2
#define MOV_SPEED_INV       5

#define JOGGING_SPEED       .005
#define ENEMY_SPEED         .02
#define FIREBALL_SPEED      .2
#define FIREBALL_ANGLES     45

#define MAX_ENTITIES            10
#define MAX_STATIC_ENTITIES     28

#define MAX_ENTITY_DISTANCE     200
#define MAX_ENEMY_VIEW          80
#define ITEM_COLLIDER_DIST      6
#define ENEMY_COLLIDER_DIST     4
#define FIREBALL_COLLIDER_DIST  2
#define ENEMY_MELEE_DIST        6
#define WALL_COLLIDER_DIST      .2

#define ENEMY_MELEE_DAMAGE      8
#define ENEMY_FIREBALL_DAMAGE   20
#define GUN_MAX_DAMAGE          15

// ── Display ───────────────────────────────────────────────────────────────────
constexpr uint8_t SCREEN_WIDTH   = 128;
constexpr uint8_t SCREEN_HEIGHT  = 64;
constexpr uint8_t HALF_WIDTH     = SCREEN_WIDTH / 2;
constexpr uint8_t RENDER_HEIGHT  = 56;   // raycaster viewport; bottom 8 rows = HUD
constexpr uint8_t HALF_HEIGHT    = SCREEN_HEIGHT / 2;

#endif // _constants_h
