#include "i2c_soft.h"

#define SDA_HIGH() pinMode(SOFT_SDA, INPUT)
#define SDA_LOW()  do { pinMode(SOFT_SDA, OUTPUT); digitalWrite(SOFT_SDA, LOW); } while(0)
#define SCL_HIGH() digitalWrite(SOFT_SCL, HIGH)
#define SCL_LOW()  digitalWrite(SOFT_SCL, LOW)

static uint8_t _txBuf[32];
static uint8_t _txLen = 0;
static uint8_t _txAddr = 0;
static uint8_t _rxBuf[32];
static uint8_t _rxLen = 0;
static uint8_t _rxIdx = 0;

void soft_i2c_init() {
    pinMode(SOFT_SCL, OUTPUT);
    digitalWrite(SOFT_SCL, HIGH);
    SDA_HIGH();
    delayMicroseconds(5);
}

void soft_i2c_start() {
    SDA_HIGH(); delayMicroseconds(2);
    SCL_HIGH(); delayMicroseconds(2);
    SDA_LOW();  delayMicroseconds(2);
    SCL_LOW();  delayMicroseconds(2);
}

void soft_i2c_stop() {
    SCL_LOW();  delayMicroseconds(2);
    SDA_LOW();  delayMicroseconds(2);
    SCL_HIGH(); delayMicroseconds(2);
    SDA_HIGH(); delayMicroseconds(2);
}

bool soft_i2c_write(uint8_t dat) {
    for (uint8_t i = 0; i < 8; i++) {
        SCL_LOW(); delayMicroseconds(1);
        if (dat & 0x80) SDA_HIGH(); else SDA_LOW();
        delayMicroseconds(1);
        SCL_HIGH(); delayMicroseconds(2);
        dat <<= 1;
    }
    // read ACK
    SCL_LOW(); delayMicroseconds(1);
    SDA_HIGH(); delayMicroseconds(1);
    SCL_HIGH(); delayMicroseconds(1);
    bool ack = (digitalRead(SOFT_SDA) == LOW);
    SCL_LOW(); delayMicroseconds(1);
    return ack;
}

uint8_t soft_i2c_read(bool ack) {
    uint8_t dat = 0;
    SDA_HIGH();
    for (uint8_t i = 0; i < 8; i++) {
        SCL_LOW();  delayMicroseconds(2);
        SCL_HIGH(); delayMicroseconds(1);
        dat <<= 1;
        if (digitalRead(SOFT_SDA)) dat |= 1;
        delayMicroseconds(1);
    }
    SCL_LOW(); delayMicroseconds(1);
    if (ack) SDA_LOW(); else SDA_HIGH();
    delayMicroseconds(1);
    SCL_HIGH(); delayMicroseconds(2);
    SCL_LOW();  delayMicroseconds(1);
    SDA_HIGH();
    return dat;
}

void soft_beginTransmission(uint8_t addr) {
    _txAddr = addr;
    _txLen  = 0;
}

void soft_write(uint8_t dat) {
    if (_txLen < 32) _txBuf[_txLen++] = dat;
}

bool soft_endTransmission(bool stop) {
    soft_i2c_start();
    bool ok = soft_i2c_write(_txAddr << 1);
    for (uint8_t i = 0; i < _txLen; i++)
        ok &= soft_i2c_write(_txBuf[i]);
    if (stop) soft_i2c_stop();
    return ok;
}

bool soft_requestFrom(uint8_t addr, uint8_t len) {
    _rxLen = 0; _rxIdx = 0;
    soft_i2c_start();
    bool ok = soft_i2c_write((addr << 1) | 1);
    if (!ok) { soft_i2c_stop(); return false; }
    for (uint8_t i = 0; i < len; i++) {
        bool ack = (i < len - 1);
        _rxBuf[_rxLen++] = soft_i2c_read(ack);
    }
    soft_i2c_stop();
    return true;
}

uint8_t soft_read() {
    if (_rxIdx < _rxLen) return _rxBuf[_rxIdx++];
    return 0;
}

uint8_t soft_available() {
    return _rxLen - _rxIdx;
}