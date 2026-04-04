// castleboy.cpp - ESP32-C3 port of CastleBoy
// Display: SSD1309 128x64 OLED   Input: 4 buttons   Audio: none

#include "castleboy.h"

// Everything lives inside namespace CB{} to match the header.
// castleboy_setup() and castleboy_loop() are defined OUTSIDE CB at the bottom.
namespace CB {

// ============================================================
// Globals
// ============================================================
uint8_t  mainState    = STATE_TITLE;
uint8_t  flashCounter = 0;
uint32_t frameCount   = 0;

// ============================================================
// INPUT
// ============================================================
namespace Input {
  static uint8_t _cur=0, _prev=0;
  static const uint8_t _pins[4]  = { BTN_UP, BTN_DOWN, BTN_SELECT, BTN_BACK };
  static const uint8_t _pmask[4] = { RIGHT_BUTTON, LEFT_BUTTON, A_BUTTON, B_BUTTON };

  void init() { for(int i=0;i<4;i++) pinMode(_pins[i],INPUT_PULLUP); }

  void poll() {
    _prev=_cur; uint8_t raw=0;
    for(int i=0;i<4;i++) if(digitalRead(_pins[i])==LOW) raw|=_pmask[i];
    uint8_t s=raw;
    if((raw&RIGHT_BUTTON)&&(raw&B_BUTTON)) s|=UP_BUTTON;
    if((raw&LEFT_BUTTON) &&(raw&B_BUTTON)) s|=DOWN_BUTTON;
    _cur=s;
  }
  bool pressed(uint8_t m)     { return(_cur &m)!=0; }
  bool justPressed(uint8_t m) { return((_cur&~_prev)&m)!=0; }
  bool justReleased(uint8_t m){ return((~_cur&_prev)&m)!=0; }
  bool everyXFrames(uint8_t x){ return x&&(frameCount%x)==0; }
}

// ============================================================
// DISPLAY
// ============================================================
namespace Display {
  void clear()   { OLED_BufferClear(); }
  void display() { OLED_Flush(); }

  void drawPixel(int16_t x,int16_t y,uint8_t c){
    if(x<0||x>=SCREEN_W||y<0||y>=SCREEN_H) return;
    if(c) OLED_DrawPoint(x,y); else OLED_ClearPoint(x,y);
  }
  void fillRect(int16_t x,int16_t y,uint8_t w,uint8_t h,uint8_t c){
    for(int16_t iy=y;iy<y+h;iy++) for(int16_t ix=x;ix<x+w;ix++) drawPixel(ix,iy,c);
  }

  void drawOverwrite(int16_t x,int16_t y,const uint8_t* bmp,uint8_t frame){
    uint8_t w=pgm_read_byte(bmp),h=pgm_read_byte(bmp+1),p=(h+7)/8;
    const uint8_t* d=bmp+2+(uint16_t)frame*w*p;
    for(uint8_t col=0;col<w;col++) for(uint8_t pg=0;pg<p;pg++){
      uint8_t b=pgm_read_byte(d+pg*w+col);
      for(uint8_t bit=0;bit<8;bit++){
        int16_t px=x+col,py=y+pg*8+bit;
        if(py>=y+h) break;
        drawPixel(px,py,(b>>bit)&1);
      }
    }
  }
  void drawSelfMasked(int16_t x,int16_t y,const uint8_t* bmp,uint8_t frame){
    uint8_t w=pgm_read_byte(bmp),h=pgm_read_byte(bmp+1),p=(h+7)/8;
    const uint8_t* d=bmp+2+(uint16_t)frame*w*p;
    for(uint8_t col=0;col<w;col++) for(uint8_t pg=0;pg<p;pg++){
      uint8_t b=pgm_read_byte(d+pg*w+col);
      for(uint8_t bit=0;bit<8;bit++){
        if((b>>bit)&1){ int16_t px=x+col,py=y+pg*8+bit; if(py>=y+h) break; drawPixel(px,py,1); }
      }
    }
  }
  void drawPlusMask(int16_t x,int16_t y,const uint8_t* bmp,uint8_t frame){
    uint8_t w=pgm_read_byte(bmp),h=pgm_read_byte(bmp+1),p=(h+7)/8;
    const uint8_t* d=bmp+2+(uint16_t)frame*w*p*2;
    for(uint8_t col=0;col<w;col++) for(uint8_t pg=0;pg<p;pg++){
      uint16_t idx=((uint16_t)pg*w+col)*2;
      uint8_t pix=pgm_read_byte(d+idx),mask=pgm_read_byte(d+idx+1);
      for(uint8_t bit=0;bit<8;bit++){
        int16_t px=x+col,py=y+pg*8+bit;
        if(py>=y+h) break;
        if(px<0||px>=SCREEN_W||py<0||py>=SCREEN_H) continue;
        if(mask&(1<<bit)) drawPixel(px,py,(pix>>bit)&1);
      }
    }
  }
  void drawNumber(int16_t x,int16_t y,uint16_t val,uint8_t align){
    char buf[8]; itoa(val,buf,10); uint8_t len=strlen(buf);
    int16_t ox=(align==ALIGN_CENTER)?-(len*2):(align==ALIGN_RIGHT)?-(len*4):0;
    fillRect(x+ox-1,y,4*len+1,7,0);
    for(uint8_t i=0;i<len;i++){ uint8_t d=buf[i]-'0'; if(d>9)d=0; drawSelfMasked(x+ox+4*i,y,font,d); }
  }
}

// ============================================================
// UTIL
// ============================================================
namespace Util {
  void toggle(uint8_t& f,uint8_t m){ f=(f&m)?(f&~m):(f|m); }
  bool collideRect(int16_t x1,int8_t y1,uint8_t w1,uint8_t h1,int16_t x2,int8_t y2,uint8_t w2,uint8_t h2){
    return !(x1>=x2+w2||x1+w1<=x2||y1>=y2+h2||y1+h1<=y2);
  }
}

// ============================================================
// MAP
// ============================================================
namespace Map {
  uint8_t width=0; bool showBackground=false; Entity* boss=nullptr;
  static const uint8_t* _tilemap=nullptr;
  static uint8_t _height=0,_solidTileIdx=2,_mainTile,_mainTileAlt,_mainStartTile,_mainStartTileAlt,_propTile,_miscTile;
  static bool _endMainTile;

  static uint8_t _getTile(uint8_t x,uint8_t y){
    uint16_t bi=(uint16_t)x*_height+y;
    return (pgm_read_byte(_tilemap+bi/4)>>((bi%4)*2))&0x03;
  }
  void init(const uint8_t* src){
    width=pgm_read_byte(src); _height=pgm_read_byte(src+1);
    uint8_t meta=pgm_read_byte(src+2); bool hasBoss=(meta&0xC0)==0xC0;
    if(meta&0x80){
      _solidTileIdx=3; _mainTile=_mainStartTile=TILE_WALL; _mainTileAlt=_mainStartTileAlt=TILE_WALL_ALT;
      _endMainTile=false; _miscTile=TILE_CHAIN; _propTile=TILE_WINDOW; showBackground=false;
    } else {
      _solidTileIdx=2; _mainTile=TILE_GROUND; _mainTileAlt=TILE_GROUND_ALT; _miscTile=TILE_WALL; _propTile=TILE_GRAVE;
      if(meta&0x40){ _mainStartTile=TILE_GROUND_START; _mainStartTileAlt=TILE_GROUND_START_ALT; showBackground=true; }
      else         { _mainStartTile=TILE_GROUND; _mainStartTileAlt=TILE_GROUND_ALT; showBackground=false; }
      _endMainTile=true;
    }
    Player::init(pgm_read_byte(src+3)*TILE_WIDTH+HALF_TILE_WIDTH,(meta&0x0F)*TILE_HEIGHT+TILE_HEIGHT);
    _tilemap=src+4;
    const uint8_t* ep=_tilemap+(uint16_t)width*_height/4;
    uint8_t ec=pgm_read_byte(ep); boss=nullptr; Entities::init();
    for(uint8_t i=0;i<ec;i++){
      uint8_t tmp=pgm_read_byte(ep+1+i*2),ex=pgm_read_byte(ep+2+i*2);
      Entity* e=Entities::add((tmp&0xF0)>>4,ex*TILE_WIDTH+HALF_TILE_WIDTH,(tmp&0x0F)*TILE_HEIGHT+TILE_HEIGHT);
      if(hasBoss) boss=e;
    }
  }
  bool collide(int16_t x,int8_t y,const Box& hb){
    x-=hb.x; y-=hb.y; if(x<0) return true;
    int16_t tx1=x/TILE_WIDTH,tx2=(x+hb.width-1)/TILE_WIDTH;
    int8_t ty1=y/TILE_HEIGHT,ty2=(y+hb.height-1)/TILE_HEIGHT;
    if(ty2<0||ty2>=_height) return false;
    if(tx1<0)tx1=0; if(tx2>=width)tx2=width-1; if(ty1<0)ty1=0; if(ty2>=_height)ty2=_height-1;
    for(int16_t ix=tx1;ix<=tx2;ix++) for(int8_t iy=ty1;iy<=ty2;iy++)
      if(_getTile(ix,iy)>=_solidTileIdx && Util::collideRect(ix*TILE_WIDTH,iy*TILE_HEIGHT,TILE_WIDTH,TILE_HEIGHT,x,y,hb.width,hb.height)) return true;
    return false;
  }
  void draw(){
    uint8_t start=Game::cameraX/8;
    for(uint8_t ix=start;ix<start+17&&ix<width;ix++){
      bool isMain=false,needToEnd=false,tileEnded=false;
      for(uint8_t iy=0;iy<_height;iy++){
        uint8_t td=_getTile(ix,iy),tile;
        if(td==TILE_DATA_EMPTY){
          if(!tileEnded&&needToEnd){ tile=(ix%2==0)?TILE_SOLID_END:TILE_SOLID_END_ALT; needToEnd=false; tileEnded=true; } else continue;
        } else if(td==TILE_DATA_MISC){ tile=_miscTile; isMain=false; }
        else if(td==TILE_DATA_MAIN){
          bool ua=(ix%2==0&&iy%2==1)||(ix%2==1&&(iy&2)==0);
          tile=isMain?(ua?_mainTileAlt:_mainTile):(ua?_mainStartTileAlt:_mainStartTile); isMain=true;
          if(_endMainTile) needToEnd=true;
        } else { tile=_propTile; needToEnd=false; }
        Display::drawOverwrite(ix*TILE_WIDTH-Game::cameraX,iy*TILE_HEIGHT,tileset,tile);
      }
    }
  }
}

// ============================================================
// ENTITIES
// ============================================================
namespace Entities {
#define ENTITY_FALLING_PLATFORM      0x00
#define ENTITY_MOVING_PLATFORM_RIGHT 0x01
#define ENTITY_MOVING_PLATFORM_LEFT  0x02
#define ENTITY_CANDLE_COIN           0x03
#define ENTITY_CANDLE_KNIFE          0x04
#define ENTITY_SKELETON_SIMPLE       0x05
#define ENTITY_SKELETON_THROW        0x06
#define ENTITY_SKELETON_ARMORED      0x07
#define ENTITY_FLYER_SKULL           0x08
#define ENTITY_BIRD                  0x09
#define ENTITY_HURLER                0x0A
#define ENTITY_FIREBALL_VERT         0x0B
#define ENTITY_CANDLESTICK           0x0C
#define ENTITY_BOSS_KNIGHT           0x0D
#define ENTITY_BOSS_HARPY            0x0E
#define ENTITY_BOSS_FINAL            0x0F
#define ENTITY_PICKUP_COIN           0x10
#define ENTITY_PICKUP_KNIFE          0x11
#define ENTITY_BONE                  0x12
#define ENTITY_FIREBALL_HORIZ        0x13
#define FLAG_PRESENT  0x80
#define FLAG_ALIVE    0x40
#define FLAG_MISC1    0x20
#define FLAG_MISC2    0x10
#define MASK_HURT     0x0F
#define HURT_DURATION 0x09

  struct ED { Box hb; int8_t sox,soy; uint8_t hp; const uint8_t* spr; };
  static const ED data[]={
    {{4,8,16,8}, 4,8, 0,entity_falling_platform_plus_mask},
    {{4,8,24,8}, 4,8, 0,entity_moving_platform_plus_mask},
    {{4,8,24,8}, 4,8, 0,entity_moving_platform_plus_mask},
    {{2,8,4,6},  2,10,1,entity_candle_plus_mask},
    {{2,8,4,6},  4,10,1,entity_candle_plus_mask},
    {{3,16,6,16},6,16,2,entity_skeleton_plus_mask},
    {{3,16,6,16},6,16,2,entity_skeleton_plus_mask},
    {{3,16,6,16},6,16,6,entity_skeleton_armored_plus_mask},
    {{2,6,4,6},  4,8, 1,entity_skull_plus_mask},
    {{4,8,8,8},  4,8, 2,entity_bird_plus_mask},
    {{4,8,8,8},  4,8, 4,entity_hurler_plus_mask},
    {{2,2,4,4},  3,3, 0,entity_fireball_vert_plus_mask},
    {{5,4,10,4}, 6,8, 0,entity_candlestick_plus_mask},
    {{7,26,14,26},12,32,BOSS_MAX_HP,entity_boss_knight_plus_mask},
    {{4,8,8,8},  6,8, BOSS_MAX_HP,entity_boss_harpy_plus_mask},
    {{7,26,14,26},12,32,BOSS_MAX_HP,entity_boss_knight_plus_mask},
    {{3,6,6,6},  4,8, 0,entity_coin_plus_mask},
    {{4,6,8,6},  3,6, 0,entity_knife_plus_mask},
    {{3,3,6,6},  4,4, 0,entity_bone_plus_mask},
    {{2,2,4,4},  3,3, 0,entity_fireball_horiz_plus_mask},
  };
  static Entity entities[ENTITY_MAX];
  static uint8_t bossState,bossState2;

  void init(){ for(uint8_t i=0;i<ENTITY_MAX;i++) entities[i].state=0; bossState=bossState2=0; }
  Entity* add(uint8_t type,int16_t x,int8_t y){
    for(uint8_t i=0;i<ENTITY_MAX;i++){ Entity& e=entities[i]; if(!e.state){
      e.type=type;e.pos.x=x;e.pos.y=y;e.hp=data[type].hp;
      e.state=FLAG_PRESENT|FLAG_ALIVE;e.frame=e.hp?1:0;e.counter=0;return &e;
    }} return nullptr;
  }
  static int8_t _dir(Entity& e){ return (e.type==ENTITY_MOVING_PLATFORM_RIGHT)?((e.state&FLAG_MISC1)?-1:1):((e.state&FLAG_MISC1)?1:-1); }
  static void _updPlatform(Entity& e){ if(Input::everyXFrames(3)){ e.pos.x+=_dir(e); if(++e.counter==23){e.counter=0;Util::toggle(e.state,FLAG_MISC1);} } }
  static void _updSkeleton(Entity& e){
    if(Input::everyXFrames(3)){
      if(e.state&FLAG_MISC2){ if(++e.counter==10){add(ENTITY_BONE,e.pos.x,e.pos.y-10);e.state&=~FLAG_MISC2;e.counter=0;} }
      else{ e.pos.x+=(e.state&FLAG_MISC1)?1:-1; if(++e.counter==23){ e.counter=0; if(e.state&FLAG_MISC1)e.state&=~FLAG_MISC1; else{ if(e.type==ENTITY_SKELETON_THROW&&e.pos.x-Player::pos.x<94)e.state|=FLAG_MISC2; e.state|=FLAG_MISC1; } } }
    }
    if(e.state&FLAG_MISC2)e.frame=3; else if(Input::everyXFrames(8))e.frame=(e.frame==2)?1:2;
  }
  static void _updHurler(Entity& e){
    if(Input::everyXFrames(3)){if(e.counter<30)e.counter++;if(e.counter==30&&e.pos.x-Player::pos.x<94){add(ENTITY_FIREBALL_HORIZ,e.pos.x,e.pos.y-4);e.counter=0;}}
    e.frame=(e.counter<20)?1:2;
  }
  static void _updFlyer(Entity& e){
    if(!(e.state&FLAG_MISC1)&&e.pos.x-Player::pos.x<90)e.state|=FLAG_MISC1;
    if(e.state&FLAG_MISC1){if(Input::everyXFrames(2)){if(--e.pos.x<-8){e.state=0;return;}e.pos.y+=(++e.counter/20%2)?1:-1;}if(Input::everyXFrames(8))e.frame=(e.frame==2)?1:2;}
  }
  static void _updBird(Entity& e){
    if(!(e.state&FLAG_MISC2)){if(e.counter<60)++e.counter;else if(Player::pos.x>e.pos.x-64&&Player::pos.x<e.pos.x+80){e.state|=FLAG_MISC2;e.counter=0;}}
    else{e.pos.x+=(e.state&FLAG_MISC1)?1:-1;if(++e.counter==104){Util::toggle(e.state,FLAG_MISC1);e.state&=~FLAG_MISC2;e.counter=0;}if(e.counter%4)e.pos.y+=(e.counter<52)?1:-1;}
    if(Input::everyXFrames(ENTITY_BIRD_WALK_INTERVAL)){if(e.state&FLAG_MISC1)e.frame=(e.frame==5)?4:5;else e.frame=(e.frame==2)?1:2;}
  }
  static void _updBossKnight(Entity& e){
    if(Input::everyXFrames(4)){if(e.state&FLAG_MISC2){e.state&=~FLAG_MISC2;Util::toggle(e.state,FLAG_MISC1);e.counter=87-e.counter;}e.pos.x+=(e.state&FLAG_MISC1)?1:-1;if(++e.counter==87){e.counter=0;Util::toggle(e.state,FLAG_MISC1);}}
    if(Input::everyXFrames(BOSS_KNIGHT_WALK_INTERVAL)){if(e.state&FLAG_MISC1)e.frame=(e.frame==6)?5:6;else e.frame=(e.frame==2)?1:2;}
  }
  static void _updBossHarpy(Entity& e){
    if(Input::everyXFrames(2)){e.pos.x+=(e.state&FLAG_MISC1)?1:-1;if(++e.counter==104){Util::toggle(e.state,FLAG_MISC1);e.counter=0;if(++bossState==3){if(e.state&FLAG_MISC2)e.state&=~FLAG_MISC2;bossState=0;}}
      if(bossState<2){if(++bossState2>=9+e.hp){add(ENTITY_FIREBALL_VERT,e.pos.x,e.pos.y);bossState2=0;}}else{if(e.counter%4)e.pos.y+=(e.counter<52)?1:-1;}}
    if(bossState<2||e.counter>=52){if(Input::everyXFrames(BOSS_HARPY_WALK_INTERVAL)){if(e.state&FLAG_MISC1)e.frame=(e.frame==6)?5:6;else e.frame=(e.frame==2)?1:2;}}else{e.frame=(e.state&FLAG_MISC1)?7:3;}
  }
  static const uint8_t _pat[] PROGMEM={7,0,0,0,15,0,0,0,7,7,0,0,7,7,0,0,7,7,0,0,15,15,0,0,7,7,0,15,15,15,15,0,7,0,7,0,7,0,0,0,7,0,15,0,7,0,15,0};
  static void _updBossFinal(Entity& e){
    if(e.state&FLAG_MISC2){e.state&=~(FLAG_MISC2|FLAG_MISC1);bossState=e.counter=0;e.frame=1;}
    if(e.state&FLAG_MISC1){if(++e.counter==100){e.state&=~FLAG_MISC1;e.frame=1;e.counter=0;}}
    else{uint8_t p=pgm_read_byte(_pat+bossState2*8+(e.hp<=6?24:0)+(bossState%8));e.frame=(p>0&&e.counter>12)?8:1;
      if(++e.counter==16){if(p>0){add(ENTITY_FIREBALL_HORIZ,e.pos.x,e.pos.y-p);}e.counter=0;if(++bossState==24){e.state|=FLAG_MISC1;e.frame=9;bossState=0;++bossState2%=3;}}}
  }
  static void _updProjectile(Entity& e){
    --e.pos.x;if(e.type==ENTITY_BONE){e.pos.y+=e.counter-2;if(e.counter<8&&Input::everyXFrames(10))++e.counter;}
    if(e.pos.y>68||e.pos.x<Game::cameraX-8)e.state=0;if(Input::everyXFrames(12))++e.frame%=2;
  }

  void update(){
    for(uint8_t i=0;i<ENTITY_MAX;i++){Entity& e=entities[i];if(!(e.state&FLAG_PRESENT))continue;
      if(e.state&MASK_HURT){uint8_t hc=(e.state&MASK_HURT)-1;e.state=(e.state&~MASK_HURT)|hc;
        if(!hc){if(!e.hp){e.frame=0;e.counter=0;}else if(e.type==ENTITY_BOSS_KNIGHT)e.frame=(e.state&FLAG_MISC1)?5:1;else if(e.type==ENTITY_BOSS_HARPY)e.frame=(e.state&FLAG_MISC1)?7:3;else e.frame=1;}continue;}
      if(!(e.state&FLAG_ALIVE)){if(++e.counter==8){if(++e.frame==3){if(e.type==ENTITY_CANDLE_COIN){e.type=ENTITY_PICKUP_COIN;e.state|=FLAG_ALIVE;e.frame=0;}else if(e.type==ENTITY_CANDLE_KNIFE){e.type=ENTITY_PICKUP_KNIFE;e.state|=FLAG_ALIVE;e.frame=0;}else e.state=0;}e.counter=0;}continue;}
      switch(e.type){
        case ENTITY_FALLING_PLATFORM:if(e.state&FLAG_MISC1){if(++e.counter==ENTITY_FALLING_PLATFORM_DURATION)e.state=0;else if(e.counter==ENTITY_FALLING_PLATFORM_WARNING)e.frame=1;}break;
        case ENTITY_MOVING_PLATFORM_LEFT:case ENTITY_MOVING_PLATFORM_RIGHT:_updPlatform(e);break;
        case ENTITY_FIREBALL_VERT:if(++e.pos.y==68){if(Map::boss)e.state=0;else e.pos.y=-4;}if(Input::everyXFrames(12))++e.frame%=2;break;
        case ENTITY_CANDLE_COIN:case ENTITY_CANDLE_KNIFE:if(Input::everyXFrames(8))++e.frame%=2;break;
        case ENTITY_PICKUP_COIN:case ENTITY_PICKUP_KNIFE:Game::moveY(e.pos,2,data[e.type].hb);if(e.type!=ENTITY_PICKUP_KNIFE&&Input::everyXFrames(12))++e.frame%=2;break;
        case ENTITY_SKELETON_SIMPLE:case ENTITY_SKELETON_THROW:case ENTITY_SKELETON_ARMORED:_updSkeleton(e);break;
        case ENTITY_FLYER_SKULL:_updFlyer(e);break;
        case ENTITY_BIRD:_updBird(e);break;
        case ENTITY_HURLER:_updHurler(e);break;
        case ENTITY_CANDLESTICK:if(e.pos.x<Player::pos.x+4)e.state|=FLAG_MISC1;if((e.state&FLAG_MISC1)&&Game::moveY(e.pos,1,data[e.type].hb))e.state&=~FLAG_ALIVE;break;
        case ENTITY_BOSS_KNIGHT:_updBossKnight(e);break;
        case ENTITY_BOSS_HARPY:_updBossHarpy(e);break;
        case ENTITY_BOSS_FINAL:_updBossFinal(e);break;
        case ENTITY_BONE:case ENTITY_FIREBALL_HORIZ:_updProjectile(e);break;
      }
    }
  }

  bool damage(int16_t x,int8_t y,uint8_t w,uint8_t h,uint8_t val){
    bool hit=false;
    for(uint8_t i=0;i<ENTITY_MAX;i++){Entity& e=entities[i];
      if(!(e.state&FLAG_ALIVE)||(e.state&MASK_HURT))continue;
      const ED& ed=data[e.type];if(!ed.hp)continue;
      if(!Util::collideRect(e.pos.x-ed.hb.x,e.pos.y-ed.hb.y,ed.hb.width,ed.hb.height,x,y,w,h))continue;
      hit=true;bool dmg=true;
      if(e.type==ENTITY_BOSS_KNIGHT){dmg=(e.state&FLAG_MISC1)?Player::pos.x<e.pos.x:Player::pos.x>e.pos.x;if(dmg){e.state|=FLAG_MISC2;e.frame=(e.state&FLAG_MISC1)?4:0;}else{e.frame=(e.state&FLAG_MISC1)?7:3;e.state|=MASK_HURT;}e.state|=HURT_DURATION;}
      else if(e.type==ENTITY_BOSS_HARPY){dmg=!(e.state&FLAG_MISC2);if(dmg){e.frame=(e.state&FLAG_MISC1)?4:0;e.state|=FLAG_MISC2|HURT_DURATION;}}
      else if(e.type==ENTITY_BOSS_FINAL){dmg=!!(e.state&FLAG_MISC1);if(dmg){e.frame=0;e.state|=FLAG_MISC2|HURT_DURATION;}else e.frame=3;e.state|=HURT_DURATION;}
      else{e.frame=0;e.state|=HURT_DURATION;}
      if(dmg){if(e.hp<=val){if(e.type==ENTITY_CANDLE_COIN||e.type==ENTITY_CANDLE_KNIFE){e.state&=~MASK_HURT;e.frame=1;Game::score+=SCORE_PER_CANDLE;}else Game::score+=SCORE_PER_MONSTER;e.state&=~FLAG_ALIVE;e.hp=0;e.counter=0;}else e.hp-=val;}
    }
    return hit;
  }

  bool moveCollide(int16_t x,int8_t y,int8_t ox,int8_t oy,const Box& hb){
    x+=ox;y+=oy;bool col=false;
    for(uint8_t i=0;i<ENTITY_MAX;i++){Entity& e=entities[i];if(!(e.state&FLAG_ALIVE)||e.type>2)continue;
      const Box& eh=data[e.type].hb;
      if(!Util::collideRect(e.pos.x-eh.x,e.pos.y-eh.y,eh.width,eh.height,x-hb.x,y-hb.y,hb.width,hb.height))continue;
      if(e.type==ENTITY_FALLING_PLATFORM){if(oy>0)e.state|=FLAG_MISC1;col=true;}
      else if(oy>0&&y==(e.pos.y-eh.y+1)){col=true;if(Input::everyXFrames(3)){int16_t nx=Player::pos.x+_dir(e);if(!Map::collide(nx,y,hb))Player::pos.x=nx;}}
    }
    return col;
  }

  Entity* checkPlayer(int16_t x,int8_t y,uint8_t w,uint8_t h){
    for(uint8_t i=0;i<ENTITY_MAX;i++){Entity& e=entities[i];
      if(!(e.state&FLAG_ALIVE)||(e.state&MASK_HURT))continue;
      if(e.type==ENTITY_CANDLE_COIN||e.type==ENTITY_CANDLE_KNIFE)continue;
      const Box& eh=data[e.type].hb;
      if(!Util::collideRect(e.pos.x-eh.x,e.pos.y-eh.y,eh.width,eh.height,x,y,w,h))continue;
      switch(e.type){
        case ENTITY_FALLING_PLATFORM:case ENTITY_MOVING_PLATFORM_LEFT:case ENTITY_MOVING_PLATFORM_RIGHT:break;
        case ENTITY_PICKUP_COIN:e.state=0;Game::score+=SCORE_PER_COIN;break;
        case ENTITY_PICKUP_KNIFE:Player::knifeCount+=PICKUP_KNIFE_VALUE;Game::score+=SCORE_PER_KNIFE;e.state=0;break;
        default:return &e;
      }
    }
    return nullptr;
  }

  void draw(){
    for(uint8_t i=0;i<ENTITY_MAX;i++){Entity& e=entities[i];if(!(e.state&FLAG_PRESENT))continue;
      const ED& ed=data[e.type];
      if((e.state&FLAG_ALIVE)||(e.state&MASK_HURT)) Display::drawPlusMask(e.pos.x-ed.sox-Game::cameraX,e.pos.y-ed.soy,ed.spr,e.frame);
      else Display::drawPlusMask(e.pos.x-4-Game::cameraX,e.pos.y-10,fx_destroy_plus_mask,e.frame);
    }
  }
}

// ============================================================
// PLAYER
// ============================================================
namespace Player {
  Vec pos={0,0}; uint8_t hp=PLAYER_MAX_HP; bool alive=true; uint8_t knifeCount=0;
  static const Box NH={4,14,8,14},DH={4,6,8,6},KH={0,1,8,4};
#define SOX 8
#define SOY 16
#define FI 0
#define FW1 0
#define FW2 2
#define FAC 4
#define FA  6
#define FAIR 8
#define FKB 9
#define FD  10
#define FFO 11
#define WFR 12
  static int8_t vx=0; static int16_t vyf=0;
  static bool grounded=true,jumping=false,ducking=false,knifeAtk=false,flipped=false,walkFr=false,visible=true,knife=false,knifeFlip=false;
  static uint8_t atkCnt=0,kbCnt=0,levCnt=0,invCnt=0;
  static Vec kpos={0,0};

  void init(int16_t x,int8_t y){pos.x=x;pos.y=y;grounded=true;alive=true;atkCnt=kbCnt=levCnt=invCnt=0;knifeAtk=jumping=ducking=flipped=knife=false;visible=true;vx=0;vyf=0;}

  void update(){
    if(kbCnt>0){if(!--kbCnt){vx=0;if(!hp)alive=false;}}
    if(invCnt>0){if(!--invCnt)visible=true;}
    if(!alive)return;
    if(!kbCnt&&!atkCnt){
      if(Input::justPressed(B_BUTTON)){knifeAtk=false;atkCnt=PLAYER_ATTACK_TOTAL_DURATION;}
      else if(Input::justPressed(UP_BUTTON)){if(knifeCount>0){--knifeCount;knifeAtk=true;atkCnt=PLAYER_ATTACK_TOTAL_DURATION;}}
    }
    if(!kbCnt&&!ducking&&!atkCnt&&grounded&&Input::justPressed(A_BUTTON)){grounded=false;jumping=true;vyf=-PLAYER_JUMP_FORCE_F;}
    if(!kbCnt&&!atkCnt){if(!ducking)ducking=grounded&&Input::pressed(DOWN_BUTTON);else if(!Input::pressed(DOWN_BUTTON))ducking=Map::collide(pos.x,pos.y,NH);}
    const Box& hb=ducking?DH:NH;
    if(levCnt>0){--levCnt;}
    else if(jumping){vyf+=PLAYER_JUMP_GRAVITY_F;if(vyf>=0){vyf=0;jumping=false;levCnt=PLAYER_LEVITATE_DURATION;}else Game::moveY(pos,(int8_t)(vyf/F_PRECISION),ducking?DH:NH,true);}
    else{vyf+=PLAYER_FALL_GRAVITY_F;int8_t oy=(int8_t)(vyf/F_PRECISION);if(oy>0)grounded=Game::moveY(pos,oy,hb,true);else grounded=Entities::moveCollide(pos.x,pos.y,0,1,hb)||Map::collide(pos.x,pos.y+1,hb);if(grounded)vyf=0;}
    if(!kbCnt){if(!atkCnt&&Input::pressed(LEFT_BUTTON)){vx=-1;flipped=true;}else if(!atkCnt&&Input::pressed(RIGHT_BUTTON)){vx=1;flipped=false;}else if(grounded)vx=0;}
    if(vx&&((!kbCnt&&Input::everyXFrames(ducking?PLAYER_SPEED_DUCK:PLAYER_SPEED_NORMAL))||(kbCnt&&Input::everyXFrames(kbCnt<PLAYER_KNOCKBACK_FAST?PLAYER_SPEED_KNOCKBACK_NORMAL:PLAYER_SPEED_KNOCKBACK_FAST))))
      if(!Entities::moveCollide(pos.x,pos.y,vx,0,hb)&&!Map::collide(pos.x+vx,pos.y,hb))pos.x+=vx;
    if(atkCnt>0){if(--atkCnt<=PLAYER_ATTACK_CHARGE){if(knifeAtk){if(atkCnt==PLAYER_ATTACK_CHARGE){knife=true;kpos.x=pos.x+(flipped?-14:6);kpos.y=pos.y-(ducking?6:14);knifeFlip=flipped;}}else Entities::damage(pos.x+(flipped?-24:0),pos.y-(ducking?4:11),24,2,2);}}
    if(pos.y-SOY>SCREEN_H)alive=false;
    if(knife){kpos.x+=knifeFlip?-3:3;if(Entities::damage(kpos.x-KH.x,kpos.y-KH.y,KH.width,KH.height,1))knife=false;if(Map::collide(kpos.x,kpos.y,KH))knife=false;if(kpos.x+KH.width<Game::cameraX||kpos.x>Game::cameraX+SCREEN_W)knife=false;}
    Entity* ent=Entities::checkPlayer(pos.x-hb.x,pos.y-hb.y,hb.width,hb.height);
    if(hp>0&&!invCnt&&ent){flipped=ent->pos.x<pos.x;vx=flipped?1:-1;kbCnt=PLAYER_KNOCKBACK_DURATION;jumping=false;levCnt=0;atkCnt=0;if(--hp>0)invCnt=PLAYER_INVINCIBLE_DURATION;flashCounter=2;}
  }

  void draw(){
    uint8_t fr=0;
    if(!alive)fr=FD;else if(kbCnt)fr=FKB;else if(!atkCnt&&!grounded)fr=FAIR;
    else{if(!atkCnt){if(!vx)fr=FI;else{if(Input::everyXFrames(WFR))walkFr=!walkFr;fr=walkFr?FW2:FW1;}}else if(knifeAtk||atkCnt<PLAYER_ATTACK_CHARGE)fr=FA;else fr=FAC;if(ducking)fr++;}
    if(Input::everyXFrames(4)&&!kbCnt&&invCnt)visible=!visible;
    if(visible){
      Display::drawPlusMask(pos.x-SOX-Game::cameraX,pos.y-SOY,player_plus_mask,fr+(flipped?FFO:0));
      if(atkCnt&&!knifeAtk&&atkCnt<PLAYER_ATTACK_CHARGE)Display::drawPlusMask(pos.x+(flipped?-24:8)-Game::cameraX,pos.y-(ducking?4:12),flipped?player_attack_left_plus_mask:player_attack_right_plus_mask,0);
    }
    if(knife)Display::drawPlusMask(kpos.x-Game::cameraX,kpos.y,entity_knife_plus_mask,(uint8_t)knifeFlip);
  }
}

// ============================================================
// GAME
// ============================================================
namespace Game {
  int16_t cameraX=0; uint8_t life=GAME_STARTING_LIFE; uint16_t timeLeft=GAME_STARTING_TIME,score=0;
  static const uint8_t* const _lvl[]={stage_1_1,stage_1_2,stage_1_3,stage_1_4,stage_2_1,stage_2_2,stage_2_3,stage_2_4,stage_3_1,stage_3_2,stage_3_3,stage_3_4};
  static bool _paused=false,_fin=false; static uint8_t _li=0,_pc=0;

  static void _hp(int16_t x,int16_t y,uint8_t v,uint8_t mx){Display::fillRect(x,y,4*mx,7,0);for(uint8_t i=0;i<mx;i++)Display::drawSelfMasked(x+i*4,y,i<v?ui_hp_full:ui_hp_empty,0);}
  void reset(){_li=0;life=GAME_STARTING_LIFE;score=0;Player::hp=PLAYER_MAX_HP;Player::knifeCount=0;}
  void play(){_paused=false;_fin=false;mainState=STATE_PLAY;Entities::init();Map::init(_lvl[_li]);cameraX=0;_pc=0;if(Map::boss)_pc=120;}

  void loop(){
    if(_paused){Display::fillRect(44,28,40,9,0);Display::drawOverwrite(44,29,text_paused,0);if(Input::justPressed(B_BUTTON)||Input::justPressed(A_BUTTON))_paused=false;return;}
    if(Input::pressed(DOWN_BUTTON)&&Input::justPressed(A_BUTTON)){_paused=true;return;}
    if(_pc>0)--_pc;else{Player::update();Entities::update();}
    if(!_fin){if(timeLeft>0)--timeLeft;
      if(Player::pos.x-4>Map::width*TILE_WIDTH){++_li;Menu::notifyLevelFinished();_fin=true;}
      else if(Map::boss&&!Map::boss->hp){++_li;Menu::notifyLevelFinished();_fin=true;}
      else if(!Player::alive||!timeLeft){Player::alive=false;Player::knifeCount=0;if(!timeLeft)life=0;else{timeLeft+=GAME_EXTRA_TIME;--life;}Menu::notifyPlayerDied();_fin=true;}
    }
    if(Player::pos.x<cameraX+CAMERA_LEFT_BUFFER){cameraX=Player::pos.x-CAMERA_LEFT_BUFFER;if(cameraX<0)cameraX=0;}
    else if(Player::pos.x>cameraX+SCREEN_W-CAMERA_RIGHT_BUFFER){cameraX=Player::pos.x-SCREEN_W+CAMERA_RIGHT_BUFFER;if(cameraX>Map::width*TILE_WIDTH-SCREEN_W)cameraX=Map::width*TILE_WIDTH-SCREEN_W;}
    if(Map::showBackground)Display::drawOverwrite(16-cameraX/28,4,background_mountain,0);
    Map::draw();Entities::draw();Player::draw();
    _hp(0,0,Player::hp,PLAYER_MAX_HP);Display::fillRect(54,0,13,7,0);Display::drawSelfMasked(55,0,ui_knife_count,0);Display::drawNumber(68,0,Player::knifeCount,ALIGN_LEFT);Display::drawNumber(128,0,timeLeft/FPS,ALIGN_RIGHT);
    if(Map::boss)_hp(40,58,Map::boss->hp,BOSS_MAX_HP);
  }
  bool moveY(Vec& pos,int8_t dy,const Box& hb,bool cte){int8_t s=dy>0?1:-1;while(dy){if(Map::collide(pos.x,pos.y+s,hb)||(cte&&Entities::moveCollide(pos.x,pos.y,0,s,hb)))return true;pos.y+=s;dy-=s;}return false;}
}

// ============================================================
// MENU
// ============================================================
namespace Menu {
#define TOPT_MAX  2
#define TOPT_PLAY 0
#define TOPT_HELP 1
#define TOPT_SFX  2
#define BX  70
#define BXX 140
#define BXXXX 280
  static uint8_t _stg=1,_cnt=60,_st=0,_mi=0; static bool _tog=false; static int8_t _off=0;
  static const uint16_t _beat[] PROGMEM={NOTE_C5,BX,NOTE_C4,BX,0,BXX,NOTE_C2,BXXXX,0,BXX,NOTE_C2,BXXXX,0,BXX,NOTE_C2,BXXXX,0,BXX,0,BXX,0,BXX,0,BXX,NOTE_C4,BXXXX,NOTE_C4,BX,NOTE_C4,BX,NOTE_C2,BXXXX,0,BXX,NOTE_C3,BXX,0,BXX,NOTE_C2,BXXXX,0,BXX,NOTE_C2,BXXXX,0,BXX,0,BXX,0,BXX,NOTE_C2,BXXXX,TONES_REPEAT};

  static void _opt(uint8_t idx,const uint8_t* spr){uint8_t hw=pgm_read_byte(spr)/2;Display::drawOverwrite(64-hw,40+idx*8,spr,0);if(idx==_mi){Display::drawOverwrite(55-hw,38+idx*8,entity_candle,_tog);Display::drawOverwrite(68+hw,38+idx*8,entity_candle,_tog);}}

  void showTitle(){mainState=STATE_TITLE;_st=0;_cnt=60;_mi=0;_stg=1;Player::hp=PLAYER_MAX_HP;Game::reset();}
  void notifyPlayerDied(){mainState=STATE_PLAYER_DIED;_cnt=140;}
  void notifyLevelFinished(){if(Map::boss){mainState=STATE_STAGE_FINISHED;_cnt=80;}else{mainState=STATE_LEVEL_FINISHED;_cnt=40;}}
  static void _stageIntro(){mainState=STATE_STAGE_INTRO;_cnt=180;}

  static void _title(){
    if(Input::everyXFrames(20))_tog=!_tog;
    if(_st==0){_off=_cnt*2;if(!--_cnt){_off=1;_st=1;flashCounter=6;}}
    else{if(Input::everyXFrames(80))_off=-_off;
      if(Input::justPressed(UP_BUTTON)&&_mi>0)--_mi;if(Input::justPressed(DOWN_BUTTON)&&_mi<TOPT_MAX)++_mi;
      if(Input::justPressed(A_BUTTON)){switch(_mi){case TOPT_PLAY:mainState=STATE_STAGE_INTRO;_cnt=100;break;case TOPT_HELP:mainState=STATE_HELP;break;}}
      _opt(TOPT_PLAY,text_play);_opt(TOPT_HELP,text_help);_opt(TOPT_SFX,text_sfx_off);}
    Display::drawOverwrite(36,2-_off,title_left,0);Display::drawOverwrite(69,2+_off,title_right,0);
  }
  static void _over(){
    if(Input::everyXFrames(4)){if(!--_cnt){if(_st==0){flashCounter=6;_st=1;}_cnt=40;}}
    uint8_t yo=_st>0?0:_cnt;Display::drawOverwrite(2,48+yo/2,background_mountain,0);Display::drawOverwrite(0,44+yo,end_hill,0);Display::drawOverwrite(20,36+yo,tileset,8);Display::drawOverwrite(47,8-yo,text_game_over,0);
    if(_st==1){Display::drawOverwrite(54,26,text_score,0);Display::drawNumber(64,34,Game::score,ALIGN_CENTER);if(Input::justPressed(A_BUTTON))showTitle();}
  }
  static void _finished(){
    if(Input::everyXFrames(8)){if(!--_cnt){if(_st==0){_st=1;_cnt=30;}else{if(_st<7){if(++_st==7)flashCounter=6;}_cnt=80;}}}
    switch(_st){case 2:Display::drawOverwrite(36,-32+_cnt,title_left,0);Display::drawOverwrite(69,-32+_cnt,title_right,0);break;case 3:Display::drawOverwrite(49,-32+_cnt,end_zcpp,0);break;case 4:Display::drawOverwrite(44,-32+_cnt,end_zappedcow,0);break;case 5:Display::drawOverwrite(44,-32+_cnt,end_increment,0);break;case 6:Display::drawOverwrite(50,8+_cnt,text_the_end,0);break;case 7:Display::drawOverwrite(50,8,text_the_end,0);Display::drawOverwrite(54,26,text_score,0);Display::drawNumber(64,34,Game::score,ALIGN_CENTER);if(Input::justPressed(A_BUTTON))showTitle();break;}
    uint8_t pf=(_st>0&&(_cnt%20)<10)?1:0,yo=_st>0?0:_cnt;Display::drawOverwrite(2,48+yo/2,background_mountain,0);Display::drawOverwrite(0,44+yo,end_hill,0);Display::drawOverwrite(16,28+yo,end_player,pf);
  }

  void loop(){
    switch(mainState){
      case STATE_TITLE:_title();break;
      case STATE_HELP:
        OLED_ShowString8(2, 0,  "CONTROLS");
        OLED_ShowString8(2, 10, "UP    Move Right");
        OLED_ShowString8(2, 19, "DOWN  Move Left");
        OLED_ShowString8(2, 28, "SEL   Jump");
        OLED_ShowString8(2, 37, "BACK  Attack");
        OLED_ShowString8(2, 46, "UP+BK Throw Knife");
        OLED_ShowString8(2, 55, "DN+BK Duck");
        if(Input::justPressed(A_BUTTON))showTitle();
        break;
      case STATE_PLAY:Game::loop();break;
      case STATE_STAGE_INTRO:
        if(Input::everyXFrames(16))_tog=!_tog;Display::drawOverwrite(52,18,text_stage,0);Display::drawNumber(75,18,_stg,ALIGN_LEFT);Display::drawPlusMask(56,32,player_plus_mask,_tog?2:0);
        if(!--_cnt){Game::timeLeft=GAME_STARTING_TIME;Game::play();}break;
      case STATE_GAME_OVER:_over();break;
      case STATE_GAME_FINISHED:_finished();break;
      case STATE_LEVEL_FINISHED:if(!--_cnt)Game::play();break;
      case STATE_STAGE_FINISHED:
        if(Game::timeLeft>0){if(_cnt>0)--_cnt;else{Game::score+=SCORE_PER_SECOND;if(Game::timeLeft>FPS)Game::timeLeft-=FPS;else{Game::timeLeft=0;_cnt=60;}}}
        else if(Player::hp<PLAYER_MAX_HP){if(!--_cnt){++Player::hp;_cnt=(Player::hp==PLAYER_MAX_HP)?90:20;}}
        else if(!--_cnt){if(_stg==STAGE_MAX){Game::score+=Game::life*SCORE_PER_LIFE;mainState=STATE_GAME_FINISHED;_cnt=32;_st=0;}else{++_stg;_stageIntro();}}
        Game::loop();if(Game::timeLeft>0){Display::fillRect(0,21,128,22,0);Display::drawOverwrite(54,23,text_score,0);Display::drawNumber(64,35,Game::score,ALIGN_CENTER);}break;
      case STATE_PLAYER_DIED:
        if(!--_cnt){if(!Game::life){mainState=STATE_GAME_OVER;_cnt=32;_st=0;}else{Player::hp=PLAYER_MAX_HP;Game::play();}}
        else if(_cnt<100){if(!Game::timeLeft)Display::drawOverwrite(47,29,text_time_up,0);else{Display::drawOverwrite(57,29,ui_life_count,0);if(_cnt>80)Display::drawNumber(69,29,Game::life+1,ALIGN_LEFT);else if(_cnt>70)Display::drawNumber(69,28,Game::life,ALIGN_LEFT);else Display::drawNumber(69,29,Game::life,ALIGN_LEFT);}}
        else Game::loop();break;
    }
  }
}

} // namespace CB  ← end of everything inside CB

// ============================================================
// TOP-LEVEL — outside CB, called directly from .ino
// ============================================================
static bool     _bootDone        = false;
static uint8_t  _bootGuardFrames = 0;   // ignore input for first N frames
static uint32_t _lastFrameMs     = 0;
static uint32_t _backHoldStart   = 0;
static const uint32_t _FRAME_MS  = 1000 / FPS;
static const uint32_t _EXIT_HOLD = 10000; // hold BACK 2s to exit

void castleboy_setup() {
  // NOTE: OLED_Init() already called by ui_init() — don't call it again here.
  CB::Input::init();
  _bootDone        = false;
  _bootGuardFrames = 0;
  _lastFrameMs     = 0;
  _backHoldStart   = 0;
  CB::frameCount   = 0;
  CB::mainState    = STATE_TITLE;
  CB::Menu::showTitle();
}

// Returns true while the game should keep running.
// Returns false when the player holds BACK for 10 seconds (exit signal).
bool castleboy_loop() {
  // Exit check: hold BACK for EXIT_HOLD ms
  if (digitalRead(BTN_BACK) == LOW) {
    if (_backHoldStart == 0) _backHoldStart = millis();
    if (millis() - _backHoldStart >= _EXIT_HOLD) {
      _backHoldStart = 0;
      _bootDone = false;   // reset for next launch
      return false;         // tell caller to exit
    }
  } else {
    _backHoldStart = 0;
  }

  // Frame rate limiter
  uint32_t now = millis();
  if (now - _lastFrameMs < _FRAME_MS) return true;
  _lastFrameMs = now;
  CB::frameCount++;

  CB::Input::poll();
  CB::Display::clear();

  // Boot / controls screen
  if (!_bootDone) {
    if (_bootGuardFrames < 20) _bootGuardFrames++;

    // Top row blinks between two messages
    uint8_t phase = (CB::frameCount / 20) % 2;
    if (phase == 0) OLED_ShowString8(22, 0, "CONTROLS");
    else            OLED_ShowString8(4,  0, ">> Press SELECT <<");
    

    // Divider line
    for (uint8_t i = 0; i < 128; i++) OLED_DrawPoint(i, 9);

    // Controls list — 6px font, 9px row height fits 6 rows in remaining 55px
    OLED_ShowString8(0, 11, "UP   = Move Right");
    OLED_ShowString8(0, 20, "DOWN = Move Left");
    OLED_ShowString8(0, 29, "SEL  = Jump/Confirm");
    OLED_ShowString8(0, 38, "BACK = Sword Attack");
    OLED_ShowString8(0, 47, "UP+BACK = Knife");
    OLED_ShowString8(0, 56, "DN+BACK = Duck");

    if (_bootGuardFrames >= 20 && CB::Input::justPressed(A_BUTTON)) {
      _bootDone = true;
    }

    CB::Display::display();
    return true;
  }

  CB::Menu::loop();

  if (CB::flashCounter > 0) {
    CB::Display::fillRect(0, 0, SCREEN_W, SCREEN_H, 1);
    CB::flashCounter--;
  }
  CB::Display::display();
  return true;
}
