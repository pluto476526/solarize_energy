#include "config.h"
#include "logging.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

/* Forward declarations for helper functions */
static config_error_t parse_json_object(char** pos, system_config_t* config);
//static config_error_t parse_json_array(char** pos, system_config_t* config);
static config_error_t parse_load_object(char** pos, load_definition_t* load);
static config_error_t parse_zone_object(char** pos, irrigation_zone_t* zone);
static config_error_t parse_ev_charger_object(char** pos, ev_charger_t* ev_charger);
static void skip_whitespace(char** pos);
static char* parse_string(char** pos, char* buffer, size_t max_len);
static double parse_number(char** pos);
static int parse_boolean(char** pos);
static void skip_value(char** pos);

/* Default configuration values */
int config_set_defaults(system_config_t* config) {
    if (!config) return -1;
    
    memset(config, 0, sizeof(system_config_t));
    
    /* General settings */
    strcpy(config->system_name, "Solarize Energy Solutions");
    config->nominal_voltage = 240.0;
    config->max_grid_import = 10000.0;  // 10kW
    config->max_grid_export = 5000.0;   // 5kW
    
    /* Battery settings */
    config->battery_soc_min = 20.0;     // 20% minimum
    config->battery_soc_max = 95.0;     // 95% maximum
    config->battery_temp_max = 45.0;    // 45°C maximum
    config->battery_reserve_soc = 30.0; // 30% reserve for outages
    
    /* PV settings */
    config->pv_curtail_start = 90.0;    // Start curtailment at 90% SOC
    config->pv_curtail_max = 50.0;      // Max 50% curtailment
    
    /* Load management */
    config->load_count = 0;
    
    /* Irrigation settings */
    config->zone_count = 0;
    config->irrigation_mode = IRRIGATION_AUTO;
    config->irrigation_power_limit = 2000.0; // 2kW limit
    
    /* EV charging */
    config->ev_charger_count = 0;
    config->ev_charge_power_limit = 7000.0; // 7kW limit
    
    /* Control parameters */
    config->control_interval = 1.0;     // 1 second control loop
    config->measurement_interval = 0.5; // 0.5 second measurements
    config->hysteresis = 2.0;          // 2% hysteresis

    return 0;
}

/* Skip whitespace */
static void skip_whitespace(char** pos) {
    while (**pos && isspace((unsigned char)**pos)) {
        (*pos)++;
    }
}

/* Parse a JSON string */
static char* parse_string(char** pos, char* buffer, size_t max_len) {
    if (**pos != '"') return NULL;
    
    (*pos)++; // Skip opening quote
    size_t i = 0;
    
    while (**pos && **pos != '"' && i < max_len - 1) {
        if (**pos == '\\') {
            (*pos)++; // Skip escape character
            /* Handle simple escape sequences */
            switch (**pos) {
                case 'n': buffer[i++] = '\n'; break;
                case 't': buffer[i++] = '\t'; break;
                case 'r': buffer[i++] = '\r'; break;
                case 'b': buffer[i++] = '\b'; break;
                case 'f': buffer[i++] = '\f'; break;
                case '\\': buffer[i++] = '\\'; break;
                case '/': buffer[i++] = '/'; break;
                case '"': buffer[i++] = '"'; break;
                default: buffer[i++] = **pos; break;
            }
            (*pos)++;
        } else {
            buffer[i++] = **pos;
            (*pos)++;
        }
    }
    
    buffer[i] = '\0';
    
    if (**pos == '"') {
        (*pos)++; // Skip closing quote
    }
    
    return buffer;
}

/* Parse a JSON number */
static double parse_number(char** pos) {
    char buffer[64];
    size_t i = 0;
    
    /* Parse number characters */
    while (**pos && (isdigit((unsigned char)**pos) || **pos == '.' || 
                    **pos == '-' || **pos == '+' || **pos == 'e' || **pos == 'E') && 
           i < sizeof(buffer) - 1) {
        buffer[i++] = **pos;
        (*pos)++;
    }
    buffer[i] = '\0';
    
    return atof(buffer);
}

/* Parse a JSON boolean */
static int parse_boolean(char** pos) {
    if (strncmp(*pos, "true", 4) == 0) {
        *pos += 4;
        return 1;
    } else if (strncmp(*pos, "false", 5) == 0) {
        *pos += 5;
        return 0;
    }
    return 0;
}

/* Skip a JSON value (string, number, object, array, boolean, null) */
static void skip_value(char** pos) {
    skip_whitespace(pos);
    
    if (**pos == '"') {
        /* Skip string */
        (*pos)++; // Skip opening quote
        while (**pos && **pos != '"') {
            if (**pos == '\\') (*pos)++; // Skip escape sequence
            (*pos)++;
        }
        if (**pos == '"') (*pos)++; // Skip closing quote
    } else if (**pos == '{') {
        /* Skip object */
        (*pos)++; // Skip opening brace
        while (**pos && **pos != '}') {
            skip_value(pos); // Skip key
            skip_whitespace(pos);
            if (**pos == ':') {
                (*pos)++; // Skip colon
                skip_value(pos); // Skip value
            }
            skip_whitespace(pos);
            if (**pos == ',') (*pos)++; // Skip comma
        }
        if (**pos == '}') (*pos)++; // Skip closing brace
    } else if (**pos == '[') {
        /* Skip array */
        (*pos)++; // Skip opening bracket
        while (**pos && **pos != ']') {
            skip_value(pos); // Skip array element
            skip_whitespace(pos);
            if (**pos == ',') (*pos)++; // Skip comma
        }
        if (**pos == ']') (*pos)++; // Skip closing bracket
    } else if (isdigit((unsigned char)**pos) || **pos == '-' || **pos == '.') {
        /* Skip number */
        parse_number(pos);
    } else if (strncmp(*pos, "true", 4) == 0 || strncmp(*pos, "false", 5) == 0) {
        /* Skip boolean */
        parse_boolean(pos);
    } else if (strncmp(*pos, "null", 4) == 0) {
        /* Skip null */
        *pos += 4;
    }
}

/* Parse a load object */
static config_error_t parse_load_object(char** pos, load_definition_t* load) {
    skip_whitespace(pos);
    
    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip opening brace
    
    memset(load, 0, sizeof(load_definition_t));
    
    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        
        /* Parse key */
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        
        char key[64];
        if (!parse_string(pos, key, sizeof(key))) {
            return CONFIG_PARSE_ERROR;
        }
        
        skip_whitespace(pos);
        
        /* Expect colon */
        if (**pos != ':') return CONFIG_PARSE_ERROR;
        (*pos)++;
        
        skip_whitespace(pos);
        
        /* Parse value based on key */
        if (strcmp(key, "id") == 0) {
            if (**pos != '"') return CONFIG_PARSE_ERROR;
            parse_string(pos, load->id, sizeof(load->id));
        } else if (strcmp(key, "rated_power") == 0) {
            load->rated_power = parse_number(pos);
        } else if (strcmp(key, "priority") == 0) {
            double priority = parse_number(pos);
            load->priority = (load_priority_t)(int)priority;
        } else if (strcmp(key, "is_deferrable") == 0) {
            if (**pos == 't' || **pos == 'f') {
                load->is_deferrable = parse_boolean(pos);
            } else {
                load->is_deferrable = (int)parse_number(pos);
            }
        } else if (strcmp(key, "is_sheddable") == 0) {
            if (**pos == 't' || **pos == 'f') {
                load->is_sheddable = parse_boolean(pos);
            } else {
                load->is_sheddable = (int)parse_number(pos);
            }
        } else if (strcmp(key, "min_on_time") == 0) {
            load->min_on_time = parse_number(pos);
        } else if (strcmp(key, "min_off_time") == 0) {
            load->min_off_time = parse_number(pos);
        } else {
            /* Unknown key, skip it */
            skip_value(pos);
        }
        
        skip_whitespace(pos);
        
        /* Check for comma or closing brace */
        if (**pos == ',') {
            (*pos)++; // Skip comma
        } else if (**pos != '}') {
            return CONFIG_PARSE_ERROR;
        }
    }
    
    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip closing brace
    
    return CONFIG_SUCCESS;
}

/* Parse an irrigation zone object */
static config_error_t parse_zone_object(char** pos, irrigation_zone_t* zone) {
    skip_whitespace(pos);
    
    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip opening brace
    
    memset(zone, 0, sizeof(irrigation_zone_t));
    
    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        
        /* Parse key */
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        
        char key[64];
        if (!parse_string(pos, key, sizeof(key))) {
            return CONFIG_PARSE_ERROR;
        }
        
        skip_whitespace(pos);
        
        /* Expect colon */
        if (**pos != ':') return CONFIG_PARSE_ERROR;
        (*pos)++;
        
        skip_whitespace(pos);
        
        /* Parse value based on key */
        if (strcmp(key, "zone_id") == 0) {
            if (**pos != '"') return CONFIG_PARSE_ERROR;
            parse_string(pos, zone->zone_id, sizeof(zone->zone_id));
        } else if (strcmp(key, "area_sqft") == 0) {
            zone->area_sqft = parse_number(pos);
        } else if (strcmp(key, "water_flow_rate") == 0) {
            zone->water_flow_rate = parse_number(pos);
        } else if (strcmp(key, "power_consumption") == 0) {
            zone->power_consumption = parse_number(pos);
        } else if (strcmp(key, "soil_moisture") == 0) {
            zone->soil_moisture = parse_number(pos);
        } else if (strcmp(key, "moisture_threshold") == 0) {
            zone->moisture_threshold = parse_number(pos);
        } else if (strcmp(key, "watering_duration") == 0) {
            zone->watering_duration = parse_number(pos);
        } else if (strcmp(key, "enabled") == 0) {
            if (**pos == 't' || **pos == 'f') {
                zone->enabled = parse_boolean(pos);
            } else {
                zone->enabled = (int)parse_number(pos);
            }
        } else {
            /* Unknown key, skip it */
            skip_value(pos);
        }
        
        skip_whitespace(pos);
        
        /* Check for comma or closing brace */
        if (**pos == ',') {
            (*pos)++; // Skip comma
        } else if (**pos != '}') {
            return CONFIG_PARSE_ERROR;
        }
    }
    
    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip closing brace
    
    return CONFIG_SUCCESS;
}

/* Parse an EV charger object */
static config_error_t parse_ev_charger_object(char** pos, ev_charger_t* ev_charger) {
    skip_whitespace(pos);
    
    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip opening brace
    
    memset(ev_charger, 0, sizeof(ev_charger_t));
    
    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        
        /* Parse key */
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        
        char key[64];
        if (!parse_string(pos, key, sizeof(key))) {
            return CONFIG_PARSE_ERROR;
        }
        
        skip_whitespace(pos);
        
        /* Expect colon */
        if (**pos != ':') return CONFIG_PARSE_ERROR;
        (*pos)++;
        
        skip_whitespace(pos);
        
        /* Parse value based on key */
        if (strcmp(key, "ev_id") == 0) {
            if (**pos != '"') return CONFIG_PARSE_ERROR;
            parse_string(pos, ev_charger->ev_id, sizeof(ev_charger->ev_id));
        } else if (strcmp(key, "max_charge_rate") == 0) {
            ev_charger->max_charge_rate = parse_number(pos);
        } else if (strcmp(key, "min_charge_rate") == 0) {
            ev_charger->min_charge_rate = parse_number(pos);
        } else if (strcmp(key, "target_soc") == 0) {
            ev_charger->target_soc = parse_number(pos);
        } else if (strcmp(key, "current_soc") == 0) {
            ev_charger->current_soc = parse_number(pos);
        } else if (strcmp(key, "charging_enabled") == 0) {
            if (**pos == 't' || **pos == 'f') {
                ev_charger->charging_enabled = parse_boolean(pos);
            } else {
                ev_charger->charging_enabled = (int)parse_number(pos);
            }
        } else if (strcmp(key, "fast_charge_requested") == 0) {
            if (**pos == 't' || **pos == 'f') {
                ev_charger->fast_charge_requested = parse_boolean(pos);
            } else {
                ev_charger->fast_charge_requested = (int)parse_number(pos);
            }
        } else {
            /* Unknown key, skip it */
            skip_value(pos);
        }
        
        skip_whitespace(pos);
        
        /* Check for comma or closing brace */
        if (**pos == ',') {
            (*pos)++; // Skip comma
        } else if (**pos != '}') {
            return CONFIG_PARSE_ERROR;
        }
    }
    
    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip closing brace
    
    return CONFIG_SUCCESS;
}


/* Parse a JSON object */
static config_error_t parse_json_object(char** pos, system_config_t* config) {
    skip_whitespace(pos);

    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip opening brace
    
    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        
        /* Check if we're at the end */
        if (**pos == '}') break;
        
        /* Parse key */
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        
        char key[64];
        if (!parse_string(pos, key, sizeof(key))) {
            return CONFIG_PARSE_ERROR;
        }
        
        skip_whitespace(pos);
        
        /* Expect colon */
        if (**pos != ':') return CONFIG_PARSE_ERROR;
        (*pos)++;
        
        skip_whitespace(pos);
        
        /* Handle different keys */
        if (strcmp(key, "system_name") == 0) {
            if (**pos != '"') return CONFIG_PARSE_ERROR;
            parse_string(pos, config->system_name, sizeof(config->system_name));
        } 
        else if (strcmp(key, "nominal_voltage") == 0) {
            config->nominal_voltage = parse_number(pos);
        }
        else if (strcmp(key, "max_grid_import") == 0) {
            config->max_grid_import = parse_number(pos);
        }
        else if (strcmp(key, "max_grid_export") == 0) {
            config->max_grid_export = parse_number(pos);
        }
        else if (strcmp(key, "battery_soc_min") == 0) {
            config->battery_soc_min = parse_number(pos);
        }
        else if (strcmp(key, "battery_soc_max") == 0) {
            config->battery_soc_max = parse_number(pos);
        }
        else if (strcmp(key, "battery_temp_max") == 0) {
            config->battery_temp_max = parse_number(pos);
        }
        else if (strcmp(key, "battery_reserve_soc") == 0) {
            config->battery_reserve_soc = parse_number(pos);
        }
        else if (strcmp(key, "pv_curtail_start") == 0) {
            config->pv_curtail_start = parse_number(pos);
        }
        else if (strcmp(key, "pv_curtail_max") == 0) {
            config->pv_curtail_max = parse_number(pos);
        }
        else if (strcmp(key, "control_interval") == 0) {
            config->control_interval = parse_number(pos);
        }
        else if (strcmp(key, "measurement_interval") == 0) {
            config->measurement_interval = parse_number(pos);
        }
        else if (strcmp(key, "hysteresis") == 0) {
            config->hysteresis = parse_number(pos);
        }
        else if (strcmp(key, "irrigation_mode") == 0) {
            double mode = parse_number(pos);
            config->irrigation_mode = (irrigation_mode_t)(int)mode;
        }
        else if (strcmp(key, "irrigation_power_limit") == 0) {
            config->irrigation_power_limit = parse_number(pos);
        }
        else if (strcmp(key, "ev_charge_power_limit") == 0) {
            config->ev_charge_power_limit = parse_number(pos);
        }
        else if (strcmp(key, "loads") == 0) {
            /* Parse loads array */
            skip_whitespace(pos);
            if (**pos != '[') return CONFIG_PARSE_ERROR;
            (*pos)++; // Skip opening bracket
            
            config->load_count = 0;
            
            while (**pos && **pos != ']' && config->load_count < MAX_CONTROLLABLE_LOADS) {
                skip_whitespace(pos);
                
                if (**pos == ']') break; // Empty array
                
                if (**pos == '{') {
                    if (parse_load_object(pos, &config->loads[config->load_count]) != CONFIG_SUCCESS) {
                        return CONFIG_PARSE_ERROR;
                    }
                    config->load_count++;
                }
                
                skip_whitespace(pos);
                
                if (**pos == ',') {
                    (*pos)++; // Skip comma
                } else if (**pos != ']') {
                    return CONFIG_PARSE_ERROR;
                }
            }
            
            if (**pos != ']') return CONFIG_PARSE_ERROR;
            (*pos)++; // Skip closing bracket
        }
        else if (strcmp(key, "zones") == 0) {
            /* Parse zones array */
            skip_whitespace(pos);
            if (**pos != '[') return CONFIG_PARSE_ERROR;
            (*pos)++; // Skip opening bracket
            
            config->zone_count = 0;
            
            while (**pos && **pos != ']' && config->zone_count < MAX_IRRIGATION_ZONES) {
                skip_whitespace(pos);
                
                if (**pos == ']') break; // Empty array
                
                if (**pos == '{') {
                    if (parse_zone_object(pos, &config->zones[config->zone_count]) != CONFIG_SUCCESS) {
                        return CONFIG_PARSE_ERROR;
                    }
                    config->zone_count++;
                }
                
                skip_whitespace(pos);
                
                if (**pos == ',') {
                    (*pos)++; // Skip comma
                } else if (**pos != ']') {
                    return CONFIG_PARSE_ERROR;
                }
            }
            
            if (**pos != ']') return CONFIG_PARSE_ERROR;
            (*pos)++; // Skip closing bracket
        }
        else if (strcmp(key, "ev_chargers") == 0) {
            /* Parse EV chargers array */
            skip_whitespace(pos);
            if (**pos != '[') return CONFIG_PARSE_ERROR;
            (*pos)++; // Skip opening bracket
            
            config->ev_charger_count = 0;
            
            while (**pos && **pos != ']' && config->ev_charger_count < MAX_EV_CHARGERS) {
                skip_whitespace(pos);
                
                if (**pos == ']') break; // Empty array
                
                if (**pos == '{') {
                    if (parse_ev_charger_object(pos, &config->ev_chargers[config->ev_charger_count]) != CONFIG_SUCCESS) {
                        return CONFIG_PARSE_ERROR;
                    }
                    config->ev_charger_count++;
                }
                
                skip_whitespace(pos);
                
                if (**pos == ',') {
                    (*pos)++; // Skip comma
                } else if (**pos != ']') {
                    return CONFIG_PARSE_ERROR;
                }
            }
            
            if (**pos != ']') return CONFIG_PARSE_ERROR;
            (*pos)++; // Skip closing bracket
        }
        else {
            /* Unknown key, skip the value */
            skip_value(pos);
        }
        
        skip_whitespace(pos);
        
        /* Check for comma or closing brace */
        if (**pos == ',') {
            (*pos)++; // Skip comma
        } else if (**pos != '}') {
            return CONFIG_PARSE_ERROR;
        }
    }
    
    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip closing brace
    

    LOG_INFO("JSON parsed. %d loads.", config->load_count);
    return CONFIG_SUCCESS;
}

config_error_t config_load(const char* filename, system_config_t* config) {
    if (!filename || !config) {
        return CONFIG_VALIDATION_ERROR;
    }

    FILE* file = fopen(filename, "r");
    if (!file) {
        LOG_ERROR("Error opening config file: %s\n", filename);
        return CONFIG_FILE_NOT_FOUND;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size > CONFIG_MAX_SIZE) {
        fclose(file);
        LOG_ERROR("Config file too large: %ld bytes\n", file_size);
        return CONFIG_FILE_TOO_LARGE;
    }
    
    /* Read file */
    char* buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return CONFIG_MEMORY_ERROR;
    }
    
    size_t bytes_read = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';
    fclose(file);
    
    /* Set defaults first */
    if (config_set_defaults(config) != 0) return CONFIG_PARSE_ERROR;

    LOG_INFO("System defaults set");
    
    /* Parse JSON */
    char* pos = buffer;
    config_error_t result = parse_json_object(&pos, config);
    
    free(buffer);
    
    if (result != CONFIG_SUCCESS) {
        LOG_ERROR("Failed to parse JSON configuration\n");
        return result;
    }
    
    /* Validate configuration */
    return CONFIG_SUCCESS;
    // return config_validate(config);
}

config_error_t config_save(const char* filename, const system_config_t* config) {
    if (!filename || !config) {
        return CONFIG_VALIDATION_ERROR;
    }
    
    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error creating config file %s: %s\n", 
                filename, strerror(errno));
        return CONFIG_FILE_NOT_FOUND;
    }
    
    /* Generate complete JSON */
    fprintf(file, "{\n");
    fprintf(file, "  \"system_name\": \"%s\",\n", config->system_name);
    fprintf(file, "  \"nominal_voltage\": %.1f,\n", config->nominal_voltage);
    fprintf(file, "  \"max_grid_import\": %.0f,\n", config->max_grid_import);
    fprintf(file, "  \"max_grid_export\": %.0f,\n", config->max_grid_export);
    fprintf(file, "  \"battery_soc_min\": %.1f,\n", config->battery_soc_min);
    fprintf(file, "  \"battery_soc_max\": %.1f,\n", config->battery_soc_max);
    fprintf(file, "  \"battery_temp_max\": %.1f,\n", config->battery_temp_max);
    fprintf(file, "  \"battery_reserve_soc\": %.1f,\n", config->battery_reserve_soc);
    fprintf(file, "  \"pv_curtail_start\": %.1f,\n", config->pv_curtail_start);
    fprintf(file, "  \"pv_curtail_max\": %.1f,\n", config->pv_curtail_max);
    
    /* Loads array */
    fprintf(file, "  \"loads\": [\n");
    for (int i = 0; i < config->load_count; i++) {
        const load_definition_t* load = &config->loads[i];
        fprintf(file, "    {\n");
        fprintf(file, "      \"id\": \"%s\",\n", load->id);
        fprintf(file, "      \"rated_power\": %.1f,\n", load->rated_power);
        fprintf(file, "      \"priority\": %d,\n", load->priority);
        fprintf(file, "      \"is_deferrable\": %s,\n", load->is_deferrable ? "true" : "false");
        fprintf(file, "      \"is_sheddable\": %s,\n", load->is_sheddable ? "true" : "false");
        fprintf(file, "      \"min_on_time\": %.1f,\n", load->min_on_time);
        fprintf(file, "      \"min_off_time\": %.1f\n", load->min_off_time);
        fprintf(file, "    }%s\n", (i < config->load_count - 1) ? "," : "");
    }
    fprintf(file, "  ],\n");
    
    /* Zones array */
    fprintf(file, "  \"zones\": [\n");
    for (int i = 0; i < config->zone_count; i++) {
        const irrigation_zone_t* zone = &config->zones[i];
        fprintf(file, "    {\n");
        fprintf(file, "      \"zone_id\": \"%s\",\n", zone->zone_id);
        fprintf(file, "      \"area_sqft\": %.1f,\n", zone->area_sqft);
        fprintf(file, "      \"water_flow_rate\": %.1f,\n", zone->water_flow_rate);
        fprintf(file, "      \"power_consumption\": %.1f,\n", zone->power_consumption);
        fprintf(file, "      \"soil_moisture\": %.1f,\n", zone->soil_moisture);
        fprintf(file, "      \"moisture_threshold\": %.1f,\n", zone->moisture_threshold);
        fprintf(file, "      \"watering_duration\": %.1f,\n", zone->watering_duration);
        fprintf(file, "      \"enabled\": %s\n", zone->enabled ? "true" : "false");
        fprintf(file, "    }%s\n", (i < config->zone_count - 1) ? "," : "");
    }
    fprintf(file, "  ],\n");
    
    fprintf(file, "  \"irrigation_mode\": %d,\n", config->irrigation_mode);
    fprintf(file, "  \"irrigation_power_limit\": %.1f,\n", config->irrigation_power_limit);
    
    /* EV chargers array */
    fprintf(file, "  \"ev_chargers\": [\n");
    for (int i = 0; i < config->ev_charger_count; i++) {
        const ev_charger_t* ev = &config->ev_chargers[i];
        fprintf(file, "    {\n");
        fprintf(file, "      \"ev_id\": \"%s\",\n", ev->ev_id);
        fprintf(file, "      \"max_charge_rate\": %.1f,\n", ev->max_charge_rate);
        fprintf(file, "      \"min_charge_rate\": %.1f,\n", ev->min_charge_rate);
        fprintf(file, "      \"target_soc\": %.1f,\n", ev->target_soc);
        fprintf(file, "      \"current_soc\": %.1f,\n", ev->current_soc);
        fprintf(file, "      \"charging_enabled\": %s,\n", ev->charging_enabled ? "true" : "false");
        fprintf(file, "      \"fast_charge_requested\": %s\n", ev->fast_charge_requested ? "true" : "false");
        fprintf(file, "    }%s\n", (i < config->ev_charger_count - 1) ? "," : "");
    }
    fprintf(file, "  ],\n");
    
    fprintf(file, "  \"ev_charge_power_limit\": %.1f,\n", config->ev_charge_power_limit);
    fprintf(file, "  \"control_interval\": %.2f,\n", config->control_interval);
    fprintf(file, "  \"measurement_interval\": %.2f,\n", config->measurement_interval);
    fprintf(file, "  \"hysteresis\": %.1f\n", config->hysteresis);
    fprintf(file, "}\n");
    
    fclose(file);
    return CONFIG_SUCCESS;
}

config_error_t config_validate(const system_config_t* config) {
    if (!config) {
        return CONFIG_VALIDATION_ERROR;
    }
    
    /* Validate ranges */
    if (config->nominal_voltage < 100 || config->nominal_voltage > 600) {
        LOG_ERROR("Invalid nominal voltage: %.1f\n", config->nominal_voltage);
        return CONFIG_VALIDATION_ERROR;
    }
    
    if (config->battery_soc_min < 0 || config->battery_soc_min > 50) {
        LOG_ERROR("Invalid battery SOC minimum: %.1f\n", config->battery_soc_min);
        return CONFIG_VALIDATION_ERROR;
    }
    
    if (config->battery_soc_max < 50 || config->battery_soc_max > 100) {
        LOG_ERROR("Invalid battery SOC maximum: %.1f\n", config->battery_soc_max);
        return CONFIG_VALIDATION_ERROR;
    }
    
    if (config->battery_soc_min >= config->battery_soc_max) {
        LOG_ERROR("Battery SOC minimum (%.1f) >= maximum (%.1f)\n",
                config->battery_soc_min, config->battery_soc_max);
        return CONFIG_VALIDATION_ERROR;
    }
    
    if (config->control_interval < 0.1 || config->control_interval > 10.0) {
        LOG_ERROR("Invalid control interval: %.2f\n", config->control_interval);
        return CONFIG_VALIDATION_ERROR;
    }
    
    /* Validate load definitions */
    for (int i = 0; i < config->load_count; i++) {
        const load_definition_t* load = &config->loads[i];
        
        if (strlen(load->id) == 0) {
            LOG_ERROR("Load %d has empty ID\n", i);
            return CONFIG_VALIDATION_ERROR;
        }
        
        if (load->rated_power <= 0) {
            LOG_ERROR("Invalid rated power for load %s: %.1f\n",
                    load->id, load->rated_power);
            return CONFIG_VALIDATION_ERROR;
        }
        
        if (load->priority > PRIORITY_NON_ESSENTIAL) {
            LOG_ERROR("Invalid priority for load %s: %d\n",
                    load->id, load->priority);
            return CONFIG_VALIDATION_ERROR;
        }
        
        if (load->min_on_time < 0 || load->min_off_time < 0) {
            LOG_ERROR("Invalid timing for load %s\n", load->id);
            return CONFIG_VALIDATION_ERROR;
        }
    }
    
    /* Validate zone definitions */
    for (int i = 0; i < config->zone_count; i++) {
        const irrigation_zone_t* zone = &config->zones[i];
        
        if (strlen(zone->zone_id) == 0) {
            LOG_ERROR("Zone %d has empty ID\n", i);
            return CONFIG_VALIDATION_ERROR;
        }
        
        if (zone->area_sqft <= 0) {
            LOG_ERROR("Invalid area for zone %s: %.1f\n",
                    zone->zone_id, zone->area_sqft);
            return CONFIG_VALIDATION_ERROR;
        }
        
        if (zone->moisture_threshold < 0 || zone->moisture_threshold > 100) {
            LOG_ERROR("Invalid moisture threshold for zone %s: %.1f\n",
                    zone->zone_id, zone->moisture_threshold);
            return CONFIG_VALIDATION_ERROR;
        }
    }
    
    /* Validate EV charger definitions */
    for (int i = 0; i < config->ev_charger_count; i++) {
        const ev_charger_t* ev = &config->ev_chargers[i];
        
        if (strlen(ev->ev_id) == 0) {
            LOG_ERROR("EV charger %d has empty ID\n", i);
            return CONFIG_VALIDATION_ERROR;
        }
        
        if (ev->min_charge_rate > ev->max_charge_rate) {
            LOG_ERROR("Invalid charge rates for EV %s: min=%.1f, max=%.1f\n",
                    ev->ev_id, ev->min_charge_rate, ev->max_charge_rate);
            return CONFIG_VALIDATION_ERROR;
        }
        
        if (ev->target_soc < 0 || ev->target_soc > 100) {
            LOG_ERROR("Invalid target SOC for EV %s: %.1f\n",
                    ev->ev_id, ev->target_soc);
            return CONFIG_VALIDATION_ERROR;
        }
    }
    
    return CONFIG_SUCCESS;
}

void config_print(const system_config_t* config) {
    if (!config) {
        LOG_INFO("Configuration: NULL\n");
        return;
    }
    
    LOG_INFO("=== System Configuration ===\n");
    LOG_INFO("System Name: %s\n", config->system_name);
    LOG_INFO("Nominal Voltage: %.1f V\n", config->nominal_voltage);
    LOG_INFO("Max Grid Import: %.0f W\n", config->max_grid_import);
    LOG_INFO("Max Grid Export: %.0f W\n", config->max_grid_export);
    LOG_INFO("\nBattery Settings:\n");
    LOG_INFO("  SOC Min: %.1f%%\n", config->battery_soc_min);
    LOG_INFO("  SOC Max: %.1f%%\n", config->battery_soc_max);
    LOG_INFO("  Temp Max: %.1f°C\n", config->battery_temp_max);
    LOG_INFO("  Reserve SOC: %.1f%%\n", config->battery_reserve_soc);
    LOG_INFO("\nPV Settings:\n");
    LOG_INFO("  Curtail Start: %.1f%% SOC\n", config->pv_curtail_start);
    LOG_INFO("  Max Curtail: %.1f%%\n", config->pv_curtail_max);
    LOG_INFO("\nControl Parameters:\n");
    LOG_INFO("  Control Interval: %.2f s\n", config->control_interval);
    LOG_INFO("  Measurement Interval: %.2f s\n", config->measurement_interval);
    LOG_INFO("  Hysteresis: %.1f%%\n", config->hysteresis);
    
    LOG_INFO("\nLoads Configured: %d\n", config->load_count);
    for (int i = 0; i < config->load_count; i++) {
        const load_definition_t* load = &config->loads[i];
        LOG_INFO("  [%d] %s: %.1fW, Priority: %d, Deferrable: %s, Sheddable: %s\n",
               i, load->id, load->rated_power, load->priority,
               load->is_deferrable ? "Yes" : "No",
               load->is_sheddable ? "Yes" : "No");
    }
    
    LOG_INFO("\nIrrigation Zones: %d\n", config->zone_count);
    LOG_INFO("  Irrigation Mode: %d\n", config->irrigation_mode);
    LOG_INFO("  Irrigation Power Limit: %.1fW\n", config->irrigation_power_limit);
    for (int i = 0; i < config->zone_count; i++) {
        const irrigation_zone_t* zone = &config->zones[i];
        LOG_INFO("  [%d] %s: %.1f sqft, Moisture: %.1f%% (threshold: %.1f%%), Enabled: %s\n",
               i, zone->zone_id, zone->area_sqft, zone->soil_moisture,
               zone->moisture_threshold, zone->enabled ? "Yes" : "No");
    }
    
    LOG_INFO("\nEV Chargers: %d\n", config->ev_charger_count);
    LOG_INFO("  EV Charge Power Limit: %.1fW\n", config->ev_charge_power_limit);
    for (int i = 0; i < config->ev_charger_count; i++) {
        const ev_charger_t* ev = &config->ev_chargers[i];
        LOG_INFO("  [%d] %s: SOC %.1f%% -> %.1f%%, Rate: %.1f-%.1fW, Charging: %s\n",
               i, ev->ev_id, ev->current_soc, ev->target_soc,
               ev->min_charge_rate, ev->max_charge_rate,
               ev->charging_enabled ? "Enabled" : "Disabled");
    }
    
    LOG_INFO("===========================\n");
}