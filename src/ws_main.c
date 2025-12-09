#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "config.h"
#include "controller.h"

/* Configuration */
typedef struct {
    char *config_file;
    char *log_file;
    int daemonize;
    int debug_level;
    char *pid_file;
    int web_port;
    char *web_root;
} app_config_t;

/* Global instances */
static volatile sig_atomic_t running = 1;
static system_controller_t *system_ctrl = NULL;
// static webserver_t *web_server = NULL;
static app_config_t app_config = {
    .config_file = "/etc/energy-mgmt/config.json",
    .log_file = "/var/log/energy-mgmt/daemon.log",
    .daemonize = 1,
    .debug_level = 0,
    .pid_file = "/var/run/energy-mgmt.pid",
    .web_port = 8080,
    .web_root = "./web"
};

void handle_shutdown_signal(int sig);
void handle_reload_signal(int sig);
int write_pid_file(const char *pid_file);
void parse_arguments(int argc, char *argv[]);
int daemonize(void);

/* Signal handlers */
void handle_shutdown_signal(int sig) {
    syslog(LOG_INFO, "Received signal %d, shutting down gracefully", sig);
    running = 0;
    
    // if (web_server) webserver_stop(web_server);
    if (system_ctrl) controller_cleanup(system_ctrl);
}

void handle_reload_signal(int sig) {
    syslog(LOG_INFO, "Received signal %d, reloading configuration", sig);
    // TODO: Implement configuration reload
}

/* Daemonize process */
int daemonize() {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    if (setsid() < 0) {
        perror("setsid");
        return -1;
    }
    
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    umask(0);
    chdir("/");
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirect to /dev/null
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    
    return 0;
}

/* Write PID file */
int write_pid_file(const char *pid_file) {
    FILE *fp = fopen(pid_file, "w");
    if (!fp) {
        syslog(LOG_ERR, "Cannot open PID file %s: %s", pid_file, strerror(errno));
        return -1;
    }
    
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
    return 0;
}

/* Parse command line arguments */
void parse_arguments(int argc, char *argv[]) {
    int opt;
    
    while ((opt = getopt(argc, argv, "c:l:dp:f:hw:r:")) != -1) {
        switch (opt) {
            case 'c':
                app_config.config_file = optarg;
                break;
            case 'l':
                app_config.log_file = optarg;
                break;
            case 'd':
                app_config.daemonize = 0;
                break;
            case 'p':
                app_config.web_port = atoi(optarg);
                break;
            case 'f':
                app_config.pid_file = optarg;
                break;
            case 'w':
                app_config.web_root = optarg;
                break;
            case 'h':
                printf("Usage: %s [options]\n", argv[0]);
                printf("Options:\n");
                printf("  -c <file>    Configuration file\n");
                printf("  -l <file>    Log file\n");
                printf("  -d           Run in foreground (no daemon)\n");
                printf("  -p <port>    Web server port (default: 8080)\n");
                printf("  -f <file>    PID file\n");
                printf("  -w <dir>     Web root directory\n");
                printf("  -h           Show this help\n");
                exit(EXIT_SUCCESS);
        }
    }
}

/* Main application entry */
int main(int argc, char *argv[]) {
    // Parse command line
    parse_arguments(argc, argv);
    
    // Setup logging
    openlog("solarize", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Solarize starting");
    
    // Daemonize if requested
    if (app_config.daemonize) {
        syslog(LOG_INFO, "Daemonizing process");
        if (daemonize() < 0) {
            syslog(LOG_ERR, "Failed to daemonize");
            return EXIT_FAILURE;
        }
    }
    
    // Write PID file
    if (write_pid_file(app_config.pid_file) < 0) {
        return EXIT_FAILURE;
    }
    
    // Setup signal handlers
    struct sigaction sa;
    sa.sa_handler = handle_shutdown_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    sa.sa_handler = handle_reload_signal;
    sigaction(SIGHUP, &sa, NULL);
    
    // Ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    // Load system configuration
    system_config_t sys_config;
    config_set_defaults(&sys_config);
    
    syslog(LOG_INFO, "Loading configuration from %s", app_config.config_file);
    config_error_t config_result = config_load(app_config.config_file, &sys_config);
    
    if (config_result != CONFIG_SUCCESS) {
        syslog(LOG_ERR, "Failed to load configuration: %d", config_result);
        if (config_result == CONFIG_FILE_NOT_FOUND && 
            config_save(app_config.config_file, &sys_config) == CONFIG_SUCCESS) {
            syslog(LOG_INFO, "Created default configuration");
        } else {
            return EXIT_FAILURE;
        }
    }
    
    // Initialize system controller
    system_ctrl = malloc(sizeof(system_controller_t));
    if (!system_ctrl) {
        syslog(LOG_ERR, "Failed to allocate system controller");
        return EXIT_FAILURE;
    }
    
    if (controller_init(system_ctrl, &sys_config) != 0) {
        syslog(LOG_ERR, "Failed to initialize system controller");
        free(system_ctrl);
        return EXIT_FAILURE;
    }
    
    // Setup web server
    // web_server = webserver_create(system_ctrl);

    // if (!web_server) {
    //     syslog(LOG_ERR, "Failed to create web server");
    //     controller_cleanup(system_ctrl);
    //     free(system_ctrl);
    //     return EXIT_FAILURE;
    // }
    
    // Configure web server
    // webserver_config_t ws_config;
    // webserver_default_config(&ws_config);
    
    // ws_config.port = app_config.web_port;
    // ws_config.web_root = app_config.web_root;
    // ws_config.static_dir = app_config.web_root;
    
    // Load web server config from environment
    // char *port_env = getenv("WEB_PORT");
    // if (port_env) ws_config.port = atoi(port_env);
    
    // char *static_dir = getenv("WEB_STATIC_DIR");
    // if (static_dir) {
    //     ws_config.static_dir = strdup(static_dir);
    // }
    
    // if (webserver_init(web_server, &ws_config) != 0) {
    //     syslog(LOG_ERR, "Failed to initialize web server");
    //     webserver_destroy(web_server);
    //     controller_cleanup(system_ctrl);
    //     free(system_ctrl);
    //     return EXIT_FAILURE;
    // }
    
    // // Start web server
    // if (webserver_start(web_server) != 0) {
    //     syslog(LOG_ERR, "Failed to start web server");
    //     webserver_destroy(web_server);
    //     controller_cleanup(system_ctrl);
    //     free(system_ctrl);
    //     return EXIT_FAILURE;
    // }
    
    // syslog(LOG_INFO, "Web server started on port %d", ws_config.port);
    syslog(LOG_INFO, "Energy Management System started successfully");
    
    // Main loop
    time_t last_statistics = time(NULL);
    // time_t last_web_update = time(NULL);
    uint64_t cycle_count = 0;
    
    while (running) {
        // Run control cycle
        controller_run_cycle(system_ctrl);
        cycle_count++;
        
        // Send WebSocket updates every second
        time_t now = time(NULL);
        // if (difftime(now, last_web_update) >= 1.0) {
        //     websocket_broadcast_system_update(web_server);
        //     last_web_update = now;
        // }
        
        // Log statistics every minute
        if (difftime(now, last_statistics) >= 60.0) {
            // syslog(LOG_INFO, "Cycles: %lu, Uptime: %.0f sec", 
            //        cycle_count, difftime(now, web_server->start_time));
            last_statistics = now;
        }
        
        // Sleep for control interval
        struct timespec ts = {
            .tv_sec = sys_config.control_interval,
            .tv_nsec = 0
        };
        nanosleep(&ts, NULL);
    }
    
    // Cleanup
    syslog(LOG_INFO, "Shutting down...");
    
    // if (web_server) webserver_destroy(web_server);
    if (system_ctrl) {
        controller_cleanup(system_ctrl);
        free(system_ctrl);
    }
    
    // Remove PID file
    unlink(app_config.pid_file);
    
    syslog(LOG_INFO, "Shutdown complete");
    closelog();
    
    return EXIT_SUCCESS;
}