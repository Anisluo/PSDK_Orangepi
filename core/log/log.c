#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static LogLevel g_level = LOG_INFO;

void log_set_level(LogLevel level) {
    g_level = level;
}

static const char *level_str(LogLevel l) {
    switch (l) {
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        case LOG_ERROR: return "ERR";
        default:        return "???";
    }
}

static void log_write(LogLevel level, const char *tag, const char *fmt, va_list ap) {
    if (level < g_level) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char timebuf[24];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_info);

    FILE *out = (level >= LOG_WARN) ? stderr : stdout;
    fprintf(out, "[%s.%03ld] [%s] [%s] ",
            timebuf, ts.tv_nsec / 1000000L,
            level_str(level), tag);
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    fflush(out);
}

void log_debug(const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    log_write(LOG_DEBUG, tag, fmt, ap);
    va_end(ap);
}

void log_info(const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    log_write(LOG_INFO, tag, fmt, ap);
    va_end(ap);
}

void log_warn(const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    log_write(LOG_WARN, tag, fmt, ap);
    va_end(ap);
}

void log_error(const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    log_write(LOG_ERROR, tag, fmt, ap);
    va_end(ap);
}
