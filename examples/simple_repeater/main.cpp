#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"
#include "ConfigTarget.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(board, display);
#endif

#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Repeater CLI"
  #include <helpers/nrf52/EthernetCLI.h>
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

// Read-only Stream wrapper over an already-collected in-memory buffer,
// used to feed ConfigSerializer::loadSerial() after a "put config" paste
// has been fully received.
class MemoryStream : public Stream {
  const char* _data;
  size_t _len, _pos = 0;
public:
  MemoryStream(const char* data, size_t len) : _data(data), _len(len) {}
  int available() override { return _len - _pos; }
  int read() override  { return _pos < _len ? (uint8_t)_data[_pos++] : -1; }
  int peek() override  { return _pos < _len ? (uint8_t)_data[_pos]   : -1; }
  size_t write(uint8_t) override { return 0; }  // unused, read-only
  void flush() override { }  // unused, read-only (pure virtual in the nRF52 core)
};

static char command[160];
#ifdef ETHERNET_ENABLED
static char ethernet_command[160];
#endif

// Command history (up/down arrow recall), serial CLI only.
#define CLI_HISTORY_SIZE 8
static char cli_history[CLI_HISTORY_SIZE][160];
static int cli_history_count = 0;   // number of entries actually stored
static int cli_history_next = 0;    // ring-buffer write position
static int cli_history_pos = -1;    // -1 = not browsing; 0..count-1 = index back from newest

// Small state machine for parsing ANSI escape sequences: arrow keys
// (ESC '[' 'A'/'B'/'C'/'D') and the Forward-Delete key (ESC '[' '3' '~'),
// since the bytes can arrive across separate loop() iterations rather
// than all at once.
enum EscState { ESC_NONE, ESC_GOT_ESC, ESC_GOT_BRACKET, ESC_GOT_DIGIT };
static EscState esc_state = ESC_NONE;
static char esc_digit = 0;  // pending digit between '[' and '~', e.g. '3' for Delete

// Cursor position within 'command' (0..strlen(command)), for left/right
// arrow editing and correct redraw when recalling history.
static int cmd_cursor = 0;

// Moves the terminal's visual cursor left, without erasing anything.
static void cliMoveCursorLeft() {
  if (cmd_cursor > 0) { cmd_cursor--; Serial.print('\b'); }
}

// Moves the terminal's visual cursor right, without erasing anything.
static void cliMoveCursorRight() {
  int len = strlen(command);
  if (cmd_cursor < len) { Serial.print(command[cmd_cursor]); cmd_cursor++; }
}

// Prints the remainder of 'command' after the cursor, so the terminal's
// visual cursor catches up to cmd_cursor before a full-line operation.
static void cliMoveCursorToEnd() {
  Serial.print(command + cmd_cursor);
  cmd_cursor = strlen(command);
}

// Inserts 'c' at cmd_cursor, shifting any text after it to the right,
// and redraws only the affected tail (not the whole line).
static void cliInsertChar(char c) {
  int len = strlen(command);
  if (len >= (int)sizeof(command) - 1) return;  // buffer full
  memmove(command + cmd_cursor + 1, command + cmd_cursor, len - cmd_cursor + 1);  // incl. null term
  command[cmd_cursor] = c;
  Serial.print(command + cmd_cursor);  // print new char + everything after it
  cmd_cursor++;
  int tail_len = (len + 1) - cmd_cursor;  // chars now after the cursor
  for (int i = 0; i < tail_len; i++) Serial.print('\b');  // walk cursor back
}

// Deletes the character immediately before cmd_cursor (Backspace/DEL),
// shifting text after it left and redrawing only the affected tail.
static void cliDeleteBeforeCursor() {
  if (cmd_cursor == 0) return;
  int len = strlen(command);
  memmove(command + cmd_cursor - 1, command + cmd_cursor, len - cmd_cursor + 1);  // incl. null term
  cmd_cursor--;
  Serial.print('\b');                  // step back over the deleted char
  Serial.print(command + cmd_cursor);  // redraw shifted tail
  Serial.print(' ');                   // clear the leftover char at the old end
  int tail_len = (len - cmd_cursor);   // tail chars + the trailing space just printed
  for (int i = 0; i < tail_len; i++) Serial.print('\b');
}

// Deletes the character at cmd_cursor (Delete key / forward-delete),
// shifting text after it left and redrawing only the affected tail.
// The cursor position itself doesn't move.
static void cliDeleteAtCursor() {
  int len = strlen(command);
  if (cmd_cursor >= len) return;  // nothing to the right of the cursor
  memmove(command + cmd_cursor, command + cmd_cursor + 1, len - cmd_cursor);  // incl. null term
  Serial.print(command + cmd_cursor);  // redraw shifted tail
  Serial.print(' ');                   // clear the leftover char at the old end
  int tail_len = (len - cmd_cursor);   // tail chars (post-shift) + the trailing space
  for (int i = 0; i < tail_len; i++) Serial.print('\b');
}

// Clears the currently-displayed command (assumes the visual cursor is
// already at the end - call cliMoveCursorToEnd() first) and replaces
// 'command' with 'text', then reprints it with the cursor at the end.
static void cliReplaceLine(const char* text) {
  int len = strlen(command);
  for (int i = 0; i < len; i++) Serial.print("\b \b");
  strncpy(command, text, sizeof(command) - 1);
  command[sizeof(command) - 1] = 0;
  Serial.print(command);
  cmd_cursor = strlen(command);
}

static void cliHistoryPush(const char* line) {
  if (line[0] == 0) return;  // don't store empty lines
  // Skip if identical to the most recently stored entry
  if (cli_history_count > 0) {
    int last = (cli_history_next - 1 + CLI_HISTORY_SIZE) % CLI_HISTORY_SIZE;
    if (strcmp(cli_history[last], line) == 0) return;
  }
  strncpy(cli_history[cli_history_next], line, sizeof(cli_history[0]) - 1);
  cli_history[cli_history_next][sizeof(cli_history[0]) - 1] = 0;
  cli_history_next = (cli_history_next + 1) % CLI_HISTORY_SIZE;
  if (cli_history_count < CLI_HISTORY_SIZE) cli_history_count++;
}

// idx counts back from the newest entry (0 = newest). Returns NULL if out of range.
static const char* cliHistoryGet(int idx) {
  if (idx < 0 || idx >= cli_history_count) return NULL;
  int pos = (cli_history_next - 1 - idx + CLI_HISTORY_SIZE * 2) % CLI_HISTORY_SIZE;
  return cli_history[pos];
}

// State for the non-blocking "put config" paste capture. Kept outside the
// normal command[] buffer so it can span many loop() iterations without
// ever blocking the_mesh.loop() (mesh RX/forwarding) while a user pastes
// or types config JSON over serial.
#define CONFIG_PASTE_BUF_SIZE 4096
static char config_paste_buf[CONFIG_PASTE_BUF_SIZE];
static size_t config_paste_len = 0;
static bool awaiting_config_paste = false;
static ConfigTarget awaiting_config_target = CFG_PREFS;

// Returns true and sets 'target' if 'arg' names a known config target.
// Empty arg ("get config"/"put config" with nothing after) means the
// normal node prefs. "filter"/"report" only match when the corresponding
// optional engine is compiled in (see ConfigTarget.h).
static bool parseConfigTarget(const char* arg, ConfigTarget& target) {
  if (arg[0] == 0) { target = CFG_PREFS; return true; }
#ifdef WITH_CHANNEL_FILTER
  if (strcmp(arg, "filter") == 0) { target = CFG_FILTER; return true; }
#endif
#ifdef WITH_REPORT_ENGINE
  if (strcmp(arg, "report") == 0) { target = CFG_REPORT; return true; }
#endif
  return false;
}

// For power saving
unsigned long POWERSAVING_FIRSTSLEEP_SECS = 300; // The first sleep (if enabled) from boot

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
static unsigned long userBtnDownAt = 0;
#define USER_BTN_HOLD_OFF_MILLIS 1500
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // give some extra time for serial to settle so
  // boot debug messages can be seen on terminal
  delay(5000);
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

  // PowerSaving variant
#if ENV_INCLUDE_GPS == 1
  // Apply PowerSaving profile for GPS
  if (sensors.getLocationProvider() != NULL) {
    // GPS on and off duration in seconds
    sensors.getLocationProvider()->setPowerSavingProfile(the_mesh.getNodePrefs()->powersaving_enabled, 600,
                                                         86400); // Max 10 minutes, 1 day
  }
#endif

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  // PowerSaving variant
  // Added board for battery display
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION, &board);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
}

void loop() {
  // Handle Serial CLI
  if (awaiting_config_paste) {
    // Non-blocking capture of a "put config" paste. Uses the same line
    // convention as the normal command parsing below ('\n' is ignored,
    // '\r' is the real line terminator), but here we watch for a line
    // that contains only "." as the end-of-input marker. Only a small
    // fixed-size buffer is scanned per call, so this never stalls
    // the_mesh.loop() while the user is typing or pasting.
    while (Serial.available() && config_paste_len < CONFIG_PASTE_BUF_SIZE - 1) {
      char c = Serial.read();

      if (c == 8 || c == 127) {   // Backspace (0x08) or DEL (0x7F)
        // Only edit within the current line; never delete a previous line's '\r'.
        if (config_paste_len > 0 && config_paste_buf[config_paste_len - 1] != '\r') {
          config_paste_len--;
          config_paste_buf[config_paste_len] = 0;
          Serial.print("\b \b");  // erase char on the terminal
        }
        continue;
      }

      if (c == '\n') continue;   // same convention as command parsing below
      Serial.print(c);
      config_paste_buf[config_paste_len++] = c;
      config_paste_buf[config_paste_len] = 0;

      if (c == '\r') {
        size_t line_start = config_paste_len - 1;
        while (line_start > 0 && config_paste_buf[line_start - 1] != '\r') line_start--;
        size_t line_len = (config_paste_len - 1) - line_start;

        if (line_len == 1 && config_paste_buf[line_start] == '.') {
          config_paste_buf[line_start] = 0;   // cut off the terminator line
          Serial.println();

          MemoryStream ms(config_paste_buf, line_start);
          bool ok = the_mesh.loadConfigFromStream(ms, awaiting_config_target);
          Serial.print("  -> "); Serial.println(ok ? "OK" : "ERROR: config parse failed");

          awaiting_config_paste = false;
          config_paste_len = 0;
          break;
        }
      }
    }
    if (awaiting_config_paste && config_paste_len >= CONFIG_PASTE_BUF_SIZE - 1) {
      Serial.println("  -> ERROR: config paste too large, aborted");
      awaiting_config_paste = false;
      config_paste_len = 0;
    }
  } else {
    int len = strlen(command);
    // `command` must stay NUL-terminated within its bounds. If it ever isn't,
    // strlen() above can return >= sizeof(command) and the loop below would then
    // index past the buffer, so clamp defensively.
    if (len >= (int)sizeof(command)) {
      command[0] = 0;
      len = 0;
    }
    if (len == 0) cmd_cursor = 0;  // keep the edit cursor in sync with an empty buffer
    while (Serial.available() && (int)strlen(command) < (int)sizeof(command)-1) {
      char c = Serial.read();

      // Feed the ANSI escape sequence state machine first. Bytes belonging
      // to an in-progress sequence are consumed here and never reach the
      // normal command-editing logic below.
      if (esc_state == ESC_NONE && c == 27) {          // ESC
        esc_state = ESC_GOT_ESC;
        continue;
      }
      if (esc_state == ESC_GOT_ESC) {
        esc_state = (c == '[') ? ESC_GOT_BRACKET : ESC_NONE;
        continue;
      }
      if (esc_state == ESC_GOT_BRACKET) {
        esc_state = ESC_NONE;
        if (c == 'A') {        // Up arrow: older command
          int next_pos = cli_history_pos + 1;
          const char* h = cliHistoryGet(next_pos);
          if (h != NULL) {
            cli_history_pos = next_pos;
            cliMoveCursorToEnd();
            cliReplaceLine(h);
          }
        } else if (c == 'B') {  // Down arrow: newer command (or blank)
          if (cli_history_pos > 0) {
            cli_history_pos--;
            const char* h = cliHistoryGet(cli_history_pos);
            cliMoveCursorToEnd();
            cliReplaceLine(h != NULL ? h : "");
          } else if (cli_history_pos == 0) {
            cli_history_pos = -1;
            cliMoveCursorToEnd();
            cliReplaceLine("");
          }
        } else if (c == 'C') {  // Right arrow
          cliMoveCursorRight();
        } else if (c == 'D') {  // Left arrow
          cliMoveCursorLeft();
        } else if (c >= '0' && c <= '9') {
          // Start of a multi-byte sequence like ESC[3~ (Delete). Remember
          // the digit and wait for the terminating '~'.
          esc_digit = c;
          esc_state = ESC_GOT_DIGIT;
        }
        // Any other final byte (Home/End etc.) is discarded.
        continue;
      }
      if (esc_state == ESC_GOT_DIGIT) {
        esc_state = ESC_NONE;
        if (c == '~' && esc_digit == '3') {  // Delete key: ESC[3~
          cliDeleteAtCursor();
        }
        // Any other terminator (other ESC[n~ codes) is discarded.
        continue;
      }

      if (c == 8 || c == 127) {   // Backspace (0x08) or DEL (0x7F)
        cliDeleteBeforeCursor();
        continue;
      }

      if (c == '\r') {
        // Append the terminator at the true end of the buffer, regardless
        // of where the edit cursor currently sits.
        int l = strlen(command);
        if (l < (int)sizeof(command) - 1) {
          command[l] = '\r';
          command[l + 1] = 0;
        }
        break;
      }

      if (c != '\n') cliInsertChar(c);
    }

    len = strlen(command);
    if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
      command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
      command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
    }

    if (len > 0 && command[len - 1] == '\r') {  // received complete line
      Serial.print('\n');
      command[len - 1] = 0;  // replace newline with C string null terminator

      cliHistoryPush(command);
      cli_history_pos = -1;

      const char* GET_CONFIG_PREFIX = "get config";
      const char* PUT_CONFIG_PREFIX = "put config";
      size_t GET_LEN = strlen(GET_CONFIG_PREFIX);
      size_t PUT_LEN = strlen(PUT_CONFIG_PREFIX);

      if (strncmp(command, GET_CONFIG_PREFIX, GET_LEN) == 0 &&
          (command[GET_LEN] == 0 || command[GET_LEN] == ' ')) {
        const char* arg = command[GET_LEN] == ' ' ? command + GET_LEN + 1 : command + GET_LEN;
        ConfigTarget target;
        if (!parseConfigTarget(arg, target)) {
          Serial.println("  -> ERROR: unknown config target");
        } else {
          // Dump the live in-memory config as JSON directly to Serial.
          // Bypasses the 160-byte reply[] buffer entirely, since a full
          // config dump can easily exceed it.
          Serial.println();
          bool ok = the_mesh.dumpConfigToStream(Serial, target);
          Serial.println();
          if (!ok) Serial.println("  -> ERROR: config dump failed");
        }
      } else if (strncmp(command, PUT_CONFIG_PREFIX, PUT_LEN) == 0 &&
                 (command[PUT_LEN] == 0 || command[PUT_LEN] == ' ')) {
        const char* arg = command[PUT_LEN] == ' ' ? command + PUT_LEN + 1 : command + PUT_LEN;
        if (!parseConfigTarget(arg, awaiting_config_target)) {
          Serial.println("  -> ERROR: unknown config target");
        } else {
          Serial.println("Paste config JSON, end with a line containing only '.':");
          awaiting_config_paste = true;
          config_paste_len = 0;
        }
      } else {
        char reply[160];
        reply[0] = 0;
#ifdef ETHERNET_ENABLED
        if (!ethernet_handle_command(command, reply)) {
          the_mesh.handleCommand(0, NULL, command, reply);
        }
#else
        the_mesh.handleCommand(0, NULL, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
        if (reply[0]) {
          Serial.print("  -> "); Serial.println(reply);
        }
      }

      command[0] = 0;  // reset command buffer
      cmd_cursor = 0;
    }
  }

  board.loop();   // let the board feed its watchdog, run periodic housekeeping

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    if (!ethernet_handle_command(ethernet_command, reply)) {
      the_mesh.handleCommand(0, ethernet_command, reply);
    }
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_) && !defined(DISPLAY_CLASS)
  // Hold the user button to power off the SenseCAP Solar repeater.
  int btnState = digitalRead(PIN_USER_BTN);
  if (btnState == LOW) {
    if (userBtnDownAt == 0) {
      userBtnDownAt = millis();
    } else if ((unsigned long)(millis() - userBtnDownAt) >= USER_BTN_HOLD_OFF_MILLIS) {
      Serial.println("Powering off...");
      board.powerOff();  // does not return
    }
  } else {
    userBtnDownAt = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

  // Don't sleep while a "put config" paste is in progress, otherwise
  // UART reception could be disrupted mid-paste.
  if (!awaiting_config_paste && the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#else
    if (the_mesh.millisHasNowPassed(POWERSAVING_FIRSTSLEEP_SECS * 1000)) { // To check if it is time to sleep
      board.sleep(30); // Sleep. Wake up after a while or when receiving a LoRa packet
    }
#endif
  }

  // PowerSaving variant
  if (the_mesh.getNodePrefs()->reboot_interval > 0 &&
      the_mesh.millisHasNowPassed(the_mesh.getNodePrefs()->reboot_interval * 3600000)) {
    board.reboot();
  }
}