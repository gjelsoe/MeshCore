#include "MeshCompression.h"

#include <ctype.h>
#include <string.h>
#include <unishox2.h>

int mesh_compression_worthwhile(const char *text, int len) {
  if (len < COMPRESS_MIN_LEN) return 0;

  uint8_t seen[256] = { 0 };
  int unique = 0;
  int alpha = 0;
  int effective_len = 0; // count codepoints, not bytes

  for (int i = 0; i < len; i++) {
    uint8_t c = (uint8_t)text[i];
    // Skip UTF-8 continuation bytes (0x80-0xBF)
    if (c >= 0x80 && c <= 0xBF) continue;
    effective_len++;
    if (!seen[c]) {
      seen[c] = 1;
      unique++;
    }
    if (isalpha(c)) alpha++;
  }

  if (effective_len < COMPRESS_MIN_LEN) return 0;

  float entropy = (float)unique / effective_len;
  float alpha_ratio = (float)alpha / effective_len;

  if (entropy > COMPRESS_ENTROPY_MAX) return 0;
  if (alpha_ratio < COMPRESS_ALPHA_RATIO) return 0;

  return 1;
}

int mesh_text_compress(const char *text, int text_len, uint8_t *out_buf, int out_max) {
  if (!mesh_compression_worthwhile(text, text_len)) return -1;

  int comp_len = unishox2_compress_simple(text, text_len, (char *)out_buf);

  if (comp_len <= 0) return -1;

  // Only use compression if it actually saves enough bytes
  if ((text_len - comp_len) < COMPRESS_MIN_SAVINGS) return -1;

  // Ensure compressed output fits in the destination buffer
  if (comp_len > out_max) return -1;

  return comp_len;
}

int mesh_text_decompress(const uint8_t *in_buf, int in_len, char *out_buf, int out_max) {
  int len = unishox2_decompress_simple((const char *)in_buf, in_len, out_buf);

  if (len < 0 || len >= out_max) return -1;

  out_buf[len] = '\0';
  return len;
}
