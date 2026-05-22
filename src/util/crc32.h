#ifndef FASTKV_UTIL_CRC32_H
#define FASTKV_UTIL_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* CRC-32C (Castagnoli) — matches hardware acceleration via SSE4.2 _mm_crc32_u* */
uint32_t fastkv_crc32c(uint32_t crc, const void *buf, size_t len);

/* Convenience: compute over a fresh buffer (initial crc = 0) */
static inline uint32_t fastkv_crc32c_buf(const void *buf, size_t len) {
 return fastkv_crc32c(0, buf, len);
}

#endif /* FASTKV_UTIL_CRC32_H */
