#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>
#include "esp_attr.h"   // ← for RTC_DATA_ATTR

extern RTC_DATA_ATTR bool rtc_inShutdown;   
extern RTC_DATA_ATTR uint32_t rtc_savedTimeSec;

struct ClockTime {
    int hour;
    int minute;
    int second;
};

void clock_init();
void clock_update();
ClockTime clock_getTime();
void clock_setTime(int h, int m, int s);
void clock_saveTimeToRTC();
void enterShutdown();

#endif