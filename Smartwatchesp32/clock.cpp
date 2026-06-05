#include "clock.h"
#include <sys/time.h>
#include <time.h>
#include "esp32-hal.h"
#include "soc/rtc.h"
#include <sys/time.h>
#include "esp_sleep.h"
#include "esp_attr.h"
#include "oled_driver.h"
#include "i2c_soft.h"
#include "stepcounter.h"

#define BTN_BACK_PIN  3
RTC_DATA_ATTR bool rtc_inShutdown = false;
RTC_DATA_ATTR uint32_t rtc_savedTimeSec = 0;

void clock_init() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        // Restore saved time from before sleep
        if (rtc_savedTimeSec > 0) {
            struct timeval tv = { .tv_sec = (time_t)rtc_savedTimeSec, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
        }
    }

    // Switch to external crystal — same as original
    rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_XTAL32K);

    // Seed to 12:00:00 if RTC lost power — same as original
    struct timeval tv_now;
    gettimeofday(&tv_now, nullptr);
    if (tv_now.tv_sec < 43200) {
        struct timeval tv = { .tv_sec = 43200, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
    }
}

void clock_update() {
    // intentionally empty — RTC ticks in hardware
}

ClockTime clock_getTime() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t now = tv.tv_sec;
    struct tm t;
    gmtime_r(&now, &t);
    ClockTime ct;
    ct.hour   = t.tm_hour;
    ct.minute = t.tm_min;
    ct.second = t.tm_sec;
    return ct;
}

void clock_setTime(int h, int m, int s) {
    struct timeval tv_now;
    gettimeofday(&tv_now, nullptr);
    time_t day_base = (tv_now.tv_sec / 86400) * 86400;
    time_t tod      = (h % 24) * 3600 + (m % 60) * 60 + (s % 60);
    struct timeval tv = { .tv_sec = day_base + tod, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
}

void clock_saveTimeToRTC() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    rtc_savedTimeSec = (uint32_t)tv.tv_sec;
}

void enterShutdown() {
    rtc_inShutdown = true;
    esp_deep_sleep_enable_gpio_wakeup(
        (1ULL << 3),
        ESP_GPIO_WAKEUP_GPIO_LOW
    );
    esp_deep_sleep_start();
}