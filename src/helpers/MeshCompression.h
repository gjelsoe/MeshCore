#pragma once

#include <MeshCore.h>
#include <stdint.h>

// Minimum text length before attempting compression.
// Below this threshold, the overhead is not worth it.
#define COMPRESS_MIN_LEN          40

// Minimum bytes saved for compression to be used.
// Ensures the flag byte overhead is always offset by the savings.
#define COMPRESS_MIN_SAVINGS      4

// Maximum ratio of unique characters to total length.
// High entropy content (e.g. base64, hex strings) compresses poorly.
#define COMPRESS_ENTROPY_MAX      0.75f

// Minimum ratio of alphabetic characters to total length.
// Unishox2 is optimised for natural language text.
#define COMPRESS_ALPHA_RATIO      0.5f

// Flag bit set in payload byte[4] to indicate compressed content.
// Uses bit 7, which is unused by the existing attempt/TXT_TYPE fields.
#define TXT_FLAG_COMPRESSED       0x80

// Contact flag bit indicating the remote node supports Unishox2 decompression.
// Stored in ContactInfo::flags bit 4, which is currently unused.
#define CONTACT_FLAG_SUPPORTS_LZW 0x10

// Capability bit advertised in ADV_FEAT1 to signal Unishox2 support.
#define ADV_CAP_LZW               0x0001

/**
 * Returns 1 if the text is a good candidate for compression, 0 otherwise.
 * Checks length, character entropy, and alphabetic ratio.
 */
int mesh_compression_worthwhile(const char *text, int len);

/**
 * Compresses text using Unishox2.
 * Returns the compressed length on success, or -1 if compression
 * is not worthwhile or fails.
 * out_buf must be at least MAX_TEXT_LEN bytes.
 */
int mesh_text_compress(const char *text, int text_len, uint8_t *out_buf, int out_max);

/**
 * Decompresses Unishox2-compressed data.
 * Returns the decompressed text length on success, or -1 on failure.
 * out_buf must be at least MAX_TEXT_LEN + 1 bytes.
 */
int mesh_text_decompress(const uint8_t *in_buf, int in_len, char *out_buf, int out_max);
