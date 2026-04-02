#include <Wire.h>
#include "stepcounter.h"
#include "ui.h"
#include "clock.h"
#include "i2c_soft.h"
#include "oled_driver.h"

#define SDA_PIN 4
#define SCL_PIN 5
#define INT1_PIN 21
#define BATT_INT_PIN 3

void IRAM_ATTR battery_isr();

volatile uint8_t interruptCount = 0;

void IRAM_ATTR isr() {
  interruptCount++;
}

void setup() {
    Serial.begin(115200);

    // Soft I2C for step counter + battery (pins 4/5)
    soft_i2c_init();

    // Hardware Wire for OLED (pins 8/9), initialized inside ui_init()

    pinMode(INT1_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT1_PIN), isr, RISING);
    pinMode(BATT_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BATT_INT_PIN), battery_isr, FALLING);

    sc_init();
    ui_init();    // calls OLED_Init() → Wire.begin(8,9)
    OLED_BufferClear();
    OLED_Refresh();
    clock_init();
}

void loop() {
  if (interruptCount > 0) {
      interruptCount = 0;
      sc_update();
  }
  ui_update();
}