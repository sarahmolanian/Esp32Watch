#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>

struct ClockTime {
    int hour;
    int minute;
    int second;
};

void clock_init();
void clock_update();

ClockTime clock_getTime();
void clock_setTime(int h, int m, int s);
void drawBattery(int x, int y, int percent);

#endif