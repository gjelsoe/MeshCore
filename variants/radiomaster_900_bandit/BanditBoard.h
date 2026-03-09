#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <helpers/ui/AnalogJoystick.h>

#ifdef RADIOMASTER_900_BANDIT
#include <Adafruit_NeoPixel.h>
extern Adafruit_NeoPixel pixels;

// User-definable TX LED color (RGB hex format)
// Examples: 0x00FF00 (green), 0xFF0000 (red), 0x0000FF (blue)
#ifndef TX_LED_COLOR
#define TX_LED_COLOR 0x009600 // Default: Green (0, 150, 0)
#endif

// Extract RGB components from hex color for TX LED
#define TX_LED_RED       ((TX_LED_COLOR >> 16) & 0xFF)
#define TX_LED_GREEN     ((TX_LED_COLOR >> 8) & 0xFF)
#define TX_LED_BLUE      (TX_LED_COLOR & 0xFF)
#endif

/*
  Radiomaster Bandit / Bandit Nano shared hardware:
  - ESP32-D0WDQ6 + SX1276 LoRa
  - Skyworks SKY66122 PA (100mW-1000mW, DAC-controlled)
  - Analog 5-way joystick on GPIO 39

  Bandit only:
  - 6x Neopixels (GRB, GPIO 15)
  - SH1115 OLED display
  - 2x buttons with backlight (GPIO 34/35 - UNUSED)
  - STK8XXX Accelerometer (I2C 0x18, INT GPIO 37)

  Bandit Nano only:
  - SSD1306 OLED display (rotated 180 deg)
  - TX LED on GPIO 15
*/

// All pin definitions are in platformio.ini via -D flags.
// See platformio.ini [radiomaster_900_bandit_base] for LoRa, I2C, joystick pins.

#define MIN_OUTPUT_DBM   20 // 100mW minimum
#define MAX_OUTPUT_DBM   30 // 1000mW maximum

class BanditBoard : public ESP32Board {
public:
#ifdef RADIOMASTER_900_BANDIT
  volatile bool tx_active = false;

  void begin() {
    ESP32Board::begin();
    pixels.begin();
    pixels.clear();
    pixels.show();
  }
#endif

  // Return fake battery status, battery/fixed power is not monitored.
  uint16_t getBattMilliVolts() override { return 5000; }

  const char *getManufacturerName() const override {
#ifdef RADIOMASTER_900_BANDIT_NANO
    return "RadioMaster Bandit Nano";
#else
    return "RadioMaster Bandit";
#endif
  }

  void powerOff() override {
#if defined(PIN_USER_BTN)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, LOW);
#elif defined(PIN_USER_JOYSTICK)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_JOYSTICK, LOW);
#endif
    // Disable PA and fan before deep sleep
    dacWrite(DAC_PA_PIN, 0);
    digitalWrite(PA_FAN_EN, LOW);
#ifdef RADIOMASTER_900_BANDIT
    pixels.clear();
    pixels.show();
#endif
    esp_deep_sleep_start();
  }

#ifdef RADIOMASTER_900_BANDIT
  void onBeforeTransmit() override {
    tx_active = true;
    for (byte i = 2; i < NEOPIXEL_NUM; i++) {
      pixels.setPixelColor(i, pixels.Color(TX_LED_RED, TX_LED_GREEN, TX_LED_BLUE));
    }
    pixels.show();
  }

  void onAfterTransmit() override {
    for (byte i = 2; i < NEOPIXEL_NUM; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 0, 0));
    }
    pixels.show();
    tx_active = false;
  }
#endif
};
