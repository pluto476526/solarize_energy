/* battery.h - Add these missing definitions */
#ifndef BATTERY_H
#define BATTERY_H

#include "core.h"
#include <time.h>
#include <stdbool.h>


// Modern LFP banks, multi-bank support
#define DEFAULT_BANK_NOMINAL_V          48.0
#define DEFAULT_BANK_SERIES_CELLS       16          // ~48 V nominal
#define DEFAULT_BANK_PARALLEL_STRINGS   1
#define DEFAULT_BANK_CAPACITY_WH        10000.0     // 10 kWh per bank
#define DEFAULT_BANK_MAX_CHARGE_W       5000.0
#define DEFAULT_BANK_MAX_DISCHARGE_W    5000.0

// Chemistry enum for supported battery types
typedef enum {
    BAT_CHEM_LFP = 0,
    BAT_CHEM_NMC,
    BAT_CHEM_LEAD_ACID
} battery_chemistry_e;

// Battery management states
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

// Battery management context
typedef struct {
    // Configuration & topology
    battery_chemistry_e chemistry;
    battery_bank_t banks[MAX_BATTERY_BANKS];
    int bank_count;                 // configured banks
    int active_bank_count;          // number of banks in service

    // Time & coulomb counting
    double accumulated_ah;          // Ah (coulomb counter)
    time_t last_update_ts;          // last timestamp for coulomb integration
    time_t last_energy_update_ts;   // for energy integration

    // State of Charge estimation
    double soc_coulomb;             // % from coulomb counting
    double soc_voltage;             // % from voltage mapping
    double soc_estimated;           // fusion result %
    double soc_smoothed;            // smoothed for control

    // Capacity and health
    double nominal_voltage;
    double capacity_nominal_wh;     // total Wh across active banks
    double capacity_remaining_wh;   // Wh
    double health_percent;          // %

    // Thermal
    double temperature_c;           // measured pack temperature
    double ambient_temperature_c;
    bool cooling_active;
    bool heating_active;

    // Limits
    double max_charge_current_a;    // A
    double max_discharge_current_a; // A
    double max_charge_power_w;      // W
    double max_discharge_power_w;   // W

    // Statistics
    double total_charge_wh;         // Wh
    double total_discharge_wh;      // Wh
    double cycle_depth_accumulated; // for cycle estimation
    int cycle_count;
    int deep_cycle_count;           // cycles below 20% DOD

    // Timing for charge stages
    time_t absorption_start_ts;
    time_t float_start_ts;
    double absorption_duration_s;
    double float_duration_s;

    // Faults
    bool overvoltage_fault;
    bool undervoltage_fault;
    bool overcurrent_fault;
    bool overtemperature_fault;
    char last_fault_reason[128];
    time_t fault_timestamp;         // When fault occurred
    int fault_clear_attempts;       // Auto-clear attempts

    // State machine
    battery_state_t state;
    battery_state_t previous_state;  // For recovery
    charge_stage_t charge_stage;

    // Balancing
    bool balancing_enabled;
    double max_cell_voltage;
    double min_cell_voltage;
    double cell_voltage_spread;     // V

    // Tuning
    double soc_voltage_weight;      // 0..1 weight for coulomb vs voltage fusion
    double soc_smoothing_alpha;     // 0..1 smoothing factor
    double min_operating_soc;       // minimum allowed SOC before hard limits (%)
    double max_operating_soc;       // maximum allowed SOC (%)
    
    // Configuration parameters
    double bulk_charge_soc_limit;       // Switch to absorption at this SOC
    double absorption_charge_soc_limit; // Switch to float at this SOC
    double equalize_voltage;            // Equalization voltage per cell
    double float_voltage;               // Float voltage per cell
    
    // Coulomb counting calibration
    double coulomb_efficiency;      // 0.95-0.99 for LFP
    double self_discharge_rate;     // % per day

} battery_system_t;

// Public API
int battery_init(battery_system_t* bat, const system_config_t* config);
void battery_update_measurements(battery_system_t* bat, system_measurements_t* measurements);
int battery_calculate_soc(battery_system_t* bat, system_measurements_t* measurements);
void battery_manage_charging(battery_system_t* bat, double available_power, double load_power);
void battery_manage_discharging(battery_system_t* bat, double load_power, bool grid_available);
double battery_calculate_max_charge(battery_system_t* bat);
double battery_calculate_max_discharge(battery_system_t* bat);
bool battery_check_limits(battery_system_t* bat, system_measurements_t* measurements);
void battery_thermal_management(battery_system_t* bat);
void battery_equalize(battery_system_t* bat);
void battery_log_status(const battery_system_t* bat);
void battery_clear_faults(battery_system_t* bat);
bool battery_check_balancing(battery_system_t* bat, system_measurements_t* measurements);
void battery_update_capacity_health(battery_system_t* bat);
void battery_enter_maintenance_mode(battery_system_t* bat);

#endif /* BATTERY_H */