#ifndef AGRICULTURE_H
#define AGRICULTURE_H

#include "core.h"

/* Irrigation system states */
typedef enum {
    IRR_STATE_IDLE = 0,
    IRR_STATE_WATERING,
    IRR_STATE_PAUSED,
    IRR_STATE_FAULT,
    IRR_STATE_MAINTENANCE
} irrigation_state_t;

/* Soil moisture sensor status */
typedef enum {
    MOISTURE_OK = 0,
    MOISTURE_LOW,
    MOISTURE_HIGH,
    MOISTURE_SENSOR_FAULT
} moisture_status_t;

/* Agriculture system context */
typedef struct {
    irrigation_zone_t zones[MAX_IRRIGATION_ZONES];
    irrigation_state_t zone_states[MAX_IRRIGATION_ZONES];
    int zone_count;
    
    /* Control parameters */
    irrigation_mode_t mode;
    double max_power_usage;
    double water_pressure;
    double flow_rate_total;
    
    /* Scheduling */
    time_t daily_start_time;
    time_t daily_end_time;
    double max_daily_water;  /* Maximum daily water usage (gallons) */
    
    /* Soil moisture management */
    moisture_status_t moisture_status[MAX_IRRIGATION_ZONES];
    double moisture_low_threshold;
    double moisture_high_threshold;
    
    /* Statistics */
    double total_water_used;
    double total_energy_used;
    double daily_water_used;
    double daily_energy_used;
    time_t last_irrigation_day;
    
    /* Fault detection */
    bool pump_fault;
    bool valve_fault;
    bool sensor_fault;
    char last_fault_reason[64];
} agriculture_system_t;

/* Function prototypes */
int agriculture_init(agriculture_system_t* ag, const system_config_t* config);
void agriculture_update_measurements(agriculture_system_t* ag, system_measurements_t* measurements);
bool agriculture_manage_irrigation(agriculture_system_t* ag, double available_power, 
                                  double battery_soc, bool grid_available);
void agriculture_check_moisture(agriculture_system_t* ag);
void agriculture_start_zone(agriculture_system_t* ag, int zone_index);
void agriculture_stop_zone(agriculture_system_t* ag, int zone_index);
void agriculture_emergency_stop(agriculture_system_t* ag);
bool agriculture_check_faults(agriculture_system_t* ag);
void agriculture_log_status(const agriculture_system_t* ag);
double agriculture_calculate_water_needed(const agriculture_system_t* ag);

#endif /* AGRICULTURE_H */
