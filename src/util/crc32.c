#include "crc32.h"

#if defined(__SSE4_2__) && defined(__x86_64__)
#include <nmmintrin.h>
#define HAVE_HW_CRC32C 1
#endif

#ifndef HAVE_HW_CRC32C
/* Software fallback — CRC-32C lookup table */
static uint32_t crc32c_table[256];
static int      table_init = 0;

static void init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0x82F63B78u & -(crc & 1));
        crc32c_table[i] = crc;
    }
    table_init = 1;
}
#endif

uint32_t fastkv_crc32c(uint32_t crc, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    crc              = ~crc;

#ifdef HAVE_HW_CRC32C
    while (len >= 8) {
        crc = (uint32_t)_mm_crc32_u64(crc, *(const uint64_t *)p);
        p += 8;
        len -= 8;
    }
    while (len--)
        crc = (uint32_t)_mm_crc32_u8(crc, *p++);
#else
    if (!table_init)
        init_table();
    while (len--)
        crc = (crc >> 8) ^ crc32c_table[(crc ^ *p++) & 0xFF];
#endif

    return ~crc;
}
