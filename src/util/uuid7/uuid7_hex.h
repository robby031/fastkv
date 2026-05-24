#ifndef FASTKV_UUID7_HEX_H
#define FASTKV_UUID7_HEX_H

#include "uuid7.h"

#include <stdint.h>

/* Generate UUID7 dan tulis sebagai 32 hex char + null terminator ke buf[33]. */
static inline void uuid7_hex(uuid7_ctx *ctx, char buf[33]) {
    static const char hx[] = "0123456789abcdef";
    uint8_t           raw[16];
    uuid7_generate(ctx, raw);
    for (int i = 0; i < 16; i++) {
        buf[i * 2]     = hx[raw[i] >> 4];
        buf[i * 2 + 1] = hx[raw[i] & 0xF];
    }
    buf[32] = '\0';
}

#endif /* FASTKV_UUID7_HEX_H */
