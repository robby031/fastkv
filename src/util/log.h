#ifndef FASTKV_UTIL_LOG_H
#define FASTKV_UTIL_LOG_H

#include <stdio.h>

typedef enum {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL,
} fastkv_log_level_t;

void fastkv_log_set_level(fastkv_log_level_t level);
void fastkv_log_set_file(FILE *f);

void fastkv__log(fastkv_log_level_t level, const char *file, int line, const char *fmt, ...);

#define LOG_TRACE(...) fastkv__log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) fastkv__log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) fastkv__log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) fastkv__log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) fastkv__log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)                                                                             \
    do {                                                                                           \
        fastkv__log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__);                                   \
        \ __builtin_trap();                                                                        \
    } while (0)

#endif /* FASTKV_UTIL_LOG_H */
