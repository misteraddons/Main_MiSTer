/*
 * MiSTer GPIO Game Launcher Daemon
 * 
 * Monitors GPIO pins for button presses and launches assigned games
 * Supports both direct button mapping and rotary encoder navigation
 * 
 * Features:
 * - Configurable GPIO pin assignments
 * - Button debouncing
 * - Rotary encoder support for game browsing
 * - Favorite games quick access
 * - Configuration file for game assignments
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

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define CONFIG_FILE "/media/fat/utils/gpio_daemon.conf"
#define PID_FILE "/tmp/gpio_daemon.pid"
#define GPIO_BASE_PATH "/sys/class/gpio"
#define MAX_BUTTONS 16
#define MAX_GAMES_LIST 1000
#define DEBOUNCE_MS 50

// Button configuration
typedef struct {
    int gpio_pin;
    char game_core[16];
    char game_id_type[16];  // "serial", "title", "path"
    char game_identifier[128];
    char description[64];
    bool enabled;
    unsigned long last_press_time;
} gpio_button_t;

// Rotary encoder configuration
typedef struct {
    int pin_a;
    int pin_b;
    int pin_button;  // Select button
    bool enabled;
    int current_position;
    int last_a_state;
    unsigned long last_turn_time;
} rotary_encoder_t;

// Game list entry for rotary encoder browsing
typedef struct {
    char core[16];
    char title[128];
    char id_type[16];
    char identifier[128];
    char path[256];
} game_entry_t;

// Configuration
typedef struct {
    gpio_button_t buttons[MAX_BUTTONS];
    int num_buttons;
    rotary_encoder_t encoder;
    int debounce_ms;
    bool show_notifications;
    char games_list_file[256];
} gpio_config_t;

// Global variables
static volatile int keep_running = 1;
static gpio_config_t config;
static game_entry_t games_list[MAX_GAMES_LIST];
static int games_count = 0;
static int current_game_index = 0;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    memset(&config, 0, sizeof(config));
    config.debounce_ms = DEBOUNCE_MS;
    config.show_notifications = true;
    strcpy(config.games_list_file, "/media/fat/utils/favorite_games.txt");
    
    // Initialize encoder
    config.encoder.pin_a = -1;
    config.encoder.pin_b = -1; 
    config.encoder.pin_button = -1;
    config.encoder.enabled = false;
}

// Load configuration file
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("gpio_daemon: Using default configuration\n");
        return;
    }
    
    char line[512];
    int button_index = 0;
    
    while (fgets(line, sizeof(line), fp) && button_index < MAX_BUTTONS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Parse button configuration: gpio_pin,core,id_type,identifier,description
        if (strncmp(line, "button=", 7) == 0) {
            char* config_line = line + 7;
            char* token = strtok(config_line, ",");
            
            if (token) {
                config.buttons[button_index].gpio_pin = atoi(token);
                
                token = strtok(NULL, ",");
                if (token) strncpy(config.buttons[button_index].game_core, token, 15);
                
                token = strtok(NULL, ",");
                if (token) strncpy(config.buttons[button_index].game_id_type, token, 15);
                
                token = strtok(NULL, ",");
                if (token) strncpy(config.buttons[button_index].game_identifier, token, 127);
                
                token = strtok(NULL, "\n");
                if (token) strncpy(config.buttons[button_index].description, token, 63);
                
                config.buttons[button_index].enabled = true;
                button_index++;
            }
        }
        // Parse rotary encoder: encoder=pin_a,pin_b,pin_button
        else if (strncmp(line, "encoder=", 8) == 0) {
            char* config_line = line + 8;
            char* token = strtok(config_line, ",");
            
            if (token) {
                config.encoder.pin_a = atoi(token);
                token = strtok(NULL, ",");
                if (token) {
                    config.encoder.pin_b = atoi(token);
                    token = strtok(NULL, "\n");
                    if (token) {
                        config.encoder.pin_button = atoi(token);
                        config.encoder.enabled = true;
                    }
                }
            }
        }
        // Parse other settings
        else if (strncmp(line, "debounce_ms=", 12) == 0) {
            config.debounce_ms = atoi(line + 12);
        }
        else if (strncmp(line, "games_list_file=", 16) == 0) {
            char* path = line + 16;
            char* newline = strchr(path, '\n');
            if (newline) *newline = '\0';
            strncpy(config.games_list_file, path, 255);
        }
    }
    
    config.num_buttons = button_index;
    fclose(fp);
    
    printf("gpio_daemon: Loaded %d button configurations\n", config.num_buttons);
    if (config.encoder.enabled) {
        printf("gpio_daemon: Rotary encoder enabled on pins %d,%d,%d\n", 
               config.encoder.pin_a, config.encoder.pin_b, config.encoder.pin_button);
    }
}

// Load games list for rotary encoder browsing
void load_games_list() {
    FILE* fp = fopen(config.games_list_file, "r");
    if (!fp) {
        printf("gpio_daemon: Games list file not found: %s\n", config.games_list_file);
        return;
    }
    
    char line[512];
    games_count = 0;
    
    while (fgets(line, sizeof(line), fp) && games_count < MAX_GAMES_LIST) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Format: core,id_type,identifier,title
        char* token = strtok(line, ",");
        if (token) {
            strncpy(games_list[games_count].core, token, 15);
            
            token = strtok(NULL, ",");
            if (token) strncpy(games_list[games_count].id_type, token, 15);
            
            token = strtok(NULL, ",");
            if (token) strncpy(games_list[games_count].identifier, token, 127);
            
            token = strtok(NULL, "\n");
            if (token) strncpy(games_list[games_count].title, token, 127);
            
            games_count++;
        }
    }
    
    fclose(fp);
    printf("gpio_daemon: Loaded %d games for rotary encoder\n", games_count);
}

// Export GPIO pin
bool export_gpio(int pin) {
    char path[64];
    snprintf(path, sizeof(path), "%s/export", GPIO_BASE_PATH);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) return false;
    
    char pin_str[8];
    snprintf(pin_str, sizeof(pin_str), "%d", pin);
    write(fd, pin_str, strlen(pin_str));
    close(fd);
    
    // Set as input
    snprintf(path, sizeof(path), "%s/gpio%d/direction", GPIO_BASE_PATH, pin);
    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "in", 2);
        close(fd);
    }
    
    // Enable pull-up
    snprintf(path, sizeof(path), "%s/gpio%d/edge", GPIO_BASE_PATH, pin);
    fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "falling", 7);
        close(fd);
    }
    
    return true;
}

// Read GPIO pin value
int read_gpio(int pin) {
    char path[64];
    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_BASE_PATH, pin);
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    char value;
    read(fd, &value, 1);
    close(fd);
    
    return (value == '1') ? 1 : 0;
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
    
    char command[256];
    snprintf(command, sizeof(command), "%s:%s:%s:gpio", core, id_type, identifier);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    return written > 0;
}

// Get current time in milliseconds
unsigned long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

// Check button press with debouncing
bool check_button_press(gpio_button_t* button) {
    int value = read_gpio(button->gpio_pin);
    if (value != 0) return false;  // Button not pressed (assuming active low)
    
    unsigned long now = get_time_ms();
    if (now - button->last_press_time < config.debounce_ms) {
        return false;  // Still in debounce period
    }
    
    button->last_press_time = now;
    return true;
}

// Handle rotary encoder
void handle_rotary_encoder() {
    if (!config.encoder.enabled || games_count == 0) return;
    
    int a_state = read_gpio(config.encoder.pin_a);
    int b_state = read_gpio(config.encoder.pin_b);
    
    // Detect rotation
    if (a_state != config.encoder.last_a_state) {
        unsigned long now = get_time_ms();
        
        if (now - config.encoder.last_turn_time > 10) {  // Debounce
            if (a_state == 0) {  // Falling edge on A
                if (b_state == 0) {
                    // Clockwise
                    current_game_index = (current_game_index + 1) % games_count;
                } else {
                    // Counter-clockwise
                    current_game_index = (current_game_index - 1 + games_count) % games_count;
                }
                
                // Show current game
                send_osd_message(games_list[current_game_index].title);
            }
            config.encoder.last_turn_time = now;
        }
        config.encoder.last_a_state = a_state;
    }
    
    // Check select button
    int button_state = read_gpio(config.encoder.pin_button);
    static unsigned long last_button_press = 0;
    static int last_button_state = 1;
    
    if (button_state == 0 && last_button_state == 1) {  // Button pressed
        unsigned long now = get_time_ms();
        if (now - last_button_press > config.debounce_ms) {
            // Launch selected game
            game_entry_t* game = &games_list[current_game_index];
            if (launch_game(game->core, game->id_type, game->identifier)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Launching: %s", game->title);
                send_osd_message(msg);
            }
            last_button_press = now;
        }
    }
    last_button_state = button_state;
}

// Main monitoring loop
void monitor_gpio() {
    printf("gpio_daemon: Monitoring GPIO pins...\n");
    
    while (keep_running) {
        // Check button presses
        for (int i = 0; i < config.num_buttons; i++) {
            if (!config.buttons[i].enabled) continue;
            
            if (check_button_press(&config.buttons[i])) {
                printf("gpio_daemon: Button %d pressed - %s\n", 
                       config.buttons[i].gpio_pin, config.buttons[i].description);
                
                if (launch_game(config.buttons[i].game_core, 
                               config.buttons[i].game_id_type,
                               config.buttons[i].game_identifier)) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "GPIO: %s", config.buttons[i].description);
                    send_osd_message(msg);
                }
            }
        }
        
        // Handle rotary encoder
        handle_rotary_encoder();
        
        usleep(10000);  // 10ms polling interval
    }
}

// Initialize GPIO pins
void init_gpio() {
    // Export button pins
    for (int i = 0; i < config.num_buttons; i++) {
        if (config.buttons[i].enabled) {
            if (!export_gpio(config.buttons[i].gpio_pin)) {
                printf("gpio_daemon: Warning - Failed to export GPIO %d\n", 
                       config.buttons[i].gpio_pin);
            }
        }
    }
    
    // Export encoder pins
    if (config.encoder.enabled) {
        export_gpio(config.encoder.pin_a);
        export_gpio(config.encoder.pin_b);
        export_gpio(config.encoder.pin_button);
        config.encoder.last_a_state = read_gpio(config.encoder.pin_a);
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
    
    printf("gpio_daemon: Starting MiSTer GPIO Game Launcher Daemon\n");
    
    // Load configuration
    load_config();
    load_games_list();
    
    // Check if game launcher service is available
    if (access(GAME_LAUNCHER_FIFO, F_OK) != 0) {
        printf("gpio_daemon: Warning - Game launcher service not available\n");
        printf("gpio_daemon: Please start /media/fat/utils/game_launcher first\n");
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
    
    // Initialize GPIO
    init_gpio();
    
    printf("gpio_daemon: GPIO monitoring active\n");
    
    // Main monitoring loop
    monitor_gpio();
    
    // Cleanup
    printf("gpio_daemon: Shutting down\n");
    unlink(PID_FILE);
    
    return 0;
}