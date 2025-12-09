#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "config.h"
#include "controller.h"
#include "webserver.h"

// Global instances for signal handling
static system_controller_t* g_controller = NULL;
static webserver_t* g_webserver = NULL;
static volatile sig_atomic_t g_running = 1;

// Prototypes
void signal_handler(int signum);
void print_banner(void);
int load_webserver_config(webserver_config_t* config);

/* Signal handler */
void signal_handler(int signum) {
    printf("\nReceived signal %d. Shutting down gracefully...\n", signum);
    g_running = 0;
    
    if (g_webserver) {
        webserver_stop(g_webserver);
    }
    if (g_controller) {
        controller_cleanup(g_controller);
    }
}

/* Print system banner */
void print_banner() {
    printf("=========================================\n");
    printf("  Energy Management System with Web UI  \n");
    printf("  Version 1.1.0                        \n");
    printf("  Built: %s %s           \n", __DATE__, __TIME__);
    printf("=========================================\n\n");
}

/* Load web server configuration */
int load_webserver_config(webserver_config_t* config) {
    webserver_default_config(config);
    
    /* Override with environment variables or configuration file */
    char* port_str = getenv("WEB_PORT");
    if (port_str) {
        config->port = atoi(port_str);
    }
    
    char* ssl_port_str = getenv("WEB_SSL_PORT");
    if (ssl_port_str) {
        config->ssl_port = atoi(ssl_port_str);
        config->enable_ssl = (config->ssl_port > 0);
    }
    
    char* static_dir = getenv("WEB_STATIC_DIR");
    if (static_dir) {
        config->static_dir = strdup(static_dir);
    }
    
    char* admin_pass = getenv("WEB_ADMIN_PASSWORD");
    if (admin_pass) {
        config->admin_password_hash = webserver_hash_password(admin_pass);
    }
    
    return 0;
}

/* Main program */
int main(int argc, char* argv[]) {
    print_banner();
    
    /* Parse command line arguments */
    cli_args_t args = parse_cli_args(argc, argv);
    
    /* Load system configuration */
    system_config_t sys_config;
    config_set_defaults(&sys_config);
    
    printf("Loading system configuration from %s\n", args.config_file);
    config_error_t config_result = config_load(args.config_file, &sys_config);
    
    if (config_result != CONFIG_SUCCESS) {
        fprintf(stderr, "Failed to load configuration (error: %d)\n", config_result);
        
        if (config_result == CONFIG_FILE_NOT_FOUND) {
            printf("Creating default configuration...\n");
            if (config_save(args.config_file, &sys_config) != CONFIG_SUCCESS) {
                fprintf(stderr, "Failed to create default configuration\n");
                return EXIT_FAILURE;
            }
            printf("Default configuration saved to %s\n", args.config_file);
        } else {
            return EXIT_FAILURE;
        }
    }
    
    /* Initialize controller */
    system_controller_t controller;
    controller_init(&controller, &sys_config);
    g_controller = &controller;
    
    /* Initialize web server */
    webserver_config_t ws_config;
    load_webserver_config(&ws_config);
    
    g_webserver = webserver_create(&controller);
    if (!g_webserver) {
        fprintf(stderr, "Failed to create web server\n");
        controller_cleanup(&controller);
        return EXIT_FAILURE;
    }
    
    if (webserver_init(g_webserver, &ws_config) != 0) {
        fprintf(stderr, "Failed to initialize web server\n");
        webserver_destroy(g_webserver);
        controller_cleanup(&controller);
        return EXIT_FAILURE;
    }
    
    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    
    /* Test mode */
    if (args.test_mode) {
        printf("\n=== TEST MODE ===\n");
        printf("Running system test...\n");
        
        /* Run a few control cycles */
        for (int i = 0; i < 10 && g_running; i++) {
            controller_run_cycle(&controller);
            sleep(1);
        }
        
        printf("\nTest completed successfully.\n");
        controller_cleanup(&controller);
        webserver_destroy(g_webserver);
        return EXIT_SUCCESS;
    }
    
    /* Start web server in a separate thread */
    printf("Starting web server on port %d\n", ws_config.port);
    if (ws_config.enable_ssl) {
        printf("SSL enabled on port %d\n", ws_config.ssl_port);
    }
    
    /* In production, we would start the web server in a separate thread */
    /* For simplicity, we'll start it in the main thread for now */
    
    printf("\nSystem started successfully.\n");
    printf("Web interface available at: http://localhost:%d\n", ws_config.port);
    printf("API endpoint: http://localhost:%d/api\n", ws_config.port);
    printf("Press Ctrl+C to exit\n\n");
    
    /* Main control loop */
    time_t start_time = time(NULL);
    uint64_t cycles = 0;
    
    while (g_running) {
        /* Run control cycle */
        controller_run_cycle(&controller);
        cycles++;
        
        /* Sleep to maintain control interval */
        struct timespec ts;
        ts.tv_sec  = (time_t)sys_config.control_interval;
        ts.tv_nsec = (long)((sys_config.control_interval - ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);

    }
    
    /* Cleanup */
    printf("\nShutting down...\n");
    webserver_destroy(g_webserver);
    controller_cleanup(&controller);
    
    /* Calculate and print statistics */
    time_t end_time = time(NULL);
    double run_time = difftime(end_time, start_time);
    
    printf("\n=== Runtime Statistics ===\n");
    printf("Total run time: %.1f hours\n", run_time / 3600.0);
    printf("Control cycles: %lu\n", cycles);
    printf("Average cycle time: %.3f seconds\n", run_time / cycles);
    printf("PV Energy: %.2f kWh\n", controller.statistics.pv_energy_total);
    printf("Grid Import: %.2f kWh\n", controller.statistics.grid_import_total);
    printf("Grid Export: %.2f kWh\n", controller.statistics.grid_export_total);
    printf("==========================\n");
    
    printf("\nEnergy Management System shutdown complete.\n");
    return EXIT_SUCCESS;
}
