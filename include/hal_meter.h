#ifndef HAL_METER_H
#define HAL_METER_H

#include "hal.h"
#include "hal_modbus.h"

/* Energy meter types */
typedef enum {
    METER_JANITZA = 0,      /* Janitza UMG 604/96 */
    METER_SCHNEIDER,        /* Schneider PM/PowerLogic */
    METER_ABB,              /* ABB B-Series */
    METER_SIEMENS,          /* Siemens SENTRON */
    METER_EASTRON,          /* Eastron SDM series */
    METER_SDM,              /* SDM120/230/630 */
    METER_GENERIC           /* Generic Modbus meter */
} energy_meter_type_t;

/* Meter measurement types */
typedef enum {
    METER_MEASUREMENT_GRID = 0,
    METER_MEASUREMENT_PV,
    METER_MEASUREMENT_LOAD,
    METER_MEASUREMENT_GENERATOR
} meter_measurement_type_t;

/* Meter phase measurements */
typedef struct {
    float voltage;          /* Phase voltage (V) */
    float current;          /* Phase current (A) */
    float power;            /* Phase power (W) */
    float power_factor;     /* Phase power factor */
    float energy_import;    /* Imported energy (kWh) */
    float energy_export;    /* Exported energy (kWh) */
} meter_phase_t;

/* Meter measurements */
typedef struct {
    meter_measurement_type_t type; /* Measurement type */
    
    /* Three-phase measurements */
    meter_phase_t phase_l1;
    meter_phase_t phase_l2;
    meter_phase_t phase_l3;
    
    /* Total measurements */
    float voltage_avg;      /* Average voltage (V) */
    float current_avg;      /* Average current (A) */
    float power_total;      /* Total power (W) */
    float power_factor_avg; /* Average power factor */
    float frequency;        /* Frequency (Hz) */
    
    /* Energy totals */
    float energy_import_total;  /* Total imported energy (kWh) */
    float energy_export_total;  /* Total exported energy (kWh) */
    
    uint32_t status;        /* Meter status flags */
    time_t timestamp;       /* Measurement time */
} meter_measurement_t;

/* Meter configuration */
typedef struct {
    energy_meter_type_t meter_type; /* Meter type */
    hal_interface_t interface;      /* Communication interface */
    uint32_t device_id;             /* HAL device ID */
    meter_measurement_type_t measurement_type; /* What this meter measures */
    float ct_ratio;                 /* Current transformer ratio */
    float pt_ratio;                 /* Potential transformer ratio */
    uint8_t phase_count;            /* Number of phases (1 or 3) */
    float rated_voltage;            /* Rated voltage (V) */
    float rated_current;            /* Rated current (A) */
} meter_config_t;

/* Meter statistics */
typedef struct {
    float peak_power_import;    /* Peak import power (W) */
    float peak_power_export;    /* Peak export power (W) */
    time_t peak_time_import;    /* Time of peak import */
    time_t peak_time_export;    /* Time of peak export */
    float avg_power;            /* Average power (W) */
    uint32_t outage_count;      /* Number of outages */
    time_t last_reset;          /* Last statistics reset */
} meter_stats_t;

/* Initialize energy meter */
hal_error_t hal_meter_init(const meter_config_t* config, uint32_t* meter_id);

/* Get meter measurements */
hal_error_t hal_meter_get_measurements(uint32_t meter_id, meter_measurement_t* measurements);

/* Get meter status */
hal_error_t hal_meter_get_status(uint32_t meter_id, device_info_t* info);

/* Get meter statistics */
hal_error_t hal_meter_get_statistics(uint32_t meter_id, meter_stats_t* stats);

/* Reset meter energy counters */
hal_error_t hal_meter_reset_energy(uint32_t meter_id);

/* Set meter configuration */
hal_error_t hal_meter_set_config(uint32_t meter_id, const meter_config_t* config);

/* Calibrate meter */
hal_error_t hal_meter_calibrate(uint32_t meter_id, float voltage_ref, float current_ref);

/* Specific meter implementations */

/* Janitza UMG 604 implementation */
hal_error_t hal_meter_janitza_read_measurements(uint32_t device_id, meter_measurement_t* measurements);
hal_error_t hal_meter_janitza_reset_energy(uint32_t device_id);

/* Eastron SDM implementation */
hal_error_t hal_meter_eastron_read_measurements(uint32_t device_id, meter_measurement_t* measurements);
hal_error_t hal_meter_eastron_reset_energy(uint32_t device_id);

/* Schneider PM implementation */
hal_error_t hal_meter_schneider_read_measurements(uint32_t device_id, meter_measurement_t* measurements);
hal_error_t hal_meter_schneider_reset_energy(uint32_t device_id);

#endif /* HAL_METER_H */
