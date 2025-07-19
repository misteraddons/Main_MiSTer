/*
 * GPIO Game Launcher Daemon
 * 
 * Monitors GPIO pins for button presses and launches predefined games
 * Useful for arcade buttons, rotary encoders, or dedicated game shortcuts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <pthread.h>

#define MAX_GPIO_PINS 32
#define DEBOUNCE_TIME_MS 50

static volatile int keep_running = 1;

// GPIO pin configuration
typedef struct {
    int pin;                    // GPIO pin number
    int fd;                     // File descriptor for pin
    char system[32];            // Game system
    char id_type[32];           // ID type (serial, title, etc.)
    char identifier[256];       // Game identifier
    char description[256];      // Human-readable description
    int last_state;             // Last pin state
    unsigned long last_change;  // Last state change time
    bool enabled;               // Pin is enabled
} gpio_pin_t;

static gpio_pin_t gpio_pins[MAX_GPIO_PINS];
static int gpio_count = 0;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Get current time in milliseconds
unsigned long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Export GPIO pin
bool export_gpio_pin(int pin) {
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        return false;
    }
    
    char pin_str[16];
    snprintf(pin_str, sizeof(pin_str), "%d", pin);
    write(fd, pin_str, strlen(pin_str));
    close(fd);
    
    // Set direction to input
    char direction_path[64];
    snprintf(direction_path, sizeof(direction_path), "/sys/class/gpio/gpio%d/direction", pin);
    
    fd = open(direction_path, O_WRONLY);
    if (fd < 0) {
        return false;
    }
    
    write(fd, "in", 2);
    close(fd);
    
    // Set edge detection
    char edge_path[64];
    snprintf(edge_path, sizeof(edge_path), "/sys/class/gpio/gpio%d/edge", pin);
    
    fd = open(edge_path, O_WRONLY);
    if (fd >= 0) {
        write(fd, "rising", 6);  // Trigger on button press
        close(fd);
    }
    
    return true;
}

// Load GPIO configuration
bool load_gpio_config(const char* config_path) {
    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        printf("gpio_daemon: Config file not found: %s\n", config_path);
        return false;
    }
    
    char line[512];
    
    while (fgets(line, sizeof(line), fp) && gpio_count < MAX_GPIO_PINS) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Parse line: pin,system,id_type,identifier,description
        char* token = strtok(line, ",");
        if (!token) continue;
        
        gpio_pin_t* pin = &gpio_pins[gpio_count];
        
        // Pin number
        pin->pin = atoi(token);
        
        // System
        token = strtok(NULL, ",");
        if (token) {
            strncpy(pin->system, token, sizeof(pin->system) - 1);
        }
        
        // ID type
        token = strtok(NULL, ",");
        if (token) {
            strncpy(pin->id_type, token, sizeof(pin->id_type) - 1);
        }
        
        // Identifier
        token = strtok(NULL, ",");
        if (token) {
            strncpy(pin->identifier, token, sizeof(pin->identifier) - 1);
        }
        
        // Description
        token = strtok(NULL, ",\n");
        if (token) {
            strncpy(pin->description, token, sizeof(pin->description) - 1);
        }
        
        // Initialize pin
        if (export_gpio_pin(pin->pin)) {
            char value_path[64];
            snprintf(value_path, sizeof(value_path), "/sys/class/gpio/gpio%d/value", pin->pin);
            
            pin->fd = open(value_path, O_RDONLY);
            if (pin->fd >= 0) {
                pin->last_state = 0;
                pin->last_change = get_time_ms();
                pin->enabled = true;
                
                printf("gpio_daemon: Configured pin %d: %s (%s %s)\n", 
                       pin->pin, pin->description, pin->system, pin->identifier);
                
                gpio_count++;
            }
        }
    }
    
    fclose(fp);
    printf("gpio_daemon: Loaded %d GPIO pins\n", gpio_count);
    return gpio_count > 0;
}

// Send game launch command
bool send_game_launch_command(const char* system, const char* id_type, const char* identifier, int pin) {
    char command[512];
    snprintf(command, sizeof(command), 
             "{"
             "\"command\": \"find_game\", "
             "\"system\": \"%s\", "
             "\"id_type\": \"%s\", "
             "\"identifier\": \"%s\", "
             "\"source\": \"gpio\", "
             "\"auto_launch\": true, "
             "\"source_data\": {\"gpio_pin\": %d}"
             "}",
             system, id_type, identifier, pin);
    
    int fd = open("/dev/MiSTer_game_launcher", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    ssize_t written = write(fd, command, strlen(command));
    write(fd, "\n", 1);
    close(fd);
    
    return written > 0;
}

// Read GPIO pin state
int read_gpio_state(int fd) {
    char value[4];
    lseek(fd, 0, SEEK_SET);
    
    if (read(fd, value, sizeof(value)) > 0) {
        return (value[0] == '1') ? 1 : 0;
    }
    
    return -1;
}

// GPIO monitoring thread
void* gpio_monitor_thread(void* arg) {
    struct pollfd poll_fds[MAX_GPIO_PINS];
    
    // Setup poll descriptors
    for (int i = 0; i < gpio_count; i++) {
        poll_fds[i].fd = gpio_pins[i].fd;
        poll_fds[i].events = POLLPRI | POLLERR;
    }
    
    while (keep_running) {
        // Poll for events
        int poll_result = poll(poll_fds, gpio_count, 100);
        
        if (poll_result > 0) {
            for (int i = 0; i < gpio_count; i++) {
                if (poll_fds[i].revents & POLLPRI) {
                    // Read pin state
                    int state = read_gpio_state(gpio_pins[i].fd);
                    unsigned long now = get_time_ms();
                    
                    // Check for state change with debouncing
                    if (state != gpio_pins[i].last_state && 
                        (now - gpio_pins[i].last_change) > DEBOUNCE_TIME_MS) {
                        
                        gpio_pins[i].last_state = state;
                        gpio_pins[i].last_change = now;
                        
                        // Trigger on rising edge (button press)
                        if (state == 1) {
                            printf("gpio_daemon: Button press on pin %d: %s\n", 
                                   gpio_pins[i].pin, gpio_pins[i].description);
                            
                            // Send launch command
                            if (send_game_launch_command(gpio_pins[i].system, 
                                                        gpio_pins[i].id_type,
                                                        gpio_pins[i].identifier,
                                                        gpio_pins[i].pin)) {
                                printf("gpio_daemon: Sent launch command for %s\n", 
                                       gpio_pins[i].description);
                            } else {
                                printf("gpio_daemon: Failed to send launch command\n");
                            }
                        }
                    }
                }
            }
        }
    }
    
    return NULL;
}

// Cleanup GPIO pins
void cleanup_gpio_pins() {
    for (int i = 0; i < gpio_count; i++) {
        if (gpio_pins[i].fd >= 0) {
            close(gpio_pins[i].fd);
        }
        
        // Unexport pin
        int fd = open("/sys/class/gpio/unexport", O_WRONLY);
        if (fd >= 0) {
            char pin_str[16];
            snprintf(pin_str, sizeof(pin_str), "%d", gpio_pins[i].pin);
            write(fd, pin_str, strlen(pin_str));
            close(fd);
        }
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("gpio_daemon: Starting GPIO Game Launcher Daemon\n");
    
    // Load GPIO configuration
    if (!load_gpio_config("/media/fat/utils/configs/gpio_mappings.conf")) {
        fprintf(stderr, "gpio_daemon: Failed to load GPIO configuration\n");
        return 1;
    }
    
    // Start monitoring thread
    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, gpio_monitor_thread, NULL) != 0) {
        fprintf(stderr, "gpio_daemon: Failed to create monitoring thread\n");
        cleanup_gpio_pins();
        return 1;
    }
    
    // Main loop
    while (keep_running) {
        sleep(1);
    }
    
    // Cleanup
    printf("gpio_daemon: Shutting down\n");
    pthread_join(monitor_thread, NULL);
    cleanup_gpio_pins();
    
    return 0;
}

/*
 * Example GPIO configuration file (/media/fat/utils/configs/gpio_mappings.conf):
 * 
 * # Pin,System,IDType,Identifier,Description
 * 18,PSX,serial,SLUS-00067,Castlevania SOTN Button
 * 19,Saturn,serial,T-8109H,Panzer Dragoon Saga Button
 * 20,MegaCD,title,Sonic CD,Sonic CD Quick Launch
 * 21,PCECD,serial,TJCD3001,Rondo of Blood Button
 * 22,PSX,title,random,Random PSX Game
 * 23,Saturn,title,random,Random Saturn Game
 */