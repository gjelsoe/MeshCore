#pragma once

#include <stdint.h>
#include <string.h>

class DisplayDriver {
  int _w, _h;

protected:
  DisplayDriver(int w, int h) {
    _w = w;
    _h = h;
  }

public:
  enum Color {
    DARK = 0,
    LIGHT,
    RED,
    GREEN,
    BLUE,
    YELLOW,
    ORANGE
  }; // on b/w screen, colors will be !=0 synonym of light

  int width() const { return _w; }
  int height() const { return _h; }

  virtual bool isOn() = 0;
  virtual void turnOn() = 0;
  virtual void turnOff() = 0;
  virtual void clear() = 0;
  virtual void startFrame(Color bkg = DARK) = 0;
  virtual void setTextSize(int sz) = 0;
  virtual void setColor(Color c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char *str) = 0;
  virtual void printWordWrap(const char *str, int max_width) {
    print(str);
  } // fallback to basic print() if no override
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;
  virtual void drawXbm(int x, int y, const uint8_t *bits, int w, int h) = 0;
  virtual uint16_t getTextWidth(const char *str) = 0;
  virtual void drawTextCentered(int mid_x, int y, const char *str) { // helper method (override to optimise)
    int w = getTextWidth(str);
    setCursor(mid_x - w / 2, y);
    print(str);
  }
  virtual void drawTextRightAlign(int x_anch, int y, const char *str) {
    int w = getTextWidth(str);
    setCursor(x_anch - w, y);
    print(str);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char *str) {
    setCursor(x_anch, y);
    print(str);
  }

  // convert UTF-8 characters to displayable block characters for compatibility
  // virtual void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
  //   size_t j = 0;
  //   for (size_t i = 0; src[i] != 0 && j < dest_size - 1; i++) {
  //     unsigned char c = (unsigned char)src[i];
  //     if (c >= 32 && c <= 126) {
  //       dest[j++] = c;  // ASCII printable
  //     } else if (c >= 0x80) {
  //       dest[j++] = '\xDB';  // CP437 full block █
  //       while (src[i+1] && (src[i+1] & 0xC0) == 0x80)
  //         i++;  // skip UTF-8 continuation bytes
  //     }
  //   }
  //   dest[j] = 0;
  // }

  // convert UTF-8 characters to displayable characters
  virtual void translateUTF8ToBlocks(char *dest, const char *src, size_t dest_size) {
    // NEW: Proper UTF-8 to CP437 conversion for æ, ø, å and other characters
    size_t j = 0;

    for (size_t i = 0; src[i] != 0 && j < dest_size - 1; i++) {
      unsigned char c = (unsigned char)src[i];

#ifdef USE_CP437
      // NEW: Proper UTF-8 to CP437 conversion for æ, ø, å and other characters
      // Handle ASCII printable characters (32-126)
      if (c >= 32 && c <= 126) {
        dest[j++] = c;
        continue;
      }

      // Handle UTF-8 multi-byte sequences
      if (c >= 0x80) {
        uint32_t codepoint = 0;
        int bytes = 0;

        // Determine UTF-8 sequence length and decode
        if ((c & 0xE0) == 0xC0) {
          // 2-byte sequence
          codepoint = c & 0x1F;
          bytes = 1;
        } else if ((c & 0xF0) == 0xE0) {
          // 3-byte sequence
          codepoint = c & 0x0F;
          bytes = 2;
        } else if ((c & 0xF8) == 0xF0) {
          // 4-byte sequence
          codepoint = c & 0x07;
          bytes = 3;
        } else {
          // Invalid UTF-8 start byte, skip it
          continue;
        }

        // Read continuation bytes
        bool valid = true;
        for (int b = 0; b < bytes; b++) {
          if (src[i + 1] && (src[i + 1] & 0xC0) == 0x80) {
            i++;
            codepoint = (codepoint << 6) | (src[i] & 0x3F);
          } else {
            valid = false;
            break;
          }
        }

        if (!valid) {
          continue;
        }

        // Map Unicode codepoint to CP437 character code
        unsigned char cp437_char = 0;

        // CP437 mapping table - optimized for messaging/chat use
        switch (codepoint) {
        // === SCANDINAVIAN (Priority 1) ===
        case 0x00E6:
          cp437_char = 0x91;
          break; // æ
        case 0x00C6:
          cp437_char = 0x92;
          break; // Æ
        case 0x00F8:
          cp437_char = 0x9B;
          break; // ø
        case 0x00D8:
          cp437_char = 0x9D;
          break; // Ø
        case 0x00E5:
          cp437_char = 0x86;
          break; // å
        case 0x00C5:
          cp437_char = 0x8F;
          break; // Å

        // === GERMAN ===
        case 0x00E4:
          cp437_char = 0x84;
          break; // ä
        case 0x00F6:
          cp437_char = 0x94;
          break; // ö
        case 0x00FC:
          cp437_char = 0x81;
          break; // ü
        case 0x00C4:
          cp437_char = 0x8E;
          break; // Ä
        case 0x00D6:
          cp437_char = 0x99;
          break; // Ö
        case 0x00DC:
          cp437_char = 0x9A;
          break; // Ü
        case 0x00DF:
          cp437_char = 0xE1;
          break; // ß

        // === FRENCH ===
        case 0x00E0:
          cp437_char = 0x85;
          break; // à
        case 0x00E2:
          cp437_char = 0x83;
          break; // â
        case 0x00E7:
          cp437_char = 0x87;
          break; // ç
        case 0x00E8:
          cp437_char = 0x8A;
          break; // è
        case 0x00E9:
          cp437_char = 0x82;
          break; // é
        case 0x00EA:
          cp437_char = 0x88;
          break; // ê
        case 0x00EB:
          cp437_char = 0x89;
          break; // ë
        case 0x00EE:
          cp437_char = 0x8C;
          break; // î
        case 0x00EF:
          cp437_char = 0x8B;
          break; // ï
        case 0x00F4:
          cp437_char = 0x93;
          break; // ô
        case 0x00F9:
          cp437_char = 0x97;
          break; // ù
        case 0x00FB:
          cp437_char = 0x96;
          break; // û

        // === SPANISH ===
        case 0x00F1:
          cp437_char = 0xA4;
          break; // ñ
        case 0x00D1:
          cp437_char = 0xA5;
          break; // Ñ
        case 0x00E1:
          cp437_char = 0xA0;
          break; // á
        case 0x00ED:
          cp437_char = 0xA1;
          break; // í
        case 0x00F3:
          cp437_char = 0xA2;
          break; // ó
        case 0x00FA:
          cp437_char = 0xA3;
          break; // ú
        case 0x00BF:
          cp437_char = 0xA8;
          break; // ¿
        case 0x00A1:
          cp437_char = 0xAD;
          break; // ¡

        // === CURRENCY (very common in messages) ===
        case 0x20AC:
          cp437_char = 0xEE;
          break; // €
        case 0x00A3:
          cp437_char = 0x9C;
          break; // £
        case 0x00A5:
          cp437_char = 0x9D;
          break; // ¥
        case 0x00A2:
          cp437_char = 0x9B;
          break; // ¢

        // === COMMON SYMBOLS ===
        case 0x00B0:
          cp437_char = 0xF8;
          break; // ° (degrees - "20°C")
        case 0x00B1:
          cp437_char = 0xF1;
          break; // ±
        case 0x00B5:
          cp437_char = 0xE6;
          break; // µ (micro)
        case 0x00F7:
          cp437_char = 0xF6;
          break; // ÷
        case 0x00D7:
          cp437_char = 0x9E;
          break; // ×
        case 0x00BD:
          cp437_char = 0xAB;
          break; // ½
        case 0x00BC:
          cp437_char = 0xAC;
          break; // ¼
        case 0x00BE:
          cp437_char = 0xF3;
          break; // ¾
        case 0x00B7:
          cp437_char = 0xFA;
          break; // · (middle dot/bullet)
        case 0x00A7:
          cp437_char = 0x15;
          break; // § (section)

        // === QUOTES & APOSTROPHES (copied from websites) ===
        case 0x2018:
          cp437_char = '\'';
          break; // ' (left single quote)
        case 0x2019:
          cp437_char = '\'';
          break; // ' (right single quote)
        case 0x201C:
          cp437_char = '"';
          break; // " (left double quote)
        case 0x201D:
          cp437_char = '"';
          break; // " (right double quote)

        // === DASHES ===
        case 0x2013:
          cp437_char = '-';
          break; // – (en dash)
        case 0x2014:
          cp437_char = '-';
          break; // — (em dash)

        // === BULLETS & DOTS ===
        case 0x2022:
          cp437_char = 0x07;
          break; // • (bullet)
        case 0x2026:
          cp437_char = '.';
          break; // … (ellipsis → .)

        // === ARROWS (directions/instructions) ===
        case 0x2190:
          cp437_char = 0x1B;
          break; // ←
        case 0x2191:
          cp437_char = 0x18;
          break; // ↑
        case 0x2192:
          cp437_char = 0x1A;
          break; // →
        case 0x2193:
          cp437_char = 0x19;
          break; // ↓

        // === MATH SYMBOLS ===
        case 0x221A:
          cp437_char = 0xFB;
          break; // √
        case 0x221E:
          cp437_char = 0xEC;
          break; // ∞
        case 0x2248:
          cp437_char = 0xF7;
          break; // ≈
        case 0x2264:
          cp437_char = 0xF3;
          break; // ≤
        case 0x2265:
          cp437_char = 0xF2;
          break; // ≥
        case 0x00B2:
          cp437_char = 0xFD;
          break; // ²
        case 0x00B3:
          cp437_char = 0xFC;
          break; // ³

        // === SIMPLE EMOJI FALLBACKS ===
        case 0x2764:
          cp437_char = 0x03;
          break; // ❤ → ♥
        case 0x263A:
          cp437_char = 0x01;
          break; // ☺
        case 0x2660:
          cp437_char = 0x06;
          break; // ♠
        case 0x2663:
          cp437_char = 0x05;
          break; // ♣
        case 0x2665:
          cp437_char = 0x03;
          break; // ♥
        case 0x2666:
          cp437_char = 0x04;
          break; // ♦
        case 0x266A:
          cp437_char = 0x0D;
          break; // ♪ (music note)
        case 0x263C:
          cp437_char = 0x0F;
          break; // ☼ (sun)

        // === CHECKMARKS ===
        case 0x2713:
          cp437_char = 0xFB;
          break; // ✓ → √
        case 0x2714:
          cp437_char = 0xFB;
          break; // ✔ → √
        case 0x2717:
          cp437_char = 'x';
          break; // ✗ → x
        case 0x2718:
          cp437_char = 'x';
          break; // ✘ → x

        // === SPACES (normalize) ===
        case 0x00A0:
          cp437_char = ' ';
          break; // Non-breaking space
        case 0x2003:
          cp437_char = ' ';
          break; // Em space
        case 0x200B:
          cp437_char = 0;
          break; // Zero-width space (skip)

        // === DEFAULT ===
        default:
          // Try direct mapping for Latin-1
          if (codepoint >= 0x00A0 && codepoint <= 0x00FF) {
            cp437_char = (unsigned char)codepoint;
          } else {
            // Unknown: use discrete placeholder
            cp437_char = 0xF9; // · (middle dot - less intrusive than █)
          }
          break;
        }

        if (cp437_char != 0 && j < dest_size - 1) {
          dest[j++] = cp437_char;
        }
      }
    }
    dest[j] = 0; // Null terminate

#else
      // OLD: Simple block character replacement (original behavior)
      if (c >= 32 && c <= 126) {
        dest[j++] = c; // ASCII printable
      } else if (c >= 0x80) {
        dest[j++] = '\xDB'; // CP437 full block █
        while (src[i + 1] && (src[i + 1] & 0xC0) == 0x80)
          i++; // skip UTF-8 continuation bytes
      }
    }
    dest[j] = 0;
#endif
  }

  // draw text with ellipsis if it exceeds max_width
  virtual void drawTextEllipsized(int x, int y, int max_width, const char *str) {
    char temp_str[256]; // reasonable buffer size
    size_t len = strlen(str);
    if (len >= sizeof(temp_str)) len = sizeof(temp_str) - 1;
    memcpy(temp_str, str, len);
    temp_str[len] = 0;

    if (getTextWidth(temp_str) <= max_width) {
      setCursor(x, y);
      print(temp_str);
      return;
    }

    // for variable-width fonts (GxEPD), add space after ellipsis
    // for fixed-width fonts (OLED), keep tight spacing to save precious characters
    const char *ellipsis;
    // use a simple heuristic: if 'i' and 'l' have different widths, it's variable-width
    int i_width = getTextWidth("i");
    int l_width = getTextWidth("l");
    if (i_width != l_width) {
      ellipsis = "... "; // variable-width fonts: add space
    } else {
      ellipsis = "..."; // fixed-width fonts: no space
    }

    int ellipsis_width = getTextWidth(ellipsis);
    int str_len = strlen(temp_str);

    while (str_len > 0 && getTextWidth(temp_str) > max_width - ellipsis_width) {
      temp_str[--str_len] = 0;
    }
    strcat(temp_str, ellipsis);

    setCursor(x, y);
    print(temp_str);
  }

  virtual void endFrame() = 0;
};
