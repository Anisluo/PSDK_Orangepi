#ifndef DRONE_LOG_H
#define DRONE_LOG_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

void log_set_level(LogLevel level);

void log_debug(const char *tag, const char *fmt, ...);
void log_info (const char *tag, const char *fmt, ...);
void log_warn (const char *tag, const char *fmt, ...);
void log_error(const char *tag, const char *fmt, ...);

#endif /* DRONE_LOG_H */
