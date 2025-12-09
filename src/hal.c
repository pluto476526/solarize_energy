#include "hal.h"
#include "hal_modbus.h"
#include "hal_can.h"
#include "hal_pv.h"
#include "hal_battery.h"
#include "hal_relay.h"
#include "hal_meter.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/* HAL context structure */
typedef struct {
    hal_config_t config;
    bool initialized;
    
    /* Device registries */
    struct {
        pv_inverter_config_t inverters[MAX_PV_INVERTERS];
        uint32_t inverter_count;
        
        battery_config_t batteries[MAX_BATTERY_BANKS];
        uint32_t battery_count;
        
        relay_config_t relays[MAX_RELAYS];
        uint32_t relay_count;
        
        meter_config_t meters[MAX_METERS];
        uint32_t meter_count;
    } devices;
    
    /* Callbacks */
    measurement_callback_t measurement_cb;
    error_callback_t error_cb;
    state_change_callback_t state_change_cb;
    
    /* Statistics */
    comm_stats_t stats;
    
    /* Thread management */
    pthread_t scan_thread;
    bool scan_thread_running;
    pthread_mutex_t lock;
} hal_context_t;

static hal_context_t g_hal_context = {0};

/* Initialize HAL */
hal_error_t hal_initialize(hal_config_t* config) {
    if (g_hal_context.initialized) {
        return HAL_SUCCESS;
    }
    
    if (!config) {
        return HAL_ERROR_INVALID_PARAM;
    }
    
    /* Initialize context */
    memcpy(&g_hal_context.config, config, sizeof(hal_config_t));
    memset(&g_hal_context.devices, 0, sizeof(g_hal_context.devices));
    
    /* Initialize mutex */
    if (pthread_mutex_init(&g_hal_context.lock, NULL) != 0) {
        return HAL_ERROR_INIT_FAILED;
    }
    
    /* Initialize communication statistics */
    g_hal_context.stats.start_time = time(NULL);
    
    /* Initialize Modbus interface */
    if (hal_modbus_init() != HAL_SUCCESS) {
        fprintf(stderr, "Failed to initialize Modbus interface\n");
        pthread_mutex_destroy(&g_hal_context.lock);
        return HAL_ERROR_INIT_FAILED;
    }
    
    /* Initialize CAN interface */
    can_config_t can_config = {
        .interface = "can0",
        .speed = CAN_SPEED_500K,
        .mode = 0,
        .tx_timeout = 100,
        .rx_timeout = 100
    };
    
    if (hal_can_init(&can_config) != HAL_SUCCESS) {
        fprintf(stderr, "Failed to initialize CAN interface\n");
        /* Continue without CAN */
    }
    
    g_hal_context.initialized = true;
    
    /* Start device scanning thread */
    g_hal_context.scan_thread_running = true;
    if (pthread_create(&g_hal_context.scan_thread, NULL, hal_scan_thread, NULL) != 0) {
        fprintf(stderr, "Failed to start scan thread\n");
        g_hal_context.scan_thread_running = false;
    }
    
    return HAL_SUCCESS;
}

/* Scan thread function */
static void* hal_scan_thread(void* arg) {
    (void)arg;
    
    while (g_hal_context.scan_thread_running) {
        /* Scan for Modbus devices */
        hal_modbus_scan_devices();
        
        /* Scan for PV inverters */
        uint32_t inverter_count = 0;
        uint32_t inverter_ids[MAX_PV_INVERTERS];
        hal_pv_scan_inverters(&inverter_count, inverter_ids);
        
        /* Update device states */
        hal_update_device_states();
        
        /* Sleep between scans */
        sleep(g_hal_context.config.scan_interval);
    }
    
    return NULL;
}

/* Update device states */
static void hal_update_device_states(void) {
    pthread_mutex_lock(&g_hal_context.lock);
    
    /* Update PV inverter states */
    for (uint32_t i = 0; i < g_hal_context.devices.inverter_count; i++) {
        device_info_t info;
        if (hal_pv_get_status(i, &info) != HAL_SUCCESS) {
            info.state = DEVICE_STATE_DISCONNECTED;
        }
        
        /* Call state change callback if needed */
        if (g_hal_context.state_change_cb) {
            /* Compare with previous state */
            static device_state_t prev_states[MAX_PV_INVERTERS] = {0};
            if (prev_states[i] != info.state) {
                g_hal_context.state_change_cb(i, prev_states[i], info.state);
                prev_states[i] = info.state;
            }
        }
    }
    
    /* Update battery states */
    for (uint32_t i = 0; i < g_hal_context.devices.battery_count; i++) {
        device_info_t info;
        if (hal_battery_get_status(i, &info) != HAL_SUCCESS) {
            info.state = DEVICE_STATE_DISCONNECTED;
        }
        
        /* Call state change callback if needed */
        if (g_hal_context.state_change_cb) {
            static device_state_t prev_states[MAX_BATTERY_BANKS] = {0};
            if (prev_states[i] != info.state) {
                g_hal_context.state_change_cb(i, prev_states[i], info.state);
                prev_states[i] = info.state;
            }
        }
    }
    
    pthread_mutex_unlock(&g_hal_context.lock);
}

/* Shutdown HAL */
hal_error_t hal_shutdown(void) {
    if (!g_hal_context.initialized) {
        return HAL_SUCCESS;
    }
    
    /* Stop scan thread */
    g_hal_context.scan_thread_running = false;
    if (g_hal_context.scan_thread) {
        pthread_join(g_hal_context.scan_thread, NULL);
    }
    
    /* Clean up mutex */
    pthread_mutex_destroy(&g_hal_context.lock);
    
    g_hal_context.initialized = false;
    return HAL_SUCCESS;
}

/* Register callbacks */
hal_error_t hal_register_measurement_callback(measurement_callback_t callback) {
    if (!g_hal_context.initialized) {
        return HAL_ERROR_INIT_FAILED;
    }
    
    g_hal_context.measurement_cb = callback;
    return HAL_SUCCESS;
}

hal_error_t hal_register_error_callback(error_callback_t callback) {
    if (!g_hal_context.initialized) {
        return HAL_ERROR_INIT_FAILED;
    }
    
    g_hal_context.error_cb = callback;
    return HAL_SUCCESS;
}

hal_error_t hal_register_state_change_callback(state_change_callback_t callback) {
    if (!g_hal_context.initialized) {
        return HAL_ERROR_INIT_FAILED;
    }
    
    g_hal_context.state_change_cb = callback;
    return HAL_SUCCESS;
}

/* Get communication statistics */
hal_error_t hal_get_comm_stats(comm_stats_t* stats) {
    if (!g_hal_context.initialized) {
        return HAL_ERROR_INIT_FAILED;
    }
    
    if (!stats) {
        return HAL_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&g_hal_context.lock);
    memcpy(stats, &g_hal_context.stats, sizeof(comm_stats_t));
    pthread_mutex_unlock(&g_hal_context.lock);
    
    return HAL_SUCCESS;
}

/* Reset communication statistics */
hal_error_t hal_reset_comm_stats(void) {
    if (!g_hal_context.initialized) {
        return HAL_ERROR_INIT_FAILED;
    }
    
    pthread_mutex_lock(&g_hal_context.lock);
    memset(&g_hal_context.stats, 0, sizeof(g_hal_context.stats));
    g_hal_context.stats.start_time = time(NULL);
    pthread_mutex_unlock(&g_hal_context.lock);
    
    return HAL_SUCCESS;
}
