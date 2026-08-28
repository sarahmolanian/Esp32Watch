#ifndef UI_H
#define UI_H

#include <Arduino.h>

void ui_init();
void ui_update();
extern volatile bool shutdownRequested;


// Wallpaper
void wallpaper_load();
void wallpaper_save(uint8_t index);
uint8_t wallpaper_get();

#endif