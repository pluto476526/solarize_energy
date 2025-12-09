/* Integration layer: ems_hal_integration.c */

#include "controller.h"
#include "hal.h"

/* Convert HAL measurements to EMS measurements */
static void convert_pv_measurements(uint32_t inverter_id, 
                                   pv_inverter_measurement_t* hal_meas,
                                   system_measurements_t* ems_meas) {
    ems_meas->pv_power_total = hal_meas->ac_power;
    ems_meas->pv_voltage[inverter_id] = hal_meas->dc_voltage;
    ems_meas->pv_current[inverter_id] = hal_meas->dc_current;
    
    /* Update individual string measurements if available */
    for (uint8_t i = 0; i < hal_meas->string_count && i < MAX_PV_STRINGS; i++) {
        ems_meas->pv_strings_active++;
    }
}

static void convert_battery_measurements(uint32_t battery_id,
                                        battery_measurement_t* hal_meas,
                                        system_measurements_t* ems_meas) {
    ems_meas->battery_power = hal_meas->power;
    ems_meas->battery_voltage = hal_meas->voltage;
    ems_meas->battery_current = hal_meas->current;
    ems_meas->battery_soc = hal_meas->soc;
    ems_meas->battery_temp = hal_meas->temperature;
}

static void convert_meter_measurements(uint32_t meter_id,
                                      meter_measurement_t* hal_meas,
                                      system_measurements_t* ems_meas) {
    ems_meas->grid_power = hal_meas->power_total;
    ems_meas->grid_voltage = hal_meas->voltage_avg;
    ems_meas->grid_frequency = hal_meas->frequency;
    
    /* Determine import/export */
    if (hal_meas->power_total > 0) {
        ems_meas->grid_power = hal_meas->power_total; /* Importing */
    } else {
        ems_meas->grid_power = -hal_meas->power_total; /* Exporting */
    }
}

/* HAL measurement callback - called by HAL when new measurements arrive */
static void hal_measurement_callback(uint32_t device_id, measurement_t* measurement) {
    /* Determine device type and update EMS measurements */
    system_measurements_t ems_meas;
    memset(&ems_meas, 0, sizeof(ems_meas));
    ems_meas.timestamp = measurement->timestamp;
    
    /* Update EMS controller measurements */
    controller_update_measurements_from_hal(&ems_meas);
}

/* HAL error callback - called by HAL when errors occur */
static void hal_error_callback(uint32_t device_id, hal_error_t error, const char* message) {
    fprintf(stderr, "HAL Error [Device %u]: %s (Error %d)\n", device_id, message, error);
    
    /* Update EMS alarm status */
    if (error >= HAL_ERROR_COMMUNICATION) {
        controller_set_alarm(ALARM_COMM_FAILURE, true);
    }
}

/* HAL state change callback - called when device state changes */
static void hal_state_change_callback(uint32_t device_id, 
                                      device_state_t old_state,
                                      device_state_t new_state) {
    printf("Device %u state changed: %d -> %d\n", device_id, old_state, new_state);
    
    /* Update EMS system status */
    if (new_state == DEVICE_STATE_FAULT) {
        controller_set_warning(WARNING_COMM_FAILURE, true);
    } else if (new_state == DEVICE_STATE_READY) {
        controller_set_warning(WARNING_COMM_FAILURE, false);
    }
}

/* Initialize EMS-HAL integration */
int ems_hal_integration_init(void) {
    /* Initialize hardware */
    hal_error_t ret = initialize_hardware();
    if (ret != HAL_SUCCESS) {
        fprintf(stderr, "Failed to initialize hardware: %d\n", ret);
        return -1;
    }
    
    /* Register HAL callbacks */
    hal_register_measurement_callback(hal_measurement_callback);
    hal_register_error_callback(hal_error_callback);
    hal_register_state_change_callback(hal_state_change_callback);
    
    return 0;
}

/* Update EMS controller with hardware measurements */
void ems_hal_update_measurements(system_controller_t* controller) {
    if (!controller) return;
    
    /* Get PV measurements */
    for (uint32_t i = 0; i < g_hal_context.devices.inverter_count; i++) {
        pv_inverter_measurement_t pv_meas;
        if (hal_pv_get_measurements(i, &pv_meas) == HAL_SUCCESS) {
            convert_pv_measurements(i, &pv_meas, &controller->measurements);
        }
    }
    
    /* Get battery measurements */
    for (uint32_t i = 0; i < g_hal_context.devices.battery_count; i++) {
        battery_measurement_t battery_meas;
        if (hal_battery_get_measurements(i, &battery_meas) == HAL_SUCCESS) {
            convert_battery_measurements(i, &battery_meas, &controller->measurements);
        }
    }
    
    /* Get meter measurements */
    for (uint32_t i = 0; i < g_hal_context.devices.meter_count; i++) {
        meter_measurement_t meter_meas;
        if (hal_meter_get_measurements(i, &meter_meas) == HAL_SUCCESS) {
            convert_meter_measurements(i, &meter_meas, &controller->measurements);
        }
    }
    
    controller->measurements.timestamp = time(NULL);
}

/* Execute EMS commands on hardware */
void ems_hal_execute_commands(system_controller_t* controller) {
    if (!controller) return;
    
    /* Execute battery commands */
    if (fabs(controller->commands.battery_setpoint) > 0.1) {
        battery_command_t bat_cmd = {0};
        
        if (controller->commands.battery_setpoint > 0) {
            /* Discharge battery */
            bat_cmd.enable_discharge = true;
            bat_cmd.discharge_current = controller->commands.battery_setpoint / 
                                      controller->measurements.battery_voltage;
        } else {
            /* Charge battery */
            bat_cmd.enable_charge = true;
            bat_cmd.charge_current = -controller->commands.battery_setpoint / 
                                   controller->measurements.battery_voltage;
        }
        
        for (uint32_t i = 0; i < g_hal_context.devices.battery_count; i++) {
            hal_battery_send_command(i, &bat_cmd);
        }
    }
    
    /* Execute PV curtailment */
    if (controller->commands.pv_curtail) {
        pv_inverter_command_t pv_cmd = {0};
        pv_cmd.power_limit = 100.0 - controller->commands.pv_curtail_percent;
        
        for (uint32_t i = 0; i < g_hal_context.devices.inverter_count; i++) {
            hal_pv_send_command(i, &pv_cmd);
        }
    }
    
    /* Execute load shedding commands */
    for (uint8_t i = 0; i < MAX_CONTROLLABLE_LOADS; i++) {
        if (controller->commands.load_shed[i]) {
            /* Turn off load */
            hal_relay_set_state(0, i, RELAY_STATE_OFF);
        }
    }
}

/* Shutdown EMS-HAL integration */
void ems_hal_integration_shutdown(void) {
    hal_shutdown();
}
