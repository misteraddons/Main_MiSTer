/*
 * Network Game Launcher Daemon
 * 
 * HTTP REST API for remote game launching
 * Allows mobile apps, web interfaces, and other network clients to launch games
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <json-c/json.h>

#define SERVER_PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 4096

static volatile int keep_running = 1;
static int server_socket = -1;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
    if (server_socket >= 0) {
        close(server_socket);
    }
}

// Send HTTP response
void send_http_response(int client_socket, int status_code, const char* content_type, const char* body) {
    char response[BUFFER_SIZE];
    int body_len = body ? strlen(body) : 0;
    
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             "Access-Control-Allow-Headers: Content-Type\r\n"
             "\r\n"
             "%s",
             status_code, 
             status_code == 200 ? "OK" : "Error",
             content_type,
             body_len,
             body ? body : "");
    
    send(client_socket, response, strlen(response), 0);
}

// Send game launch command
bool send_game_launch_command(const char* system, const char* id_type, const char* identifier, const char* client_ip) {
    char command[512];
    snprintf(command, sizeof(command), 
             "{"
             "\"command\": \"find_game\", "
             "\"system\": \"%s\", "
             "\"id_type\": \"%s\", "
             "\"identifier\": \"%s\", "
             "\"source\": \"network\", "
             "\"auto_launch\": true, "
             "\"source_data\": {\"client_ip\": \"%s\"}"
             "}",
             system, id_type, identifier, client_ip);
    
    int fd = open("/dev/MiSTer_game_launcher", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    ssize_t written = write(fd, command, strlen(command));
    write(fd, "\n", 1);
    close(fd);
    
    return written > 0;
}

// Handle GET request
void handle_get_request(int client_socket, const char* path) {
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        // Serve simple web interface
        const char* html = 
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head><title>MiSTer Game Launcher</title></head>\n"
            "<body>\n"
            "<h1>MiSTer Game Launcher</h1>\n"
            "<form method='POST' action='/launch'>\n"
            "  <label>System:</label>\n"
            "  <select name='system'>\n"
            "    <option value='PSX'>PlayStation</option>\n"
            "    <option value='Saturn'>Sega Saturn</option>\n"
            "    <option value='MegaCD'>Sega CD</option>\n"
            "    <option value='PCECD'>PC Engine CD</option>\n"
            "  </select><br><br>\n"
            "  <label>Game Serial:</label>\n"
            "  <input type='text' name='serial' placeholder='SLUS-00067'><br><br>\n"
            "  <input type='submit' value='Launch Game'>\n"
            "</form>\n"
            "</body>\n"
            "</html>";
        
        send_http_response(client_socket, 200, "text/html", html);
        
    } else if (strcmp(path, "/status") == 0) {
        // Return service status
        const char* status = "{\"status\": \"running\", \"service\": \"network_daemon\"}";
        send_http_response(client_socket, 200, "application/json", status);
        
    } else {
        // 404 Not Found
        send_http_response(client_socket, 404, "text/plain", "Not Found");
    }
}

// Handle POST request
void handle_post_request(int client_socket, const char* path, const char* body, const char* client_ip) {
    if (strcmp(path, "/launch") == 0) {
        // Parse JSON body
        json_object* root = json_tokener_parse(body);
        if (!root) {
            send_http_response(client_socket, 400, "application/json", "{\"error\": \"Invalid JSON\"}");
            return;
        }
        
        json_object* system_obj, *id_type_obj, *identifier_obj;
        
        // Default values
        const char* system = "PSX";
        const char* id_type = "serial";
        const char* identifier = "";
        
        if (json_object_object_get_ex(root, "system", &system_obj)) {
            system = json_object_get_string(system_obj);
        }
        if (json_object_object_get_ex(root, "id_type", &id_type_obj)) {
            id_type = json_object_get_string(id_type_obj);
        }
        if (json_object_object_get_ex(root, "identifier", &identifier_obj)) {
            identifier = json_object_get_string(identifier_obj);
        }
        
        printf("network_daemon: Launch request from %s: %s %s %s\n", client_ip, system, id_type, identifier);
        
        // Send launch command
        if (send_game_launch_command(system, id_type, identifier, client_ip)) {
            char response[256];
            snprintf(response, sizeof(response), 
                     "{\"success\": true, \"message\": \"Launch command sent\", \"system\": \"%s\", \"identifier\": \"%s\"}",
                     system, identifier);
            send_http_response(client_socket, 200, "application/json", response);
        } else {
            send_http_response(client_socket, 500, "application/json", "{\"error\": \"Failed to send launch command\"}");
        }
        
        json_object_put(root);
        
    } else {
        send_http_response(client_socket, 404, "text/plain", "Not Found");
    }
}

// Handle client connection
void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        
        // Parse HTTP request
        char method[16], path[256], version[16];
        sscanf(buffer, "%15s %255s %15s", method, path, version);
        
        // Get client IP
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        getpeername(client_socket, (struct sockaddr*)&client_addr, &addr_len);
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        
        printf("network_daemon: %s %s from %s\n", method, path, client_ip);
        
        if (strcmp(method, "GET") == 0) {
            handle_get_request(client_socket, path);
        } else if (strcmp(method, "POST") == 0) {
            // Find request body
            char* body = strstr(buffer, "\r\n\r\n");
            if (body) {
                body += 4; // Skip past \r\n\r\n
                handle_post_request(client_socket, path, body, client_ip);
            } else {
                send_http_response(client_socket, 400, "text/plain", "Bad Request");
            }
        } else if (strcmp(method, "OPTIONS") == 0) {
            // Handle CORS preflight
            send_http_response(client_socket, 200, "text/plain", "");
        } else {
            send_http_response(client_socket, 405, "text/plain", "Method Not Allowed");
        }
    }
    
    close(client_socket);
    return NULL;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("network_daemon: Starting Network Game Launcher Daemon on port %d\n", SERVER_PORT);
    
    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("network_daemon: Failed to create socket");
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("network_daemon: Failed to bind socket");
        close(server_socket);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("network_daemon: Failed to listen");
        close(server_socket);
        return 1;
    }
    
    printf("network_daemon: Server listening on port %d\n", SERVER_PORT);
    printf("network_daemon: Web interface: http://mister-ip:8080/\n");
    printf("network_daemon: API endpoint: http://mister-ip:8080/launch\n");
    
    // Accept connections
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (keep_running) {
                perror("network_daemon: Accept failed");
            }
            continue;
        }
        
        // Handle client in separate thread
        int* client_socket_ptr = malloc(sizeof(int));
        *client_socket_ptr = client_socket;
        
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, client_socket_ptr) != 0) {
            perror("network_daemon: Failed to create client thread");
            close(client_socket);
            free(client_socket_ptr);
        } else {
            pthread_detach(client_thread);
        }
    }
    
    // Cleanup
    printf("network_daemon: Shutting down\n");
    close(server_socket);
    
    return 0;
}

/*
 * Example API usage:
 * 
 * Launch game by serial:
 * curl -X POST http://mister-ip:8080/launch \
 *   -H "Content-Type: application/json" \
 *   -d '{"system": "PSX", "id_type": "serial", "identifier": "SLUS-00067"}'
 * 
 * Launch game by title:
 * curl -X POST http://mister-ip:8080/launch \
 *   -H "Content-Type: application/json" \
 *   -d '{"system": "Saturn", "id_type": "title", "identifier": "Panzer Dragoon Saga"}'
 * 
 * Mobile app integration:
 * fetch('http://mister-ip:8080/launch', {
 *   method: 'POST',
 *   headers: {'Content-Type': 'application/json'},
 *   body: JSON.stringify({
 *     system: 'PSX',
 *     id_type: 'serial',
 *     identifier: 'SLUS-00067'
 *   })
 * })
 */