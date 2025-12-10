#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "config.h"
#include "controller.h"
#include "logging.h"

// Application Config
typedef struct {
    char *config_file;
    char *log_file;
    int debug_level;
} app_config_t;

// Global instances
static volatile sig_atomic_t running = 1;
static system_controller_t *system_ctrl = NULL;
static system_config_t sys_config;

static app_config_t app_config = {
    .config_file = "config/default_config.json",
    .log_file = "log/solarize.log",
    .debug_level = 1,
};

// Signal Handler
static void signal_handler(int sig) {
    LOG_INFO("Received signal %d, shutting down...", sig);
    running = 0;
}

// Argument Parsing
static void parse_arguments(int argc, char *argv[]) {
    int opt;

    while ((opt = getopt(argc, argv, "c:l:d:h")) != -1) {
        switch (opt) {
            case 'c':
                app_config.config_file = optarg;
                break;
            case 'l':
                app_config.log_file = optarg;
                break;
            case 'd':
                app_config.debug_level = 1;
                break;
            case 'h':
                printf("Usage: %s [options]\n", argv[0]);
                printf("Options:\n");
                printf("  -c <file>    Configuration file\n");
                printf("  -l <file>    Log file\n");
                printf("  -d           Enable debug logging\n");
                printf("  -h           Show this help\n");
                exit(EXIT_SUCCESS);
        }
    }
}

// Application Initialization
static int app_init(void) {
    // Initialize logging
    log_level_t log_level = app_config.debug_level ? LOG_DEBUG : LOG_INFO;

    if (log_init(app_config.log_file, log_level, log_level, "solarize") != 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return -1;
    }

    // Setup signals
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    LOG_DEBUG("Configuration: file=%s, log=%s, debug=%d",
        app_config.config_file, app_config.log_file, app_config.debug_level);

    // Allocate controller
    system_ctrl = malloc(sizeof(system_controller_t));
    if (!system_ctrl) {
        LOG_ERROR_ERRNO("Failed to allocate system controller");
        return -1;
    }

    // Load configuration
    config_set_defaults(&sys_config);
    config_error_t config_result = config_load(app_config.config_file, &sys_config);

    if (config_result != CONFIG_SUCCESS) {
        LOG_ERROR("Failed to load configuration: %d", config_result);

        if (config_result == CONFIG_FILE_NOT_FOUND) {
            LOG_WARNING("Configuration file not found, creating default...");

            if (config_save(app_config.config_file, &sys_config) == CONFIG_SUCCESS) {
                LOG_INFO("Created default configuration at %s", app_config.config_file);
            } else {
                LOG_ERROR("Failed to create default configuration");
                return -1;
            }
        } else {
            return -1;
        }
    }

    // Initialize controller
    if (controller_init(system_ctrl, &sys_config) != 0) {
        LOG_ERROR("Failed to initialize system controller");
        return -1;
    }

    LOG_INFO("System init complete. Solarize now online.");
    LOG_DEBUG("Control interval: %d seconds", sys_config.control_interval);

    return 0;
}

// Application Main Loop
static void app_run(void) {
    uint64_t cycle_count = 0;

    while (running) {
        if (controller_run_cycle(system_ctrl) != 0)
            LOG_WARNING("Controller cycle %lu encountered an issue", cycle_count);

        cycle_count++;

        struct timespec ts = {
            .tv_sec = sys_config.control_interval,
            .tv_nsec = 0
        };

        nanosleep(&ts, NULL);
    }

    LOG_DEBUG("Total cycles completed: %lu", cycle_count);
}

// Application Cleanup
static void app_cleanup(void) {
    if (system_ctrl) {
        controller_cleanup(system_ctrl);
        free(system_ctrl);
        LOG_DEBUG("Controller cleaned up");
    }

    LOG_INFO("Shutdown complete");
    log_close();
}

// Main Entry
int main(int argc, char *argv[]) {
    printf("Solarize Energy Solutions\n");

    parse_arguments(argc, argv);

    if (app_init() != 0)
        return EXIT_FAILURE;

    app_run();
    app_cleanup();

    return EXIT_SUCCESS;
}
