#ifndef HAL_MODBUS_H
#define HAL_MODBUS_H

#include "hal.h"

/* Modbus function codes */
typedef enum {
    MODBUS_READ_COILS = 0x01,
    MODBUS_READ_DISCRETE_INPUTS = 0x02,
    MODBUS_READ_HOLDING_REGISTERS = 0x03,
    MODBUS_READ_INPUT_REGISTERS = 0x04,
    MODBUS_WRITE_SINGLE_COIL = 0x05,
    MODBUS_WRITE_SINGLE_REGISTER = 0x06,
    MODBUS_WRITE_MULTIPLE_COILS = 0x0F,
    MODBUS_WRITE_MULTIPLE_REGISTERS = 0x10
} modbus_function_t;

/* Modbus exception codes */
typedef enum {
    MODBUS_EXCEPTION_NONE = 0,
    MODBUS_EXCEPTION_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EXCEPTION_ILLEGAL_ADDRESS = 0x02,
    MODBUS_EXCEPTION_ILLEGAL_VALUE = 0x03,
    MODBUS_EXCEPTION_SERVER_FAILURE = 0x04,
    MODBUS_EXCEPTION_ACKNOWLEDGE = 0x05,
    MODBUS_EXCEPTION_SERVER_BUSY = 0x06
} modbus_exception_t;

/* Modbus RTU configuration */
typedef struct {
    char port[64];          /* Serial port (e.g., /dev/ttyUSB0) */
    uint32_t baud_rate;     /* Baud rate (9600, 19200, 38400, 57600, 115200) */
    uint8_t data_bits;      /* Data bits (5, 6, 7, 8) */
    uint8_t stop_bits;      /* Stop bits (1, 2) */
    uint8_t parity;         /* Parity (0=none, 1=odd, 2=even) */
    uint32_t response_timeout; /* Response timeout in ms */
} modbus_rtu_config_t;

/* Modbus TCP configuration */
typedef struct {
    char ip_address[16];    /* IP address (e.g., 192.168.1.100) */
    uint16_t port;          /* TCP port (default: 502) */
    uint32_t timeout;       /* Connection timeout in ms */
    uint8_t unit_id;        /* Modbus unit ID */
} modbus_tcp_config_t;

/* Modbus device context */
typedef struct {
    uint32_t device_id;
    hal_interface_t interface_type;
    union {
        modbus_rtu_config_t rtu_config;
        modbus_tcp_config_t tcp_config;
    } config;
    uint8_t unit_id;        /* Modbus slave address */
    bool connected;
    uint32_t error_count;
    time_t last_comm_time;
} modbus_device_t;

/* Modbus register mapping */
typedef struct {
    uint16_t address;       /* Register address */
    uint16_t count;         /* Number of registers */
    char name[32];          /* Register name/description */
    float scale_factor;     /* Scaling factor for conversion */
    float offset;           /* Offset for conversion */
    uint8_t data_type;      /* Data type (0=uint16, 1=int16, 2=uint32, 3=int32, 4=float) */
} modbus_register_t;

/* Initialize Modbus interface */
hal_error_t hal_modbus_init(void);

/* Add Modbus RTU device */
hal_error_t hal_modbus_add_rtu_device(const modbus_rtu_config_t* config, uint8_t unit_id, uint32_t* device_id);

/* Add Modbus TCP device */
hal_error_t hal_modbus_add_tcp_device(const modbus_tcp_config_t* config, uint8_t unit_id, uint32_t* device_id);

/* Read holding registers */
hal_error_t hal_modbus_read_registers(uint32_t device_id, uint16_t start_addr, uint16_t count, uint16_t* values);

/* Write holding registers */
hal_error_t hal_modbus_write_registers(uint32_t device_id, uint16_t start_addr, uint16_t count, const uint16_t* values);

/* Read input registers */
hal_error_t hal_modbus_read_input_registers(uint32_t device_id, uint16_t start_addr, uint16_t count, uint16_t* values);

/* Read coils */
hal_error_t hal_modbus_read_coils(uint32_t device_id, uint16_t start_addr, uint16_t count, uint8_t* values);

/* Write single coil */
hal_error_t hal_modbus_write_coil(uint32_t device_id, uint16_t address, uint8_t value);

/* Read discrete inputs */
hal_error_t hal_modbus_read_discrete_inputs(uint32_t device_id, uint16_t start_addr, uint16_t count, uint8_t* values);

/* Get device status */
hal_error_t hal_modbus_get_device_status(uint32_t device_id, device_info_t* info);

/* Scan all Modbus devices */
hal_error_t hal_modbus_scan_devices(void);

/* Parse float from two registers */
float hal_modbus_parse_float(uint16_t reg_high, uint16_t reg_low);

/* Parse 32-bit integer from two registers */
int32_t hal_modbus_parse_int32(uint16_t reg_high, uint16_t reg_low);

#endif /* HAL_MODBUS_H */
