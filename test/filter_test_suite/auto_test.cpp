#include "mock_mesh.h"
#include "FilterRule.h"
#include "FilterParser.h"
#include "ChannelFilter.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------

static int _pass = 0, _fail = 0;

#define CHECK(desc, expr) do { \
    if (expr) { \
        printf("  PASS  %s\n", desc); \
        _pass++; \
    } else { \
        printf("  FAIL  %s  (line %d)\n", desc, __LINE__); \
        _fail++; \
    } \
} while(0)

static void section(const char* name) {
    printf("\n── %s\n", name);
}

// ---------------------------------------------------------------------------
// Packet builder helpers
// ---------------------------------------------------------------------------

static mesh::Packet makePacket(uint8_t route, uint8_t type,
                               int8_t snr_qdB = 0,
                               uint8_t channel = 0,
                               uint8_t hop_count = 0, uint8_t hash_size = 1,
                               const uint8_t* path_data = nullptr) {
    mesh::Packet p;
    p.setHeader(route, type);
    p._snr = snr_qdB;
    if (type == PAYLOAD_TYPE_GRP_TXT || type == PAYLOAD_TYPE_GRP_DATA) {
        p.payload[0]  = channel;
        p.payload_len = 1;
    }
    if (hop_count > 0 && path_data) {
        p.setPath(path_data, hash_size, hop_count);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_parser() {
    section("Parser — valid commands");

    auto r = parseFilterCommand("add drop payload eq grptxt");
    CHECK("add drop payload eq grptxt -> OK",        r.error == FilterParseError::OK);
    CHECK("  command == ADD",                         r.command == FilterCommand::ADD);
    CHECK("  action  == DROP",                        r.rule.action == FilterAction::DROP);
    CHECK("  field   == TYPE",                        r.rule.field  == FilterField::TYPE);
    CHECK("  op      == EQ",                          r.rule.op     == FilterOp::EQ);
    CHECK("  value   == 0x05 (grptxt)",               r.rule.value  == 0x05);

    r = parseFilterCommand("add allow route eq flood");
    CHECK("add allow route eq flood -> OK",           r.error == FilterParseError::OK);
    CHECK("  action == ALLOW",                        r.rule.action == FilterAction::ALLOW);
    CHECK("  field  == ROUTE",                        r.rule.field  == FilterField::ROUTE);
    CHECK("  value  == 0x01 (flood)",                 r.rule.value  == 0x01);

    r = parseFilterCommand("add drop hops gt 5");
    CHECK("add drop hops gt 5 -> OK",                 r.error == FilterParseError::OK);
    CHECK("  field == HOPS, op == GT, value == 5",
          r.rule.field == FilterField::HOPS && r.rule.op == FilterOp::GT && r.rule.value == 5);

    r = parseFilterCommand("add drop snr lt -10");
    CHECK("add drop snr lt -10 -> OK",                r.error == FilterParseError::OK);
    CHECK("  SNR stored as quarter-dB (-40)",          r.rule.value == -40);

    r = parseFilterCommand("add drop rssi lt -110");
    CHECK("add drop rssi lt -110 -> OK",              r.error == FilterParseError::OK);
    CHECK("  RSSI value == -110",                      r.rule.value == -110);

    r = parseFilterCommand("add drop channel eq 0xAB");
    CHECK("add drop channel eq 0xAB -> OK",           r.error == FilterParseError::OK);
    CHECK("  channel_hash_count == 1",                 r.rule.channel_hash_count == 1);
    CHECK("  channel_hashes[0] == 0xAB",               r.rule.channel_hashes[0] == 0xAB);

    r = parseFilterCommand("add drop channel eq 0x11 0x22 0x33");
    CHECK("channel OR-list 3 values -> OK",            r.error == FilterParseError::OK);
    CHECK("  channel_hash_count == 3",                 r.rule.channel_hash_count == 3);
    CHECK("  channel_hashes[0] == 0x11",               r.rule.channel_hashes[0] == 0x11);
    CHECK("  channel_hashes[1] == 0x22",               r.rule.channel_hashes[1] == 0x22);
    CHECK("  channel_hashes[2] == 0x33",               r.rule.channel_hashes[2] == 0x33);

    r = parseFilterCommand("add drop channel eq 0x11 0x22 0x33 0x44 0x55");
    CHECK("channel OR-list 5 values -> TOO_MANY_CHANNELS", r.error == FilterParseError::TOO_MANY_CHANNELS);

    r = parseFilterCommand("add drop path eq AB 12 CD");
    CHECK("add drop path eq AB 12 CD -> OK",          r.error == FilterParseError::OK);
    CHECK("  path_hash_count == 3",                    r.rule.path_hash_count == 3);
    CHECK("  path_hash_len   == 1",                    r.rule.path_hash_len   == 1);
    CHECK("  hash[0] == 0xAB",                         r.rule.path_hashes[0][0] == 0xAB);
    CHECK("  hash[1] == 0x12",                         r.rule.path_hashes[1][0] == 0x12);
    CHECK("  hash[2] == 0xCD",                         r.rule.path_hashes[2][0] == 0xCD);

    r = parseFilterCommand("del 3");
    CHECK("del 3 -> OK, rule_id == 3",
          r.error == FilterParseError::OK && r.command == FilterCommand::DEL && r.rule_id == 3);

    r = parseFilterCommand("disable 0");
    CHECK("disable 0 -> OK",
          r.error == FilterParseError::OK && r.command == FilterCommand::DISABLE);

    r = parseFilterCommand("enable 7");
    CHECK("enable 7 -> OK",
          r.error == FilterParseError::OK && r.command == FilterCommand::ENABLE);

    r = parseFilterCommand("list");
    CHECK("list -> OK, page 0",
          r.error == FilterParseError::OK && r.command == FilterCommand::LIST && r.rule_id == 0);

    r = parseFilterCommand("list 1");
    CHECK("list 1 -> OK, page 1",
          r.error == FilterParseError::OK && r.rule_id == 1);

    r = parseFilterCommand("clear");
    CHECK("clear -> OK",
          r.error == FilterParseError::OK && r.command == FilterCommand::CLEAR);

    r = parseFilterCommand("mode drop");
    CHECK("mode drop -> OK",
          r.error == FilterParseError::OK && r.mode == FilterMode::DROP);

    r = parseFilterCommand("mode allow");
    CHECK("mode allow -> OK",
          r.error == FilterParseError::OK && r.mode == FilterMode::ALLOW);

    section("Parser — error cases");

    r = parseFilterCommand("add drop payload eq grptxt_TYPO");
    CHECK("unknown value -> UNKNOWN_VALUE",            r.error == FilterParseError::UNKNOWN_VALUE);

    r = parseFilterCommand("add drop BADFIELD eq 5");
    CHECK("unknown field -> UNKNOWN_FIELD",            r.error == FilterParseError::UNKNOWN_FIELD);

    r = parseFilterCommand("add drop payload BADOP grptxt");
    CHECK("unknown op -> UNKNOWN_OP",                  r.error == FilterParseError::UNKNOWN_OP);

    r = parseFilterCommand("add drop payload eq");
    CHECK("missing value -> MISSING_TOKEN",            r.error == FilterParseError::MISSING_TOKEN);

    r = parseFilterCommand("del 99");
    CHECK("del out-of-range -> INVALID_RULE_ID",       r.error == FilterParseError::INVALID_RULE_ID);

    r = parseFilterCommand("BADCMD");
    CHECK("unknown command -> UNKNOWN_COMMAND",        r.error == FilterParseError::UNKNOWN_COMMAND);

    r = parseFilterCommand("add drop path eq AB 12 ZZZZ");
    CHECK("invalid hex in path -> INVALID_HEX",        r.error == FilterParseError::INVALID_HEX);

    r = parseFilterCommand("add drop path eq AB 1234");
    CHECK("mixed hash size -> HASH_SIZE_MISMATCH",     r.error == FilterParseError::HASH_SIZE_MISMATCH);

    r = parseFilterCommand("mode BADMODE");
    CHECK("unknown mode -> UNKNOWN_MODE",              r.error == FilterParseError::UNKNOWN_MODE);
}

// Helper: add a rule and immediately enable it (rules are added disabled by default)
static void addRule(ChannelFilter& f, const char* cmd, MockFS& fs) {
    char reply[160];
    char enable_cmd[16];
    f.handleCommand(cmd, reply, fs);
    // Extract slot id from "OK - rule N added"
    int id = -1;
    sscanf(reply, "OK - rule %d added", &id);
    if (id >= 0) {
        snprintf(enable_cmd, sizeof(enable_cmd), "enable %d", id);
        f.handleCommand(enable_cmd, reply, fs);
    }
}

static void test_evaluate() {
    MockFS fs;
    ChannelFilter f;

    section("Evaluate — default mode allow (no rules)");
    mesh::Packet p = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    CHECK("no rules, mode allow -> pass",  !f.evaluate(&p, -80));

    section("Evaluate — route match");
    char reply[160];
    addRule(f, "add drop route eq tflood", fs);
    mesh::Packet tflood = makePacket(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    mesh::Packet flood  = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_TXT_MSG);
    CHECK("tflood packet -> DROP",    f.evaluate(&tflood, -80));
    CHECK("flood packet  -> PASS",   !f.evaluate(&flood,  -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — payload type match");
    addRule(f, "add drop payload eq grptxt", fs);
    mesh::Packet grptxt = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    mesh::Packet txt    = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    CHECK("grptxt packet -> DROP",    f.evaluate(&grptxt, -80));
    CHECK("txt packet    -> PASS",   !f.evaluate(&txt,    -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — hops gt");
    addRule(f, "add drop hops gt 3", fs);
    uint8_t path4[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t path2[2] = {0x01, 0x02};
    mesh::Packet p4 = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 4, 1, path4);
    mesh::Packet p2 = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 2, 1, path2);
    CHECK("4 hops, rule gt 3 -> DROP",   f.evaluate(&p4, -80));
    CHECK("2 hops, rule gt 3 -> PASS",  !f.evaluate(&p2, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — SNR lt");
    addRule(f, "add drop snr lt -10", fs);
    // SNR -12 dB = -48 quarter-dB; SNR -8 dB = -32 quarter-dB
    mesh::Packet pLowSNR  = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, -48);
    mesh::Packet pHighSNR = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, -32);
    CHECK("SNR -12 dB < -10 -> DROP",    f.evaluate(&pLowSNR,  -80));
    CHECK("SNR  -8 dB > -10 -> PASS",   !f.evaluate(&pHighSNR, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — RSSI lt");
    addRule(f, "add drop rssi lt -100", fs);
    CHECK("RSSI -110 < -100 -> DROP",    f.evaluate(&p, -110));
    CHECK("RSSI  -80 > -100 -> PASS",   !f.evaluate(&p,  -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — channel match");
    addRule(f, "add drop channel eq 0xAB", fs);
    mesh::Packet chanAB = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0xAB);
    mesh::Packet chanCD = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0xCD);
    CHECK("channel 0xAB -> DROP",    f.evaluate(&chanAB, -80));
    CHECK("channel 0xCD -> PASS",   !f.evaluate(&chanCD, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — channel OR-list");
    addRule(f, "add drop channel eq 0xAB 0xCD 0xEF", fs);
    mesh::Packet ch11 = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0x11);
    CHECK("channel 0xAB in OR-list -> DROP",   f.evaluate(&chanAB, -80));
    CHECK("channel 0xCD in OR-list -> DROP",   f.evaluate(&chanCD, -80));
    CHECK("channel 0x11 not in OR-list -> PASS", !f.evaluate(&ch11, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — path OR-match");
    addRule(f, "add drop path eq AB CD", fs);
    uint8_t hopAB[1] = {0xAB};
    uint8_t hopCD[1] = {0xCD};
    uint8_t hopEF[1] = {0xEF};
    mesh::Packet pAB = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 1, 1, hopAB);
    mesh::Packet pCD = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 1, 1, hopCD);
    mesh::Packet pEF = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, 0, 1, 1, hopEF);
    CHECK("last hop 0xAB in list -> DROP",    f.evaluate(&pAB, -80));
    CHECK("last hop 0xCD in list -> DROP",    f.evaluate(&pCD, -80));
    CHECK("last hop 0xEF not in list -> PASS",!f.evaluate(&pEF, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — first-match order");
    addRule(f, "add allow payload eq grptxt", fs);
    addRule(f, "add drop  route  eq flood",   fs);
    mesh::Packet grp  = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    mesh::Packet norm = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    CHECK("grptxt flood: allow rule fires first -> PASS", !f.evaluate(&grp,  -80));
    CHECK("txt   flood: drop  rule fires        -> DROP",  f.evaluate(&norm, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — disabled rule ignored");
    f.handleCommand("add drop payload eq grptxt", reply, fs);  // added disabled — do NOT enable
    mesh::Packet g = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
    CHECK("added rule is disabled by default -> packet passes",  !f.evaluate(&g, -80));
    f.handleCommand("enable 0", reply, fs);
    CHECK("re-enabled rule -> packet drops",  f.evaluate(&g, -80));
    f.handleCommand("disable 0", reply, fs);
    CHECK("disabled again -> packet passes",  !f.evaluate(&g, -80));
    f.handleCommand("clear", reply, fs);

    section("Evaluate — default mode drop");
    f.handleCommand("mode drop", reply, fs);
    mesh::Packet any = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    CHECK("mode drop, no rules -> DROP",    f.evaluate(&any, -80));
    f.handleCommand("mode allow", reply, fs);
    CHECK("mode allow, no rules -> PASS",  !f.evaluate(&any, -80));
}

static void test_persistence() {
    section("Persistence — save and reload");

    MockFS fs;
    ChannelFilter f1;
    char reply[160];

    addRule(f1, "add drop payload eq grptxt", fs);
    addRule(f1, "add drop route eq tflood",   fs);
    f1.handleCommand("disable 0",             reply, fs);
    f1.handleCommand("mode drop",             reply, fs);

    ChannelFilter f2;
    f2.load(fs);

    mesh::Packet grptxt = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_GRP_TXT);
    mesh::Packet tflood = makePacket(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    mesh::Packet norm   = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_TXT_MSG);

    CHECK("rule 0 disabled after reload -> grptxt drops (default policy)", f2.evaluate(&grptxt, -80));
    CHECK("rule 1 active after reload   -> tflood drops",                  f2.evaluate(&tflood, -80));
    CHECK("mode drop after reload       -> norm drops",                    f2.evaluate(&norm,   -80));

    ChannelFilter f3;
    f3.handleCommand("add drop payload eq grptxt", reply, fs);  // added disabled
    CHECK("added disabled, mode allow -> grptxt passes", !f3.evaluate(&grptxt, -80));

    section("Persistence — magic and version validation");

    // Old binary format in .bin file — should be migrated and converted to JSON
    MockFS fs_bin;
    {
        // Write a valid v3 binary file
        uint8_t header[3] = { 0xFC, 0x03, 0x01 };  // magic + version=3 + mode=drop
        FilterRule rules[MAX_FILTER_RULES];
        memset(rules, 0, sizeof(rules));
        rules[0].action  = FilterAction::DROP;
        rules[0].field   = FilterField::TYPE;
        rules[0].op      = FilterOp::EQ;
        rules[0].value   = 5;  // grptxt
        rules[0].and_field = FILTER_FIELD_NONE;
        rules[0].enabled = true;
        rules[0].in_use  = true;
        auto f = fs_bin.open(FILTER_RULES_FILE_BIN, "w");
        f.write(header, 3);
        f.write((uint8_t*)rules, sizeof(rules));
        f.close();
    }
    ChannelFilter f_bin;
    f_bin.load(fs_bin);  // should migrate binary → JSON
    CHECK("binary migrated: grptxt drops",      f_bin.evaluate(&grptxt, -80));
    CHECK("binary migrated: JSON file created", fs_bin.exists(FILTER_RULES_FILE));
    CHECK("binary migrated: .bin file deleted", !fs_bin.exists(FILTER_RULES_FILE_BIN));

    // Corrupt JSON file — load should start fresh
    MockFS fs_bad;
    {
        auto f = fs_bad.open(FILTER_RULES_FILE, "w");
        const char* bad = "NOT JSON AT ALL";
        f.write((const uint8_t*)bad, strlen(bad));
        f.close();
    }
    ChannelFilter f_bad;
    f_bad.load(fs_bad);
    CHECK("corrupt JSON -> mode defaults to allow", !f_bad.evaluate(&norm, -80));

    // Verify save writes valid JSON
    MockFS fs_save;
    ChannelFilter f_save;
    addRule(f_save, "add drop payload eq grptxt", fs_save);
    f_save.handleCommand("mode drop", reply, fs_save, nullptr);
    f_save.save(fs_save);
    {
        std::string json = fs_save.content(FILTER_RULES_FILE);
        CHECK("saved file is non-empty JSON",    json.size() > 2);
        CHECK("saved JSON contains mode key",    json.find("mode") != std::string::npos);
        CHECK("saved JSON contains rule key",    json.find("ra") != std::string::npos);

        ChannelFilter f_rt;
        f_rt.load(fs_save);
        CHECK("round-trip: grptxt drops",  f_rt.evaluate(&grptxt, -80));
        CHECK("round-trip: mode drop",     f_rt.evaluate(&norm,   -80));
    }

    section("Persistence — JSON round-trip extended");

    // Path hash OR-list round-trip
    MockFS fs_path;
    ChannelFilter fp;
    addRule(fp, "add drop path eq AABBCC 112233", fs_path);
    fp.save(fs_path);
    {
        ChannelFilter fp2;
        fp2.load(fs_path);
        uint8_t h1[3]={0xAA,0xBB,0xCC}, h2[3]={0x11,0x22,0x33}, h3[3]={0xFF,0xFF,0xFF};
        mesh::Packet p1={}, p2={}, p3={};
        p1.setHeader(ROUTE_TYPE_FLOOD,PAYLOAD_TYPE_TXT_MSG); p1.setPath(h1,3,1);
        p2.setHeader(ROUTE_TYPE_FLOOD,PAYLOAD_TYPE_TXT_MSG); p2.setPath(h2,3,1);
        p3.setHeader(ROUTE_TYPE_FLOOD,PAYLOAD_TYPE_TXT_MSG); p3.setPath(h3,3,1);
        CHECK("path round-trip: AABBCC -> DROP",  fp2.evaluate(&p1,-80));
        CHECK("path round-trip: 112233 -> DROP",  fp2.evaluate(&p2,-80));
        CHECK("path round-trip: FFFFFF -> PASS", !fp2.evaluate(&p3,-80));
    }

    // Channel OR-list round-trip
    MockFS fs_ch;
    ChannelFilter fc;
    addRule(fc, "add drop channel eq 0x11 0x22 0x33", fs_ch);
    fc.save(fs_ch);
    {
        ChannelFilter fc2;
        fc2.load(fs_ch);
        mesh::Packet pc1={}, pc2={}, pc3={};
        pc1.setHeader(ROUTE_TYPE_FLOOD,PAYLOAD_TYPE_GRP_TXT); pc1.payload[0]=0x11; pc1.payload_len=1;
        pc2.setHeader(ROUTE_TYPE_FLOOD,PAYLOAD_TYPE_GRP_TXT); pc2.payload[0]=0x22; pc2.payload_len=1;
        pc3.setHeader(ROUTE_TYPE_FLOOD,PAYLOAD_TYPE_GRP_TXT); pc3.payload[0]=0xFF; pc3.payload_len=1;
        CHECK("channel round-trip: 0x11 -> DROP",   fc2.evaluate(&pc1,-80));
        CHECK("channel round-trip: 0x22 -> DROP",   fc2.evaluate(&pc2,-80));
        CHECK("channel round-trip: 0xFF -> PASS",  !fc2.evaluate(&pc3,-80));
    }

    // AND condition round-trip
    MockFS fs_and;
    ChannelFilter fa;
    addRule(fa, "add drop snr lt -10 and rssi lt -100", fs_and);
    fa.save(fs_and);
    {
        ChannelFilter fa2;
        fa2.load(fs_and);
        mesh::Packet pa={};
        pa.setHeader(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG);
        pa._snr = -48;  // -12 dB in quarter-dB
        CHECK("AND round-trip: both conditions -> DROP",  fa2.evaluate(&pa,-110));
        CHECK("AND round-trip: only SNR matches -> PASS", !fa2.evaluate(&pa,-80));
    }

    // Disabled rule round-trip
    MockFS fs_dis;
    ChannelFilter fd;
    fd.handleCommand("add drop payload eq grptxt", reply, fs_dis, nullptr);  // added disabled
    fd.save(fs_dis);
    {
        ChannelFilter fd2;
        fd2.load(fs_dis);
        mesh::Packet pg={};
        pg.setHeader(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT);
        CHECK("disabled rule round-trip -> PASS", !fd2.evaluate(&pg,-80));
    }
}

static void test_and_condition() {
    MockFS fs;
    ChannelFilter f;
    char reply[160];

    section("Parser — AND condition");

    auto r = parseFilterCommand("add drop channel eq 0x11 and hops gt 8");
    CHECK("AND parse -> OK",                           r.error == FilterParseError::OK);
    CHECK("  primary field  == CHANNEL",               r.rule.field    == FilterField::CHANNEL);
    CHECK("  channel_hashes[0] == 0x11",               r.rule.channel_hashes[0] == 0x11);
    CHECK("  and_field      == HOPS",                  r.rule.and_field == (uint8_t)FilterField::HOPS);
    CHECK("  and_op         == GT",                    r.rule.and_op   == FilterOp::GT);
    CHECK("  and_value      == 8",                     r.rule.and_value == 8);

    r = parseFilterCommand("add drop snr lt -10 and rssi lt -100");
    CHECK("AND snr+rssi -> OK",                        r.error == FilterParseError::OK);
    CHECK("  and_field == RSSI",                       r.rule.and_field == (uint8_t)FilterField::RSSI);
    CHECK("  and_value == -100",                       r.rule.and_value == -100);

    r = parseFilterCommand("add drop path eq AB and hops gt 3");
    CHECK("AND path+hops -> OK",                       r.error == FilterParseError::OK);
    CHECK("  path primary intact",                     r.rule.path_hash_count == 1);
    CHECK("  and_field == HOPS",                       r.rule.and_field == (uint8_t)FilterField::HOPS);

    r = parseFilterCommand("add drop channel eq 0x11 and path eq AB");
    CHECK("AND with path -> AND_PATH_NOT_ALLOWED",     r.error == FilterParseError::AND_PATH_NOT_ALLOWED);

    r = parseFilterCommand("add drop channel eq 0x11 and channel eq 0x22");
    CHECK("AND duplicate field -> AND_DUPLICATE_FIELD",r.error == FilterParseError::AND_DUPLICATE_FIELD);

    section("Evaluate — AND condition");

    // Rule: drop channel eq 0x11 and hops gt 3
    addRule(f, "add drop channel eq 0x11 and hops gt 3", fs);

    uint8_t path4[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t path2[2] = {0x01, 0x02};

    // Both conditions true -> DROP
    mesh::Packet p_both = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0x11, 4, 1, path4);
    CHECK("channel 0x11 AND hops=4 > 3 -> DROP",       f.evaluate(&p_both, -80));

    // Channel matches, hops does not -> PASS
    mesh::Packet p_ch_only = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0x11, 2, 1, path2);
    CHECK("channel 0x11 AND hops=2 not > 3 -> PASS",  !f.evaluate(&p_ch_only, -80));

    // Hops match, channel does not -> PASS
    mesh::Packet p_hop_only = makePacket(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, 0x22, 4, 1, path4);
    CHECK("channel 0x22 AND hops=4, ch mismatch -> PASS", !f.evaluate(&p_hop_only, -80));

    f.handleCommand("clear", reply, fs);

    section("List — AND condition display");

    f.handleCommand("add drop channel eq 0x11 and hops gt 8", reply, fs, nullptr);
    f.handleCommand("list", reply, fs, (ClientInfo*)1);  // remote = paginated
    CHECK("list shows 'and' keyword",   strstr(reply, "and") != nullptr);
    CHECK("list shows 'hops'",          strstr(reply, "hops") != nullptr);
    CHECK("list shows '8'",             strstr(reply, "8")    != nullptr);
    CHECK("list within 138 chars",      strlen(reply) <= 138);
    f.handleCommand("clear", reply, fs, nullptr);
}

static void test_list_length() {
    section("List output — page length within 138 chars");

    MockFS fs;
    ChannelFilter f;
    char reply[160];

    // Fill with worst-case rules
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);
    f.handleCommand("add drop payload neq grpdata", reply, fs);

    for (int page = 0; ; page++) {
        char pr[16];
        snprintf(pr, sizeof(pr), "list %d", page);
        f.handleCommand(pr, reply, fs);
        int len = strlen(reply);
        char desc[64];
        snprintf(desc, sizeof(desc), "page %d length %d <= 138", page, len);
        CHECK(desc, len <= 138);
        // Stop when no more pages hinted
        if (strstr(reply, "-> filter list") == nullptr) break;
        if (page > 10) break;  // safety
    }
}

static void test_profiles() {
    MockFS fs;
    char reply[160];

    section("Parser — profile prefix");

    auto r = parseFilterCommand("1 add drop payload eq grptxt");
    CHECK("'1 add ...' -> target_profile == 1",  r.error == FilterParseError::OK && r.target_profile == 1);

    r = parseFilterCommand("2 add drop route eq tflood");
    CHECK("'2 add ...' -> target_profile == 2",  r.error == FilterParseError::OK && r.target_profile == 2);

    r = parseFilterCommand("add drop payload eq grptxt");
    CHECK("'add ...' -> target_profile == 0 (active)", r.error == FilterParseError::OK && r.target_profile == 0);

    r = parseFilterCommand("set profile 1");
    CHECK("'set profile 1' -> SET_PROFILE, profile=1",
          r.error == FilterParseError::OK && r.command == FilterCommand::SET_PROFILE && r.target_profile == 1);

    r = parseFilterCommand("set profile 2");
    CHECK("'set profile 2' -> SET_PROFILE, profile=2",
          r.error == FilterParseError::OK && r.command == FilterCommand::SET_PROFILE && r.target_profile == 2);

    r = parseFilterCommand("set profile 3");
    CHECK("'set profile 3' -> INVALID_PROFILE",  r.error == FilterParseError::INVALID_PROFILE);

    r = parseFilterCommand("profile");
    CHECK("'profile' -> GET_PROFILE",             r.error == FilterParseError::OK && r.command == FilterCommand::GET_PROFILE);

    r = parseFilterCommand("1 list");
    CHECK("'1 list' -> LIST, target_profile=1",   r.error == FilterParseError::OK && r.target_profile == 1);

    section("Profiles — independent rule sets");

    ChannelFilter f;

    // Add to profile 1 (active by default)
    addRule(f, "add drop payload eq grptxt", fs);

    // Add to profile 2
    f.handleCommand("2 add drop route eq tflood", reply, fs, nullptr);
    f.handleCommand("2 enable 0", reply, fs, nullptr);

    mesh::Packet grptxt = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_GRP_TXT);
    mesh::Packet tflood = makePacket(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG);
    mesh::Packet norm   = makePacket(ROUTE_TYPE_FLOOD,           PAYLOAD_TYPE_TXT_MSG);

    // Active = profile 1 — grptxt drops, tflood passes
    CHECK("profile 1 active: grptxt -> DROP",    f.evaluate(&grptxt, -80));
    CHECK("profile 1 active: tflood -> PASS",   !f.evaluate(&tflood, -80));

    // Switch to profile 2 — tflood drops, grptxt passes
    f.handleCommand("set profile 2", reply, fs, nullptr);
    CHECK("profile 2 active: tflood -> DROP",    f.evaluate(&tflood, -80));
    CHECK("profile 2 active: grptxt -> PASS",   !f.evaluate(&grptxt, -80));

    // Switch back to profile 1
    f.handleCommand("set profile 1", reply, fs, nullptr);
    CHECK("back to profile 1: grptxt -> DROP",   f.evaluate(&grptxt, -80));

    section("Profiles — mode per profile");

    ChannelFilter fm;
    fm.handleCommand("1 mode drop", reply, fs, nullptr);
    fm.handleCommand("2 mode allow", reply, fs, nullptr);

    // Profile 1: mode drop, no rules -> everything drops
    CHECK("profile 1 mode drop, no rules -> norm drops",   fm.evaluate(&norm, -80));

    // Switch to profile 2: mode allow, no rules -> everything passes
    fm.handleCommand("set profile 2", reply, fs, nullptr);
    CHECK("profile 2 mode allow, no rules -> norm passes", !fm.evaluate(&norm, -80));

    section("Profiles — persistence round-trip");

    MockFS fs2;
    ChannelFilter fs_f;
    addRule(fs_f, "add drop payload eq grptxt", fs2);          // profile 1
    fs_f.handleCommand("2 add drop route eq tflood", reply, fs2, nullptr);
    fs_f.handleCommand("2 enable 0", reply, fs2, nullptr);
    fs_f.handleCommand("set profile 2", reply, fs2, nullptr);  // switch active to 2
    fs_f.save(fs2);

    ChannelFilter fs_f2;
    fs_f2.load(fs2);

    // After reload: active = profile 2
    CHECK("reload: active profile 2 -> tflood drops",   fs_f2.evaluate(&tflood, -80));
    CHECK("reload: active profile 2 -> grptxt passes", !fs_f2.evaluate(&grptxt, -80));

    // Switch to profile 1 and verify
    fs_f2.handleCommand("set profile 1", reply, fs2, nullptr);
    CHECK("reload: switch to profile 1 -> grptxt drops", fs_f2.evaluate(&grptxt, -80));
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main() {
    printf("═══════════════════════════════════════\n");
    printf(" MeshCore Filter Engine — Test Suite\n");
    printf("═══════════════════════════════════════\n");

    test_parser();
    test_evaluate();
    test_persistence();
    test_and_condition();
    test_profiles();
    test_list_length();

    printf("\n═══════════════════════════════════════\n");
    printf(" Results: %d passed, %d failed\n", _pass, _fail);
    printf("═══════════════════════════════════════\n");

    return (_fail == 0) ? 0 : 1;
}
