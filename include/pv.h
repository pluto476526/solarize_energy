#ifndef PV_H
#define PV_H

#include "core.h"

/* PV system states */
typedef enum {
    PV_STATE_OFF = 0,
    PV_STATE_STARTING,
    PV_STATE_MPPT,
    PV_STATE_CURTAILED,
    PV_STATE_FAULT,
    PV_STATE_MAINTENANCE
} pv_state_t;

/* MPPT algorithms */
typedef enum {
    MPPT_OFF = 0,
    MPPT_PERTURB_OBSERVE,
    MPPT_INCREMENTAL_CONDUCTANCE,
    MPPT_CONSTANT_VOLTAGE
} mppt_algorithm_t;

/* PV system context */
typedef struct {
    pv_state_t state;
    mppt_algorithm_t mppt_algorithm;
    
    pv_string_t strings[MAX_PV_STRINGS];
    int active_string_count;
    
    double total_capacity;      // Total installed capacity (W)
    double available_power;     // Currently available power (W)
    double max_operating_power; // Current max operating power (W)
    
    /* MPPT tracking */
    double mppt_voltage_ref;
    double mppt_power_ref;
    double mppt_step_size;
    
    /* Statistics */
    double daily_energy;
    double monthly_energy;
    double total_energy;
    time_t last_reset_time;
    
    /* Fault detection */
    uint32_t fault_count;
    time_t last_fault_time;
    char last_fault_reason[64];
} pv_system_t;

/* Function prototypes */
int pv_init(pv_system_t* pv, const system_config_t* config);
void pv_update_measurements(pv_system_t* pv, system_measurements_t* measurements);
double pv_calculate_available_power(pv_system_t* pv, system_measurements_t* measurements);
void pv_run_mppt(pv_system_t* pv, system_measurements_t* measurements);
void pv_apply_curtailment(pv_system_t* pv, double curtail_percent);
bool pv_detect_faults(pv_system_t* pv, system_measurements_t* measurements);
void pv_clear_faults(pv_system_t* pv);
void pv_log_status(const pv_system_t* pv);
double pv_get_efficiency(const pv_system_t* pv);

#endif /* PV_H */
