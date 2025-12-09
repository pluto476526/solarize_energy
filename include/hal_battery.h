#ifndef HAL_BATTERY_H
#define HAL_BATTERY_H

#include "hal.h"
#include "hal_modbus.h"
#include "hal_can.h"

/* Battery chemistry types */
typedef enum {
    BATTERY_LFP = 0,        /* Lithium Iron Phosphate */
    BATTERY_NMC,            /* Lithium NMC */
    BATTERY_LTO,            /* Lithium Titanate */
    BATTERY_LEAD_ACID,      /* Lead Acid */
    BATTERY_AGM,            /* AGM */
    BATTERY_GEL             /* Gel */
} battery_chemistry_t;

/* Battery management system types */
typedef enum {
    BMS_DALY = 0,           /* Daly BMS */
    BMS_REC,                /* REC BMS */
    BMS_BATTERY_MONITOR,    /* Victron Battery Monitor */
    BMS_SMA,                /* SMA Sunny Island */
    BMS_VICTRON,            /* Victron BMV/SmartShunt */
    BMS_SOLAX,              /* Solax Triple Power */
    BMS_GENERIC             /* Generic BMS */
} bms_type_t;

/* Battery cell status */
typedef struct {
    float voltage;          /* Cell voltage (V) */
    float temperature;      /* Cell temperature (°C) */
    uint8_t balance_status; /* Balance status (0=idle, 1=balancing) */
} battery_cell_t;

/* Battery measurements */
typedef struct {
    float voltage;          /* Pack voltage (V) */
    float current;          /* Current (A, positive=discharge, negative=charge) */
    float power;            /* Power (W) */
    float soc;              /* State of charge (%) */
    float soh;              /* State of health (%) */
    float temperature;      /* Average temperature (°C) */
    float cell_voltage_max; /* Maximum cell voltage (V) */
    float cell_voltage_min; /* Minimum cell voltage (V) */
    float cell_temp_max;    /* Maximum cell temperature (°C) */
    float cell_temp_min;    /* Minimum cell temperature (°C) */
    uint32_t status;        /* Status flags */
    uint32_t error_code;    /* Error code if any */
    time_t timestamp;       /* Measurement time */
} battery_measurement_t;

/* Battery configuration */
typedef struct {
    bms_type_t bms_type;    /* BMS type */
    battery_chemistry_t chemistry; /* Battery chemistry */
    hal_interface_t interface; /* Communication interface */
    uint32_t device_id;     /* HAL device ID */
    float nominal_voltage;  /* Nominal voltage (V) */
    float capacity_ah;      /* Capacity (Ah) */
    float capacity_wh;      /* Capacity (Wh) */
    uint16_t series_cells;  /* Number of cells in series */
    uint16_t parallel_cells; /* Number of cells in parallel */
    float max_charge_current; /* Maximum charge current (A) */
    float max_discharge_current; /* Maximum discharge current (A) */
} battery_config_t;

/* Battery statistics */
typedef struct {
    float total_charge_energy;      /* Total charge energy (kWh) */
    float total_discharge_energy;   /* Total discharge energy (kWh) */
    uint32_t cycle_count;           /* Number of cycles */
    uint32_t charge_count;          /* Number of charge cycles */
    uint32_t error_count;           /* Number of errors */
    time_t last_full_charge;        /* Last full charge time */
    time_t last_equalization;       /* Last equalization time */
} battery_stats_t;

/* Battery commands */
typedef struct {
    bool enable_charge;     /* Enable charging */
    bool enable_discharge;  /* Enable discharging */
    float charge_current;   /* Charge current limit (A) */
    float discharge_current; /* Discharge current limit (A) */
    float charge_voltage;   /* Charge voltage limit (V) */
    bool start_equalization; /* Start equalization cycle */
    uint16_t command_code;  /* Manufacturer-specific command */
} battery_command_t;

/* Initialize battery system */
hal_error_t hal_battery_init(const battery_config_t* config, uint32_t* battery_id);

/* Get battery measurements */
hal_error_t hal_battery_get_measurements(uint32_t battery_id, battery_measurement_t* measurements);

/* Get detailed cell information */
hal_error_t hal_battery_get_cell_info(uint32_t battery_id, battery_cell_t* cells, uint16_t* count);

/* Get battery status */
hal_error_t hal_battery_get_status(uint32_t battery_id, device_info_t* info);

/* Get battery statistics */
hal_error_t hal_battery_get_statistics(uint32_t battery_id, battery_stats_t* stats);

/* Send command to battery */
hal_error_t hal_battery_send_command(uint32_t battery_id, const battery_command_t* command);

/* Set charge current limit */
hal_error_t hal_battery_set_charge_current(uint32_t battery_id, float current);

/* Set discharge current limit */
hal_error_t hal_battery_set_discharge_current(uint32_t battery_id, float current);

/* Enable/disable battery */
hal_error_t hal_battery_set_enabled(uint32_t battery_id, bool enabled);

/* Clear battery errors */
hal_error_t hal_battery_clear_errors(uint32_t battery_id);

/* Reset battery statistics */
hal_error_t hal_battery_reset_statistics(uint32_t battery_id);

/* Specific BMS implementations */

/* Daly BMS implementation (CAN & UART) */
hal_error_t hal_battery_daly_read_measurements(uint32_t device_id, battery_measurement_t* measurements);
hal_error_t hal_battery_daly_send_command(uint32_t device_id, const battery_command_t* command);

/* REC BMS implementation (CAN) */
hal_error_t hal_battery_rec_read_measurements(uint32_t device_id, battery_measurement_t* measurements);
hal_error_t hal_battery_rec_send_command(uint32_t device_id, const battery_command_t* command);

/* Victron BMV implementation (VE.Direct/VE.Can) */
hal_error_t hal_battery_victron_read_measurements(uint32_t device_id, battery_measurement_t* measurements);
hal_error_t hal_battery_victron_send_command(uint32_t device_id, const battery_command_t* command);

#endif /* HAL_BATTERY_H */
