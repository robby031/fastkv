#include "internal.h"

#if defined(_WIN32)
// Windows
#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <windows.h>

uint64_t os_time_ms(void) {
    FILETIME ft;
#if defined(_MSC_VER) && (_WIN32_WINNT >= 0x0602)
    GetSystemTimePreciseAsFileTime(&ft);
#else
    GetSystemTimeAsFileTime(&ft);
#endif

    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

    const uint64_t EPOCH_OFFSET = 116444736000000000ULL;
    return (ticks - EPOCH_OFFSET) / 10000;
}

#elif defined(__APPLE__) || defined(__unix__) || defined(__linux__) || defined(__MACH__)
// Unix-like (Linux, macOS, BSD)
#include <stdint.h>
#include <time.h>

uint64_t os_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return (uint64_t)time(NULL) * 1000;
    }
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

#else
#include <stdint.h>
#include <time.h>

uint64_t os_time_ms(void) {
    return (uint64_t)time(NULL) * 1000;
}
#endif