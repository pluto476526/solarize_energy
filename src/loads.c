#include "loads.h"
#include "logging.h"
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

static const char* load_state_str[] = {
    "OFF", "ON", "SHED", "DEFERRED", "FAULT"
};

int loads_init(load_manager_t* lm, const system_config_t* config) {
    if (!lm || !config) return -1;

    memset(lm, 0, sizeof(load_manager_t));

    lm->load_count = config->load_count;

    for (int i = 0; i < lm->load_count && i < MAX_CONTROLLABLE_LOADS; i++) {
        memcpy(&lm->loads[i], &config->loads[i], sizeof(load_definition_t));

        lm->load_states[i] = LOAD_STATE_ON;
        lm->loads[i].last_state_change = time(NULL);
        lm->loads[i].current_state = false;
    }

    for (int p = 0; p < 5; p++) {
        lm->priority_power[p] = 0;
        lm->priority_count[p] = 0;
    }

    for (int i = 0; i < lm->load_count; i++) {
        int p = lm->loads[i].priority;

        if (p >= 0 && p < 5) {
            lm->priority_power[p] += lm->loads[i].rated_power;
            lm->priority_count[p]++;
        }
    }

    lm->shedding_active = true;
    lm->shed_power_target = 0;
    lm->shedding_start_time = 0;
    lm->deferred_power = 0;
    lm->next_deferrable_start = time(NULL) + 300;

    lm->min_shed_duration = 60.0;
    lm->max_shed_duration = 1800.0;
    lm->load_rotation_interval = 300.0;

    return 0;
}

void loads_update_measurements(load_manager_t* lm, system_measurements_t* measurements) {
    if (!lm || !measurements) return;
    
    double critical_power = 0;
    double deferrable_power = 0;
    double total_power = 0;
    
    // Update power measurements based on load states
    for (int i = 0; i < lm->load_count; i++) {
        if (lm->load_states[i] == LOAD_STATE_ON) {
            double load_power = lm->loads[i].rated_power;
            total_power += load_power;
            
            if (lm->loads[i].priority == PRIORITY_CRITICAL) {
                critical_power += load_power;
            }
            
            if (lm->loads[i].is_deferrable) {
                deferrable_power += load_power;
            }
        }
    }
    
    measurements->load_power_total = total_power;
    measurements->load_power_critical = critical_power;
    measurements->load_power_deferrable = deferrable_power;

    loads_update_energy_consumed(lm);
}

bool loads_manage_shedding(load_manager_t* lm, double available_power, double total_load,
    double battery_soc, bool grid_available) {
    if (!lm || grid_available || battery_soc < 50) return false;
    
    double power_deficit = total_load - available_power;
    bool shedding_changed = false;
    
    /* Determine if shedding is needed */
    if (power_deficit > 100.0) {  /* At least 100W deficit */
        if (!lm->shedding_active) {
            lm->shedding_active = true;
            lm->shedding_start_time = time(NULL);
            lm->shed_power_target = power_deficit * 1.2;  /* Target 20% more than deficit */
            lm->shed_event_count++;
        }
        
        /* Shed loads starting from lowest priority */
        double power_shed = 0;
        
        for (int priority = PRIORITY_NON_ESSENTIAL; priority >= PRIORITY_CRITICAL; priority--) {
            for (int i = 0; i < lm->load_count; i++) {
                if ((int)lm->loads[i].priority == priority && 
                    lm->load_states[i] == LOAD_STATE_ON &&
                    lm->loads[i].is_sheddable &&
                    loads_can_shed_load(lm, i, available_power)) {
                    
                    /* Check timing constraints */
                    if (!loads_check_timing_constraints(lm, i)) {
                        continue;
                    }
                    
                    /* Shed this load */
                    lm->load_states[i] = LOAD_STATE_SHED;
                    lm->loads[i].current_state = false;
                    lm->loads[i].last_state_change = time(NULL);
                    
                    power_shed += lm->loads[i].rated_power;
                    shedding_changed = true;
                    
                    /* Check if we've shed enough */
                    if (power_shed >= lm->shed_power_target) {
                        break;
                    }
                }
            }
            
            if (power_shed >= lm->shed_power_target) {
                break;
            }
        }
        
    } else if (lm->shedding_active && power_deficit < -200.0) {
        /* Restore shed loads if we have excess power */
        loads_restore_shed(lm, available_power);
    }
    
    /* Rotate shedding if active for too long */
    if (lm->shedding_active) {
        double shedding_duration = difftime(time(NULL), lm->shedding_start_time);
        if (shedding_duration > lm->load_rotation_interval) {
            loads_rotate_shedding(lm);
            shedding_changed = true;
        }
    }
    
    return shedding_changed;
}

void loads_restore_shed(load_manager_t* lm, double available_power) {
    if (!lm) return;
    
    double excess_power = available_power;
    
    /* Restore loads starting from highest priority */
    for (int priority = PRIORITY_CRITICAL; priority <= PRIORITY_NON_ESSENTIAL; priority++) {
        for (int i = 0; i < lm->load_count; i++) {
            if ((int)lm->loads[i].priority == priority && 
                lm->load_states[i] == LOAD_STATE_SHED &&
                loads_can_shed_load(lm, i, available_power)) {
                
                /* Check if we have enough power to restore */
                if (lm->loads[i].rated_power <= excess_power) {
                    /* Restore this load */
                    lm->load_states[i] = LOAD_STATE_ON;
                    lm->loads[i].current_state = true;
                    lm->loads[i].last_state_change = time(NULL);
                    
                    excess_power -= lm->loads[i].rated_power;
                    lm->restart_event_count++;
                }
            }
        }
    }
    
    /* If all loads restored, stop shedding */
    bool any_shed = false;
    for (int i = 0; i < lm->load_count; i++) {
        if (lm->load_states[i] == LOAD_STATE_SHED) {
            any_shed = true;
            break;
        }
    }
    
    if (!any_shed) {
        lm->shedding_active = false;
        lm->shed_power_target = 0;
    }
}

void loads_rotate_shedding(load_manager_t* lm) {
    if (!lm) return;
    
    time_t now = time(NULL);
    
    /* Find loads that have been shed for min_shed_duration */
    for (int i = 0; i < lm->load_count; i++) {
        if (lm->load_states[i] == LOAD_STATE_SHED) {
            double shed_duration = difftime(now, lm->loads[i].last_state_change);
            if (shed_duration >= lm->min_shed_duration) {
                /* Restore this load temporarily */
                lm->load_states[i] = LOAD_STATE_ON;
                lm->loads[i].current_state = true;
                lm->loads[i].last_state_change = now;
                
                /* Find another load to shed instead */
                for (int j = 0; j < lm->load_count; j++) {
                    if (i != j && 
                        lm->load_states[j] == LOAD_STATE_ON &&
                        lm->loads[j].is_sheddable &&
                        loads_can_shed_load(lm, j, 0)) {
                        
                        /* Check timing constraints */
                        if (!loads_check_timing_constraints(lm, j)) {
                            continue;
                        }
                        
                        /* Shed the alternate load */
                        lm->load_states[j] = LOAD_STATE_SHED;
                        lm->loads[j].current_state = false;
                        lm->loads[j].last_state_change = now;
                        break;
                    }
                }
                
                break;  /* Rotate one load at a time */
            }
        }
    }
    
    lm->shedding_start_time = now;  /* Reset rotation timer */
}

void loads_prioritize_deferrable(load_manager_t* lm, double excess_power) {
    if (!lm || excess_power <= 0) return;
    
    /* Enable deferrable loads if we have excess power */
    for (int i = 0; i < lm->load_count; i++) {
        if (lm->loads[i].is_deferrable && 
            lm->load_states[i] == LOAD_STATE_DEFERRED &&
            lm->loads[i].rated_power <= excess_power) {
            
            /* Check if it's time to start */
            if (time(NULL) >= lm->next_deferrable_start) {
                lm->load_states[i] = LOAD_STATE_ON;
                lm->loads[i].current_state = true;
                lm->loads[i].last_state_change = time(NULL);
                
                excess_power -= lm->loads[i].rated_power;
                lm->deferred_power -= lm->loads[i].rated_power;
            }
        }
    }
}

bool loads_check_timing_constraints(const load_manager_t* lm, int load_index) {
    if (!lm || load_index < 0 || load_index >= lm->load_count) {
        return false;
    }
    
    const load_definition_t* load = &lm->loads[load_index];
    time_t now = time(NULL);
    double time_since_change = difftime(now, load->last_state_change);
    
    if (load->current_state) {  /* Load is currently ON */
        /* Check minimum ON time */
        if (time_since_change < load->min_on_time) {
            return false;  /* Can't turn OFF yet */
        }
    } else {  /* Load is currently OFF */
        /* Check minimum OFF time */
        if (time_since_change < load->min_off_time) {
            return false;  /* Can't turn ON yet */
        }
    }
    
    return true;
}

void loads_log_status(const load_manager_t* lm) {
    if (!lm) return;
    
    printf("=== Load Management Status ===\n");
    printf("Total Loads: %d\n", lm->load_count);
    printf("Shedding Active: %s\n", lm->shedding_active ? "YES" : "NO");
    printf("Deferred Power: %.0f W\n", lm->deferred_power);
    printf("Shed Events: %u\n", lm->shed_event_count);
    printf("Restart Events: %u\n", lm->restart_event_count);
    printf("Total Energy Needed: %.2f kWh\n", loads_calculate_power_needed(lm));
    
    printf("\nLoad Details:\n");
    printf("ID                   Priority State     Power(W) Deferrable\n");
    printf("-----------------------------------------------------------\n");
    
    for (int i = 0; i < lm->load_count; i++) {
        const load_definition_t* load = &lm->loads[i];
        
        printf("%-20s %-9d %-9s %-9.0f %-9s\n",
               load->id, load->priority, load_state_str[lm->load_states[i]],
               load->rated_power, load->is_deferrable ? "YES" : "NO");
    }
    printf("=============================\n");
}

double loads_calculate_power_needed(const load_manager_t* lm) {
    if (!lm) return 0;
    
    double power_needed = 0;
    
    /* Calculate power needed for all ON and deferred loads */
    for (int i = 0; i < lm->load_count; i++) {
        if (lm->load_states[i] == LOAD_STATE_ON || 
            lm->load_states[i] == LOAD_STATE_DEFERRED) {
            power_needed += lm->loads[i].rated_power;
        }
    }
    
    return power_needed;
}


/* Update total energy consumed for all loads (in watt-seconds) */
void loads_update_energy_consumed(load_manager_t* lm) {
    if (!lm) return;

    time_t now = time(NULL);
    double energy_consumed;

    for (int i = 0; i < lm->load_count; i++) {
        load_definition_t* load = &lm->loads[i];

        /* Only count energy for loads that are ON */
        if (load->current_state) {
            double duration = difftime(now, load->last_state_change);   // seconds
            energy_consumed += load->rated_power * duration;     // W * s
            lm->total_energy_consumed = energy_consumed / 3600 / 1000;        // KWh
            load->last_state_change = now;                              // reset timer
        }
    }
}


bool loads_can_shed_load(const load_manager_t* lm, int load_index, double available_power) {
    if (!lm || load_index < 0 || load_index >= lm->load_count || available_power >= 40.0) {
        return false;
    }
    
    const load_definition_t* load = &lm->loads[load_index];
    
    /* Critical loads cannot be shed */
    if (load->priority == PRIORITY_CRITICAL) {
        return false;
    }
    
    /* Check if load is sheddable */
    if (!load->is_sheddable) {
        return false;
    }
    
    /* Check timing constraints */
    if (!loads_check_timing_constraints(lm, load_index)) {
        return false;
    }
    
    return true;
}
