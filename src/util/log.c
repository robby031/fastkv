#include "log.h"

#include <stdarg.h>
#include <time.h>

static fastkv_log_level_t g_level = LOG_INFO;
static FILE              *g_file  = NULL;

static const char *level_str[] = {
    [LOG_TRACE] = "TRACE",
    [LOG_DEBUG] = "DEBUG",
    [LOG_INFO]  = "INFO ",
    [LOG_WARN]  = "WARN ",
    [LOG_ERROR] = "ERROR",
    [LOG_FATAL] = "FATAL",
};

void fastkv_log_set_level(fastkv_log_level_t level) {
    g_level = level;
}
void fastkv_log_set_file(FILE *f) {
    g_file = f;
}

void fastkv__log(fastkv_log_level_t level, const char *file, int line, const char *fmt, ...) {
    if (level < g_level)
        return;

    FILE *out = g_file ? g_file : stderr;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    gmtime_r(&ts.tv_sec, &tm_info);

    char timebuf[32];
    strftime(timebuf, sizeof timebuf, "%H:%M:%S", &tm_info);

    fprintf(
        out, "%s.%03ld [%s] %s:%d  ", timebuf, ts.tv_nsec / 1000000L, level_str[level], file, line);

    va_list ap;
    va_start(ap, fmt);
    if (fmt)
        vfprintf(out, fmt, ap);
    va_end(ap);

    fputc('\n', out);
    fflush(out);
}
