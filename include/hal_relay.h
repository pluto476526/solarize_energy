#ifndef HAL_RELAY_H
#define HAL_RELAY_H

#include "hal.h"
#include "hal_modbus.h"

/* Relay module types */
typedef enum {
    RELAY_WAGO_750 = 0,     /* WAGO 750 series */
    RELAY_PHOENIX,          /* Phoenix Contact */
    RELAY_SCHNEIDER,        /* Schneider Electric */
    RELAY_SIEMENS,          /* Siemens LOGO/S7 */
    RELAY_OPTO22,           /* Opto22 SNAP I/O */
    RELAY_GENERIC           /* Generic Modbus relay */
} relay_module_type_t;

/* Relay types */
typedef enum {
    RELAY_TYPE_SPST = 0,    /* Single Pole Single Throw */
    RELAY_TYPE_SPDT,        /* Single Pole Double Throw */
    RELAY_TYPE_DPST,        /* Double Pole Single Throw */
    RELAY_TYPE_DPDT         /* Double Pole Double Throw */
} relay_type_t;

/* Relay state */
typedef enum {
    RELAY_STATE_OFF = 0,
    RELAY_STATE_ON,
    RELAY_STATE_TRIPPED,
    RELAY_STATE_FAULT
} relay_state_t;

/* Relay configuration */
typedef struct {
    relay_module_type_t module_type; /* Module type */
    hal_interface_t interface;       /* Communication interface */
    uint32_t device_id;              /* HAL device ID */
    uint8_t channel_count;           /* Number of channels */
    relay_type_t relay_type;         /* Type of relays */
    float rated_current;             /* Rated current per channel (A) */
    float rated_voltage;             /* Rated voltage (V) */
} relay_config_t;

/* Relay channel state */
typedef struct {
    relay_state_t state;    /* Current state */
    bool commanded_state;   /* Commanded state */
    float current;          /* Measured current (A) */
    float voltage;         /* Measured voltage (V) */
    uint32_t on_count;      /* Number of on cycles */
    uint32_t fault_count;   /* Number of faults */
    time_t last_change;     /* Last state change time */
} relay_channel_state_t;

/* Relay module measurements */
typedef struct {
    float input_voltage;    /* Module input voltage (V) */
    float temperature;      /* Module temperature (°C) */
    uint32_t status;        /* Module status flags */
    uint32_t error_code;    /* Error code if any */
    time_t timestamp;       /* Measurement time */
} relay_module_measurement_t;

/* Relay module statistics */
typedef struct {
    uint32_t total_operations;  /* Total relay operations */
    uint32_t fault_operations;  /* Faulty operations */
    uint32_t overcurrent_events; /* Overcurrent events */
    uint32_t overtemperature_events; /* Overtemperature events */
    time_t last_reset;       /* Last statistics reset */
} relay_module_stats_t;

/* Initialize relay module */
hal_error_t hal_relay_init_module(const relay_config_t* config, uint32_t* module_id);

/* Set relay state */
hal_error_t hal_relay_set_state(uint32_t module_id, uint8_t channel, relay_state_t state);

/* Get relay state */
hal_error_t hal_relay_get_state(uint32_t module_id, uint8_t channel, relay_channel_state_t* state);

/* Get all relay states */
hal_error_t hal_relay_get_all_states(uint32_t module_id, relay_channel_state_t* states);

/* Get module measurements */
hal_error_t hal_relay_get_measurements(uint32_t module_id, relay_module_measurement_t* measurements);

/* Get module statistics */
hal_error_t hal_relay_get_statistics(uint32_t module_id, relay_module_stats_t* stats);

/* Pulse relay */
hal_error_t hal_relay_pulse(uint32_t module_id, uint8_t channel, uint32_t duration_ms);

/* Set multiple relays */
hal_error_t hal_relay_set_multiple(uint32_t module_id, uint8_t start_channel, uint8_t count, const relay_state_t* states);

/* Get module status */
hal_error_t hal_relay_get_status(uint32_t module_id, device_info_t* info);

/* Clear module faults */
hal_error_t hal_relay_clear_faults(uint32_t module_id);

/* Reset module statistics */
hal_error_t hal_relay_reset_statistics(uint32_t module_id);

/* Specific relay module implementations */

/* WAGO 750-652 implementation (8-channel DO) */
hal_error_t hal_relay_wago_set_state(uint32_t device_id, uint8_t channel, relay_state_t state);
hal_error_t hal_relay_wago_get_state(uint32_t device_id, uint8_t channel, relay_channel_state_t* state);

/* Phoenix Contact implementation */
hal_error_t hal_relay_phoenix_set_state(uint32_t device_id, uint8_t channel, relay_state_t state);
hal_error_t hal_relay_phoenix_get_state(uint32_t device_id, uint8_t channel, relay_channel_state_t* state);

#endif /* HAL_RELAY_H */
