/*
 * MiSTer Game Launcher Service
 * 
 * Centralized service for game identification, GameDB lookup, and MGL creation
 * Supports multiple input sources through JSON command protocol
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <json-c/json.h>
#include "game_launcher_service.h"

// Global configuration
static daemon_config_t config;
static volatile int keep_running = 1;
static pthread_t service_thread;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize the service
bool game_launcher_init(const char* config_path) {
    // Initialize default configuration
    strcpy(config.games_dir, "/media/fat/games");
    strcpy(config.gamedb_dir, "/media/fat/utils/gamedb");
    strcpy(config.temp_dir, "/tmp");
    config.fuzzy_threshold = 30;
    config.show_notifications = true;
    config.osd_timeout = 3000;
    
    // Load configuration if provided
    if (config_path) {
        // TODO: Load config from file
        printf("game_launcher: Loading config from %s\n", config_path);
    }
    
    // Create FIFO for communication
    unlink(GAME_LAUNCHER_FIFO);
    if (mkfifo(GAME_LAUNCHER_FIFO, 0666) < 0) {
        perror("game_launcher: Failed to create FIFO");
        return false;
    }
    chmod(GAME_LAUNCHER_FIFO, 0666);
    
    printf("game_launcher: Service initialized\n");
    return true;
}

// Process JSON command
bool process_command(const char* json_str, char* response, size_t response_size) {
    json_object* root = json_tokener_parse(json_str);
    if (!root) {
        snprintf(response, response_size, "{\"error\": \"Invalid JSON\"}");
        return false;
    }
    
    // Extract command
    json_object* cmd_obj;
    if (!json_object_object_get_ex(root, "command", &cmd_obj)) {
        snprintf(response, response_size, "{\"error\": \"Missing command\"}");
        json_object_put(root);
        return false;
    }
    
    const char* command = json_object_get_string(cmd_obj);
    
    if (strcmp(command, "find_game") == 0) {
        // Extract game request parameters
        json_object* system_obj, *id_type_obj, *identifier_obj;
        
        if (!json_object_object_get_ex(root, "system", &system_obj) ||
            !json_object_object_get_ex(root, "id_type", &id_type_obj) ||
            !json_object_object_get_ex(root, "identifier", &identifier_obj)) {
            snprintf(response, response_size, "{\"error\": \"Missing required parameters\"}");
            json_object_put(root);
            return false;
        }
        
        const char* system = json_object_get_string(system_obj);
        const char* id_type = json_object_get_string(id_type_obj);
        const char* identifier = json_object_get_string(identifier_obj);
        
        // TODO: Implement game lookup
        snprintf(response, response_size, 
                "{\"success\": true, \"system\": \"%s\", \"id_type\": \"%s\", \"identifier\": \"%s\"}",
                system, id_type, identifier);
        
    } else if (strcmp(command, "get_status") == 0) {
        snprintf(response, response_size, 
                "{\"status\": \"running\", \"version\": \"1.0\", \"uptime\": %ld}",
                time(NULL));
        
    } else {
        snprintf(response, response_size, "{\"error\": \"Unknown command: %s\"}", command);
        json_object_put(root);
        return false;
    }
    
    json_object_put(root);
    return true;
}

// Service thread function
void* service_thread_func(void* arg) {
    char buffer[4096];
    char response[4096];
    
    while (keep_running) {
        // Open FIFO for reading
        int fd = open(GAME_LAUNCHER_FIFO, O_RDONLY);
        if (fd < 0) {
            if (keep_running) {
                perror("game_launcher: Failed to open FIFO");
                sleep(1);
            }
            continue;
        }
        
        // Read commands
        while (keep_running) {
            ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) {
                break; // Writer closed
            }
            
            buffer[bytes] = '\0';
            
            // Process each line as a separate command
            char* line = strtok(buffer, "\n");
            while (line) {
                if (strlen(line) > 0) {
                    printf("game_launcher: Processing command: %s\n", line);
                    
                    // Process command and generate response
                    if (process_command(line, response, sizeof(response))) {
                        printf("game_launcher: Response: %s\n", response);
                    } else {
                        printf("game_launcher: Error processing command\n");
                    }
                    
                    // TODO: Send response back to caller
                }
                line = strtok(NULL, "\n");
            }
        }
        
        close(fd);
    }
    
    return NULL;
}

// Main service function
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("game_launcher: Starting Game Launcher Service\n");
    
    // Initialize service
    if (!game_launcher_init(NULL)) {
        fprintf(stderr, "game_launcher: Failed to initialize service\n");
        return 1;
    }
    
    // Start service thread
    if (pthread_create(&service_thread, NULL, service_thread_func, NULL) != 0) {
        perror("game_launcher: Failed to create service thread");
        return 1;
    }
    
    // Main loop
    while (keep_running) {
        sleep(1);
    }
    
    // Cleanup
    printf("game_launcher: Shutting down\n");
    pthread_join(service_thread, NULL);
    unlink(GAME_LAUNCHER_FIFO);
    
    return 0;
}

// Utility functions for clients
bool game_launcher_send_command(const char* command, char* response, size_t response_size) {
    int fd = open(GAME_LAUNCHER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    ssize_t written = write(fd, command, strlen(command));
    write(fd, "\n", 1);
    close(fd);
    
    // TODO: Read response from service
    // For now, just return success
    if (response) {
        strcpy(response, "{\"success\": true}");
    }
    
    return written > 0;
}

// Example client functions
bool game_launcher_find_game_by_serial(const char* system, const char* serial, char* response, size_t response_size) {
    char command[512];
    snprintf(command, sizeof(command), 
             "{\"command\": \"find_game\", \"system\": \"%s\", \"id_type\": \"serial\", \"identifier\": \"%s\"}",
             system, serial);
    
    return game_launcher_send_command(command, response, response_size);
}

bool game_launcher_find_game_by_title(const char* system, const char* title, char* response, size_t response_size) {
    char command[512];
    snprintf(command, sizeof(command), 
             "{\"command\": \"find_game\", \"system\": \"%s\", \"id_type\": \"title\", \"identifier\": \"%s\"}",
             system, title);
    
    return game_launcher_send_command(command, response, response_size);
}

bool game_launcher_find_game_by_uuid(const char* uuid, char* response, size_t response_size) {
    char command[512];
    snprintf(command, sizeof(command), 
             "{\"command\": \"find_game\", \"system\": \"auto\", \"id_type\": \"uuid\", \"identifier\": \"%s\"}",
             uuid);
    
    return game_launcher_send_command(command, response, response_size);
}