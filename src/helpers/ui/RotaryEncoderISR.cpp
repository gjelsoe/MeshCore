#include "RotaryEncoderISR.h"

#define MULTI_CLICK_WINDOW_MS 280

// Static member initialization
RotaryEncoderISR *RotaryEncoderISR::_instance = nullptr;

RotaryEncoderISR::RotaryEncoderISR(int8_t pin_clk, int8_t pin_dt, int8_t pin_sw, int long_press_millis,
                                   bool multiclick) {
  _pin_clk = pin_clk;
  _pin_dt = pin_dt;
  _pin_sw = pin_sw;

  _position = 0;
  _last_position = 0;
  _last_interrupt_time = 0;

  _button_prev = HIGH;
  _button_down_at = 0;
  _long_millis = long_press_millis;
  _click_count = 0;
  _last_click_time = 0;
  _multi_click_window = multiclick ? MULTI_CLICK_WINDOW_MS : 0;
  _pending_click = false;
  _cancel = 0;

  _instance = this;
}

void IRAM_ATTR RotaryEncoderISR::handleInterrupt() {
  if (_instance == nullptr) return;

  // Debounce in ISR - ignore rapid pulses
  unsigned long now = millis();
  if (now - _instance->_last_interrupt_time < 5) {
    return;
  }
  _instance->_last_interrupt_time = now;

  // Read both pins
  int clk_state = digitalRead(_instance->_pin_clk);
  int dt_state = digitalRead(_instance->_pin_dt);

  // Determine direction based on pin states when CLK goes LOW
  if (clk_state == LOW) {
    if (dt_state == HIGH) {
      _instance->_position--; // Counter-clockwise
    } else {
      _instance->_position++; // Clockwise
    }
  }
}

void RotaryEncoderISR::begin() {
  if (_pin_clk >= 0) {
    pinMode(_pin_clk, INPUT_PULLUP);
    pinMode(_pin_dt, INPUT_PULLUP);

    // Attach interrupt to CLK pin - trigger on any change
    attachInterrupt(digitalPinToInterrupt(_pin_clk), handleInterrupt, CHANGE);
  }

  if (_pin_sw >= 0) {
    pinMode(_pin_sw, INPUT_PULLUP);
  }
}

bool RotaryEncoderISR::isButtonPressed(int level) const {
  return level == LOW;
}

bool RotaryEncoderISR::isPressed() const {
  if (_pin_sw < 0) return false;
  return isButtonPressed(digitalRead(_pin_sw));
}

void RotaryEncoderISR::cancelClick() {
  _cancel = 1;
  _button_down_at = 0;
  _click_count = 0;
  _last_click_time = 0;
  _pending_click = false;
}

int RotaryEncoderISR::check() {
  int event = ENCODER_EVENT_NONE;

  // Check for rotation
  if (_position != _last_position) {
    if (_position > _last_position) {
      event = ENCODER_EVENT_CW;
#ifdef ENCODER_DEBUG
      Serial.println("ENCODER ISR: CW");
#endif
    } else {
      event = ENCODER_EVENT_CCW;
#ifdef ENCODER_DEBUG
      Serial.println("ENCODER ISR: CCW");
#endif
    }
    _last_position = _position;
    return event; // Return immediately to avoid missing rapid rotations
  }

  // Check button (same logic as before)
  if (_pin_sw >= 0) {
    int btn = digitalRead(_pin_sw);

    if (btn != _button_prev) {
      if (isButtonPressed(btn)) {
        _button_down_at = millis();
      } else {
        if (_long_millis > 0) {
          if (_button_down_at > 0 && (unsigned long)(millis() - _button_down_at) < _long_millis) {
            _click_count++;
            _last_click_time = millis();
            _pending_click = true;
          }
        } else {
          _click_count++;
          _last_click_time = millis();
          _pending_click = true;
        }

        if (_cancel) {
          _click_count = 0;
          _last_click_time = 0;
          _pending_click = false;
        }

        _button_down_at = 0;
      }
      _button_prev = btn;
    }

    if (!isButtonPressed(btn) && _cancel) {
      _cancel = 0;
    }

    if (_long_millis > 0 && _button_down_at > 0 &&
        (unsigned long)(millis() - _button_down_at) >= _long_millis) {
      if (_pending_click) {
        cancelClick();
      } else {
        event = ENCODER_EVENT_LONG_PRESS;
        _button_down_at = 0;
        _click_count = 0;
        _last_click_time = 0;
        _pending_click = false;
#ifdef ENCODER_DEBUG
        Serial.println("ENCODER ISR: LONG_PRESS");
#endif
      }
    }

    if (_pending_click && (millis() - _last_click_time) >= _multi_click_window) {
      if (_button_down_at > 0) {
        return event;
      }

      if (_click_count == 1) {
        event = ENCODER_EVENT_CLICK;
#ifdef ENCODER_DEBUG
        Serial.println("ENCODER ISR: CLICK");
#endif
      }

      _click_count = 0;
      _last_click_time = 0;
      _pending_click = false;
    }
  }

  return event;
}