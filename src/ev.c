#include "ev.h"
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

static const char* ev_state_str[] = {
    "DISCONNECTED", "CONNECTED", "CHARGING", "PAUSED", "COMPLETE", "FAULT"
};

static const char* ev_charge_mode_str[] = {
    "SLOW", "NORMAL", "FAST", "SMART"
};

int ev_init(ev_charging_system_t* ev, const system_config_t* config) {
    if (!ev || !config) return -1;
    
    memset(ev, 0, sizeof(ev_charging_system_t));
    
    ev->charger_count = config->ev_charger_count;
    ev->max_total_power = config->ev_charge_power_limit;
    
    // Copy charger config
    for (int i = 0; i < config->ev_charger_count && i < MAX_EV_CHARGERS; i++) {
        memcpy(&ev->chargers[i], &config->ev_chargers[i], sizeof(ev_charger_t));
        ev->charger_states[i] = EV_STATE_DISCONNECTED;
        ev->charge_modes[i] = EV_MODE_SMART;
        
        // Set default values if not configured
        if (ev->chargers[i].max_charge_rate == 0) {
            ev->chargers[i].max_charge_rate = 7000.0;  // 7kW default
        }
        if (ev->chargers[i].min_charge_rate == 0) {
            ev->chargers[i].min_charge_rate = 1500.0;  // 1.5kW default
        }
        if (ev->chargers[i].target_soc == 0) {
            ev->chargers[i].target_soc = 80.0;  // 80% default target
        }
    }
    
    // Set smart charging parameters
    ev->smart_charging_enabled = true;
    ev->grid_power_limit = 3000.0;  // 3kW max from grid
    ev->battery_soc_limit = 30.0;   // Don't use battery below 30%
    ev->allow_grid_charging = true;
    ev->allow_solar_charging = true;
    
    // Set preferred charging window (11 PM to 6 AM)
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    tm_info->tm_hour = 23;
    tm_info->tm_min = 0;
    tm_info->tm_sec = 0;
    ev->preferred_start_time = mktime(tm_info);
    
    tm_info->tm_hour = 6;
    ev->preferred_end_time = mktime(tm_info);
    
    // Initialize statistics
    ev->last_charge_session = now;

    return 0;
}

void ev_update_measurements(ev_charging_system_t* ev, system_measurements_t* measurements) {
    if (!ev || !measurements) return;
    
    /* Update EV charging measurements */
    double total_ev_power = 0;
    
    for (int i = 0; i < ev->charger_count; i++) {
        if (ev->charger_states[i] == EV_STATE_CHARGING) {
            /* Update EV SOC (simulated) */
            if (ev->chargers[i].current_soc < ev->chargers[i].target_soc) {
                /* Increase SOC based on charge rate */
                double charge_rate = ev->chargers[i].max_charge_rate;  /* Would be actual rate */
                double battery_capacity = 75000.0;  /* 75kWh typical EV battery */
                
                ev->chargers[i].current_soc += (charge_rate / battery_capacity) * 100.0 * 
                                              (1.0 / 3600.0);  /* Per second */
                
                if (ev->chargers[i].current_soc > ev->chargers[i].target_soc) {
                    ev->chargers[i].current_soc = ev->chargers[i].target_soc;
                }
            }
            
            total_ev_power += ev->chargers[i].max_charge_rate;  /* Use configured rate */
            
            /* Check if charging is complete */
            if (ev_check_charging_complete(ev, i)) {
                ev->charger_states[i] = EV_STATE_COMPLETE;
                ev->chargers[i].charging_enabled = false;
            }
        }
    }
    
    measurements->ev_charging_power = total_ev_power;
    ev->current_total_power = total_ev_power;
}

bool ev_manage_charging(ev_charging_system_t* ev, double available_power, 
                       double battery_soc, bool grid_available) {
    if (!ev) return false;
    
    bool charging_changed = false;
    time_t now = time(NULL);
    
    /* Check for faults first */
    if (ev_check_faults(ev)) {
        return false;
    }
    
    /* Reset daily energy at midnight */
    struct tm* tm_now = localtime(&now);
    if (tm_now->tm_hour == 0 && tm_now->tm_min == 0) {
        ev->daily_energy_delivered = 0;
    }
    
    /* Manage each charger */
    for (int i = 0; i < ev->charger_count; i++) {
        ev_charger_t* charger = &ev->chargers[i];
        
        /* Skip if not connected */
        if (ev->charger_states[i] == EV_STATE_DISCONNECTED) {
            continue;
        }
        
        /* Check if charging is complete */
        if (ev_check_charging_complete(ev, i)) {
            if (ev->charger_states[i] != EV_STATE_COMPLETE) {
                ev->charger_states[i] = EV_STATE_COMPLETE;
                charger->charging_enabled = false;
                charging_changed = true;
            }
            continue;
        }
        
        /* Determine optimal charge rate */
        double optimal_rate = ev_calculate_optimal_rate(ev, i, available_power, battery_soc);
        
        /* Apply charge rate based on mode */
        switch (ev->charge_modes[i]) {
            case EV_MODE_SLOW:
                optimal_rate = charger->min_charge_rate;
                break;
                
            case EV_MODE_NORMAL:
                optimal_rate = charger->max_charge_rate * 0.5;
                break;
                
            case EV_MODE_FAST:
                optimal_rate = charger->max_charge_rate;
                break;
                
            case EV_MODE_SMART:
                /* Use calculated optimal rate */
                break;
        }
        
        /* Clamp to charger limits */
        if (optimal_rate < charger->min_charge_rate) {
            optimal_rate = charger->min_charge_rate;
        }
        if (optimal_rate > charger->max_charge_rate) {
            optimal_rate = charger->max_charge_rate;
        }
        
        /* Check if we're in preferred charging window */
        bool in_preferred_window = (now >= ev->preferred_start_time || 
                                   now <= ev->preferred_end_time);
        
        /* Smart charging logic */
        if (ev->smart_charging_enabled) {
            /* Check battery SOC */
            if (!grid_available && battery_soc < ev->battery_soc_limit) {
                /* Pause charging if battery is too low */
                if (ev->charger_states[i] == EV_STATE_CHARGING) {
                    ev_pause_charging(ev, i);
                    charging_changed = true;
                }
                continue;
            }
            
            /* Check available power */
            if (optimal_rate > available_power * 0.8) {  /* Use 80% of available */
                optimal_rate = available_power * 0.8;
            }
            
            /* Check if we should charge now or defer */
            if (!in_preferred_window && charger->fast_charge_requested == false) {
                /* Outside preferred window - defer if possible */
                if (ev->charger_states[i] == EV_STATE_CHARGING) {
                    ev_pause_charging(ev, i);
                    charging_changed = true;
                }
                continue;
            }
        }
        
        /* Apply charge rate */
        if (optimal_rate >= charger->min_charge_rate) {
            if (ev->charger_states[i] != EV_STATE_CHARGING) {
                ev->charger_states[i] = EV_STATE_CHARGING;
                charger->charging_enabled = true;
                charger->charge_start_time = now;
                charging_changed = true;
            }
            
            ev_set_charge_rate(ev, i, optimal_rate);
            
            /* Update statistics */
            ev->total_energy_delivered += optimal_rate / 3600.0;  /* Wh per second */
            ev->daily_energy_delivered += optimal_rate / 3600.0;
        } else {
            /* Not enough power for minimum charge rate */
            if (ev->charger_states[i] == EV_STATE_CHARGING) {
                ev_pause_charging(ev, i);
                charging_changed = true;
            }
        }
    }
    
    return charging_changed;
}

void ev_set_charge_rate(ev_charging_system_t* ev, int charger_index, double rate) {
    if (!ev || charger_index < 0 || charger_index >= ev->charger_count) {
        return;
    }
    
    /* In a real system, this would send command to EVSE */
    ev->chargers[charger_index].max_charge_rate = rate;
}

void ev_pause_charging(ev_charging_system_t* ev, int charger_index) {
    if (!ev || charger_index < 0 || charger_index >= ev->charger_count) {
        return;
    }
    
    ev->charger_states[charger_index] = EV_STATE_PAUSED;
    ev->chargers[charger_index].charging_enabled = false;
}

void ev_resume_charging(ev_charging_system_t* ev, int charger_index) {
    if (!ev || charger_index < 0 || charger_index >= ev->charger_count) {
        return;
    }
    
    if (ev->charger_states[charger_index] == EV_STATE_PAUSED) {
        ev->charger_states[charger_index] = EV_STATE_CHARGING;
        ev->chargers[charger_index].charging_enabled = true;
    }
}

bool ev_check_charging_complete(ev_charging_system_t* ev, int charger_index) {
    if (!ev || charger_index < 0 || charger_index >= ev->charger_count) {
        return false;
    }
    
    ev_charger_t* charger = &ev->chargers[charger_index];
    
    if (charger->current_soc >= charger->target_soc - 0.5) {  /* Within 0.5% */
        return true;
    }
    
    return false;
}

bool ev_check_faults(ev_charging_system_t* ev) {
    if (!ev) return false;
    
    bool fault_detected = false;
    
    /* Check for communication faults */
    static time_t last_communication[MAX_EV_CHARGERS] = {0};
    time_t now = time(NULL);
    
    for (int i = 0; i < ev->charger_count; i++) {
        if (ev->charger_states[i] != EV_STATE_DISCONNECTED) {
            if (last_communication[i] > 0 && 
                difftime(now, last_communication[i]) > 30.0) {  /* 30 seconds timeout */
                ev->communication_fault = true;
                strncpy(ev->last_fault_reason, "Communication timeout", 
                        sizeof(ev->last_fault_reason) - 1);
                fault_detected = true;
                ev->charger_states[i] = EV_STATE_FAULT;
            }
            last_communication[i] = now;
        }
    }
    
    /* Check for overcurrent (simplified) */
    if (ev->current_total_power > ev->max_total_power * 1.1) {
        ev->overcurrent_fault = true;
        strncpy(ev->last_fault_reason, "Overcurrent fault", 
                sizeof(ev->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    /* Check for overtemperature (simplified) */
    for (int i = 0; i < ev->charger_count; i++) {
        if (ev->charger_states[i] == EV_STATE_CHARGING) {
            /* Simulate temperature rise */
            double simulated_temp = 25.0 + (ev->chargers[i].max_charge_rate / 1000.0);
            if (simulated_temp > 60.0) {
                ev->overtemperature_fault = true;
                strncpy(ev->last_fault_reason, "Overtemperature fault", 
                        sizeof(ev->last_fault_reason) - 1);
                fault_detected = true;
                ev_pause_charging(ev, i);
            }
        }
    }
    
    return fault_detected;
}

void ev_log_status(const ev_charging_system_t* ev) {
    if (!ev) return;
    
    printf("=== EV Charging System Status ===\n");
    printf("Smart Charging: %s\n", ev->smart_charging_enabled ? "ENABLED" : "DISABLED");
    printf("Max Total Power: %.0f W\n", ev->max_total_power);
    printf("Current Total Power: %.0f W\n", ev->current_total_power);
    printf("Total Energy Delivered: %.2f kWh\n", ev->total_energy_delivered / 1000.0);
    printf("Daily Energy Delivered: %.2f kWh\n", ev->daily_energy_delivered / 1000.0);
    printf("Charge Sessions: %d\n", ev->charge_session_count);
    
    printf("\nCharger Details:\n");
    printf("EV ID               State       SOC%%   Target Mode    Rate(W)  Connected\n");
    printf("------------------------------------------------------------------------\n");
    
    for (int i = 0; i < ev->charger_count; i++) {
        const ev_charger_t* charger = &ev->chargers[i];
        
        printf("%-20s %-11s %-6.1f %-6.1f %-6s %-8.0f %-9s\n",
               charger->ev_id,
               ev_state_str[ev->charger_states[i]],
               charger->current_soc,
               charger->target_soc,
               ev_charge_mode_str[ev->charge_modes[i]],
               charger->max_charge_rate,
               ev->charger_states[i] != EV_STATE_DISCONNECTED ? "YES" : "NO");
    }
    
    if (ev->communication_fault || ev->overcurrent_fault || ev->overtemperature_fault) {
        printf("\nFAULTS: ");
        if (ev->communication_fault) printf("Communication ");
        if (ev->overcurrent_fault) printf("Overcurrent ");
        if (ev->overtemperature_fault) printf("Overtemperature ");
        printf("\nLast Fault: %s\n", ev->last_fault_reason);
    }
    
    printf("================================\n");
}

double ev_calculate_optimal_rate(ev_charging_system_t* ev, int charger_index, 
                                 double available_power, double battery_soc) {
    if (!ev || charger_index < 0 || charger_index >= ev->charger_count) {
        return 0;
    }

    ev_charger_t* charger = &ev->chargers[charger_index];
    time_t now = time(NULL);

    double hours_until_departure = 8.0;  // Default
    if (ev->departure_time[charger_index] > now) {
        hours_until_departure = difftime(ev->departure_time[charger_index], now) / 3600.0;
    }
    if (hours_until_departure <= 0) return 0;

    double battery_capacity = 75000.0;  // Wh
    double energy_needed = (charger->target_soc - battery_soc) / 100.0 * battery_capacity;

    double required_rate = energy_needed / hours_until_departure;  // W
    double optimal_rate = required_rate;

    if (optimal_rate > available_power * 0.8) optimal_rate = available_power * 0.8;
    if (optimal_rate > charger->max_charge_rate) optimal_rate = charger->max_charge_rate;
    if (!ev->allow_grid_charging && available_power < optimal_rate) optimal_rate = available_power;

    if (optimal_rate < 0) optimal_rate = 0;  // Prevent negative rate

    return optimal_rate;
}
