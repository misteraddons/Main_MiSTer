/*
 * MiSTer NFC Daemon
 * 
 * NFC input source for the game launcher system using PN532
 * Supports both I2C and UART connections with auto-detection
 * 
 * Features:
 * - Auto-detect PN532 across multiple interfaces
 * - Tap mode (launch once) and Hold mode (exit on tag removal)
 * - Minimal tag data storage with centralized GameDB lookup
 * - Tag registry for management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <time.h>
#include <errno.h>
#include <cstdint>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define CONFIG_FILE "/media/fat/utils/nfc_daemon.conf"
#define TAG_REGISTRY_FILE "/media/fat/utils/nfc_tags.json"
#define PID_FILE "/tmp/nfc_daemon.pid"

// PN532 Configuration
#define PN532_I2C_ADDRESS 0x24
#define PN532_UART_BAUD 115200
#define NFC_POLL_INTERVAL_MS 250
#define TAG_REMOVAL_TIMEOUT_SEC 3
#define TAG_COOLDOWN_SEC 2

// PN532 Commands
#define PN532_COMMAND_GETFIRMWAREVERSION 0x02
#define PN532_COMMAND_SAMCONFIGURATION 0x14
#define PN532_COMMAND_INLISTPASSIVETARGET 0x4A
#define PN532_COMMAND_INDATAEXCHANGE 0x40

// NFC Tag Data Format (32 bytes total)
typedef struct __attribute__((packed)) {
    char magic[4];       // "NFC1"
    char core[8];        // "PSX\0\0\0\0\0"
    char game_id[16];    // "SLUS-00067\0\0\0\0\0"
    uint8_t tag_type;    // SINGLE_GAME, PLAYLIST, etc.
    uint8_t reserved[3]; // Future use
} nfc_tag_data_t;

// Tag Types
typedef enum {
    SINGLE_GAME = 0,
    PLAYLIST = 1,
    RANDOM_GAME = 2,
    LAST_PLAYED = 3,
    FAVORITES = 4
} nfc_tag_type_t;

// NFC Modes
typedef enum {
    NFC_MODE_TAP = 0,    // Launch on tap, ignore removal
    NFC_MODE_HOLD = 1    // Launch on tap, exit on removal
} nfc_mode_t;

// Interface Types
typedef enum {
    INTERFACE_I2C = 0,
    INTERFACE_UART = 1
} interface_type_t;

// PN532 Interface Configuration
typedef struct {
    char device_path[64];
    interface_type_t type;
    union {
        struct { int address; } i2c;
        struct { int baud; } uart;
    } config;
} pn532_interface_t;

// NFC Configuration
typedef struct {
    int poll_interval_ms;
    bool show_notifications;
    nfc_mode_t mode;
    int tag_removal_timeout_sec;
    int tag_cooldown_sec;
    char interface_path[64];
    interface_type_t interface_type;
} nfc_config_t;

// Tag State
typedef struct {
    char uid[32];
    char core[16];
    char game_id[32];
    time_t first_detected;
    time_t last_seen;
    bool game_launched;
    char launched_game[128];
} nfc_tag_state_t;

// Global variables
static volatile int keep_running = 1;
static nfc_config_t config;
static nfc_tag_state_t current_tag = {0};
static int pn532_fd = -1;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    config.poll_interval_ms = NFC_POLL_INTERVAL_MS;
    config.show_notifications = true;
    config.mode = NFC_MODE_TAP;
    config.tag_removal_timeout_sec = TAG_REMOVAL_TIMEOUT_SEC;
    config.tag_cooldown_sec = TAG_COOLDOWN_SEC;
    strcpy(config.interface_path, "auto");
    config.interface_type = INTERFACE_I2C;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("nfc_daemon: Using default configuration\n");
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
        
        if (strcmp(key, "poll_interval_ms") == 0) {
            config.poll_interval_ms = atoi(value);
        } else if (strcmp(key, "show_notifications") == 0) {
            config.show_notifications = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "mode") == 0) {
            config.mode = (strcmp(value, "hold") == 0) ? NFC_MODE_HOLD : NFC_MODE_TAP;
        } else if (strcmp(key, "tag_removal_timeout_sec") == 0) {
            config.tag_removal_timeout_sec = atoi(value);
        } else if (strcmp(key, "tag_cooldown_sec") == 0) {
            config.tag_cooldown_sec = atoi(value);
        } else if (strcmp(key, "interface") == 0) {
            strncpy(config.interface_path, value, sizeof(config.interface_path) - 1);
        }
    }
    
    fclose(fp);
    printf("nfc_daemon: Configuration loaded\n");
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
    snprintf(command, sizeof(command), "%s:%s:%s:nfc", system, id_type, identifier);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    return written > 0;
}

// Calculate checksum for PN532 frame
uint8_t calculate_checksum(const uint8_t* data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return (~sum) + 1;
}

// Send PN532 command (simplified for demonstration)
bool pn532_send_command(uint8_t command, const uint8_t* data, size_t data_len, uint8_t* response, size_t* response_len) {
    if (pn532_fd < 0) return false;
    
    // Build PN532 frame
    uint8_t frame[256];
    size_t frame_len = 0;
    
    if (config.interface_type == INTERFACE_I2C) {
        // I2C frame format
        frame[frame_len++] = 0x00;  // Preamble
        frame[frame_len++] = 0x00;  // Start code
        frame[frame_len++] = 0xFF;  // Start code
        frame[frame_len++] = data_len + 2;  // Length
        frame[frame_len++] = (~(data_len + 2)) + 1;  // Length checksum
        frame[frame_len++] = 0xD4;  // Direction (host to PN532)
        frame[frame_len++] = command;
        
        // Add data
        for (size_t i = 0; i < data_len; i++) {
            frame[frame_len++] = data[i];
        }
        
        // Add data checksum
        uint8_t checksum = 0xD4 + command;
        for (size_t i = 0; i < data_len; i++) {
            checksum += data[i];
        }
        frame[frame_len++] = (~checksum) + 1;
        frame[frame_len++] = 0x00;  // Postamble
        
        // Send via I2C
        if (write(pn532_fd, frame, frame_len) != (ssize_t)frame_len) {
            return false;
        }
        
    } else {
        // UART frame format (similar but with different framing)
        frame[frame_len++] = 0x00;  // Preamble
        frame[frame_len++] = 0x00;  // Start code
        frame[frame_len++] = 0xFF;  // Start code
        frame[frame_len++] = data_len + 2;  // Length
        frame[frame_len++] = (~(data_len + 2)) + 1;  // Length checksum
        frame[frame_len++] = 0xD4;  // Direction
        frame[frame_len++] = command;
        
        for (size_t i = 0; i < data_len; i++) {
            frame[frame_len++] = data[i];
        }
        
        uint8_t checksum = 0xD4 + command;
        for (size_t i = 0; i < data_len; i++) {
            checksum += data[i];
        }
        frame[frame_len++] = (~checksum) + 1;
        frame[frame_len++] = 0x00;
        
        if (write(pn532_fd, frame, frame_len) != (ssize_t)frame_len) {
            return false;
        }
    }
    
    // Read response (simplified - should handle ACK, then actual response)
    usleep(100000);  // Wait 100ms for response
    
    uint8_t resp_buffer[256];
    ssize_t bytes_read = read(pn532_fd, resp_buffer, sizeof(resp_buffer));
    
    if (bytes_read > 6) {
        // Basic response parsing - extract data portion
        *response_len = bytes_read - 6;  // Remove frame overhead
        memcpy(response, resp_buffer + 6, *response_len);
        return true;
    }
    
    return false;
}

// Test PN532 interface
bool test_pn532_interface(pn532_interface_t* interface) {
    int fd = -1;
    
    if (interface->type == INTERFACE_I2C) {
        fd = open(interface->device_path, O_RDWR);
        if (fd < 0) return false;
        
        if (ioctl(fd, I2C_SLAVE, interface->config.i2c.address) < 0) {
            close(fd);
            return false;
        }
    } else {
        fd = open(interface->device_path, O_RDWR | O_NOCTTY);
        if (fd < 0) return false;
        
        // Configure UART (simplified)
        // In real implementation, would set baud rate, parity, etc.
    }
    
    // Try to get firmware version
    uint8_t response[4];
    size_t response_len;
    
    // Temporarily set global fd for command function
    int old_fd = pn532_fd;
    pn532_fd = fd;
    config.interface_type = interface->type;
    
    bool success = pn532_send_command(PN532_COMMAND_GETFIRMWAREVERSION, NULL, 0, response, &response_len);
    
    pn532_fd = old_fd;
    close(fd);
    
    return success && response_len >= 4;
}

// Auto-detect PN532
bool auto_detect_pn532() {
    pn532_interface_t interfaces[6];
    
    // Initialize interfaces array
    strcpy(interfaces[0].device_path, "/dev/ttyUSB0");
    interfaces[0].type = INTERFACE_UART;
    interfaces[0].config.uart.baud = PN532_UART_BAUD;
    
    strcpy(interfaces[1].device_path, "/dev/ttyUSB1");
    interfaces[1].type = INTERFACE_UART;
    interfaces[1].config.uart.baud = PN532_UART_BAUD;
    
    strcpy(interfaces[2].device_path, "/dev/i2c-0");
    interfaces[2].type = INTERFACE_I2C;
    interfaces[2].config.i2c.address = PN532_I2C_ADDRESS;
    
    strcpy(interfaces[3].device_path, "/dev/ttyS0");
    interfaces[3].type = INTERFACE_UART;
    interfaces[3].config.uart.baud = PN532_UART_BAUD;
    
    strcpy(interfaces[4].device_path, "/dev/ttyS1");
    interfaces[4].type = INTERFACE_UART;
    interfaces[4].config.uart.baud = PN532_UART_BAUD;
    
    strcpy(interfaces[5].device_path, "/dev/i2c-1");
    interfaces[5].type = INTERFACE_I2C;
    interfaces[5].config.i2c.address = PN532_I2C_ADDRESS;
    
    for (size_t i = 0; i < sizeof(interfaces)/sizeof(interfaces[0]); i++) {
        printf("nfc_daemon: Testing PN532 on %s...\n", interfaces[i].device_path);
        
        if (test_pn532_interface(&interfaces[i])) {
            printf("nfc_daemon: PN532 found on %s\n", interfaces[i].device_path);
            
            // Open the interface
            if (interfaces[i].type == INTERFACE_I2C) {
                pn532_fd = open(interfaces[i].device_path, O_RDWR);
                if (pn532_fd >= 0) {
                    ioctl(pn532_fd, I2C_SLAVE, interfaces[i].config.i2c.address);
                }
            } else {
                pn532_fd = open(interfaces[i].device_path, O_RDWR | O_NOCTTY);
                // Configure UART parameters here
            }
            
            if (pn532_fd >= 0) {
                strncpy(config.interface_path, interfaces[i].device_path, sizeof(config.interface_path));
                config.interface_type = interfaces[i].type;
                return true;
            }
        }
    }
    
    return false;
}

// Initialize PN532
bool init_pn532() {
    if (strcmp(config.interface_path, "auto") == 0) {
        return auto_detect_pn532();
    } else {
        // Use specified interface
        printf("nfc_daemon: Using configured interface: %s\n", config.interface_path);
        
        // Determine interface type from path
        if (strstr(config.interface_path, "i2c")) {
            config.interface_type = INTERFACE_I2C;
            pn532_fd = open(config.interface_path, O_RDWR);
            if (pn532_fd >= 0) {
                ioctl(pn532_fd, I2C_SLAVE, PN532_I2C_ADDRESS);
            }
        } else {
            config.interface_type = INTERFACE_UART;
            pn532_fd = open(config.interface_path, O_RDWR | O_NOCTTY);
        }
        
        return pn532_fd >= 0;
    }
}

// Configure PN532 for tag detection
bool configure_pn532() {
    if (pn532_fd < 0) return false;
    
    // Configure SAM (Security Access Module)
    uint8_t sam_config[] = {0x01, 0x14, 0x01};  // Normal mode, timeout 50ms * 20 = 1 second
    uint8_t response[16];
    size_t response_len;
    
    if (!pn532_send_command(PN532_COMMAND_SAMCONFIGURATION, sam_config, sizeof(sam_config), response, &response_len)) {
        printf("nfc_daemon: Failed to configure SAM\n");
        return false;
    }
    
    printf("nfc_daemon: PN532 configured for tag detection\n");
    return true;
}

// Detect NFC tags
bool detect_nfc_tag(nfc_tag_data_t* tag_data) {
    if (pn532_fd < 0) return false;
    
    // InListPassiveTarget command for ISO14443 Type A
    uint8_t target_data[] = {0x01, 0x00};  // Max targets=1, Type A
    uint8_t response[64];
    size_t response_len;
    
    if (!pn532_send_command(PN532_COMMAND_INLISTPASSIVETARGET, target_data, sizeof(target_data), response, &response_len)) {
        return false;
    }
    
    if (response_len < 6 || response[0] != 0x01) {
        return false; // No targets found
    }
    
    // Tag detected - now read NDEF data
    // This is simplified - real implementation would:
    // 1. Read NDEF capability container
    // 2. Find and read NDEF message
    // 3. Parse NDEF records
    
    // For demonstration, simulate reading our custom format
    uint8_t read_data[] = {0x01, 0x30, 0x04};  // Read block 4 (where our data starts)
    if (!pn532_send_command(PN532_COMMAND_INDATAEXCHANGE, read_data, sizeof(read_data), response, &response_len)) {
        return false;
    }
    
    if (response_len >= sizeof(nfc_tag_data_t)) {
        memcpy(tag_data, response + 1, sizeof(nfc_tag_data_t));  // Skip status byte
        
        // Validate magic number
        if (memcmp(tag_data->magic, "NFC1", 4) == 0) {
            return true;
        }
    }
    
    return false;
}

// Generate tag UID for tracking
void generate_tag_uid(char* uid, size_t uid_size) {
    // In real implementation, extract actual UID from PN532 response
    // For now, use a simple hash of the tag data
    snprintf(uid, uid_size, "SIM_%08X", (unsigned int)time(NULL));
}

// Process detected NFC tag
void process_nfc_tag(nfc_tag_data_t* tag_data) {
    time_t current_time = time(NULL);
    
    // Generate UID for this tag
    char tag_uid[32];
    generate_tag_uid(tag_uid, sizeof(tag_uid));
    
    // Check if this is the same tag as before (cooldown check)
    if (strcmp(tag_uid, current_tag.uid) == 0 && 
        (current_time - current_tag.last_seen) < config.tag_cooldown_sec) {
        current_tag.last_seen = current_time;
        return; // Same tag within cooldown period
    }
    
    // New tag or cooldown expired
    if (strcmp(tag_uid, current_tag.uid) != 0) {
        // Reset tag state for new tag
        memset(&current_tag, 0, sizeof(current_tag));
        strncpy(current_tag.uid, tag_uid, sizeof(current_tag.uid));
        strncpy(current_tag.core, tag_data->core, sizeof(current_tag.core));
        strncpy(current_tag.game_id, tag_data->game_id, sizeof(current_tag.game_id));
        current_tag.first_detected = current_time;
    }
    
    current_tag.last_seen = current_time;
    
    printf("nfc_daemon: Processing NFC tag: %s:%s\n", tag_data->core, tag_data->game_id);
    
    if (config.mode == NFC_MODE_TAP) {
        // Tap mode: Only launch if this tag hasn't launched a game yet
        if (!current_tag.game_launched) {
            char msg[256];
            snprintf(msg, sizeof(msg), "NFC: Loading %s game", tag_data->core);
            send_osd_message(msg);
            
            // Determine ID type (serial vs title)
            const char* id_type = "title";
            if ((strncmp(tag_data->game_id, "SLUS", 4) == 0 && tag_data->game_id[4] == '-') ||
                (strncmp(tag_data->game_id, "SCUS", 4) == 0 && tag_data->game_id[4] == '-') ||
                (strncmp(tag_data->game_id, "SCES", 4) == 0 && tag_data->game_id[4] == '-') ||
                (strncmp(tag_data->game_id, "T-", 2) == 0)) {
                id_type = "serial";
            }
            
            // Send to game launcher service
            if (send_game_launcher_command(tag_data->core, id_type, tag_data->game_id)) {
                printf("nfc_daemon: Sent request to game launcher: %s:%s:%s\n", 
                       tag_data->core, id_type, tag_data->game_id);
                current_tag.game_launched = true;
                snprintf(current_tag.launched_game, sizeof(current_tag.launched_game), 
                         "%s:%s", tag_data->core, tag_data->game_id);
            } else {
                send_osd_message("Game launcher service unavailable");
            }
        }
    } else {
        // Hold mode: Launch game and monitor for removal
        if (!current_tag.game_launched) {
            char msg[256];
            snprintf(msg, sizeof(msg), "NFC: Loading %s game (Hold mode)", tag_data->core);
            send_osd_message(msg);
            
            const char* id_type = "title";
            if ((strncmp(tag_data->game_id, "SLUS", 4) == 0 && tag_data->game_id[4] == '-') ||
                (strncmp(tag_data->game_id, "SCUS", 4) == 0 && tag_data->game_id[4] == '-') ||
                (strncmp(tag_data->game_id, "SCES", 4) == 0 && tag_data->game_id[4] == '-') ||
                (strncmp(tag_data->game_id, "T-", 2) == 0)) {
                id_type = "serial";
            }
            
            if (send_game_launcher_command(tag_data->core, id_type, tag_data->game_id)) {
                current_tag.game_launched = true;
                snprintf(current_tag.launched_game, sizeof(current_tag.launched_game), 
                         "%s:%s", tag_data->core, tag_data->game_id);
            } else {
                send_osd_message("Game launcher service unavailable");
            }
        }
    }
}

// Check for tag removal (Hold mode)
void check_tag_removal() {
    if (config.mode != NFC_MODE_HOLD || !current_tag.game_launched) {
        return;
    }
    
    time_t current_time = time(NULL);
    if ((current_time - current_tag.last_seen) > config.tag_removal_timeout_sec) {
        printf("nfc_daemon: Tag removed in hold mode - exiting game\n");
        send_osd_message("NFC tag removed - Exiting game");
        
        // Send exit command to MiSTer (implementation depends on MiSTer capabilities)
        int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            write(fd, "exit_game", 9);
            close(fd);
        }
        
        // Reset tag state
        memset(&current_tag, 0, sizeof(current_tag));
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
    
    printf("nfc_daemon: Starting NFC Daemon with PN532 support\n");
    
    // Load configuration
    load_config();
    
    // Check if game launcher service is available
    if (access(GAME_LAUNCHER_FIFO, F_OK) != 0) {
        printf("nfc_daemon: Warning - Game launcher service not available\n");
        printf("nfc_daemon: Please start /media/fat/utils/game_launcher first\n");
    }
    
    // Initialize PN532
    if (!init_pn532()) {
        printf("nfc_daemon: Failed to initialize PN532\n");
        return 1;
    }
    
    if (!configure_pn532()) {
        printf("nfc_daemon: Failed to configure PN532\n");
        close(pn532_fd);
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
    
    printf("nfc_daemon: NFC daemon ready (polling every %dms, mode: %s)\n", 
           config.poll_interval_ms, 
           config.mode == NFC_MODE_TAP ? "tap" : "hold");
    
    // Main polling loop
    while (keep_running) {
        nfc_tag_data_t tag_data;
        
        if (detect_nfc_tag(&tag_data)) {
            process_nfc_tag(&tag_data);
        } else {
            // No tag detected - check for removal in hold mode
            check_tag_removal();
        }
        
        // Sleep for configured interval
        usleep(config.poll_interval_ms * 1000);
    }
    
    // Cleanup
    printf("nfc_daemon: Shutting down\n");
    if (pn532_fd >= 0) {
        close(pn532_fd);
    }
    unlink(PID_FILE);
    
    return 0;
}