#ifndef BATTERY_H
#define BATTERY_H

#include "core.h"

/* Battery management states */
typedef enum {
    BATTERY_STATE_IDLE = 0,
    BATTERY_STATE_CHARGING,
    BATTERY_STATE_DISCHARGING,
    BATTERY_STATE_FLOAT,
    BATTERY_STATE_EQUALIZE,
    BATTERY_STATE_FAULT,
    BATTERY_STATE_MAINTENANCE
} battery_state_t;

/* Charge algorithms */
typedef enum {
    CHARGE_BULK = 0,
    CHARGE_ABSORPTION,
    CHARGE_FLOAT,
    CHARGE_EQUALIZE
} charge_stage_t;

/* Battery management context */
typedef struct {
    battery_state_t state;
    charge_stage_t charge_stage;
    
    battery_bank_t banks[MAX_BATTERY_BANKS];
    int active_bank_count;
    
    /* State of Charge estimation */
    double soc_coulomb_counting;    // Coulomb counting method
    double soc_voltage_based;       // Voltage-based method
    double soc_estimated;           // Final estimated SOC
    double soc_smoothed;           // Smoothed SOC for control
    
    /* Battery parameters */
    double capacity_remaining;      // Remaining capacity (Wh)
    double capacity_nominal;        // Nominal capacity (Wh)
    double internal_resistance;     // Internal resistance (Ohms)
    double health_percentage;       // State of Health (%)
    
    /* Thermal management */
    double temperature;
    double temperature_ambient;
    bool cooling_active;
    bool heating_active;
    
    /* Charge/discharge limits */
    double max_charge_current;
    double max_discharge_current;
    double max_charge_power;
    double max_discharge_power;
    
    /* Statistics */
    double total_charge_energy;
    double total_discharge_energy;
    double cycle_depth_accumulated;
    int cycle_count;
    
    /* Timing */
    time_t absorption_start_time;
    time_t float_start_time;
    double absorption_duration;
    double float_duration;
    
    /* Fault detection */
    bool overvoltage_fault;
    bool undervoltage_fault;
    bool overcurrent_fault;
    bool overtemperature_fault;
    char last_fault_reason[64];
} battery_system_t;

/* Function prototypes */
int battery_init(battery_system_t* bat, const system_config_t* config);
void battery_update_measurements(battery_system_t* bat, system_measurements_t* measurements);
double battery_calculate_soc(battery_system_t* bat, system_measurements_t* measurements);
void battery_manage_charging(battery_system_t* bat, double available_power, double load_power);
void battery_manage_discharging(battery_system_t* bat, double load_power, bool grid_available);
double battery_calculate_max_charge(battery_system_t* bat);
double battery_calculate_max_discharge(battery_system_t* bat);
bool battery_check_limits(battery_system_t* bat, system_measurements_t* measurements);
void battery_thermal_management(battery_system_t* bat);
void battery_equalize(battery_system_t* bat);
void battery_log_status(const battery_system_t* bat);

#endif /* BATTERY_H */
