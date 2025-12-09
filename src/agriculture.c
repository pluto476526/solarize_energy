#include "agriculture.h"
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

static const char* irrigation_state_str[] = {
    "IDLE", "WATERING", "PAUSED", "FAULT", "MAINTENANCE"
};

// static const char* moisture_status_str[] = {
//     "OK", "LOW", "HIGH", "SENSOR_FAULT"
// };

int agriculture_init(agriculture_system_t* ag, const system_config_t* config) {
    if (!ag || !config) return -1;
    
    memset(ag, 0, sizeof(agriculture_system_t));
    
    ag->zone_count = config->zone_count;
    ag->mode = config->irrigation_mode;
    ag->max_power_usage = config->irrigation_power_limit;
    
    // Copy zone config
    for (int i = 0; i < config->zone_count && i < MAX_IRRIGATION_ZONES; i++) {
        memcpy(&ag->zones[i], &config->zones[i], sizeof(irrigation_zone_t));
        ag->zone_states[i] = IRR_STATE_IDLE;
        ag->moisture_status[i] = MOISTURE_OK;
        
        // Initialize thresholds if not set
        if (ag->zones[i].moisture_threshold == 0) {
            ag->zones[i].moisture_threshold = 30.0;  // Default 30%
        }
    }
    
    // Set default moisture thresholds
    ag->moisture_low_threshold = 25.0;
    ag->moisture_high_threshold = 85.0;
    
    // Set default schedule (6 AM to 10 AM)
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    tm_info->tm_hour = 6;
    tm_info->tm_min = 0;
    tm_info->tm_sec = 0;
    ag->daily_start_time = mktime(tm_info);
    
    tm_info->tm_hour = 10;
    ag->daily_end_time = mktime(tm_info);
    ag->max_daily_water = 1000.0;  // 1000 gallons limit
    ag->last_irrigation_day = time(NULL);

    return 0;
}

void agriculture_update_measurements(agriculture_system_t* ag, system_measurements_t* measurements) {
    if (!ag || !measurements) return;
    
    /* Update soil moisture readings from sensors */
    /* In a real system, this would read from actual sensors */
    for (int i = 0; i < ag->zone_count; i++) {
        /* Simulate sensor readings */
        if (!ag->sensor_fault) {
            /* Normal variation in soil moisture */
            double base_moisture = 40.0;  /* Base moisture level */
            double variation = sin(time(NULL) / 3600.0) * 10.0;  /* Daily cycle */
            ag->zones[i].soil_moisture = base_moisture + variation;
            
            /* Reduce moisture when watering */
            if (ag->zone_states[i] == IRR_STATE_WATERING) {
                ag->zones[i].soil_moisture += 0.1;  /* Increase moisture while watering */
            }
        }
    }
    
    /* Check moisture levels */
    agriculture_check_moisture(ag);
    
    /* Update power measurement */
    double irrigation_power = 0;
    for (int i = 0; i < ag->zone_count; i++) {
        if (ag->zone_states[i] == IRR_STATE_WATERING) {
            irrigation_power += ag->zones[i].power_consumption;
        }
    }
    
    measurements->irrigation_power = irrigation_power;
}

bool agriculture_manage_irrigation(agriculture_system_t* ag, double available_power, 
                                  double battery_soc, bool grid_available) {
    if (!ag) return false;
    
    bool irrigation_changed = false;
    time_t now = time(NULL);
    
    /* Check for emergency conditions */
    if (ag->pump_fault || ag->valve_fault) {
        agriculture_emergency_stop(ag);
        return false;
    }
    
    /* Check if we're in irrigation window */
    struct tm* tm_now = localtime(&now);
    //int current_hour = tm_now->tm_hour;
    int current_minute = tm_now->tm_hour * 60 + tm_now->tm_min;
    
    struct tm* tm_start = localtime(&ag->daily_start_time);
    int start_minute = tm_start->tm_hour * 60 + tm_start->tm_min;
    
    struct tm* tm_end = localtime(&ag->daily_end_time);
    int end_minute = tm_end->tm_hour * 60 + tm_end->tm_min;
    
    bool in_schedule_window = (current_minute >= start_minute && 
                              current_minute <= end_minute);
    
    /* Reset daily usage at midnight */
    if (tm_now->tm_mday != localtime(&ag->last_irrigation_day)->tm_mday) {
        ag->daily_water_used = 0;
        ag->daily_energy_used = 0;
        ag->last_irrigation_day = now;
    }
    
    /* Manage irrigation based on mode */
    switch (ag->mode) {
        case IRRIGATION_AUTO:
            /* Automatic irrigation based on soil moisture */
            for (int i = 0; i < ag->zone_count; i++) {
                if (ag->zones[i].enabled && 
                    ag->moisture_status[i] == MOISTURE_LOW &&
                    ag->zone_states[i] == IRR_STATE_IDLE) {
                    
                    /* Check if we have enough power */
                    double zone_power = ag->zones[i].power_consumption;
                    if (zone_power <= available_power * 0.8) {  /* Use 80% of available */
                        /* Check daily water limit */
                        double water_needed = ag->zones[i].water_flow_rate * 
                                            ag->zones[i].watering_duration / 60.0;
                        
                        if (ag->daily_water_used + water_needed <= ag->max_daily_water) {
                            /* Check battery SOC if off-grid */
                            if (!grid_available && battery_soc < 40.0) {
                                continue;  /* Skip if battery is low */
                            }
                            
                            agriculture_start_zone(ag, i);
                            irrigation_changed = true;
                        }
                    }
                }
            }
            break;
            
        case IRRIGATION_SCHEDULED:
            /* Scheduled irrigation */
            if (in_schedule_window) {
                /* Water each zone in sequence */
                static int current_zone = 0;
                static time_t zone_start_time = 0;
                
                if (zone_start_time == 0) {
                    zone_start_time = now;
                }
                
                /* Check if current zone has finished */
                if (ag->zone_states[current_zone] == IRR_STATE_WATERING) {
                    double watering_time = difftime(now, ag->zones[current_zone].last_watered);
                    if (watering_time >= ag->zones[current_zone].watering_duration * 60.0) {
                        agriculture_stop_zone(ag, current_zone);
                        
                        /* Move to next zone */
                        current_zone = (current_zone + 1) % ag->zone_count;
                        if (current_zone == 0) {
                            /* Completed full cycle */
                            zone_start_time = 0;
                        } else {
                            agriculture_start_zone(ag, current_zone);
                            irrigation_changed = true;
                        }
                    }
                } else {
                    /* Start current zone */
                    agriculture_start_zone(ag, current_zone);
                    irrigation_changed = true;
                }
            } else {
                /* Stop all irrigation outside schedule */
                for (int i = 0; i < ag->zone_count; i++) {
                    if (ag->zone_states[i] == IRR_STATE_WATERING) {
                        agriculture_stop_zone(ag, i);
                        irrigation_changed = true;
                    }
                }
            }
            break;
            
        case IRRIGATION_MANUAL:
            /* Manual mode - no automatic control */
            break;
            
        case IRRIGATION_OFF:
            /* Turn off all irrigation */
            for (int i = 0; i < ag->zone_count; i++) {
                if (ag->zone_states[i] == IRR_STATE_WATERING) {
                    agriculture_stop_zone(ag, i);
                    irrigation_changed = true;
                }
            }
            break;
    }
    
    return irrigation_changed;
}

void agriculture_check_moisture(agriculture_system_t* ag) {
    if (!ag) return;
    
    for (int i = 0; i < ag->zone_count; i++) {
        double moisture = ag->zones[i].soil_moisture;
        double threshold = ag->zones[i].moisture_threshold;
        
        if (moisture < 0 || moisture > 100) {
            /* Sensor fault */
            ag->moisture_status[i] = MOISTURE_SENSOR_FAULT;
            ag->sensor_fault = true;
        } else if (moisture < threshold - 5.0) {
            /* Too dry */
            ag->moisture_status[i] = MOISTURE_LOW;
        } else if (moisture > threshold + 15.0) {
            /* Too wet */
            ag->moisture_status[i] = MOISTURE_HIGH;
        } else {
            /* OK */
            ag->moisture_status[i] = MOISTURE_OK;
        }
    }
}

void agriculture_start_zone(agriculture_system_t* ag, int zone_index) {
    if (!ag || zone_index < 0 || zone_index >= ag->zone_count) {
        return;
    }
    
    if (!ag->zones[zone_index].enabled) {
        return;
    }
    
    if (ag->zone_states[zone_index] == IRR_STATE_WATERING) {
        return;  /* Already watering */
    }
    
    /* Start watering */
    ag->zone_states[zone_index] = IRR_STATE_WATERING;
    ag->zones[zone_index].last_watered = time(NULL);
    
    /* Update statistics */
    double water_used = ag->zones[zone_index].water_flow_rate * 
                       ag->zones[zone_index].watering_duration / 60.0;
    double energy_used = ag->zones[zone_index].power_consumption * 
                        ag->zones[zone_index].watering_duration / 60.0 / 1000.0;  /* kWh */
    
    ag->daily_water_used += water_used;
    ag->total_water_used += water_used;
    
    ag->daily_energy_used += energy_used;
    ag->total_energy_used += energy_used;
}

void agriculture_stop_zone(agriculture_system_t* ag, int zone_index) {
    if (!ag || zone_index < 0 || zone_index >= ag->zone_count) {
        return;
    }
    
    ag->zone_states[zone_index] = IRR_STATE_IDLE;
}

void agriculture_emergency_stop(agriculture_system_t* ag) {
    if (!ag) return;
    
    for (int i = 0; i < ag->zone_count; i++) {
        agriculture_stop_zone(ag, i);
    }
    
    ag->mode = IRRIGATION_OFF;
}

bool agriculture_check_faults(agriculture_system_t* ag) {
    if (!ag) return false;
    
    bool fault_detected = false;
    
    /* Check for pump faults */
    static double last_flow_rate = 0;
    double current_flow_rate = 0;
    
    for (int i = 0; i < ag->zone_count; i++) {
        if (ag->zone_states[i] == IRR_STATE_WATERING) {
            current_flow_rate += ag->zones[i].water_flow_rate;
        }
    }
    
    /* Detect pump failure if flow is zero when watering */
    if (current_flow_rate == 0 && last_flow_rate > 0) {
        ag->pump_fault = true;
        strncpy(ag->last_fault_reason, "Pump failure - no flow detected", 
                sizeof(ag->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    last_flow_rate = current_flow_rate;
    
    /* Check for pressure faults */
    if (ag->water_pressure < 20.0 && current_flow_rate > 0) {
        /* Low pressure while watering */
        strncpy(ag->last_fault_reason, "Low water pressure", 
                sizeof(ag->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    if (ag->water_pressure > 80.0) {
        /* High pressure - possible blockage */
        strncpy(ag->last_fault_reason, "High water pressure - possible blockage", 
                sizeof(ag->last_fault_reason) - 1);
        fault_detected = true;
    }
    
    if (fault_detected) {
        agriculture_emergency_stop(ag);
    }
    
    return fault_detected;
}

void agriculture_log_status(const agriculture_system_t* ag) {
    if (!ag) return;
    
    printf("=== Agriculture System Status ===\n");
    printf("Mode: %d\n", ag->mode);
    printf("Max Power: %.0f W\n", ag->max_power_usage);
    printf("Daily Water Used: %.1f / %.1f gallons\n", 
           ag->daily_water_used, ag->max_daily_water);
    printf("Total Water Used: %.1f gallons\n", ag->total_water_used);
    printf("Total Energy Used: %.2f kWh\n", ag->total_energy_used);
    printf("Active Zones: ");
    
    int active_count = 0;
    for (int i = 0; i < ag->zone_count; i++) {
        if (ag->zone_states[i] == IRR_STATE_WATERING) {
            printf("%s ", ag->zones[i].zone_id);
            active_count++;
        }
    }
    printf("(%d total)\n", active_count);
    
    printf("\nZone Details:\n");
    printf("Zone ID             Area(sqft) Moisture State      Flow(GPM) Power(W)\n");
    printf("---------------------------------------------------------------------\n");
    
    for (int i = 0; i < ag->zone_count; i++) {
        const irrigation_zone_t* zone = &ag->zones[i];
        
        printf("%-20s %-10.0f %-8.1f%% %-11s %-9.1f %-8.0f\n",
               zone->zone_id,
               zone->area_sqft,
               zone->soil_moisture,
               irrigation_state_str[ag->zone_states[i]],
               zone->water_flow_rate,
               zone->power_consumption);
    }
    
    if (ag->pump_fault || ag->valve_fault || ag->sensor_fault) {
        printf("\nFAULTS: ");
        if (ag->pump_fault) printf("Pump ");
        if (ag->valve_fault) printf("Valve ");
        if (ag->sensor_fault) printf("Sensor ");
        printf("\nLast Fault: %s\n", ag->last_fault_reason);
    }
    
    printf("=================================\n");
}

double agriculture_calculate_water_needed(const agriculture_system_t* ag) {
    if (!ag) return 0;
    
    double water_needed = 0;
    
    for (int i = 0; i < ag->zone_count; i++) {
        if (ag->moisture_status[i] == MOISTURE_LOW && 
            ag->zones[i].enabled) {
            water_needed += ag->zones[i].water_flow_rate * 
                          ag->zones[i].watering_duration / 60.0;
        }
    }
    
    return water_needed;
}
