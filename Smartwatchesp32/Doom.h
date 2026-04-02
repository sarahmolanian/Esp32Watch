#ifndef _DOOM_H
#define _DOOM_H

/*
 * Doom.h / Doom.cpp
 * Self-contained Doom raycaster module for ESP32-C3 + SSD1309 128x64 OLED.
 *
 * Usage in your main sketch:
 *
 *   #include "Doom.h"
 *
 *   void setup() { doom_setup(); }
 *   void loop()  { doom_loop();  }
 *
 * doom_loop() runs one full scene (intro or gameplay) then returns,
 * so you can wrap it behind any condition in your own loop().
 */

#include <Arduino.h>
#include <pgmspace.h>
#include "constants.h"
#include "entities.h"
#include "types.h"

// ── Public API ────────────────────────────────────────────────────────────────
void doom_setup();
void doom_loop();

// Readable from outside at any time
extern uint8_t doom_scene;   // INTRO (0) or GAME_PLAY (1)
extern Player  doom_player;  // health, keys, pos …

// ── Forward declarations (all internal functions) ─────────────────────────────
// Default arguments live HERE (in the declaration) only — not in Doom.cpp.

void doom_jumpTo(uint8_t target_scene);

uint8_t doom_getBlockAt(const uint8_t level[], uint8_t x, uint8_t y);
void    doom_initializeLevel(const uint8_t level[]);

bool doom_isSpawned(UID uid);
bool doom_isStatic(UID uid);
void doom_spawnEntity(uint8_t type, uint8_t x, uint8_t y);
void doom_spawnFireball(double x, double y);
void doom_removeEntity(UID uid, bool makeStatic = false);
void doom_removeStaticEntity(UID uid);

UID doom_detectCollision(const uint8_t level[], Coords* pos,
                         double rel_x, double rel_y, bool only_walls = false);
UID doom_updatePosition(const uint8_t level[], Coords* pos,
                        double rel_x, double rel_y, bool only_walls = false);

void doom_fire();
void doom_updateEntities(const uint8_t level[]);

void doom_renderMap(const uint8_t level[], double view_height);
void doom_sortEntities();
Coords doom_translateIntoView(Coords* pos);
void doom_renderEntities(double view_height);
void doom_renderGun(uint8_t gun_pos, double amount_jogging);

void doom_renderHud();
void doom_updateHud();
void doom_renderStats();

void doom_loopIntro();
void doom_loopGamePlay();

#endif // _DOOM_H
