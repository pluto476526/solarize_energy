/* Example hardware setup file: hardware_setup.c */

#include "hal.h"

/* Example: SMA Sunny Boy 5.0 inverter via Modbus TCP */
static const modbus_tcp_config_t sma_inverter_tcp = {
    .ip_address = "192.168.1.100",
    .port = 502,
    .timeout = 1000,
    .unit_id = 3
};

static const pv_inverter_config_t sma_inverter_config = {
    .type = PV_INVERTER_SMA,
    .interface = HAL_IFACE_MODBUS_TCP,
    .max_power = 5000.0,
    .max_voltage = 600.0,
    .max_current = 10.0,
    .mppt_count = 1,
    .string_count = 2
};

/* Example: Victron MultiPlus-II via VE.Can */
static const can_config_t victron_can_config = {
    .interface = "can0",
    .speed = CAN_SPEED_250K,
    .mode = 0,
    .tx_timeout = 100,
    .rx_timeout = 100
};

static const can_device_config_t victron_can_device = {
    .base_id = 0x600,
    .rx_id = 0x600,
    .tx_id = 0x580,
    .node_id = 1,
    .cob_id = 0x600
};

static const battery_config_t victron_battery_config = {
    .bms_type = BMS_VICTRON,
    .chemistry = BATTERY_LFP,
    .interface = HAL_IFACE_CAN_BUS,
    .nominal_voltage = 48.0,
    .capacity_ah = 200.0,
    .capacity_wh = 9600.0,
    .series_cells = 15,
    .parallel_cells = 1,
    .max_charge_current = 70.0,
    .max_discharge_current = 100.0
};

/* Example: WAGO 750-652 relay module via Modbus RTU */
static const modbus_rtu_config_t wago_relay_rtu = {
    .port = "/dev/ttyUSB0",
    .baud_rate = 38400,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = 0,
    .response_timeout = 500
};

static const relay_config_t wago_relay_config = {
    .module_type = RELAY_WAGO_750,
    .interface = HAL_IFACE_MODBUS_RTU,
    .channel_count = 8,
    .relay_type = RELAY_TYPE_SPST,
    .rated_current = 10.0,
    .rated_voltage = 230.0
};

/* Example: Janitza UMG 604 meter via Modbus RTU */
static const modbus_rtu_config_t janitza_meter_rtu = {
    .port = "/dev/ttyUSB1",
    .baud_rate = 19200,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = 0,
    .response_timeout = 500
};

static const meter_config_t janitza_meter_config = {
    .meter_type = METER_JANITZA,
    .interface = HAL_IFACE_MODBUS_RTU,
    .measurement_type = METER_MEASUREMENT_GRID,
    .ct_ratio = 100.0,
    .pt_ratio = 1.0,
    .phase_count = 3,
    .rated_voltage = 230.0,
    .rated_current = 5.0
};

/* Initialize all hardware */
hal_error_t initialize_hardware(void) {
    hal_error_t ret;
    uint32_t device_id;
    
    /* Initialize HAL */
    hal_config_t hal_config = {
        .config_file = "/etc/energy_manager/hal_config.json",
        .enable_logging = true,
        .log_level = 2,
        .scan_interval = 5.0,
        .response_timeout = 1000,
        .retry_count = 3
    };
    
    ret = hal_initialize(&hal_config);
    if (ret != HAL_SUCCESS) {
        return ret;
    }
    
    /* Add SMA inverter */
    ret = hal_modbus_add_tcp_device(&sma_inverter_tcp, 3, &device_id);
    if (ret == HAL_SUCCESS) {
        sma_inverter_config.device_id = device_id;
        uint32_t inverter_id;
        ret = hal_pv_init_inverter(&sma_inverter_config, &inverter_id);
        if (ret != HAL_SUCCESS) {
            fprintf(stderr, "Failed to initialize SMA inverter\n");
        }
    }
    
    /* Add Victron battery system */
    ret = hal_can_add_device(&victron_can_device, &device_id);
    if (ret == HAL_SUCCESS) {
        victron_battery_config.device_id = device_id;
        uint32_t battery_id;
        ret = hal_battery_init(&victron_battery_config, &battery_id);
        if (ret != HAL_SUCCESS) {
            fprintf(stderr, "Failed to initialize Victron battery\n");
        }
    }
    
    /* Add WAGO relay module */
    ret = hal_modbus_add_rtu_device(&wago_relay_rtu, 1, &device_id);
    if (ret == HAL_SUCCESS) {
        wago_relay_config.device_id = device_id;
        uint32_t relay_id;
        ret = hal_relay_init_module(&wago_relay_config, &relay_id);
        if (ret != HAL_SUCCESS) {
            fprintf(stderr, "Failed to initialize WAGO relay module\n");
        }
    }
    
    /* Add Janitza meter */
    ret = hal_modbus_add_rtu_device(&janitza_meter_rtu, 2, &device_id);
    if (ret == HAL_SUCCESS) {
        janitza_meter_config.device_id = device_id;
        uint32_t meter_id;
        ret = hal_meter_init(&janitza_meter_config, &meter_id);
        if (ret != HAL_SUCCESS) {
            fprintf(stderr, "Failed to initialize Janitza meter\n");
        }
    }
    
    return HAL_SUCCESS;
}

/* Example: Control loads based on power availability */
void control_loads_based_on_power(float available_power) {
    static const float load_powers[] = {150.0, 100.0, 500.0, 1000.0, 2000.0, 4500.0};
    static const uint8_t load_channels[] = {0, 1, 2, 3, 4, 5};
    static const uint8_t load_count = 6;
    
    float remaining_power = available_power;
    
    /* Turn on loads that can be powered */
    for (uint8_t i = 0; i < load_count; i++) {
        if (load_powers[i] <= remaining_power) {
            hal_relay_set_state(0, load_channels[i], RELAY_STATE_ON);
            remaining_power -= load_powers[i];
        } else {
            hal_relay_set_state(0, load_channels[i], RELAY_STATE_OFF);
        }
    }
}

/* Example: Monitor and log system measurements */
void monitor_system(void) {
    pv_inverter_measurement_t pv_meas;
    battery_measurement_t battery_meas;
    meter_measurement_t grid_meas;
    
    /* Read PV measurements */
    if (hal_pv_get_measurements(0, &pv_meas) == HAL_SUCCESS) {
        printf("PV Power: %.0f W, DC Voltage: %.1f V\n", 
               pv_meas.dc_power, pv_meas.dc_voltage);
    }
    
    /* Read battery measurements */
    if (hal_battery_get_measurements(0, &battery_meas) == HAL_SUCCESS) {
        printf("Battery SOC: %.1f%%, Power: %.0f W\n",
               battery_meas.soc, battery_meas.power);
    }
    
    /* Read grid measurements */
    if (hal_meter_get_measurements(0, &grid_meas) == HAL_SUCCESS) {
        printf("Grid Power: %.0f W, Import: %.2f kWh\n",
               grid_meas.power_total, grid_meas.energy_import_total);
    }
}
