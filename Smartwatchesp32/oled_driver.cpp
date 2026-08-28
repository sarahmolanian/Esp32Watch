#include "oled_driver.h"
#include <pgmspace.h>
#include "font.h"
#include <Wire.h>

#define OLED_I2C_ADDR 0x3C

uint8_t OLED_GRAM[128][8];
uint8_t OLED_GRAM_PREV[128][8];

// ============================================================
// Stubs — kept so any code that calls them still compiles
// ============================================================
void I2C_Start(void)        {}
void I2C_Stop(void)         {}
void I2C_WaitAck(void)      {}
void Send_Byte(uint8_t dat) {}

// ============================================================
// I2C via hardware peripheral
// ============================================================
void OLED_WR_Byte(uint8_t dat, uint8_t mode) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(mode ? 0x40 : 0x00);
    Wire.write(dat);
    Wire.endTransmission();
}

static void _sendPage(uint8_t page) {
    if (page == 0) {
        OLED_WR_Byte(0x40, OLED_CMD);   // lock display start line
    }
    OLED_WR_Byte(0xB0 + page, OLED_CMD);
    uint8_t col_offset = 0;  // 🔥 try 2 (most common)

    OLED_WR_Byte(0x00 + (col_offset & 0x0F), OLED_CMD);
    OLED_WR_Byte(0x10 + (col_offset >> 4), OLED_CMD);

    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(0x40);
    for (uint8_t col = 0; col < 128; col++) {
        Wire.write(OLED_GRAM[col][page]);
        OLED_GRAM_PREV[col][page] = OLED_GRAM[col][page];
    }
    Wire.endTransmission();
}

// ============================================================
// Flush — only sends pages that have changed
// ============================================================
void OLED_Flush(void) {
    for (uint8_t page = 0; page < 8; page++) {
        bool dirty = false;
        for (uint8_t col = 0; col < 128; col++) {
            if (OLED_GRAM[col][page] != OLED_GRAM_PREV[col][page]) {
                dirty = true;
                break;
            }
        }
        if (dirty) {
            _sendPage(page);
        } else {
            OLED_WR_Byte(0xB0 + page, OLED_CMD);
            OLED_WR_Byte(0x00,        OLED_CMD);
            OLED_WR_Byte(0x10,        OLED_CMD);
        }
    }
}

// ============================================================
// Refresh — sends all 8 pages unconditionally
// ============================================================
void OLED_Refresh(void) {
    for (uint8_t page = 0; page < 8; page++)
        _sendPage(page);
}

// ============================================================
// Buffer helpers
// ============================================================
void OLED_BufferClear(void) {
    memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
}

void OLED_Clear(void) {
    OLED_BufferClear();
    OLED_Flush();
}

// ============================================================
void OLED_ColorTurn(uint8_t i) {
    OLED_WR_Byte(i ? 0xA7 : 0xA6, OLED_CMD);
}

void OLED_DisplayTurn(uint8_t i) {
    if (i == 0) {
        OLED_WR_Byte(0xC8, OLED_CMD);
        OLED_WR_Byte(0xA1, OLED_CMD);
    } else {
        OLED_WR_Byte(0xC0, OLED_CMD);
        OLED_WR_Byte(0xA0, OLED_CMD);
    }
}

// ============================================================
void OLED_DrawPoint(uint8_t x, uint8_t y) {
    if (x >= 128 || y >= 64) return;
    OLED_GRAM[x][y / 8] |= (1 << (y % 8));
}

void OLED_ClearPoint(uint8_t x, uint8_t y) {
    if (x >= 128 || y >= 64) return;
    OLED_GRAM[x][y / 8] &= ~(1 << (y % 8));
}

void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    for (uint8_t yy = y; yy < y + h; yy++) {
        for (uint8_t xx = x; xx < x + w; xx++) {
            OLED_ClearPoint(xx, yy);
        }
    }
}

// ============================================================
void OLED_ShowChar(uint8_t x, uint8_t y, const char chr, uint8_t size1) {
    uint8_t i, m, temp, size2, chr1;
    uint8_t y0 = y;
    size2 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * (size1 / 2);
    chr1 = chr - ' ';
    for (i = 0; i < size2; i++) {
        if      (size1 == 12) temp = pgm_read_byte(&asc2_1206[chr1][i]);
        else if (size1 == 16) temp = pgm_read_byte(&asc2_1608[chr1][i]);
        else if (size1 == 24) temp = pgm_read_byte(&asc2_2412[chr1][i]);
        else return;
        for (m = 0; m < 8; m++) {
            if (temp & 0x80) OLED_DrawPoint(x, y);
            else             OLED_ClearPoint(x, y);
            temp <<= 1;
            y++;
            if ((y - y0) == size1) { y = y0; x++; break; }
        }
    }
}

void OLED_ShowString(uint8_t x, uint8_t y, const char* chr, uint8_t size1) {
    while ((*chr >= ' ') && (*chr <= '~')) {
        OLED_ShowChar(x, y, *chr, size1);
        x += size1 / 2;
        if (x > 128 - size1 / 2) { x = 0; y += size1; }
        chr++;
    }
}

void OLED_ShowChar8(uint8_t x, uint8_t y, char chr) {
    if (chr < 32 || chr > 127) return;
    uint8_t index = chr - 32;
    for (int i = 0; i < 5; i++) {
        uint8_t line = pgm_read_byte(&font5x8[index][i]);
        for (int j = 0; j < 8; j++) {
            if (line & (1 << j))
                OLED_DrawPoint(x + i, y + j);
            else
                OLED_ClearPoint(x + i, y + j);
        }
    }
}

void OLED_ShowString8(uint8_t x, uint8_t y, const char* str) {
    while (*str) {
        OLED_ShowChar8(x, y, *str);
        x += 6;
        str++;
    }
}


void OLED_HardClear() {
    uint8_t col_offset = 0;  // 🔥 MUST match _sendPage()

    for (uint8_t page = 0; page < 8; page++) {
        OLED_WR_Byte(0xB0 + page, OLED_CMD);

        OLED_WR_Byte(0x00 + (col_offset & 0x0F), OLED_CMD);
        OLED_WR_Byte(0x10 + (col_offset >> 4), OLED_CMD);

        Wire.beginTransmission(OLED_I2C_ADDR);
        Wire.write(0x40);

        for (uint8_t col = 0; col < 128; col++) {
            Wire.write(0x00);
        }

        Wire.endTransmission();
    }
}


// ============================================================
void OLED_Init(void) {
    Wire.begin(8, 9);       // SDA=pin8, SCL=pin9
    Wire.setClock(400000);  // 400kHz fast mode

    // Hardware reset using res pin
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH); delay(100);
    digitalWrite(2, LOW);  delay(50);
    digitalWrite(2, HIGH); delay(100);

    OLED_WR_Byte(0xAE, OLED_CMD);
    delay(50);                  
    OLED_HardClear();
    OLED_WR_Byte(0x00, OLED_CMD);
    OLED_WR_Byte(0x10, OLED_CMD);
    OLED_WR_Byte(0x40, OLED_CMD);
    OLED_WR_Byte(0x81, OLED_CMD);
    OLED_WR_Byte(0xCF, OLED_CMD);
    OLED_WR_Byte(0xA1, OLED_CMD);
    OLED_WR_Byte(0xC8, OLED_CMD);
    OLED_WR_Byte(0xA6, OLED_CMD);
    OLED_WR_Byte(0xA8, OLED_CMD);
    OLED_WR_Byte(0x3F, OLED_CMD);
    OLED_WR_Byte(0xD3, OLED_CMD);
    OLED_WR_Byte(0x00, OLED_CMD);
    OLED_WR_Byte(0xD5, OLED_CMD);
    OLED_WR_Byte(0x80, OLED_CMD);
    OLED_WR_Byte(0xD9, OLED_CMD);
    OLED_WR_Byte(0xF1, OLED_CMD);
    OLED_WR_Byte(0xDA, OLED_CMD);
    OLED_WR_Byte(0x12, OLED_CMD);
    OLED_WR_Byte(0xDB, OLED_CMD);
    OLED_WR_Byte(0x40, OLED_CMD);
    OLED_WR_Byte(0x20, OLED_CMD);
    OLED_WR_Byte(0x02, OLED_CMD);
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x14, OLED_CMD);
    OLED_WR_Byte(0xA4, OLED_CMD);
    OLED_WR_Byte(0xA6, OLED_CMD);
    OLED_WR_Byte(0xAF, OLED_CMD);
    
    
    memset(OLED_GRAM,      0, sizeof(OLED_GRAM));
    //memset(OLED_GRAM_PREV, 0, sizeof(OLED_GRAM_PREV));
    memset(OLED_GRAM_PREV, 0xFF, sizeof(OLED_GRAM_PREV)); // 🔥 force mismatch
    OLED_Refresh();
}