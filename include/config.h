#ifndef CONFIG_H
#define CONFIG_H

#include "core.h"
#include <stdio.h>

#define CONFIG_FILENAME "system_config.json"
#define CONFIG_MAX_SIZE 16384  // 16KB maximum config size

/* Configuration error codes */
typedef enum {
    CONFIG_SUCCESS = 0,
    CONFIG_FILE_NOT_FOUND,
    CONFIG_FILE_TOO_LARGE,
    CONFIG_PARSE_ERROR,
    CONFIG_VALIDATION_ERROR,
    CONFIG_MEMORY_ERROR
} config_error_t;

/* Configuration management functions */
config_error_t config_load(const char* filename, system_config_t* config);
config_error_t config_save(const char* filename, const system_config_t* config);
config_error_t config_validate(const system_config_t* config);
void config_print(const system_config_t* config);
int config_set_defaults(system_config_t* config);


#endif /* CONFIG_H */
