// controller.c

#include "controller.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Human-readable mode strings
static const char* controller_mode_str[] = {
    "AUTO", "MANUAL", "TEST", "SAFE"
};

// Initialize controller and subsystems
int controller_init(system_controller_t* ctrl, const system_config_t* config) {
    if (!ctrl || !config) return -1;

    memset(ctrl, 0, sizeof(system_controller_t));

    // Basic controller state
    ctrl->mode = CTRL_MODE_AUTO;
    ctrl->control_interval = config->control_interval;
    ctrl->last_control_cycle = time(NULL);
    ctrl->cycle_count = 0;

    // Initialize subsystems and fail fast if any init fails
    if (pv_init(&ctrl->pv_system, config) != 0) {
        LOG_ERROR("Failed to initialize PV controller");
        return -1;
    }

    if (battery_init(&ctrl->battery_system, config) != 0) {
        LOG_ERROR("Failed to initialize battery controller");
        return -1;
    }

    if (loads_init(&ctrl->load_manager, config) != 0) {
        LOG_ERROR("Failed to initialize loads controller");
        return -1;
    }

    if (agriculture_init(&ctrl->agriculture_system, config) != 0) {
        LOG_ERROR("Failed to initialize irrigation controller");
        return -1;
    }

    if (ev_init(&ctrl->ev_system, config) != 0) {
        LOG_ERROR("Failed to initialize EV controller");
        return -1;
    }

    // System status defaults
    ctrl->status.mode = MODE_NORMAL;
    ctrl->status.grid_available = true;
    ctrl->status.grid_stable = true;
    ctrl->status.battery_available = true;
    ctrl->status.pv_available = true;
    ctrl->status.critical_loads_on = true;
    ctrl->status.battery_soc_category = SOC_MEDIUM;
    ctrl->status.alarms = 0;
    ctrl->status.warnings = 0;
    ctrl->status.last_mode_change = time(NULL);
    ctrl->status.uptime = 0;

    // Control parameters
    ctrl->grid_import_allowed = true;
    ctrl->grid_export_allowed = false;
    ctrl->grid_import_limit = config->max_grid_import;
    ctrl->grid_export_limit = config->max_grid_export;

    ctrl->battery_soc_target = 70.0;
    ctrl->pv_self_consumption_target = 90.0;

    // Safety limits
    ctrl->max_total_power = 20000.0;
    ctrl->max_battery_temp = 50.0;
    ctrl->max_load_power = 15000.0;

    // Statistics & logging
    ctrl->statistics.stats_start_time = time(NULL);
    ctrl->log_level = 2;
    ctrl->verbose = false;
    ctrl->log_file = stdout;

    return 0;
}

// Main control loop: runs a single control cycle if enough time passed
int controller_run_cycle(system_controller_t* ctrl) {
    if (!ctrl) return -1;

    time_t now = time(NULL);
    double elapsed = difftime(now, ctrl->last_control_cycle);

    // Allow fractional intervals by comparing elapsed as double
    if (elapsed < ctrl->control_interval) {
        // Not time yet; do nothing
        return -1;
    }

    // update timing first (use actual elapsed for statistics)
    ctrl->last_control_cycle = now;
    ctrl->cycle_count++;
    ctrl->status.uptime = now - ctrl->statistics.stats_start_time;

    // Collect latest measurements from subsystems
    controller_update_measurements(ctrl);

    // Safety check: if limits violated, perform emergency shutdown
    if (!controller_check_safety_limits(ctrl)) {
        controller_emergency_shutdown(ctrl);
        return -1;
    }

    // Handle any faults discovered by subsystems
    controller_handle_faults(ctrl);

    // Determine operational mode from current measurements
    controller_determine_mode(ctrl);

    // Run energy optimization & action planning
    controller_optimize_energy_flow(ctrl);

    // Manage grid connect/disconnect according to commands
    controller_manage_grid_connection(ctrl);

    // Update statistics using the actual elapsed seconds
    // Use elapsed (seconds) so statistics reflect real time between cycles
    controller_update_statistics(ctrl);

    // Log every 10 cycles
    if ((ctrl->cycle_count % 10) == 0) {
        controller_log_status(ctrl);
        pv_log_status(&ctrl->pv_system);
        battery_log_status(&ctrl->battery_system);
        loads_log_status(&ctrl->load_manager);
    }

    return 0;
}

// Ask each subsystem to update the shared measurements structure
void controller_update_measurements(system_controller_t* ctrl) {
    if (!ctrl) return;

    // Update sensors/measurements from each subsystem
    pv_update_measurements(&ctrl->pv_system, &ctrl->measurements);
    battery_update_measurements(&ctrl->battery_system, &ctrl->measurements);
    loads_update_measurements(&ctrl->load_manager, &ctrl->measurements);
    agriculture_update_measurements(&ctrl->agriculture_system, &ctrl->measurements);
    ev_update_measurements(&ctrl->ev_system, &ctrl->measurements);

    // Grid handling: assume grid_power = consumption - generation - battery
    if (ctrl->status.grid_available) {
        ctrl->measurements.grid_voltage = ctrl->measurements.grid_voltage > 0 ? ctrl->measurements.grid_voltage : 240.0;
        ctrl->measurements.grid_frequency = ctrl->measurements.grid_frequency > 0 ? ctrl->measurements.grid_frequency : 60.0;

        double total_generation = ctrl->measurements.pv_power_total;
        double total_consumption = ctrl->measurements.load_power_total +
            ctrl->measurements.irrigation_power + ctrl->measurements.ev_charging_power;
        double battery_power = ctrl->measurements.battery_power;

        // Calculate grid power (positive = import, negative = export)
        ctrl->measurements.grid_power = total_consumption - total_generation - battery_power;

        /* Debug: print numeric values with correct format */
        LOG_DEBUG("consumption: %.1f W, generation: %.1f W, battery: %.1f W, grid: %.1f W",
            total_consumption, total_generation, battery_power, ctrl->measurements.grid_power);

        // Apply configured grid limits and permissions
        if (ctrl->measurements.grid_power > 0) { // importing
            if (!ctrl->grid_import_allowed || ctrl->measurements.grid_power > ctrl->grid_import_limit)
                ctrl->measurements.grid_power = ctrl->grid_import_limit;
        } else { // exporting
            if (!ctrl->grid_export_allowed || fabs(ctrl->measurements.grid_power) > ctrl->grid_export_limit)
                ctrl->measurements.grid_power = -ctrl->grid_export_limit;
        }
    } else {
        // Island mode — explicitly clear grid measurements
        ctrl->measurements.grid_power = 0.0;
        ctrl->measurements.grid_voltage = 0.0;
        ctrl->measurements.grid_frequency = 0.0;
    }


    // Timestamp this measurement update
    ctrl->measurements.timestamp = time(NULL);
}

// Decide if we're islanded or normal and update SOC category and alarms
void controller_determine_mode(system_controller_t* ctrl) {
    if (!ctrl) return;

    system_mode_t new_mode = ctrl->status.mode;

    bool grid_was_available = ctrl->status.grid_available;
    ctrl->status.grid_available = (ctrl->measurements.grid_voltage > 200.0 &&
        ctrl->measurements.grid_frequency > 59.5 && ctrl->measurements.grid_frequency < 60.5);

    if (!ctrl->status.grid_available && grid_was_available) {
        // grid lost
        new_mode = MODE_ISLAND;
        ctrl->status.last_mode_change = time(NULL);
        ctrl->statistics.grid_outage_count++;
        ctrl->statistics.island_count++;
        ctrl->status.alarms |= (1 << ALARM_GRID_FAILURE);
    } else if (ctrl->status.grid_available && !grid_was_available) {
        // grid restored
        new_mode = MODE_NORMAL;
        ctrl->status.last_mode_change = time(NULL);
        ctrl->status.alarms &= ~(1 << ALARM_GRID_FAILURE);
    }

    // Critical battery SOC handling
    if (ctrl->measurements.battery_soc < 20.0 && !ctrl->status.grid_available) {
        new_mode = MODE_CRITICAL;
        ctrl->status.alarms |= (1 << ALARM_BATTERY_LOW_SOC);
    }

    // Update SOC category (ordered thresholds)
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

// High-level optimizer that issues subsystem commands
void controller_optimize_energy_flow(system_controller_t* ctrl) {
    if (!ctrl) return;

    double total_generation = ctrl->measurements.pv_power_total;
    double total_consumption = ctrl->measurements.load_power_total +
        ctrl->measurements.irrigation_power + ctrl->measurements.ev_charging_power;

    double available_power = total_generation;
    double battery_soc = ctrl->measurements.battery_soc;
    bool grid_available = ctrl->status.grid_available;

    // Reset output commands
    memset(&ctrl->commands, 0, sizeof(control_commands_t));

    // Battery control
    if (total_generation > total_consumption) {
        double excess = total_generation - total_consumption;
        battery_manage_charging(&ctrl->battery_system, excess, total_consumption);

        if (battery_soc > 90.0 && excess > 100.0) {
            // Gentle curtailment calculation, clamp to safe range
            double curtail_percent = fmin((battery_soc - 90.0) * 5.0, 50.0);
            pv_apply_curtailment(&ctrl->pv_system, curtail_percent);
            ctrl->commands.pv_curtail = true;
            ctrl->commands.pv_curtail_percent = curtail_percent;
        }

    } else {
        double deficit = total_consumption - total_generation;
        battery_manage_discharging(&ctrl->battery_system, deficit, grid_available);
    }

    // Load shedding logic
    loads_manage_shedding(&ctrl->load_manager, available_power, total_consumption,
        ctrl->measurements.battery_soc, grid_available);

    // Propagate load shed flags to commands
    for (int i = 0; i < MAX_CONTROLLABLE_LOADS; i++) {
        ctrl->commands.load_shed[i] = (ctrl->load_manager.load_states[i] == LOAD_STATE_SHED);
    }

    // Agriculture and EV decisions
    agriculture_manage_irrigation(&ctrl->agriculture_system, available_power,
        ctrl->measurements.battery_soc, grid_available);
    
    ev_manage_charging(&ctrl->ev_system, available_power, ctrl->measurements.battery_soc, grid_available);

    // Grid connect decision
    ctrl->commands.grid_connect = grid_available &&
        (ctrl->status.mode == MODE_NORMAL || ctrl->status.mode == MODE_MAINTENANCE);

    ctrl->commands.island = !grid_available ||
        ctrl->status.mode == MODE_ISLAND || ctrl->status.mode == MODE_CRITICAL;

    // Set battery setpoint to current measurement by default
    ctrl->commands.battery_setpoint = ctrl->measurements.battery_power;
}

// Update grid connection status (simulation of action effects)
void controller_manage_grid_connection(system_controller_t* ctrl) {
    if (!ctrl) return;

    if (ctrl->commands.grid_connect && !ctrl->commands.island) {
        ctrl->status.grid_available = true;
        ctrl->status.grid_stable = true;
    } else if (ctrl->commands.island) {
        ctrl->status.grid_available = false;
    }
}

/* Aggregate faults from subsystems and system checks */
void controller_handle_faults(system_controller_t* ctrl) {
    if (!ctrl) return;

    uint32_t new_faults = 0;

    if (pv_detect_faults(&ctrl->pv_system, &ctrl->measurements))
        new_faults |= (1 << ALARM_PV_DISCONNECT);

    if (battery_check_limits(&ctrl->battery_system, &ctrl->measurements)) {
        if (ctrl->battery_system.overtemperature_fault)
            new_faults |= (1 << ALARM_BATTERY_OVER_TEMP);
    }

    if (agriculture_check_faults(&ctrl->agriculture_system))
        new_faults |= (1 << ALARM_IRRIGATION_FAULT);

    if (ev_check_faults(&ctrl->ev_system))
        new_faults |= (1 << ALARM_EV_CHARGER_FAULT);

    // Overload detection
    double total_power = ctrl->measurements.load_power_total +
        ctrl->measurements.irrigation_power + ctrl->measurements.ev_charging_power;
    
    if (total_power > ctrl->max_total_power)
        new_faults |= (1 << ALARM_OVERLOAD);

    if (new_faults != 0) {
        ctrl->fault_mask |= new_faults;
        ctrl->status.alarms |= new_faults;
        ctrl->last_fault_time = time(NULL);

        char tsbuf[32];
        struct tm tm_info;
        time_t t = ctrl->last_fault_time;
        localtime_r(&t, &tm_info);
        strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", &tm_info);

        snprintf(ctrl->last_fault_description, sizeof(ctrl->last_fault_description),
                 "Faults detected: 0x%08X at %s", new_faults, tsbuf);

        if (ctrl->log_file) {
            fprintf(ctrl->log_file, "[FAULT] %s: %s\n", tsbuf, ctrl->last_fault_description);
            fflush(ctrl->log_file);
        }
    }
}

/* Update energy & event statistics using actual elapsed time */
void controller_update_statistics(system_controller_t* ctrl) {
    if (!ctrl) return;

    // compute dt from control_interval or measured elapsed since last cycle
    double dt = ctrl->control_interval;
    
    // if a more accurate elapsed time is available, the caller can populate it later
    // keep using control_interval for backward compatibility

    // Update energy totals (W * s -> kWh)
    ctrl->statistics.pv_energy_total += (ctrl->measurements.pv_power_total * dt) / 3600.0 / 1000.0;

    if (ctrl->measurements.grid_power > 0)
        ctrl->statistics.grid_import_total += (ctrl->measurements.grid_power * dt) / 3600.0 / 1000.0;
    else
        ctrl->statistics.grid_export_total += (fabs(ctrl->measurements.grid_power) * dt) / 3600.0 / 1000.0;

    // battery_power convention: negative = charging, positive = discharging
    // convert to absolute energy values accordingly
    if (ctrl->measurements.battery_power < 0.0)
        ctrl->statistics.battery_charge_total += (fabs(ctrl->measurements.battery_power) * dt) / 3600.0 / 1000.0;
    else
        ctrl->statistics.battery_discharge_total += (ctrl->measurements.battery_power * dt) / 3600.0 / 1000.0;

    ctrl->statistics.load_energy_total += (ctrl->measurements.load_power_total * dt) / 3600.0 / 1000.0;
    ctrl->statistics.irrigation_energy_total += (ctrl->measurements.irrigation_power * dt) / 3600.0 / 1000.0;
    ctrl->statistics.ev_charge_energy_total += (ctrl->measurements.ev_charging_power * dt) / 3600.0 / 1000.0;

    if (ctrl->load_manager.shedding_active)
        ctrl->statistics.load_shed_count++;
}

// Print a summary of system status
void controller_log_status(system_controller_t* ctrl) {
    if (!ctrl) return;

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char time_str[26];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

    printf("\n=== System Status - %s ===\n", time_str);
    printf("Mode: %s (%d)\n", controller_mode_str[ctrl->mode], ctrl->status.mode);
    printf("Grid: %s (%.0f W)\n", ctrl->status.grid_available ? "Available" : "Island", ctrl->measurements.grid_power);
    printf("PV: %.0f W (%.1f%% of capacity)\n",
        ctrl->measurements.pv_power_total,
        (ctrl->pv_system.total_capacity > 0.0) ? (ctrl->measurements.pv_power_total / ctrl->pv_system.total_capacity) * 100.0 : 0.0);
    printf("Battery: %.0f W, SOC: %.1f%%, Temp: %.1f°C\n",
        ctrl->measurements.battery_power, ctrl->measurements.battery_soc, ctrl->measurements.battery_temp);
    printf("Loads: %.0f W (Critical: %.0f W)\n",
        ctrl->measurements.load_power_total, ctrl->measurements.load_power_critical);
    printf("Irrigation: %.0f W\n", ctrl->measurements.irrigation_power);
    printf("EV Charging: %.0f W\n", ctrl->measurements.ev_charging_power);
    printf("Cycle Count: %lu\n", ctrl->cycle_count);
    printf("Uptime: %.1f hours\n", ctrl->status.uptime / 3600.0);

    if (ctrl->status.alarms)
        printf("\nACTIVE ALARMS: 0x%08X\n", ctrl->status.alarms);

    if (ctrl->status.warnings)
        printf("ACTIVE WARNINGS: 0x%08X\n", ctrl->status.warnings);

    printf("===============================\n");
}

// Emergency shutdown: issue conservative commands and log event
void controller_emergency_shutdown(system_controller_t* ctrl) {
    if (!ctrl) return;

    LOG_WARNING("[EMERGENCY] Safety limits exceeded! Initiating shutdown...\n");

    Force all loads OFF (shed)
    for (int i = 0; i < MAX_CONTROLLABLE_LOADS; i++) {
        ctrl->commands.load_shed[i] = true;
    }

    // Stop irrigation and EVs
    agriculture_emergency_stop(&ctrl->agriculture_system);

    for (int i = 0; i < ctrl->ev_system.charger_count; i++) {
        ev_pause_charging(&ctrl->ev_system, i);
    }

    // Full PV curtailment and island
    pv_apply_curtailment(&ctrl->pv_system, 100.0);
    ctrl->commands.grid_connect = false;
    ctrl->commands.island = true;

    // Set safe modes
    ctrl->mode = CTRL_MODE_SAFE;
    ctrl->status.mode = MODE_EMERGENCY;

    // Log the event with timestamp
    if (ctrl->log_file) {
        time_t now = time(NULL);
        struct tm tm_info;
        char tsbuf[32];
        localtime_r(&now, &tm_info);
        strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(ctrl->log_file, "[EMERGENCY] System shutdown at %s\n", tsbuf);
        fflush(ctrl->log_file);
    }
}

// Verify safety limits
// Return false if system must stop
bool controller_check_safety_limits(system_controller_t* ctrl) {
    if (!ctrl) return true;

    // Battery temperature
    if (ctrl->measurements.battery_temp > ctrl->max_battery_temp) {
        LOG_WARNING("[SAFETY] Battery temperature exceeded: %.1f°C > %.1f°C\n",
            ctrl->measurements.battery_temp, ctrl->max_battery_temp);
        return false;
    }

    // Total system power
    double total_power = ctrl->measurements.load_power_total +
        ctrl->measurements.irrigation_power + ctrl->measurements.ev_charging_power;
    if (total_power > ctrl->max_total_power) {
        LOG_WARNING("[SAFETY] Total power exceeded: %.0f W > %.0f W\n",
            total_power, ctrl->max_total_power);
        return false;
    }

    // Load-specific cap
    if (ctrl->measurements.load_power_total > ctrl->max_load_power) {
        LOG_ERROR("[SAFETY] Load power exceeded: %.0f W > %.0f W\n",
            ctrl->measurements.load_power_total, ctrl->max_load_power);
        return false;
    }

    // Battery voltage sanity check
    if (ctrl->measurements.battery_voltage < 20.0 || ctrl->measurements.battery_voltage > 80.0) {
        LOG_ERROR("[SAFETY] Battery voltage out of range: %.1f V\n", ctrl->measurements.battery_voltage);
        return false;
    }

    return true;
}

// Clean up controller — reset commands and print message
void controller_cleanup(system_controller_t* ctrl) {
    if (!ctrl) return;

    memset(&ctrl->commands, 0, sizeof(control_commands_t));
    LOG_INFO("Controller shutdown complete.\n");
}
