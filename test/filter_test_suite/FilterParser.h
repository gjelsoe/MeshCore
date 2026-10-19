#pragma once

#include "FilterRule.h"

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

enum class FilterParseError : uint8_t {
    OK = 0,
    UNKNOWN_COMMAND,         // unrecognised sub-command after "filter"
    UNKNOWN_ACTION,          // unrecognised action token (expected drop/allow)
    UNKNOWN_FIELD,           // unrecognised field token
    UNKNOWN_OP,              // unrecognised operator token
    UNKNOWN_VALUE,           // unrecognised or out-of-range value token
    MISSING_TOKEN,           // expected another token but input ended
    INVALID_RULE_ID,         // rule id out of range or not a number
    INVALID_HEX,             // malformed hex string
    TOO_MANY_HASHES,         // more path hashes than MAX_PATH_HASHES_PER_RULE
    HASH_SIZE_MISMATCH,      // mixed hash sizes in a single path rule
    UNKNOWN_MODE,            // unrecognised mode token (expected allow/drop)
    AND_PATH_NOT_ALLOWED,    // path field not supported as AND condition
    AND_DUPLICATE_FIELD,     // AND condition uses same field as primary condition
    AND_CHANNEL_NOT_ALLOWED, // channel field not supported as AND condition
    TOO_MANY_CHANNELS,       // more channel hashes than MAX_CHANNEL_HASHES
    INVALID_PROFILE,         // profile number out of range (must be 1 or 2)
};

// ---------------------------------------------------------------------------
// Command types returned by the parser
// ---------------------------------------------------------------------------

enum class FilterCommand : uint8_t {
    ADD,         // add a new rule      — result.rule is populated
    DEL,         // delete by id        — result.rule_id is populated
    LIST,        // list rules          — result.target_profile selects which
    DISABLE,     // disable by id       — result.rule_id is populated
    ENABLE,      // enable by id        — result.rule_id is populated
    CLEAR,       // clear all rules
    MODE,        // set default policy  — result.mode is populated
    SET_PROFILE, // switch active profile — result.target_profile is populated
    GET_PROFILE, // show active profile number
};

// ---------------------------------------------------------------------------
// Parse result
// ---------------------------------------------------------------------------

struct FilterParseResult {
    FilterParseError error;
    FilterCommand    command;
    FilterRule       rule;           // valid when command == ADD and error == OK
    uint8_t          rule_id;        // valid when command == DEL / DISABLE / ENABLE
    FilterMode       mode;           // valid when command == MODE
    // target_profile: 0 = active profile, 1 = profile 1, 2 = profile 2
    uint8_t          target_profile; // valid for all commands
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Parse a full "filter ..." command string.
// 'input' must be a null-terminated C string starting after "filter ".
FilterParseResult parseFilterCommand(const char* input);

// Return a short human-readable description of a parse error.
const char* filterParseErrorStr(FilterParseError err);
