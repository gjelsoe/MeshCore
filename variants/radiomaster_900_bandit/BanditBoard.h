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

/*
  Pin connections from ESP32-D0WDQ6 to SX1276.
*/
#define P_LORA_DIO_0     22
#define P_LORA_DIO_1     21
#define P_LORA_NSS       4
#define P_LORA_RESET     5
#define P_LORA_SCLK      18
#define P_LORA_MISO      19
#define P_LORA_MOSI      23
#define SX176X_TXEN      33

/*
  I2C SDA and SCL.
*/
#define PIN_BOARD_SDA    14
#define PIN_BOARD_SCL    12

/*
  This unit has a FAN built-in.
  FAN is active at 250mW on its ExpressLRS Firmware.
  Always ON
*/
#define PA_FAN_EN        2 // FAN on GPIO 2

/*
  This module has Skyworks SKY66122 controlled by dacWrite
  power ranging from 100mW to 1000mW.

  Mapping of PA_LEVEL to Power output: GPIO26/dacWrite
  168 -> 100mW  -> 2.11v
  148 -> 250mW  -> 1.87v
  128 -> 500mW  -> 1.63v
  90  -> 1000mW -> 1.16v
*/
#define DAC_PA_PIN       26 // GPIO 26 controls the PA

// Configuration - adjust these for your hardware
#define PA_CONSTANT_GAIN 18 // SKY66122 operates at constant 18dB gain
#define MIN_OUTPUT_DBM   20 // 100mW minimum
#define MAX_OUTPUT_DBM   30 // 1000mW maximum

class BanditBoard : public ESP32Board {
public:
#ifdef RADIOMASTER_900_BANDIT
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
  }
#endif
};
