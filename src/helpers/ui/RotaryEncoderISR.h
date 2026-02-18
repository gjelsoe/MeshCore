// Interrupt-based RotaryEncoder - More reliable than polling
// Use this version for Heltec V3 and ESP32 boards

#pragma once

#include <Arduino.h>

#define ENCODER_EVENT_NONE         0
#define ENCODER_EVENT_CW           1
#define ENCODER_EVENT_CCW          2
#define ENCODER_EVENT_CLICK        3
#define ENCODER_EVENT_LONG_PRESS   4

class RotaryEncoderISR {
  int8_t _pin_clk;
  int8_t _pin_dt;
  int8_t _pin_sw;
  
  // Encoder state
  volatile int _position;
  int _last_position;
  volatile unsigned long _last_interrupt_time;
  
  // Button state
  int8_t _button_prev;
  unsigned long _button_down_at;
  int _long_millis;
  uint8_t _click_count;
  unsigned long _last_click_time;
  int _multi_click_window;
  bool _pending_click;
  int8_t _cancel;
  
  bool isButtonPressed(int level) const;
  
  // Static members for ISR
  static RotaryEncoderISR* _instance;
  static void IRAM_ATTR handleInterrupt();

public:
  RotaryEncoderISR(int8_t pin_clk, int8_t pin_dt, int8_t pin_sw, int long_press_millis = 1200, bool multiclick = false);
  
  void begin();
  int check();
  void cancelClick();
  bool isPressed() const;
  int8_t getClkPin() { return _pin_clk; }
  int8_t getDtPin() { return _pin_dt; }
  int8_t getSwPin() { return _pin_sw; }
};