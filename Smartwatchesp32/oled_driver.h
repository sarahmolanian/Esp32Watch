#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include <Arduino.h>
#include <pgmspace.h>

extern uint8_t OLED_GRAM[128][8];
extern uint8_t OLED_GRAM_PREV[128][8];
#define OLED_CMD  0

void I2C_Start(void);
void I2C_Stop(void);
void I2C_WaitAck(void);
void Send_Byte(uint8_t dat);
void OLED_WR_Byte(uint8_t dat, uint8_t mode);
void OLED_ColorTurn(uint8_t i);
void OLED_DisplayTurn(uint8_t i);
void OLED_Flush(void);
void OLED_BufferClear(void);
void OLED_Refresh(void);
void OLED_Clear(void);
void OLED_DrawPoint(uint8_t x, uint8_t y);
void OLED_ClearPoint(uint8_t x, uint8_t y);
void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void OLED_ShowChar(uint8_t x, uint8_t y, const char chr, uint8_t size1);
void OLED_ShowString(uint8_t x, uint8_t y, const char* chr, uint8_t size1);
void OLED_ShowChar8(uint8_t x, uint8_t y, char chr);
void OLED_ShowString8(uint8_t x, uint8_t y, const char* str);
void OLED_Init(void);
void OLED_Reinit(void);

#endif