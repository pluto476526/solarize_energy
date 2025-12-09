#ifndef CORE_H
#define CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// System-wide constants
#define MAX_PV_STRINGS         4
#define MAX_BATTERY_BANKS      4
#define MAX_CONTROLLABLE_LOADS 12
#define MAX_IRRIGATION_ZONES   8
#define MAX_EV_CHARGERS        2

// System operating modes
typedef enum {
    MODE_NORMAL = 0,           // Grid-connected normal operation
    MODE_ISLAND,               // Off-grid/island mode
    MODE_CRITICAL,             // Critical loads only
    MODE_MAINTENANCE,          // Maintenance/testing
    MODE_EMERGENCY             // Emergency/fallback
} system_mode_t;

// Battery state of charge categories
typedef enum {
    SOC_CRITICAL = 0,          // < 20%
    SOC_LOW,                   // 20-40%
    SOC_MEDIUM,                // 40-70%
    SOC_HIGH,                  // 70-90%
    SOC_FULL                   // > 90%
} soc_category_t;

// Load priority levels
typedef enum {
    PRIORITY_CRITICAL = 0,     // Essential loads (refrigeration, medical, etc.)
    PRIORITY_HIGH,             // Important loads (lighting, communications)
    PRIORITY_MEDIUM,           // Comfort loads (HVAC, entertainment)
    PRIORITY_LOW,              // Deferrable loads (EV charging, hot water)
    PRIORITY_NON_ESSENTIAL     // Non-essential loads
} load_priority_t;

// Irrigation control modes
typedef enum {
    IRRIGATION_AUTO = 0,       // Automatic based on soil moisture
    IRRIGATION_SCHEDULED,      // Scheduled timing
    IRRIGATION_MANUAL,         // Manual control
    IRRIGATION_OFF             // Irrigation disabled
} irrigation_mode_t;

// Real-time measurements structure
typedef struct {
    double grid_power;          // Grid power (positive = import, negative = export)
    double grid_voltage;        // Grid voltage (V)
    double grid_frequency;      // Grid frequency (Hz)
    
    double pv_power_total;      // Total PV power production (W)
    double pv_voltage[MAX_PV_STRINGS]; // Per-string voltages
    double pv_current[MAX_PV_STRINGS]; // Per-string currents
    uint8_t pv_strings_active;  // Number of active PV strings
    
    double battery_power;       // Battery power (positive = discharging, negative = charging)
    double battery_voltage;     // Battery voltage (V)
    double battery_current;     // Battery current (A)
    double battery_soc;         // State of charge (%)
    double battery_temp;        // Battery temperature (°C)
    
    double load_power_total;    // Total load power (W)
    double load_power_critical; // Critical loads power (W)
    double load_power_deferrable; // Deferrable loads power (W)
    
    double irrigation_power;    // Irrigation system power (W)
    double ev_charging_power;   // EV charging power (W)
    
    time_t timestamp;           // Unix timestamp of measurement
} system_measurements_t;

/* System status structure */
typedef struct {
    system_mode_t mode;         // Current system mode
    bool grid_available;        // Grid connection status
    bool grid_stable;           // Grid quality stable
    bool battery_available;     // Battery system status
    bool pv_available;          // PV system status
    bool critical_loads_on;     // Critical loads status
    
    soc_category_t battery_soc_category; // Battery SOC category
    uint8_t alarms;             // Bitmask of active alarms
    uint8_t warnings;           // Bitmask of active warnings
    
    time_t last_mode_change;    // When mode was last changed
    time_t uptime;              // System uptime in seconds
} system_status_t;

// Control commands structur
typedef struct {
    double battery_setpoint;     // Battery power setpoint (W)
    bool pv_curtail;            // PV curtailment active
    double pv_curtail_percent;  // PV curtailment percentage
    
    bool load_shed[MAX_CONTROLLABLE_LOADS]; // Load shed commands
    bool irrigation_enable[MAX_IRRIGATION_ZONES]; // Irrigation zone control
    double ev_charge_rate[MAX_EV_CHARGERS]; // EV charge rate setpoints
    
    bool grid_connect;          // Command to connect to grid
    bool island;               // Command to island from grid
} control_commands_t;

// Error and alarm codes
typedef enum {
    ALARM_GRID_FAILURE = 0,
    ALARM_BATTERY_OVER_TEMP,
    ALARM_BATTERY_LOW_SOC,
    ALARM_PV_DISCONNECT,
    ALARM_OVERLOAD,
    ALARM_COMM_FAILURE,
    ALARM_IRRIGATION_FAULT,
    ALARM_EV_CHARGER_FAULT
} alarm_code_t;

// Warning codes
typedef enum {
    WARNING_BATTERY_HIGH_TEMP = 0,
    WARNING_BATTERY_MID_SOC,
    WARNING_PV_LOW_PRODUCTION,
    WARNING_GRID_UNSTABLE,
    WARNING_HIGH_LOAD,
    WARNING_IRRIGATION_SKIPPED
} warning_code_t;

// Load definition structure
typedef struct {
    char id[32];                // Load identifier
    double rated_power;         // Rated power (W)
    load_priority_t priority;   // Load priority
    bool is_deferrable;         // Can be deferred
    bool is_sheddable;          // Can be shed
    double min_on_time;         // Minimum on time (seconds)
    double min_off_time;        // Minimum off time (seconds)
    time_t last_state_change;   // Last state change time
    bool current_state;         // Current on/off state
} load_definition_t;

// Irrigation zone structure
typedef struct {
    char zone_id[32];           // Zone identifier
    double area_sqft;           // Area in square feet
    double water_flow_rate;     // Flow rate (GPM)
    double power_consumption;   // Pump power (W)
    double soil_moisture;       // Current soil moisture (%)
    double moisture_threshold;  // Moisture threshold for irrigation
    double watering_duration;   // Watering duration (minutes)
    bool enabled;               // Zone enabled
    time_t last_watered;        // Last watering time
} irrigation_zone_t;

// EV charger structure
typedef struct {
    char ev_id[32];             // EV identifier
    double max_charge_rate;     // Maximum charge rate (W)
    double min_charge_rate;     // Minimum charge rate (W)
    double target_soc;          // Target state of charge (%)
    double current_soc;         // Current state of charge (%)
    bool charging_enabled;      // Charging enabled
    bool fast_charge_requested; // Fast charge requested
    time_t charge_start_time;   // Charge start time
} ev_charger_t;

// PV string information
typedef struct {
    char string_id[32];         // String identifier
    double max_power;           // Maximum power (W)
    double max_voltage;         // Maximum voltage (V)
    double max_current;         // Maximum current (A)
    bool enabled;               // String enabled
    bool fault;                 // Fault detected
    double efficiency;          // String efficiency (%)
} pv_string_t;

// Battery bank description (one physical bank)
typedef struct {
    char bank_id[32];
    double nominal_voltage;     // V (e.g. 48.0)
    int cells_in_series;           // cells in series (S)
    int parallel_strings;       // parallel strings (P)
    double capacity_wh;         // nominal energy per bank in Wh (e.g. 10000 Wh)
    double max_charge_power;    // W
    double max_discharge_power; // W
    int cycle_count;
    time_t last_full_charge_ts;
} battery_bank_t;

// System config structure
typedef struct {
    // General settings
    char system_name[64];
    double nominal_voltage;
    double max_grid_import;
    double max_grid_export;
    
    // Battery settings
    double battery_soc_min;      // Minimum SOC for discharge
    double battery_soc_max;      // Maximum SOC for charge
    double battery_temp_max;     // Maximum temperature
    double battery_reserve_soc;  // Reserve SOC for outages
    battery_bank_t batteries[MAX_BATTERY_BANKS];
    int bank_count;
    
    // PV settings
    double pv_curtail_start;     // SOC level to start PV curtailment
    double pv_curtail_max;       // Maximum curtailment percentage
    
    // Load management
    load_definition_t loads[MAX_CONTROLLABLE_LOADS];
    int load_count;
    
    // Irrigation settings
    irrigation_zone_t zones[MAX_IRRIGATION_ZONES];
    int zone_count;
    irrigation_mode_t irrigation_mode;
    double irrigation_power_limit;
    
    // EV charging
    ev_charger_t ev_chargers[MAX_EV_CHARGERS];
    int ev_charger_count;
    double ev_charge_power_limit;
    
    // Control parameters
    double control_interval;     // Control loop interval (seconds)
    double measurement_interval; // Measurement interval (seconds)
    double hysteresis;           // Hysteresis for mode changes
} system_config_t;

/* System statistics */
typedef struct {
    double pv_energy_total;      // Total PV energy produced (kWh)
    double grid_import_total;    // Total grid energy imported (kWh)
    double grid_export_total;    // Total grid energy exported (kWh)
    double battery_charge_total; // Total battery charge energy (kWh)
    double battery_discharge_total; // Total battery discharge energy (kWh)
    double load_energy_total;    // Total load energy consumed (kWh)
    double irrigation_energy_total; // Total irrigation energy (kWh)
    double ev_charge_energy_total;  // Total EV charge energy (kWh)
    
    uint32_t grid_outage_count;  // Number of grid outages
    uint32_t load_shed_count;    // Number of load shed events
    uint32_t island_count;       // Number of islanding events
    
    time_t stats_start_time;     // Statistics collection start time
} system_statistics_t;

#endif /* CORE_H */
