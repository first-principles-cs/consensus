/**
 * crc32.c - CRC32 checksum implementation
 */

#include "crc32.h"

/* CRC32 lookup table (polynomial 0xEDB88320) */
static uint32_t crc32_table[256];
static int table_initialized = 0;

static void init_crc32_table(void) {
    if (table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
    table_initialized = 1;
}

uint32_t crc32_update(uint32_t crc, const void* data, size_t len) {
    init_crc32_table();

    const uint8_t* buf = (const uint8_t*)data;
    crc = ~crc;

    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }

    return ~crc;
}

uint32_t crc32(const void* data, size_t len) {
    return crc32_update(0, data, len);
}
