#include "CannedMessagesScreen.h"

#include "../MyMesh.h"
#include "UITask.h"

#include <Arduino.h>

// Canned messages - can be customized in Platformio.ini for your device.
// ------------------------------------------------------------
#ifndef CANNED_MESSAGES
#define CANNED_MESSAGES                                                                               \
  "Help", "On my way", "Yes", "No", "Roger that", "10-4", "Location?", "Call me", "Busy", "Available"
#endif

static constexpr const char *DEFAULT_MESSAGES[] = { CANNED_MESSAGES, nullptr };

// ------------------------------------------------------------
// Constructor
// ------------------------------------------------------------
CannedMessagesScreen::CannedMessagesScreen(UITask *task)
    : UIScreen(), _task(task), message_count(0), selected_channel(0), selected_message(0), scroll_offset(0),
      in_channel_selection(true), confirm_send(false)
#ifdef PIN_USER_BTN
      ,
      confirm_option(0)
#endif
{
  // Initialize array
  for (int i = 0; i < MAX_MSG_COUNT; i++)
    messages[i] = nullptr;

  // Copy messages, skipping ones that are too long
  int srcIndex = 0;
  int destIndex = 0;

  while (srcIndex < MAX_MSG_COUNT && DEFAULT_MESSAGES[srcIndex] != nullptr) {
    const char *msg = DEFAULT_MESSAGES[srcIndex];
    int len = strlen(msg);

    if (len <= MAX_MSG_LENGTH) {
      messages[destIndex] = msg;
      destIndex++;
    } else {
      Serial.printf("WARNING: Skipping message %d (too long: %d chars): \"%s\"\n", srcIndex + 1, len, msg);
    }

    srcIndex++;
  }

  reset();
  countMessages();

#ifdef MAX_GROUP_CHANNELS
  selected_channel = 0;
  if (!isValidChannel(selected_channel)) nextChannel(true);
#endif

  Serial.printf("CannedMessages: Loaded %d valid messages\n", message_count);
}

// ------------------------------------------------------------
// Reset state
// ------------------------------------------------------------
void CannedMessagesScreen::reset() {
  in_channel_selection = true;
  selected_message = 0;
  scroll_offset = 0;
  confirm_send = false;
#ifdef PIN_USER_BTN
  confirm_option = 0;
  // Reset channel to first valid one
  selected_channel = 0;
#ifdef MAX_GROUP_CHANNELS
  if (!isValidChannel(selected_channel)) nextChannel(true);
#endif
#endif
}

// ------------------------------------------------------------
// Message helpers
// ------------------------------------------------------------
void CannedMessagesScreen::countMessages() {
  message_count = 0;
  while (message_count < MAX_MSG_COUNT && messages[message_count])
    message_count++;
}

// ------------------------------------------------------------
// Channel helpers
// ------------------------------------------------------------
bool CannedMessagesScreen::isValidChannel(int idx) {
#ifndef MAX_GROUP_CHANNELS
  return idx == 0;
#else
  if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return false;

  ChannelDetails d;
  if (!the_mesh.getChannel(idx, d)) return false;

  return d.name[0] != 0;
#endif
}

void CannedMessagesScreen::nextChannel(bool forward) {
#ifndef MAX_GROUP_CHANNELS
  selected_channel = 0;
#else
  ChannelDetails d;
  int start = selected_channel;

  do {
    selected_channel += forward ? 1 : -1;

    if (selected_channel >= MAX_GROUP_CHANNELS)
      selected_channel = 0;
    else if (selected_channel < 0)
      selected_channel = MAX_GROUP_CHANNELS - 1;

    the_mesh.getChannel(selected_channel, d);
    if (d.name[0] != 0) return;

  } while (selected_channel != start);

  selected_channel = 0;
#endif
}

const char *CannedMessagesScreen::getChannelName(int idx) {
  static char buf[32];
  ChannelDetails d;

  if (the_mesh.getChannel(idx, d) && d.name[0]) {
    strncpy(buf, d.name, sizeof(buf));
    buf[sizeof(buf) - 1] = 0;
    return buf;
  }
  return "Unknown";
}

#ifdef PIN_USER_BTN
// ------------------------------------------------------------
// Single button navigation helpers
// ------------------------------------------------------------
int CannedMessagesScreen::getValidChannelCount() {
#ifndef MAX_GROUP_CHANNELS
  return 1;
#else
  int count = 0;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (isValidChannel(i)) count++;
  }
  return count;
#endif
}

int CannedMessagesScreen::getTotalItems() {
  if (confirm_send) {
    return 3; // Send, Cancel, Back
  } else if (in_channel_selection) {
    return getValidChannelCount() + 1; // channels + Back
  } else {
    return message_count + 1; // messages + Back
  }
}

bool CannedMessagesScreen::isOnBackOption() {
  if (confirm_send) {
    return confirm_option == 2;
  } else if (in_channel_selection) {
    return selected_channel == -1;
  } else {
    return selected_message >= message_count;
  }
}
#endif

// ------------------------------------------------------------
// Rendering
// ------------------------------------------------------------
int CannedMessagesScreen::render(DisplayDriver &display) {
  if (confirm_send)
    renderConfirmSend(display);
  else if (in_channel_selection)
    renderChannelSelection(display);
  else
    renderMessageSelection(display);

  return 1000;
}

void CannedMessagesScreen::renderChannelSelection(DisplayDriver &display) {
  display.setColor(DisplayDriver::GREEN);
  display.setTextSize(1);
  display.drawTextCentered(display.width() / 2, 0, "Select Channel");

  int y = 12;
  int shown = 0;

#ifdef PIN_USER_JOYSTICK
  int idx = selected_channel;

  while (shown < 4 && idx < MAX_GROUP_CHANNELS) {
    if (isValidChannel(idx)) {
      display.setColor(idx == selected_channel ? DisplayDriver::YELLOW : DisplayDriver::GREEN);
      if (idx == selected_channel) display.fillRect(0, y - 1, 3, 9);

      char buf[32];
      snprintf(buf, sizeof(buf), "%d: %s", idx, getChannelName(idx));
      display.drawTextLeftAlign(5, y, buf);
      y += 11;
      shown++;
    }
    idx++;
  }

  display.setColor(DisplayDriver::BLUE);
  display.drawTextCentered(display.width() / 2, 54, "\x1B=Exit \x18/\x19 ENTER=OK");

#elif defined(PIN_USER_BTN)
  int idx = 0;

  while (shown < 4 && idx < MAX_GROUP_CHANNELS) {
    if (isValidChannel(idx)) {
      display.setColor(idx == selected_channel ? DisplayDriver::YELLOW : DisplayDriver::GREEN);
      if (idx == selected_channel) display.fillRect(0, y - 1, 3, 9);

      char buf[32];
      snprintf(buf, sizeof(buf), "%d: %s", idx, getChannelName(idx));
      display.drawTextLeftAlign(5, y, buf);
      y += 11;
      shown++;
    }
    idx++;
  }

  // Always show back option as last item
  display.setColor(selected_channel == -1 ? DisplayDriver::YELLOW : DisplayDriver::GREEN);
  if (selected_channel == -1) display.fillRect(0, y - 1, 3, 9);
  display.drawTextLeftAlign(5, y, "\x1B Back to Home");

  display.setColor(DisplayDriver::BLUE);
  display.drawTextCentered(display.width() / 2, 54, "Click=Next Long=OK");
#endif
}

void CannedMessagesScreen::renderMessageSelection(DisplayDriver &display) {
  display.setColor(DisplayDriver::GREEN);
  display.setTextSize(1);

  char header[40];
  snprintf(header, sizeof(header), "Ch %d: %s", selected_channel, getChannelName(selected_channel));
  display.drawTextCentered(display.width() / 2, 0, header);

  int y = 12;

#ifdef PIN_USER_JOYSTICK
  int end = min(message_count, scroll_offset + MESSAGES_PER_PAGE);

  for (int i = scroll_offset; i < end; i++) {
    display.setColor(i == selected_message ? DisplayDriver::YELLOW : DisplayDriver::GREEN);
    if (i == selected_message) display.fillRect(0, y - 1, 3, 9);

    char buf[50];
    snprintf(buf, sizeof(buf), "%d: %s", i + 1, messages[i]);
    display.drawTextEllipsized(5, y, display.width() - 6, buf);
    y += 11;
  }

  display.setColor(DisplayDriver::BLUE);
  display.drawTextCentered(display.width() / 2, 54, "\x1B=Back \x18/\x19 ENTER=OK");

#elif defined(PIN_USER_BTN)
  // Back option is a virtual item at index message_count
  // Treat it as part of the list so it scrolls into view naturally
  int total_items = message_count + 1;
  int end = min(total_items, scroll_offset + MESSAGES_PER_PAGE);

  for (int i = scroll_offset; i < end; i++) {
    if (i < message_count) {
      // Regular message item
      display.setColor(i == selected_message ? DisplayDriver::YELLOW : DisplayDriver::GREEN);
      if (i == selected_message) display.fillRect(0, y - 1, 3, 9);

      char buf[50];
      snprintf(buf, sizeof(buf), "%d: %s", i + 1, messages[i]);
      display.drawTextEllipsized(5, y, display.width() - 6, buf);
    } else {
      // Back option - scrolls into view like any other item
      display.setColor(selected_message >= message_count ? DisplayDriver::YELLOW : DisplayDriver::GREEN);
      if (selected_message >= message_count) display.fillRect(0, y - 1, 3, 9);
      display.drawTextLeftAlign(5, y, "\x1B Back to Channels");
    }
    y += 11;
  }

  display.setColor(DisplayDriver::BLUE);
  display.drawTextCentered(display.width() / 2, 54, "Click=Next Long=OK");
#endif
}

void CannedMessagesScreen::renderConfirmSend(DisplayDriver &display) {
  display.setColor(DisplayDriver::YELLOW);
  display.drawTextCentered(display.width() / 2, 10, "Send this message?");

  char msgbuf[50];
  snprintf(msgbuf, sizeof(msgbuf), "\"%s\"", messages[selected_message]);
  display.setColor(DisplayDriver::GREEN);
  display.drawTextCentered(display.width() / 2, 22, msgbuf);

  char chbuf[40];
  snprintf(chbuf, sizeof(chbuf), "Ch %d: %s", selected_channel, getChannelName(selected_channel));
  display.drawTextCentered(display.width() / 2, 34, chbuf);

#ifdef PIN_USER_JOYSTICK
  // Original joystick confirm screen - untouched
  display.setColor(DisplayDriver::BLUE);
  display.drawTextCentered(display.width() / 2, 54, "\x1B=Back \x18=Yes \x19=No");

#elif defined(PIN_USER_BTN)
  // Simple two-action confirm - no cycling needed
  display.setColor(DisplayDriver::BLUE);
  display.drawTextCentered(display.width() / 2, 54, "Click=Back Long=Send");
#endif
}

// ------------------------------------------------------------
// Input handling
// ------------------------------------------------------------
bool CannedMessagesScreen::handleInput(char c) {

#ifdef PIN_USER_JOYSTICK
  // ============================================================
  // JOYSTICK NAVIGATION - Original code, untouched
  // ============================================================

  if (c == KEY_PREV) {
    if (confirm_send) {
      confirm_send = false;
      return true;
    } else if (!in_channel_selection) {
      in_channel_selection = true;
      return true;
    } else {
      _task->gotoHomeScreen();
      return true;
    }
  }

  if (confirm_send) {
    if (c == KEY_UP) {
      _pending_send = true;
      return true;
    }
    if (c == KEY_DOWN) {
      confirm_send = false;
      return true;
    }
    return false;
  }

  if (in_channel_selection) {
    if (c == KEY_UP) {
      nextChannel(false);
      return true;
    }
    if (c == KEY_DOWN) {
      nextChannel(true);
      return true;
    }
    if (c == KEY_ENTER) {
      in_channel_selection = false;
      return true;
    }
  } else {
    if (c == KEY_UP) {
      if (selected_message > 0) selected_message--;
      if (selected_message < scroll_offset) scroll_offset = selected_message;
      return true;
    }
    if (c == KEY_DOWN) {
      if (selected_message < message_count - 1) selected_message++;
      if (selected_message >= scroll_offset + MESSAGES_PER_PAGE)
        scroll_offset = selected_message - MESSAGES_PER_PAGE + 1;
      return true;
    }
    if (c == KEY_ENTER) {
      confirm_send = true;
      return true;
    }
  }

#elif defined(PIN_USER_BTN)
  // ============================================================
  // SINGLE BUTTON NAVIGATION
  // KEY_NEXT  = single click  = cycle / go back
  // KEY_ENTER = long press    = select / send
  // ============================================================

  if (c == KEY_NEXT) {
    if (confirm_send) {
      // Single click on confirm screen = go back to message selection
      confirm_send = false;
      confirm_option = 0;
    } else if (in_channel_selection) {
      if (selected_channel == -1) {
        // Currently on back - wrap to first valid channel
        selected_channel = 0;
        while (selected_channel < MAX_GROUP_CHANNELS && !isValidChannel(selected_channel)) {
          selected_channel++;
        }
      } else {
        // Find next valid channel
        int next = selected_channel + 1;
        bool found = false;
        while (next < MAX_GROUP_CHANNELS) {
          if (isValidChannel(next)) {
            selected_channel = next;
            found = true;
            break;
          }
          next++;
        }
        // No more valid channels - go to back option
        if (!found) selected_channel = -1;
      }
    } else {
      // Cycle through messages + back option (wraps at message_count + 1)
      selected_message++;
      if (selected_message > message_count) {
        selected_message = 0; // Wrap to first message
      }

      // Update scroll offset - back option scrolls into view like any other item
      if (selected_message < scroll_offset) scroll_offset = selected_message;
      if (selected_message >= scroll_offset + MESSAGES_PER_PAGE)
        scroll_offset = selected_message - MESSAGES_PER_PAGE + 1;
    }
    return true;
  }

  if (c == KEY_ENTER) {
    if (confirm_send) {
      // Long press on confirm screen = send
      _pending_send = true;
    } else if (in_channel_selection) {
      if (selected_channel == -1) {
        // Back to home
        _task->gotoHomeScreen();
      } else {
        // Confirm channel, go to message selection
        in_channel_selection = false;
        selected_message = 0;
        scroll_offset = 0;
      }
    } else {
      if (selected_message >= message_count) {
        // Back to channel selection
        in_channel_selection = true;
      } else {
        // Confirm message, go to confirm screen
        confirm_send = true;
        confirm_option = 0;
      }
    }
    return true;
  }

#endif

  return false;
}

// ------------------------------------------------------------
// Poll
// ------------------------------------------------------------
void CannedMessagesScreen::poll() {
  if (_pending_send) {
    _pending_send = false;

    ChannelDetails details;
    if (the_mesh.getChannel(selected_channel, details)) {
      auto now = the_mesh.getRTCClock()->getCurrentTime();
      the_mesh.sendGroupMessage(now, details.channel, the_mesh.getNodeName(), messages[selected_message],
                                strlen(messages[selected_message]));
      _task->showAlert("Message sent!", 1000);
    }
    reset();
    _task->gotoHomeScreen();
  }
}