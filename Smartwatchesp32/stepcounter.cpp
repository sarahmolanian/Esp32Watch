#include <math.h>
#include <Arduino.h>
#include "stepcounter.h"
#include "i2c_soft.h"
#include "esp_attr.h"

#define LIS2DH12_ADDR 0x19
#define OUT_X_L       0x28

RTC_DATA_ATTR uint32_t rtc_savedSteps = 0;

static int16_t x, y, z;
static float filtered      = 0;
static unsigned long lastStepTime = 0;
static uint32_t stepCount  = 0;

#define STEP_THRESHOLD  0.17f
#define STEP_DELAY      350
#define FILTER_ALPHA    0.85f

void sc_writeRegister(uint8_t reg, uint8_t value) {
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(reg);
    soft_write(value);
    soft_endTransmission();
}

void sc_init() {
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x20); soft_write(0x57);
    soft_endTransmission();

    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x21); soft_write(0x09);
    soft_endTransmission();

    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x22); soft_write(0x40);
    soft_endTransmission();

    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x23); soft_write(0x08);
    soft_endTransmission();

    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x24); soft_write(0x08);
    soft_endTransmission();

    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x30); soft_write(0x2A);
    soft_endTransmission();

    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x32); soft_write(0x08);
    soft_endTransmission();

    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x33); soft_write(0x01);
    soft_endTransmission();

    Serial.println("LIS2DH12 initialized");
}

void sc_readRaw() {
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(OUT_X_L | 0x80);
    soft_endTransmission(false);
    soft_requestFrom(LIS2DH12_ADDR, 6);
    if (soft_available() == 6) {
        uint8_t xL = soft_read(), xH = soft_read();
        uint8_t yL = soft_read(), yH = soft_read();
        uint8_t zL = soft_read(), zH = soft_read();
        x = (int16_t)(xH << 8 | xL) >> 4;
        y = (int16_t)(yH << 8 | yL) >> 4;
        z = (int16_t)(zH << 8 | zL) >> 4;
    }
}

void sc_update() {
    sc_readRaw();

    float gx = x / 1000.0f;
    float gy = y / 1000.0f;
    float gz = z / 1000.0f;
    float magnitude = sqrt(gx*gx + gy*gy + gz*gz);

    filtered = FILTER_ALPHA * filtered + (1.0f - FILTER_ALPHA) * magnitude;
    float motion = magnitude - filtered;

    unsigned long now = millis();
    static bool wasAbove = false;
    bool isAbove = (motion > STEP_THRESHOLD);
    if (isAbove && !wasAbove) {
        if (now - lastStepTime > STEP_DELAY) {
            stepCount++;
            lastStepTime = now;
        }
    }
    wasAbove = isAbove;

    // Clear latched interrupt
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x31);
    soft_endTransmission(false);
    soft_requestFrom(LIS2DH12_ADDR, 1);
    soft_read();
}

void sc_saveStepsToRTC() {
    rtc_savedSteps = stepCount;
}

void sc_restoreStepsFromRTC() {
    stepCount = rtc_savedSteps;
}

void sc_getSteps_raw() {}  // unused

uint32_t sc_getSteps() {
    return stepCount;
}

void sc_resetSteps() {
    stepCount      = 0;
    rtc_savedSteps = 0;
}

void sc_setSteps(uint32_t s) { stepCount = s; }