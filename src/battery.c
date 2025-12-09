#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "battery.h"
#include "logging.h"

// Battery chemistry parameters
typedef struct {
    float nominal_voltage;
    float max_charge_voltage;
    float float_voltage;
    float equalize_voltage;
    float min_discharge_voltage;
    float max_charge_current_c;    // As multiple of C-rate
    float max_discharge_current_c;
    float temperature_coeff;       // Voltage temperature coefficient mV/°C
} battery_chemistry_t;

// Common battery chemistries
// static const battery_chemistry_t battery_chemistries[] = {
//     {3.2, 3.65, 3.4, 3.65, 2.5, 0.5, 1.0, -0.3},    // Lithium Iron Phosphate (LFP)
//     {3.7, 4.2, 4.05, 4.2, 3.0, 0.7, 1.0, -0.5},     // Lithium NMC
//     {2.0, 2.45, 2.25, 2.58, 1.75, 0.2, 0.5, -4.0},  // Lead Acid (Flooded)
//     {2.0, 2.4, 2.27, 2.5, 1.75, 0.3, 0.8, -3.0}     // Lead Acid (AGM)
// };

static const char* battery_state_str[] = {
    "IDLE", "CHARGING", "DISCHARGING", "FLOAT", "EQUALIZE", "FAULT", "MAINTENANCE"
};

static const char* charge_stage_str[] = {
    "BULK", "ABSORPTION", "FLOAT", "EQUALIZE"
};

int battery_init(battery_system_t* bat, const system_config_t* config) {
    LOG_DEBUG("Initializing batteries 1");
    if (!bat || !config) return -1;

    LOG_DEBUG("Initializing batteries");
    
    memset(bat, 0, sizeof(battery_system_t));
    
    bat->state = BATTERY_STATE_IDLE;
    bat->charge_stage = CHARGE_BULK;
    
    // Initialize battery banks
    for (int i = 0; i < MAX_BATTERY_BANKS; i++) {
        snprintf(bat->banks[i].bank_id, sizeof(bat->banks[i].bank_id), "BANK_%d", i + 1);
        bat->banks[i].capacity_kwh = 10.0;  // Default 10kWh per bank
        bat->banks[i].max_charge_rate = 5000.0;  // 5kW charge
        bat->banks[i].max_discharge_rate = 5000.0; // 5kW discharge
        bat->banks[i].voltage_nominal = 48.0;
        bat->banks[i].temp_nominal = 25.0;
        bat->banks[i].cycle_count = 0;
        bat->banks[i].last_full_charge = 0;
        
        bat->capacity_nominal += bat->banks[i].capacity_kwh * 1000.0;  // Convert to Wh
        LOG_DEBUG("BANK %s", bat->banks[i].bank_id);
    }
    
    bat->active_bank_count = MAX_BATTERY_BANKS;
    LOG_DEBUG("BANK_%i", bat->active_bank_count);
    bat->capacity_remaining = bat->capacity_nominal * 0.5;  // Start at 50% SOC
    bat->soc_estimated = 50.0;
    bat->soc_smoothed = 50.0;
    bat->health_percentage = 100.0;
    bat->internal_resistance = 0.01;
    
    // Set default limits
    bat->max_charge_current = 100.0;  // Amps
    bat->max_discharge_current = 100.0;
    bat->max_charge_power = 4800.0;   // Watts
    bat->max_discharge_power = 4800.0;
    
    // Timing parameters
    bat->absorption_duration = 120.0;  // 2 hours
    bat->float_duration = 7200.0;      // 2 hours
    
    // Initialize thermal management
    bat->temperature = 25.0;
    bat->temperature_ambient = 25.0;
    bat->cooling_active = false;
    bat->heating_active = false;

    LOG_DEBUG("Battery init completed");
    return 0;
}

void battery_update_measurements(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return;
    
    /* Update from hardware measurements */
    bat->temperature = measurements->battery_temp;
    
    /* Calculate power from voltage and current */
    measurements->battery_power = measurements->battery_voltage * measurements->battery_current;
    
    /* Update SOC */
    battery_calculate_soc(bat, measurements);
    
    /* Check for safety limits */
    battery_check_limits(bat, measurements);
    
    /* Update thermal management */
    battery_thermal_management(bat);
}

double battery_calculate_soc(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return 0;
    
    static double accumulated_current = 0;
    static time_t last_update = 0;
    time_t now = time(NULL);
    
    /* Coulomb counting method */
    if (last_update > 0) {
        double dt = difftime(now, last_update);
        accumulated_current += measurements->battery_current * dt / 3600.0;  /* Ah */
    }
    last_update = now;
    
    bat->soc_coulomb_counting = 100.0 * (1.0 - accumulated_current / 
                                        (bat->capacity_nominal / measurements->battery_voltage));
    
    /* Voltage-based SOC estimation */
    /* This is chemistry-specific - simplified for LFP */
    double voltage_ratio = measurements->battery_voltage / 
                          (bat->banks[0].voltage_nominal * 
                           (bat->active_bank_count > 0 ? bat->active_bank_count : 1));
    
    /* Simplified voltage-SOC curve for LFP */
    double soc_voltage = 0;
    if (voltage_ratio > 3.4) soc_voltage = 100.0;
    else if (voltage_ratio > 3.3) soc_voltage = 80.0 + (voltage_ratio - 3.3) * 200.0;
    else if (voltage_ratio > 3.2) soc_voltage = 20.0 + (voltage_ratio - 3.2) * 600.0;
    else if (voltage_ratio > 3.1) soc_voltage = 10.0 + (voltage_ratio - 3.1) * 100.0;
    else soc_voltage = 10.0 * voltage_ratio / 3.1;
    
    bat->soc_voltage_based = soc_voltage;
    
    /* Combine methods with weighting */
    /* Use coulomb counting during high currents, voltage at rest */
    double weight_coulomb = fabs(measurements->battery_current) > 5.0 ? 0.7 : 0.3;
    bat->soc_estimated = weight_coulomb * bat->soc_coulomb_counting + 
                        (1.0 - weight_coulomb) * bat->soc_voltage_based;
    
    /* Apply exponential smoothing */
    double alpha = 0.1;  /* Smoothing factor */
    bat->soc_smoothed = alpha * bat->soc_estimated + (1.0 - alpha) * bat->soc_smoothed;
    
    /* Clamp SOC between 0-100% */
    if (bat->soc_smoothed < 0) bat->soc_smoothed = 0;
    if (bat->soc_smoothed > 100) bat->soc_smoothed = 100;
    
    measurements->battery_soc = bat->soc_smoothed;
    bat->capacity_remaining = bat->capacity_nominal * bat->soc_smoothed / 100.0;
    
    return bat->soc_smoothed;
}

void battery_manage_charging(battery_system_t* bat, double available_power, double load_power) {
    if (!bat) return;
    
    double max_charge_power = battery_calculate_max_charge(bat);
    double excess_power = available_power - load_power;
    
    /* Determine if we should charge */
    bool should_charge = false;
    
    if (excess_power > 100.0) {  /* At least 100W excess */
        if (bat->soc_smoothed < 95.0) {  /* Don't charge if nearly full */
            should_charge = true;
        }
    }
    
    if (should_charge) {
        bat->state = BATTERY_STATE_CHARGING;
        
        /* Calculate charge power - limited by available excess and battery limits */
        double charge_power = excess_power;
        if (charge_power > max_charge_power) {
            charge_power = max_charge_power;
        }
        
        /* Apply charge algorithm based on SOC and charge stage */
        if (bat->soc_smoothed < 85.0) {
            bat->charge_stage = CHARGE_BULK;
            /* Constant current charge */
        } else if (bat->soc_smoothed < 95.0) {
            bat->charge_stage = CHARGE_ABSORPTION;
            /* Constant voltage charge */
            if (bat->absorption_start_time == 0) {
                bat->absorption_start_time = time(NULL);
            }
            
            /* Reduce charge power during absorption */
            double time_in_absorption = difftime(time(NULL), bat->absorption_start_time);
            double absorption_factor = 1.0 - (time_in_absorption / bat->absorption_duration);
            if (absorption_factor < 0.1) absorption_factor = 0.1;
            
            charge_power *= absorption_factor;
        } else {
            bat->charge_stage = CHARGE_FLOAT;
            /* Float charge - maintenance charging */
            charge_power = max_charge_power * 0.05;  /* 5% of max for float */
        }
        
        /* Update statistics */
        bat->total_charge_energy += charge_power / 3600.0;  /* Wh per second */
    } else {
        bat->state = BATTERY_STATE_IDLE;
        bat->absorption_start_time = 0;  /* Reset absorption timer */
    }
}

void battery_manage_discharging(battery_system_t* bat, double load_power, bool grid_available) {
    if (!bat) return;
    
    double max_discharge_power = battery_calculate_max_discharge(bat);
    
    /* Determine if we should discharge */
    bool should_discharge = false;
    
    if (!grid_available) {
        /* Island mode - discharge to support loads */
        should_discharge = true;
    } else if (load_power > 0 && bat->soc_smoothed > 30.0) {
        /* Grid-connected with sufficient SOC */
        /* Could implement peak shaving or time-based optimization here */
        should_discharge = false;  /* Default: don't discharge when grid available */
    }
    
    if (should_discharge) {
        bat->state = BATTERY_STATE_DISCHARGING;
        
        /* Calculate discharge power - limited by load and battery limits */
        double discharge_power = load_power;
        if (discharge_power > max_discharge_power) {
            discharge_power = max_discharge_power;
        }
        
        /* Respect minimum SOC */
        double soc_after_discharge = bat->soc_smoothed - 
                                   (discharge_power * 100.0) / (bat->capacity_nominal * 3600.0);
        
        if (soc_after_discharge < 20.0) {
            discharge_power *= 0.5;  /* Reduce discharge as SOC approaches minimum */
        }
        
        /* Update statistics */
        bat->total_discharge_energy += discharge_power / 3600.0;  /* Wh per second */
    } else {
        bat->state = BATTERY_STATE_IDLE;
    }
}

double battery_calculate_max_charge(battery_system_t* bat) {
    if (!bat) return 0;
    
    double max_charge = bat->max_charge_power;
    
    /* Reduce charge rate at high SOC */
    if (bat->soc_smoothed > 80.0) {
        max_charge *= (100.0 - bat->soc_smoothed) / 20.0;
    }
    
    /* Reduce charge rate at high temperature */
    if (bat->temperature > 40.0) {
        max_charge *= 0.5;
    } else if (bat->temperature > 35.0) {
        max_charge *= 0.8;
    }
    
    /* Reduce charge rate at low temperature */
    if (bat->temperature < 0.0) {
        max_charge = 0;  /* No charging below freezing */
    } else if (bat->temperature < 10.0) {
        max_charge *= 0.5;
    }
    
    return max_charge;
}

double battery_calculate_max_discharge(battery_system_t* bat) {
    if (!bat) return 0;
    
    double max_discharge = bat->max_discharge_power;
    
    /* Reduce discharge rate at low SOC */
    if (bat->soc_smoothed < 30.0) {
        max_discharge *= (bat->soc_smoothed - 20.0) / 10.0;
        if (max_discharge < 0) max_discharge = 0;
    }
    
    /* Reduce discharge rate at high temperature */
    if (bat->temperature > 45.0) {
        max_discharge *= 0.5;
    }
    
    /* Reduce discharge rate at low temperature */
    if (bat->temperature < -10.0) {
        max_discharge *= 0.2;
    } else if (bat->temperature < 0.0) {
        max_discharge *= 0.5;
    }
    
    return max_discharge;
}

bool battery_check_limits(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return false;
    
    bool fault_detected = false;
    
    /* Check voltage limits */
    double max_cell_voltage = 3.65;  /* LFP max */
    double min_cell_voltage = 2.5;   /* LFP min */
    
    int series_cells = (int)(measurements->battery_voltage / 3.2);
    double cell_voltage = measurements->battery_voltage / series_cells;
    
    if (cell_voltage > max_cell_voltage * 1.05) {
        bat->overvoltage_fault = true;
        strncpy(bat->last_fault_reason, "Overvoltage fault", sizeof(bat->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    if (cell_voltage < min_cell_voltage * 0.95) {
        bat->undervoltage_fault = true;
        strncpy(bat->last_fault_reason, "Undervoltage fault", sizeof(bat->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    /* Check current limits */
    if (measurements->battery_current > bat->max_charge_current * 1.2) {
        bat->overcurrent_fault = true;
        strncpy(bat->last_fault_reason, "Overcurrent fault", sizeof(bat->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    /* Check temperature limits */
    if (measurements->battery_temp > 60.0) {
        bat->overtemperature_fault = true;
        strncpy(bat->last_fault_reason, "Overtemperature fault", sizeof(bat->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    if (fault_detected) {
        bat->state = BATTERY_STATE_FAULT;
    }
    
    return fault_detected;
}

void battery_thermal_management(battery_system_t* bat) {
    if (!bat) return;
    
    /* Simple thermal management logic */
    if (bat->temperature > 35.0) {
        bat->cooling_active = true;
        bat->heating_active = false;
    } else if (bat->temperature < 10.0) {
        bat->cooling_active = false;
        bat->heating_active = true;
    } else {
        bat->cooling_active = false;
        bat->heating_active = false;
    }
}

void battery_equalize(battery_system_t* bat) {
    if (!bat) return;
    
    /* Equalize charge for lead-acid batteries */
    /* Not typically needed for lithium, but included for completeness */
    
    if (bat->soc_smoothed < 95.0) {
        return;  /* Only equalize when nearly full */
    }
    
    bat->state = BATTERY_STATE_CHARGING;
    bat->charge_stage = CHARGE_EQUALIZE;
    
    /* Equalize at higher voltage for limited time */
    /* In practice, this would be chemistry-specific */
}

void battery_log_status(const battery_system_t* bat) {
    if (!bat) return;
    
    printf("=== Battery System Status ===\n");
    printf("State: %s\n", battery_state_str[bat->state]);
    printf("Charge Stage: %s\n", charge_stage_str[bat->charge_stage]);
    printf("SOC: %.1f%% (Smoothed: %.1f%%)\n", bat->soc_estimated, bat->soc_smoothed);
    printf("Voltage-based SOC: %.1f%%\n", bat->soc_voltage_based);
    printf("Coulomb Counting SOC: %.1f%%\n", bat->soc_coulomb_counting);
    printf("Temperature: %.1f°C\n", bat->temperature);
    printf("Capacity Remaining: %.0f Wh\n", bat->capacity_remaining);
    printf("Health: %.1f%%\n", bat->health_percentage);
    printf("Active Banks: %d\n", bat->active_bank_count);
    printf("Total Charge Energy: %.2f kWh\n", bat->total_charge_energy / 1000.0);
    printf("Total Discharge Energy: %.2f kWh\n", bat->total_discharge_energy / 1000.0);
    printf("Cycle Count: %d\n", bat->cycle_count);
    
    if (bat->state == BATTERY_STATE_FAULT) {
        printf("Fault: %s\n", bat->last_fault_reason);
    }
    
    printf("Thermal: Cooling=%s, Heating=%s\n", 
           bat->cooling_active ? "ON" : "OFF", 
           bat->heating_active ? "ON" : "OFF");
    printf("============================\n");
}
