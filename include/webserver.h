#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "controller.h"
#include <stdbool.h>
#include <time.h>
#include <jansson.h>

/* Web server modes */
typedef enum {
    WS_MODE_DEVELOPMENT = 0,
    WS_MODE_PRODUCTION,
    WS_MODE_MAINTENANCE
} webserver_mode_t;

/* Authentication */
typedef enum {
    ROLE_GUEST = 0,
    ROLE_VIEWER,
    ROLE_OPERATOR,
    ROLE_ADMIN,
    ROLE_SUPERUSER
} user_role_t;

/* Forward declaration */
struct mg_connection;

/* API route handler type */
typedef void (*api_handler_t)(struct mg_connection *c, void *user_data);

/* Route definition */
typedef struct {
    const char *method;
    const char *path;
    api_handler_t handler;
    user_role_t min_role;
    bool require_auth;
} api_route_t;

/* WebSocket client context */
typedef struct {
    int id;
    time_t connected_at;
    time_t last_activity;
    char ip_address[46];
    user_role_t role;
    char username[32];
    
    /* Subscriptions */
    bool subscribe_system;
    bool subscribe_pv;
    bool subscribe_battery;
    bool subscribe_loads;
    bool subscribe_agriculture;
    bool subscribe_ev;
    bool subscribe_alarms;
} ws_client_t;

/* User session */
typedef struct {
    char session_id[33];
    char username[32];
    user_role_t role;
    time_t created;
    time_t last_activity;
    char ip_address[46];
    bool valid;
} user_session_t;

/* API key */
typedef struct {
    char key[65];
    char name[64];
    user_role_t role;
    time_t created;
    time_t last_used;
    bool enabled;
} api_key_t;

/* Web server configuration */
typedef struct {
    int port;
    int ssl_port;
    bool enable_ssl;
    
    /* SSL configuration */
    char *ssl_cert_file;
    char *ssl_key_file;
    char *ssl_ca_file;
    
    /* Authentication */
    bool enable_auth;
    char *admin_password_hash;
    int session_timeout;
    
    /* Directories */
    char *web_root;
    char *static_dir;
    char *upload_dir;
    
    /* Security */
    bool enable_cors;
    char *cors_origin;
    int rate_limit;
    
    /* Performance */
    int max_connections;
    int thread_count;
    int request_timeout;
    
    /* Logging */
    char *access_log;
    char *error_log;
    int log_level;
} webserver_config_t;

/* Web server context */
typedef struct {
    struct mg_context *ctx;
    webserver_config_t config;
    webserver_mode_t mode;
    
    /* System reference */
    system_controller_t *controller;
    
    /* Authentication */
    user_session_t sessions[100];
    int session_count;
    api_key_t api_keys[50];
    int api_key_count;
    
    /* WebSocket clients */
    ws_client_t ws_clients[64];
    int ws_client_count;
    pthread_mutex_t ws_mutex;
    
    /* Statistics */
    time_t start_time;
    uint64_t total_requests;
    uint64_t total_errors;
    
    /* Shutdown flag */
    volatile bool shutdown_requested;
} webserver_t;

/* Web server functions */
webserver_t* webserver_create(system_controller_t *controller);
int webserver_init(webserver_t *server, webserver_config_t *config);
int webserver_start(webserver_t *server);
void webserver_stop(webserver_t *server);
void webserver_destroy(webserver_t *server);

/* Configuration */
void webserver_default_config(webserver_config_t *config);

/* Authentication */
int webserver_authenticate(webserver_t *server, struct mg_connection *c);
user_session_t* webserver_create_session(webserver_t *server, const char *username,
                                        user_role_t role, const char *ip);
int webserver_validate_session(webserver_t *server, const char *session_id);
int webserver_destroy_session(webserver_t *server, const char *session_id);
api_key_t* webserver_create_api_key(webserver_t *server, const char *name,
                                   user_role_t role);
int webserver_validate_api_key(webserver_t *server, const char *key, const char *ip);

/* WebSocket */
void websocket_broadcast_system_update(webserver_t *server);
void websocket_broadcast_alarm(webserver_t *server, alarm_code_t alarm, bool active);

/* Utility */
char* webserver_generate_session_id(void);
char* webserver_generate_api_key(void);
char* webserver_hash_password(const char *password);
int webserver_verify_password(const char *password, const char *hash);
json_t* webserver_get_json_body(struct mg_connection *c);

/* API Handlers */
void api_system_status(struct mg_connection *c, void *user_data);
void api_system_config(struct mg_connection *c, void *user_data);
void api_system_stats(struct mg_connection *c, void *user_data);
void api_system_mode(struct mg_connection *c, void *user_data);
void api_pv_status(struct mg_connection *c, void *user_data);
void api_battery_status(struct mg_connection *c, void *user_data);
void api_loads_status(struct mg_connection *c, void *user_data);
void api_loads_control(struct mg_connection *c, void *user_data);
void api_agriculture_status(struct mg_connection *c, void *user_data);
void api_agriculture_control(struct mg_connection *c, void *user_data);
void api_ev_status(struct mg_connection *c, void *user_data);
void api_ev_control(struct mg_connection *c, void *user_data);
void api_alarms(struct mg_connection *c, void *user_data);
void api_alarms_ack(struct mg_connection *c, void *user_data);
void api_history(struct mg_connection *c, void *user_data);
void api_export_data(struct mg_connection *c, void *user_data);
void api_login(struct mg_connection *c, void *user_data);
void api_logout(struct mg_connection *c, void *user_data);
void api_user_info(struct mg_connection *c, void *user_data);
void api_create_apikey(struct mg_connection *c, void *user_data);
void api_revoke_apikey(struct mg_connection *c, void *user_data);

/* Static file serving */
void serve_static_file(struct mg_connection *c, const char *path);

#endif /* WEBSERVER_H */