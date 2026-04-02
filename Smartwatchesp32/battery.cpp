#include "battery.h"
#include "i2c_soft.h"
#define MAX17043_ADDR 0x36

static float batteryPercent = 0;

// Optional interrupt flag
volatile bool batteryAlert = false;

void IRAM_ATTR battery_isr() {
    batteryAlert = true;
}

// ----------------------------

uint16_t readRegister16(uint8_t reg) {
    soft_beginTransmission(MAX17043_ADDR);
    soft_write(reg);
    soft_endTransmission(false);

    soft_requestFrom(MAX17043_ADDR, 2);

    uint16_t val = 0;
    if (soft_available() == 2) {
        val = (soft_read() << 8) | soft_read();
    }
    return val;
}

// ----------------------------

void battery_init() {
    // nothing special required

    // Optional: reset chip (good practice)
    soft_beginTransmission(MAX17043_ADDR);
    soft_write(0xFE);
    soft_write(0x54);
    soft_write(0x00);
    soft_endTransmission();

    delay(50);
}

// ----------------------------

void battery_update() {

    // Only update periodically OR on interrupt
    static unsigned long lastRead = 0;

    if (millis() - lastRead < 1000 && !batteryAlert) return;
    lastRead = millis();
    batteryAlert = false;

    uint16_t soc = readRegister16(0x04);

    // Convert to percentage
    batteryPercent = soc >> 8; // high byte = %

    // clamp just in case
    if (batteryPercent > 100) batteryPercent = 100;
    if (batteryPercent < 0) batteryPercent = 0;
}

// ----------------------------

float battery_getPercentage() {
    return batteryPercent;
}