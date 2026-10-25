#include "CronManager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // gmtime_r / struct tm

CronManager::CronManager() {
  for (int i = 0; i < MAX_CRON_ENTRIES; i++) {
    _entrySer[i].bind(&_entries[i]);
  }
}

void CronManager::begin(CommonCLI *cli, mesh::RTCClock *rtc, FILESYSTEM &fs) {
  _cli = cli;
  _rtc = rtc;
  _fs = &fs;
  load(fs);
}

// -------------------- ConfigSerializer structure --------------------
// top-level: one named sub-object per fixed slot -- keys are "ea".."e"+letter,
// NOT "e0".."e15": ConfigSerializer's key tokenizer only accepts letters/'_',
// never digits, so a digit-suffixed key writes fine but fails to parse back
// on load (silently -- loadSerial() just stops at the bad key, leaving the
// list empty). MAX_CRON_ENTRIES is capped at 26 accordingly (see header).
void CronManager::structure() {
  char keybuf[3];
  for (int i = 0; i < MAX_CRON_ENTRIES; i++) {
    keybuf[0] = 'e';
    keybuf[1] = 'a' + i;
    keybuf[2] = 0;
    def(keybuf, _entrySer[i]);
  }
}

// per-slot fields, same idea as NodePrefs' RadioPrefs/BridgePrefs sub-objects
void CronManager::EntrySerializer::structure() {
  if (!_e) return;
  def("on", _e->in_use);
  def("en", _e->enabled);
  def("p", _e->persist);
  def("h", _e->hour);
  def("m", _e->minute);
  def("lfy", _e->last_fired_yday);
  def("k", _e->key, sizeof(_e->key));
  def("v", _e->value, sizeof(_e->value));
}

// -------------------- persistence (JSON, via ConfigSerializer) --------------------
void CronManager::load(FILESYSTEM &fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = fs.open(CRON_RULES_FILE, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  File f = fs.open(CRON_RULES_FILE, "r");
#else
  File f = fs.open(CRON_RULES_FILE);
#endif
  if (!f) return;
  loadSerial(f);
  f.close();
}

void CronManager::save(FILESYSTEM &fs) const {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs.remove(CRON_RULES_FILE);
  File f = fs.open(CRON_RULES_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = fs.open(CRON_RULES_FILE, "w");
#else
  if (fs.exists(CRON_RULES_FILE)) fs.remove(CRON_RULES_FILE);
  File f = fs.open(CRON_RULES_FILE, "w");
#endif
  if (!f) return;
  // saveSerial() requires a non-const Stream -- cast is safe since it only writes
  const_cast<CronManager *>(this)->saveSerial(f);
  f.close();
}

// -------------------- runtime --------------------
static void breakdownEpoch(uint32_t epoch, int &hour, int &minute, int &yday) {
  time_t t = (time_t)epoch;
  struct tm tmv;
  gmtime_r(&t, &tmv); // RTCClock epoch is UTC
  hour = tmv.tm_hour;
  minute = tmv.tm_min;
  yday = tmv.tm_yday;
}

int CronManager::_firstFreeSlot() const {
  for (int i = 0; i < MAX_CRON_ENTRIES; i++) {
    if (!_entries[i].in_use) return i;
  }
  return -1;
}

// Called the moment the clock transitions from "no time" -> "time known"
// (e.g. GPS fix / manual set just after boot on a node with no HW RTC).
// Prevents a stale entry from firing retroactively just because the clock
// jumped several hours forward at once, while still letting an entry fire
// normally if the fix happens to land in its exact target minute.
void CronManager::onTimeBecameValid(uint32_t epoch) {
  int hour, minute, yday;
  breakdownEpoch(epoch, hour, minute, yday);

  bool changed = false;
  for (auto &e : _entries) {
    if (!e.in_use) continue;
    if (e.last_fired_yday == yday) continue;            // already marked done today
    if (hour == e.hour && minute == e.minute) continue; // let normal loop() fire it
    bool alreadyPast = (hour > e.hour) || (hour == e.hour && minute > e.minute);
    if (alreadyPast) {
      e.last_fired_yday = yday; // mark as done without executing (late boot, not a real tick)
      changed = true;
    }
  }
  if (changed && _fs) save(*_fs);
}

void CronManager::loop() {
  if (!_rtc) return;
  // isTimeValid() reflects whether getCurrentTime() is a real synced time
  // (GPS/manual/HW RTC) rather than a board-specific fallback seed that
  // ticks forward on its own before any real sync -- see mesh::RTCClock.
  bool validNow = _rtc->isTimeValid();
  uint32_t epoch = _rtc->getCurrentTime();

  if (validNow && !_timeWasValid) {
    onTimeBecameValid(epoch);
  }
  _timeWasValid = validNow;
  if (!validNow) return;

  int hour, minute, yday;
  breakdownEpoch(epoch, hour, minute, yday);

  bool anyChanged = false;
  for (auto &e : _entries) {
    if (!e.in_use || !e.enabled) continue;
    if (e.hour == hour && e.minute == minute && e.last_fired_yday != yday) {
      applySetting(e.key, e.value, e.persist);
      e.last_fired_yday = yday;
      anyChanged = true;
    }
  }
  if (anyChanged && _fs) save(*_fs);
}

void CronManager::applySetting(const char *key, const char *value, bool persist) {
  char cmd[8 + CRON_MAX_KEY_LEN + CRON_MAX_VALUE_LEN]; // "set " + key + " " + value
  char tmp_reply[160];
  snprintf(cmd, sizeof(cmd), "set %s %s", key, value);
  _cli->handleCommand(0, cmd, tmp_reply); // sender_timestamp=0 = local/system-initiated
  if (persist) {
    _cli->savePrefs(_fs); // only write to flash if 'persist' was set on cron add
  }
  // persist == false: applied in RAM only via handleCommand() above; reverts
  // to whatever's on flash the next time NodePrefs is loaded (reboot, or a
  // later persisted set/cron fire) -- no flash write happens here at all
}

// -------------------- list formatting --------------------
// mirrors ChannelFilter's FILTER_REPLY_* budget/pagination constants
#define CRON_REPLY_BUDGET     128
#define CRON_REPLY_HEADER_MAX 32
#define CRON_REPLY_HINT_LEN   18 // "-> cron list N\0"

// Format a single entry line into buf (null-terminated). Returns chars written.
#define CRON_LINE_BUF_LEN     (CRON_MAX_KEY_LEN + CRON_MAX_VALUE_LEN + 24)

static int formatEntryLine(int id, const CronEntry &e, char *buf, int buflen) {
  return snprintf(buf, buflen, "#%d %s=%s %02d:%02d %s %s\n", id, e.key, e.value, e.hour, e.minute,
                  e.enabled ? "on" : "off", e.persist ? "persist" : "temp");
}

void CronManager::_listEntries(char *reply, uint8_t page, bool is_remote) const {
  // collect in_use indexes first, same idea as ChannelFilter's index-collection pass
  uint8_t idx[MAX_CRON_ENTRIES];
  uint8_t total = 0;
  for (int i = 0; i < MAX_CRON_ENTRIES; i++) {
    if (_entries[i].in_use) idx[total++] = i;
  }

  if (!is_remote) {
    // local serial: no packet-size limit, dump the full list directly
    Serial.printf("cron entries:%d\n", total);
    for (uint8_t n = 0; n < total; n++) {
      char line_buf[CRON_LINE_BUF_LEN];
      formatEntryLine(idx[n], _entries[idx[n]], line_buf, sizeof(line_buf));
      Serial.printf("%s", line_buf);
    }
    reply[0] = '\0';
    return;
  }

  // remote client: paginate, same dynamic budget-based page-splitting as ChannelFilter::_listRules()
  uint8_t page_start[MAX_CRON_ENTRIES + 2];
  uint8_t num_pages = 0;
  page_start[0] = 0;

  {
    uint8_t n = 0;
    while (n < total) {
      int budget = CRON_REPLY_BUDGET - CRON_REPLY_HEADER_MAX - CRON_REPLY_HINT_LEN;
      uint8_t page_end = n;

      while (page_end < total) {
        char line_buf[CRON_LINE_BUF_LEN];
        int line_len = formatEntryLine(idx[page_end], _entries[idx[page_end]], line_buf, sizeof(line_buf));
        if (budget - line_len < 0) break; // doesn't fit
        budget -= line_len;
        page_end++;
      }

      if (page_end == n) page_end = n + 1; // always advance at least one entry

      num_pages++;
      n = page_end;
      page_start[num_pages] = n;
    }
  }

  if (total == 0) num_pages = 1;

  if (page >= num_pages) {
    snprintf(reply, 80, "Err - page %d out of range (0-%d)", page, num_pages - 1);
    return;
  }

  int pos;
  if (num_pages > 1) {
    pos = snprintf(reply, CRON_REPLY_BUDGET, "cron entries:%d p%d/%d\n", total, page + 1, num_pages);
  } else {
    pos = snprintf(reply, CRON_REPLY_BUDGET, "cron entries:%d\n", total);
  }

  if (total == 0) {
    snprintf(reply + pos, CRON_REPLY_BUDGET - pos, "(no entries)");
    return;
  }

  uint8_t start = page_start[page];
  uint8_t end = page_start[page + 1];
  for (uint8_t n = start; n < end; n++) {
    char line_buf[CRON_LINE_BUF_LEN];
    formatEntryLine(idx[n], _entries[idx[n]], line_buf, sizeof(line_buf));
    pos += snprintf(reply + pos, CRON_REPLY_BUDGET - pos, "%s", line_buf);
  }

  if (page + 1 < num_pages) {
    snprintf(reply + pos, CRON_REPLY_BUDGET - pos, "-> cron list %d", page + 1);
  }
}

// -------------------- CLI dispatch --------------------
// 'args' is everything after "cron " (or "" for bare "cron").
void CronManager::handleCommand(const char *args, char *reply, FILESYSTEM &fs, ClientInfo *sender) {
  bool is_remote = (sender != NULL); // NULL = local serial, same convention as ChannelFilter
  while (*args == ' ')
    args++;

  if (strncmp(args, "add ", 4) == 0) {
    args += 4;
    while (*args == ' ')
      args++;

    // key = first whitespace-delimited token (setting names are single words)
    const char *sp = strchr(args, ' ');
    if (!sp) {
      snprintf(reply, 80, "Usage: cron add <key> <value> time:<HH>[:MM] [persist]");
      return;
    }
    size_t key_len = sp - args;
    if (key_len >= CRON_MAX_KEY_LEN) key_len = CRON_MAX_KEY_LEN - 1;
    char key[CRON_MAX_KEY_LEN];
    memcpy(key, args, key_len);
    key[key_len] = 0;

    const char *rest = sp + 1;
    while (*rest == ' ')
      rest++;

    // value may itself contain spaces (e.g. "owner" text) -- find the "time:"
    // marker instead of assuming a fixed single-token value
    const char *time_marker = strstr(rest, "time:");
    while (time_marker && time_marker != rest && *(time_marker - 1) != ' ') {
      time_marker = strstr(time_marker + 1, "time:"); // must start a token, not appear mid-word
    }
    if (!time_marker) {
      snprintf(reply, 80, "Usage: cron add <key> <value> time:<HH>[:MM] [persist]");
      return;
    }

    size_t value_len = time_marker - rest;
    while (value_len > 0 && rest[value_len - 1] == ' ')
      value_len--; // trim trailing space
    if (value_len == 0 || value_len >= CRON_MAX_VALUE_LEN) {
      snprintf(reply, 80, "Err - value missing or too long (max %d chars)", CRON_MAX_VALUE_LEN - 1);
      return;
    }
    char value[CRON_MAX_VALUE_LEN];
    memcpy(value, rest, value_len);
    value[value_len] = 0;

    int hour = -1, minute = 0;
    if (sscanf(time_marker + 5, "%d:%d", &hour, &minute) < 1 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59) {
      snprintf(reply, 80, "Invalid time (expected HH or HH:MM)");
      return;
    }

    // optional trailing bare "persist" keyword -- controls whether a fire
    // also calls _cli->savePrefs() (default: RAM-only, no flash write)
    bool persist = false;
    const char *persist_marker = strstr(time_marker, "persist");
    if (persist_marker) {
      bool token_start = (persist_marker == time_marker) || *(persist_marker - 1) == ' ';
      char after = persist_marker[7];
      bool token_end = (after == 0 || after == ' ');
      if (token_start && token_end) persist = true;
    }

    int slot = _firstFreeSlot();
    if (slot < 0) {
      snprintf(reply, 80, "Err - cron full (max %d entries)", MAX_CRON_ENTRIES);
      return;
    }

    CronEntry &e = _entries[slot];
    e.in_use = true;
    e.enabled = true;
    e.hour = (uint8_t)hour;
    e.minute = (uint8_t)minute;
    e.last_fired_yday = -1;
    e.persist = persist;
    strncpy(e.key, key, sizeof(e.key) - 1);
    e.key[sizeof(e.key) - 1] = 0;
    strncpy(e.value, value, sizeof(e.value) - 1);
    e.value[sizeof(e.value) - 1] = 0;
    save(fs);

    snprintf(reply, 80, "OK - cron #%d added: %s=%s at %02d:%02d (persist:%s)", slot, e.key, e.value, e.hour,
             e.minute, persist ? "yes" : "no");
    return;
  }

  if (strncmp(args, "list", 4) == 0 && (args[4] == 0 || args[4] == ' ')) {
    uint8_t page = 0;
    const char *pg = args + 4;
    while (*pg == ' ')
      pg++;
    if (*pg) page = (uint8_t)atoi(pg);
    _listEntries(reply, page, is_remote);
    return;
  }

  if (strncmp(args, "del ", 4) == 0) {
    int id = atoi(args + 4);
    if (id < 0 || id >= MAX_CRON_ENTRIES || !_entries[id].in_use) {
      snprintf(reply, 80, "Err - cron #%d not found", id);
      return;
    }
    _entries[id] = CronEntry{}; // reset slot (in_use=false, defaults)
    save(fs);
    snprintf(reply, 80, "OK - cron #%d deleted", id);
    return;
  }

  if (strncmp(args, "enable ", 7) == 0 || strncmp(args, "disable ", 8) == 0) {
    bool enable = args[0] == 'e';
    int id = atoi(args + (enable ? 7 : 8));
    if (id < 0 || id >= MAX_CRON_ENTRIES || !_entries[id].in_use) {
      snprintf(reply, 80, "Err - cron #%d not found", id);
      return;
    }
    _entries[id].enabled = enable;
    save(fs);
    snprintf(reply, 80, "OK - cron #%d %s", id, enable ? "enabled" : "disabled");
    return;
  }

  snprintf(reply, 80, "Err - unknown cron subcommand");
}