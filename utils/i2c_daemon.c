/*
 * I2C Game Launcher Daemon
 * 
 * Monitors I2C devices for input events and launches games
 * Supports GPIO expanders, rotary encoders, button matrices, and custom controllers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <pthread.h>
#include <time.h>

#define I2C_BUS "/dev/i2c-1"
#define MAX_I2C_DEVICES 16
#define DEBOUNCE_TIME_MS 50
#define ENCODER_DETENT_COUNT 4  // Pulses per detent

// I2C Device Types
typedef enum {
    I2C_DEV_MCP23017,       // 16-bit GPIO expander
    I2C_DEV_PCF8574,        // 8-bit GPIO expander
    I2C_DEV_ROTARY_ENCODER, // Rotary encoder with I2C interface
    I2C_DEV_BUTTON_MATRIX,  // Button matrix controller
    I2C_DEV_CUSTOM          // Custom microcontroller
} i2c_device_type_t;

// Button/Input Configuration
typedef struct {
    int device_id;              // Which I2C device
    int pin_or_button;          // Pin number or button ID
    char system[32];            // Game system
    char id_type[32];           // ID type (serial, title, etc.)
    char identifier[256];       // Game identifier
    char description[256];      // Human-readable description
    int last_state;             // Last button state
    unsigned long last_change;  // Last state change time
    bool enabled;               // Input is enabled
} input_config_t;

// Rotary Encoder Configuration
typedef struct {
    int device_id;              // Which I2C device
    int encoder_id;             // Encoder number on device
    char* game_list[256];       // List of games to cycle through
    int game_count;             // Number of games in list
    int current_position;       // Current encoder position
    int last_position;          // Last encoder position
    bool enabled;               // Encoder is enabled
} encoder_config_t;

// I2C Device Configuration
typedef struct {
    uint8_t address;            // I2C address
    i2c_device_type_t type;     // Device type
    int fd;                     // File descriptor
    char name[64];              // Device name
    bool enabled;               // Device is enabled
    
    // Device-specific data
    union {
        struct {
            uint16_t last_gpio_state;   // Last GPIO state (MCP23017)
            uint8_t iodir_a, iodir_b;   // Direction registers
        } mcp23017;
        
        struct {
            uint8_t last_gpio_state;    // Last GPIO state (PCF8574)
        } pcf8574;
        
        struct {
            int encoder_count;          // Number of encoders
            int32_t positions[8];       // Encoder positions
        } rotary;
        
        struct {
            uint8_t rows, cols;         // Matrix dimensions
            uint8_t last_state[8];      // Last row states
        } matrix;
    } data;
} i2c_device_t;

static volatile int keep_running = 1;
static int i2c_fd = -1;
static i2c_device_t i2c_devices[MAX_I2C_DEVICES];
static int device_count = 0;
static input_config_t inputs[256];
static int input_count = 0;
static encoder_config_t encoders[32];
static int encoder_count = 0;

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

// Initialize I2C bus
bool init_i2c() {
    i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0) {
        perror("i2c_daemon: Failed to open I2C bus");
        return false;
    }
    
    printf("i2c_daemon: I2C bus opened successfully\n");
    return true;
}

// Read from I2C device
bool i2c_read(uint8_t address, uint8_t reg, uint8_t* data, int length) {
    if (ioctl(i2c_fd, I2C_SLAVE, address) < 0) {
        return false;
    }
    
    if (write(i2c_fd, &reg, 1) != 1) {
        return false;
    }
    
    if (read(i2c_fd, data, length) != length) {
        return false;
    }
    
    return true;
}

// Write to I2C device
bool i2c_write(uint8_t address, uint8_t reg, uint8_t* data, int length) {
    if (ioctl(i2c_fd, I2C_SLAVE, address) < 0) {
        return false;
    }
    
    uint8_t buffer[32];
    buffer[0] = reg;
    memcpy(&buffer[1], data, length);
    
    if (write(i2c_fd, buffer, length + 1) != length + 1) {
        return false;
    }
    
    return true;
}

// Initialize MCP23017 GPIO expander
bool init_mcp23017(i2c_device_t* device) {
    uint8_t config;
    
    // Set all pins as inputs with pull-ups
    config = 0xFF;
    if (!i2c_write(device->address, 0x00, &config, 1)) return false; // IODIRA
    if (!i2c_write(device->address, 0x01, &config, 1)) return false; // IODIRB
    
    // Enable pull-ups
    if (!i2c_write(device->address, 0x0C, &config, 1)) return false; // GPPUA
    if (!i2c_write(device->address, 0x0D, &config, 1)) return false; // GPPUB
    
    // Read initial state
    uint8_t gpio_a, gpio_b;
    if (!i2c_read(device->address, 0x12, &gpio_a, 1)) return false; // GPIOA
    if (!i2c_read(device->address, 0x13, &gpio_b, 1)) return false; // GPIOB
    
    device->data.mcp23017.last_gpio_state = (gpio_b << 8) | gpio_a;
    
    printf("i2c_daemon: MCP23017 at 0x%02X initialized\n", device->address);
    return true;
}

// Poll MCP23017 for changes
void poll_mcp23017(i2c_device_t* device) {
    uint8_t gpio_a, gpio_b;
    
    if (!i2c_read(device->address, 0x12, &gpio_a, 1)) return; // GPIOA
    if (!i2c_read(device->address, 0x13, &gpio_b, 1)) return; // GPIOB
    
    uint16_t current_state = (gpio_b << 8) | gpio_a;
    uint16_t last_state = device->data.mcp23017.last_gpio_state;
    
    if (current_state != last_state) {
        // Check each pin for changes
        for (int pin = 0; pin < 16; pin++) {
            bool current_pin = (current_state >> pin) & 1;
            bool last_pin = (last_state >> pin) & 1;
            
            if (current_pin != last_pin) {
                printf("i2c_daemon: MCP23017 pin %d changed to %d\n", pin, current_pin);
                
                // Find corresponding input configuration
                for (int i = 0; i < input_count; i++) {
                    if (inputs[i].device_id == device - i2c_devices && 
                        inputs[i].pin_or_button == pin && 
                        inputs[i].enabled) {
                        
                        // Button pressed (active low with pull-up)
                        if (!current_pin && last_pin) {
                            unsigned long now = get_time_ms();
                            if ((now - inputs[i].last_change) > DEBOUNCE_TIME_MS) {
                                inputs[i].last_change = now;
                                
                                printf("i2c_daemon: Button press: %s\n", inputs[i].description);
                                
                                // TODO: Send game launch command
                                // send_game_launch_command(inputs[i].system, inputs[i].id_type, inputs[i].identifier);
                            }
                        }
                        break;
                    }
                }
            }
        }
        
        device->data.mcp23017.last_gpio_state = current_state;
    }
}

// Initialize custom rotary encoder controller
bool init_rotary_encoder(i2c_device_t* device) {
    // Send initialization command to custom controller
    uint8_t init_cmd = 0x01; // Initialize command
    if (!i2c_write(device->address, 0x00, &init_cmd, 1)) return false;
    
    // Read number of encoders
    uint8_t encoder_count_byte;
    if (!i2c_read(device->address, 0x01, &encoder_count_byte, 1)) return false;
    
    device->data.rotary.encoder_count = encoder_count_byte;
    
    // Initialize encoder positions
    for (int i = 0; i < device->data.rotary.encoder_count; i++) {
        device->data.rotary.positions[i] = 0;
    }
    
    printf("i2c_daemon: Rotary encoder controller at 0x%02X initialized (%d encoders)\n", 
           device->address, device->data.rotary.encoder_count);
    return true;
}

// Poll rotary encoder for changes
void poll_rotary_encoder(i2c_device_t* device) {
    for (int enc = 0; enc < device->data.rotary.encoder_count; enc++) {
        // Read encoder position (4 bytes per encoder)
        uint8_t pos_bytes[4];
        if (!i2c_read(device->address, 0x10 + (enc * 4), pos_bytes, 4)) continue;
        
        int32_t current_pos = (pos_bytes[3] << 24) | (pos_bytes[2] << 16) | 
                             (pos_bytes[1] << 8) | pos_bytes[0];
        
        int32_t last_pos = device->data.rotary.positions[enc];
        
        if (current_pos != last_pos) {
            // Calculate detent movement
            int detent_change = (current_pos - last_pos) / ENCODER_DETENT_COUNT;
            
            if (detent_change != 0) {
                printf("i2c_daemon: Encoder %d moved by %d detents (pos: %d)\n", 
                       enc, detent_change, current_pos);
                
                // Find corresponding encoder configuration
                for (int i = 0; i < encoder_count; i++) {
                    if (encoders[i].device_id == device - i2c_devices && 
                        encoders[i].encoder_id == enc && 
                        encoders[i].enabled) {
                        
                        // Update game selection
                        encoders[i].current_position += detent_change;
                        
                        // Wrap around
                        if (encoders[i].current_position >= encoders[i].game_count) {
                            encoders[i].current_position = 0;
                        } else if (encoders[i].current_position < 0) {
                            encoders[i].current_position = encoders[i].game_count - 1;
                        }
                        
                        printf("i2c_daemon: Selected game: %s\n", 
                               encoders[i].game_list[encoders[i].current_position]);
                        
                        // TODO: Update OSD display with current selection
                        break;
                    }
                }
            }
            
            device->data.rotary.positions[enc] = current_pos;
        }
    }
}

// Load I2C device configuration
bool load_i2c_config(const char* config_path) {
    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        printf("i2c_daemon: Config file not found: %s\n", config_path);
        return false;
    }
    
    char line[512];
    
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Parse device configuration
        if (strncmp(line, "device:", 7) == 0) {
            // device:0x20,MCP23017,GPIO Expander 1
            char* addr_str = strtok(line + 7, ",");
            char* type_str = strtok(NULL, ",");
            char* name_str = strtok(NULL, ",\n");
            
            if (addr_str && type_str && device_count < MAX_I2C_DEVICES) {
                i2c_device_t* dev = &i2c_devices[device_count];
                
                dev->address = strtol(addr_str, NULL, 16);
                
                if (strcmp(type_str, "MCP23017") == 0) {
                    dev->type = I2C_DEV_MCP23017;
                } else if (strcmp(type_str, "PCF8574") == 0) {
                    dev->type = I2C_DEV_PCF8574;
                } else if (strcmp(type_str, "ROTARY") == 0) {
                    dev->type = I2C_DEV_ROTARY_ENCODER;
                } else {
                    dev->type = I2C_DEV_CUSTOM;
                }
                
                if (name_str) {
                    strncpy(dev->name, name_str, sizeof(dev->name) - 1);
                }
                
                dev->enabled = true;
                device_count++;
            }
        }
        // Parse button configuration
        else if (strncmp(line, "button:", 7) == 0) {
            // button:0,5,PSX,serial,SLUS-00067,Castlevania SOTN
            char* dev_str = strtok(line + 7, ",");
            char* pin_str = strtok(NULL, ",");
            char* sys_str = strtok(NULL, ",");
            char* type_str = strtok(NULL, ",");
            char* id_str = strtok(NULL, ",");
            char* desc_str = strtok(NULL, ",\n");
            
            if (dev_str && pin_str && sys_str && type_str && id_str && input_count < 256) {
                input_config_t* input = &inputs[input_count];
                
                input->device_id = atoi(dev_str);
                input->pin_or_button = atoi(pin_str);
                strncpy(input->system, sys_str, sizeof(input->system) - 1);
                strncpy(input->id_type, type_str, sizeof(input->id_type) - 1);
                strncpy(input->identifier, id_str, sizeof(input->identifier) - 1);
                
                if (desc_str) {
                    strncpy(input->description, desc_str, sizeof(input->description) - 1);
                }
                
                input->enabled = true;
                input->last_change = get_time_ms();
                input_count++;
            }
        }
    }
    
    fclose(fp);
    printf("i2c_daemon: Loaded %d devices and %d inputs\n", device_count, input_count);
    return true;
}

// Initialize all I2C devices
bool init_all_devices() {
    for (int i = 0; i < device_count; i++) {
        i2c_device_t* dev = &i2c_devices[i];
        
        switch (dev->type) {
            case I2C_DEV_MCP23017:
                if (!init_mcp23017(dev)) {
                    printf("i2c_daemon: Failed to initialize MCP23017 at 0x%02X\n", dev->address);
                    dev->enabled = false;
                }
                break;
                
            case I2C_DEV_ROTARY_ENCODER:
                if (!init_rotary_encoder(dev)) {
                    printf("i2c_daemon: Failed to initialize rotary encoder at 0x%02X\n", dev->address);
                    dev->enabled = false;
                }
                break;
                
            default:
                printf("i2c_daemon: Device type not implemented: %d\n", dev->type);
                dev->enabled = false;
                break;
        }
    }
    
    return true;
}

// Main polling loop
void* polling_thread(void* arg) {
    while (keep_running) {
        for (int i = 0; i < device_count; i++) {
            if (!i2c_devices[i].enabled) continue;
            
            switch (i2c_devices[i].type) {
                case I2C_DEV_MCP23017:
                    poll_mcp23017(&i2c_devices[i]);
                    break;
                    
                case I2C_DEV_ROTARY_ENCODER:
                    poll_rotary_encoder(&i2c_devices[i]);
                    break;
                    
                default:
                    break;
            }
        }
        
        usleep(10000); // Poll every 10ms
    }
    
    return NULL;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("i2c_daemon: Starting I2C Game Launcher Daemon\n");
    
    // Initialize I2C
    if (!init_i2c()) {
        return 1;
    }
    
    // Load configuration
    if (!load_i2c_config("/media/fat/utils/configs/i2c_devices.conf")) {
        close(i2c_fd);
        return 1;
    }
    
    // Initialize devices
    if (!init_all_devices()) {
        close(i2c_fd);
        return 1;
    }
    
    // Start polling thread
    pthread_t poll_thread;
    if (pthread_create(&poll_thread, NULL, polling_thread, NULL) != 0) {
        perror("i2c_daemon: Failed to create polling thread");
        close(i2c_fd);
        return 1;
    }
    
    // Main loop
    while (keep_running) {
        sleep(1);
    }
    
    // Cleanup
    printf("i2c_daemon: Shutting down\n");
    pthread_join(poll_thread, NULL);
    close(i2c_fd);
    
    return 0;
}

/*
 * Example I2C configuration file (/media/fat/utils/configs/i2c_devices.conf):
 * 
 * # I2C Device Configuration
 * # Format: device:address,type,name
 * device:0x20,MCP23017,Button Panel 1
 * device:0x21,MCP23017,Button Panel 2
 * device:0x30,ROTARY,Game Selector
 * 
 * # Button Configuration
 * # Format: button:device_id,pin,system,id_type,identifier,description
 * button:0,0,PSX,serial,SLUS-00067,Castlevania SOTN
 * button:0,1,Saturn,serial,T-8109H,Panzer Dragoon Saga
 * button:0,2,MegaCD,title,Sonic CD,Sonic CD
 * button:1,0,PSX,title,random,Random PSX Game
 * button:1,1,Saturn,title,random,Random Saturn Game
 * 
 * # Rotary Encoder Configuration
 * # Format: encoder:device_id,encoder_id,game_list_file
 * encoder:2,0,/media/fat/utils/configs/psx_games.txt
 */