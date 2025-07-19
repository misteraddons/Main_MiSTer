/*
 * MiSTer Network Daemon
 * 
 * HTTP REST API server for remote game launching
 * Another input source for the modular game launcher system
 * 
 * Features:
 * - HTTP REST API for game launching
 * - JSON request/response format
 * - Integration with game_launcher service
 * - Status monitoring and health checks
 * - CORS support for web interfaces
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define ANNOUNCEMENT_FIFO "/dev/MiSTer_announcements"
#define CONFIG_FILE "/media/fat/utils/network_daemon.conf"
#define PID_FILE "/tmp/network_daemon.pid"
#define DEFAULT_PORT 8080
#define MAX_REQUEST_SIZE 4096
#define MAX_RESPONSE_SIZE 2048

// Configuration
typedef struct {
    int port;
    bool enable_cors;
    bool show_notifications;
    bool forward_announcements;
    char allowed_origins[512];
    char api_key[128];
    bool require_auth;
} network_config_t;

// HTTP Request structure
typedef struct {
    char method[16];
    char path[256];
    char query[512];
    char headers[1024];
    char body[2048];
    size_t content_length;
} http_request_t;

// HTTP Response structure  
typedef struct {
    int status_code;
    char status_text[64];
    char headers[512];
    char body[2048];
} http_response_t;

// Global variables
static volatile int keep_running = 1;
static network_config_t config;
static int server_socket = -1;
static int announcement_fd = -1;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
    if (server_socket >= 0) {
        close(server_socket);
    }
}

// Initialize default configuration
void init_config_defaults() {
    config.port = DEFAULT_PORT;
    config.enable_cors = true;
    config.show_notifications = true;
    config.forward_announcements = true;
    strcpy(config.allowed_origins, "*");
    config.api_key[0] = '\0';
    config.require_auth = false;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("network_daemon: Using default configuration\n");
        return;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char* equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char* key = line;
        char* value = equals + 1;
        
        // Trim whitespace
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        char* newline = strchr(value, '\n');
        if (newline) *newline = '\0';
        
        if (strcmp(key, "port") == 0) {
            config.port = atoi(value);
        } else if (strcmp(key, "enable_cors") == 0) {
            config.enable_cors = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "show_notifications") == 0) {
            config.show_notifications = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "forward_announcements") == 0) {
            config.forward_announcements = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "allowed_origins") == 0) {
            strncpy(config.allowed_origins, value, sizeof(config.allowed_origins) - 1);
        } else if (strcmp(key, "api_key") == 0) {
            strncpy(config.api_key, value, sizeof(config.api_key) - 1);
            config.require_auth = (strlen(config.api_key) > 0);
        }
    }
    
    fclose(fp);
    printf("network_daemon: Configuration loaded (port: %d)\n", config.port);
}

// Send OSD message
void send_osd_message(const char* message) {
    if (!config.show_notifications) return;
    
    int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "osd_message %s", message);
        write(fd, cmd, strlen(cmd));
        close(fd);
    }
}

// Send command to game launcher
bool send_game_launcher_command(const char* system, const char* id_type, const char* identifier) {
    int fd = open(GAME_LAUNCHER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    char command[512];
    snprintf(command, sizeof(command), "%s:%s:%s:network", system, id_type, identifier);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    return written > 0;
}

// URL decode
void url_decode(char* dst, const char* src) {
    char* p = dst;
    char code[3];
    
    while (*src) {
        if (*src == '%') {
            memcpy(code, src + 1, 2);
            code[2] = '\0';
            *p++ = (char)strtol(code, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *p++ = ' ';
            src++;
        } else {
            *p++ = *src++;
        }
    }
    *p = '\0';
}

// Parse query parameters
bool get_query_param(const char* query, const char* param, char* value, size_t value_size) {
    char param_pattern[128];
    snprintf(param_pattern, sizeof(param_pattern), "%s=", param);
    
    const char* start = strstr(query, param_pattern);
    if (!start) return false;
    
    start += strlen(param_pattern);
    const char* end = strchr(start, '&');
    if (!end) end = start + strlen(start);
    
    size_t len = end - start;
    if (len >= value_size) len = value_size - 1;
    
    memcpy(value, start, len);
    value[len] = '\0';
    url_decode(value, value);
    
    return true;
}

// Parse JSON body for game launch request
bool parse_launch_request(const char* json, char* core, char* id_type, char* identifier) {
    // Simple JSON parser for our specific format
    // Expected: {"core": "PSX", "id_type": "serial", "identifier": "SLUS-00067"}
    
    const char* core_start = strstr(json, "\"core\"");
    const char* id_type_start = strstr(json, "\"id_type\"");
    const char* identifier_start = strstr(json, "\"identifier\"");
    
    if (!core_start || !id_type_start || !identifier_start) {
        return false;
    }
    
    // Extract core
    core_start = strchr(core_start, ':');
    if (!core_start) return false;
    core_start = strchr(core_start, '"');
    if (!core_start) return false;
    core_start++;
    const char* core_end = strchr(core_start, '"');
    if (!core_end) return false;
    
    size_t core_len = core_end - core_start;
    if (core_len >= 16) core_len = 15;
    memcpy(core, core_start, core_len);
    core[core_len] = '\0';
    
    // Extract id_type
    id_type_start = strchr(id_type_start, ':');
    if (!id_type_start) return false;
    id_type_start = strchr(id_type_start, '"');
    if (!id_type_start) return false;
    id_type_start++;
    const char* id_type_end = strchr(id_type_start, '"');
    if (!id_type_end) return false;
    
    size_t id_type_len = id_type_end - id_type_start;
    if (id_type_len >= 16) id_type_len = 15;
    memcpy(id_type, id_type_start, id_type_len);
    id_type[id_type_len] = '\0';
    
    // Extract identifier
    identifier_start = strchr(identifier_start, ':');
    if (!identifier_start) return false;
    identifier_start = strchr(identifier_start, '"');
    if (!identifier_start) return false;
    identifier_start++;
    const char* identifier_end = strchr(identifier_start, '"');
    if (!identifier_end) return false;
    
    size_t identifier_len = identifier_end - identifier_start;
    if (identifier_len >= 64) identifier_len = 63;
    memcpy(identifier, identifier_start, identifier_len);
    identifier[identifier_len] = '\0';
    
    return true;
}

// Check authentication
bool check_auth(const http_request_t* request) {
    if (!config.require_auth) return true;
    
    // Look for Authorization header or api_key parameter
    char auth_header[256];
    if (strstr(request->headers, "Authorization:")) {
        const char* auth_start = strstr(request->headers, "Authorization:");
        auth_start += 14;
        while (*auth_start == ' ') auth_start++;
        
        const char* auth_end = strchr(auth_start, '\n');
        if (!auth_end) auth_end = auth_start + strlen(auth_start);
        
        size_t auth_len = auth_end - auth_start;
        if (auth_len >= sizeof(auth_header)) auth_len = sizeof(auth_header) - 1;
        memcpy(auth_header, auth_start, auth_len);
        auth_header[auth_len] = '\0';
        
        // Remove carriage return
        char* cr = strchr(auth_header, '\r');
        if (cr) *cr = '\0';
        
        if (strcmp(auth_header, config.api_key) == 0) {
            return true;
        }
    }
    
    // Check query parameter
    char api_key_param[128];
    if (get_query_param(request->query, "api_key", api_key_param, sizeof(api_key_param))) {
        if (strcmp(api_key_param, config.api_key) == 0) {
            return true;
        }
    }
    
    return false;
}

// Build HTTP response
void build_response(http_response_t* response, int status, const char* body) {
    response->status_code = status;
    
    switch (status) {
        case 200: strcpy(response->status_text, "OK"); break;
        case 400: strcpy(response->status_text, "Bad Request"); break;
        case 401: strcpy(response->status_text, "Unauthorized"); break;
        case 404: strcpy(response->status_text, "Not Found"); break;
        case 405: strcpy(response->status_text, "Method Not Allowed"); break;
        case 500: strcpy(response->status_text, "Internal Server Error"); break;
        default: strcpy(response->status_text, "Unknown"); break;
    }
    
    // Build headers
    snprintf(response->headers, sizeof(response->headers),
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Server: MiSTer-Network-Daemon/1.0\r\n"
        "%s"
        "\r\n",
        strlen(body),
        config.enable_cors ? "Access-Control-Allow-Origin: *\r\n"
                           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                           "Access-Control-Allow-Headers: Content-Type, Authorization\r\n" : ""
    );
    
    strncpy(response->body, body, sizeof(response->body) - 1);
    response->body[sizeof(response->body) - 1] = '\0';
}

// Handle OPTIONS request (CORS preflight)
void handle_options(http_response_t* response) {
    build_response(response, 200, "{}");
}

// Handle GET /status
void handle_status(http_response_t* response) {
    // Check if game launcher service is available
    bool game_launcher_available = (access(GAME_LAUNCHER_FIFO, F_OK) == 0);
    
    char status_json[512];
    snprintf(status_json, sizeof(status_json),
        "{"
        "\"status\": \"running\","
        "\"game_launcher_available\": %s,"
        "\"port\": %d,"
        "\"cors_enabled\": %s,"
        "\"auth_required\": %s,"
        "\"timestamp\": %ld"
        "}",
        game_launcher_available ? "true" : "false",
        config.port,
        config.enable_cors ? "true" : "false",
        config.require_auth ? "true" : "false",
        time(NULL)
    );
    
    build_response(response, 200, status_json);
}

// Handle POST /launch
void handle_launch(const http_request_t* request, http_response_t* response) {
    char core[16], id_type[16], identifier[64];
    
    // Parse JSON body
    if (!parse_launch_request(request->body, core, id_type, identifier)) {
        build_response(response, 400, 
            "{\"error\": \"Invalid JSON format\", \"expected\": \"{\\\"core\\\": \\\"PSX\\\", \\\"id_type\\\": \\\"serial\\\", \\\"identifier\\\": \\\"SLUS-00067\\\"}\"}");
        return;
    }
    
    printf("network_daemon: Launch request - Core: %s, ID Type: %s, Identifier: %s\n",
           core, id_type, identifier);
    
    // Send to game launcher service
    if (send_game_launcher_command(core, id_type, identifier)) {
        char success_json[256];
        snprintf(success_json, sizeof(success_json),
            "{"
            "\"success\": true,"
            "\"message\": \"Game launch request sent\","
            "\"core\": \"%s\","
            "\"id_type\": \"%s\","
            "\"identifier\": \"%s\""
            "}",
            core, id_type, identifier
        );
        
        build_response(response, 200, success_json);
        
        // Send OSD notification
        char osd_msg[128];
        snprintf(osd_msg, sizeof(osd_msg), "Network: Loading %s game", core);
        send_osd_message(osd_msg);
        
    } else {
        build_response(response, 500,
            "{\"error\": \"Failed to communicate with game launcher service\"}");
    }
}

// Handle GET /api
void handle_api_info(http_response_t* response) {
    char api_json[1024];
    snprintf(api_json, sizeof(api_json),
        "{"
        "\"name\": \"MiSTer Network Game Launcher API\","
        "\"version\": \"1.0\","
        "\"endpoints\": {"
        "\"GET /status\": \"Get system status\","
        "\"POST /launch\": \"Launch a game\","
        "\"GET /api\": \"Get API information\""
        "},"
        "\"launch_format\": {"
        "\"core\": \"Core name (PSX, Saturn, MegaCD, etc.)\","
        "\"id_type\": \"serial or title\","
        "\"identifier\": \"Game serial number or title\""
        "},"
        "\"example\": {"
        "\"core\": \"PSX\","
        "\"id_type\": \"serial\","
        "\"identifier\": \"SLUS-00067\""
        "}"
        "}"
    );
    
    build_response(response, 200, api_json);
}

// Parse HTTP request
bool parse_http_request(const char* data, http_request_t* request) {
    memset(request, 0, sizeof(http_request_t));
    
    // Parse request line
    const char* line_end = strstr(data, "\r\n");
    if (!line_end) return false;
    
    char request_line[256];
    size_t line_len = line_end - data;
    if (line_len >= sizeof(request_line)) line_len = sizeof(request_line) - 1;
    memcpy(request_line, data, line_len);
    request_line[line_len] = '\0';
    
    // Parse method, path, and query
    char* method = strtok(request_line, " ");
    char* uri = strtok(NULL, " ");
    
    if (!method || !uri) return false;
    
    strncpy(request->method, method, sizeof(request->method) - 1);
    
    // Split path and query
    char* query_start = strchr(uri, '?');
    if (query_start) {
        *query_start = '\0';
        strncpy(request->query, query_start + 1, sizeof(request->query) - 1);
    }
    strncpy(request->path, uri, sizeof(request->path) - 1);
    
    // Parse headers
    const char* headers_start = line_end + 2;
    const char* body_start = strstr(headers_start, "\r\n\r\n");
    if (body_start) {
        size_t headers_len = body_start - headers_start;
        if (headers_len >= sizeof(request->headers)) headers_len = sizeof(request->headers) - 1;
        memcpy(request->headers, headers_start, headers_len);
        request->headers[headers_len] = '\0';
        
        // Parse body
        body_start += 4;
        strncpy(request->body, body_start, sizeof(request->body) - 1);
        request->content_length = strlen(request->body);
    }
    
    return true;
}

// Process HTTP request
void process_request(const http_request_t* request, http_response_t* response) {
    // Check authentication
    if (!check_auth(request)) {
        build_response(response, 401, "{\"error\": \"Authentication required\"}");
        return;
    }
    
    // Handle OPTIONS (CORS preflight)
    if (strcmp(request->method, "OPTIONS") == 0) {
        handle_options(response);
        return;
    }
    
    // Route requests
    if (strcmp(request->method, "GET") == 0) {
        if (strcmp(request->path, "/status") == 0) {
            handle_status(response);
        } else if (strcmp(request->path, "/api") == 0 || strcmp(request->path, "/") == 0) {
            handle_api_info(response);
        } else {
            build_response(response, 404, "{\"error\": \"Endpoint not found\"}");
        }
    } else if (strcmp(request->method, "POST") == 0) {
        if (strcmp(request->path, "/launch") == 0) {
            handle_launch(request, response);
        } else {
            build_response(response, 404, "{\"error\": \"Endpoint not found\"}");
        }
    } else {
        build_response(response, 405, "{\"error\": \"Method not allowed\"}");
    }
}

// Handle client connection
void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);
    
    char buffer[MAX_REQUEST_SIZE];
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        
        http_request_t request;
        http_response_t response;
        
        if (parse_http_request(buffer, &request)) {
            process_request(&request, &response);
        } else {
            build_response(&response, 400, "{\"error\": \"Invalid HTTP request\"}");
        }
        
        // Send response
        char response_buffer[MAX_RESPONSE_SIZE];
        snprintf(response_buffer, sizeof(response_buffer),
            "HTTP/1.1 %d %s\r\n%s%s",
            response.status_code, response.status_text,
            response.headers, response.body
        );
        
        send(client_socket, response_buffer, strlen(response_buffer), 0);
    }
    
    close(client_socket);
    return NULL;
}

// Write PID file
void write_pid_file() {
    FILE* fp = fopen(PID_FILE, "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
}

// Main function
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe
    
    printf("network_daemon: Starting Network Game Launcher Daemon\n");
    
    // Load configuration
    load_config();
    
    // Check if game launcher service is available
    if (access(GAME_LAUNCHER_FIFO, F_OK) != 0) {
        printf("network_daemon: Warning - Game launcher service not available\n");
        printf("network_daemon: Please start /media/fat/utils/game_launcher first\n");
    }
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config.port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        return 1;
    }
    
    // Listen
    if (listen(server_socket, 10) < 0) {
        perror("listen");
        close(server_socket);
        return 1;
    }
    
    bool foreground = (argc > 1 && strcmp(argv[1], "-f") == 0);
    
    if (!foreground) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid > 0) {
            exit(0);
        }
        
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    // Write PID file
    write_pid_file();
    
    printf("network_daemon: HTTP server listening on port %d\n", config.port);
    printf("network_daemon: Game launcher API ready\n");
    
    // Main server loop
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (keep_running) {
                perror("accept");
            }
            continue;
        }
        
        // Handle client in separate thread
        int* client_socket_ptr = (int*)malloc(sizeof(int));
        *client_socket_ptr = client_socket;
        
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, client_socket_ptr) != 0) {
            perror("pthread_create");
            close(client_socket);
            free(client_socket_ptr);
            continue;
        }
        
        // Detach thread so it cleans up automatically
        pthread_detach(client_thread);
    }
    
    // Cleanup
    printf("network_daemon: Shutting down\n");
    if (server_socket >= 0) {
        close(server_socket);
    }
    unlink(PID_FILE);
    
    return 0;
}