#include "controller.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

static const char* controller_mode_str[] = {
    "AUTO", "MANUAL", "TEST", "SAFE"
};

int controller_init(system_controller_t* ctrl, const system_config_t* config) {
    if (!ctrl || !config) return -1;
    
    memset(ctrl, 0, sizeof(system_controller_t));
    
    ctrl->mode = CTRL_MODE_AUTO;
    ctrl->control_interval = config->control_interval;
    ctrl->last_control_cycle = time(NULL);
    ctrl->cycle_count = 0;
    
    /* Initialize subsystems */
    if (pv_init(&ctrl->pv_system, config) != 0) {
        LOG_ERROR("Failed to initialize PV controller");
        return -1;
    }
    
    if (battery_init(&ctrl->battery_system, config) != 0) {
        LOG_ERROR("Cannot initialize battery controller");
        return -1;
    }

    if (loads_init(&ctrl->load_manager, config) != 0) {
        LOG_ERROR("Cannot initialize loads controller");
        return -1;
    }
    
    if (agriculture_init(&ctrl->agriculture_system, config) != 0) {
        LOG_ERROR("Cannot initialize irrigation controller");
        return -1;
    }

    if (ev_init(&ctrl->ev_system, config) != 0) {
        LOG_ERROR("Cannot initialize EV Charging controller");
        return -1;
    }
    
    /* Initialize system state */
    ctrl->status.mode = MODE_NORMAL;
    ctrl->status.grid_available = false;
    ctrl->status.grid_stable = true;
    ctrl->status.battery_available = true;
    ctrl->status.pv_available = true;
    ctrl->status.critical_loads_on = true;
    ctrl->status.battery_soc_category = SOC_MEDIUM;
    ctrl->status.alarms = 0;
    ctrl->status.warnings = 0;
    ctrl->status.last_mode_change = time(NULL);
    ctrl->status.uptime = 0;
    
    /* Set control parameters */
    ctrl->grid_import_allowed = true;
    ctrl->grid_export_allowed = false;
    ctrl->grid_import_limit = config->max_grid_import;
    ctrl->grid_export_limit = config->max_grid_export;
    
    ctrl->battery_soc_target = 70.0;  /* Target 70% SOC */
    ctrl->pv_self_consumption_target = 90.0;  /* Target 90% self-consumption */
    
    /* Set safety limits */
    ctrl->max_total_power = 20000.0;  /* 20kW total system limit */
    ctrl->max_battery_temp = 50.0;    /* 50°C battery temp limit */
    ctrl->max_load_power = 15000.0;   /* 15kW load limit */
    
    /* Initialize statistics */
    ctrl->statistics.stats_start_time = time(NULL);
    
    /* Initialize logging */
    ctrl->log_level = 2;  /* WARNING level by default */
    ctrl->verbose = false;
    ctrl->log_file = stdout;

    return 0;
}

int controller_run_cycle(system_controller_t* ctrl) {
    if (!ctrl) return -1;
    
    time_t now = time(NULL);
    
    /* Check if enough time has passed for control cycle */
    if (difftime(now, ctrl->last_control_cycle) < ctrl->control_interval) {
        return -1;
    }
    
    ctrl->last_control_cycle = now;
    ctrl->cycle_count++;
    ctrl->status.uptime = now - ctrl->statistics.stats_start_time;
    
    /* Update measurements from all subsystems */
    controller_update_measurements(ctrl);
    
    /* Check safety limits */
    if (!controller_check_safety_limits(ctrl)) {
        controller_emergency_shutdown(ctrl);
        return -1;
    }
    
    /* Handle any faults */
    controller_handle_faults(ctrl);

    /* Determine system mode based on conditions */
    controller_determine_mode(ctrl);

    /* Run optimization based on current mode */
    controller_optimize_energy_flow(ctrl);

    /* Manage grid connection */
    controller_manage_grid_connection(ctrl);

    /* Update statistics */
    controller_update_statistics(ctrl);

    /* Log status if verbose */
    if (ctrl->cycle_count % 10 == 0) {
        controller_log_status(ctrl);
        battery_log_status(&ctrl->battery_system);
        loads_log_status(&ctrl->load_manager);
    }

    return 0;
}

void controller_update_measurements(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    /* Update PV measurements */
    pv_update_measurements(&ctrl->pv_system, &ctrl->measurements);
    
    /* Update battery measurements */
    battery_update_measurements(&ctrl->battery_system, &ctrl->measurements);
    
    /* Update load measurements */
    loads_update_measurements(&ctrl->load_manager, &ctrl->measurements);
    
    /* Update agriculture measurements */
    agriculture_update_measurements(&ctrl->agriculture_system, &ctrl->measurements);
    
    /* Update EV measurements */
    ev_update_measurements(&ctrl->ev_system, &ctrl->measurements);
    
    /* Update grid measurements (simulated) */
    if (ctrl->status.grid_available) {
        /* Simulate grid conditions */
        ctrl->measurements.grid_voltage = 240.0;
        ctrl->measurements.grid_frequency = 60.0;
        
        /* Calculate grid power based on energy balance */
        double total_generation = ctrl->measurements.pv_power_total;
        double total_consumption = ctrl->measurements.load_power_total +
                                  ctrl->measurements.irrigation_power +
                                  ctrl->measurements.ev_charging_power;
        
        double battery_power = ctrl->measurements.battery_power;
        
        /* Grid power = consumption - generation - battery */
        ctrl->measurements.grid_power = total_consumption - total_generation - battery_power;
        LOG_DEBUG("consumption: %d", total_consumption);
        
        /* Apply grid limits */
        if (ctrl->measurements.grid_power > 0) {  /* Importing */
            if (!ctrl->grid_import_allowed || 
                ctrl->measurements.grid_power > ctrl->grid_import_limit) {
                ctrl->measurements.grid_power = ctrl->grid_import_limit;
            }
        } else {  /* Exporting */
            if (!ctrl->grid_export_allowed || 
                fabs(ctrl->measurements.grid_power) > ctrl->grid_export_limit) {
                ctrl->measurements.grid_power = -ctrl->grid_export_limit;
            }
        }
    } else {
        /* Island mode - no grid connection */
        ctrl->measurements.grid_power = 0;
        ctrl->measurements.grid_voltage = 0;
        ctrl->measurements.grid_frequency = 0;
    }
    
     ctrl->measurements.battery_voltage = 50;
    ctrl->measurements.timestamp = time(NULL);
}

void controller_determine_mode(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    system_mode_t new_mode = ctrl->status.mode;
    
    /* Check grid availability */
    bool grid_was_available = ctrl->status.grid_available;
    ctrl->status.grid_available = (ctrl->measurements.grid_voltage > 200.0 && 
                                  ctrl->measurements.grid_frequency > 59.5 && 
                                  ctrl->measurements.grid_frequency < 60.5);
    
    if (!ctrl->status.grid_available && grid_was_available) {
        /* Grid just failed */
        new_mode = MODE_ISLAND;
        ctrl->status.last_mode_change = time(NULL);
        ctrl->statistics.grid_outage_count++;
        ctrl->statistics.island_count++;
        
        /* Set alarm */
        ctrl->status.alarms |= (1 << ALARM_GRID_FAILURE);
    } else if (ctrl->status.grid_available && !grid_was_available) {
        /* Grid restored */
        new_mode = MODE_NORMAL;
        ctrl->status.last_mode_change = time(NULL);
        
        /* Clear grid alarm */
        ctrl->status.alarms &= ~(1 << ALARM_GRID_FAILURE);
    }
    
    /* Check battery SOC for critical mode */
    if (ctrl->measurements.battery_soc < 20.0 && !ctrl->status.grid_available) {
        new_mode = MODE_CRITICAL;
        ctrl->status.alarms |= (1 << ALARM_BATTERY_LOW_SOC);
    }
    
    /* Update battery SOC category */
    if (ctrl->measurements.battery_soc < 20.0) {
        ctrl->status.battery_soc_category = SOC_CRITICAL;
    } else if (ctrl->measurements.battery_soc < 40.0) {
        ctrl->status.battery_soc_category = SOC_LOW;
    } else if (ctrl->measurements.battery_soc < 70.0) {
        ctrl->status.battery_soc_category = SOC_MEDIUM;
    } else if (ctrl->measurements.battery_soc < 90.0) {
        ctrl->status.battery_soc_category = SOC_HIGH;
    } else {
        ctrl->status.battery_soc_category = SOC_FULL;
    }
    
    ctrl->status.mode = new_mode;
}

void controller_optimize_energy_flow(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    double total_generation = ctrl->measurements.pv_power_total;
    double total_consumption = ctrl->measurements.load_power_total +
                              ctrl->measurements.irrigation_power +
                              ctrl->measurements.ev_charging_power;
    
    double available_power = total_generation;
    double battery_soc = ctrl->measurements.battery_soc;
    bool grid_available = ctrl->status.grid_available;
    
    /* Reset commands */
    memset(&ctrl->commands, 0, sizeof(control_commands_t));
    
    /* Battery management */
    if (total_generation > total_consumption) {
        /* Excess generation - charge battery */
        double excess_power = total_generation - total_consumption;
        battery_manage_charging(&ctrl->battery_system, excess_power, total_consumption);
        
        /* Apply PV curtailment if battery is full */
        if (battery_soc > 90.0 && excess_power > 100.0) {
            double curtail_percent = (battery_soc - 90.0) * 5.0;  /* 5% per % over 90 */
            if (curtail_percent > 50.0) curtail_percent = 50.0;
            pv_apply_curtailment(&ctrl->pv_system, curtail_percent);
            ctrl->commands.pv_curtail = true;
            ctrl->commands.pv_curtail_percent = curtail_percent;
        }
    } else {
        /* Generation deficit - discharge battery if needed */
        double deficit = total_consumption - total_generation;
        battery_manage_discharging(&ctrl->battery_system, deficit, grid_available);
    }
    
    /* Load management */
    loads_manage_shedding(&ctrl->load_manager, available_power, total_consumption, 
                         battery_soc, grid_available);
    
    /* Update load shed commands */
    for (int i = 0; i < MAX_CONTROLLABLE_LOADS; i++) {
        ctrl->commands.load_shed[i] = (ctrl->load_manager.load_states[i] == LOAD_STATE_SHED);
    }
    
    /* Agriculture management */
    agriculture_manage_irrigation(&ctrl->agriculture_system, available_power, 
                                 battery_soc, grid_available);
    
    /* EV charging management */
    ev_manage_charging(&ctrl->ev_system, available_power, battery_soc, grid_available);
    
    /* Grid connection commands */
    ctrl->commands.grid_connect = grid_available && 
                                 (ctrl->status.mode == MODE_NORMAL || 
                                  ctrl->status.mode == MODE_MAINTENANCE);
    
    ctrl->commands.island = !grid_available || 
                           ctrl->status.mode == MODE_ISLAND || 
                           ctrl->status.mode == MODE_CRITICAL;
    
    /* Set battery power setpoint */
    ctrl->commands.battery_setpoint = ctrl->measurements.battery_power;
}

void controller_manage_grid_connection(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    /* In a real system, this would control physical grid-tie inverter */
    /* For now, just update status based on commands */
    
    if (ctrl->commands.grid_connect && !ctrl->commands.island) {
        /* Connect to grid */
        ctrl->status.grid_available = true;
        ctrl->status.grid_stable = true;
    } else if (ctrl->commands.island) {
        /* Island from grid */
        ctrl->status.grid_available = false;
    }
}

void controller_handle_faults(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    uint32_t new_faults = 0;
    
    /* Check PV faults */
    if (pv_detect_faults(&ctrl->pv_system, &ctrl->measurements)) {
        new_faults |= (1 << ALARM_PV_DISCONNECT);
    }
    
    /* Check battery faults */
    if (battery_check_limits(&ctrl->battery_system, &ctrl->measurements)) {
        if (ctrl->battery_system.overtemperature_fault) {
            new_faults |= (1 << ALARM_BATTERY_OVER_TEMP);
        }
    }
    
    /* Check irrigation faults */
    if (agriculture_check_faults(&ctrl->agriculture_system)) {
        new_faults |= (1 << ALARM_IRRIGATION_FAULT);
    }
    
    /* Check EV charger faults */
    if (ev_check_faults(&ctrl->ev_system)) {
        new_faults |= (1 << ALARM_EV_CHARGER_FAULT);
    }
    
    /* Check for overload */
    double total_power = ctrl->measurements.load_power_total +
                        ctrl->measurements.irrigation_power +
                        ctrl->measurements.ev_charging_power;
    
    if (total_power > ctrl->max_total_power) {
        new_faults |= (1 << ALARM_OVERLOAD);
    }
    
    /* Update fault mask and alarms */
    if (new_faults != 0) {
        ctrl->fault_mask |= new_faults;
        ctrl->status.alarms |= new_faults;
        ctrl->last_fault_time = time(NULL);
        
        /* Log fault */
        snprintf(ctrl->last_fault_description, 
                sizeof(ctrl->last_fault_description) - 1,
                "Faults detected: 0x%08X", new_faults);
        
        if (ctrl->log_file) {
            fprintf(ctrl->log_file, "[FAULT] %s: %s\n", 
                    ctime(&ctrl->last_fault_time),
                    ctrl->last_fault_description);
        }
    }
}

void controller_update_statistics(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    double dt = ctrl->control_interval;
    
    /* Update energy statistics */
    ctrl->statistics.pv_energy_total += ctrl->measurements.pv_power_total * dt / 3600.0 / 1000.0;  /* kWh */
    
    if (ctrl->measurements.grid_power > 0) {
        ctrl->statistics.grid_import_total += ctrl->measurements.grid_power * dt / 3600.0 / 1000.0;
    } else {
        ctrl->statistics.grid_export_total += fabs(ctrl->measurements.grid_power) * dt / 3600.0 / 1000.0;
    }
    
    if (ctrl->measurements.battery_power < 0) {
        ctrl->statistics.battery_charge_total += fabs(ctrl->measurements.battery_power) * dt / 3600.0 / 1000.0;
    } else {
        ctrl->statistics.battery_discharge_total += ctrl->measurements.battery_power * dt / 3600.0 / 1000.0;
    }
    
    ctrl->statistics.load_energy_total += ctrl->measurements.load_power_total * dt / 3600.0 / 1000.0;
    ctrl->statistics.irrigation_energy_total += ctrl->measurements.irrigation_power * dt / 3600.0 / 1000.0;
    ctrl->statistics.ev_charge_energy_total += ctrl->measurements.ev_charging_power * dt / 3600.0 / 1000.0;
    
    /* Update event counts */
    if (ctrl->load_manager.shedding_active) {
        ctrl->statistics.load_shed_count++;
    }
}

void controller_log_status(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[26];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("\n=== System Status - %s ===\n", time_str);
    printf("Mode: %s (%d)\n", 
           controller_mode_str[ctrl->mode], ctrl->status.mode);
    printf("Grid: %s (%.0f W)\n", 
           ctrl->status.grid_available ? "Available" : "Island", 
           ctrl->measurements.grid_power);
    printf("PV: %.0f W (%.1f%% of capacity)\n", 
           ctrl->measurements.pv_power_total,
           (ctrl->measurements.pv_power_total / ctrl->pv_system.total_capacity) * 100.0);
    printf("Battery: %.0f W, SOC: %.1f%%, Temp: %.1f°C\n",
           ctrl->measurements.battery_power,
           ctrl->measurements.battery_soc,
           ctrl->measurements.battery_temp);
    printf("Loads: %.0f W (Critical: %.0f W)\n",
           ctrl->measurements.load_power_total,
           ctrl->measurements.load_power_critical);
    printf("Irrigation: %.0f W\n", ctrl->measurements.irrigation_power);
    printf("EV Charging: %.0f W\n", ctrl->measurements.ev_charging_power);
    printf("Cycle Count: %lu\n", ctrl->cycle_count);
    printf("Uptime: %.1f hours\n", ctrl->status.uptime / 3600.0);
    
    if (ctrl->status.alarms != 0) {
        printf("\nACTIVE ALARMS: 0x%08X\n", ctrl->status.alarms);
    }
    
    if (ctrl->status.warnings != 0) {
        printf("ACTIVE WARNINGS: 0x%08X\n", ctrl->status.warnings);
    }
    
    printf("===============================\n");
}

void controller_emergency_shutdown(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    printf("[EMERGENCY] Safety limits exceeded! Initiating shutdown...\n");
    
    /* Stop all subsystems */
    for (int i = 0; i < MAX_CONTROLLABLE_LOADS; i++) {
        ctrl->commands.load_shed[i] = true;
    }
    
    agriculture_emergency_stop(&ctrl->agriculture_system);
    
    for (int i = 0; i < ctrl->ev_system.charger_count; i++) {
        ev_pause_charging(&ctrl->ev_system, i);
    }
    
    pv_apply_curtailment(&ctrl->pv_system, 100.0);  /* Full curtailment */
    
    /* Disconnect from grid */
    ctrl->commands.grid_connect = false;
    ctrl->commands.island = true;
    
    /* Set to safe mode */
    ctrl->mode = CTRL_MODE_SAFE;
    ctrl->status.mode = MODE_EMERGENCY;
    
    /* Log emergency */
    if (ctrl->log_file) {
        time_t now = time(NULL);
        fprintf(ctrl->log_file, "[EMERGENCY] System shutdown at %s\n", ctime(&now));
    }
}

bool controller_check_safety_limits(system_controller_t* ctrl) {
    if (!ctrl) return true;
    
    /* Check battery temperature */
    if (ctrl->measurements.battery_temp > ctrl->max_battery_temp) {
        printf("[SAFETY] Battery temperature exceeded: %.1f°C > %.1f°C\n",
               ctrl->measurements.battery_temp, ctrl->max_battery_temp);
        return false;
    }
    
    /* Check total system power */
    double total_power = ctrl->measurements.load_power_total +
                        ctrl->measurements.irrigation_power +
                        ctrl->measurements.ev_charging_power;
    
    if (total_power > ctrl->max_total_power) {
        printf("[SAFETY] Total power exceeded: %.0f W > %.0f W\n",
               total_power, ctrl->max_total_power);
        return false;
    }
    
    /* Check load power */
    if (ctrl->measurements.load_power_total > ctrl->max_load_power) {
        printf("[SAFETY] Load power exceeded: %.0f W > %.0f W\n",
               ctrl->measurements.load_power_total, ctrl->max_load_power);
        return false;
    }
    
    // Check battery voltage limits
    if (ctrl->measurements.battery_voltage < 20.0 || 
        ctrl->measurements.battery_voltage > 80.0) {
        printf("[SAFETY] Battery voltage out of range: %.1f V\n",
               ctrl->measurements.battery_voltage);
        return false;
    }
    
    return true;
}

void controller_cleanup(system_controller_t* ctrl) {
    if (!ctrl) return;
    
    // Reset all commands for safety
    memset(&ctrl->commands, 0, sizeof(control_commands_t));
    
    // Log shutdown
    printf("Controller shutdown complete.\n");
}
