#ifndef EV_H
#define EV_H

#include "core.h"

/* EV charging states */
typedef enum {
    EV_STATE_DISCONNECTED = 0,
    EV_STATE_CONNECTED,
    EV_STATE_CHARGING,
    EV_STATE_PAUSED,
    EV_STATE_COMPLETE,
    EV_STATE_FAULT
} ev_state_t;

/* Charging modes */
typedef enum {
    EV_MODE_SLOW = 0,
    EV_MODE_NORMAL,
    EV_MODE_FAST,
    EV_MODE_SMART
} ev_charge_mode_t;

/* EV charging context */
typedef struct {
    ev_charger_t chargers[MAX_EV_CHARGERS];
    ev_state_t charger_states[MAX_EV_CHARGERS];
    ev_charge_mode_t charge_modes[MAX_EV_CHARGERS];
    int charger_count;
    
    /* Control parameters */
    double max_total_power;
    double current_total_power;
    bool smart_charging_enabled;
    
    /* Scheduling */
    time_t preferred_start_time;
    time_t preferred_end_time;
    time_t departure_time[MAX_EV_CHARGERS];
    
    /* Statistics */
    double total_energy_delivered;
    double daily_energy_delivered;
    int charge_session_count;
    time_t last_charge_session;
    
    /* Smart charging */
    double grid_power_limit;
    double battery_soc_limit;
    bool allow_grid_charging;
    bool allow_solar_charging;
    
    /* Fault detection */
    bool communication_fault;
    bool overcurrent_fault;
    bool overtemperature_fault;
    char last_fault_reason[64];
} ev_charging_system_t;

/* Function prototypes */
int ev_init(ev_charging_system_t* ev, const system_config_t* config);
void ev_update_measurements(ev_charging_system_t* ev, system_measurements_t* measurements);
bool ev_manage_charging(ev_charging_system_t* ev, double available_power, 
                       double battery_soc, bool grid_available);
void ev_set_charge_rate(ev_charging_system_t* ev, int charger_index, double rate);
void ev_pause_charging(ev_charging_system_t* ev, int charger_index);
void ev_resume_charging(ev_charging_system_t* ev, int charger_index);
bool ev_check_charging_complete(ev_charging_system_t* ev, int charger_index);
bool ev_check_faults(ev_charging_system_t* ev);
void ev_log_status(const ev_charging_system_t* ev);
double ev_calculate_optimal_rate(ev_charging_system_t* ev, int charger_index, 
                                double available_power, double battery_soc);

#endif /* EV_H */
