#pragma once

#define WITH_REPORT_ENGINE

#include "helpers/ConfigSerializer.h"

#include <Arduino.h>

// ── Limits ────────────────────────────────────────────────────────────────────
#define MAX_SCRIPT_RULES         8
#define SCRIPT_MIN_INTERVAL_SECS 1800 // 30 min absolute minimum
#define SCRIPT_RULES_FILE        "/report_rules"
#define SCRIPT_RULES_FILE_JSON   "/report_rules.json"
#define SCRIPT_ENGINE_VERSION    2 // binary format version — used for migration only

// ── Trigger types ─────────────────────────────────────────────────────────────
enum ScriptTriggerType : uint8_t {
  TRIGGER_BAT_BELOW = 0,   // bat<XXXX   (mV)
  TRIGGER_BAT_ABOVE = 1,   // bat>XXXX   (mV)
  TRIGGER_TEMP_BELOW = 2,  // temp<XX    (°C, int16)
  TRIGGER_TEMP_ABOVE = 3,  // temp>XX    (°C, int16)
  TRIGGER_NOISE_ABOVE = 4, // noise>XX   (dBm, int16 — typically negative)
  TRIGGER_NOISE_BELOW = 5, // noise<XX   (dBm, int16 — typically negative)
  TRIGGER_PERIODIC = 6,    // report:bat / report:temp / report:noise
};

// What is reported when trigger is TRIGGER_PERIODIC
enum ScriptReportVar : uint8_t {
  REPORT_BAT = 0,
  REPORT_TEMP = 1,
  REPORT_NOISE = 2,
};

// ── Channel type ──────────────────────────────────────────────────────────────
enum ChanType : uint8_t {
  CHAN_HASH = 0,    // @hash:name    — public hashtag channel
  CHAN_PRIVATE = 1, // @private:key  — key-protected channel (32 hex chars)
};

// ── Single rule ───────────────────────────────────────────────────────────────
struct ScriptRule {
  bool enabled;
  ScriptTriggerType trigger;
  int16_t trigger_value;      // threshold: mV, °C or dBm
  ScriptReportVar report_var; // only used for TRIGGER_PERIODIC
  uint32_t interval_secs;     // desired interval (>= SCRIPT_MIN_INTERVAL_SECS)
  ChanType chan_type;         // CHAN_HASH or CHAN_PRIVATE
  char chan_name[36];         // channel name (hash, max 32) or key (private, 32 hex chars)
  char message[64];           // template with {value} and {unit} placeholders
  char scope_name[32];        // optional region scope name (empty = use default_scope)
  bool zero_hop;              // if true, send via sendZeroHop (neighbors only, no flood)
  int8_t at_hour;             // -1 = not set, 0-23 = fire at this hour (UTC)
  int8_t at_minute;           // 0-59, only used when at_hour != -1
  uint32_t last_fired;        // RTC timestamp of last send (0 = never)
};

// ── Boot message config ───────────────────────────────────────────────────────
struct BootConfig {
  bool enabled;
  ChanType chan_type;
  bool zero_hop;
  char chan_name[36]; // channel name (hash) or key (private, 32 hex chars)
  char scope_name[32];
  char message[64];
};

// ── Persistent file header (binary — migration only) ──────────────────────────
struct ScriptRulesFile {
  uint8_t version;
  uint8_t count;
  ScriptRule rules[MAX_SCRIPT_RULES];
};

// ── JSON serializer for a single rule ─────────────────────────────────────────
class ScriptRuleSerializer : public ConfigSerializer {
public:
  ScriptRule &rule;
  explicit ScriptRuleSerializer(ScriptRule &r) : rule(r) {}

  void structure() override {
    def("en", rule.enabled);
    def("trig", (int8_t &)rule.trigger);
    def("tval", rule.trigger_value);
    def("rvar", (uint8_t &)rule.report_var);
    def("ivl", rule.interval_secs);
    def("ctype", (uint8_t &)rule.chan_type);
    def("chan", rule.chan_name, sizeof(rule.chan_name));
    def("msg", rule.message, sizeof(rule.message));
    def("sc", rule.scope_name, sizeof(rule.scope_name));
    def("zh", rule.zero_hop);
    def("ath", rule.at_hour);
    def("atm", rule.at_minute);
    // last_fired intentionally omitted — reset on reboot
  }
};

// ── JSON serializer for boot config ──────────────────────────────────────────
class BootConfigSerializer : public ConfigSerializer {
public:
  BootConfig &cfg;
  explicit BootConfigSerializer(BootConfig &c) : cfg(c) {}

  void structure() override {
    def("en", cfg.enabled);
    def("ctype", (uint8_t &)cfg.chan_type);
    def("zh", cfg.zero_hop);
    def("chan", cfg.chan_name, sizeof(cfg.chan_name));
    def("sc", cfg.scope_name, sizeof(cfg.scope_name));
    def("msg", cfg.message, sizeof(cfg.message));
  }
};

// ── JSON serializer for the full rules file ───────────────────────────────────
class ScriptRulesSerializer : public ConfigSerializer {
  ScriptRuleSerializer _rs[MAX_SCRIPT_RULES];
  BootConfigSerializer _bcs;

public:
  uint8_t &count;
  ScriptRule *rules;

  ScriptRulesSerializer(uint8_t &cnt, ScriptRule *r, BootConfig &boot)
      : count(cnt), rules(r), _bcs(boot),
        _rs{ ScriptRuleSerializer(r[0]), ScriptRuleSerializer(r[1]), ScriptRuleSerializer(r[2]),
             ScriptRuleSerializer(r[3]), ScriptRuleSerializer(r[4]), ScriptRuleSerializer(r[5]),
             ScriptRuleSerializer(r[6]), ScriptRuleSerializer(r[7]) } {}

  void structure() override {
    def("cnt", count);
    def("boot", _bcs);

    char _rkey[4]; // "r0".."r7" — reused across calls

    for (uint8_t i = 0; i < MAX_SCRIPT_RULES; i++) {
      snprintf(_rkey, sizeof(_rkey), "r%d", i);
      def(_rkey, _rs[i]);
    }
  }
};