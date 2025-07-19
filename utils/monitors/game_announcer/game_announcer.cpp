/*
 * MiSTer Game Announcer
 * 
 * Monitors MiSTer's current game state and announces changes
 * via UART, HTTP, and other connected interfaces
 * 
 * Features:
 * - MGL file monitoring for game detection
 * - Core state monitoring
 * - Real-time announcements to connected clients
 * - GameDB integration for rich game information
 * - Multiple announcement channels (UART, HTTP, etc.)
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
#include <json/json.h>

#define CONFIG_FILE "/media/fat/utils/game_announcer.conf"
#define PID_FILE "/tmp/game_announcer.pid"
#define ANNOUNCEMENT_FIFO "/dev/MiSTer_announcements"
#define MGL_DIR "/tmp"
#define CORES_DIR "/media/fat"
#define GAMEDB_DIR "/media/fat/utils/gamedb"
#define MAX_PATH_LENGTH 512
#define MAX_GAME_NAME 128
#define MAX_CORE_NAME 32

// Configuration
typedef struct {
    bool monitor_mgl_files;
    bool monitor_core_process;
    bool send_uart_announcements;
    bool send_http_announcements;
    bool gamedb_lookup;
    char announcement_format[256];
    int poll_interval_ms;
} announcer_config_t;

// Game information
typedef struct {
    char core[MAX_CORE_NAME];
    char game_name[MAX_GAME_NAME];
    char file_path[MAX_PATH_LENGTH];
    char serial_id[32];
    time_t timestamp;
    bool is_valid;
} game_info_t;

// Global variables
static volatile int keep_running = 1;
static announcer_config_t config;
static game_info_t current_game = {0};
static int inotify_fd = -1;
static int announcement_fd = -1;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
    if (inotify_fd >= 0) {
        close(inotify_fd);
    }
    if (announcement_fd >= 0) {
        close(announcement_fd);
    }
}

// Initialize default configuration
void init_config_defaults() {
    config.monitor_mgl_files = true;
    config.monitor_core_process = true;
    config.send_uart_announcements = true;
    config.send_http_announcements = true;
    config.gamedb_lookup = true;
    strcpy(config.announcement_format, "GAME_CHANGED %s \"%s\" \"%s\"");
    config.poll_interval_ms = 1000;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("game_announcer: Using default configuration\n");
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
        
        if (strcmp(key, "monitor_mgl_files") == 0) {
            config.monitor_mgl_files = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "monitor_core_process") == 0) {
            config.monitor_core_process = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "send_uart_announcements") == 0) {
            config.send_uart_announcements = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "send_http_announcements") == 0) {
            config.send_http_announcements = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "gamedb_lookup") == 0) {
            config.gamedb_lookup = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "announcement_format") == 0) {
            strncpy(config.announcement_format, value, sizeof(config.announcement_format) - 1);
        } else if (strcmp(key, "poll_interval_ms") == 0) {
            config.poll_interval_ms = atoi(value);
        }
    }
    
    fclose(fp);
    printf("game_announcer: Configuration loaded\n");
}

// Create announcement FIFO
void create_announcement_fifo() {
    // Remove existing FIFO
    unlink(ANNOUNCEMENT_FIFO);
    
    // Create new FIFO
    if (mkfifo(ANNOUNCEMENT_FIFO, 0666) == -1) {
        perror("Failed to create announcement FIFO");
        return;
    }
    
    // Open for writing (non-blocking)
    announcement_fd = open(ANNOUNCEMENT_FIFO, O_WRONLY | O_NONBLOCK);
    if (announcement_fd < 0) {
        // No readers yet, that's okay
        printf("game_announcer: Announcement FIFO created (no readers yet)\n");
    } else {
        printf("game_announcer: Announcement FIFO ready\n");
    }
}

// Send announcement
void send_announcement(const char* message) {
    if (!message) return;
    
    printf("game_announcer: %s\n", message);
    
    // Try to reopen FIFO if needed
    if (announcement_fd < 0) {
        announcement_fd = open(ANNOUNCEMENT_FIFO, O_WRONLY | O_NONBLOCK);
    }
    
    // Send to FIFO if available
    if (announcement_fd >= 0) {
        ssize_t written = write(announcement_fd, message, strlen(message));
        write(announcement_fd, "\n", 1);
        
        if (written < 0 && errno == EPIPE) {
            // No readers, close and try again later
            close(announcement_fd);
            announcement_fd = -1;
        }
    }
}

// Extract core name from MGL file path
bool extract_core_from_mgl(const char* mgl_path, char* core_name) {
    // MGL files are typically named like: /tmp/PSX_GameName.mgl
    const char* filename = strrchr(mgl_path, '/');
    if (!filename) filename = mgl_path;
    else filename++;
    
    // Find first underscore
    const char* underscore = strchr(filename, '_');
    if (!underscore) return false;
    
    size_t core_len = underscore - filename;
    if (core_len >= MAX_CORE_NAME) core_len = MAX_CORE_NAME - 1;
    
    memcpy(core_name, filename, core_len);
    core_name[core_len] = '\0';
    
    return true;
}

// Extract game name from MGL file
bool extract_game_info_from_mgl(const char* mgl_path, game_info_t* game_info) {
    FILE* fp = fopen(mgl_path, "r");
    if (!fp) return false;
    
    char line[512];
    bool found_file = false;
    
    // Clear game info
    memset(game_info, 0, sizeof(game_info_t));
    game_info->timestamp = time(NULL);
    
    // Extract core from filename
    if (!extract_core_from_mgl(mgl_path, game_info->core)) {
        fclose(fp);
        return false;
    }
    
    // Parse MGL file for game file path
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        // Skip empty lines and XML tags
        if (line[0] == '\0' || line[0] == '<') continue;
        
        // First non-XML line should be the file path
        if (!found_file) {
            strncpy(game_info->file_path, line, sizeof(game_info->file_path) - 1);
            found_file = true;
        }
    }
    
    fclose(fp);
    
    if (!found_file) return false;
    
    // Extract game name from file path
    const char* filename = strrchr(game_info->file_path, '/');
    if (!filename) filename = game_info->file_path;
    else filename++;
    
    // Remove extension
    strncpy(game_info->game_name, filename, sizeof(game_info->game_name) - 1);
    char* dot = strrchr(game_info->game_name, '.');
    if (dot) *dot = '\0';
    
    game_info->is_valid = true;
    return true;
}

// Lookup game in GameDB
bool lookup_game_in_gamedb(game_info_t* game_info) {
    if (!config.gamedb_lookup || !game_info->is_valid) return false;
    
    char gamedb_file[MAX_PATH_LENGTH];
    snprintf(gamedb_file, sizeof(gamedb_file), "%s/%s.data.json", GAMEDB_DIR, game_info->core);
    
    FILE* fp = fopen(gamedb_file, "r");
    if (!fp) return false;
    
    // Read entire file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);
    
    char* json_data = malloc(file_size + 1);
    if (!json_data) {
        fclose(fp);
        return false;
    }
    
    fread(json_data, 1, file_size, fp);
    json_data[file_size] = '\0';
    fclose(fp);
    
    // Parse JSON
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(json_data, root)) {
        free(json_data);
        return false;
    }
    
    free(json_data);
    
    // Search for game by filename matching
    for (const auto& game : root) {
        if (game.isMember("title")) {
            const std::string title = game["title"].asString();
            
            // Simple filename matching (could be improved with fuzzy matching)
            if (strstr(game_info->game_name, title.c_str()) || 
                strstr(title.c_str(), game_info->game_name)) {
                
                // Update with proper title
                strncpy(game_info->game_name, title.c_str(), sizeof(game_info->game_name) - 1);
                
                // Extract serial if available
                if (game.isMember("id")) {
                    strncpy(game_info->serial_id, game["id"].asString().c_str(), sizeof(game_info->serial_id) - 1);
                }
                
                return true;
            }
        }
    }
    
    return false;
}

// Check if game has changed
bool has_game_changed(const game_info_t* new_game) {
    if (!current_game.is_valid && !new_game->is_valid) return false;
    if (!current_game.is_valid && new_game->is_valid) return true;
    if (current_game.is_valid && !new_game->is_valid) return true;
    
    return (strcmp(current_game.core, new_game->core) != 0 ||
            strcmp(current_game.game_name, new_game->game_name) != 0 ||
            strcmp(current_game.file_path, new_game->file_path) != 0);
}

// Announce game change
void announce_game_change(const game_info_t* game_info) {
    char announcement[512];
    
    if (!game_info->is_valid) {
        snprintf(announcement, sizeof(announcement), "GAME_STOPPED");
    } else {
        // Use configured format
        snprintf(announcement, sizeof(announcement), config.announcement_format,
                game_info->core, game_info->game_name, game_info->file_path);
    }
    
    send_announcement(announcement);
    
    // Also send detailed info
    if (game_info->is_valid) {
        char detail[512];
        snprintf(detail, sizeof(detail), "GAME_DETAILS core=\"%s\" name=\"%s\" path=\"%s\" serial=\"%s\" timestamp=%ld",
                game_info->core, game_info->game_name, game_info->file_path, 
                game_info->serial_id, game_info->timestamp);
        send_announcement(detail);
    }
}

// Find latest MGL file
char* find_latest_mgl_file() {
    DIR* dir = opendir(MGL_DIR);
    if (!dir) return NULL;
    
    struct dirent* entry;
    time_t latest_time = 0;
    static char latest_file[MAX_PATH_LENGTH];
    latest_file[0] = '\0';
    
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".mgl") == NULL) continue;
        
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s/%s", MGL_DIR, entry->d_name);
        
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            if (file_stat.st_mtime > latest_time) {
                latest_time = file_stat.st_mtime;
                strncpy(latest_file, full_path, sizeof(latest_file) - 1);
            }
        }
    }
    
    closedir(dir);
    
    return latest_file[0] ? latest_file : NULL;
}

// Monitor MGL files
void monitor_mgl_files() {
    static time_t last_check = 0;
    time_t current_time = time(NULL);
    
    // Only check every poll interval
    if (current_time - last_check < config.poll_interval_ms / 1000) {
        return;
    }
    last_check = current_time;
    
    char* latest_mgl = find_latest_mgl_file();
    if (!latest_mgl) {
        // No MGL file found - game may have stopped
        game_info_t empty_game = {0};
        if (has_game_changed(&empty_game)) {
            announce_game_change(&empty_game);
            current_game = empty_game;
        }
        return;
    }
    
    // Check if this MGL file is newer than our last known game
    struct stat mgl_stat;
    if (stat(latest_mgl, &mgl_stat) != 0) return;
    
    if (mgl_stat.st_mtime <= current_game.timestamp) {
        return; // No change
    }
    
    // Extract game info from MGL
    game_info_t new_game;
    if (!extract_game_info_from_mgl(latest_mgl, &new_game)) {
        return;
    }
    
    // Try GameDB lookup
    lookup_game_in_gamedb(&new_game);
    
    // Check if game has changed
    if (has_game_changed(&new_game)) {
        announce_game_change(&new_game);
        current_game = new_game;
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

// Main function
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    printf("game_announcer: Starting MiSTer Game Announcer\n");
    
    // Load configuration
    load_config();
    
    // Create announcement FIFO
    create_announcement_fifo();
    
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
    
    printf("game_announcer: Game announcement service ready\n");
    
    // Main monitoring loop
    while (keep_running) {
        if (config.monitor_mgl_files) {
            monitor_mgl_files();
        }
        
        usleep(100000); // 100ms sleep
    }
    
    // Cleanup
    printf("game_announcer: Shutting down\n");
    if (announcement_fd >= 0) {
        close(announcement_fd);
    }
    unlink(ANNOUNCEMENT_FIFO);
    unlink(PID_FILE);
    
    return 0;
}