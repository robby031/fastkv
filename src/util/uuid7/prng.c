#include "internal.h"

#include <string.h>

// Rotasi kiri 64-bit
static inline uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t xoshiro256_next(uuid7_prng *prng) {
    uint64_t *s      = prng->s;
    uint64_t  result = rotl64(s[1] * 5, 7) * 9;
    uint64_t  t      = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];

    s[2] ^= t;
    s[3] = rotl64(s[3], 45);

    return result;
}

#if defined(_WIN32)
#include <bcrypt.h>
#include <windows.h>
#pragma comment(lib, "bcrypt.lib")

static int get_entropy(void *buf, size_t len) {
    NTSTATUS status =
        BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return (status >= 0) ? 0 : -1;
}
#elif defined(__linux__) && defined(SYS_getrandom)
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

static int get_entropy(void *buf, size_t len) {
    uint8_t *p    = (uint8_t *)buf;
    size_t   done = 0;
    while (done < len) {
        ssize_t ret = syscall(SYS_getrandom, p + done, len - done, 0);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        done += (size_t)ret;
    }
    return 0;
}
#else
#include <stdio.h>

static int get_entropy(void *buf, size_t len) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f)
        return -1;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}
#endif

#if defined(_MSC_VER)
#include <intrin.h>
static uint64_t rdtsc(void) {
    return __rdtsc();
}
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
static uint64_t rdtsc(void) {
    return __rdtsc();
}
#else
static uint64_t rdtsc(void) {
    return 0;
}
#endif

void prng_seed(uuid7_prng *prng) {
    uint64_t seed[4];
    if (get_entropy(seed, sizeof(seed)) != 0) {
        uint64_t t = os_time_ms();
        uint64_t c = rdtsc();
        seed[0]    = t ^ c ^ ((uint64_t)(uintptr_t)prng << 32);
        seed[1]    = t ^ 0x9E3779B97F4A7C15ULL;
        seed[2]    = (uint64_t)(uintptr_t)&prng ^ (c << 17);
        seed[3]    = t ^ (c >> 7);
    }
    memcpy(prng->s, seed, sizeof(prng->s));
    for (int i = 0; i < 8; i++) {
        xoshiro256_next(prng);
    }
}

void prng_fill(uuid7_prng *prng, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len >= 8) {
        uint64_t r = xoshiro256_next(prng);
        memcpy(p, &r, 8);
        p += 8;
        len -= 8;
    }
    if (len > 0) {
        uint64_t r = xoshiro256_next(prng);
        memcpy(p, &r, len);
    }
}