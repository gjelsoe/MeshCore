#pragma once

#include <helpers/ui/UIScreen.h>

class UITask;
class DisplayDriver;

class CannedMessagesScreen : public UIScreen {
public:
  explicit CannedMessagesScreen(UITask *task);

  int render(DisplayDriver &display) override;
  bool handleInput(char c) override;
  void poll() override;

  void reset();

private:
  UITask *_task;

  bool _pending_send = false;
  static constexpr int MAX_MSG_COUNT = 20;
  static constexpr int MAX_MSG_LENGTH = 40;
  static constexpr int MESSAGES_PER_PAGE = 4;

  const char *messages[MAX_MSG_COUNT];
  int message_count;

  int selected_channel;
  int selected_message;
  int scroll_offset;

  bool in_channel_selection;
  bool confirm_send;

  void countMessages();
  bool isValidChannel(int idx);
  void nextChannel(bool forward);
  const char *getChannelName(int idx);

  void renderChannelSelection(DisplayDriver &display);
  void renderMessageSelection(DisplayDriver &display);
  void renderConfirmSend(DisplayDriver &display);
};
