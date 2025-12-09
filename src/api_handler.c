#include "webserver.h"
#include "mongoose.h"
#include <jansson.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* Helper functions */
static json_t* create_system_status_json(system_controller_t *controller);
static json_t* create_pv_status_json(system_controller_t *controller);
static json_t* create_battery_status_json(system_controller_t *controller);
static json_t* create_loads_status_json(system_controller_t *controller);
static json_t* create_agriculture_status_json(system_controller_t *controller);
static json_t* create_ev_status_json(system_controller_t *controller);
static json_t* create_alarms_json(system_controller_t *controller);
static json_t* create_system_stats_json(system_controller_t *controller);

/* System Status API */
void api_system_status(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *status = create_system_status_json(controller);
    
    if (status) {
        send_json_response(c, 200, status);
        json_decref(status);
    } else {
        send_error_response(c, 500, "Failed to get system status", 5001);
    }
}

/* System Config API */
void api_system_config(struct mg_connection *c, void *user_data) {
    struct http_message *hm = (struct http_message *)c->data;
    
    if (strncmp(hm->method.p, "GET", 3) == 0) {
        /* Return current configuration */
        json_t *config = json_object();
        json_object_set_new(config, "config", json_string("Configuration endpoint"));
        
        send_json_response(c, 200, config);
        json_decref(config);
    } else if (strncmp(hm->method.p, "POST", 4) == 0) {
        /* Update configuration */
        json_t *body = webserver_get_json_body(c);
        if (!body) {
            send_error_response(c, 400, "Invalid JSON body", 4001);
            return;
        }
        
        /* TODO: Update configuration */
        json_decref(body);
        send_success_response(c, "Configuration updated", NULL);
    }
}

/* System Stats API */
void api_system_stats(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *stats = create_system_stats_json(controller);
    
    if (stats) {
        send_json_response(c, 200, stats);
        json_decref(stats);
    } else {
        send_error_response(c, 500, "Failed to get system statistics", 5002);
    }
}

/* System Mode Control API */
void api_system_mode(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *body = webserver_get_json_body(c);
    
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    json_t *mode_json = json_object_get(body, "mode");
    if (!mode_json || !json_is_integer(mode_json)) {
        json_decref(body);
        send_error_response(c, 400, "Missing or invalid mode parameter", 4002);
        return;
    }
    
    int mode = json_integer_value(mode_json);
    
    if (mode >= MODE_NORMAL && mode <= MODE_EMERGENCY) {
        controller->status.mode = (system_mode_t)mode;
        controller->status.last_mode_change = time(NULL);
        
        /* Update controller mode based on system mode */
        switch (mode) {
            case MODE_NORMAL:
            case MODE_ISLAND:
            case MODE_CRITICAL:
                controller->mode = CTRL_MODE_AUTO;
                break;
            case MODE_MAINTENANCE:
                controller->mode = CTRL_MODE_MANUAL;
                break;
            case MODE_EMERGENCY:
                controller->mode = CTRL_MODE_SAFE;
                break;
        }
        
        json_decref(body);
        send_success_response(c, "System mode changed successfully", NULL);
    } else {
        json_decref(body);
        send_error_response(c, 400, "Invalid system mode", 4003);
    }
}

/* PV Status API */
void api_pv_status(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *status = create_pv_status_json(controller);
    
    if (status) {
        send_json_response(c, 200, status);
        json_decref(status);
    } else {
        send_error_response(c, 500, "Failed to get PV status", 5003);
    }
}

/* Battery Status API */
void api_battery_status(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *status = create_battery_status_json(controller);
    
    if (status) {
        send_json_response(c, 200, status);
        json_decref(status);
    } else {
        send_error_response(c, 500, "Failed to get battery status", 5004);
    }
}

/* Loads Status API */
void api_loads_status(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *status = create_loads_status_json(controller);
    
    if (status) {
        send_json_response(c, 200, status);
        json_decref(status);
    } else {
        send_error_response(c, 500, "Failed to get loads status", 5005);
    }
}

/* Loads Control API */
void api_loads_control(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *body = webserver_get_json_body(c);
    
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    const char *load_id = NULL;
    int command = -1;
    
    json_t *load_id_json = json_object_get(body, "load_id");
    json_t *command_json = json_object_get(body, "command");
    
    if (load_id_json) load_id = json_string_value(load_id_json);
    if (command_json) command = json_integer_value(command_json);
    
    if (!load_id || command < 0) {
        json_decref(body);
        send_error_response(c, 400, "Missing required parameters", 4002);
        return;
    }
    
    /* Find and control load */
    load_manager_t *lm = &controller->load_manager;
    bool success = false;
    
    for (int i = 0; i < lm->load_count; i++) {
        if (strcmp(lm->loads[i].id, load_id) == 0) {
            switch (command) {
                case 0: /* Turn off */
                    lm->load_states[i] = LOAD_STATE_OFF;
                    lm->loads[i].current_state = false;
                    lm->loads[i].last_state_change = time(NULL);
                    success = true;
                    break;
                case 1: /* Turn on */
                    lm->load_states[i] = LOAD_STATE_ON;
                    lm->loads[i].current_state = true;
                    lm->loads[i].last_state_change = time(NULL);
                    success = true;
                    break;
                case 2: /* Shed */
                    if (lm->loads[i].is_sheddable) {
                        lm->load_states[i] = LOAD_STATE_SHED;
                        lm->loads[i].current_state = false;
                        lm->loads[i].last_state_change = time(NULL);
                        success = true;
                    }
                    break;
            }
            break;
        }
    }
    
    json_decref(body);
    
    if (success) {
        send_success_response(c, "Load command executed", NULL);
    } else {
        send_error_response(c, 400, "Failed to execute load command", 4003);
    }
}

/* Agriculture Status API */
void api_agriculture_status(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *status = create_agriculture_status_json(controller);
    
    if (status) {
        send_json_response(c, 200, status);
        json_decref(status);
    } else {
        send_error_response(c, 500, "Failed to get agriculture status", 5006);
    }
}

/* Agriculture Control API */
void api_agriculture_control(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *body = webserver_get_json_body(c);
    
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    /* TODO: Implement agriculture control */
    
    json_decref(body);
    send_success_response(c, "Irrigation command executed", NULL);
}

/* EV Status API */
void api_ev_status(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *status = create_ev_status_json(controller);
    
    if (status) {
        send_json_response(c, 200, status);
        json_decref(status);
    } else {
        send_error_response(c, 500, "Failed to get EV status", 5007);
    }
}

/* EV Control API */
void api_ev_control(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *body = webserver_get_json_body(c);
    
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    /* TODO: Implement EV control */
    
    json_decref(body);
    send_success_response(c, "EV command executed", NULL);
}

/* Alarms API */
void api_alarms(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *alarms = create_alarms_json(controller);
    
    if (alarms) {
        send_json_response(c, 200, alarms);
        json_decref(alarms);
    } else {
        send_error_response(c, 500, "Failed to get alarms", 5008);
    }
}

/* Acknowledge Alarms API */
void api_alarms_ack(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    json_t *body = webserver_get_json_body(c);
    
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    json_t *ack_all = json_object_get(body, "acknowledge_all");
    json_t *alarm_code = json_object_get(body, "alarm_code");
    
    if (ack_all && json_is_true(ack_all)) {
        /* Acknowledge all alarms */
        controller->status.alarms = 0;
        controller->status.warnings = 0;
    } else if (alarm_code && json_is_integer(alarm_code)) {
        int code = json_integer_value(alarm_code);
        if (code >= 0 && code < 32) {
            controller->status.alarms &= ~(1 << code);
        }
    } else {
        json_decref(body);
        send_error_response(c, 400, "Missing parameters", 4002);
        return;
    }
    
    json_decref(body);
    send_success_response(c, "Alarms acknowledged", NULL);
}

/* History API */
void api_history(struct mg_connection *c, void *user_data) {
    /* Parse query parameters */
    struct http_message *hm = (struct http_message *)c->data;
    struct mg_str *qs = &hm->query_string;
    
    time_t start_time = time(NULL) - 86400; /* Last 24 hours */
    time_t end_time = time(NULL);
    const char *metric = "all";
    const char *aggregation = "hour";
    
    if (qs->len > 0) {
        /* Parse query string */
        char query[256];
        strncpy(query, qs->p, qs->len);
        query[qs->len] = '\0';
        
        char *token = strtok(query, "&");
        while (token) {
            if (strncmp(token, "start=", 6) == 0) {
                start_time = atol(token + 6);
            } else if (strncmp(token, "end=", 4) == 0) {
                end_time = atol(token + 4);
            } else if (strncmp(token, "metric=", 7) == 0) {
                metric = token + 7;
            } else if (strncmp(token, "aggregation=", 12) == 0) {
                aggregation = token + 12;
            }
            token = strtok(NULL, "&");
        }
    }
    
    json_t *response = json_object();
    json_object_set_new(response, "start_time", json_integer(start_time));
    json_object_set_new(response, "end_time", json_integer(end_time));
    json_object_set_new(response, "metric", json_string(metric));
    json_object_set_new(response, "aggregation", json_string(aggregation));
    
    /* TODO: Add actual historical data */
    json_t *data = json_array();
    json_object_set_new(response, "data", data);
    
    send_json_response(c, 200, response);
    json_decref(response);
}

/* Export Data API */
void api_export_data(struct mg_connection *c, void *user_data) {
    system_controller_t *controller = (system_controller_t *)user_data;
    struct http_message *hm = (struct http_message *)c->data;
    struct mg_str *qs = &hm->query_string;
    
    const char *format = "json";
    const char *type = "all";
    
    if (qs->len > 0) {
        char query[256];
        strncpy(query, qs->p, qs->len);
        query[qs->len] = '\0';
        
        char *token = strtok(query, "&");
        while (token) {
            if (strncmp(token, "format=", 7) == 0) {
                format = token + 7;
            } else if (strncmp(token, "type=", 5) == 0) {
                type = token + 5;
            }
            token = strtok(NULL, "&");
        }
    }
    
    if (strcmp(format, "csv") == 0) {
        /* Create CSV response */
        const char *csv = "timestamp,grid_power,pv_power,battery_power,load_power\n"
                          "0,0,0,0,0\n";
        
        mg_printf(c, "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/csv\r\n"
                  "Content-Disposition: attachment; filename=\"energy_data.csv\"\r\n"
                  "Content-Length: %lu\r\n"
                  "Connection: close\r\n"
                  "\r\n%s",
                  strlen(csv), csv);
    } else {
        /* JSON export */
        json_t *export_data = json_object();
        json_object_set_new(export_data, "export_timestamp", json_integer(time(NULL)));
        json_object_set_new(export_data, "system_name", json_string(controller->name));
        
        json_t *stats = json_object();
        json_object_set_new(stats, "pv_energy_total", 
                           json_real(controller->statistics.pv_energy_total));
        json_object_set_new(stats, "grid_import_total", 
                           json_real(controller->statistics.grid_import_total));
        json_object_set_new(stats, "grid_export_total", 
                           json_real(controller->statistics.grid_export_total));
        
        json_object_set_new(export_data, "statistics", stats);
        
        send_json_response(c, 200, export_data);
        json_decref(export_data);
    }
}

/* Login API */
void api_login(struct mg_connection *c, void *user_data) {
    webserver_t *server = (webserver_t *)c->user_data;
    json_t *body = webserver_get_json_body(c);
    
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    const char *username = NULL;
    const char *password = NULL;
    
    json_t *username_json = json_object_get(body, "username");
    json_t *password_json = json_object_get(body, "password");
    
    if (username_json) username = json_string_value(username_json);
    if (password_json) password = json_string_value(password_json);
    
    if (!username || !password) {
        json_decref(body);
        send_error_response(c, 400, "Missing username or password", 4002);
        return;
    }
    
    /* Simple authentication */
    user_role_t role = ROLE_GUEST;
    bool authenticated = false;
    
    if (strcmp(username, "admin") == 0) {
        if (server->config.admin_password_hash) {
            char *password_hash = webserver_hash_password(password);
            if (password_hash && strcmp(password_hash, server->config.admin_password_hash) == 0) {
                authenticated = true;
                role = ROLE_ADMIN;
            }
            free(password_hash);
        } else {
            /* Default admin password */
            if (strcmp(password, "admin123") == 0) {
                authenticated = true;
                role = ROLE_ADMIN;
            }
        }
    } else if (strcmp(username, "operator") == 0) {
        if (strcmp(password, "operator123") == 0) {
            authenticated = true;
            role = ROLE_OPERATOR;
        }
    } else if (strcmp(username, "viewer") == 0) {
        if (strcmp(password, "viewer123") == 0) {
            authenticated = true;
            role = ROLE_VIEWER;
        }
    }
    
    json_decref(body);
    
    if (authenticated) {
        /* Create session */
        user_session_t *session = webserver_create_session(server, username, 
                                                          role, c->remote_ip);
        if (session) {
            json_t *response = json_object();
            json_object_set_new(response, "success", json_boolean(true));
            json_object_set_new(response, "message", json_string("Login successful"));
            json_object_set_new(response, "session_id", 
                              json_string(session->session_id));
            json_object_set_new(response, "username", json_string(username));
            json_object_set_new(response, "role", json_integer(role));
            json_object_set_new(response, "expires_in", 
                              json_integer(server->config.session_timeout));
            
            char *json_str = json_dumps(response, JSON_INDENT(2));
            
            mg_printf(c, "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Set-Cookie: session_id=%s; Path=/; HttpOnly; Max-Age=%d\r\n"
                      "Content-Length: %lu\r\n"
                      "Connection: close\r\n"
                      "\r\n%s",
                      session->session_id, server->config.session_timeout,
                      strlen(json_str), json_str);
            
            free(json_str);
            json_decref(response);
        } else {
            send_error_response(c, 500, "Failed to create session", 5009);
        }
    } else {
        send_error_response(c, 401, "Invalid username or password", 4011);
    }
}

/* Logout API */
void api_logout(struct mg_connection *c, void *user_data) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    /* Get session ID from cookie or Authorization header */
    const char *session_id = NULL;
    const char *auth_header = mg_get_header(c, "Authorization");
    
    if (auth_header && strncmp(auth_header, "Bearer ", 7) == 0) {
        session_id = auth_header + 7;
    }
    
    if (!session_id) {
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
    
    if (session_id) {
        webserver_destroy_session(server, session_id);
    }
    
    send_success_response(c, "Logout successful", NULL);
}

/* User Info API */
void api_user_info(struct mg_connection *c, void *user_data) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    if (!is_authenticated(server, c)) {
        send_error_response(c, 401, "Authentication required", 1001);
        return;
    }
    
    user_role_t role = get_user_role(server, c);
    
    /* Find user session */
    const char *session_id = NULL;
    const char *auth_header = mg_get_header(c, "Authorization");
    
    if (auth_header && strncmp(auth_header, "Bearer ", 7) == 0) {
        session_id = auth_header + 7;
    }
    
    if (!session_id) return;
    
    for (int i = 0; i < server->session_count; i++) {
        if (server->sessions[i].valid &&
            strcmp(server->sessions[i].session_id, session_id) == 0) {
            
            json_t *user_info = json_object();
            json_object_set_new(user_info, "username", 
                              json_string(server->sessions[i].username));
            json_object_set_new(user_info, "role", 
                              json_integer(server->sessions[i].role));
            json_object_set_new(user_info, "ip_address", 
                              json_string(server->sessions[i].ip_address));
            json_object_set_new(user_info, "session_created", 
                              json_integer(server->sessions[i].created));
            
            send_success_response(c, "User information retrieved", user_info);
            return;
        }
    }
    
    send_error_response(c, 404, "User not found", 4041);
}

/* Create API Key API */
void api_create_apikey(struct mg_connection *c, void *user_data) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    if (get_user_role(server, c) < ROLE_ADMIN) {
        send_error_response(c, 403, "Insufficient privileges", 1002);
        return;
    }
    
    json_t *body = webserver_get_json_body(c);
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    const char *name = NULL;
    int role = ROLE_VIEWER;
    
    json_t *name_json = json_object_get(body, "name");
    json_t *role_json = json_object_get(body, "role");
    
    if (name_json) name = json_string_value(name_json);
    if (role_json) role = json_integer_value(role_json);
    
    if (!name) {
        json_decref(body);
        send_error_response(c, 400, "Missing name parameter", 4002);
        return;
    }
    
    if (role < ROLE_VIEWER || role > ROLE_SUPERUSER) {
        json_decref(body);
        send_error_response(c, 400, "Invalid role", 4003);
        return;
    }
    
    api_key_t *api_key = webserver_create_api_key(server, name, (user_role_t)role);
    json_decref(body);
    
    if (api_key) {
        json_t *key_info = json_object();
        json_object_set_new(key_info, "name", json_string(api_key->name));
        json_object_set_new(key_info, "key", json_string(api_key->key));
        json_object_set_new(key_info, "role", json_integer(api_key->role));
        json_object_set_new(key_info, "created", json_integer(api_key->created));
        
        send_success_response(c, "API key created", key_info);
    } else {
        send_error_response(c, 500, "Failed to create API key", 5009);
    }
}

/* Revoke API Key API */
void api_revoke_apikey(struct mg_connection *c, void *user_data) {
    webserver_t *server = (webserver_t *)c->user_data;
    
    if (get_user_role(server, c) < ROLE_ADMIN) {
        send_error_response(c, 403, "Insufficient privileges", 1002);
        return;
    }
    
    json_t *body = webserver_get_json_body(c);
    if (!body) {
        send_error_response(c, 400, "Invalid JSON body", 4001);
        return;
    }
    
    const char *api_key = NULL;
    json_t *key_json = json_object_get(body, "api_key");
    
    if (key_json) api_key = json_string_value(key_json);
    
    if (!api_key) {
        json_decref(body);
        send_error_response(c, 400, "Missing api_key parameter", 4002);
        return;
    }
    
    bool found = false;
    for (int i = 0; i < server->api_key_count; i++) {
        if (strcmp(server->api_keys[i].key, api_key) == 0) {
            server->api_keys[i].enabled = false;
            found = true;
            break;
        }
    }
    
    json_decref(body);
    
    if (found) {
        send_success_response(c, "API key revoked", NULL);
    } else {
        send_error_response(c, 404, "API key not found", 4042);
    }
}

/* Helper: Create system status JSON */
static json_t* create_system_status_json(system_controller_t *controller) {
    json_t *root = json_object();
    
    /* Measurements */
    json_t *measurements = json_object();
    json_object_set_new(measurements, "grid_power", 
                       json_real(controller->measurements.grid_power));
    json_object_set_new(measurements, "grid_voltage", 
                       json_real(controller->measurements.grid_voltage));
    json_object_set_new(measurements, "grid_frequency", 
                       json_real(controller->measurements.grid_frequency));
    json_object_set_new(measurements, "pv_power_total", 
                       json_real(controller->measurements.pv_power_total));
    json_object_set_new(measurements, "battery_power", 
                       json_real(controller->measurements.battery_power));
    json_object_set_new(measurements, "battery_soc", 
                       json_real(controller->measurements.battery_soc));
    json_object_set_new(measurements, "load_power_total", 
                       json_real(controller->measurements.load_power_total));
    json_object_set_new(measurements, "timestamp", 
                       json_integer(controller->measurements.timestamp));
    
    /* Status */
    json_t *status = json_object();
    json_object_set_new(status, "mode", 
                       json_integer(controller->status.mode));
    json_object_set_new(status, "grid_available", 
                       json_boolean(controller->status.grid_available));
    json_object_set_new(status, "grid_stable", 
                       json_boolean(controller->status.grid_stable));
    json_object_set_new(status, "battery_available", 
                       json_boolean(controller->status.battery_available));
    json_object_set_new(status, "pv_available", 
                       json_boolean(controller->status.pv_available));
    json_object_set_new(status, "alarms", 
                       json_integer(controller->status.alarms));
    json_object_set_new(status, "warnings", 
                       json_integer(controller->status.warnings));
    
    json_object_set_new(root, "measurements", measurements);
    json_object_set_new(root, "status", status);
    
    return root;
}

/* Helper: Create PV status JSON */
static json_t* create_pv_status_json(system_controller_t *controller) {
    pv_system_t *pv = &controller->pv_system;
    json_t *root = json_object();
    
    json_object_set_new(root, "state", json_integer(pv->state));
    json_object_set_new(root, "active_string_count", 
                       json_integer(pv->active_string_count));
    json_object_set_new(root, "total_capacity", 
                       json_real(pv->total_capacity));
    json_object_set_new(root, "available_power", 
                       json_real(pv->available_power));
    json_object_set_new(root, "daily_energy", 
                       json_real(pv->daily_energy));
    json_object_set_new(root, "total_energy", 
                       json_real(pv->total_energy));
    
    return root;
}

/* Helper: Create battery status JSON */
static json_t* create_battery_status_json(system_controller_t *controller) {
    battery_system_t *bat = &controller->battery_system;
    json_t *root = json_object();
    
    json_object_set_new(root, "state", json_integer(bat->state));
    json_object_set_new(root, "soc_estimated", 
                       json_real(bat->soc_estimated));
    json_object_set_new(root, "capacity_remaining", 
                       json_real(bat->capacity_remaining));
    json_object_set_new(root, "capacity_nominal", 
                       json_real(bat->capacity_nominal));
    json_object_set_new(root, "health_percentage", 
                       json_real(bat->health_percentage));
    json_object_set_new(root, "temperature", 
                       json_real(bat->temperature));
    
    return root;
}

/* Helper: Create loads status JSON */
static json_t* create_loads_status_json(system_controller_t *controller) {
    load_manager_t *lm = &controller->load_manager;
    json_t *root = json_object();
    
    json_object_set_new(root, "load_count", json_integer(lm->load_count));
    json_object_set_new(root, "shedding_active", 
                       json_boolean(lm->shedding_active));
    json_object_set_new(root, "deferred_power", 
                       json_real(lm->deferred_power));
    
    /* Load list */
    json_t *loads = json_array();
    for (int i = 0; i < lm->load_count; i++) {
        json_t *load = json_object();
        json_object_set_new(load, "id", json_string(lm->loads[i].id));
        json_object_set_new(load, "rated_power", 
                           json_real(lm->loads[i].rated_power));
        json_object_set_new(load, "priority", 
                           json_integer(lm->loads[i].priority));
        json_object_set_new(load, "current_state", 
                           json_boolean(lm->load_states[i] == LOAD_STATE_ON));
        
        json_array_append_new(loads, load);
    }
    json_object_set_new(root, "loads", loads);
    
    return root;
}

/* Helper: Create agriculture status JSON */
static json_t* create_agriculture_status_json(system_controller_t *controller) {
    agriculture_system_t *ag = &controller->agriculture_system;
    json_t *root = json_object();
    
    json_object_set_new(root, "mode", json_integer(ag->mode));
    json_object_set_new(root, "zone_count", 
                       json_integer(ag->zone_count));
    json_object_set_new(root, "total_water_used", 
                       json_real(ag->total_water_used));
    json_object_set_new(root, "daily_water_used", 
                       json_real(ag->daily_water_used));
    json_object_set_new(root, "daily_energy_used", 
                       json_real(ag->daily_energy_used));
    
    return root;
}

/* Helper: Create EV status JSON */
static json_t* create_ev_status_json(system_controller_t *controller) {
    ev_charging_system_t *ev = &controller->ev_system;
    json_t *root = json_object();
    
    json_object_set_new(root, "charger_count", 
                       json_integer(ev->charger_count));
    json_object_set_new(root, "current_total_power", 
                       json_real(ev->current_total_power));
    json_object_set_new(root, "total_energy_delivered", 
                       json_real(ev->total_energy_delivered));
    json_object_set_new(root, "daily_energy_delivered", 
                       json_real(ev->daily_energy_delivered));
    
    return root;
}

/* Helper: Create alarms JSON */
static json_t* create_alarms_json(system_controller_t *controller) {
    json_t *root = json_object();
    
    /* Active alarms */
    json_t *alarms = json_array();
    uint8_t active_alarms = controller->status.alarms;
    
    for (int i = 0; i < 32; i++) {
        if (active_alarms & (1 << i)) {
            json_t *alarm = json_object();
            json_object_set_new(alarm, "code", json_integer(i));
            
            const char *description = "Unknown alarm";
            switch (i) {
                case ALARM_GRID_FAILURE: description = "Grid failure"; break;
                case ALARM_BATTERY_OVER_TEMP: description = "Battery over temperature"; break;
                case ALARM_BATTERY_LOW_SOC: description = "Battery low SOC"; break;
                case ALARM_PV_DISCONNECT: description = "PV disconnect"; break;
                case ALARM_OVERLOAD: description = "System overload"; break;
            }
            
            json_object_set_new(alarm, "description", json_string(description));
            json_object_set_new(alarm, "timestamp", json_integer(time(NULL)));
            
            json_array_append_new(alarms, alarm);
        }
    }
    json_object_set_new(root, "active_alarms", alarms);
    
    /* Active warnings */
    json_t *warnings = json_array();
    uint8_t active_warnings = controller->status.warnings;
    
    for (int i = 0; i < 32; i++) {
        if (active_warnings & (1 << i)) {
            json_t *warning = json_object();
            json_object_set_new(warning, "code", json_integer(i));
            
            const char *description = "Unknown warning";
            switch (i) {
                case WARNING_BATTERY_HIGH_TEMP: description = "Battery high temperature"; break;
                case WARNING_BATTERY_MID_SOC: description = "Battery medium SOC"; break;
                case WARNING_PV_LOW_PRODUCTION: description = "PV low production"; break;
                case WARNING_GRID_UNSTABLE: description = "Grid unstable"; break;
            }
            
            json_object_set_new(warning, "description", json_string(description));
            json_object_set_new(warning, "timestamp", json_integer(time(NULL)));
            
            json_array_append_new(warnings, warning);
        }
    }
    json_object_set_new(root, "active_warnings", warnings);
    
    return root;
}

/* Helper: Create system stats JSON */
static json_t* create_system_stats_json(system_controller_t *controller) {
    system_statistics_t *stats = &controller->statistics;
    json_t *root = json_object();
    
    json_object_set_new(root, "pv_energy_total", 
                       json_real(stats->pv_energy_total));
    json_object_set_new(root, "grid_import_total", 
                       json_real(stats->grid_import_total));
    json_object_set_new(root, "grid_export_total", 
                       json_real(stats->grid_export_total));
    json_object_set_new(root, "battery_charge_total", 
                       json_real(stats->battery_charge_total));
    json_object_set_new(root, "battery_discharge_total", 
                       json_real(stats->battery_discharge_total));
    json_object_set_new(root, "load_energy_total", 
                       json_real(stats->load_energy_total));
    
    json_object_set_new(root, "grid_outage_count", 
                       json_integer(stats->grid_outage_count));
    json_object_set_new(root, "load_shed_count", 
                       json_integer(stats->load_shed_count));
    
    json_object_set_new(root, "stats_start_time", 
                       json_integer(stats->stats_start_time));
    
    return root;
}