#ifndef HAL_CAN_H
#define HAL_CAN_H

#include "hal.h"

/* CAN bus speeds */
typedef enum {
    CAN_SPEED_125K = 0,
    CAN_SPEED_250K,
    CAN_SPEED_500K,
    CAN_SPEED_1M
} can_speed_t;

/* CAN frame structure */
typedef struct {
    uint32_t id;            /* CAN message ID */
    uint8_t data[8];        /* Data payload */
    uint8_t dlc;            /* Data length code (0-8) */
    uint8_t ext;            /* Extended frame flag */
    uint8_t rtr;            /* Remote transmission request */
    uint32_t timestamp;     /* Timestamp in microseconds */
} can_frame_t;

/* CAN bus configuration */
typedef struct {
    char interface[32];     /* CAN interface (e.g., "can0") */
    can_speed_t speed;      /* Bus speed */
    uint8_t mode;           /* 0=normal, 1=listen-only, 2=loopback */
    uint32_t tx_timeout;    /* Transmit timeout in ms */
    uint32_t rx_timeout;    /* Receive timeout in ms */
} can_config_t;

/* CAN device configuration */
typedef struct {
    uint32_t base_id;       /* Base CAN ID for device */
    uint32_t rx_id;         /* Receive CAN ID */
    uint32_t tx_id;         /* Transmit CAN ID */
    uint8_t node_id;        /* CANopen node ID */
    uint16_t cob_id;        /* CANopen COB-ID */
} can_device_config_t;

/* CAN message filters */
typedef struct {
    uint32_t id;            /* Filter ID */
    uint32_t mask;          /* Mask for filtering */
    uint8_t extended;       /* Extended frame filter */
} can_filter_t;

/* Initialize CAN interface */
hal_error_t hal_can_init(const can_config_t* config);

/* Add CAN device */
hal_error_t hal_can_add_device(const can_device_config_t* config, uint32_t* device_id);

/* Send CAN frame */
hal_error_t hal_can_send_frame(uint32_t device_id, const can_frame_t* frame);

/* Receive CAN frame */
hal_error_t hal_can_receive_frame(uint32_t device_id, can_frame_t* frame, uint32_t timeout_ms);

/* Send raw data */
hal_error_t hal_can_send_data(uint32_t device_id, uint32_t can_id, const uint8_t* data, uint8_t length);

/* Add message filter */
hal_error_t hal_can_add_filter(uint32_t device_id, const can_filter_t* filter);

/* Get bus statistics */
hal_error_t hal_can_get_bus_stats(uint32_t device_id, comm_stats_t* stats);

/* CANopen specific functions */
hal_error_t hal_canopen_sdo_read(uint32_t device_id, uint16_t index, uint8_t subindex, uint8_t* data, uint8_t* length);
hal_error_t hal_canopen_sdo_write(uint32_t device_id, uint16_t index, uint8_t subindex, const uint8_t* data, uint8_t length);
hal_error_t hal_canopen_pdo_send(uint32_t device_id, uint8_t pdo_number, const uint8_t* data, uint8_t length);
hal_error_t hal_canopen_nmt_command(uint32_t device_id, uint8_t command);

/* Parse float from CAN data */
float hal_can_parse_float(const uint8_t* data);

/* Parse 32-bit integer from CAN data */
int32_t hal_can_parse_int32(const uint8_t* data);

/* Parse 16-bit integer from CAN data */
int16_t hal_can_parse_int16(const uint8_t* data);

#endif /* HAL_CAN_H */
