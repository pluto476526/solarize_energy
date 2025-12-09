#include "pv.h"
#include "logging.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#define MPPT_PERTURB_INTERVAL 0.1  // 100ms between perturbations
#define FAULT_VOLTAGE_THRESHOLD 0.3  // 30% voltage deviation
#define FAULT_CURRENT_THRESHOLD 0.5  // 50% current imbalance

static const char* pv_state_str[] = {
    "OFF", "STARTING", "MPPT", "CURTAILED", "FAULT", "MAINTENANCE"
};

static const char* mppt_algorithm_str[] = {
    "OFF", "PERTURB_OBSERVE", "INCREMENTAL_CONDUCTANCE", "CONSTANT_VOLTAGE"
};

int pv_init(pv_system_t* pv, const system_config_t* config) {
    if (!pv || !config) return -1;
    
    memset(pv, 0, sizeof(pv_system_t));
    
    pv->state = PV_STATE_OFF;
    pv->mppt_algorithm = MPPT_PERTURB_OBSERVE;
    pv->mppt_step_size = 0.5;  // 0.5V step size
    pv->active_string_count = 2;
    pv->total_capacity = 10000.0;
    
    /* Initialize string data */
    for (int i = 0; i < MAX_PV_STRINGS; i++) {
        snprintf(pv->strings[i].string_id, sizeof(pv->strings[i].string_id), "PV_STRING_%d", i + 1);
        pv->strings[i].max_power = 5000.0;  // Default 5kW per string
        pv->strings[i].max_voltage = 600.0;
        pv->strings[i].max_current = 10.0;
        pv->strings[i].enabled = true;
        pv->strings[i].fault = false;
        pv->strings[i].efficiency = 98.5;
        LOG_DEBUG("Pv ID: %s ", pv->strings[i].string_id);

        if (pv->strings[i].enabled) {
            pv->active_string_count++;
            pv->total_capacity += pv->strings[i].max_power;
        }
    }
    
    pv->last_reset_time = time(NULL);
    pv->available_power = 0;
    pv->max_operating_power = pv->total_capacity;

    return 0;
}

void pv_update_measurements(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements) return;
    
    /* Update PV measurements from hardware */
    double total_power = 0;
    double total_voltage = 0;
    double total_current = 0;
    int active_strings = 0;
    
    for (int i = 0; i < MAX_PV_STRINGS; i++) {
        if (pv->strings[i].enabled && !pv->strings[i].fault) {
            double string_power = measurements->pv_voltage[i] * measurements->pv_current[i];
            total_power += string_power;
            total_voltage += measurements->pv_voltage[i];
            total_current += measurements->pv_current[i];
            active_strings++;
        }
    }
    
    measurements->pv_power_total = total_power;
    measurements->pv_strings_active = active_strings;
    
    /* Update PV system state */
    pv->available_power = total_power;
}

double pv_calculate_available_power(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements) return 0;
    
    /* Calculate available power based on irradiance, temperature, and system state */
    double available_power = 0;
    
    for (int i = 0; i < MAX_PV_STRINGS; i++) {
        if (pv->strings[i].enabled && !pv->strings[i].fault) {
            /* Calculate string derating factors */
            double irradiance_factor = 1.0;  /* Would come from sensors */
            double temp_factor = 1.0;        /* Would come from temperature sensors */
            double soiling_factor = 0.98;    /* 2% soiling loss */
            double wiring_loss = 0.97;       /* 3% wiring loss */
            
            /* Calculate maximum available power for this string */
            double max_string_power = pv->strings[i].max_power * 
                                     irradiance_factor * 
                                     temp_factor * 
                                     soiling_factor * 
                                     wiring_loss;
            
            available_power += max_string_power;
        }
    }
    
    return available_power;
}

void pv_run_mppt(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements || pv->state != PV_STATE_MPPT) return;
    
    static time_t last_mppt_time = 0;
    time_t now = time(NULL);
    
    /* Only run MPPT at specified interval */
    if (difftime(now, last_mppt_time) < MPPT_PERTURB_INTERVAL) {
        return;
    }
    
    last_mppt_time = now;
    
    switch (pv->mppt_algorithm) {
        case MPPT_PERTURB_OBSERVE: {
            /* Simple Perturb and Observe algorithm */
            double current_power = measurements->pv_power_total;
            double current_voltage = measurements->pv_voltage[0];  /* Assuming single inverter */
            
            if (pv->mppt_power_ref == 0) {
                /* First run - initialize */
                pv->mppt_power_ref = current_power;
                pv->mppt_voltage_ref = current_voltage;
            } else {
                if (current_power > pv->mppt_power_ref) {
                    /* Keep moving in same direction */
                    pv->mppt_voltage_ref += (current_voltage > pv->mppt_voltage_ref) ? 
                                           pv->mppt_step_size : -pv->mppt_step_size;
                } else {
                    /* Reverse direction */
                    pv->mppt_voltage_ref += (current_voltage > pv->mppt_voltage_ref) ? 
                                           -pv->mppt_step_size : pv->mppt_step_size;
                }
                pv->mppt_power_ref = current_power;
            }
            break;
        }
        
        case MPPT_INCREMENTAL_CONDUCTANCE: {
            /* Incremental Conductance algorithm */
            static double prev_power = 0;
            static double prev_voltage = 0;
            
            double delta_power = measurements->pv_power_total - prev_power;
            double delta_voltage = measurements->pv_voltage[0] - prev_voltage;
            
            if (delta_voltage != 0) {
                double conductance = measurements->pv_power_total / measurements->pv_voltage[0];
                double inc_conductance = delta_power / delta_voltage;
                
                if (fabs(inc_conductance) > fabs(conductance)) {
                    pv->mppt_voltage_ref += pv->mppt_step_size;
                } else {
                    pv->mppt_voltage_ref -= pv->mppt_step_size;
                }
            }
            
            prev_power = measurements->pv_power_total;
            prev_voltage = measurements->pv_voltage[0];
            break;
        }
        
        case MPPT_CONSTANT_VOLTAGE:
            /* Constant voltage method - typically 0.78 * Voc */
            pv->mppt_voltage_ref = pv->strings[0].max_voltage * 0.78;
            break;

        case MPPT_OFF:
            pv->mppt_voltage_ref = 0;
            
        default:
            break;
    }
    
    /* Clamp voltage reference to safe limits */
    double min_voltage = pv->strings[0].max_voltage * 0.5;
    double max_voltage = pv->strings[0].max_voltage * 0.9;
    
    if (pv->mppt_voltage_ref < min_voltage) pv->mppt_voltage_ref = min_voltage;
    if (pv->mppt_voltage_ref > max_voltage) pv->mppt_voltage_ref = max_voltage;
}

void pv_apply_curtailment(pv_system_t* pv, double curtail_percent) {
    if (!pv) return;
    
    /* Apply curtailment by reducing maximum operating power */
    if (curtail_percent < 0) curtail_percent = 0;
    if (curtail_percent > 100) curtail_percent = 100;
    
    pv->max_operating_power = pv->total_capacity * (1.0 - curtail_percent / 100.0);
    
    if (curtail_percent > 0) {
        pv->state = PV_STATE_CURTAILED;
    } else if (pv->state == PV_STATE_CURTAILED) {
        pv->state = PV_STATE_MPPT;
    }
}

bool pv_detect_faults(pv_system_t* pv, system_measurements_t* measurements) {
    if (!pv || !measurements) return false;
    
    bool fault_detected = false;
    
    /* Check for string faults */
    for (int i = 0; i < MAX_PV_STRINGS; i++) {
        if (!pv->strings[i].enabled) continue;
        
        /* Check for open circuit (voltage too high) */
        if (measurements->pv_voltage[i] > pv->strings[i].max_voltage * 1.1) {
            pv->strings[i].fault = true;
            strncpy(pv->last_fault_reason, "Overvoltage fault", sizeof(pv->last_fault_reason) - 1);
            fault_detected = true;
        }
        
        /* Check for short circuit (current too high) */
        if (measurements->pv_current[i] > pv->strings[i].max_current * 1.2) {
            pv->strings[i].fault = true;
            strncpy(pv->last_fault_reason, "Overcurrent fault", sizeof(pv->last_fault_reason) - 1);
            fault_detected = true;
        }
        
        /* Check for ground faults (voltage imbalance) */
        if (i > 0) {
            double voltage_diff = fabs(measurements->pv_voltage[i] - measurements->pv_voltage[0]);
            if (voltage_diff > measurements->pv_voltage[0] * FAULT_VOLTAGE_THRESHOLD) {
                pv->strings[i].fault = true;
                strncpy(pv->last_fault_reason, "Voltage imbalance", sizeof(pv->last_fault_reason) - 1);
                fault_detected = true;
            }
        }
    }
    
    if (fault_detected) {
        pv->state = PV_STATE_FAULT;
        pv->fault_count++;
        pv->last_fault_time = time(NULL);
    }
    
    return fault_detected;
}

void pv_clear_faults(pv_system_t* pv) {
    if (!pv) return;
    
    for (int i = 0; i < MAX_PV_STRINGS; i++) {
        pv->strings[i].fault = false;
    }
    
    if (pv->state == PV_STATE_FAULT) {
        pv->state = PV_STATE_MPPT;
    }
    
    memset(pv->last_fault_reason, 0, sizeof(pv->last_fault_reason));
}

void pv_log_status(const pv_system_t* pv) {
    if (!pv) return;
    
    printf("=== PV System Status ===\n");
    printf("State: %s\n", pv_state_str[pv->state]);
    printf("MPPT Algorithm: %s\n", mppt_algorithm_str[pv->mppt_algorithm]);
    printf("Active Strings: %d/%d\n", pv->active_string_count, MAX_PV_STRINGS);
    printf("Total Capacity: %.1f W\n", pv->total_capacity);
    printf("Available Power: %.1f W\n", pv->available_power);
    printf("Max Operating Power: %.1f W\n", pv->max_operating_power);
    printf("Daily Energy: %.2f kWh\n", pv->daily_energy / 1000.0);
    printf("Total Energy: %.2f kWh\n", pv->total_energy / 1000.0);
    printf("Fault Count: %u\n", pv->fault_count);
    
    if (pv->state == PV_STATE_FAULT) {
        printf("Last Fault: %s at %s", pv->last_fault_reason, ctime(&pv->last_fault_time));
    }
    printf("========================\n");
}

double pv_get_efficiency(const pv_system_t* pv) {
    if (!pv || pv->active_string_count == 0) return 0;
    
    double total_efficiency = 0;
    int count = 0;
    
    for (int i = 0; i < MAX_PV_STRINGS; i++) {
        if (pv->strings[i].enabled && !pv->strings[i].fault) {
            total_efficiency += pv->strings[i].efficiency;
            count++;
        }
    }
    
    return count > 0 ? total_efficiency / count : 0;
}
