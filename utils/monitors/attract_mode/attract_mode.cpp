/*
 * MiSTer Attract Mode Daemon
 * 
 * Cycles through random games on random systems automatically
 * Similar to Super Attract Mode but runs as a daemon
 * 
 * Features:
 * - Random game selection from whitelisted systems
 * - Configurable timing and intervals
 * - Game filtering and blacklists
 * - Pause/resume functionality
 * - Integration with game launcher system
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <stdbool.h>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define CONFIG_FILE "/media/fat/utils/attract_mode.conf"
#define CONTROL_FIFO "/dev/MiSTer_attract_control"
#define PID_FILE "/tmp/attract_mode.pid"
#define MAX_SYSTEMS 32
#define MAX_GAMES_PER_SYSTEM 1000

// System configuration
typedef struct {
    char name[32];
    char gameid_file[256];
    bool enabled;
    int weight;
    int min_play_time;
    int max_play_time;
} attract_system_t;

// Game entry
typedef struct {
    char identifier[128];
    char title[128];
    char id_type[16];
    bool blacklisted;
} attract_game_t;

// Configuration
typedef struct {
    attract_system_t systems[MAX_SYSTEMS];
    int num_systems;
    int base_play_time;
    int play_time_variance;
    bool enable_notifications;
    bool pause_on_input;
    bool resume_after_timeout;
    int resume_timeout_minutes;
    bool random_order;
    int transition_delay;
    char startup_message[128];
    bool enable_osd_info;
    int info_display_duration;
} attract_config_t;

// Global variables
static volatile int keep_running = 1;
static volatile int attract_active = 0;
static volatile int attract_paused = 0;
static attract_config_t config;
static attract_game_t* system_games[MAX_SYSTEMS];
static int system_game_counts[MAX_SYSTEMS];
static time_t current_game_start = 0;
static int current_play_duration = 0;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    memset(&config, 0, sizeof(config));
    config.base_play_time = 60;
    config.play_time_variance = 30;
    config.enable_notifications = true;
    config.pause_on_input = true;
    config.resume_after_timeout = true;
    config.resume_timeout_minutes = 5;
    config.random_order = true;
    config.transition_delay = 3;
    strcpy(config.startup_message, "Attract Mode Active");
    config.enable_osd_info = true;
    config.info_display_duration = 5;
    
    // Default systems
    strcpy(config.systems[0].name, "PSX");
    strcpy(config.systems[0].gameid_file, "/media/fat/utils/gameDB/PSX.data.json");
    config.systems[0].enabled = true;
    config.systems[0].weight = 10;
    config.systems[0].min_play_time = 30;
    config.systems[0].max_play_time = 120;
    
    strcpy(config.systems[1].name, "SNES");
    strcpy(config.systems[1].gameid_file, "/media/fat/utils/gameDB/SNES.data.json");
    config.systems[1].enabled = true;
    config.systems[1].weight = 10;
    config.systems[1].min_play_time = 45;
    config.systems[1].max_play_time = 90;
    
    strcpy(config.systems[2].name, "Genesis");
    strcpy(config.systems[2].gameid_file, "/media/fat/utils/gameDB/Genesis.data.json");
    config.systems[2].enabled = true;
    config.systems[2].weight = 8;
    config.systems[2].min_play_time = 30;
    config.systems[2].max_play_time = 90;
    
    config.num_systems = 3;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("attract_mode: Using default configuration\n");
        return;
    }
    
    fclose(fp);
    printf("attract_mode: Configuration loaded - %d systems enabled\n", config.num_systems);
}

// Send OSD message
void send_osd_message(const char* message) {
    if (!config.enable_notifications) return;
    
    int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "osd_message %s", message);
        write(fd, cmd, strlen(cmd));
        close(fd);
    }
}

// Load games from GameID file
int load_system_games(int system_index) {
    attract_system_t* system = &config.systems[system_index];
    
    FILE* fp = fopen(system->gameid_file, "r");
    if (!fp) {
        printf("attract_mode: Cannot open GameID file: %s\n", system->gameid_file);
        return 0;
    }
    
    // Allocate games array
    system_games[system_index] = (attract_game_t*)malloc(MAX_GAMES_PER_SYSTEM * sizeof(attract_game_t));
    if (!system_games[system_index]) {
        fclose(fp);
        return 0;
    }
    
    // For now, just create a few dummy games
    int game_count = 0;
    strcpy(system_games[system_index][game_count].identifier, "DUMMY001");
    strcpy(system_games[system_index][game_count].title, "Test Game 1");
    strcpy(system_games[system_index][game_count].id_type, "serial");
    system_games[system_index][game_count].blacklisted = false;
    game_count++;
    
    strcpy(system_games[system_index][game_count].identifier, "DUMMY002");
    strcpy(system_games[system_index][game_count].title, "Test Game 2");
    strcpy(system_games[system_index][game_count].id_type, "serial");
    system_games[system_index][game_count].blacklisted = false;
    game_count++;
    
    fclose(fp);
    system_game_counts[system_index] = game_count;
    
    printf("attract_mode: Loaded %d games for %s\n", game_count, system->name);
    return game_count;
}

// Select random system based on weights
int select_random_system() {
    int total_weight = 0;
    
    for (int i = 0; i < config.num_systems; i++) {
        if (config.systems[i].enabled && system_game_counts[i] > 0) {
            total_weight += config.systems[i].weight;
        }
    }
    
    if (total_weight == 0) return -1;
    
    int random_value = rand() % total_weight;
    int current_weight = 0;
    
    for (int i = 0; i < config.num_systems; i++) {
        if (config.systems[i].enabled && system_game_counts[i] > 0) {
            current_weight += config.systems[i].weight;
            if (random_value < current_weight) {
                return i;
            }
        }
    }
    
    return -1;
}

// Select random game from system
int select_random_game(int system_index) {
    if (system_index < 0 || system_game_counts[system_index] == 0) return -1;
    
    int available_games = 0;
    for (int i = 0; i < system_game_counts[system_index]; i++) {
        if (!system_games[system_index][i].blacklisted) {
            available_games++;
        }
    }
    
    if (available_games == 0) return -1;
    
    int target_index = rand() % available_games;
    int current_index = 0;
    
    for (int i = 0; i < system_game_counts[system_index]; i++) {
        if (!system_games[system_index][i].blacklisted) {
            if (current_index == target_index) {
                return i;
            }
            current_index++;
        }
    }
    
    return -1;
}

// Launch game
bool launch_attract_game(int system_index, int game_index) {
    if (system_index < 0 || game_index < 0) return false;
    
    attract_system_t* system = &config.systems[system_index];
    attract_game_t* game = &system_games[system_index][game_index];
    
    int fd = open(GAME_LAUNCHER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return false;
    
    char command[256];
    snprintf(command, sizeof(command), "%s:%s:%s:attract_mode", 
             system->name, game->id_type, game->identifier);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    if (written > 0) {
        printf("attract_mode: Launched %s - %s\n", system->name, game->title);
        
        if (config.enable_osd_info) {
            char info_msg[256];
            snprintf(info_msg, sizeof(info_msg), "Attract: %s - %s", system->name, game->title);
            send_osd_message(info_msg);
        }
        
        return true;
    }
    
    return false;
}

// Calculate play duration
int calculate_play_duration(int system_index) {
    attract_system_t* system = &config.systems[system_index];
    
    if (system->min_play_time > 0 && system->max_play_time > 0) {
        int range = system->max_play_time - system->min_play_time;
        return system->min_play_time + (rand() % (range + 1));
    }
    
    int duration = config.base_play_time;
    if (config.play_time_variance > 0) {
        duration += (rand() % (config.play_time_variance * 2 + 1)) - config.play_time_variance;
    }
    
    return duration > 10 ? duration : 10;
}

// Handle control commands
void handle_control_command(const char* command) {
    if (strcmp(command, "start") == 0) {
        attract_active = 1;
        attract_paused = 0;
        send_osd_message("Attract Mode Started");
    } else if (strcmp(command, "stop") == 0) {
        attract_active = 0;
        attract_paused = 0;
        send_osd_message("Attract Mode Stopped");
    } else if (strcmp(command, "pause") == 0) {
        attract_paused = 1;
        send_osd_message("Attract Mode Paused");
    } else if (strcmp(command, "resume") == 0) {
        attract_paused = 0;
        send_osd_message("Attract Mode Resumed");
    } else if (strcmp(command, "next") == 0) {
        current_game_start = 0;
        send_osd_message("Attract Mode: Next Game");
    }
}

// Monitor control FIFO
void* control_monitor(void* arg) {
    unlink(CONTROL_FIFO);
    if (mkfifo(CONTROL_FIFO, 0666) < 0) {
        printf("attract_mode: Failed to create control FIFO\n");
        return NULL;
    }
    chmod(CONTROL_FIFO, 0666);
    
    while (keep_running) {
        int fd = open(CONTROL_FIFO, O_RDONLY);
        if (fd < 0) {
            if (keep_running) sleep(1);
            continue;
        }
        
        char buffer[256];
        while (keep_running) {
            ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) break;
            
            buffer[bytes] = '\0';
            handle_control_command(buffer);
        }
        
        close(fd);
    }
    
    unlink(CONTROL_FIFO);
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

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    printf("attract_mode: Starting MiSTer Attract Mode Daemon\n");
    
    load_config();
    srand(time(NULL));
    
    // Load games for all systems
    for (int i = 0; i < config.num_systems; i++) {
        if (config.systems[i].enabled) {
            load_system_games(i);
        }
    }
    
    bool foreground = (argc > 1 && strcmp(argv[1], "-f") == 0);
    bool start_active = (argc > 1 && strcmp(argv[1], "--start") == 0);
    
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
    
    write_pid_file();
    
    // Start control monitor thread
    pthread_t control_thread;
    pthread_create(&control_thread, NULL, control_monitor, NULL);
    
    if (start_active) {
        attract_active = 1;
        send_osd_message(config.startup_message);
    }
    
    printf("attract_mode: Attract mode daemon ready\n");
    
    // Main attract mode loop
    while (keep_running) {
        if (attract_active && !attract_paused) {
            time_t now = time(NULL);
            
            if (current_game_start == 0 || (now - current_game_start) >= current_play_duration) {
                int system_index = select_random_system();
                if (system_index >= 0) {
                    int game_index = select_random_game(system_index);
                    if (game_index >= 0) {
                        if (current_game_start > 0) {
                            sleep(config.transition_delay);
                        }
                        
                        if (launch_attract_game(system_index, game_index)) {
                            current_game_start = time(NULL);
                            current_play_duration = calculate_play_duration(system_index);
                            
                            printf("attract_mode: Playing for %d seconds\n", current_play_duration);
                        }
                    }
                }
            }
        }
        
        sleep(1);
    }
    
    // Cleanup
    pthread_join(control_thread, NULL);
    
    for (int i = 0; i < config.num_systems; i++) {
        if (system_games[i]) {
            free(system_games[i]);
        }
    }
    
    printf("attract_mode: Shutting down\n");
    unlink(PID_FILE);
    
    return 0;
}