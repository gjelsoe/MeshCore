/**
 * FanSpeedScreen.h
 *
 * UI Screen for fan speed control in MeshCore
 *
 * Navigation:
 * - KEY_ENTER: Open/close menu
 * - KEY_UP: Increase speed by 10%
 * - KEY_DOWN: Decrease speed by 10%
 * - KEY_ENTER (in menu): Save and close
 * - KEY_PREV: Go back (view mode) or cancel (edit mode)
 */

#pragma once

#include "FanController.h"

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>

// Forward declaration
class UITask;

class FanSpeedScreen : public UIScreen {
private:
  FanController *_fan;
  UIScreen *_return_screen;
  UITask *_task;       // Pointer to UITask for navigation
  uint8_t _temp_speed; // Temporary speed during editing
  bool _is_editing;    // Whether we are in edit mode
  unsigned long _blink_timer;
  bool _show_cursor;

public:
  /**
   * Constructor
   * @param fan Pointer to FanController instance
   * @param return_screen Screen to return to after save
   * @param task Pointer to UITask for navigation
   */
  FanSpeedScreen(FanController *fan, UIScreen *return_screen, UITask *task)
      : _fan(fan), _return_screen(return_screen), _task(task), _temp_speed(0), _is_editing(false),
        _blink_timer(0), _show_cursor(true) {}

  /**
   * Render fan speed screen
   */
  int render(DisplayDriver &display) override {
    char tmp[40];

    // Title
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(display.width() / 2, 0, "Fan Speed Control");

    // Divider line (using drawRect with height=1)
    display.drawRect(0, 12, display.width(), 1);

    if (_is_editing) {
      // Edit mode - show current value with blinking cursor
      display.setTextSize(2);
      display.setColor(DisplayDriver::YELLOW);

      sprintf(tmp, "%d%%", _temp_speed);
      int text_width = strlen(tmp) * 12; // Approx width for size 2
      int x = (display.width() - text_width) / 2;
      display.setCursor(x, 22);
      display.print(tmp);

      // Blinking cursor (using drawRect with height=1)
      if (_show_cursor) {
        display.drawRect(x, 40, text_width, 1);
      }

      // Instructions
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, 46, "\x18/\x19 Adjust");        // ↑/↓ Adjust
      display.drawTextCentered(display.width() / 2, 56, "ENTER: Save | \x1B Back"); // ← Cancel

      // Blink cursor
      if (millis() - _blink_timer > 500) {
        _show_cursor = !_show_cursor;
        _blink_timer = millis();
      }

    } else {
      // View mode - show current speed
      uint8_t current = _fan->getSpeed();
      uint16_t rpm = _fan->getRPM();

      display.setTextSize(3);
      display.setColor(DisplayDriver::GREEN);
      sprintf(tmp, "%d%%", current);
      display.drawTextCentered(display.width() / 2, 20, tmp);

      // RPM display
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      sprintf(tmp, "RPM: %d", rpm);
      display.drawTextCentered(display.width() / 2, 46, tmp);

      // Instruction
      display.drawTextCentered(display.width() / 2, 56, "ENTER: Edit | \x1B Back"); // ← Back
    }

    return 200; // Refresh every 200ms
  }

  /**
   * Handle user input
   */
  bool handleInput(char c) override {
    if (!_is_editing) {
      // Not editing - ENTER starts editing, PREV goes back
      if (c == KEY_ENTER) {
        _is_editing = true;
        _temp_speed = _fan->getSpeed(); // Load current value
        _blink_timer = millis();
        _show_cursor = true;
        return true;
      } else if (c == KEY_PREV) {
        // Go back to previous screen
        if (_task && _return_screen) {
          _task->gotoHomeScreen();
        }
        return true;
      }
    } else {
      // Editing mode
      if (c == KEY_UP) {
        // Increase by 10%, max 100
        if (_temp_speed <= 90) {
          _temp_speed += 10;
        } else {
          _temp_speed = 100;
        }
        return true;
      } else if (c == KEY_DOWN) {
        // Decrease by 10%, min 0
        if (_temp_speed >= 10) {
          _temp_speed -= 10;
        } else {
          _temp_speed = 0;
        }
        return true;
      } else if (c == KEY_ENTER) {
        // Save and exit
        _fan->setSpeed(_temp_speed); // This saves to NVS
        _is_editing = false;
        return true;
      } else if (c == KEY_PREV) {
        // Cancel editing - revert to original value
        _is_editing = false;
        _temp_speed = _fan->getSpeed(); // Restore original
        return true;
      }
    }
    return false;
  }

  /**
   * Called when screen is activated
   */
  void onActivate() {
    _is_editing = false;
    _temp_speed = _fan->getSpeed();
  }

  /**
   * Called when screen is deactivated
   */
  void onDeactivate() { _is_editing = false; }
};