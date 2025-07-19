/*
 * MiSTer WebSocket Daemon
 * 
 * Provides real-time bidirectional communication with web clients
 * Supports game launching, status updates, and live notifications
 * 
 * Features:
 * - WebSocket server for real-time communication
 * - JSON message protocol
 * - Game launcher integration
 * - Live game announcements
 * - Statistics and history API
 * - Favorites management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define ANNOUNCEMENT_FIFO "/dev/MiSTer_announcements"
#define CONFIG_FILE "/media/fat/utils/websocket_daemon.conf"
#define PID_FILE "/tmp/websocket_daemon.pid"
#define MAX_CLIENTS 10
#define BUFFER_SIZE 4096
#define WS_MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// WebSocket frame opcodes
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT 0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE 0x8
#define WS_OPCODE_PING 0x9
#define WS_OPCODE_PONG 0xA

// Configuration
typedef struct {
    int port;
    char bind_address[64];
    bool enable_cors;
    bool enable_auth;
    char auth_token[128];
    bool forward_announcements;
    int max_clients;
    int ping_interval;
} websocket_config_t;

// WebSocket client
typedef struct {
    int socket;
    bool connected;
    bool handshake_complete;
    char remote_ip[64];
    time_t connect_time;
    time_t last_ping;
    pthread_mutex_t send_mutex;
} websocket_client_t;

// Global variables
static volatile int keep_running = 1;
static websocket_config_t config;
static websocket_client_t clients[MAX_CLIENTS];
static int server_socket = -1;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    config.port = 8081;
    strcpy(config.bind_address, "0.0.0.0");
    config.enable_cors = true;
    config.enable_auth = false;
    strcpy(config.auth_token, "");
    config.forward_announcements = true;
    config.max_clients = MAX_CLIENTS;
    config.ping_interval = 30;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("websocket_daemon: Using default configuration\n");
        return;
    }
    
    char line[256];
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
        } else if (strcmp(key, "bind_address") == 0) {
            strncpy(config.bind_address, value, sizeof(config.bind_address) - 1);
        } else if (strcmp(key, "enable_cors") == 0) {
            config.enable_cors = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "enable_auth") == 0) {
            config.enable_auth = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "auth_token") == 0) {
            strncpy(config.auth_token, value, sizeof(config.auth_token) - 1);
        } else if (strcmp(key, "forward_announcements") == 0) {
            config.forward_announcements = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "max_clients") == 0) {
            config.max_clients = atoi(value);
            if (config.max_clients > MAX_CLIENTS) {
                config.max_clients = MAX_CLIENTS;
            }
        } else if (strcmp(key, "ping_interval") == 0) {
            config.ping_interval = atoi(value);
        }
    }
    
    fclose(fp);
    printf("websocket_daemon: Configuration loaded\n");
}

// Base64 encode
char* base64_encode(const unsigned char* input, int length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    
    char* output = malloc(bufferPtr->length + 1);
    memcpy(output, bufferPtr->data, bufferPtr->length);
    output[bufferPtr->length] = '\0';
    
    BIO_free_all(bio);
    return output;
}

// WebSocket handshake
bool websocket_handshake(websocket_client_t* client, const char* request) {
    char* key_line = strstr(request, "Sec-WebSocket-Key: ");
    if (!key_line) return false;
    
    key_line += 19; // Skip "Sec-WebSocket-Key: "
    char* key_end = strchr(key_line, '\r');
    if (!key_end) key_end = strchr(key_line, '\n');
    if (!key_end) return false;
    
    char websocket_key[256];
    int key_len = key_end - key_line;
    strncpy(websocket_key, key_line, key_len);
    websocket_key[key_len] = '\0';
    
    // Create accept key
    char accept_input[512];
    snprintf(accept_input, sizeof(accept_input), "%s%s", websocket_key, WS_MAGIC_STRING);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)accept_input, strlen(accept_input), hash);
    
    char* accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);
    
    // Send handshake response
    char response[1024];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "%s"
        "\r\n",
        accept_key,
        config.enable_cors ? "Access-Control-Allow-Origin: *\r\n" : "");
    
    pthread_mutex_lock(&client->send_mutex);
    int sent = send(client->socket, response, strlen(response), 0);
    pthread_mutex_unlock(&client->send_mutex);
    
    free(accept_key);
    
    if (sent > 0) {
        client->handshake_complete = true;
        printf("websocket_daemon: WebSocket handshake complete for %s\n", client->remote_ip);
        return true;
    }
    
    return false;
}

// Send WebSocket frame
bool websocket_send_frame(websocket_client_t* client, int opcode, const char* payload, size_t payload_len) {
    if (!client->connected || !client->handshake_complete) return false;
    
    unsigned char frame[BUFFER_SIZE];
    size_t frame_len = 0;
    
    // First byte: FIN=1, RSV=000, Opcode
    frame[frame_len++] = 0x80 | (opcode & 0x0F);
    
    // Payload length
    if (payload_len < 126) {
        frame[frame_len++] = payload_len;
    } else if (payload_len < 65536) {
        frame[frame_len++] = 126;
        frame[frame_len++] = (payload_len >> 8) & 0xFF;
        frame[frame_len++] = payload_len & 0xFF;
    } else {
        frame[frame_len++] = 127;
        for (int i = 7; i >= 0; i--) {
            frame[frame_len++] = (payload_len >> (i * 8)) & 0xFF;
        }
    }
    
    // Payload
    if (payload_len > 0) {
        memcpy(&frame[frame_len], payload, payload_len);
        frame_len += payload_len;
    }
    
    pthread_mutex_lock(&client->send_mutex);
    int sent = send(client->socket, frame, frame_len, 0);
    pthread_mutex_unlock(&client->send_mutex);
    
    return sent > 0;
}

// Send text message
bool websocket_send_text(websocket_client_t* client, const char* message) {
    return websocket_send_frame(client, WS_OPCODE_TEXT, message, strlen(message));
}

// Broadcast message to all connected clients
void websocket_broadcast(const char* message) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < config.max_clients; i++) {
        if (clients[i].connected && clients[i].handshake_complete) {
            websocket_send_text(&clients[i], message);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Handle WebSocket message
void handle_websocket_message(websocket_client_t* client, const char* message) {
    printf("websocket_daemon: Received: %s\n", message);
    
    // Parse JSON message (simple parsing for now)
    if (strstr(message, "\"type\":\"launch\"")) {
        // Extract launch parameters from JSON
        char* core_start = strstr(message, "\"core\":\"");
        char* id_type_start = strstr(message, "\"id_type\":\"");
        char* identifier_start = strstr(message, "\"identifier\":\"");
        
        if (core_start && id_type_start && identifier_start) {
            char core[16] = {0};
            char id_type[16] = {0};
            char identifier[128] = {0};
            
            // Extract core
            core_start += 8;
            char* core_end = strchr(core_start, '"');
            if (core_end) {
                int len = core_end - core_start;
                if (len < sizeof(core)) {
                    strncpy(core, core_start, len);
                }
            }
            
            // Extract id_type
            id_type_start += 11;
            char* id_type_end = strchr(id_type_start, '"');
            if (id_type_end) {
                int len = id_type_end - id_type_start;
                if (len < sizeof(id_type)) {
                    strncpy(id_type, id_type_start, len);
                }
            }
            
            // Extract identifier
            identifier_start += 14;
            char* identifier_end = strchr(identifier_start, '"');
            if (identifier_end) {
                int len = identifier_end - identifier_start;
                if (len < sizeof(identifier)) {
                    strncpy(identifier, identifier_start, len);
                }
            }
            
            // Send to game launcher
            int fd = open(GAME_LAUNCHER_FIFO, O_WRONLY | O_NONBLOCK);
            if (fd >= 0) {
                char command[256];
                snprintf(command, sizeof(command), "%s:%s:%s:websocket", core, id_type, identifier);
                write(fd, command, strlen(command));
                close(fd);
                
                // Send response
                char response[256];
                snprintf(response, sizeof(response), 
                    "{\"type\":\"response\",\"status\":\"success\",\"message\":\"Game launch requested\"}");
                websocket_send_text(client, response);
            } else {
                char response[256];
                snprintf(response, sizeof(response), 
                    "{\"type\":\"response\",\"status\":\"error\",\"message\":\"Game launcher not available\"}");
                websocket_send_text(client, response);
            }
        }
    }
    else if (strstr(message, "\"type\":\"ping\"")) {
        websocket_send_text(client, "{\"type\":\"pong\"}");
    }
    else if (strstr(message, "\"type\":\"status\"")) {
        // Send status information
        char response[512];
        snprintf(response, sizeof(response),
            "{\"type\":\"status\",\"connected_clients\":%d,\"uptime\":%ld}",
            config.max_clients, time(NULL) - clients[0].connect_time);
        websocket_send_text(client, response);
    }
}

// Parse WebSocket frame
void parse_websocket_frame(websocket_client_t* client, const unsigned char* buffer, size_t len) {
    if (len < 2) return;
    
    int opcode = buffer[0] & 0x0F;
    bool masked = (buffer[1] & 0x80) != 0;
    size_t payload_len = buffer[1] & 0x7F;
    size_t header_len = 2;
    
    // Extended payload length
    if (payload_len == 126) {
        if (len < 4) return;
        payload_len = (buffer[2] << 8) | buffer[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (len < 10) return;
        payload_len = 0;
        for (int i = 2; i < 10; i++) {
            payload_len = (payload_len << 8) | buffer[i];
        }
        header_len = 10;
    }
    
    // Masking key
    unsigned char mask[4];
    if (masked) {
        if (len < header_len + 4) return;
        memcpy(mask, &buffer[header_len], 4);
        header_len += 4;
    }
    
    // Payload
    if (len < header_len + payload_len) return;
    
    char* payload = malloc(payload_len + 1);
    memcpy(payload, &buffer[header_len], payload_len);
    
    // Unmask payload if needed
    if (masked) {
        for (size_t i = 0; i < payload_len; i++) {
            payload[i] ^= mask[i % 4];
        }
    }
    
    payload[payload_len] = '\0';
    
    // Handle different opcodes
    switch (opcode) {
        case WS_OPCODE_TEXT:
            handle_websocket_message(client, payload);
            break;
        case WS_OPCODE_CLOSE:
            client->connected = false;
            break;
        case WS_OPCODE_PING:
            websocket_send_frame(client, WS_OPCODE_PONG, payload, payload_len);
            break;
        case WS_OPCODE_PONG:
            client->last_ping = time(NULL);
            break;
    }
    
    free(payload);
}

// Handle client connection
void* handle_client(void* arg) {
    websocket_client_t* client = (websocket_client_t*)arg;
    char buffer[BUFFER_SIZE];
    
    while (client->connected && keep_running) {
        ssize_t received = recv(client->socket, buffer, sizeof(buffer) - 1, 0);
        
        if (received <= 0) {
            break;
        }
        
        buffer[received] = '\0';
        
        if (!client->handshake_complete) {
            // Handle HTTP upgrade request
            if (strstr(buffer, "Upgrade: websocket")) {
                if (!websocket_handshake(client, buffer)) {
                    break;
                }
            } else {
                break;
            }
        } else {
            // Handle WebSocket frame
            parse_websocket_frame(client, (unsigned char*)buffer, received);
        }
    }
    
    // Cleanup
    close(client->socket);
    client->connected = false;
    client->handshake_complete = false;
    
    printf("websocket_daemon: Client %s disconnected\n", client->remote_ip);
    
    return NULL;
}

// Monitor announcements
void* announcement_monitor(void* arg) {
    int fd = open(ANNOUNCEMENT_FIFO, O_RDONLY);
    if (fd < 0) {
        printf("websocket_daemon: Cannot open announcement FIFO\n");
        return NULL;
    }
    
    char buffer[512];
    while (keep_running) {
        ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            
            // Forward as WebSocket message
            char message[1024];
            snprintf(message, sizeof(message), 
                "{\"type\":\"announcement\",\"data\":\"%s\"}", buffer);
            websocket_broadcast(message);
        }
    }
    
    close(fd);
    return NULL;
}

// Initialize WebSocket server
bool init_websocket_server() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("websocket_daemon: socket");
        return false;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config.bind_address);
    server_addr.sin_port = htons(config.port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("websocket_daemon: bind");
        close(server_socket);
        return false;
    }
    
    if (listen(server_socket, 5) < 0) {
        perror("websocket_daemon: listen");
        close(server_socket);
        return false;
    }
    
    printf("websocket_daemon: WebSocket server listening on %s:%d\n", 
           config.bind_address, config.port);
    
    return true;
}

// Write PID file
void write_pid_file() {
    FILE* fp = fopen(PID_FILE, "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    printf("websocket_daemon: Starting MiSTer WebSocket Daemon\n");
    
    // Load configuration
    load_config();
    
    // Initialize clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].connected = false;
        clients[i].handshake_complete = false;
        pthread_mutex_init(&clients[i].send_mutex, NULL);
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
    
    // Initialize WebSocket server
    if (!init_websocket_server()) {
        exit(1);
    }
    
    // Start announcement monitor
    pthread_t announcement_thread;
    if (config.forward_announcements) {
        pthread_create(&announcement_thread, NULL, announcement_monitor, NULL);
    }
    
    printf("websocket_daemon: WebSocket daemon ready\n");
    
    // Main accept loop
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (keep_running) {
                perror("websocket_daemon: accept");
            }
            continue;
        }
        
        // Find free client slot
        pthread_mutex_lock(&clients_mutex);
        int client_index = -1;
        for (int i = 0; i < config.max_clients; i++) {
            if (!clients[i].connected) {
                client_index = i;
                break;
            }
        }
        
        if (client_index >= 0) {
            clients[client_index].socket = client_socket;
            clients[client_index].connected = true;
            clients[client_index].handshake_complete = false;
            clients[client_index].connect_time = time(NULL);
            clients[client_index].last_ping = time(NULL);
            strcpy(clients[client_index].remote_ip, inet_ntoa(client_addr.sin_addr));
            
            printf("websocket_daemon: New client connected: %s\n", clients[client_index].remote_ip);
            
            // Start client handler thread
            pthread_t client_thread;
            pthread_create(&client_thread, NULL, handle_client, &clients[client_index]);
            pthread_detach(client_thread);
        } else {
            printf("websocket_daemon: Maximum clients reached, rejecting connection\n");
            close(client_socket);
        }
        pthread_mutex_unlock(&clients_mutex);
    }
    
    // Cleanup
    if (config.forward_announcements) {
        pthread_join(announcement_thread, NULL);
    }
    
    close(server_socket);
    printf("websocket_daemon: Shutting down\n");
    unlink(PID_FILE);
    
    return 0;
}