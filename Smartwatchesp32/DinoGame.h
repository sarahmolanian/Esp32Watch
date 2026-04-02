#ifndef _DINO_GAME_H_
#define _DINO_GAME_H_

// ============================================================
// DinoGame.h  —  Chrome Dino for ESP32-C3 + I2C SSD1309
// Requires: oled_driver.h / oled_driver.cpp
//           assets/ folder (original sprite headers)
//
// Controls:
//   BTN_UP / BTN_SELECT  ->  Jump  (start / restart)
//   BTN_DOWN             ->  Duck  (hold)
//   BTN_BACK             ->  Exit back to games menu
//
// Integration in ui.cpp  (mirrors how runDoom() works):
//
//   // in handleInput(), UI_GAME branch:
//   if(gamesIndex == 0) { currentState = UI_DINO_GAME; }
//
//   // at the top of ui_update():
//   if(currentState == UI_DINO_GAME) { runDino(); return; }
//
//   // runDino():
//   void runDino() {
//       dinoGame_run();          // blocks until BTN_BACK pressed
//       currentState = UI_GAME;
//       forceRedraw  = true;
//   }
// ============================================================

#include <Arduino.h>
#include <pgmspace.h>
#include "oled_driver.h"

// ============================================================
// Button pins
// ============================================================
#define BTN_UP         6
#define BTN_DOWN       7
#define BTN_SELECT     10
#define BTN_BACK       20
#define UI_DEBOUNCE_MS 200

// ============================================================
// Game balance  (all verbatim from the original)
// ============================================================
#define PLAYER_SAFE_ZONE_WIDTH     32
#define CACTI_RESPAWN_RATE         50
#define GROUND_CACTI_SCROLL_SPEED  3
#define PTERODACTY_SPEED           5
#define PTERODACTY_RESPAWN_RATE    255
#define INCREASE_FPS_EVERY_N_SCORE 256
#define LIVES_START                3
#define LIVES_MAX                  3
#define SPAWN_NEW_LIVE_MIN_CYCLES  800
#define DAY_NIGHT_SWITCH_CYCLES    1024
#define TARGET_FPS_START           23
#define TARGET_FPS_MAX             48

#define LCD_HEIGHT 64U
#define LCD_WIDTH  128U

// ============================================================
// Engine types  — verbatim from the original engine_types.h
// One addition: SpireScrollingToLeft uses int16_t posX so the
// sprite x coordinate never wraps at -128 after long sessions.
// ============================================================

struct flash_uint8_t {
private:
  flash_uint8_t();
  uint8_t data;
};
static inline uint8_t flashByte(const flash_uint8_t* ptr, uint16_t offset) {
  return pgm_read_byte(ptr + offset);
}

template<typename T>
struct Point2D { T x; T y; };
using Point2Di8 = Point2D<int8_t>;

struct BitmapMasked {
  uint8_t width, height;
  const flash_uint8_t* data;
  const flash_uint8_t* mask;

  // width & height encoded as first two bytes of the array
  BitmapMasked(const uint8_t* bmp, const uint8_t* msk)
    : width (pgm_read_byte(bmp)),
      height(pgm_read_byte(bmp + 1)),
      data  ((const flash_uint8_t*)(bmp + 2)),
      mask  (msk ? (const flash_uint8_t*)(msk + 2) : nullptr) {}

  // explicit w/h (for digit font slices)
  BitmapMasked(const uint8_t* bmp, const uint8_t* msk,
               uint8_t w, uint8_t h)
    : width(w), height(h),
      data ((const flash_uint8_t*)bmp),
      mask (msk ? (const flash_uint8_t*)msk : nullptr) {}
};

struct Sprite {
  const BitmapMasked* bitmap = nullptr;
  Point2Di8 position{};
  enum AnchorCorner : uint8_t { ANCHOR_TOP_LEFT, ANCHOR_BOTTOM_LEFT };
  AnchorCorner anchor = ANCHOR_TOP_LEFT;
  uint8_t limitRenderWidthTo = 0xFF;
  Sprite() {}
  Sprite(const BitmapMasked* bm, const Point2Di8& pos,
         AnchorCorner anch = ANCHOR_TOP_LEFT)
    : bitmap(bm), position(pos), anchor(anch) {}
};

struct SpriteAnimated : Sprite {
  virtual void step() = 0;
protected:
  SpriteAnimated() {}
  SpriteAnimated(const BitmapMasked* bm, const Point2Di8& pos,
                 AnchorCorner anch = ANCHOR_TOP_LEFT)
    : Sprite(bm, pos, anch) {}
};

// posX is int16_t so it never overflows past -128 (int8_t would wrap
// back to +127, making isActive() return true forever → crash)
struct SpireScrollingToLeft : SpriteAnimated {
  SpireScrollingToLeft(const BitmapMasked* bm, uint8_t spd, int8_t posY,
                       AnchorCorner anch = ANCHOR_TOP_LEFT,
                       int16_t resetX = 127)
    : SpriteAnimated(bm, {-127, posY}, anch),
      speed(spd), resetPosX(resetX), posX(-127) {}

  virtual void step() override {
    for(uint8_t i = 0; isActive() && i < speed; ++i) --posX;
    _sync();
  }

  bool isActive() const { return posX > -(int16_t)bitmap->width; }
  void rearm()          { posX = resetPosX; _sync(); }

protected:
  const uint8_t speed;
  const int16_t resetPosX;
  int16_t       posX;

  void _sync() {
    position.x = (posX < -127) ? (int8_t)-127
               : (posX >  127) ? (int8_t) 127
               : (int8_t)posX;
  }
};

// BitCanvas — verbatim from the original.
// One fix: guard the >> shift when bitOffset==0 to prevent
// undefined behaviour (uint8_t >> 8 is UB in C++, produces
// garbage on Xtensa which appears as corrupted pixels).
struct BitCanvas {
  uint8_t* const bitmap;
  const uint8_t  height;
  const uint8_t  width;
  uint8_t xOffset = 0;
  uint8_t yOffset = 0;

  BitCanvas(uint8_t* buf, uint8_t h, uint8_t w)
    : bitmap(buf), height(h), width(w) { clear(); }

  void clear(bool v = false) {
    memset(bitmap, v ? 0xFF : 0x00, width * height / 8);
  }

  void render(const Sprite& s) {
    if(!s.bitmap) return;

    const int16_t sX0 = (int16_t)s.position.x - xOffset;
    const int16_t sX1 = sX0 + min((int)s.bitmap->width,
                                    (int)s.limitRenderWidthTo);
    const int16_t sY0 = (int16_t)s.position.y
      - (s.anchor == Sprite::ANCHOR_BOTTOM_LEFT ? s.bitmap->height : 0)
      - yOffset;
    const int16_t sY1 = sY0 + s.bitmap->height;

    if(sX0 >= width || sX1 <= 0 || sY0 >= height || sY1 <= 0) return;

    const uint8_t Xto = min((int)sX1, (int)width);
    const uint8_t Yto = min((int)sY1, (int)height);

    const uint8_t bitOffset   = (sY0 % 8) + (sY0 % 8 < 0 ? 8 : 0);
    const uint8_t bitmapByteH = (s.bitmap->height + 7) / 8;
    const uint8_t canvasByteH = height / 8;   // = 8 for 64-pixel display

    for(uint8_t y = max((int)sY0, 0); y < 8*((Yto+7)/8); y += 8) {
      const uint8_t yByte  = y / 8;
      if(yByte >= canvasByteH) break;         // never write outside canvas

      const uint8_t sy     = y - sY0;
      const uint8_t syByte = (sy + 7) / 8;
      if(syByte > bitmapByteH) continue;      // never read outside sprite

      uint8_t* const row = bitmap + yByte * width;

      const flash_uint8_t* sMask_c =
        (s.bitmap->mask && syByte < bitmapByteH)
          ? s.bitmap->mask + syByte * s.bitmap->width : nullptr;
      const flash_uint8_t* sMask_p =
        (s.bitmap->mask && syByte && (syByte-1) < bitmapByteH)
          ? s.bitmap->mask + (syByte-1) * s.bitmap->width : nullptr;
      const flash_uint8_t* sData_c =
        (syByte < bitmapByteH)
          ? s.bitmap->data + syByte * s.bitmap->width : nullptr;
      const flash_uint8_t* sData_p =
        (syByte && (syByte-1) < bitmapByteH)
          ? s.bitmap->data + (syByte-1) * s.bitmap->width : nullptr;

      for(uint8_t x = max((int)sX0, 0); x < Xto; ++x) {
        const uint8_t sx = x - sX0;
        uint8_t& cb = row[x];
        if(sMask_p && bitOffset) cb &= ~(flashByte(sMask_p,sx) >> (8-bitOffset));
        if(sMask_c)              cb &= ~(flashByte(sMask_c,sx) <<    bitOffset);
        if(sData_p && bitOffset) cb |=  (flashByte(sData_p,sx) >> (8-bitOffset));
        if(sData_c)              cb |=  (flashByte(sData_c,sx) <<    bitOffset);
      }
    }
  }
};

// CollisionDetector — verbatim from original
struct CollisionDetector {
  template<class D>
  static bool check(const Sprite& s, const D* const* ss, uint8_t sz) {
    while(sz--) if(check(s, **ss++)) return true;
    return false;
  }
  static bool check(const Sprite& s1, const Sprite& s2) {
    if(!s1.bitmap || !s2.bitmap) return false;
    const uint8_t  w   = s1.bitmap->width, h = s1.bitmap->height;
    const int16_t  sX0 = (int16_t)s2.position.x - s1.position.x;
    const int16_t  sX1 = sX0 + min((int)s2.bitmap->width,
                                     (int)s2.limitRenderWidthTo);
    const int16_t  sY0 = (int16_t)s2.position.y
      - (s2.anchor==Sprite::ANCHOR_BOTTOM_LEFT ? s2.bitmap->height : 0)
      - s1.position.y
      + (s1.anchor==Sprite::ANCHOR_BOTTOM_LEFT ? s1.bitmap->height : 0);
    const int16_t  sY1 = sY0 + s2.bitmap->height;
    if(sX0>=w||sX1<=0||sY0>=h||sY1<=0) return false;
    const uint8_t Xto = min((int)sX1,(int)w);
    const uint8_t Yto = min((int)sY1,(int)h);
    for(uint8_t y=max((int)sY0,0); y<Yto; ++y) {
      const uint8_t yB=y/8, yb=y%8, sy=y-sY0, syB=sy/8, syb=sy%8;
      const flash_uint8_t* d1=s1.bitmap->data+yB*w;
      const flash_uint8_t* d2=s2.bitmap->data+syB*s2.bitmap->width;
      for(uint8_t x=max((int)sX0,0); x<Xto; ++x)
        if((flashByte(d1,x)&(1<<yb))&&(flashByte(d2,x-sX0)&(1<<syb)))
          return true;
    }
    return false;
  }
private: CollisionDetector();
};

struct SpawnHold {
  bool tryAcquire(const void* me, uint8_t n) {
    if(!owner)    { counter=n; owner=me; return false; }
    if(owner==me) { if(counter){--counter;return false;} owner=nullptr;return true; }
    return false;
  }
private:
  const void* owner=nullptr;
  uint8_t counter=0;
};

static inline uint8_t dino_scaleValue(uint8_t val, uint8_t limit) {
  return (uint16_t(val)*limit)>>8;
}

template<typename T, uint8_t SZ>
struct dino_array {
  T data[SZ];
  constexpr uint8_t size() const { return SZ; }
  constexpr operator T*()             { return data; }
  constexpr operator const T*() const { return data; }
  template<typename I> constexpr const T& operator[](I i) const { return data[i]; }
};

struct Symbol {
  Symbol(const uint8_t* bmp, uint8_t w, uint8_t h, uint8_t n)
    : cur(bmp,(const uint8_t*)nullptr,w,h),
      sp(&cur,{0,0}),
      base((const flash_uint8_t*)bmp), nSym(n) {}
  const Sprite& getSprite(uint8_t n, const Point2Di8& pos) {
    if(n>=nSym) n=0;
    cur.data = base + (uint16_t)n*cur.width*((cur.height+7)/8);
    sp.position=pos; return sp;
  }
  uint8_t getWidth() const { return cur.width; }
private:
  BitmapMasked cur; Sprite sp;
  const flash_uint8_t* base; uint8_t nSym;
};

// ============================================================
// Forward declarations — defined in DinoGame.cpp
// ============================================================
extern const BitmapMasked trex_up_1, trex_up_2, trex_up_3;
extern const BitmapMasked trex_duck_1, trex_duck_2;
extern const BitmapMasked trex_dead_1, trex_dead_2;
extern const BitmapMasked ground_1, ground_2, ground_3, ground_4, ground_5;
extern const BitmapMasked cacti_2b, cacti_bs, cacti_sb, cacti_3s;
extern const BitmapMasked pterodactyl_1, pterodactyl_2;
extern const BitmapMasked hearts_5x_bm, game_overver_bm,
                           restart_icon_bm, hi_score;
extern Symbol numbers;
extern const BitmapMasked* const trex_sprites[];
extern const BitmapMasked* const ground_sprites[];
extern const BitmapMasked* const cacti_sprites[];
extern const uint8_t              cacti_widths[];
extern const BitmapMasked* const pterodactyl_sprites[];
extern const int8_t               pterodactyl_y_positions[];

// ============================================================
// Defines — verbatim from original
// ============================================================
#define T_REX_SPRITE_UP_START    0
#define T_REX_SPRITE_UP_END      3
#define T_REX_SPRITE_DUCK_START  3
#define T_REX_SPRITE_DUCK_END    5
#define T_REX_SPRITE_DEAD_UP     5
#define T_REX_SPRITE_DEAD_DUCK   6
#define T_REX_JUMP_MOMENTUM      8
#define T_REX_JUMP_MOMENTUM_DUCK 6
#define T_REX_START_POINT        {5, 60}
#define GROUND_POSITION_Y        64
#define CACTUS_POSITION_Y        61
#define PTERODACTYL_POSITION_Y1  15
#define PTERODACTYL_POSITION_Y2  25
#define PTERODACTYL_POSITION_Y3  35
#define HEART_MIN_Y              15
#define HEART_DY                 35

// ============================================================
// Game objects — verbatim from original
// ============================================================
struct TrexPlayer : SpriteAnimated {
  enum State : uint8_t { UP, DUCK, DEAD };
  State state = UP;
  TrexPlayer() : SpriteAnimated(trex_sprites[0],T_REX_START_POINT,ANCHOR_BOTTOM_LEFT){}
  virtual void step() override { animationStep(); motionStep(); }
  void jump() {
    if(isJumping()||state==DEAD) return;
    vy=(state==UP)?T_REX_JUMP_MOMENTUM:T_REX_JUMP_MOMENTUM_DUCK;
  }
  void duck(bool d) {
    if(d){if(state==UP&&!isJumping())state=DUCK;}
    else {if(state==DUCK)state=UP;}
  }
  void die() {
    bitmap=trex_sprites[state==DUCK?T_REX_SPRITE_DEAD_DUCK:T_REX_SPRITE_DEAD_UP];
    state=DEAD; vy=0;
  }
  void blink()      { blinkCnt=PLAYER_SAFE_ZONE_WIDTH; }
  bool isBlinking() { return blinkCnt!=0; }
protected:
  int16_t dy=0,vy=0; bool skipStep=false;
  bool isJumping() const { return dy||vy; }
  void motionStep() {
    if(abs(vy)<=1&&!skipStep){skipStep=true;return;}
    skipStep=false; dy+=vy; position.y-=vy;

    // Clamp top — prevents int8_t wrap
    if(position.y < 0) {
      position.y = 0;
      dy = 0;
      vy = 0;
    }

    // Clamp bottom — snap back to ground
    if(position.y > 60) {
      position.y = 60;
      dy = 0;
      vy = 0;
    }

    if(dy)--vy; else vy=0;
  }
  uint8_t bitmapId=0,blinkCnt=0;
  void animationStep() {
    if(blinkCnt)--blinkCnt;
    if(blinkCnt&1){bitmap=nullptr;return;}
    uint8_t s,e;
    if(state==UP){s=T_REX_SPRITE_UP_START;e=isJumping()?s:T_REX_SPRITE_UP_END;}
    else if(state==DUCK){s=T_REX_SPRITE_DUCK_START;e=isJumping()?s:T_REX_SPRITE_DUCK_END;}
    else{s=e=T_REX_SPRITE_DEAD_UP;}
    if(!(bitmapId>=s&&bitmapId<e))bitmapId=s;
    if(bitmapId+1<e)++bitmapId;else bitmapId=s;
    bitmap=trex_sprites[bitmapId];
  }
};

struct Ground : SpireScrollingToLeft {
  Ground(int16_t startX)
    : SpireScrollingToLeft(ground_sprites[0],GROUND_CACTI_SCROLL_SPEED,
                            GROUND_POSITION_Y,ANCHOR_BOTTOM_LEFT)
  { posX=startX; _sync(); }
  virtual void step() override {
    for(uint8_t i=0;i<speed;++i){
      --posX; _sync();
      if(!isActive()){bitmap=ground_sprites[rand()&7];rearm();}
    }
  }
};

struct Cactus : SpireScrollingToLeft {
  Cactus(SpawnHold& sh)
    : SpireScrollingToLeft(cacti_sprites[0],GROUND_CACTI_SCROLL_SPEED,
                            CACTUS_POSITION_Y,ANCHOR_BOTTOM_LEFT),
      spawnHolder(sh){}
  virtual void step() override {
    SpireScrollingToLeft::step();
    if(!isActive()){
      if(respawnWait){--respawnWait;return;}
      if(!spawnHolder.tryAcquire(this,PLAYER_SAFE_ZONE_WIDTH))return;
      const uint16_t r=(uint16_t)rand();
      const uint8_t  i=r&7;
      bitmap=cacti_sprites[i]; limitRenderWidthTo=cacti_widths[i];
      respawnWait=dino_scaleValue((uint8_t)r,CACTI_RESPAWN_RATE);
      rearm();
    }
  }
private:
  uint8_t respawnWait=0; SpawnHold& spawnHolder;
};

struct Pterodactyl : SpireScrollingToLeft {
  Pterodactyl(SpawnHold& sh)
    : SpireScrollingToLeft(pterodactyl_sprites[0],PTERODACTY_SPEED,
                            PTERODACTYL_POSITION_Y1),
      spawnHolder(sh){}
  virtual void step() override {
    SpireScrollingToLeft::step(); wingFlap();
    if(!isActive()){
      if(respawnWait){--respawnWait;return;}
      if(!spawnHolder.tryAcquire(this,PLAYER_SAFE_ZONE_WIDTH*2))return;
      const uint16_t r=(uint16_t)rand();
      position.y=pterodactyl_y_positions[r&3];
      respawnWait=dino_scaleValue((uint8_t)r,PTERODACTY_RESPAWN_RATE/2)
                  +PTERODACTY_RESPAWN_RATE/2;
      rearm();
    }
  }
private:
  uint8_t respawnWait=0; SpawnHold& spawnHolder; uint8_t flapSkip=0;
  void wingFlap(){
    if(flapSkip){--flapSkip;return;} flapSkip=6;
    bitmap=(bitmap==pterodactyl_sprites[0])?pterodactyl_sprites[1]:pterodactyl_sprites[0];
  }
};

struct HeartLive : SpireScrollingToLeft {
  HeartLive()
    : SpireScrollingToLeft(&hearts_5x_bm,GROUND_CACTI_SCROLL_SPEED,HEART_MIN_Y)
  { limitRenderWidthTo=7; }
  virtual void step() override {
    SpireScrollingToLeft::step();
    if(!isActive()){
      if(respawnWait){--respawnWait;return;}
      const uint16_t r=(uint16_t)rand();
      respawnWait=SPAWN_NEW_LIVE_MIN_CYCLES+uint16_t(r&0xFF);
      position.y=HEART_MIN_Y+dino_scaleValue((uint8_t)(r>>6),HEART_DY);
      rearm();
    }
  }
  void eat(){ posX=-32; _sync(); }
private:
  uint16_t respawnWait=SPAWN_NEW_LIVE_MIN_CYCLES;
};

// ============================================================
// Public API — single blocking call, returns when player exits
// ============================================================
void dinoGame_run();   // blocks until BTN_BACK pressed

#endif // _DINO_GAME_H_
