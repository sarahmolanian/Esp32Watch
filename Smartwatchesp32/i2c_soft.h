#ifndef I2C_SOFT_H
#define I2C_SOFT_H

#include <Arduino.h>

#define SOFT_SDA 4
#define SOFT_SCL 5

void soft_i2c_init();
void soft_i2c_start();
void soft_i2c_stop();
bool soft_i2c_write(uint8_t dat);
uint8_t soft_i2c_read(bool ack);

// Drop-in Wire-style helpers
void soft_beginTransmission(uint8_t addr);
bool soft_endTransmission(bool stop = true);
void soft_write(uint8_t dat);
uint8_t soft_read();
bool soft_requestFrom(uint8_t addr, uint8_t len);
uint8_t soft_available();

#endif