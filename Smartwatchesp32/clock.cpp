#include "clock.h"

static ClockTime currentTime = {12, 0, 0};
static unsigned long lastTick = 0;

//----------------------------------

void clock_init() {
    lastTick = millis();
}

//----------------------------------

void clock_update() {
    if (millis() - lastTick >= 1000) {
        lastTick = millis();

        currentTime.second++;

        if (currentTime.second >= 60) {
            currentTime.second = 0;
            currentTime.minute++;
        }

        if (currentTime.minute >= 60) {
            currentTime.minute = 0;
            currentTime.hour++;
        }

        if (currentTime.hour >= 24) {
            currentTime.hour = 0;
        }
    }
}

//----------------------------------

ClockTime clock_getTime() {
    return currentTime;
}

//----------------------------------

void clock_setTime(int h, int m, int s) {
    currentTime.hour = h % 24;
    currentTime.minute = m % 60;
    currentTime.second = s % 60;
}