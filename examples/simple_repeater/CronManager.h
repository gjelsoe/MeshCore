#pragma once

#include <Mesh.h>              // mesh::RTCClock, ClientInfo
#include <helpers/CommonCLI.h> // CommonCLI, NodePrefs
#include <helpers/ConfigSerializer.h>

#define CRON_RULES_FILE    "/cron_rules.json"
#define CRON_MAX_KEY_LEN   16
#define CRON_MAX_VALUE_LEN 128 // large enough for owner_info[120] etc.

#ifndef MAX_CRON_ENTRIES
#define MAX_CRON_ENTRIES 16
#endif
#if MAX_CRON_ENTRIES > 26
#error MAX_CRON_ENTRIES > 26 not supported: slot keys are encoded as a single letter a-z, since ConfigSerializer key tokens cannot contain digits
#endif

struct CronEntry {
  bool in_use = false;
  bool enabled = false;
  bool persist =
      false;          // if true, a fired change is also written to flash (via 'persist' keyword on cron add)
  uint8_t hour = 0;   // 0-23
  uint8_t minute = 0; // 0-59
  int16_t last_fired_yday = -1; // -1 = never fired
  char key[CRON_MAX_KEY_LEN] = { 0 };
  char value[CRON_MAX_VALUE_LEN] = { 0 };
};

// Schedules "set <key> <value>" style config changes to run at a given
// time of day (e.g. "cron set cad off time:21"). Does NOT send anything
// over the mesh -- purely a local settings-change scheduler.
//
// Persistence via ConfigSerializer/JSON, same as NodePrefs/ChannelFilter --
// entries are a fixed-size array of named sub-objects ("e0".."eN"), each
// an in_use slot, mirroring how ChannelFilter stores FilterRule[MAX_FILTER_RULES].
class CronManager : public ConfigSerializer {
public:
  CronManager();

  void begin(CommonCLI *cli, mesh::RTCClock *rtc, FILESYSTEM &fs);
  void loop(); // call once per second (or so) from MyMesh::loop()

  // Mirrors ChannelFilter::handleCommand()'s signature/pattern:
  // sender == NULL -> local serial (full listing, printed directly to Serial)
  // sender != NULL -> remote client (paginated reply, packet-size budgeted)
  void handleCommand(const char *args, char *reply, FILESYSTEM &fs, ClientInfo *sender);

  void load(FILESYSTEM &fs);
  void save(FILESYSTEM &fs) const;

protected:
  void structure() override;

private:
  // one ConfigSerializer sub-object per fixed slot, same idea as NodePrefs' RadioPrefs etc.
  class EntrySerializer : public ConfigSerializer {
    CronEntry *_e = nullptr;

  protected:
    void structure() override;

  public:
    void bind(CronEntry *e) { _e = e; }
  };

  CronEntry _entries[MAX_CRON_ENTRIES];
  EntrySerializer _entrySer[MAX_CRON_ENTRIES];

  CommonCLI *_cli = nullptr;
  mesh::RTCClock *_rtc = nullptr;
  FILESYSTEM *_fs = nullptr;
  bool _timeWasValid = false;

  int _firstFreeSlot() const;
  void applySetting(const char *key, const char *value, bool persist);
  void onTimeBecameValid(uint32_t epoch);

  // list formatting -- same paginate-for-remote / dump-for-serial split as ChannelFilter::_listRules()
  void _listEntries(char *reply, uint8_t page, bool is_remote) const;
};