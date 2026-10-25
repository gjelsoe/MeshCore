#pragma once
// Minimal mesh::Utils stub for Linux test build
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace mesh {
class Utils {
public:
    static void printHex(Stream& s, const uint8_t* data, size_t len) {
        char tmp[3];
        for (size_t i = 0; i < len; i++) {
            snprintf(tmp, sizeof(tmp), "%02X", data[i]);
            s.print(tmp);
        }
    }
    static size_t fromHex(uint8_t* dest, size_t max_len, const char* hex) {
        size_t len = strlen(hex) / 2;
        if (len > max_len) len = max_len;
        for (size_t i = 0; i < len; i++) {
            char tmp[3] = {hex[i*2], hex[i*2+1], 0};
            dest[i] = (uint8_t)strtol(tmp, nullptr, 16);
        }
        return len;
    }
};
}
