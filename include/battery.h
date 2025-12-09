#ifndef BATTERY_H
#define BATTERY_H

#include "core.h"
#include <time.h>
#include <stdbool.h>

/* Chemistry enum for supported battery types */
typedef enum {
    BAT_CHEM_LFP = 0,
    BAT_CHEM_NMC,
    BAT_CHEM_LEAD_ACID
} battery_chemistry_e;


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
    /* Configuration & topology */
    battery_chemistry_e chemistry;
    battery_bank_t banks[MAX_BATTERY_BANKS];
    int bank_count;                 /* configured banks */
    int active_bank_count;          /* number of banks in service */

    /* Time & coulomb counting stored in object (no statics) */
    double accumulated_ah;          /* Ah (coulomb counter) */
    time_t last_update_ts;          /* last timestamp for coulomb integration */

    /* State of Charge estimation */
    double soc_coulomb;             /* % from coulomb counting */
    double soc_voltage;             /* % from voltage mapping */
    double soc_estimated;           /* fusion result % */
    double soc_smoothed;            /* smoothed for control */

    /* Capacity and health */
    double capacity_nominal_wh;     /* total Wh across active banks */
    double capacity_remaining_wh;   /* Wh */
    double health_percent;          /* % */

    /* Thermal */
    double temperature_c;           /* measured pack temperature */
    double ambient_temperature_c;
    bool cooling_active;
    bool heating_active;

    /* Limits */
    double max_charge_current_a;    /* A */
    double max_discharge_current_a; /* A */
    double max_charge_power_w;      /* W */
    double max_discharge_power_w;   /* W */

    /* Statistics */
    double total_charge_wh;         /* Wh */
    double total_discharge_wh;      /* Wh */
    double cycle_depth_accumulated; /* for cycle estimation */
    int cycle_count;

    /* Timing for charge stages */
    time_t absorption_start_ts;
    time_t float_start_ts;
    double absorption_duration_s;
    double float_duration_s;

    /* Faults */
    bool overvoltage_fault;
    bool undervoltage_fault;
    bool overcurrent_fault;
    bool overtemperature_fault;
    char last_fault_reason[128];

    /* State machine */
    battery_state_t state;
    charge_stage_t charge_stage;

    /* Tuning (exposed for tests & runtime adjustment) */
    double soc_voltage_weight;      /* 0..1 weight for coulomb vs voltage fusion */
    double soc_smoothing_alpha;     /* 0..1 smoothing factor */
    double min_operating_soc;       /* minimum allowed SOC before hard limits (%) */
    double max_operating_soc;       /* maximum allowed SOC (%) */

} battery_system_t;

/* Public API - function signatures preserved */
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
