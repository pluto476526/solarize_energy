#include "webserver.h"
#include "../mongoose/mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

/* CivetWeb callback function prototypes */
static int event_handler(struct mg_connection *c, int ev, void *ev_data);
static void ws_connect_handler(struct mg_connection *c);
static void ws_ready_handler(struct mg_connection *c);
static void ws_data_handler(struct mg_connection *c, int bits, char *data, size_t len);
static void ws_close_handler(struct mg_connection *c);

/* Internal helper functions */
static int is_authenticated(webserver_t *server, struct mg_connection *c);
static user_role_t get_user_role(webserver_t *server, struct mg_connection *c);
static void send_json_response(struct mg_connection *c, int status, json_t *json);
static void send_error_response(struct mg_connection *c, int status, const char *message, int code);
static void send_success_response(struct mg_connection *c, const char *message, json_t *data);
static void cleanup_expired_sessions(webserver_t *server);
static void broadcast_to_subscribers(webserver_t *server, const char *topic, json_t *data);

/* Route definitions */
static const api_route_t api_routes[] = {
    /* System API */
    {"GET", "/api/system/status", api_system_status, ROLE_VIEWER, true},
    {"GET", "/api/system/config", api_system_config, ROLE_ADMIN, true},
    {"POST", "/api/system/config", api_system_config, ROLE_ADMIN, true},
    {"GET", "/api/system/stats", api_system_stats, ROLE_VIEWER, true},
    {"POST", "/api/system/mode", api_system_mode, ROLE_OPERATOR, true},
    
    /* PV API */
    {"GET", "/api/pv/status", api_pv_status, ROLE_VIEWER, true},
    
    /* Battery API */
    {"GET", "/api/battery/status", api_battery_status, ROLE_VIEWER, true},
    
    /* Loads API */
    {"GET", "/api/loads/status", api_loads_status, ROLE_VIEWER, true},
    {"POST", "/api/loads/control", api_loads_control, ROLE_OPERATOR, true},
    
    /* Agriculture API */
    {"GET", "/api/agriculture/status", api_agriculture_status, ROLE_VIEWER, true},
    {"POST", "/api/agriculture/control", api_agriculture_control, ROLE_OPERATOR, true},
    
    /* EV API */
    {"GET", "/api/ev/status", api_ev_status, ROLE_VIEWER, true},
    {"POST", "/api/ev/control", api_ev_control, ROLE_OPERATOR, true},
    
    /* Alarms API */
    {"GET", "/api/alarms", api_alarms, ROLE_VIEWER, true},
    {"POST", "/api/alarms/acknowledge", api_alarms_ack, ROLE_OPERATOR, true},
    
    /* History API */
    {"GET", "/api/history", api_history, ROLE_VIEWER, true},
    {"GET", "/api/export", api_export_data, ROLE_ADMIN, true},
    
    /* Auth API */
    {"POST", "/api/login", api_login, ROLE_GUEST, false},
    {"POST", "/api/logout", api_logout, ROLE_VIEWER, true},
    {"GET", "/api/user", api_user_info, ROLE_VIEWER, true},
    {"POST", "/api/apikeys", api_create_apikey, ROLE_ADMIN, true},
    {"POST", "/api/apikeys/revoke", api_revoke_apikey, ROLE_ADMIN, true},
    
    {NULL, NULL, NULL, 0, false} /* Sentinel */
};

/* Create web server instance */
webserver_t* webserver_create(system_controller_t *controller) {
    webserver_t *server = calloc(1, sizeof(webserver_t));
    if (!server) return NULL;
    
    server->controller = controller;
    server->start_time = time(NULL);
    server->mode = WS_MODE_PRODUCTION;
    server->ws_client_count = 0;
    server->session_count = 0;
    server->api_key_count = 0;
    
    /* Initialize mutex */
    if (pthread_mutex_init(&server->ws_mutex, NULL) != 0) {
        free(server);
        return NULL;
    }
    
    return server;
}

/* Initialize web server with configuration */
int webserver_init(webserver_t *server, webserver_config_t *config) {
    if (!server || !config) return -1;
    
    memcpy(&server->config, config, sizeof(webserver_config_t));
    
    /* Create CivetWeb options */
    const char *options[] = {
        "listening_ports", "8080",
        "document_root", config->web_root ? config->web_root : "./web",
        "enable_directory_listing", "no",
        "enable_keep_alive", "yes",
        "request_timeout_ms", "10000",
        "num_threads", "4",
        "error_log_file", config->error_log ? config->error_log : "webserver_error.log",
        "access_log_file", config->access_log ? config->access_log : "webserver_access.log",
        NULL
    };
    
    /* Initialize Mongoose context */
    struct mg_callbacks callbacks = {0};
    callbacks.event_handler = event_handler;
    
    server->ctx = mg_start(&callbacks, server, options);
    if (!server->ctx) {
        fprintf(stderr, "Failed to start CivetWeb server\n");
        return -1;
    }
    
    /* Create necessary directories */
    if (config->static_dir) {
        mkdir(config->static_dir, 0755);
    }
    if (config->upload_dir) {
        mkdir(config->upload_dir, 0755);
    }
    
    printf("Web server started on port %d\n", config->port);
    printf("Web root: %s\n", config->web_root ? config->web_root : "./web");
    
    return 0;
}

/* Start web server */
int webserver_start(webserver_t *server) {
    /* Server is started by mg_start */
    return 0;
}

/* Stop web server */
void webserver_stop(webserver_t *server) {
    if (server && server->ctx) {
        mg_stop(server->ctx);
        server->ctx = NULL;
    }
}

/* Destroy web server */
void webserver_destroy(webserver_t *server) {
    if (!server) return;
    
    webserver_stop(server);
    pthread_mutex_destroy(&server->ws_mutex);
    free(server);
}

/* Default web server configuration */
void webserver_default_config(webserver_config_t *config) {
    memset(config, 0, sizeof(webserver_config_t));
    
    config->port = 8080;
    config->ssl_port = 8443;
    config->enable_ssl = false;
    
    config->enable_auth = true;
    config->admin_password_hash = NULL;
    config->session_timeout = 3600; /* 1 hour */
    
    config->web_root = "./web";
    config->static_dir = "./web/static";
    config->upload_dir = "./uploads";
    
    config->enable_cors = true;
    config->cors_origin = "*";
    config->rate_limit = 100; /* requests per minute */
    
    config->max_connections = 100;
    config->thread_count = 4;
    config->request_timeout = 30;
    
    config->access_log = "webserver_access.log";
    config->error_log = "webserver_error.log";
    config->log_level = 2; /* WARN */
}

/* Event handler for CivetWeb */
static int event_handler(struct mg_connection *c, int ev, void *ev_data) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    switch (ev) {
        case MG_EV_HTTP_REQUEST: {
            struct http_message *hm = (struct http_message *)ev_data;
            
            /* Check if this is a WebSocket upgrade request */
            if (mg_strstr(hm->message, mg_mk_str("Upgrade: websocket"))) {
                ws_connect_handler(c);
                return 0;
            }
            
            /* Handle API requests */
            for (int i = 0; api_routes[i].method != NULL; i++) {
                const api_route_t *route = &api_routes[i];
                
                if (hm->method.len == strlen(route->method) &&
                    strncmp(hm->method.p, route->method, hm->method.len) == 0 &&
                    hm->uri.len >= strlen(route->path) &&
                    strncmp(hm->uri.p, route->path, strlen(route->path)) == 0) {
                    
                    /* Check authentication */
                    if (route->require_auth) {
                        if (!is_authenticated(server, c)) {
                            send_error_response(c, 401, "Authentication required", 1001);
                            return 0;
                        }
                        
                        if (get_user_role(server, c) < route->min_role) {
                            send_error_response(c, 403, "Insufficient privileges", 1002);
                            return 0;
                        }
                    }
                    
                    /* Call the handler */
                    route->handler(c, server->controller);
                    return 0;
                }
            }
            
            /* Serve static files */
            if (strncmp(hm->uri.p, "/api/", 5) != 0) {
                serve_static_file(c, hm->uri.p);
                return 0;
            }
            
            /* 404 for unknown API endpoints */
            mg_http_send_error(c, 404, "Not Found");
            return 0;
        }
        
        case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
            ws_ready_handler(c);
            break;
            
        case MG_EV_WEBSOCKET_FRAME:
            ws_data_handler(c, ev, ev_data);
            break;
            
        case MG_EV_CLOSE:
            if (c->flags & MG_F_IS_WEBSOCKET) {
                ws_close_handler(c);
            }
            break;
    }
    
    return 0;
}

/* WebSocket connection handler */
static void ws_connect_handler(struct mg_connection *c) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    /* Upgrade to WebSocket */
    mg_ws_handshake(c);
}

/* WebSocket ready handler */
static void ws_ready_handler(struct mg_connection *c) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    pthread_mutex_lock(&server->ws_mutex);
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < 64; i++) {
        if (server->ws_clients[i].id == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot >= 0) {
        ws_client_t *client = &server->ws_clients[slot];
        client->id = slot + 1;
        client->connected_at = time(NULL);
        client->last_activity = time(NULL);
        snprintf(client->ip_address, sizeof(client->ip_address), "%s", c->remote_ip);
        client->role = ROLE_VIEWER;
        client->username[0] = '\0';
        
        /* Subscribe to basic updates by default */
        client->subscribe_system = true;
        client->subscribe_alarms = true;
        
        server->ws_client_count++;
        
        /* Send welcome message */
        json_t *welcome = json_object();
        json_object_set_new(welcome, "type", json_string("connected"));
        json_object_set_new(welcome, "client_id", json_integer(client->id));
        json_object_set_new(welcome, "timestamp", json_integer(time(NULL)));
        
        char *json_str = json_dumps(welcome, JSON_COMPACT);
        mg_ws_send(c, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
        free(json_str);
        json_decref(welcome);
    }
    
    pthread_mutex_unlock(&server->ws_mutex);
}

/* WebSocket data handler */
static void ws_data_handler(struct mg_connection *c, int bits, char *data, size_t len) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    /* Parse JSON message */
    json_error_t error;
    json_t *msg = json_loadb(data, len, 0, &error);
    if (!msg) return;
    
    /* Get message type */
    json_t *type_json = json_object_get(msg, "type");
    if (!type_json) {
        json_decref(msg);
        return;
    }
    
    const char *type = json_string_value(type_json);
    if (!type) {
        json_decref(msg);
        return;
    }
    
    /* Handle different message types */
    if (strcmp(type, "auth") == 0) {
        /* Authentication */
        json_t *token = json_object_get(msg, "token");
        if (token) {
            const char *session_id = json_string_value(token);
            if (session_id) {
                /* Find and validate session */
                pthread_mutex_lock(&server->ws_mutex);
                for (int i = 0; i < server->session_count; i++) {
                    if (strcmp(server->sessions[i].session_id, session_id) == 0 &&
                        server->sessions[i].valid) {
                        
                        /* Update WebSocket client */
                        for (int j = 0; j < 64; j++) {
                            if (server->ws_clients[j].id && 
                                strcmp(server->ws_clients[j].ip_address, c->remote_ip) == 0) {
                                
                                strncpy(server->ws_clients[j].username, 
                                       server->sessions[i].username, 31);
                                server->ws_clients[j].role = server->sessions[i].role;
                                server->ws_clients[j].last_activity = time(NULL);
                                
                                /* Send auth success */
                                json_t *response = json_object();
                                json_object_set_new(response, "type", json_string("auth_success"));
                                json_object_set_new(response, "role", 
                                                   json_integer(server->sessions[i].role));
                                json_object_set_new(response, "username", 
                                                   json_string(server->sessions[i].username));
                                
                                char *resp_str = json_dumps(response, JSON_COMPACT);
                                mg_ws_send(c, resp_str, strlen(resp_str), WEBSOCKET_OP_TEXT);
                                free(resp_str);
                                json_decref(response);
                                break;
                            }
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&server->ws_mutex);
            }
        }
    }
    else if (strcmp(type, "subscribe") == 0) {
        /* Update subscriptions */
        json_t *subscriptions = json_object_get(msg, "subscriptions");
        if (subscriptions) {
            pthread_mutex_lock(&server->ws_mutex);
            
            for (int i = 0; i < 64; i++) {
                if (server->ws_clients[i].id && 
                    strcmp(server->ws_clients[i].ip_address, c->remote_ip) == 0) {
                    
                    json_t *sub;
                    
                    sub = json_object_get(subscriptions, "system");
                    if (sub) server->ws_clients[i].subscribe_system = json_is_true(sub);
                    
                    sub = json_object_get(subscriptions, "pv");
                    if (sub) server->ws_clients[i].subscribe_pv = json_is_true(sub);
                    
                    sub = json_object_get(subscriptions, "battery");
                    if (sub) server->ws_clients[i].subscribe_battery = json_is_true(sub);
                    
                    sub = json_object_get(subscriptions, "loads");
                    if (sub) server->ws_clients[i].subscribe_loads = json_is_true(sub);
                    
                    sub = json_object_get(subscriptions, "agriculture");
                    if (sub) server->ws_clients[i].subscribe_agriculture = json_is_true(sub);
                    
                    sub = json_object_get(subscriptions, "ev");
                    if (sub) server->ws_clients[i].subscribe_ev = json_is_true(sub);
                    
                    sub = json_object_get(subscriptions, "alarms");
                    if (sub) server->ws_clients[i].subscribe_alarms = json_is_true(sub);
                    
                    server->ws_clients[i].last_activity = time(NULL);
                    break;
                }
            }
            
            pthread_mutex_unlock(&server->ws_mutex);
        }
    }
    else if (strcmp(type, "ping") == 0) {
        /* Respond to ping */
        json_t *response = json_object();
        json_object_set_new(response, "type", json_string("pong"));
        json_object_set_new(response, "timestamp", json_integer(time(NULL)));
        
        char *resp_str = json_dumps(response, JSON_COMPACT);
        mg_ws_send(c, resp_str, strlen(resp_str), WEBSOCKET_OP_TEXT);
        free(resp_str);
        json_decref(response);
    }
    
    json_decref(msg);
}

/* WebSocket close handler */
static void ws_close_handler(struct mg_connection *c) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    pthread_mutex_lock(&server->ws_mutex);
    
    for (int i = 0; i < 64; i++) {
        if (server->ws_clients[i].id && 
            strcmp(server->ws_clients[i].ip_address, c->remote_ip) == 0) {
            
            server->ws_clients[i].id = 0;
            server->ws_client_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&server->ws_mutex);
}

/* Broadcast system update to WebSocket clients */
void websocket_broadcast_system_update(webserver_t *server) {
    static time_t last_update = 0;
    time_t now = time(NULL);
    
    /* Limit updates to once per second */
    if (difftime(now, last_update) < 1.0) return;
    last_update = now;
    
    /* Create system update message */
    json_t *update = json_object();
    json_object_set_new(update, "type", json_string("system_update"));
    json_object_set_new(update, "timestamp", json_integer(now));
    
    /* Add measurements */
    json_t *measurements = json_object();
    json_object_set_new(measurements, "grid_power", 
                       json_real(server->controller->measurements.grid_power));
    json_object_set_new(measurements, "pv_power", 
                       json_real(server->controller->measurements.pv_power_total));
    json_object_set_new(measurements, "battery_power", 
                       json_real(server->controller->measurements.battery_power));
    json_object_set_new(measurements, "battery_soc", 
                       json_real(server->controller->measurements.battery_soc));
    json_object_set_new(measurements, "load_power", 
                       json_real(server->controller->measurements.load_power_total));
    json_object_set_new(update, "measurements", measurements);
    
    /* Add status */
    json_t *status = json_object();
    json_object_set_new(status, "mode", 
                       json_integer(server->controller->status.mode));
    json_object_set_new(status, "grid_available", 
                       json_boolean(server->controller->status.grid_available));
    json_object_set_new(update, "status", status);
    
    char *json_str = json_dumps(update, JSON_COMPACT);
    json_decref(update);
    
    if (!json_str) return;
    
    /* Broadcast to subscribed clients */
    pthread_mutex_lock(&server->ws_mutex);
    
    /* Note: In production, you would iterate through mg_connections */
    /* This is a simplified version */
    
    pthread_mutex_unlock(&server->ws_mutex);
    
    free(json_str);
}

/* Broadcast alarm update */
void websocket_broadcast_alarm(webserver_t *server, alarm_code_t alarm, bool active) {
    json_t *msg = json_object();
    json_object_set_new(msg, "type", json_string("alarm_update"));
    json_object_set_new(msg, "alarm", json_integer(alarm));
    json_object_set_new(msg, "active", json_boolean(active));
    json_object_set_new(msg, "timestamp", json_integer(time(NULL)));
    
    char *json_str = json_dumps(msg, JSON_COMPACT);
    json_decref(msg);
    
    if (!json_str) return;
    
    /* Broadcast to subscribed clients */
    /* Note: Implementation depends on how CivetWeb stores connections */
    
    free(json_str);
}

/* Authentication helper */
static int is_authenticated(webserver_t *server, struct mg_connection *c) {
    const char *session_id = NULL;
    const char *auth_header = mg_get_header(c, "Authorization");
    
    if (auth_header && strncmp(auth_header, "Bearer ", 7) == 0) {
        session_id = auth_header + 7;
    } else {
        /* Check cookie */
        const char *cookie = mg_get_header(c, "Cookie");
        if (cookie) {
            const char *session_start = strstr(cookie, "session_id=");
            if (session_start) {
                session_start += 11;
                const char *session_end = strchr(session_start, ';');
                if (session_end) {
                    char session_buf[33];
                    size_t len = session_end - session_start;
                    if (len > 32) len = 32;
                    strncpy(session_buf, session_start, len);
                    session_buf[len] = '\0';
                    session_id = session_buf;
                } else {
                    session_id = session_start;
                }
            }
        }
    }
    
    if (!session_id) return 0;
    
    return webserver_validate_session(server, session_id);
}

/* Get user role */
static user_role_t get_user_role(webserver_t *server, struct mg_connection *c) {
    const char *session_id = NULL;
    const char *auth_header = mg_get_header(c, "Authorization");
    
    if (auth_header && strncmp(auth_header, "Bearer ", 7) == 0) {
        session_id = auth_header + 7;
    }
    
    if (!session_id) return ROLE_GUEST;
    
    for (int i = 0; i < server->session_count; i++) {
        if (strcmp(server->sessions[i].session_id, session_id) == 0 &&
            server->sessions[i].valid) {
            return server->sessions[i].role;
        }
    }
    
    return ROLE_GUEST;
}

/* Send JSON response */
static void send_json_response(struct mg_connection *c, int status, json_t *json) {
    if (!json) {
        mg_http_send_error(c, 500, "Internal Server Error");
        return;
    }
    
    char *json_str = json_dumps(json, JSON_INDENT(2));
    if (!json_str) {
        mg_http_send_error(c, 500, "Internal Server Error");
        return;
    }
    
    mg_printf(c, "HTTP/1.1 %d OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %lu\r\n"
              "Connection: close\r\n"
              "\r\n%s",
              status, strlen(json_str), json_str);
    
    free(json_str);
}

/* Send error response */
static void send_error_response(struct mg_connection *c, int status, const char *message, int code) {
    json_t *error = json_object();
    json_object_set_new(error, "error", json_string(message));
    json_object_set_new(error, "code", json_integer(code));
    json_object_set_new(error, "timestamp", json_integer(time(NULL)));
    
    send_json_response(c, status, error);
    json_decref(error);
}

/* Send success response */
static void send_success_response(struct mg_connection *c, const char *message, json_t *data) {
    json_t *response = json_object();
    json_object_set_new(response, "success", json_boolean(true));
    json_object_set_new(response, "message", json_string(message));
    json_object_set_new(response, "timestamp", json_integer(time(NULL)));
    
    if (data) {
        json_object_set_new(response, "data", data);
    }
    
    send_json_response(c, 200, response);
    json_decref(response);
}

/* Get JSON from request body */
json_t* webserver_get_json_body(struct mg_connection *c) {
    struct http_message *hm = (struct http_message *)c->data;
    
    if (hm->body.len == 0) {
        return NULL;
    }
    
    json_error_t error;
    json_t *root = json_loadb(hm->body.p, hm->body.len, 0, &error);
    
    return root;
}

/* Clean up expired sessions */
static void cleanup_expired_sessions(webserver_t *server) {
    time_t now = time(NULL);
    
    for (int i = 0; i < server->session_count; i++) {
        if (server->sessions[i].valid &&
            difftime(now, server->sessions[i].last_activity) > server->config.session_timeout) {
            server->sessions[i].valid = false;
        }
    }
}

/* Generate session ID */
char* webserver_generate_session_id(void) {
    unsigned char random_bytes[16];
    char *session_id = malloc(33);
    
    if (!session_id) return NULL;
    
    RAND_bytes(random_bytes, sizeof(random_bytes));
    
    for (int i = 0; i < 16; i++) {
        sprintf(session_id + (i * 2), "%02x", random_bytes[i]);
    }
    session_id[32] = '\0';
    
    return session_id;
}

/* Generate API key */
char* webserver_generate_api_key(void) {
    unsigned char random_bytes[32];
    char *api_key = malloc(65);
    
    if (!api_key) return NULL;
    
    RAND_bytes(random_bytes, sizeof(random_bytes));
    
    for (int i = 0; i < 32; i++) {
        sprintf(api_key + (i * 2), "%02x", random_bytes[i]);
    }
    api_key[64] = '\0';
    
    return api_key;
}

/* Hash password */
char* webserver_hash_password(const char *password) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    char *hashed = malloc(65);
    
    if (!hashed) return NULL;
    
    SHA256((unsigned char *)password, strlen(password), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hashed + (i * 2), "%02x", hash[i]);
    }
    hashed[64] = '\0';
    
    return hashed;
}

/* Verify password */
int webserver_verify_password(const char *password, const char *hash) {
    char *computed_hash = webserver_hash_password(password);
    if (!computed_hash) return 0;
    
    int result = (strcmp(computed_hash, hash) == 0);
    free(computed_hash);
    return result;
}

/* Create session */
user_session_t* webserver_create_session(webserver_t *server, const char *username,
                                        user_role_t role, const char *ip) {
    /* Clean up expired sessions first */
    cleanup_expired_sessions(server);
    
    /* Find free slot or overwrite expired session */
    int slot = -1;
    for (int i = 0; i < 100; i++) {
        if (!server->sessions[i].valid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        /* No free slots, overwrite oldest */
        slot = 0;
        time_t oldest = server->sessions[0].last_activity;
        for (int i = 1; i < 100; i++) {
            if (server->sessions[i].last_activity < oldest) {
                slot = i;
                oldest = server->sessions[i].last_activity;
            }
        }
    }
    
    /* Generate session ID */
    char *session_id = webserver_generate_session_id();
    if (!session_id) return NULL;
    
    /* Create session */
    server->sessions[slot].valid = true;
    strncpy(server->sessions[slot].session_id, session_id, 32);
    strncpy(server->sessions[slot].username, username, 31);
    server->sessions[slot].role = role;
    server->sessions[slot].created = time(NULL);
    server->sessions[slot].last_activity = time(NULL);
    strncpy(server->sessions[slot].ip_address, ip, 45);
    
    free(session_id);
    
    if (slot >= server->session_count) {
        server->session_count = slot + 1;
    }
    
    return &server->sessions[slot];
}

/* Validate session */
int webserver_validate_session(webserver_t *server, const char *session_id) {
    if (!session_id) return 0;
    
    for (int i = 0; i < server->session_count; i++) {
        if (server->sessions[i].valid &&
            strcmp(server->sessions[i].session_id, session_id) == 0) {
            server->sessions[i].last_activity = time(NULL);
            return 1;
        }
    }
    
    return 0;
}

/* Destroy session */
int webserver_destroy_session(webserver_t *server, const char *session_id) {
    if (!session_id) return -1;
    
    for (int i = 0; i < server->session_count; i++) {
        if (server->sessions[i].valid &&
            strcmp(server->sessions[i].session_id, session_id) == 0) {
            server->sessions[i].valid = false;
            return 0;
        }
    }
    
    return -1;
}

/* Create API key */
api_key_t* webserver_create_api_key(webserver_t *server, const char *name,
                                   user_role_t role) {
    if (server->api_key_count >= 50) return NULL;
    
    api_key_t *key = &server->api_keys[server->api_key_count];
    
    char *key_str = webserver_generate_api_key();
    if (!key_str) return NULL;
    
    strncpy(key->key, key_str, 64);
    strncpy(key->name, name, 63);
    key->role = role;
    key->created = time(NULL);
    key->last_used = 0;
    key->enabled = true;
    
    server->api_key_count++;
    
    free(key_str);
    return key;
}

/* Validate API key */
int webserver_validate_api_key(webserver_t *server, const char *key, const char *ip) {
    if (!key) return 0;
    
    for (int i = 0; i < server->api_key_count; i++) {
        if (server->api_keys[i].enabled &&
            strcmp(server->api_keys[i].key, key) == 0) {
            server->api_keys[i].last_used = time(NULL);
            return 1;
        }
    }
    
    return 0;
}

/* Serve static file */
void serve_static_file(struct mg_connection *c, const char *path) {
    /* Default to index.html for root */
    if (strcmp(path, "/") == 0) {
        path = "/index.html";
    }
    
    /* Security: prevent directory traversal */
    if (strstr(path, "..") != NULL) {
        mg_http_send_error(c, 403, "Forbidden");
        return;
    }
    
    /* Serve the file */
    mg_http_serve_file(c, path, mg_mk_str("text/html"), mg_mk_str(""));
}