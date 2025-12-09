#include "logging.h"
#include <stdlib.h>
#include <unistd.h>

static log_config_t log_config = {
    .log_file = NULL,
    .console_level = LOG_INFO,
    .file_level = LOG_INFO,
    .use_color = 1,
    .program_name = "solarize"
};

/* Get current timestamp as string */
static void get_timestamp(char *buffer, size_t buffer_size) {
    time_t now;
    struct tm *tm_info;
    
    time(&now);
    tm_info = localtime(&now);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* Get log level as string */
static const char *get_level_string(log_level_t level) {
    switch(level) {
        case LOG_ERROR:   return "ERROR";
        case LOG_WARNING: return "WARNING";
        case LOG_INFO:    return "INFO";
        case LOG_DEBUG:   return "DEBUG";
        default:          return "UNKNOWN";
    }
}

/* Get log level color (ANSI escape codes) */
static const char *get_level_color(log_level_t level) {
    if (!log_config.use_color) return "";
    
    switch(level) {
        case LOG_ERROR:   return "\033[1;31m";  /* Bold Red */
        case LOG_WARNING: return "\033[1;33m";  /* Bold Yellow */
        case LOG_INFO:    return "\033[1;32m";  /* Bold Green */
        case LOG_DEBUG:   return "\033[1;36m";  /* Bold Cyan */
        default:          return "\033[0m";     /* Reset */
    }
}

/* Get reset color */
static const char *get_reset_color(void) {
    return log_config.use_color ? "\033[0m" : "";
}

/* Initialize logging system */
int log_init(const char *filename, log_level_t console_level, 
             log_level_t file_level, const char *program_name) {
    
    /* Set configuration */
    log_config.console_level = console_level;
    log_config.file_level = file_level;
    if (program_name) {
        log_config.program_name = program_name;
    }
    
    /* Check if we should use color (disable if output is redirected) */
    log_config.use_color = isatty(fileno(stdout)) ? 1 : 0;
    
    /* Open log file if specified */
    if (filename && filename[0] != '\0') {
        log_config.log_file = fopen(filename, "a");
        if (!log_config.log_file) {
            fprintf(stderr, "Failed to open log file: %s\n", filename);
            return -1;
        }
        /* Make line buffered for log file */
        setvbuf(log_config.log_file, NULL, _IOLBF, 0);
    }
    
    return 0;
}

/* Close logging system */
void log_close(void) {
    if (log_config.log_file) {
        fclose(log_config.log_file);
        log_config.log_file = NULL;
    }
}

/* Log a message */
void log_message(log_level_t level, const char *file, int line, const char *format, ...) {
    char timestamp[32];
    va_list args_console, args_file;
    
    /* Get timestamp */
    get_timestamp(timestamp, sizeof(timestamp));
    
    /* Check if we should log to console */
    if (level <= log_config.console_level) {
        va_start(args_console, format);
         fprintf(stdout, "%s[%s] [%s] [%s] (%s:%d) ",
                get_level_color(level),
                timestamp,
                log_config.program_name,
                get_level_string(level),
                file, line);
        vfprintf(stdout, format, args_console);
        fprintf(stdout, "%s\n", get_reset_color());
        va_end(args_console);
        fflush(stdout);
    }
    
    /* Check if we should log to file */
    if (log_config.log_file && level <= log_config.file_level) {
        va_start(args_file, format);
        fprintf(log_config.log_file, "[%s] [%s] [%s] (%s:%d) ",
                timestamp,
                log_config.program_name,
                get_level_string(level),
                file, line);
        vfprintf(log_config.log_file, format, args_file);
        fprintf(log_config.log_file, "\n");
        fflush(log_config.log_file);
        va_end(args_file);
    }
}
