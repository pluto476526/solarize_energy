#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdio.h>
#include "core.h"
#include "pv.h"
#include "battery.h"
#include "loads.h"
#include "agriculture.h"
#include "ev.h"

/* Controller operating modes */
typedef enum {
    CTRL_MODE_AUTO = 0,
    CTRL_MODE_MANUAL,
    CTRL_MODE_TEST,
    CTRL_MODE_SAFE
} controller_mode_t;

/* Controller context */
typedef struct {
    controller_mode_t mode;
    
    /* Subsystem instances */
    pv_system_t pv_system;
    battery_system_t battery_system;
    load_manager_t load_manager;
    agriculture_system_t agriculture_system;
    ev_charging_system_t ev_system;
    
    /* System state */
    system_measurements_t measurements;
    system_status_t status;
    control_commands_t commands;
    system_statistics_t statistics;
    
    /* Control parameters */
    double control_interval;
    time_t last_control_cycle;
    uint64_t cycle_count;
    
    /* Grid connection */
    bool grid_import_allowed;
    bool grid_export_allowed;
    double grid_import_limit;
    double grid_export_limit;
    
    /* Optimization targets */
    double battery_soc_target;
    double pv_self_consumption_target;
    
    /* Safety limits */
    double max_total_power;
    double max_battery_temp;
    double max_load_power;
    
    /* Logging and monitoring */
    FILE* log_file;
    int log_level;
    bool verbose;
    char* name;
    
    /* Fault handling */
    uint32_t fault_mask;
    time_t last_fault_time;
    char last_fault_description[128];
} system_controller_t;

/* Function prototypes */
int controller_init(system_controller_t* ctrl, const system_config_t* config);
int controller_run_cycle(system_controller_t* ctrl);
void controller_update_measurements(system_controller_t* ctrl);
void controller_determine_mode(system_controller_t* ctrl);
void controller_optimize_energy_flow(system_controller_t* ctrl);
void controller_manage_grid_connection(system_controller_t* ctrl);
void controller_handle_faults(system_controller_t* ctrl);
void controller_update_statistics(system_controller_t* ctrl);
void controller_log_status(system_controller_t* ctrl);
void controller_emergency_shutdown(system_controller_t* ctrl);
bool controller_check_safety_limits(system_controller_t* ctrl);
void controller_cleanup(system_controller_t* ctrl);

#endif /* CONTROLLER_H */
