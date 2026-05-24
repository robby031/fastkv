#include "internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

static inline void write_be48(uint8_t *buf, uint64_t val) {
    buf[0] = (uint8_t)(val >> 40);
    buf[1] = (uint8_t)(val >> 32);
    buf[2] = (uint8_t)(val >> 24);
    buf[3] = (uint8_t)(val >> 16);
    buf[4] = (uint8_t)(val >> 8);
    buf[5] = (uint8_t)(val);
}

static inline void write_be56(uint8_t *buf, uint64_t val) {
    buf[0] = (uint8_t)(val >> 48);
    buf[1] = (uint8_t)(val >> 40);
    buf[2] = (uint8_t)(val >> 32);
    buf[3] = (uint8_t)(val >> 24);
    buf[4] = (uint8_t)(val >> 16);
    buf[5] = (uint8_t)(val >> 8);
    buf[6] = (uint8_t)(val);
}

uuid7_ctx *uuid7_create(void) {
    uuid7_ctx *ctx = (uuid7_ctx *)malloc(sizeof(uuid7_ctx));
    if (ctx) {
        uuid7_init(ctx);
    }
    return ctx;
}

void uuid7_destroy(uuid7_ctx *ctx) {
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
        free(ctx);
    }
}

void uuid7_init(uuid7_ctx *ctx) {
    if (!ctx)
        return;
    prng_seed(&ctx->prng);
    ctx->last_timestamp_ms = os_time_ms();
    uint64_t rnd[2];
    prng_fill(&ctx->prng, rnd, sizeof(rnd));
    ctx->rand_a_high  = (rnd[0] >> 60) & 0xF;
    ctx->counter_high = (rnd[0] >> 46) & 0x3FFF;
    ctx->counter_low  = rnd[1] & 0x00FFFFFFFFFFFFFFULL;
}

void uuid7_generate_ex(uuid7_ctx *ctx, uint8_t out[16], uint64_t now) {
    if (!ctx || !out)
        return;
    int64_t delta = (int64_t)(now - ctx->last_timestamp_ms);

    if (LIKELY(delta > 0)) {
        // Milidetik baru
        ctx->last_timestamp_ms = now;
        uint64_t rnd[2];
        prng_fill(&ctx->prng, rnd, sizeof(rnd));
        ctx->rand_a_high  = (rnd[0] >> 60) & 0xF;
        ctx->counter_high = (rnd[0] >> 46) & 0x3FFF;
        ctx->counter_low  = rnd[1] & 0x00FFFFFFFFFFFFFFULL;
    } else if (LIKELY(delta == 0) ||
               (delta < 0 && (uint64_t)(-delta) <= UUID7_CLOCK_BACKWARD_THRESHOLD_MS)) {
        // Milidetik sama / mundur kecil
        // Mask setelah increment agar overflow 56-bit terdeteksi dengan benar.
        // Tanpa mask, ++counter_low dari 0x00FFFFFFFFFFFFFF menjadi 0x0100000000000000
        // yang tidak pernah == 0, sehingga overflow tidak pernah terdeteksi.
        ctx->counter_low = (ctx->counter_low + 1) & 0x00FFFFFFFFFFFFFFULL;
        if (UNLIKELY(ctx->counter_low == 0)) {
            if (UNLIKELY(++ctx->counter_high == 0x4000)) {
                // Overflow counter
                uint64_t fresh_now;
                while ((fresh_now = os_time_ms()) == ctx->last_timestamp_ms)
                    ;
                ctx->last_timestamp_ms = fresh_now;
                uint64_t rnd[2];
                prng_fill(&ctx->prng, rnd, sizeof(rnd));
                ctx->rand_a_high  = (rnd[0] >> 60) & 0xF;
                ctx->counter_high = (rnd[0] >> 46) & 0x3FFF;
                ctx->counter_low  = rnd[1] & 0x00FFFFFFFFFFFFFFULL;
            }
        }
    } else {
        // Mundur besar: reset timestamp
        ctx->last_timestamp_ms = now;
        uint64_t rnd[2];
        prng_fill(&ctx->prng, rnd, sizeof(rnd));
        ctx->rand_a_high  = (rnd[0] >> 60) & 0xF;
        ctx->counter_high = (rnd[0] >> 46) & 0x3FFF;
        ctx->counter_low  = rnd[1] & 0x00FFFFFFFFFFFFFFULL;
    }

    // Serialisasi
    write_be48(out, ctx->last_timestamp_ms);
    out[6] = 0x70 | ctx->rand_a_high;
    out[7] = (uint8_t)(ctx->counter_high >> 6);
    out[8] = 0x80 | (uint8_t)(ctx->counter_high & 0x3F);
    write_be56(&out[9], ctx->counter_low);
}

void uuid7_generate(uuid7_ctx *ctx, uint8_t out[16]) {
    if (!ctx || !out)
        return;
    uuid7_generate_ex(ctx, out, os_time_ms());
}