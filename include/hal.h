#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Maximum number of devices per type */
#define MAX_PV_INVERTERS     4
#define MAX_BATTERY_BANKS    2
#define MAX_RELAYS           16
#define MAX_METERS           8
#define MAX_SENSORS          32

/* Communication interface types */
typedef enum {
    HAL_IFACE_NONE = 0,
    HAL_IFACE_MODBUS_RTU,
    HAL_IFACE_MODBUS_TCP,
    HAL_IFACE_CAN_BUS,
    HAL_IFACE_RS485,
    HAL_IFACE_I2C,
    HAL_IFACE_SPI,
    HAL_IFACE_ETHERNET,
    HAL_IFACE_SERIAL
} hal_interface_t;

/* Device states */
typedef enum {
    DEVICE_STATE_UNINITIALIZED = 0,
    DEVICE_STATE_INITIALIZING,
    DEVICE_STATE_READY,
    DEVICE_STATE_ACTIVE,
    DEVICE_STATE_FAULT,
    DEVICE_STATE_DISCONNECTED,
    DEVICE_STATE_STANDBY
} device_state_t;

/* Error codes */
typedef enum {
    HAL_SUCCESS = 0,
    HAL_ERROR_INIT_FAILED,
    HAL_ERROR_COMMUNICATION,
    HAL_ERROR_TIMEOUT,
    HAL_ERROR_INVALID_PARAM,
    HAL_ERROR_NOT_SUPPORTED,
    HAL_ERROR_DEVICE_BUSY,
    HAL_ERROR_CRC_FAILED,
    HAL_ERROR_PROTOCOL,
    HAL_ERROR_HARDWARE
} hal_error_t;

/* Common device information */
typedef struct {
    char manufacturer[32];
    char model[32];
    char serial_number[32];
    char firmware_version[16];
    uint32_t device_id;
    time_t last_communication;
    uint32_t error_count;
    device_state_t state;
} device_info_t;

/* Common measurement structure */
typedef struct {
    float voltage;          /* Volts */
    float current;          /* Amps */
    float power;            /* Watts */
    float frequency;        /* Hz */
    float temperature;      /* Celsius */
    uint32_t status;        /* Status bits */
    time_t timestamp;       /* Measurement time */
} measurement_t;

/* Common command structure */
typedef struct {
    float setpoint;         /* Power setpoint */
    uint16_t command_code;  /* Device-specific command */
    uint32_t parameters[4]; /* Command parameters */
    time_t timestamp;       /* Command time */
    uint8_t priority;       /* Command priority 0-255 */
} command_t;

/* Callback function types */
typedef void (*measurement_callback_t)(uint32_t device_id, measurement_t* measurement);
typedef void (*error_callback_t)(uint32_t device_id, hal_error_t error, const char* message);
typedef void (*state_change_callback_t)(uint32_t device_id, device_state_t old_state, device_state_t new_state);

/* Communication statistics */
typedef struct {
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t crc_errors;
    uint32_t timeout_errors;
    uint32_t protocol_errors;
    time_t start_time;
} comm_stats_t;

/* HAL initialization parameters */
typedef struct {
    char config_file[256];      /* Configuration file path */
    bool enable_logging;        /* Enable debug logging */
    uint32_t log_level;         /* Log verbosity level */
    float scan_interval;        /* Device scan interval (seconds) */
    uint32_t response_timeout;  /* Communication timeout (ms) */
    uint32_t retry_count;       /* Max retry attempts */
} hal_config_t;

/* Initialize HAL */
hal_error_t hal_initialize(hal_config_t* config);

/* Shutdown HAL */
hal_error_t hal_shutdown(void);

/* Register callbacks */
hal_error_t hal_register_measurement_callback(measurement_callback_t callback);
hal_error_t hal_register_error_callback(error_callback_t callback);
hal_error_t hal_register_state_change_callback(state_change_callback_t callback);

/* Get communication statistics */
hal_error_t hal_get_comm_stats(comm_stats_t* stats);

/* Reset communication statistics */
hal_error_t hal_reset_comm_stats(void);

#endif /* HAL_COMMON_H */
