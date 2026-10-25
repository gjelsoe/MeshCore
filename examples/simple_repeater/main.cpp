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
    while (Serial.available() && len < sizeof(command)-1) {
      char c = Serial.read();
      if (c != '\n') {
        command[len++] = c;
        command[len] = 0;
        Serial.print(c);
      }
      if (c == '\r') break;
    }
    if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
      command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
      command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
    }

    if (len > 0 && command[len - 1] == '\r') {  // received complete line
      Serial.print('\n');
      command[len - 1] = 0;  // replace newline with C string null terminator

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
