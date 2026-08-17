#pragma once

// NOTE: This header relies on FILESYSTEM and ConfigSerializer being defined before inclusion.
// MyMesh.h includes the platform-specific filesystem headers before including this file.

#include "FilterRule.h"
#include "FilterParser.h"
#include <MeshCore.h>
#include <helpers/ConfigSerializer.h>

// JSON persistence file path
#define FILTER_RULES_FILE      "/filter_rules.json"

// Legacy binary file path — read for migration, then deleted
#define FILTER_RULES_FILE_BIN  "/filter_rules.bin"

// Legacy binary format magic and minimum version for migration
#define FILTER_FILE_MAGIC      0xFC
#define FILTER_FILE_VERSION    3

// ---------------------------------------------------------------------------
// FilterRuleSerializer — serializes a single FilterRule as a JSON sub-object
// ---------------------------------------------------------------------------

class FilterRuleSerializer : public ConfigSerializer {
    FilterRule* _rule;
protected:
    void structure() override {
        def("act",    (uint8_t&)_rule->action);
        def("fld",    (uint8_t&)_rule->field);
        def("op",     (uint8_t&)_rule->op);
        def("val",    _rule->value);
        def("af",     _rule->and_field);
        def("aop",    (uint8_t&)_rule->and_op);
        def("av",     _rule->and_value);
        // channel hashes as flat hex blob (MAX_CHANNEL_HASHES bytes)
        def("ch",     _rule->channel_hashes, MAX_CHANNEL_HASHES);
        def("ch_n",   _rule->channel_hash_count);
        // path hashes as flat hex blob (MAX_PATH_HASHES_PER_RULE * MAX_PATH_HASH_SIZE bytes)
        def("path",   _rule->path_hashes, MAX_PATH_HASHES_PER_RULE * MAX_PATH_HASH_SIZE);
        def("pl",     _rule->path_hash_len);
        def("pn",     _rule->path_hash_count);
        def("en",     _rule->enabled);
        def("use",    _rule->in_use);
    }
public:
    FilterRuleSerializer() : _rule(nullptr) {}
    void setRule(FilterRule* r) { _rule = r; }
};

// ---------------------------------------------------------------------------
// ChannelFilter
// ---------------------------------------------------------------------------

class ChannelFilter : public ConfigSerializer {
    FilterRule           _rules[MAX_FILTER_RULES];
    FilterMode           _mode;
    FilterRuleSerializer _rule_ser[MAX_FILTER_RULES];

protected:
    void structure() override {
        def("mode", (uint8_t&)_mode);
        char key[4];
        for (int i = 0; i < MAX_FILTER_RULES; i++) {
            key[0] = 'r'; key[1] = 'a' + i; key[2] = '\0';  // ra, rb, rc ... rh
            def(key, _rule_ser[i]);
        }
    }

public:
    ChannelFilter();

    // Non-copyable: _rule_ser[] contains pointers into _rules[].
    // A copy would leave those pointers dangling into the source object.
    ChannelFilter(const ChannelFilter&)            = delete;
    ChannelFilter& operator=(const ChannelFilter&) = delete;

    // --- Lifecycle ----------------------------------------------------------

    // Load rules from JSON. Falls back to legacy binary if JSON not found.
    void load(FILESYSTEM& fs);

    // Save rules to JSON.
    void save(FILESYSTEM& fs) const;

    // --- Evaluation ---------------------------------------------------------

    // Evaluate all active rules against a received packet.
    // 'rssi' is passed separately as it lives in the radio driver, not in Packet.
    // Returns true if the packet should be DROPPED, false if it should pass.
    bool evaluate(const mesh::Packet* pkt, int16_t rssi) const;

    // --- CLI dispatch -------------------------------------------------------

    // Handle a "filter ..." command string (everything after "filter ").
    // Writes a human-readable result into 'reply' (assumed >= 80 bytes).
    // sender == nullptr means local Serial CLI; sender != nullptr means remote CLI (paginates output).
    void handleCommand(const char* args, char* reply, FILESYSTEM& fs, ClientInfo* sender = nullptr);

private:
    // --- Rule helpers -------------------------------------------------------

    int  _firstFreeSlot() const;
    bool _ruleMatches(const FilterRule& rule, const mesh::Packet* pkt, int16_t rssi) const;
    bool _evalScalar(FilterField field, FilterOp op, int16_t val,
                     const mesh::Packet* pkt, int16_t rssi) const;

    // --- list command -------------------------------------------------------
    void _listRules(char* reply, uint8_t page, bool is_remote) const;

    // --- legacy binary migration --------------------------------------------
    bool _loadBinary(FILESYSTEM& fs);
};
