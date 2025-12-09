#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

// Log levels
typedef enum {
    LOG_ERROR = 0,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

// Log configuration
typedef struct {
    FILE *log_file;
    log_level_t console_level;
    log_level_t file_level;
    int use_color;
    const char *program_name;
} log_config_t;

// Initialize logging system
int log_init(const char *filename, log_level_t console_level, 
             log_level_t file_level, const char *program_name);

// Close logging system
void log_close(void);

// Log a message
void log_message(log_level_t level, const char *file, int line, const char *format, ...);

// High-level macro wrapper
#define log_message_(level, fmt, ...) \
    log_message(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* Convenience macros */
#define LOG_ERROR(...)   log_message_(LOG_ERROR, __VA_ARGS__)
#define LOG_WARNING(...) log_message_(LOG_WARNING, __VA_ARGS__)
#define LOG_INFO(...)    log_message_(LOG_INFO, __VA_ARGS__)
#define LOG_DEBUG(...)   log_message_(LOG_DEBUG, __VA_ARGS__)

/* Log with errno */
#define LOG_ERROR_ERRNO(...) do { \
    int saved_errno = errno; \
    log_message_(LOG_ERROR, __VA_ARGS__); \
    log_message_(LOG_ERROR, "Error details: %s (errno=%d)", \
                strerror(saved_errno), saved_errno); \
} while(0)

#endif /* LOGGING_H */
