#include <Wire.h>
#include "stepcounter.h"
#include "ui.h"
#include "clock.h"
#include "i2c_soft.h"
#include "oled_driver.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "bluetooth_module.h"
#include "wifi_module.h"
#include <WiFi.h>
#include "esp_bt.h"
#include <NimBLEDevice.h>
#include "esp_wifi.h"
#include "soc/rtc.h"
#include "battery.h"

#define INT1_PIN            21
#define BATT_INT_PIN        20

void IRAM_ATTR battery_isr();
volatile uint8_t interruptCount = 0;
void IRAM_ATTR isr() { interruptCount++; }
volatile bool shutdownRequested = false;

void setup() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    rtc_inShutdown = false;
    Serial.begin(115200);
    Serial.print("Boot cause: ");
    Serial.println(cause);
    soft_i2c_init();
    pinMode(INT1_PIN,     INPUT_PULLUP);
    pinMode(BATT_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT1_PIN),     isr,         RISING);
    attachInterrupt(digitalPinToInterrupt(BATT_INT_PIN), battery_isr, FALLING);
    sc_init();
    sc_restoreStepsFromRTC();
    ui_init();
    OLED_BufferClear();
    OLED_Refresh();
    clock_init();

    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        unsigned long t = millis();
        while (digitalRead(3) == LOW && millis() - t < 5000) {
            delay(10);
        }
        delay(300);
        OLED_BufferClear();
        OLED_ShowString(20, 20, "Waking up...", 12);
        OLED_Flush();
        delay(1000);
        OLED_BufferClear();
        OLED_Flush();
    }
}

void loop() {
    if (interruptCount > 0) {
        interruptCount = 0;
        sc_update();
    }

    ui_update();

    if (shutdownRequested) {
        shutdownRequested = false;

        // Save state
        sc_saveStepsToRTC();
        clock_saveTimeToRTC();  // ← save time before sleep

        rtc_inShutdown = true;
        OLED_WR_Byte(0xAE, OLED_CMD);
        delay(50);

        detachInterrupt(digitalPinToInterrupt(INT1_PIN));
        detachInterrupt(digitalPinToInterrupt(BATT_INT_PIN));

        // Switch RTC to internal clock so GPIO wakeup works
        rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_RC_SLOW);
        delay(100);

        // Wait for GPIO3 to be released
        unsigned long t = millis();
        while (digitalRead(3) == LOW && millis() - t < 5000) {
            delay(10);
        }
        delay(300);

        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(200);

        if (bt_isEnabled()) {
            bt_prepareForSleep();
            delay(300);
        }
        esp_bt_controller_disable();
        delay(200);
        esp_bt_controller_deinit();
        delay(200);

        gpio_reset_pin(GPIO_NUM_3);
        gpio_set_direction(GPIO_NUM_3, GPIO_MODE_INPUT);
        gpio_pullup_en(GPIO_NUM_3);
        gpio_pulldown_dis(GPIO_NUM_3);
        delay(100);

        if (digitalRead(3) != HIGH) {
            Serial.println("GPIO3 stuck LOW, aborting sleep");
            Serial.flush();
            rtc_inShutdown = false;
            rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_XTAL32K);
            attachInterrupt(digitalPinToInterrupt(INT1_PIN),
                            isr, RISING);
            attachInterrupt(digitalPinToInterrupt(BATT_INT_PIN),
                            battery_isr, FALLING);
            return;
        }

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_deep_sleep_enable_gpio_wakeup(
            (1ULL << 3),
            ESP_GPIO_WAKEUP_GPIO_LOW
        );

        Serial.println("Entering deep sleep. Press GPIO3 to wake.");
        Serial.flush();
        delay(100);
        esp_deep_sleep_start();
    }
}   