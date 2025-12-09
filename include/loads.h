#ifndef LOADS_H
#define LOADS_H

#include "core.h"

/* Load control states */
typedef enum {
    LOAD_STATE_OFF = 0,
    LOAD_STATE_ON,
    LOAD_STATE_SHED,
    LOAD_STATE_DEFERRED,
    LOAD_STATE_FAULT
} load_state_t;

/* Load scheduling modes */
typedef enum {
    SCHEDULE_NONE = 0,
    SCHEDULE_TIME_OF_DAY,
    SCHEDULE_SOC_BASED,
    SCHEDULE_POWER_BASED
} schedule_mode_t;

/* Load management context */
typedef struct {
    load_definition_t loads[MAX_CONTROLLABLE_LOADS];
    load_state_t load_states[MAX_CONTROLLABLE_LOADS];
    int load_count;
    
    /* Load groups by priority */
    double priority_power[5];  // Total power per priority level
    int priority_count[5];     // Count of loads per priority
    
    /* Shedding control */
    bool shedding_active;
    double shed_power_target;
    time_t shedding_start_time;
    
    /* Deferrable loads */
    double deferred_power;
    time_t next_deferrable_start;
    
    /* Statistics */
    double total_energy_consumed;
    uint32_t shed_event_count;
    uint32_t restart_event_count;
    
    /* Timing constraints */
    double min_shed_duration;
    double max_shed_duration;
    double load_rotation_interval;
} load_manager_t;

/* Function prototypes */
int loads_init(load_manager_t* lm, const system_config_t* config);
void loads_update_measurements(load_manager_t* lm, system_measurements_t* measurements);
bool loads_manage_shedding(load_manager_t* lm, double available_power, double total_load,
    double battery_soc, bool grid_available);
void loads_restore_shed(load_manager_t* lm, double available_power);
void loads_rotate_shedding(load_manager_t* lm);
void loads_prioritize_deferrable(load_manager_t* lm, double excess_power);
bool loads_check_timing_constraints(const load_manager_t* lm, int load_index);
void loads_log_status(const load_manager_t* lm);
double loads_calculate_power_needed(const load_manager_t* lm);
bool loads_can_shed_load(const load_manager_t* lm, int load_index, double available_power);

#endif /* LOADS_H */
