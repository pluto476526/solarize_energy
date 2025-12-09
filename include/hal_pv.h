#ifndef HAL_PV_H
#define HAL_PV_H

#include "hal.h"
#include "hal_modbus.h"

/* PV inverter types */
typedef enum {
    PV_INVERTER_SMA = 0,    /* SMA Sunny Boy/Tripower */
    PV_INVERTER_FRONIUS,    /* Fronius Symo/Prismo */
    PV_INVERTER_SOLIS,      /* Solis/SolisCloud */
    PV_INVERTER_VICTRON,    /* Victron MultiPlus/Quattro */
    PV_INVERTER_HUAWEI,     /* Huawei SUN2000 */
    PV_INVERTER_GOODWE,     /* Goodwe DNS/ET */
    PV_INVERTER_GENERIC     /* Generic Modbus inverter */
} pv_inverter_type_t;

/* PV inverter operating modes */
typedef enum {
    PV_MODE_OFF = 0,
    PV_MODE_STARTUP,
    PV_MODE_MPPT,
    PV_MODE_THROTTLED,
    PV_MODE_SHUTDOWN,
    PV_MODE_FAULT,
    PV_MODE_STANDBY,
    PV_MODE_TEST
} pv_inverter_mode_t;

/* PV inverter configuration */
typedef struct {
    pv_inverter_type_t type;    /* Inverter type */
    hal_interface_t interface;  /* Communication interface */
    uint32_t device_id;         /* HAL device ID */
    float max_power;            /* Maximum power rating (W) */
    float max_voltage;          /* Maximum DC voltage (V) */
    float max_current;          /* Maximum DC current (A) */
    uint8_t mppt_count;         /* Number of MPPTs */
    uint8_t string_count;       /* Number of PV strings */
} pv_inverter_config_t;

/* PV string measurements */
typedef struct {
    float voltage;              /* String voltage (V) */
    float current;              /* String current (A) */
    float power;                /* String power (W) */
    float temperature;          /* String temperature (°C) */
    uint16_t status;            /* String status bits */
} pv_string_measurement_t;

/* PV inverter measurements */
typedef struct {
    float dc_voltage;           /* Total DC voltage (V) */
    float dc_current;           /* Total DC current (A) */
    float dc_power;             /* Total DC power (W) */
    float ac_voltage;           /* AC output voltage (V) */
    float ac_current;           /* AC output current (A) */
    float ac_power;             /* AC output power (W) */
    float ac_frequency;         /* AC frequency (Hz) */
    float efficiency;           /* Current efficiency (%) */
    float temperature;          /* Inverter temperature (°C) */
    pv_inverter_mode_t mode;    /* Operating mode */
    uint32_t status;            /* Status flags */
    uint32_t error_code;        /* Error code if any */
    time_t timestamp;           /* Measurement time */
    
    /* Per-string measurements */
    pv_string_measurement_t strings[8];
    uint8_t string_count;
} pv_inverter_measurement_t;

/* PV inverter statistics */
typedef struct {
    float total_energy;         /* Total energy produced (kWh) */
    float daily_energy;         /* Daily energy produced (kWh) */
    float monthly_energy;       /* Monthly energy produced (kWh) */
    uint32_t operating_hours;   /* Total operating hours */
    uint32_t start_count;       /* Number of starts */
    uint32_t error_count;       /* Number of errors */
    time_t last_reset;          /* Last statistics reset */
} pv_inverter_stats_t;

/* PV inverter commands */
typedef struct {
    float power_limit;          /* Power limit (0-100%) */
    bool enable_output;         /* Enable AC output */
    bool enable_mppt;           /* Enable MPPT */
    uint16_t command_code;      /* Manufacturer-specific command */
} pv_inverter_command_t;

/* Initialize PV inverter */
hal_error_t hal_pv_init_inverter(const pv_inverter_config_t* config, uint32_t* inverter_id);

/* Get inverter measurements */
hal_error_t hal_pv_get_measurements(uint32_t inverter_id, pv_inverter_measurement_t* measurements);

/* Get inverter status */
hal_error_t hal_pv_get_status(uint32_t inverter_id, device_info_t* info);

/* Get inverter statistics */
hal_error_t hal_pv_get_statistics(uint32_t inverter_id, pv_inverter_stats_t* stats);

/* Send command to inverter */
hal_error_t hal_pv_send_command(uint32_t inverter_id, const pv_inverter_command_t* command);

/* Set power limit */
hal_error_t hal_pv_set_power_limit(uint32_t inverter_id, float limit_percent);

/* Enable/disable inverter */
hal_error_t hal_pv_set_enabled(uint32_t inverter_id, bool enabled);

/* Clear inverter errors */
hal_error_t hal_pv_clear_errors(uint32_t inverter_id);

/* Reset inverter statistics */
hal_error_t hal_pv_reset_statistics(uint32_t inverter_id);

/* Scan for inverters */
hal_error_t hal_pv_scan_inverters(uint32_t* count, uint32_t* inverter_ids);

/* Specific inverter implementations */

/* SMA Sunny Boy implementation */
hal_error_t hal_pv_sma_read_measurements(uint32_t device_id, pv_inverter_measurement_t* measurements);
hal_error_t hal_pv_sma_send_command(uint32_t device_id, const pv_inverter_command_t* command);

/* Fronius Symo implementation */
hal_error_t hal_pv_fronius_read_measurements(uint32_t device_id, pv_inverter_measurement_t* measurements);
hal_error_t hal_pv_fronius_send_command(uint32_t device_id, const pv_inverter_command_t* command);

/* Victron implementation */
hal_error_t hal_pv_victron_read_measurements(uint32_t device_id, pv_inverter_measurement_t* measurements);
hal_error_t hal_pv_victron_send_command(uint32_t device_id, const pv_inverter_command_t* command);

#endif /* HAL_PV_H */
