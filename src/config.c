#include "config.h"
#include "logging.h"
#include "core.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>

/* Forward declarations */
static void skip_whitespace(char** pos);
static char* parse_string(char** pos, char* buffer, size_t max_len);
static double parse_number(char** pos);
static int parse_boolean(char** pos);
static void skip_value(char** pos);

/* Generic object parsers with void* */
static config_error_t parse_load_object(char** pos, void* obj_ptr);
static config_error_t parse_zone_object(char** pos, void* obj_ptr);
static config_error_t parse_ev_charger_object(char** pos, void* obj_ptr);
static config_error_t parse_battery_object(char** pos, void* obj_ptr);

/* Generic array parser */
static config_error_t parse_array_generic(char** pos, void* array, int* count, int max_count,
    size_t struct_size, config_error_t (*parser)(char**, void*)) 
{
    skip_whitespace(pos);
    if (**pos != '[') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip '['

    *count = 0;
    while (**pos && **pos != ']' && *count < max_count) {
        skip_whitespace(pos);
        void* obj_ptr = (char*)array + (*count) * struct_size;
        if (**pos == '{') {
            if (parser(pos, obj_ptr) != CONFIG_SUCCESS) return CONFIG_PARSE_ERROR;
            (*count)++;
        }
        skip_whitespace(pos);
        if (**pos == ',') (*pos)++;
        else if (**pos != ']') return CONFIG_PARSE_ERROR;
    }

    if (**pos != ']') return CONFIG_PARSE_ERROR;
    (*pos)++; // Skip ']'
    return CONFIG_SUCCESS;
}


/* Default configuration values */
int config_set_defaults(system_config_t* config) {
    if (!config) return -1;
    memset(config, 0, sizeof(system_config_t));

    strcpy(config->system_name, "Solarize Energy Solutions");
    config->nominal_voltage = 240.0;
    config->max_grid_import = 10000.0;
    config->max_grid_export = 5000.0;

    config->battery_soc_min = 20.0;
    config->battery_soc_max = 95.0;
    config->battery_temp_max = 45.0;
    config->battery_reserve_soc = 30.0;

    config->pv_curtail_start = 90.0;
    config->pv_curtail_max = 50.0;

    config->load_count = 0;
    config->zone_count = 0;
    config->bank_count = MAX_BATTERY_BANKS;
    config->ev_charger_count = 0;

    config->irrigation_mode = IRRIGATION_AUTO;
    config->irrigation_power_limit = 2000.0;
    config->ev_charge_power_limit = 7000.0;

    config->control_interval = 1.0;
    config->measurement_interval = 0.5;
    config->hysteresis = 2.0;

    // Initialize battery banks with default values
    // for (int i = 0; i < MAX_BATTERY_BANKS; i++) {
    //     battery_bank_t* bat = &config->batteries[i];
    //     snprintf(bat->bank_id, sizeof(bat->bank_id), "BAT%02d", i+1);
    //     bat->capacity_wh = 10.0;           // default 10 kWh
    //     // bat->current_soc = 50.0;            // default 50% SOC
    //     bat->max_charge_power = 5000.0;     // default 5 kW
    //     bat->max_discharge_power = 5000.0;  // default 5 kW
    // }

    return 0;
}


/* Skip whitespace */
static void skip_whitespace(char** pos) {
    while (**pos && isspace((unsigned char)**pos)) (*pos)++;
}

/* Parse string safely */
static char* parse_string(char** pos, char* buffer, size_t max_len) {
    if (**pos != '"') return NULL;
    (*pos)++;
    size_t i = 0;
    while (**pos && **pos != '"' && (buffer == NULL || i < max_len - 1)) {
        if (**pos == '\\') {
            (*pos)++;
            char c = **pos;
            (*pos)++;
            if (!buffer) continue;
            switch (c) {
                case 'n': buffer[i++] = '\n'; break;
                case 't': buffer[i++] = '\t'; break;
                case 'r': buffer[i++] = '\r'; break;
                case 'b': buffer[i++] = '\b'; break;
                case 'f': buffer[i++] = '\f'; break;
                case '\\': buffer[i++] = '\\'; break;
                case '/': buffer[i++] = '/'; break;
                case '"': buffer[i++] = '"'; break;
                default: buffer[i++] = c; break;
            }
        } else {
            if (buffer) buffer[i++] = **pos;
            (*pos)++;
        }
    }

    if (buffer) buffer[i] = '\0';
    if (**pos == '"') (*pos)++;
    return buffer;
}

/* Parse number */
static double parse_number(char** pos) {
    char buffer[64];
    size_t i = 0;

    while (**pos && (isdigit((unsigned char)**pos) || **pos == '.' || **pos == '-' || **pos == '+' || **pos == 'e' || **pos == 'E') && i < sizeof(buffer) - 1) {
        buffer[i++] = **pos;
        (*pos)++;
    }

    buffer[i] = '\0';
    return atof(buffer);
}

/* Parse boolean */
static int parse_boolean(char** pos) {
    if (strncmp(*pos, "true", 4) == 0) { *pos += 4; return 1; }
    if (strncmp(*pos, "false", 5) == 0) { *pos += 5; return 0; }
    return 0;
}

/* Skip any value */
static void skip_value(char** pos) {
    skip_whitespace(pos);
    if (**pos == '"') { parse_string(pos, NULL, 0); }
    else if (**pos == '{') { (*pos)++; while (**pos && **pos != '}') { skip_value(pos); if (**pos==':') (*pos)++; skip_value(pos); if (**pos==',') (*pos)++; } if (**pos=='}') (*pos)++; }
    else if (**pos == '[') { (*pos)++; while (**pos && **pos!=']') { skip_value(pos); if (**pos==',') (*pos)++; } if (**pos==']') (*pos)++; }
    else if (isdigit((unsigned char)**pos) || **pos=='-' || **pos=='.') parse_number(pos);
    else if (strncmp(*pos,"true",4)==0 || strncmp(*pos,"false",5)==0) parse_boolean(pos);
    else if (strncmp(*pos,"null",4)==0) *pos+=4;
}

/* Object parsers */
static config_error_t parse_load_object(char** pos, void* obj_ptr) {
    load_definition_t* load = (load_definition_t*)obj_ptr;
    memset(load, 0, sizeof(*load));
    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++;

    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        char key[64];
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        if (!parse_string(pos, key, sizeof(key))) return CONFIG_PARSE_ERROR;
        skip_whitespace(pos); if (**pos != ':') return CONFIG_PARSE_ERROR; (*pos)++;
        skip_whitespace(pos);

        if (strcmp(key, "id") == 0) parse_string(pos, load->id, sizeof(load->id));
        else if (strcmp(key, "rated_power") == 0) load->rated_power = parse_number(pos);
        else if (strcmp(key, "priority") == 0) load->priority = (load_priority_t)(int)parse_number(pos);
        else if (strcmp(key, "is_deferrable") == 0) load->is_deferrable = (**pos=='t'||**pos=='f')?parse_boolean(pos):(int)parse_number(pos);
        else if (strcmp(key, "is_sheddable") == 0) load->is_sheddable = (**pos=='t'||**pos=='f')?parse_boolean(pos):(int)parse_number(pos);
        else if (strcmp(key, "min_on_time") == 0) load->min_on_time = parse_number(pos);
        else if (strcmp(key, "min_off_time") == 0) load->min_off_time = parse_number(pos);
        else skip_value(pos);

        skip_whitespace(pos);
        if (**pos == ',') (*pos)++;
    }

    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++;
    return CONFIG_SUCCESS;
}

static config_error_t parse_zone_object(char** pos, void* obj_ptr) {
    irrigation_zone_t* zone = (irrigation_zone_t*)obj_ptr;
    memset(zone, 0, sizeof(*zone));

    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++;

    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        char key[64];
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        if (!parse_string(pos, key, sizeof(key))) return CONFIG_PARSE_ERROR;
        skip_whitespace(pos); if (**pos != ':') return CONFIG_PARSE_ERROR; (*pos)++; skip_whitespace(pos);

        if (strcmp(key, "zone_id") == 0) parse_string(pos, zone->zone_id, sizeof(zone->zone_id));
        else if (strcmp(key, "area_sqft") == 0) zone->area_sqft = parse_number(pos);
        else if (strcmp(key, "water_flow_rate") == 0) zone->water_flow_rate = parse_number(pos);
        else if (strcmp(key, "power_consumption") == 0) zone->power_consumption = parse_number(pos);
        else if (strcmp(key, "soil_moisture") == 0) zone->soil_moisture = parse_number(pos);
        else if (strcmp(key, "moisture_threshold") == 0) zone->moisture_threshold = parse_number(pos);
        else if (strcmp(key, "watering_duration") == 0) zone->watering_duration = parse_number(pos);
        else if (strcmp(key, "enabled") == 0) zone->enabled = (**pos=='t'||**pos=='f')?parse_boolean(pos):(int)parse_number(pos);
        else skip_value(pos);

        skip_whitespace(pos);
        if (**pos == ',') (*pos)++;
    }

    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++;
    return CONFIG_SUCCESS;
}

static config_error_t parse_ev_charger_object(char** pos, void* obj_ptr) {
    ev_charger_t* ev = (ev_charger_t*)obj_ptr;
    memset(ev, 0, sizeof(*ev));

    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++;

    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        char key[64];
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        if (!parse_string(pos, key, sizeof(key))) return CONFIG_PARSE_ERROR;
        skip_whitespace(pos); if (**pos != ':') return CONFIG_PARSE_ERROR; (*pos)++; skip_whitespace(pos);

        if (strcmp(key, "ev_id") == 0) parse_string(pos, ev->ev_id, sizeof(ev->ev_id));
        else if (strcmp(key, "max_charge_rate") == 0) ev->max_charge_rate = parse_number(pos);
        else if (strcmp(key, "min_charge_rate") == 0) ev->min_charge_rate = parse_number(pos);
        else if (strcmp(key, "target_soc") == 0) ev->target_soc = parse_number(pos);
        else if (strcmp(key, "current_soc") == 0) ev->current_soc = parse_number(pos);
        else if (strcmp(key, "charging_enabled") == 0) ev->charging_enabled = (**pos=='t'||**pos=='f')?parse_boolean(pos):(int)parse_number(pos);
        else if (strcmp(key, "fast_charge_requested") == 0) ev->fast_charge_requested = (**pos=='t'||**pos=='f')?parse_boolean(pos):(int)parse_number(pos);
        else skip_value(pos);

        skip_whitespace(pos);
        if (**pos == ',') (*pos)++;
    }

    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++;
    return CONFIG_SUCCESS;
}

/* New battery parser */
static config_error_t parse_battery_object(char** pos, void* obj_ptr) {
    battery_bank_t* bat = (battery_bank_t*)obj_ptr;
    memset(bat, 0, sizeof(*bat));

    if (**pos != '{') return CONFIG_PARSE_ERROR;

    (*pos)++;

    while (**pos && **pos != '}') {
        skip_whitespace(pos);

        char key[64];

        if (**pos != '"') return CONFIG_PARSE_ERROR;
        if (!parse_string(pos, key, sizeof(key))) return CONFIG_PARSE_ERROR;

        skip_whitespace(pos);

        if (**pos != ':') return CONFIG_PARSE_ERROR;

        (*pos)++;

        skip_whitespace(pos);

        if (strcmp(key, "bank_id") == 0) parse_string(pos, bat->bank_id, sizeof(bat->bank_id));
        else if (strcmp(key, "capacity_wh") == 0) bat->capacity_wh = parse_number(pos);
        else if (strcmp(key, "cells_in_series") == 0) bat->cells_in_series = parse_number(pos);
        else if (strcmp(key, "nominal_voltage") == 0) bat->nominal_voltage = parse_number(pos);
        else if (strcmp(key, "max_charge_power") == 0) bat->max_charge_power = parse_number(pos);
        else if (strcmp(key, "max_discharge_power") == 0) bat->max_discharge_power = parse_number(pos);
        else skip_value(pos);

        skip_whitespace(pos);

        if (**pos == ',') (*pos)++; }
        if (**pos != '}') return CONFIG_PARSE_ERROR;

        (*pos)++;

        return CONFIG_SUCCESS;
}



static config_error_t parse_batteries_object(char** pos, system_config_t* config) {
    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++;

    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        char key[64];
        if (**pos != '"') return CONFIG_PARSE_ERROR;
        if (!parse_string(pos, key, sizeof(key))) return CONFIG_PARSE_ERROR;
        skip_whitespace(pos); if (**pos != ':') return CONFIG_PARSE_ERROR; (*pos)++; skip_whitespace(pos);

        if (strcmp(key, "banks") == 0) {
            // Parse the banks array
            parse_array_generic(pos, config->batteries, &config->bank_count, MAX_BATTERY_BANKS,
                                sizeof(battery_bank_t), parse_battery_object);
        } else {
            // Skip any metadata fields like chemistry, nominal_voltage, etc.
            skip_value(pos);
        }

        skip_whitespace(pos);
        if (**pos == ',') (*pos)++;
    }

    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++;
    return CONFIG_SUCCESS;
}




/* Parse main JSON object */
static config_error_t parse_json_object(char** pos, system_config_t* config) {
    skip_whitespace(pos);

    if (**pos != '{') return CONFIG_PARSE_ERROR;
    (*pos)++;

    while (**pos && **pos != '}') {
        skip_whitespace(pos);
        if (**pos == '"') {
            char key[64];
            if (!parse_string(pos, key, sizeof(key))) return CONFIG_PARSE_ERROR;
            skip_whitespace(pos); if (**pos != ':') return CONFIG_PARSE_ERROR; (*pos)++; skip_whitespace(pos);

            if (strcmp(key, "system_name") == 0) parse_string(pos, config->system_name, sizeof(config->system_name));
            else if (strcmp(key, "nominal_voltage") == 0) config->nominal_voltage = parse_number(pos);
            else if (strcmp(key, "max_grid_import") == 0) config->max_grid_import = parse_number(pos);
            else if (strcmp(key, "max_grid_export") == 0) config->max_grid_export = parse_number(pos);
            else if (strcmp(key, "battery_soc_min") == 0) config->battery_soc_min = parse_number(pos);
            else if (strcmp(key, "battery_soc_max") == 0) config->battery_soc_max = parse_number(pos);
            else if (strcmp(key, "battery_temp_max") == 0) config->battery_temp_max = parse_number(pos);
            else if (strcmp(key, "battery_reserve_soc") == 0) config->battery_reserve_soc = parse_number(pos);
            else if (strcmp(key, "pv_curtail_start") == 0) config->pv_curtail_start = parse_number(pos);
            else if (strcmp(key, "pv_curtail_max") == 0) config->pv_curtail_max = parse_number(pos);
            else if (strcmp(key, "control_interval") == 0) config->control_interval = parse_number(pos);
            else if (strcmp(key, "measurement_interval") == 0) config->measurement_interval = parse_number(pos);
            else if (strcmp(key, "hysteresis") == 0) config->hysteresis = parse_number(pos);
            else if (strcmp(key, "irrigation_mode") == 0) config->irrigation_mode = (irrigation_mode_t)(int)parse_number(pos);
            else if (strcmp(key, "irrigation_power_limit") == 0) config->irrigation_power_limit = parse_number(pos);
            else if (strcmp(key, "ev_charge_power_limit") == 0) config->ev_charge_power_limit = parse_number(pos);
            else if (strcmp(key, "loads") == 0) parse_array_generic(pos, config->loads, &config->load_count, MAX_CONTROLLABLE_LOADS, sizeof(load_definition_t), parse_load_object);
            else if (strcmp(key, "zones") == 0) parse_array_generic(pos, config->zones, &config->zone_count, MAX_IRRIGATION_ZONES, sizeof(irrigation_zone_t), parse_zone_object);
            else if (strcmp(key, "ev_chargers") == 0) parse_array_generic(pos, config->ev_chargers, &config->ev_charger_count, MAX_EV_CHARGERS, sizeof(ev_charger_t), parse_ev_charger_object);
            else if (strcmp(key, "batteries") == 0) parse_batteries_object(pos, config);
            else skip_value(pos);
        }
        skip_whitespace(pos); if (**pos == ',') (*pos)++;
    }

    if (**pos != '}') return CONFIG_PARSE_ERROR;
    (*pos)++;

    LOG_INFO("JSON parsed: %d loads, %d zones, %d EV chargers, %d batteries.", 
        config->load_count, config->zone_count, config->ev_charger_count, config->bank_count);
    return CONFIG_SUCCESS;
}

/* Load configuration from file */
config_error_t config_load(const char* filename, system_config_t* config) {
    if (!filename || !config) return CONFIG_VALIDATION_ERROR;

    FILE* f = fopen(filename, "r");
    if (!f) { LOG_ERROR("Failed to open %s\n", filename); return CONFIG_FILE_NOT_FOUND; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size > CONFIG_MAX_SIZE) { fclose(f); LOG_ERROR("File too large\n"); return CONFIG_FILE_TOO_LARGE; }

    char* buf = malloc(size + 1); 
    if (!buf) { fclose(f); return CONFIG_MEMORY_ERROR; }
    fread(buf, 1, size, f); fclose(f); buf[size] = '\0';

    config_set_defaults(config);
    char* pos = buf;
    config_error_t res = parse_json_object(&pos, config);
    free(buf);
    return res;
}

/* Validate configuration */
config_error_t config_validate(const system_config_t* config) {
    if (!config) return CONFIG_VALIDATION_ERROR;
    if (config->nominal_voltage < 100 || config->nominal_voltage > 600) return CONFIG_VALIDATION_ERROR;
    if (config->battery_soc_min < 0 || config->battery_soc_min > 50) return CONFIG_VALIDATION_ERROR;
    if (config->battery_soc_max < 50 || config->battery_soc_max > 100) return CONFIG_VALIDATION_ERROR;
    if (config->battery_soc_min >= config->battery_soc_max) return CONFIG_VALIDATION_ERROR;
    return CONFIG_SUCCESS;
}

/* Save configuration (partial for brevity) */
config_error_t config_save(const char* filename, const system_config_t* config) {
    if (!filename || !config) return CONFIG_VALIDATION_ERROR;
    FILE* f = fopen(filename, "w"); if(!f){ LOG_ERROR("Cannot open %s\n", filename); return CONFIG_FILE_NOT_FOUND; }

    fprintf(f, "{\n  \"system_name\":\"%s\",\n  \"nominal_voltage\":%.1f\n", config->system_name, config->nominal_voltage);
    fclose(f);
    return CONFIG_SUCCESS;
}
