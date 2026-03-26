#pragma once

#include <helpers/ui/UIScreen.h>

class UITask;
class DisplayDriver;
class AbstractUITask;

class CannedMessagesScreen : public UIScreen {
public:
  explicit CannedMessagesScreen(UITask *task);
  CannedMessagesScreen() : CannedMessagesScreen(nullptr) {}  // CLI rescue use

  int render(DisplayDriver &display) override;
  bool handleInput(char c) override;
  void poll() override;

  void reset();

  // Load messages from /canned.txt — shows placeholder if file missing
  void loadFromFile();

  // Save current messages back to /canned.txt
  bool saveToFile();

  // Handle "canned ..." CLI commands — called from MyMesh rescue CLI
  // ui may be nullptr when called from CLI rescue mode (no UI running)
  static void handleCLI(const char *command, AbstractUITask *ui);

  // CLI helpers
  int getMessageCount() const { return message_count; }
  const char *getMessage(int idx) const {
    if (idx < 0 || idx >= message_count) return nullptr;
    return _msg_buf[idx];
  }
  bool addMessage(const char *msg) {
    if (!msg || message_count >= MAX_MSG_COUNT) return false;
    int len = strlen(msg);
    if (len == 0 || len > MAX_MSG_LENGTH) return false;
    strncpy(_msg_buf[message_count], msg, MAX_MSG_LENGTH);
    _msg_buf[message_count][MAX_MSG_LENGTH] = '\0';
    messages[message_count] = _msg_buf[message_count];
    message_count++;
    return true;
  }
  bool deleteMessage(int idx) {
    if (idx < 0 || idx >= message_count) return false;
    for (int i = idx; i < message_count - 1; i++) {
      strncpy(_msg_buf[i], _msg_buf[i + 1], MAX_MSG_LENGTH + 1);
      messages[i] = _msg_buf[i];
    }
    message_count--;
    _msg_buf[message_count][0] = '\0';
    messages[message_count] = nullptr;
    return true;
  }

private:
  UITask *_task;

  bool _pending_send = false;
  static constexpr int MAX_MSG_COUNT = 20;
  static constexpr int MAX_MSG_LENGTH = 40;
  static constexpr int MESSAGES_PER_PAGE = 4;
  static constexpr const char *CANNED_FILE = "/canned.txt";

  // Mutable storage for file-loaded messages (owned by this class)
  char _msg_buf[MAX_MSG_COUNT][MAX_MSG_LENGTH + 1];
  const char *messages[MAX_MSG_COUNT];
  int message_count;

  int selected_channel;
  int selected_message;
  int scroll_offset;

  bool in_channel_selection;
  bool confirm_send;

#if defined(PIN_USER_BTN) || defined(USE_ENCODER)
  int confirm_option; // 0=Send, 1=Cancel, 2=Back
  int getValidChannelCount();
  int getTotalItems();
  bool isOnBackOption();
#endif

  void countMessages();
  bool isValidChannel(int idx);
  void nextChannel(bool forward);
  const char *getChannelName(int idx);

  void renderNoMessages(DisplayDriver &display);
  void renderChannelSelection(DisplayDriver &display);
  void renderMessageSelection(DisplayDriver &display);
  void renderConfirmSend(DisplayDriver &display);
};