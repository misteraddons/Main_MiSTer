/*
 * MiSTer File System Trigger Daemon
 * 
 * Monitors special directories for file drops and automatically launches games
 * Supports different trigger methods and file formats
 * 
 * Features:
 * - Hot folder monitoring with inotify
 * - Multiple trigger directories for different cores
 * - Automatic file cleanup after launch
 * - Support for text files with game identifiers
 * - Image file support for barcode/QR integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define CONFIG_FILE "/media/fat/utils/filesystem_daemon.conf"
#define PID_FILE "/tmp/filesystem_daemon.pid"
#define MAX_WATCH_DIRS 16
#define MAX_PATH_LEN 256
#define EVENT_BUF_LEN 1024

// Trigger directory configuration
typedef struct {
    char path[MAX_PATH_LEN];
    char default_core[16];
    char default_id_type[16];
    bool auto_cleanup;
    bool recursive;
    int cleanup_delay_sec;
    char description[64];
} watch_dir_t;

// Configuration
typedef struct {
    watch_dir_t watch_dirs[MAX_WATCH_DIRS];
    int num_watch_dirs;
    bool show_notifications;
    int poll_interval_ms;
} filesystem_config_t;

// Global variables
static volatile int keep_running = 1;
static filesystem_config_t config;
static int inotify_fd = -1;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    memset(&config, 0, sizeof(config));
    config.show_notifications = true;
    config.poll_interval_ms = 100;
    
    // Default trigger directories
    strcpy(config.watch_dirs[0].path, "/tmp/mister_launch");
    strcpy(config.watch_dirs[0].default_core, "");
    strcpy(config.watch_dirs[0].default_id_type, "auto");
    config.watch_dirs[0].auto_cleanup = true;
    config.watch_dirs[0].cleanup_delay_sec = 5;
    strcpy(config.watch_dirs[0].description, "General game launcher");
    config.num_watch_dirs = 1;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("filesystem_daemon: Using default configuration\n");
        return;
    }
    
    char line[512];
    int dir_index = 0;
    
    while (fgets(line, sizeof(line), fp) && dir_index < MAX_WATCH_DIRS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Parse watch directory: watch_dir=path,core,id_type,cleanup,description
        if (strncmp(line, "watch_dir=", 10) == 0) {
            char* config_line = line + 10;
            char* token = strtok(config_line, ",");
            
            if (token) {
                strncpy(config.watch_dirs[dir_index].path, token, MAX_PATH_LEN - 1);
                
                token = strtok(NULL, ",");
                if (token) strncpy(config.watch_dirs[dir_index].default_core, token, 15);
                
                token = strtok(NULL, ",");
                if (token) strncpy(config.watch_dirs[dir_index].default_id_type, token, 15);
                
                token = strtok(NULL, ",");
                if (token) config.watch_dirs[dir_index].auto_cleanup = (strcmp(token, "true") == 0);
                
                token = strtok(NULL, "\n");
                if (token) strncpy(config.watch_dirs[dir_index].description, token, 63);
                
                dir_index++;
            }
        }
        else if (strncmp(line, "show_notifications=", 19) == 0) {
            config.show_notifications = (strcmp(line + 19, "true\n") == 0);
        }
        else if (strncmp(line, "poll_interval_ms=", 17) == 0) {
            config.poll_interval_ms = atoi(line + 17);
        }
    }
    
    config.num_watch_dirs = dir_index;
    fclose(fp);
    
    printf("filesystem_daemon: Loaded %d watch directories\n", config.num_watch_dirs);
}

// Send OSD message
void send_osd_message(const char* message) {
    if (!config.show_notifications) return;
    
    int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "osd_message %s", message);
        write(fd, cmd, strlen(cmd));
        close(fd);
    }
}

// Launch game
bool launch_game(const char* core, const char* id_type, const char* identifier) {
    int fd = open(GAME_LAUNCHER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return false;
    
    char command[512];
    snprintf(command, sizeof(command), "%s:%s:%s:filesystem", core, id_type, identifier);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    return written > 0;
}

// Parse game identifier from file content
bool parse_game_file(const char* filepath, char* core, char* id_type, char* identifier) {
    FILE* fp = fopen(filepath, "r");
    if (!fp) return false;
    
    char line[512];
    bool found = false;
    
    // Look for game identifier in file
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        if (line[0] == '#' || line[0] == '\0') continue;
        
        // Format: core:id_type:identifier
        char* first_colon = strchr(line, ':');
        if (first_colon) {
            *first_colon = '\0';
            strcpy(core, line);
            
            char* second_colon = strchr(first_colon + 1, ':');
            if (second_colon) {
                *second_colon = '\0';
                strcpy(id_type, first_colon + 1);
                strcpy(identifier, second_colon + 1);
                found = true;
                break;
            }
        }
        // Simple format: just identifier (use directory defaults)
        else if (strlen(line) > 0) {
            strcpy(identifier, line);
            found = true;
            break;
        }
    }
    
    fclose(fp);
    return found;
}

// Get file extension
const char* get_file_extension(const char* filename) {
    const char* dot = strrchr(filename, '.');
    return dot ? dot + 1 : "";
}

// Process trigger file
void process_trigger_file(const char* filepath, watch_dir_t* watch_dir) {
    char core[16] = {0};
    char id_type[16] = {0};
    char identifier[256] = {0};
    
    const char* extension = get_file_extension(filepath);
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    printf("filesystem_daemon: Processing file: %s\n", filename);
    
    // Handle different file types
    if (strcmp(extension, "txt") == 0 || strcmp(extension, "launch") == 0) {
        // Text file with game identifier
        if (parse_game_file(filepath, core, id_type, identifier)) {
            // Use parsed values or directory defaults
            if (strlen(core) == 0) strcpy(core, watch_dir->default_core);
            if (strlen(id_type) == 0) strcpy(id_type, watch_dir->default_id_type);
        } else {
            send_osd_message("Error: Could not parse game file");
            return;
        }
    }
    else if (strcmp(extension, "serial") == 0) {
        // Serial number file - filename without extension is the serial
        char* name_copy = strdup(filename);
        char* dot = strrchr(name_copy, '.');
        if (dot) *dot = '\0';
        
        strcpy(core, watch_dir->default_core);
        strcpy(id_type, "serial");
        strcpy(identifier, name_copy);
        free(name_copy);
    }
    else if (strcmp(extension, "title") == 0) {
        // Title file - filename without extension is the title
        char* name_copy = strdup(filename);
        char* dot = strrchr(name_copy, '.');
        if (dot) *dot = '\0';
        
        strcpy(core, watch_dir->default_core);
        strcpy(id_type, "title");
        strcpy(identifier, name_copy);
        free(name_copy);
    }
    else if (strcmp(extension, "png") == 0 || strcmp(extension, "jpg") == 0 || 
             strcmp(extension, "jpeg") == 0 || strcmp(extension, "bmp") == 0) {
        // Image file - could contain barcode/QR code
        // For now, treat filename as identifier
        char* name_copy = strdup(filename);
        char* dot = strrchr(name_copy, '.');
        if (dot) *dot = '\0';
        
        strcpy(core, watch_dir->default_core);
        strcpy(id_type, watch_dir->default_id_type);
        strcpy(identifier, name_copy);
        free(name_copy);
        
        send_osd_message("Image processing not yet implemented");
    }
    else {
        // Unknown file type - use filename as identifier
        char* name_copy = strdup(filename);
        char* dot = strrchr(name_copy, '.');
        if (dot) *dot = '\0';
        
        strcpy(core, watch_dir->default_core);
        strcpy(id_type, watch_dir->default_id_type);
        strcpy(identifier, name_copy);
        free(name_copy);
    }
    
    // Launch game if we have valid parameters
    if (strlen(identifier) > 0) {
        if (launch_game(core, id_type, identifier)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "FS Trigger: %s", identifier);
            send_osd_message(msg);
            
            // Schedule cleanup if enabled
            if (watch_dir->auto_cleanup) {
                // Note: In a full implementation, we'd use a timer/thread for cleanup
                // For now, just delete immediately after a short delay
                sleep(1);
                unlink(filepath);
                printf("filesystem_daemon: Cleaned up file: %s\n", filename);
            }
        } else {
            send_osd_message("Error: Failed to launch game");
        }
    } else {
        send_osd_message("Error: No game identifier found");
    }
}

// Handle inotify events
void handle_inotify_events() {
    char buffer[EVENT_BUF_LEN];
    int length = read(inotify_fd, buffer, EVENT_BUF_LEN);
    
    if (length < 0) return;
    
    int i = 0;
    while (i < length) {
        struct inotify_event* event = (struct inotify_event*)&buffer[i];
        
        if (event->len > 0 && (event->mask & IN_CLOSE_WRITE)) {
            // Find which watch directory this event belongs to
            for (int j = 0; j < config.num_watch_dirs; j++) {
                char full_path[MAX_PATH_LEN * 2];
                snprintf(full_path, sizeof(full_path), "%s/%s", 
                         config.watch_dirs[j].path, event->name);
                
                // Check if file exists and process it
                if (access(full_path, F_OK) == 0) {
                    process_trigger_file(full_path, &config.watch_dirs[j]);
                    break;
                }
            }
        }
        
        i += sizeof(struct inotify_event) + event->len;
    }
}

// Initialize file system monitoring
void init_filesystem_monitoring() {
    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("inotify_init");
        return;
    }
    
    // Create and watch directories
    for (int i = 0; i < config.num_watch_dirs; i++) {
        // Create directory if it doesn't exist
        struct stat st;
        if (stat(config.watch_dirs[i].path, &st) == -1) {
            if (mkdir(config.watch_dirs[i].path, 0755) == -1) {
                printf("filesystem_daemon: Warning - Could not create directory: %s\n",
                       config.watch_dirs[i].path);
                continue;
            }
        }
        
        // Add inotify watch
        int wd = inotify_add_watch(inotify_fd, config.watch_dirs[i].path, 
                                   IN_CLOSE_WRITE | IN_MOVED_TO);
        if (wd == -1) {
            printf("filesystem_daemon: Warning - Could not watch directory: %s\n",
                   config.watch_dirs[i].path);
        } else {
            printf("filesystem_daemon: Watching directory: %s\n", 
                   config.watch_dirs[i].path);
        }
    }
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
    
    printf("filesystem_daemon: Starting MiSTer File System Trigger Daemon\n");
    
    // Load configuration
    load_config();
    
    // Check if game launcher service is available
    if (access(GAME_LAUNCHER_FIFO, F_OK) != 0) {
        printf("filesystem_daemon: Warning - Game launcher service not available\n");
        printf("filesystem_daemon: Please start /media/fat/utils/game_launcher first\n");
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
    
    // Initialize file system monitoring
    init_filesystem_monitoring();
    
    printf("filesystem_daemon: File system monitoring active\n");
    
    // Main monitoring loop
    while (keep_running) {
        fd_set readfds;
        struct timeval timeout;
        
        FD_ZERO(&readfds);
        FD_SET(inotify_fd, &readfds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = config.poll_interval_ms * 1000;
        
        int result = select(inotify_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (result > 0 && FD_ISSET(inotify_fd, &readfds)) {
            handle_inotify_events();
        }
    }
    
    // Cleanup
    if (inotify_fd >= 0) {
        close(inotify_fd);
    }
    
    printf("filesystem_daemon: Shutting down\n");
    unlink(PID_FILE);
    
    return 0;
}