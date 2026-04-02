#include <math.h>
#include <Arduino.h>
#include "stepcounter.h"
#include "i2c_soft.h"

#define LIS2DH12_ADDR 0x19
#define OUT_X_L 0x28

static int16_t x, y, z;

static float lastMagnitude = 0;
static float filtered = 0;

static unsigned long lastStepTime = 0;
static uint32_t stepCount = 0;

// Tunable
#define STEP_THRESHOLD  0.15f   // g-force delta threshold //increase if too sensitive & decrease if not sensitive enough
#define STEP_DELAY      350     // minimum ms between steps
#define FILTER_ALPHA    0.85f   // low-pass filter strength

//----------------------------------

void sc_writeRegister(uint8_t reg, uint8_t value) {
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(reg);
    soft_write(value);
    soft_endTransmission();
}

//----------------------------------

void sc_init() {
    // ... WHO_AM_I check unchanged ...

    // CTRL_REG1: 100Hz, normal mode, XYZ enable
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x20);
    soft_write(0x57);
    soft_endTransmission();

    // CTRL_REG2: high-pass filter for data AND interrupt
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x21);
    soft_write(0x09);
    soft_endTransmission();

    // CTRL_REG3: route IA1 (INT1 activity) to INT1 pin
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x22);
    soft_write(0x40);   // ← IA1 on INT1, not DRDY
    soft_endTransmission();

    // CTRL_REG4: ±2g, high resolution
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x23);
    soft_write(0x08);
    soft_endTransmission();

    // CTRL_REG5: latch interrupt
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x24);
    soft_write(0x08);
    soft_endTransmission();

    // INT1_CFG: enable high events on X, Y, Z (OR combination)
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x30);
    soft_write(0x2A);   // XH|YH|ZH
    soft_endTransmission();

    // INT1_THS: threshold = ~100mg
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x32);
    soft_write(0x08);   // 8 × 16mg = 128mg
    soft_endTransmission();

    // INT1_DURATION: minimum duration = 1 sample
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x33);
    soft_write(0x01);
    soft_endTransmission();

    Serial.println("LIS2DH12 initialized");
}

//----------------------------------

void sc_readRaw() {
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(OUT_X_L | 0x80);
    soft_endTransmission(false);

    soft_requestFrom(LIS2DH12_ADDR, 6);

    if (soft_available() == 6) {
        uint8_t xL = soft_read();
        uint8_t xH = soft_read();
        uint8_t yL = soft_read();
        uint8_t yH = soft_read();
        uint8_t zL = soft_read();
        uint8_t zH = soft_read();

        x = (int16_t)(xH << 8 | xL) >> 4;
        y = (int16_t)(yH << 8 | yL) >> 4;
        z = (int16_t)(zH << 8 | zL) >> 4;
    }
}

//----------------------------------



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

    // Clear latched interrupt by reading INT1_SRC
    soft_beginTransmission(LIS2DH12_ADDR);
    soft_write(0x31);
    soft_endTransmission(false);
    soft_requestFrom(LIS2DH12_ADDR, 1);
    soft_read();
}

//----------------------------------

uint32_t sc_getSteps() {
    return stepCount;
}

void sc_resetSteps() {
    stepCount = 0;
}