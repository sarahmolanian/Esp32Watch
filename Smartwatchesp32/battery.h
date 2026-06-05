#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

void battery_init();
void battery_update();

float battery_getPercentage();
void IRAM_ATTR battery_isr();

#endif