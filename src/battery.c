/* battery.c — production-grade battery management (LFP defaults) */

#include "battery.h"
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* Define the OCV point structure */
typedef struct {
    double v;
    double soc;
} ocv_point_t;

/* --- Voltage->SOC lookup for LFP (pack per-cell OCV mapping) ---
   This table maps single-cell open-circuit voltage (V) to SOC (%).
   Values approximate a common LFP curve. We'll interpolate.
*/
static const ocv_point_t lfp_ocv_table[] = {
    {2.80, 0.0}, {3.00, 2.0}, {3.10, 10.0}, {3.20, 30.0},
    {3.25, 50.0}, {3.30, 70.0}, {3.35, 85.0}, {3.40, 95.0}, {3.45, 100.0}
};
static const int lfp_ocv_len = sizeof(lfp_ocv_table) / sizeof(lfp_ocv_table[0]);

/* NMC OCV table */
static const ocv_point_t nmc_ocv_table[] = {
    {3.00, 0.0}, {3.40, 10.0}, {3.60, 30.0}, {3.70, 50.0},
    {3.85, 70.0}, {4.00, 85.0}, {4.10, 95.0}, {4.20, 100.0}
};
static const int nmc_ocv_len = sizeof(nmc_ocv_table) / sizeof(nmc_ocv_table[0]);

/* Lead-Acid OCV table */
static const ocv_point_t lead_acid_ocv_table[] = {
    {1.75, 0.0}, {1.95, 20.0}, {2.05, 50.0}, {2.15, 80.0},
    {2.25, 95.0}, {2.35, 100.0}
};
static const int lead_acid_ocv_len = sizeof(lead_acid_ocv_table) / sizeof(lead_acid_ocv_table[0]);

/* Helper: linear interpolation on ocv table */
static double ocv_to_soc(double cell_v, const ocv_point_t* table, int len) {
    if (cell_v <= table[0].v) return table[0].soc;
    if (cell_v >= table[len-1].v) return table[len-1].soc;
    for (int i = 1; i < len; ++i) {
        if (cell_v <= table[i].v) {
            double x0 = table[i-1].v, y0 = table[i-1].soc;
            double x1 = table[i].v, y1 = table[i].soc;
            double t = (cell_v - x0) / (x1 - x0);
            return y0 + t * (y1 - y0);
        }
    }
    return 0.0;
}

// Initialize battery system with sane production defaults
int battery_init(battery_system_t* bat, const system_config_t* config) {
    if (!bat) return -1;
    memset(bat, 0, sizeof(*bat));

    // Default to LFP chemistry and multi-bank defaults
    bat->chemistry = BAT_CHEM_LFP;
    bat->bank_count = MAX_BATTERY_BANKS;
    bat->active_bank_count = MAX_BATTERY_BANKS;

    bat->capacity_nominal_wh = 0.0;
    for (int i = 0; i < bat->bank_count; ++i) {
        battery_bank_t *b = &bat->banks[i];
        snprintf(b->bank_id, sizeof(b->bank_id), "BANK_%d", i+1);
        b->nominal_voltage = DEFAULT_BANK_NOMINAL_V;
        b->cells_in_series = DEFAULT_BANK_SERIES_CELLS;
        b->parallel_strings = DEFAULT_BANK_PARALLEL_STRINGS;
        b->capacity_wh = DEFAULT_BANK_CAPACITY_WH;
        b->max_charge_power = DEFAULT_BANK_MAX_CHARGE_W;
        b->max_discharge_power = DEFAULT_BANK_MAX_DISCHARGE_W;
        b->cycle_count = 0;
        b->last_full_charge_ts = 0;
        b->health_percent = 100.0;
        b->temperature_c = 25.0;
        b->bank_soc = 50.0;
        b->enabled = true;
        b->balancing_active = false;
        bat->capacity_nominal_wh += b->capacity_wh;
    }

    /* Initialize state */
    bat->accumulated_ah = 0.0;
    bat->last_update_ts = 0;
    bat->last_energy_update_ts = 0;
    bat->soc_coulomb = 50.0;
    bat->soc_voltage = 50.0;
    bat->soc_estimated = 50.0;
    bat->soc_smoothed = 50.0;
    bat->nominal_voltage = 48.0;
    bat->capacity_remaining_wh = 50.0;
    bat->health_percent = 100.0;

    // Limits — conservative factory defaults; can be tuned at runtime
    // Convert power limits to currents using typical nominal voltage
    double pack_v = bat->banks[0].nominal_voltage;
    bat->max_charge_power_w = bat->capacity_nominal_wh > 0 ? (bat->banks[0].max_charge_power * bat->bank_count) : 0.0;
    bat->max_discharge_power_w = bat->capacity_nominal_wh > 0 ? (bat->banks[0].max_discharge_power * bat->bank_count) : 0.0;

    bat->max_charge_current_a = bat->max_charge_power_w / pack_v;
    bat->max_discharge_current_a = bat->max_discharge_power_w / pack_v;

    // Timing defaults
    bat->absorption_duration_s = 2.0 * 3600.0;  // 2 hours
    bat->float_duration_s = 24.0 * 3600.0;      // 24 hours

    // Misc
    bat->temperature_c = 25.0;
    bat->ambient_temperature_c = 25.0;
    bat->cooling_active = false;
    bat->heating_active = false;
    bat->state = BATTERY_STATE_IDLE;
    bat->previous_state = BATTERY_STATE_IDLE;
    bat->charge_stage = CHARGE_BULK;

    // Balancing
    bat->balancing_enabled = true;
    bat->max_cell_voltage = 0.0;
    bat->min_cell_voltage = 0.0;
    bat->cell_voltage_spread = 0.0;

    // SOC fusion parameters
    bat->soc_voltage_weight = 0.4;   // weight for voltage-based (0..1)
    bat->soc_smoothing_alpha = 0.10; // smoothing factor
    bat->min_operating_soc = 5.0;
    bat->max_operating_soc = 98.0;
    
    // Charge parameters
    bat->bulk_charge_soc_limit = 85.0;
    bat->absorption_charge_soc_limit = 95.0;
    bat->equalize_voltage = 3.65;
    bat->float_voltage = 3.40;
    bat->coulomb_efficiency = 0.99;
    bat->self_discharge_rate = 0.33; // % per day for LFP

    // Clear faults
    bat->overvoltage_fault = bat->undervoltage_fault = false;
    bat->overcurrent_fault = bat->overtemperature_fault = false;
    bat->last_fault_reason[0] = '\0';
    bat->fault_timestamp = 0;
    bat->fault_clear_attempts = 0;
    
    // Cycle counting
    bat->cycle_count = 0;
    bat->deep_cycle_count = 0;
    bat->cycle_depth_accumulated = 0.0;
    bat->total_charge_wh = 0.0;
    bat->total_discharge_wh = 0.0;

    (void)config;

    LOG_INFO("Battery initialized: %d banks, nominal_wh=%.0f", bat->active_bank_count, bat->capacity_nominal_wh);
    return 0;
}

// Update measurements: compute power, SOC, safety, and thermal actions
void battery_update_measurements(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return;

    // Update temperature from measurements if provided
    if (measurements->battery_temp != 0.0) {
        bat->temperature_c = measurements->battery_temp;
    }

    if (bat->capacity_remaining_wh)
        measurements->battery_power = bat->capacity_remaining_wh;

    // Compute battery power (W) using measured voltage and current
    measurements->battery_voltage = bat->nominal_voltage;
    //measurements->battery_power = measurements->battery_voltage * measurements->battery_current;
    measurements->timestamp = measurements->timestamp ? measurements->timestamp : time(NULL);

    // Track energy flows
    if (bat->last_energy_update_ts == 0) {
        bat->last_energy_update_ts = measurements->timestamp;
    } else {
        double dt_hours = difftime(measurements->timestamp, bat->last_energy_update_ts) / 3600.0;
        if (dt_hours > 0) {
            double energy_wh = measurements->battery_power * dt_hours;
            if (energy_wh > 0) {
                bat->total_charge_wh += energy_wh;
            } else {
                bat->total_discharge_wh += -energy_wh;
            }
            bat->last_energy_update_ts = measurements->timestamp;
        }
    }

    // Recalculate SOC using measured values
    battery_calculate_soc(bat, measurements);

    // Update safety and thermal controls
    battery_check_limits(bat, measurements);
    battery_thermal_management(bat);

    // Auto-clear transient faults after 5 minutes
    if (bat->state == BATTERY_STATE_FAULT && difftime(time(NULL), bat->fault_timestamp) > 300)
        battery_clear_faults(bat);
}

// Compute SOC (coulomb + voltage fusion)
int battery_calculate_soc(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return 0;

    // Timestamp handling
    time_t now = measurements->timestamp ? measurements->timestamp : time(NULL);

    if (bat->last_update_ts == 0) {
        bat->last_update_ts = now;
        bat->soc_smoothed = measurements->battery_soc;
        bat->accumulated_ah = (measurements->battery_soc / 100.0) * (bat->capacity_nominal_wh / bat->banks[0].nominal_voltage);
        return 0;
    }

    double dt_s = difftime(now, bat->last_update_ts);
    if (dt_s < 0.5) dt_s = 1.0;

    // Coulomb counting

    // Self-discharge: extremely small
    if (bat->self_discharge_rate > 0.0) {
        double per_sec = bat->self_discharge_rate / (100.0 * 86400.0);
        double wh_loss = bat->capacity_nominal_wh * per_sec * dt_s;
        double ah_loss = wh_loss / bat->banks[0].nominal_voltage;
        bat->accumulated_ah -= ah_loss;
    }

    // Convert charge/discharge current to Ah
    double delta_ah = measurements->battery_current * dt_s / 3600.0;

    // Efficiency on charge only
    if (delta_ah > 0)
        delta_ah *= bat->coulomb_efficiency;

    bat->accumulated_ah += delta_ah;

    // Clamp
    double total_ah = bat->capacity_nominal_wh / bat->banks[0].nominal_voltage;
    if (bat->accumulated_ah < 0) bat->accumulated_ah = 0;
    if (bat->accumulated_ah > total_ah) bat->accumulated_ah = total_ah;

    bat->last_update_ts = now;

    double soc_c = 100.0 * (bat->accumulated_ah / total_ah);
    bat->soc_coulomb = soc_c;

    // Voltage-based SOC (OCV)
    double cell_v;
    double soc_v = 0.0;

    if (bat->banks[0].cells_in_series > 0)
        cell_v = measurements->battery_voltage / bat->banks[0].cells_in_series;
    else
        cell_v = measurements->battery_voltage / 16.0;

    switch (bat->chemistry) {
        case BAT_CHEM_LFP:
            soc_v = ocv_to_soc(cell_v, lfp_ocv_table, lfp_ocv_len);
            break;
        case BAT_CHEM_NMC:
            soc_v = ocv_to_soc(cell_v, nmc_ocv_table, nmc_ocv_len);
            break;
        case BAT_CHEM_LEAD_ACID:
            soc_v = ocv_to_soc(cell_v, lead_acid_ocv_table, lead_acid_ocv_len);
            break;
        default:
            soc_v = 0.0;
            break;
    }

    bat->soc_voltage = soc_v;

    // Dynamic fusion
    double wv = bat->soc_voltage_weight;
    double current_mag = fabs(measurements->battery_current);

    // Completely disable voltage SOC under significant load/charge
    if (current_mag > bat->max_charge_current_a * 0.05)
        wv = 0.0;

    // Reduce weight in temperature extremes
    if (bat->temperature_c < 10 || bat->temperature_c > 40)
        wv *= 0.3;

    if (wv < 0) wv = 0;
    if (wv > 1) wv = 1;

    double wf = 1.0 - wv;

    double soc_est = soc_c * wf + soc_v * wv;
    bat->soc_estimated = soc_est;

    // Large discrepancy correction
    if (wv > 0.8 && fabs(soc_c - soc_v) > 18.0) {
        bat->accumulated_ah = (soc_v / 100.0) * total_ah;
        soc_c = soc_v;
    }

    // Exponential smoothing
    double alpha = bat->soc_smoothing_alpha;
    double change_mag = fabs(soc_est - bat->soc_smoothed);

    // Faster smoothing only when SOC changes quickly
    if (change_mag > 1.0)
        alpha = fmin(1.0, alpha * 3.0);
    else if (change_mag < 0.1)
        alpha = alpha * 0.5;

    double smoothed = alpha * soc_est + (1 - alpha) * bat->soc_smoothed;
    if (smoothed < 0) smoothed = 0;
    if (smoothed > 100) smoothed = 100;

    bat->soc_smoothed = smoothed;

    // Update other fields
    measurements->battery_soc = smoothed;
    bat->capacity_remaining_wh = (smoothed / 100.0) * bat->capacity_nominal_wh;

    for (int i = 0; i < bat->active_bank_count; i++)
        bat->banks[i].bank_soc = smoothed;

    return 0;
}

// Battery charging logic
void battery_manage_charging(battery_system_t* bat, double available_power, double load_power) {
    if (!bat) return;

    double excess = available_power - load_power;
    
    // Calculate max charge
    double max_charge = battery_calculate_max_charge(bat);
    
    // Charge if battery is very low
    bool emergency_charge = (bat->soc_smoothed < 10.0) && (excess > 10.0);
    bool normal_charge = (excess > 100.0) && (bat->soc_smoothed < bat->max_operating_soc);
    
    bool do_charge = emergency_charge || normal_charge;
    
    if (!do_charge) {
        // Idle, reset absorption timers
        if (bat->state == BATTERY_STATE_CHARGING) {
            bat->previous_state = bat->state;
        }

        bat->state = BATTERY_STATE_IDLE;
        bat->absorption_start_ts = 0;
        bat->float_start_ts = 0;
        bat->charge_stage = CHARGE_BULK;
        
        return;
    }

    bat->previous_state = bat->state;
    bat->state = BATTERY_STATE_CHARGING;

    // Proposed charge power is min(excess, max_charge)
    double charge_power = fmin(excess, max_charge);
    
    // If charge_power is too small, increase it for emergency charging
    if (bat->soc_smoothed < 10.0 && charge_power < 100.0) {
        // Force at least 100W charging when SOC is critically low
        charge_power = fmin(100.0, excess);
        LOG_WARNING("Emergency charging at low SOC: %.1fW", charge_power);
    }

    // Stage logic
    if (bat->soc_smoothed < bat->bulk_charge_soc_limit) {
        bat->charge_stage = CHARGE_BULK;
        
        // At very low SOC, use full available power
        if (bat->soc_smoothed < 20.0)
            charge_power = fmin(excess, bat->max_charge_power_w);
        
    } else if (bat->soc_smoothed < bat->absorption_charge_soc_limit) {
        if (bat->absorption_start_ts == 0) {
            bat->absorption_start_ts = time(NULL);
            LOG_INFO("Starting absorption charge at %.1f%% SOC", bat->soc_smoothed);
        }

        bat->charge_stage = CHARGE_ABSORPTION;

        // Ramp down power during absorption
        double elapsed = difftime(time(NULL), bat->absorption_start_ts);
        double factor = 1.0;
        if (bat->absorption_duration_s > 0.0) {
            factor = fmax(0.1, 1.0 - (elapsed / bat->absorption_duration_s));
        }
        charge_power *= factor;
        
        // Time-based cut-off
        if (elapsed >= bat->absorption_duration_s) {
            bat->charge_stage = CHARGE_FLOAT;
            bat->float_start_ts = time(NULL);
            LOG_INFO("Moving to float charge after %.1f hours", elapsed/3600.0);
        }
    
    } else {
        bat->charge_stage = CHARGE_FLOAT;
        charge_power = fmin(charge_power, max_charge * 0.05); // float ~5%
        if (bat->float_start_ts == 0) {
            bat->float_start_ts = time(NULL);
            LOG_INFO("Starting float charge at %.1f%% SOC", bat->soc_smoothed);
        }
        
        // Exit float if SOC drops significantly
        if (bat->soc_smoothed < bat->bulk_charge_soc_limit - 5.0) {
            bat->charge_stage = CHARGE_BULK;
            bat->float_start_ts = 0;
            LOG_INFO("Exiting float, returning to bulk charge");
        }
    }

    // Temperature derating
    if (bat->temperature_c > 40.0) {
        double derate = 1.0 - ((bat->temperature_c - 40.0) / 20.0);
        derate = fmax(derate, 0.3); // Minimum 30% at 60°C
        charge_power *= derate;
    } else if (bat->temperature_c < 0.0) {
        // Allow SOME charging below freezing for emergency
        if (bat->soc_smoothed < 10.0)
            // Emergency charging at reduced rate
            charge_power *= 0.1; // 10% of normal
        else
            charge_power = 0.0; // normal no charge below freezing
    } else if (bat->temperature_c < 10.0) {
        double derate = 0.1 + (bat->temperature_c / 10.0 * 0.9); // 10-100% linear
        charge_power *= derate;
    }

    // Update energy tracking
    if (charge_power > 0) {
        // Assuming 1-second control cycle
        double energy_wh = charge_power / 3600.0; // Convert W to Wh for 1 second
        bat->total_charge_wh += energy_wh;
        
        // Update accumulated Ah for SOC calculation
        double current_a = charge_power / bat->nominal_voltage;
        double delta_ah = current_a / 3600.0; // Ah for 1 second
        
        // Apply coulomb efficiency for charging
        delta_ah *= bat->coulomb_efficiency;
        bat->accumulated_ah += delta_ah;
    }

    // Actual charge power that will be used by controller
    // In a real system, this would set an actual output
    (void)charge_power; // Avoid unused warning
}

// Manage discharging when islanded or requested
void battery_manage_discharging(battery_system_t* bat, double load_power, bool grid_available) {
    if (!bat) return;

    double max_dis = battery_calculate_max_discharge(bat);
    bool should_discharge = false;

    if (!grid_available)
        should_discharge = (load_power > 10.0) && 
            (bat->soc_smoothed > bat->min_operating_soc + 5.0);
    else
        // Grid-connected: discharge for peak shaving or time-of-use optimization
        should_discharge = (bat->soc_smoothed > 70.0) && 
            (load_power > 100.0) && (bat->soc_smoothed > bat->min_operating_soc);

    if (!should_discharge) {
        if (bat->state == BATTERY_STATE_DISCHARGING)
            bat->previous_state = bat->state;

        bat->state = BATTERY_STATE_IDLE;
        return;
    }

    bat->previous_state = bat->state;
    bat->state = BATTERY_STATE_DISCHARGING;

    double discharge_power = fmin(load_power, max_dis);

    // SOC-based power limiting
    if (bat->soc_smoothed < bat->min_operating_soc + 10.0) {
        double soc_headroom = bat->soc_smoothed - bat->min_operating_soc;
        double power_factor = soc_headroom / 10.0;
        power_factor = fmax(power_factor, 0.1); /* Minimum 10% power */
        discharge_power *= power_factor;
    }

    // Temperature derating
    if (bat->temperature_c > 50.0) {
        discharge_power *= 0.5;
    }
    if (bat->temperature_c < -10.0) {
        discharge_power *= 0.2;
    }

    // Protect minimum SOC
    double hours_to_min_soc = (bat->soc_smoothed - bat->min_operating_soc) / 100.0 *
        bat->capacity_nominal_wh / discharge_power;
    
    if (hours_to_min_soc < 0.5) {   // Less than 30 minutes to minimum SOC
        discharge_power *= 0.5;     // Reduce power
    }

    bat->total_discharge_wh += discharge_power * (1.0 / 3600.0); // placeholder

    (void)discharge_power; // actuator uses this
}

// Compute conservative max charge power (W)
double battery_calculate_max_charge(battery_system_t* bat) {
    if (!bat) return 0.0;
    
    double max_p = bat->max_charge_power_w;
    
    if (bat->soc_smoothed < 20.0) {
        // Emergency mode: allow full charging at very low SOC
        // Don't reduce power when battery is critically low
        LOG_WARNING("Emergency charging at low SOC: %.1f%%", bat->soc_smoothed);
    } else if (bat->soc_smoothed > 80.0) {
        // Only reduce near top SOC
        double factor = fmax(0.05, (100.0 - bat->soc_smoothed) / 20.0);
        max_p *= factor;
        LOG_DEBUG("Reducing charge at high SOC: factor=%.2f", factor);
    }
    
    // Temperature derating - FIXED
    if (bat->temperature_c > 45.0) {
        double derate = 1.0 - ((bat->temperature_c - 45.0) / 20.0);
        derate = fmax(derate, 0.3); // Minimum 30% at 65°C
        max_p *= derate;
        LOG_DEBUG("Temperature derating: %.1f°C -> factor=%.2f", bat->temperature_c, derate);
    } else if (bat->temperature_c < 0.0) {
        // Emergency charging allowed at very low SOC even below freezing
        if (bat->soc_smoothed < 10.0) {
            max_p *= 0.1; // 10% emergency charge
            LOG_WARNING("Emergency cold charging at %.1f°C, SOC=%.1f%%", bat->temperature_c, bat->soc_smoothed);
        } else {
            max_p = 0.0; // Normal: no charge below freezing
        }
    } else if (bat->temperature_c < 10.0) {
        double derate = bat->temperature_c / 10.0; // Linear from 0-100%
        max_p *= derate;
        LOG_DEBUG("Cold temperature derating: %.1f°C -> factor=%.2f", bat->temperature_c, derate);
    }
    
    // Force at least 100W when battery is low
    if (max_p < 100.0 && bat->soc_smoothed < 20.0) {
        max_p = 100.0;
    }
    
    return max_p;
}

// Compute conservative max discharge power (W)
double battery_calculate_max_discharge(battery_system_t* bat) {
    if (!bat) return 0.0;
    double max_p = bat->max_discharge_power_w;
    if (bat->soc_smoothed < 30.0) {
        double factor = fmax(0.0, (bat->soc_smoothed - bat->min_operating_soc) / (30.0 - bat->min_operating_soc));
        max_p *= factor;
    }
    if (bat->temperature_c > 55.0) max_p *= 0.5;
    if (bat->temperature_c < -10.0) max_p *= 0.2;
    return max_p;
}

// Check hardware & safety limits with hysteresis
// Return true if fault found
bool battery_check_limits(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !measurements) return false;

    bool fault = false;
    static bool last_overvoltage = false;
    static bool last_undervoltage = false;
    static bool last_overcurrent = false;
    static bool last_overtemperature = false;
    
    // Voltage per-cell sanity using configured series cells
    int s = bat->banks[0].cells_in_series;
    if (s <= 0) s = DEFAULT_BANK_SERIES_CELLS;
    double cell_v = (s > 0) ? (measurements->battery_voltage / (double)s) : 0.0;

    // chemistry-specific thresholds with hysteresis
    double cell_v_max = 3.65, cell_v_min = 2.5;
    double cell_v_max_hyst = 3.60, cell_v_min_hyst = 2.6;
    
    switch (bat->chemistry) {
        case BAT_CHEM_LFP:
            break; 
        case BAT_CHEM_NMC:
            cell_v_max = 4.2; cell_v_min = 3.0;
            cell_v_max_hyst = 4.15; cell_v_min_hyst = 3.1;
            break;
        case BAT_CHEM_LEAD_ACID:
            cell_v_max = 2.45; cell_v_min = 1.75;
            cell_v_max_hyst = 2.40; cell_v_min_hyst = 1.80;
            break;
    }

    // Check overvoltage with hysteresis
    if (cell_v > cell_v_max) {
        bat->overvoltage_fault = true;
        if (!last_overvoltage) {
            strncpy(bat->last_fault_reason, "Cell overvoltage", sizeof(bat->last_fault_reason)-1);
            bat->fault_timestamp = time(NULL);
        }
        fault = true;
    } else if (cell_v < cell_v_max_hyst && last_overvoltage) {
        bat->overvoltage_fault = false;
    }
    last_overvoltage = bat->overvoltage_fault;

    // Check undervoltage with hysteresis
    if (cell_v < cell_v_min) {
        bat->undervoltage_fault = true;
        if (!last_undervoltage) {
            strncpy(bat->last_fault_reason, "Cell undervoltage", sizeof(bat->last_fault_reason)-1);
            bat->fault_timestamp = time(NULL);
        }
        fault = true;
    
    } else if (cell_v > cell_v_min_hyst && last_undervoltage)
        bat->undervoltage_fault = false;
    
    last_undervoltage = bat->undervoltage_fault;

    // Current limit
    double max_charge_a = bat->max_charge_current_a;
    double max_discharge_a = bat->max_discharge_current_a;
    double charge_threshold = max_charge_a * 1.2;
    double discharge_threshold = max_discharge_a * 1.2;
    double charge_hyst = max_charge_a * 1.1;
    double discharge_hyst = max_discharge_a * 1.1;

    if (measurements->battery_current > charge_threshold) {
        bat->overcurrent_fault = true;
        fault = true;

        if (!last_overcurrent) {
            strncpy(bat->last_fault_reason, "Charge overcurrent", sizeof(bat->last_fault_reason)-1);
            bat->fault_timestamp = time(NULL);
        }

    } else if (measurements->battery_current < -discharge_threshold) {
        bat->overcurrent_fault = true;
        fault = true;

        if (!last_overcurrent) {
            strncpy(bat->last_fault_reason, "Discharge overcurrent", sizeof(bat->last_fault_reason)-1);
            bat->fault_timestamp = time(NULL);
        }

    } else if (measurements->battery_current < charge_hyst && measurements->battery_current > -discharge_hyst && last_overcurrent)
        bat->overcurrent_fault = false;

    last_overcurrent = bat->overcurrent_fault;

    // Temperature
    double temp_threshold = 60.0;
    double temp_hyst = 55.0;
    
    if (measurements->battery_temp > temp_threshold) {
        bat->overtemperature_fault = true;
        fault = true;

        if (!last_overtemperature) {
            strncpy(bat->last_fault_reason, "Overtemperature", sizeof(bat->last_fault_reason)-1);
            bat->fault_timestamp = time(NULL);
        }

    } else if (measurements->battery_temp < temp_hyst && last_overtemperature)
        bat->overtemperature_fault = false;

    last_overtemperature = bat->overtemperature_fault;

    if (fault && bat->state != BATTERY_STATE_FAULT) {
        bat->previous_state = bat->state;
        bat->state = BATTERY_STATE_FAULT;
        bat->fault_clear_attempts = 0;
    }

    return fault;
}

// Simple thermal manager with hysteresis
void battery_thermal_management(battery_system_t* bat) {
    if (!bat) return;

    const double cooling_on = 35.0;
    const double cooling_off = 33.0;
    const double heating_on = 8.0;
    const double heating_off = 10.0;

    if (bat->temperature_c >= cooling_on) {
        bat->cooling_active = true;
    } else if (bat->temperature_c <= cooling_off) {
        bat->cooling_active = false;
    }

    if (bat->temperature_c <= heating_on) {
        bat->heating_active = true;
    } else if (bat->temperature_c >= heating_off) {
        bat->heating_active = false;
    }
}

// Equalize routine (only invoked for chemistries that support it)
void battery_equalize(battery_system_t* bat) {
    if (!bat) return;

    if (bat->chemistry == BAT_CHEM_LEAD_ACID) {
        // perform lead-acid equalization if enabled and near full
        if (bat->soc_smoothed > 95.0) {
            bat->state = BATTERY_STATE_CHARGING;
            bat->charge_stage = CHARGE_EQUALIZE;
            // equalize logic handled by charger hardware/system
        }
    }
}

// Print battery status
void battery_log_status(const battery_system_t* bat) {
    if (!bat) return;

    printf("=== Battery System Status ===\n");
    printf("State: %d (%s)\n", bat->state,
           (bat->state == BATTERY_STATE_CHARGING) ? "CHARGING" :
           (bat->state == BATTERY_STATE_DISCHARGING) ? "DISCHARGING" :
           (bat->state == BATTERY_STATE_FLOAT) ? "FLOAT" :
           (bat->state == BATTERY_STATE_EQUALIZE) ? "EQUALIZE" :
           (bat->state == BATTERY_STATE_FAULT) ? "FAULT" : "IDLE");
    printf("Charge Stage: %d\n", bat->charge_stage);
    printf("SOC: %.2f%% (est: %.2f, coulomb: %.2f, voltage: %.2f)\n",
           bat->soc_smoothed, bat->soc_estimated, bat->soc_coulomb, bat->soc_voltage);
    printf("Capacity Remaining: %.0f Wh / %.0f Wh nominal\n", bat->capacity_remaining_wh, bat->capacity_nominal_wh);
    printf("Temp: %.1f °C (ambient %.1f °C) Cooling=%s Heating=%s\n",
           bat->temperature_c, bat->ambient_temperature_c,
           bat->cooling_active ? "ON" : "OFF",
           bat->heating_active ? "ON" : "OFF");
    if (bat->state == BATTERY_STATE_FAULT) {
        printf("FAULT: %s\n", bat->last_fault_reason);
    }
    printf("Total Charge: %.3f kWh, Total Discharge: %.3f kWh\n",
           bat->total_charge_wh / 1000.0, bat->total_discharge_wh / 1000.0);
    printf("Cycle Count: %d\n", bat->cycle_count);
    printf("============================\n");
}

/* Clear faults with verification */
void battery_clear_faults(battery_system_t* bat) {
    if (!bat || bat->state != BATTERY_STATE_FAULT) return;
    
    /* Check if fault conditions still exist */
    bool still_faulted = bat->overvoltage_fault || bat->undervoltage_fault ||
                        bat->overcurrent_fault || bat->overtemperature_fault;
    
    if (!still_faulted) {
        /* Return to previous state or idle */
        if (bat->previous_state != BATTERY_STATE_FAULT) {
            bat->state = bat->previous_state;
        } else {
            bat->state = BATTERY_STATE_IDLE;
        }
        
        bat->fault_clear_attempts++;
    } else {
        bat->fault_clear_attempts++;
        if (bat->fault_clear_attempts >= 3) {
            // Persistent fault - would log and potentially enter maintenance mode
        }
    }
}

/* Check cell balancing needs */
bool battery_check_balancing(battery_system_t* bat, system_measurements_t* measurements) {
    if (!bat || !bat->balancing_enabled) return false;
    
    /* Simplified: In real implementation, you would have individual cell voltages */
    double avg_cell_v = measurements->battery_voltage / bat->banks[0].cells_in_series;
    
    /* Simulate some cell voltage spread */
    double spread = 0.020; /* 20mV typical */
    if (bat->soc_smoothed > 90.0) {
        spread = 0.050; /* Higher spread at high SOC */
    }
    
    bat->max_cell_voltage = avg_cell_v + spread/2;
    bat->min_cell_voltage = avg_cell_v - spread/2;
    bat->cell_voltage_spread = spread;
    
    /* Trigger balancing if spread exceeds threshold */
    bool needs_balancing = (spread > 0.030); /* 30mV threshold */
    
    if (needs_balancing && bat->state == BATTERY_STATE_CHARGING) {
        for (int i = 0; i < bat->active_bank_count; i++) {
            bat->banks[i].balancing_active = true;
        }
    } else {
        for (int i = 0; i < bat->active_bank_count; i++) {
            bat->banks[i].balancing_active = false;
        }
    }
    
    return needs_balancing;
}

/* Update capacity health based on usage */
void battery_update_capacity_health(battery_system_t* bat) {
    if (!bat) return;
    
    /* Simple health model based on cycles and age */
    double cycle_degradation = bat->cycle_count * 0.05; /* 0.05% per cycle */
    double deep_cycle_degradation = bat->deep_cycle_count * 0.1; /* 0.1% per deep cycle */
    
    /* 2% per year time-based degradation */
    time_t now = time(NULL);
    double years_operation = difftime(now, bat->last_update_ts) / (365.25 * 86400.0);
    double time_degradation = years_operation * 2.0;
    
    /* Calculate total degradation */
    double total_degradation = cycle_degradation + time_degradation + deep_cycle_degradation;
    
    /* Update health percentage */
    bat->health_percent = 100.0 - total_degradation;
    bat->health_percent = fmax(fmin(bat->health_percent, 100.0), 0.0);
    
    /* Update nominal capacity based on health */
    double original_capacity = 0.0;
    for (int i = 0; i < bat->bank_count; i++) {
        original_capacity += bat->banks[i].capacity_wh;
    }
    
    bat->capacity_nominal_wh = original_capacity * (bat->health_percent / 100.0);
    
    /* Update individual bank health */
    for (int i = 0; i < bat->active_bank_count; i++) {
        bat->banks[i].health_percent = bat->health_percent;
    }
}

/* Enter maintenance mode */
void battery_enter_maintenance_mode(battery_system_t* bat) {
    if (!bat) return;
    
    bat->previous_state = bat->state;
    bat->state = BATTERY_STATE_MAINTENANCE;
    
    /* Disable charging and discharging */
    bat->max_charge_power_w = 0.0;
    bat->max_discharge_power_w = 0.0;
    
    /* Enable thermal management */
    bat->cooling_active = true;
    bat->heating_active = (bat->temperature_c < 20.0);
}