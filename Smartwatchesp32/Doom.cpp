#include "Doom.h"
#include "level.h"
#include "sprites.h"
#include "input.h"
#include "display.h"
#include "sound.h"
#include "oled_driver.h"
#include <math.h>

#define doom_swap(a, b)  do { __typeof__(a) _t = (a); (a) = (b); (b) = _t; } while(0)
#define doom_sign(a, b)  (double)((a) > (b) ? 1 : ((b) > (a) ? -1 : 0))

// Scene state
uint8_t doom_scene           = INTRO;
static bool    doom_exit_scene    = false;
static bool    doom_invert_screen = false;
static uint8_t doom_flash_screen  = 0;

// Game objects
Player       doom_player;
static Entity       doom_entity[MAX_ENTITIES];
static StaticEntity doom_static_entity[MAX_STATIC_ENTITIES];
static uint8_t      doom_num_entities        = 0;
static uint8_t      doom_num_static_entities = 0;

// ── Public entry points ───────────────────────────────────────────────────────

void doom_setup() {
    setupDisplay();
    input_setup();
    sound_init();
}

void doom_loop() {
    switch (doom_scene) {
        case INTRO:     doom_loopIntro();    break;
        case GAME_PLAY: doom_loopGamePlay(); break;
    }
    for (uint8_t i = 0; i < GRADIENT_COUNT; i++) {
        fadeScreen(i, false);
        displayFlush();
        delay(40);
    }
    doom_exit_scene = false;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void doom_jumpTo(uint8_t target_scene) {
    doom_scene      = target_scene;
    doom_exit_scene = true;
}

uint8_t doom_getBlockAt(const uint8_t level[], uint8_t x, uint8_t y) {
    if (x >= LEVEL_WIDTH || y >= LEVEL_HEIGHT) return E_FLOOR;
    return pgm_read_byte(level + (((LEVEL_HEIGHT - 1 - y) * LEVEL_WIDTH + x) / 2))
           >> (!(x % 2) * 4) & 0b1111;
}

void doom_initializeLevel(const uint8_t level[]) {
    for (int8_t y = LEVEL_HEIGHT - 1; y >= 0; y--)
        for (uint8_t x = 0; x < LEVEL_WIDTH; x++)
            if (doom_getBlockAt(level, x, (uint8_t)y) == E_PLAYER) {
                doom_player = create_player(x, y);
                return;
            }
}

bool doom_isSpawned(UID uid) {
    for (uint8_t i = 0; i < doom_num_entities; i++)
        if (doom_entity[i].uid == uid) return true;
    return false;
}

bool doom_isStatic(UID uid) {
    for (uint8_t i = 0; i < doom_num_static_entities; i++)
        if (doom_static_entity[i].uid == uid) return true;
    return false;
}

void doom_spawnEntity(uint8_t type, uint8_t x, uint8_t y) {
    if (doom_num_entities >= MAX_ENTITIES) return;
    switch (type) {
        case E_ENEMY:   doom_entity[doom_num_entities++] = create_enemy(x, y);   break;
        case E_KEY:     doom_entity[doom_num_entities++] = create_key(x, y);     break;
        case E_MEDIKIT: doom_entity[doom_num_entities++] = create_medikit(x, y); break;
    }
}

void doom_spawnFireball(double x, double y) {
    if (doom_num_entities >= MAX_ENTITIES) return;
    UID uid = create_uid(E_FIREBALL, (uint8_t)x, (uint8_t)y);
    if (doom_isSpawned(uid)) return;
    int16_t dir = FIREBALL_ANGLES
                  + (int16_t)(atan2(y - doom_player.pos.y,
                                    x - doom_player.pos.x) / PI * FIREBALL_ANGLES);
    if (dir < 0) dir += FIREBALL_ANGLES * 2;
    doom_entity[doom_num_entities++] = create_fireball(x, y, dir);
}

void doom_removeEntity(UID uid, bool makeStatic /* default false in header */) {
    uint8_t i = 0; bool found = false;
    while (i < doom_num_entities) {
        if (!found && doom_entity[i].uid == uid) { found = true; doom_num_entities--; }
        if (found) doom_entity[i] = doom_entity[i + 1];
        i++;
    }
}

void doom_removeStaticEntity(UID uid) {
    uint8_t i = 0; bool found = false;
    while (i < doom_num_static_entities) {
        if (!found && doom_static_entity[i].uid == uid) { found = true; doom_num_static_entities--; }
        if (found) doom_static_entity[i] = doom_static_entity[i + 1];
        i++;
    }
}

UID doom_detectCollision(const uint8_t level[], Coords* pos,
                         double rel_x, double rel_y, bool only_walls) {
    uint8_t rx = (uint8_t)(pos->x + rel_x);
    uint8_t ry = (uint8_t)(pos->y + rel_y);
    uint8_t block = doom_getBlockAt(level, rx, ry);
    if (block == E_WALL) { playSound(hit_wall_snd, HIT_WALL_SND_LEN); return create_uid(block, rx, ry); }
    if (only_walls) return UID_null;
    for (uint8_t i = 0; i < doom_num_entities; i++) {
        if (&(doom_entity[i].pos) == pos) continue;
        uint8_t type = uid_get_type(doom_entity[i].uid);
        if (type != E_ENEMY || doom_entity[i].state == S_DEAD || doom_entity[i].state == S_HIDDEN) continue;
        Coords nc = { doom_entity[i].pos.x - rel_x, doom_entity[i].pos.y - rel_y };
        uint8_t dist = coords_distance(pos, &nc);
        if (dist < ENEMY_COLLIDER_DIST && dist < doom_entity[i].distance) return doom_entity[i].uid;
    }
    return UID_null;
}

UID doom_updatePosition(const uint8_t level[], Coords* pos,
                        double rel_x, double rel_y, bool only_walls) {
    UID cx = doom_detectCollision(level, pos, rel_x, 0, only_walls);
    UID cy = doom_detectCollision(level, pos, 0, rel_y, only_walls);
    if (!cx) pos->x += rel_x;
    if (!cy) pos->y += rel_y;
    return cx || cy ? cx || cy : UID_null;
}

void doom_fire() {
    playSound(shoot_snd, SHOOT_SND_LEN);
    for (uint8_t i = 0; i < doom_num_entities; i++) {
        if (uid_get_type(doom_entity[i].uid) != E_ENEMY
            || doom_entity[i].state == S_DEAD
            || doom_entity[i].state == S_HIDDEN) continue;
        Coords t = doom_translateIntoView(&(doom_entity[i].pos));
        if (abs(t.x) < 20 && t.y > 0) {
            uint8_t dmg = (uint8_t)min((double)GUN_MAX_DAMAGE,
                (double)GUN_MAX_DAMAGE / (abs(t.x) * doom_entity[i].distance) / 5.0);
            if (dmg > 0) {
                doom_entity[i].health = (uint8_t)max(0, (int)doom_entity[i].health - dmg);
                doom_entity[i].state  = S_HIT;
                doom_entity[i].timer  = 4;
            }
        }
    }
}

void doom_updateEntities(const uint8_t level[]) {
    uint8_t i = 0;
    while (i < doom_num_entities) {
        doom_entity[i].distance = coords_distance(&(doom_player.pos), &(doom_entity[i].pos));
        if (doom_entity[i].timer > 0) doom_entity[i].timer--;
        if (doom_entity[i].distance > MAX_ENTITY_DISTANCE) { doom_removeEntity(doom_entity[i].uid); continue; }
        if (doom_entity[i].state == S_HIDDEN) { i++; continue; }
        uint8_t type = uid_get_type(doom_entity[i].uid);
        switch (type) {
            case E_ENEMY: {
                if (doom_entity[i].health == 0) {
                    if (doom_entity[i].state != S_DEAD) { doom_entity[i].state = S_DEAD; doom_entity[i].timer = 6; }
                } else if (doom_entity[i].state == S_HIT) {
                    if (doom_entity[i].timer == 0) { doom_entity[i].state = S_ALERT; doom_entity[i].timer = 40; }
                } else if (doom_entity[i].state == S_FIRING) {
                    if (doom_entity[i].timer == 0) { doom_entity[i].state = S_ALERT; doom_entity[i].timer = 40; }
                } else {
                    if (doom_entity[i].distance > ENEMY_MELEE_DIST && doom_entity[i].distance < MAX_ENEMY_VIEW) {
                        if (doom_entity[i].state != S_ALERT) {
                            doom_entity[i].state = S_ALERT; doom_entity[i].timer = 20;
                        } else if (doom_entity[i].timer == 0) {
                            doom_spawnFireball(doom_entity[i].pos.x, doom_entity[i].pos.y);
                            doom_entity[i].state = S_FIRING; doom_entity[i].timer = 6;
                        } else {
                            doom_updatePosition(level, &(doom_entity[i].pos),
                                doom_sign(doom_player.pos.x, doom_entity[i].pos.x) * ENEMY_SPEED * delta,
                                doom_sign(doom_player.pos.y, doom_entity[i].pos.y) * ENEMY_SPEED * delta, true);
                        }
                    } else if (doom_entity[i].distance <= ENEMY_MELEE_DIST) {
                        if (doom_entity[i].state != S_MELEE) {
                            doom_entity[i].state = S_MELEE; doom_entity[i].timer = 10;
                        } else if (doom_entity[i].timer == 0) {
                            doom_player.health = (uint8_t)max(0, (int)doom_player.health - ENEMY_MELEE_DAMAGE);
                            doom_entity[i].timer = 14; doom_flash_screen = 1; doom_updateHud();
                        }
                    } else { doom_entity[i].state = S_STAND; }
                }
                break;
            }
            case E_FIREBALL: {
                if (doom_entity[i].distance < FIREBALL_COLLIDER_DIST) {
                    doom_player.health = (uint8_t)max(0, (int)doom_player.health - ENEMY_FIREBALL_DAMAGE);
                    doom_flash_screen = 1; doom_updateHud();
                    doom_removeEntity(doom_entity[i].uid); continue;
                } else {
                    UID hit = doom_updatePosition(level, &(doom_entity[i].pos),
                        cos((double)doom_entity[i].health / FIREBALL_ANGLES * PI) * FIREBALL_SPEED,
                        sin((double)doom_entity[i].health / FIREBALL_ANGLES * PI) * FIREBALL_SPEED, true);
                    if (hit) { doom_removeEntity(doom_entity[i].uid); continue; }
                }
                break;
            }
            case E_MEDIKIT: {
                if (doom_entity[i].distance < ITEM_COLLIDER_DIST) {
                    playSound(medkit_snd, MEDKIT_SND_LEN);
                    doom_entity[i].state = S_HIDDEN;
                    doom_player.health = (uint8_t)min(100, (int)doom_player.health + 50);
                    doom_updateHud(); doom_flash_screen = 1;
                }
                break;
            }
            case E_KEY: {
                if (doom_entity[i].distance < ITEM_COLLIDER_DIST) {
                    playSound(get_key_snd, GET_KEY_SND_LEN);
                    doom_entity[i].state = S_HIDDEN;
                    doom_player.keys++; doom_updateHud(); doom_flash_screen = 1;
                }
                break;
            }
        }
        i++;
    }
}

void doom_renderMap(const uint8_t level[], double view_height) {
    UID last_uid = 0;
    for (uint8_t x = 0; x < SCREEN_WIDTH; x += RES_DIVIDER) {
        double cam_x = 2.0 * x / SCREEN_WIDTH - 1.0;
        double ray_x = doom_player.dir.x + doom_player.plane.x * cam_x;
        double ray_y = doom_player.dir.y + doom_player.plane.y * cam_x;
        uint8_t map_x = (uint8_t)doom_player.pos.x;
        uint8_t map_y = (uint8_t)doom_player.pos.y;
        Coords mc = { doom_player.pos.x, doom_player.pos.y };
        double delta_x = (ray_x == 0) ? 1e30 : fabs(1.0 / ray_x);
        double delta_y = (ray_y == 0) ? 1e30 : fabs(1.0 / ray_y);
        int8_t step_x, step_y;
        double side_x, side_y;
        if (ray_x < 0) { step_x = -1; side_x = (doom_player.pos.x - map_x) * delta_x; }
        else           { step_x =  1; side_x = (map_x + 1.0 - doom_player.pos.x) * delta_x; }
        if (ray_y < 0) { step_y = -1; side_y = (doom_player.pos.y - map_y) * delta_y; }
        else           { step_y =  1; side_y = (map_y + 1.0 - doom_player.pos.y) * delta_y; }
        uint8_t depth = 0; bool hit = false; bool side = false;
        while (!hit && depth < MAX_RENDER_DEPTH) {
            if (side_x < side_y) { side_x += delta_x; map_x += step_x; side = false; }
            else                  { side_y += delta_y; map_y += step_y; side = true;  }
            uint8_t block = doom_getBlockAt(level, map_x, map_y);
            if (block == E_WALL) { hit = true; }
            else {
                mc = { (double)map_x, (double)map_y };
                if (block == E_ENEMY || (block & 0x08)) {
                    if (coords_distance(&(doom_player.pos), &mc) < MAX_ENTITY_DISTANCE) {
                        UID uid = create_uid(block, map_x, map_y);
                        if (last_uid != uid && !doom_isSpawned(uid)) { doom_spawnEntity(block, map_x, map_y); last_uid = uid; }
                    }
                }
            }
            depth++;
        }
        if (hit) {
            double distance = (side == false)
                ? max(1.0, (map_x - doom_player.pos.x + (1 - step_x) / 2.0) / ray_x)
                : max(1.0, (map_y - doom_player.pos.y + (1 - step_y) / 2.0) / ray_y);
            zbuffer[x / Z_RES_DIVIDER] = (uint8_t)min(distance * DISTANCE_MULTIPLIER, 255.0);
            uint8_t line_height = (uint8_t)(RENDER_HEIGHT / distance);
            drawVLine(x,
                (int8_t)(view_height / distance - line_height / 2 + RENDER_HEIGHT / 2),
                (int8_t)(view_height / distance + line_height / 2 + RENDER_HEIGHT / 2),
                GRADIENT_COUNT - (int)(distance / MAX_RENDER_DEPTH * GRADIENT_COUNT) - side * 2);
        }
    }
}

void doom_sortEntities() {
    uint8_t gap = doom_num_entities; bool swapped = false;
    while (gap > 1 || swapped) {
        gap = (gap * 10) / 13;
        if (gap == 9 || gap == 10) gap = 11;
        if (gap < 1) gap = 1;
        swapped = false;
        for (uint8_t i = 0; i < doom_num_entities - gap; i++) {
            uint8_t j = i + gap;
            if (doom_entity[i].distance < doom_entity[j].distance) {
                doom_swap(doom_entity[i], doom_entity[j]); swapped = true;
            }
        }
    }
}

Coords doom_translateIntoView(Coords* pos) {
    double sx = pos->x - doom_player.pos.x;
    double sy = pos->y - doom_player.pos.y;
    double inv = 1.0 / (doom_player.plane.x * doom_player.dir.y - doom_player.dir.x * doom_player.plane.y);
    return { inv * (doom_player.dir.y * sx - doom_player.dir.x * sy),
             inv * (-doom_player.plane.y * sx + doom_player.plane.x * sy) };
}

void doom_renderEntities(double view_height) {
    doom_sortEntities();
    for (uint8_t i = 0; i < doom_num_entities; i++) {
        if (doom_entity[i].state == S_HIDDEN) continue;
        Coords t = doom_translateIntoView(&(doom_entity[i].pos));
        if (t.y <= 0.1 || t.y > MAX_SPRITE_DEPTH) continue;
        int16_t sx = (int16_t)(HALF_WIDTH * (1.0 + t.x / t.y));
        int8_t  sy = (int8_t)(RENDER_HEIGHT / 2 + view_height / t.y);
        uint8_t type = uid_get_type(doom_entity[i].uid);
        if (sx < -HALF_WIDTH || sx > SCREEN_WIDTH + HALF_WIDTH) continue;
        switch (type) {
            case E_ENEMY: {
                uint8_t sprite;
                if      (doom_entity[i].state == S_ALERT)  sprite = (int)(millis()/500)%2;
                else if (doom_entity[i].state == S_FIRING)  sprite = 2;
                else if (doom_entity[i].state == S_HIT)     sprite = 3;
                else if (doom_entity[i].state == S_MELEE)   sprite = doom_entity[i].timer > 10 ? 2 : 1;
                else if (doom_entity[i].state == S_DEAD)    sprite = doom_entity[i].timer > 0  ? 3 : 4;
                else sprite = 0;
                drawSprite((int8_t)(sx - BMP_IMP_WIDTH * 0.5 / t.y), (int8_t)(sy - 8 / t.y),
                    bmp_imp_bits, bmp_imp_mask, BMP_IMP_WIDTH, BMP_IMP_HEIGHT, sprite, t.y);
                break;
            }
            case E_FIREBALL:
                drawSprite((int8_t)(sx - BMP_FIREBALL_WIDTH/2/t.y), (int8_t)(sy - BMP_FIREBALL_HEIGHT/2/t.y),
                    bmp_fireball_bits, bmp_fireball_mask, BMP_FIREBALL_WIDTH, BMP_FIREBALL_HEIGHT, 0, t.y);
                break;
            case E_MEDIKIT:
                drawSprite((int8_t)(sx - BMP_ITEMS_WIDTH/2/t.y), (int8_t)(sy + 5/t.y),
                    bmp_items_bits, bmp_items_mask, BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, 0, t.y);
                break;
            case E_KEY:
                drawSprite((int8_t)(sx - BMP_ITEMS_WIDTH/2/t.y), (int8_t)(sy + 5/t.y),
                    bmp_items_bits, bmp_items_mask, BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, 1, t.y);
                break;
        }
    }
}

void doom_renderGun(uint8_t gun_pos, double amount_jogging) {
    char gx = (char)(48 + (int8_t)(sin((double)millis() * JOGGING_SPEED) * 10 * amount_jogging));
    char gy = (char)(RENDER_HEIGHT - gun_pos
              + (int8_t)(fabs(cos((double)millis() * JOGGING_SPEED)) * 8 * amount_jogging));
    if (gun_pos > GUN_SHOT_POS - 2) {
        const int16_t fw = BMP_FIRE_WIDTH, fh = BMP_FIRE_HEIGHT;
        int16_t bw = (fw + 7) / 8;
        for (int16_t j = 0; j < fh; j++)
            for (int16_t k = 0; k < fw; k++) {
                uint8_t b = pgm_read_byte(&bmp_fire_bits[j * bw + k / 8]);
                if (b & (0x80 >> (k & 7))) drawPixel(gx + 6 + k, gy - 11 + j, 1, false);
            }
    }
    uint8_t clip_height = (uint8_t)max(0, min((int)gy + BMP_GUN_HEIGHT, (int)RENDER_HEIGHT) - (int)gy);
    const int16_t gw = BMP_GUN_WIDTH;
    int16_t gbw = (gw + 7) / 8;
    for (int16_t j = 0; j < (int16_t)clip_height; j++)
        for (int16_t k = 0; k < gw; k++) {
            uint8_t mb = pgm_read_byte(&bmp_gun_mask[j * gbw + k / 8]);
            if (mb & (0x80 >> (k & 7))) drawPixel(gx + k, gy + j, 0, false);
            uint8_t sb = pgm_read_byte(&bmp_gun_bits[j * gbw + k / 8]);
            if (sb & (0x80 >> (k & 7))) drawPixel(gx + k, gy + j, 1, false);
        }
}

void doom_renderHud() {
    drawText(2, 58, F("{}"), 0);
    drawText(40, 58, F("[]"), 0);
    doom_updateHud();
}

void doom_updateHud() {
    for (uint8_t col = 12; col < 12 + 15; col++) OLED_GRAM[col][58/8] &= ~(0xFF << (58 & 7));
    for (uint8_t col = 50; col < 50 + 5;  col++) OLED_GRAM[col][58/8] &= ~(0xFF << (58 & 7));
    drawText(12, 58, doom_player.health);
    drawText(50, 58, doom_player.keys);
}

void doom_renderStats() {
    drawText(114, 58, (uint8_t)getActualFps());
    drawText(82,  58, doom_num_entities);
}

void doom_loopIntro() {
    const int16_t lw = BMP_LOGO_WIDTH, lh = BMP_LOGO_HEIGHT;
    int16_t lx = (SCREEN_WIDTH - lw) / 2;
    int16_t ly = (SCREEN_HEIGHT - lh) / 3;
    int16_t lbw = (lw + 7) / 8;
    OLED_BufferClear();
    for (int16_t j = 0; j < lh; j++)
        for (int16_t k = 0; k < lw; k++) {
            uint8_t b = pgm_read_byte(&bmp_logo_bits[j * lbw + k / 8]);
            if (b & (0x80 >> (k & 7))) drawPixel(lx + k, ly + j, 1, false);
        }
    delay(1000);
    drawText((int8_t)(SCREEN_WIDTH / 2 - 25), (int8_t)(SCREEN_HEIGHT * 0.8), F("PRESS FIRE"));
    displayFlush();
    while (!doom_exit_scene) {
        if (input_fire()) doom_jumpTo(GAME_PLAY);
        delay(50);
    }
}

void doom_loopGamePlay() {
    bool     gun_fired       = false;
    bool     walkSoundToggle = false;
    uint8_t  gun_pos         = 0;
    double   rot_speed, old_dir_x, old_plane_x;
    double   view_height = 0, jogging = 0;
    uint8_t  fade = GRADIENT_COUNT - 1;

    const uint32_t BACK_EXIT_MS = 10000UL;
    uint32_t back_held_since = 0;
    bool     back_was_held   = false;

    doom_num_entities        = 0;
    doom_num_static_entities = 0;
    doom_initializeLevel(sto_level_1);

    do {
        fps();
        displayClearViewport();

        bool back_now = input_left();
        if (back_now) {
            if (!back_was_held) { back_held_since = millis(); back_was_held = true; }
            if (millis() - back_held_since >= BACK_EXIT_MS) doom_jumpTo(INTRO);
        } else {
            back_was_held = false;
        }

        if (doom_player.health > 0) {
            if (input_up() && !back_now) {
                doom_player.velocity += (MOV_SPEED - doom_player.velocity) * 0.4;
                jogging = fabs(doom_player.velocity) * MOV_SPEED_INV;
            } else if (input_down()) {
                doom_player.velocity += (-MOV_SPEED - doom_player.velocity) * 0.4;
                jogging = fabs(doom_player.velocity) * MOV_SPEED_INV;
            } else {
                doom_player.velocity *= 0.5;
                jogging = fabs(doom_player.velocity) * MOV_SPEED_INV;
            }

            rot_speed = ROT_SPEED * delta;

            if (back_now) {
                double angle = input_up() ? -rot_speed : rot_speed;
                old_dir_x           = doom_player.dir.x;
                doom_player.dir.x   = doom_player.dir.x   * cos(angle) - doom_player.dir.y   * sin(angle);
                doom_player.dir.y   = old_dir_x            * sin(angle) + doom_player.dir.y   * cos(angle);
                old_plane_x         = doom_player.plane.x;
                doom_player.plane.x = doom_player.plane.x  * cos(angle) - doom_player.plane.y * sin(angle);
                doom_player.plane.y = old_plane_x           * sin(angle) + doom_player.plane.y * cos(angle);
            }

            view_height = fabs(sin((double)millis() * JOGGING_SPEED)) * 6 * jogging;

            if (view_height > 5.9 && !sound) {
                if (walkSoundToggle) { playSound(walk1_snd, WALK1_SND_LEN); walkSoundToggle = false; }
                else                 { playSound(walk2_snd, WALK2_SND_LEN); walkSoundToggle = true;  }
            }

            if      (gun_pos > GUN_TARGET_POS)  { gun_pos--; }
            else if (gun_pos < GUN_TARGET_POS)  { gun_pos += 2; }
            else if (!gun_fired && input_fire()) { gun_pos = GUN_SHOT_POS; gun_fired = true; doom_fire(); }
            else if (gun_fired && !input_fire()) { gun_fired = false; }

        } else {
            if (view_height > -10) view_height--;
            else if (input_fire()) doom_jumpTo(INTRO);
            if (gun_pos > 1) gun_pos -= 2;
        }

        if (fabs(doom_player.velocity) > 0.003) {
            doom_updatePosition(sto_level_1, &(doom_player.pos),
                doom_player.dir.x * doom_player.velocity * delta,
                doom_player.dir.y * doom_player.velocity * delta);
        } else {
            doom_player.velocity = 0;
        }

        doom_updateEntities(sto_level_1);
        doom_renderMap(sto_level_1, view_height);
        doom_renderEntities(view_height);
        doom_renderGun(gun_pos, jogging);

        if (fade > 0) {
            fadeScreen(fade); fade--;
            if (fade == 0) doom_renderHud();
        } else {
            doom_renderStats();
        }

        if (doom_flash_screen > 0) { doom_invert_screen = !doom_invert_screen; doom_flash_screen--; }
        else if (doom_invert_screen) { doom_invert_screen = false; }

        displayInvert(doom_invert_screen);
        displayFlush();

    } while (!doom_exit_scene);
}
