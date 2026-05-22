#include "hash.h"

/* wyhash v4 — https://github.com/wangyi-fudan/wyhash */
static inline uint64_t _wymix(uint64_t a, uint64_t b) {
    __uint128_t r = (__uint128_t)a * b;
    return (uint64_t)(r ^ (r >> 64));
}

static inline uint64_t _wyr8(const uint8_t *p) {
    uint64_t v;
    __builtin_memcpy(&v, p, 8);
    return v;
}
static inline uint64_t _wyr4(const uint8_t *p) {
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}

uint64_t fastkv_hash64(const void *key, size_t len, uint64_t seed) {
    const uint8_t *p = (const uint8_t *)key;
    seed ^= 0xa0761d6478bd642full;
    uint64_t a, b;

    if (__builtin_expect(len <= 16, 1)) {
        if (len >= 4) {
            a = (_wyr4(p) << 32) | _wyr4(p + ((len >> 3) << 2));
            b = (_wyr4(p + len - 4) << 32) | _wyr4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) | p[len - 1];
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        size_t i = len;
        if (__builtin_expect(i > 48, 0)) {
            uint64_t s1 = seed, s2 = seed;
            do {
                seed = _wymix(_wyr8(p) ^ 0xe7037ed1a0b428dbull, _wyr8(p + 8) ^ seed);
                s1   = _wymix(_wyr8(p + 16) ^ 0x8ebc6af09c88c6e3ull, _wyr8(p + 24) ^ s1);
                s2   = _wymix(_wyr8(p + 32) ^ 0x589965cc75374cc3ull, _wyr8(p + 40) ^ s2);
                p += 48;
                i -= 48;
            } while (__builtin_expect(i > 48, 0));
            seed ^= s1 ^ s2;
        }
        while (__builtin_expect(i > 16, 0)) {
            seed = _wymix(_wyr8(p) ^ 0xe7037ed1a0b428dbull, _wyr8(p + 8) ^ seed);
            p += 16;
            i -= 16;
        }
        a = _wyr8(p + i - 16);
        b = _wyr8(p + i - 8);
    }

    return _wymix(
        0xa0761d6478bd642full ^ (uint64_t)len, _wymix(a ^ 0xe7037ed1a0b428dbull, b ^ seed));
}
