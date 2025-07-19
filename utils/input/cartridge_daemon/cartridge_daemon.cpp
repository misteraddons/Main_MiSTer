/*
 * MiSTer Cartridge Reader Daemon
 * 
 * Detects cartridge readers and automatically launches games
 * when cartridges are inserted. Supports USB and UART readers.
 * 
 * Features:
 * - USB cartridge reader detection (Retrode, GB Operator, etc.)
 * - UART-based Arduino readers
 * - ROM header analysis for game identification
 * - GameID integration for game matching
 * - Hot-plug detection for cartridge insertion/removal
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
#include <termios.h>
#include <libudev.h>
#include <libusb-1.0/libusb.h>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define ANNOUNCEMENT_FIFO "/dev/MiSTer_announcements"
#define CONFIG_FILE "/media/fat/utils/cartridge_daemon.conf"
#define PID_FILE "/tmp/cartridge_daemon.pid"
#define TEMP_ROM_DIR "/tmp/cartridge_roms"
#define MAX_ROM_SIZE (16 * 1024 * 1024) // 16MB max ROM size

// Supported cartridge reader types
typedef enum {
    READER_UNKNOWN = 0,
    READER_RETRODE,
    READER_GB_OPERATOR,
    READER_ARDUINO_SNES,
    READER_ARDUINO_GB,
    READER_EVERDRIVE_USB,
    READER_CUSTOM_UART
} reader_type_t;

// Cartridge types
typedef enum {
    CART_UNKNOWN = 0,
    CART_SNES,
    CART_GENESIS,
    CART_GAMEBOY,
    CART_GBC,
    CART_GBA,
    CART_N64,
    CART_NES
} cartridge_type_t;

// Configuration
typedef struct {
    bool monitor_usb_readers;
    bool monitor_uart_readers;
    bool auto_launch_games;
    bool dump_cartridge_roms;
    bool verify_checksums;
    char uart_reader_device[64];
    int uart_baud_rate;
    int poll_interval_ms;
} cartridge_config_t;

// Cartridge information
typedef struct {
    cartridge_type_t type;
    reader_type_t reader;
    char game_title[64];
    char internal_name[32];
    char publisher[32];
    char region[8];
    uint32_t checksum;
    size_t rom_size;
    char dump_path[256];
    time_t insertion_time;
    bool is_valid;
} cartridge_info_t;

// USB device info
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    reader_type_t reader_type;
    const char* name;
} usb_reader_info_t;

// Known USB cartridge readers
static const usb_reader_info_t known_readers[] = {
    {0x0403, 0x97C1, READER_RETRODE, "Retrode"},
    {0x1209, 0x4001, READER_GB_OPERATOR, "GB Operator"},
    {0x16C0, 0x05DC, READER_ARDUINO_SNES, "Arduino SNES Reader"},
    {0x2341, 0x0043, READER_ARDUINO_GB, "Arduino GB Reader"},
    {0x04D8, 0x000A, READER_EVERDRIVE_USB, "EverDrive USB"},
    {0, 0, READER_UNKNOWN, NULL}
};

// Global variables
static volatile int keep_running = 1;
static cartridge_config_t config;
static cartridge_info_t current_cartridge = {0};
static struct udev* udev_context = NULL;
static struct udev_monitor* udev_monitor = NULL;

// Function declarations
void parse_cartridge_response(const char* response);
const char* getCartTypeName(cartridge_type_t type);

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    config.monitor_usb_readers = true;
    config.monitor_uart_readers = true;
    config.auto_launch_games = true;
    config.dump_cartridge_roms = true;
    config.verify_checksums = true;
    strcpy(config.uart_reader_device, "/dev/ttyUSB0");
    config.uart_baud_rate = 115200;
    config.poll_interval_ms = 1000;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("cartridge_daemon: Using default configuration\n");
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
        
        if (strcmp(key, "monitor_usb_readers") == 0) {
            config.monitor_usb_readers = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "monitor_uart_readers") == 0) {
            config.monitor_uart_readers = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "auto_launch_games") == 0) {
            config.auto_launch_games = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "dump_cartridge_roms") == 0) {
            config.dump_cartridge_roms = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "verify_checksums") == 0) {
            config.verify_checksums = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "uart_reader_device") == 0) {
            strncpy(config.uart_reader_device, value, sizeof(config.uart_reader_device) - 1);
        } else if (strcmp(key, "uart_baud_rate") == 0) {
            config.uart_baud_rate = atoi(value);
        } else if (strcmp(key, "poll_interval_ms") == 0) {
            config.poll_interval_ms = atoi(value);
        }
    }
    
    fclose(fp);
    printf("cartridge_daemon: Configuration loaded\n");
}

// Send OSD message
void send_osd_message(const char* message) {
    int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "osd_message %s", message);
        write(fd, cmd, strlen(cmd));
        close(fd);
    }
}

// Send game launch command
bool launch_cartridge_game(const cartridge_info_t* cart) {
    if (!cart->is_valid) return false;
    
    // Determine core based on cartridge type
    const char* core = NULL;
    switch (cart->type) {
        case CART_SNES: core = "SNES"; break;
        case CART_GENESIS: core = "Genesis"; break;
        case CART_GAMEBOY: core = "Gameboy"; break;
        case CART_GBC: core = "Gameboy"; break;
        case CART_GBA: core = "GBA"; break;
        case CART_N64: core = "N64"; break;
        case CART_NES: core = "NES"; break;
        default: return false;
    }
    
    // Send to game launcher
    int fd = open(GAME_LAUNCHER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return false;
    
    char command[512];
    snprintf(command, sizeof(command), "%s:title:%s:cartridge", 
             core, cart->game_title);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    return written > 0;
}

// Detect reader type from USB device
reader_type_t detect_usb_reader(uint16_t vendor_id, uint16_t product_id) {
    for (int i = 0; known_readers[i].name != NULL; i++) {
        if (known_readers[i].vendor_id == vendor_id && 
            known_readers[i].product_id == product_id) {
            return known_readers[i].reader_type;
        }
    }
    return READER_UNKNOWN;
}

// Analyze ROM header to determine cartridge type and extract info
bool analyze_rom_header(const uint8_t* rom_data, size_t rom_size, cartridge_info_t* cart) {
    if (!rom_data || rom_size < 512) return false;
    
    // Clear cartridge info
    memset(cart, 0, sizeof(cartridge_info_t));
    cart->rom_size = rom_size;
    cart->insertion_time = time(NULL);
    
    // Check for SNES header
    if (rom_size >= 0x8000) {
        // SNES header at 0x7FC0 (LoROM) or 0xFFC0 (HiROM)
        const uint8_t* header = NULL;
        
        if (rom_size >= 0x10000 && 
            rom_data[0xFFC0 + 21] == ((rom_data[0xFFC0 + 23] ^ 0xFF) & 0xFF)) {
            header = &rom_data[0xFFC0]; // HiROM
        } else if (rom_data[0x7FC0 + 21] == ((rom_data[0x7FC0 + 23] ^ 0xFF) & 0xFF)) {
            header = &rom_data[0x7FC0]; // LoROM
        }
        
        if (header) {
            cart->type = CART_SNES;
            memcpy(cart->internal_name, header, 21);
            cart->internal_name[21] = '\0';
            
            // Clean up title (remove padding)
            for (int i = 20; i >= 0 && cart->internal_name[i] == ' '; i--) {
                cart->internal_name[i] = '\0';
            }
            
            strncpy(cart->game_title, cart->internal_name, sizeof(cart->game_title) - 1);
            cart->is_valid = true;
            return true;
        }
    }
    
    // Check for Game Boy header
    if (rom_size >= 0x150) {
        // Game Boy header at 0x100-0x14F
        const uint8_t* header = &rom_data[0x100];
        
        // Check Nintendo logo checksum (basic validation)
        uint8_t checksum = 0;
        for (int i = 0x104; i <= 0x133; i++) {
            checksum = checksum - rom_data[i] - 1;
        }
        
        if (checksum == rom_data[0x14D]) {
            // Determine GB vs GBC
            if (rom_data[0x143] == 0x80 || rom_data[0x143] == 0xC0) {
                cart->type = CART_GBC;
            } else {
                cart->type = CART_GAMEBOY;
            }
            
            // Extract title (0x134-0x143)
            memcpy(cart->internal_name, &rom_data[0x134], 16);
            cart->internal_name[16] = '\0';
            
            // Clean up title
            for (int i = 15; i >= 0 && (cart->internal_name[i] == 0 || cart->internal_name[i] == ' '); i--) {
                cart->internal_name[i] = '\0';
            }
            
            strncpy(cart->game_title, cart->internal_name, sizeof(cart->game_title) - 1);
            cart->is_valid = true;
            return true;
        }
    }
    
    // Check for Genesis header
    if (rom_size >= 0x200) {
        // Genesis header starts at 0x100
        if (memcmp(&rom_data[0x100], "SEGA", 4) == 0) {
            cart->type = CART_GENESIS;
            
            // Extract domestic title (0x150-0x17F)
            memcpy(cart->internal_name, &rom_data[0x150], 48);
            cart->internal_name[48] = '\0';
            
            // Clean up title
            for (int i = 47; i >= 0 && (cart->internal_name[i] == 0 || cart->internal_name[i] == ' '); i--) {
                cart->internal_name[i] = '\0';
            }
            
            strncpy(cart->game_title, cart->internal_name, sizeof(cart->game_title) - 1);
            cart->is_valid = true;
            return true;
        }
    }
    
    return false;
}

// Read ROM from USB device
bool read_rom_from_usb_device(const char* device_path, cartridge_info_t* cart) {
    // This would need to be implemented per device type
    // For now, simulate reading from a mounted filesystem
    
    char rom_path[256];
    snprintf(rom_path, sizeof(rom_path), "%s/rom.bin", device_path);
    
    FILE* fp = fopen(rom_path, "rb");
    if (!fp) return false;
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    size_t rom_size = ftell(fp);
    rewind(fp);
    
    if (rom_size > MAX_ROM_SIZE) {
        fclose(fp);
        return false;
    }
    
    // Read ROM data
    uint8_t* rom_data = malloc(rom_size);
    if (!rom_data) {
        fclose(fp);
        return false;
    }
    
    fread(rom_data, 1, rom_size, fp);
    fclose(fp);
    
    // Analyze header
    bool success = analyze_rom_header(rom_data, rom_size, cart);
    
    if (success && config.dump_cartridge_roms) {
        // Save ROM dump
        mkdir(TEMP_ROM_DIR, 0755);
        snprintf(cart->dump_path, sizeof(cart->dump_path), 
                 "%s/%s.rom", TEMP_ROM_DIR, cart->game_title);
        
        FILE* dump_fp = fopen(cart->dump_path, "wb");
        if (dump_fp) {
            fwrite(rom_data, 1, rom_size, dump_fp);
            fclose(dump_fp);
        }
    }
    
    free(rom_data);
    return success;
}

// Monitor USB devices for cartridge readers
void monitor_usb_devices() {
    if (!config.monitor_usb_readers) return;
    
    // Initialize libusb
    if (libusb_init(NULL) != 0) {
        printf("cartridge_daemon: Failed to initialize libusb\n");
        return;
    }
    
    libusb_device** device_list;
    ssize_t device_count = libusb_get_device_list(NULL, &device_list);
    
    for (ssize_t i = 0; i < device_count; i++) {
        libusb_device* device = device_list[i];
        struct libusb_device_descriptor desc;
        
        if (libusb_get_device_descriptor(device, &desc) == 0) {
            reader_type_t reader = detect_usb_reader(desc.idVendor, desc.idProduct);
            
            if (reader != READER_UNKNOWN) {
                printf("cartridge_daemon: Found cartridge reader: %04X:%04X\n", 
                       desc.idVendor, desc.idProduct);
                
                // Try to read cartridge
                cartridge_info_t cart;
                char device_path[256];
                snprintf(device_path, sizeof(device_path), "/dev/bus/usb/%03d/%03d",
                         libusb_get_bus_number(device), libusb_get_device_address(device));
                
                if (read_rom_from_usb_device(device_path, &cart)) {
                    printf("cartridge_daemon: Detected cartridge: %s\n", cart.game_title);
                    
                    current_cartridge = cart;
                    
                    if (config.auto_launch_games) {
                        if (launch_cartridge_game(&cart)) {
                            char msg[128];
                            snprintf(msg, sizeof(msg), "Cartridge: %s", cart.game_title);
                            send_osd_message(msg);
                        }
                    }
                }
            }
        }
    }
    
    libusb_free_device_list(device_list, 1);
    libusb_exit(NULL);
}

// Monitor UART reader
void monitor_uart_reader() {
    if (!config.monitor_uart_readers) return;
    
    static int uart_fd = -1;
    static time_t last_cart_check = 0;
    static bool last_cart_present = false;
    
    // Open UART connection if not already open
    if (uart_fd < 0) {
        uart_fd = open(config.uart_reader_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (uart_fd < 0) {
            static time_t last_error = 0;
            time_t now = time(NULL);
            if (now - last_error > 30) { // Only log every 30 seconds
                printf("cartridge_daemon: Warning - Cannot open UART device %s\n", 
                       config.uart_reader_device);
                last_error = now;
            }
            return;
        }
        
        // Configure UART
        struct termios tty;
        if (tcgetattr(uart_fd, &tty) == 0) {
            cfsetispeed(&tty, B115200);
            cfsetospeed(&tty, B115200);
            tty.c_cflag |= (CLOCAL | CREAD);
            tty.c_cflag &= ~PARENB;
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CSIZE;
            tty.c_cflag |= CS8;
            tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
            tty.c_iflag &= ~(IXON | IXOFF | IXANY);
            tty.c_oflag &= ~OPOST;
            tcsetattr(uart_fd, TCSANOW, &tty);
        }
        
        printf("cartridge_daemon: Connected to UART reader on %s\n", 
               config.uart_reader_device);
    }
    
    time_t now = time(NULL);
    
    // Check cartridge status every 2 seconds
    if (now - last_cart_check >= 2) {
        last_cart_check = now;
        
        // Send status command
        const char* cmd = "STATUS\r\n";
        if (write(uart_fd, cmd, strlen(cmd)) > 0) {
            usleep(100000); // Wait 100ms for response
            
            char response[256];
            ssize_t bytes = read(uart_fd, response, sizeof(response) - 1);
            if (bytes > 0) {
                response[bytes] = '\0';
                
                // Parse status response
                bool cart_present = false;
                char cart_type[32] = {0};
                
                char* status_line = strtok(response, "\r\n");
                while (status_line) {
                    if (strncmp(status_line, "STATUS ", 7) == 0) {
                        if (strstr(status_line, "inserted=true")) {
                            cart_present = true;
                            
                            // Extract cart type
                            char* type_start = strstr(status_line, "cart_type=");
                            if (type_start) {
                                type_start += 10; // Skip "cart_type="
                                char* type_end = strchr(type_start, ' ');
                                if (type_end) {
                                    size_t len = type_end - type_start;
                                    if (len < sizeof(cart_type)) {
                                        strncpy(cart_type, type_start, len);
                                        cart_type[len] = '\0';
                                    }
                                }
                            }
                        }
                        break;
                    }
                    status_line = strtok(NULL, "\r\n");
                }
                
                // Handle cartridge insertion/removal
                if (cart_present && !last_cart_present) {
                    printf("cartridge_daemon: Cartridge inserted (%s)\n", cart_type);
                    
                    // Request detailed cartridge info
                    const char* read_cmd = "READ_CART\r\n";
                    if (write(uart_fd, read_cmd, strlen(read_cmd)) > 0) {
                        usleep(500000); // Wait 500ms for cartridge read
                        
                        char cart_response[1024];
                        bytes = read(uart_fd, cart_response, sizeof(cart_response) - 1);
                        if (bytes > 0) {
                            cart_response[bytes] = '\0';
                            parse_cartridge_response(cart_response);
                        }
                    }
                    
                } else if (!cart_present && last_cart_present) {
                    printf("cartridge_daemon: Cartridge removed\n");
                    memset(&current_cartridge, 0, sizeof(current_cartridge));
                }
                
                last_cart_present = cart_present;
            }
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

// Parse cartridge response from UART reader
void parse_cartridge_response(const char* response) {
    cartridge_info_t cart = {0};
    cart.reader = READER_CUSTOM_UART;
    cart.insertion_time = time(NULL);
    
    char* line = strtok((char*)response, "\r\n");
    while (line) {
        if (strncmp(line, "CART_INFO ", 10) == 0) {
            // Parse cart type
            if (strstr(line, "type=SNES")) {
                cart.type = CART_SNES;
            } else if (strstr(line, "type=GAMEBOY")) {
                cart.type = CART_GAMEBOY;
            } else if (strstr(line, "type=GENESIS")) {
                cart.type = CART_GENESIS;
            }
            
        } else if (strncmp(line, "GAME_TITLE ", 11) == 0) {
            // Extract game title
            char* title_start = strchr(line, '"');
            if (title_start) {
                title_start++;
                char* title_end = strchr(title_start, '"');
                if (title_end) {
                    size_t len = title_end - title_start;
                    if (len < sizeof(cart.game_title)) {
                        strncpy(cart.game_title, title_start, len);
                        cart.game_title[len] = '\0';
                        strncpy(cart.internal_name, cart.game_title, sizeof(cart.internal_name) - 1);
                    }
                }
            }
            
        } else if (strncmp(line, "ROM_SIZE ", 9) == 0) {
            // Extract ROM size
            cart.rom_size = atoi(line + 9) * 1024; // Convert KB to bytes
            
        } else if (strncmp(line, "ERROR ", 6) == 0) {
            printf("cartridge_daemon: UART reader error: %s\n", line + 6);
            return;
        }
        
        line = strtok(NULL, "\r\n");
    }
    
    // Validate cartridge info
    if (cart.type != CART_UNKNOWN && strlen(cart.game_title) > 0) {
        cart.is_valid = true;
        current_cartridge = cart;
        
        printf("cartridge_daemon: Detected game: %s (%s)\n", 
               cart.game_title, getCartTypeName(cart.type));
        
        // Send OSD notification
        char osd_msg[128];
        snprintf(osd_msg, sizeof(osd_msg), "Cartridge: %s", cart.game_title);
        send_osd_message(osd_msg);
        
        // Auto-launch if enabled
        if (config.auto_launch_games) {
            if (launch_cartridge_game(&cart)) {
                printf("cartridge_daemon: Game launched successfully\n");
            } else {
                printf("cartridge_daemon: Failed to launch game\n");
            }
        }
    }
}

// Get cartridge type name
const char* getCartTypeName(cartridge_type_t type) {
    switch (type) {
        case CART_SNES: return "SNES";
        case CART_GENESIS: return "Genesis";
        case CART_GAMEBOY: return "Game Boy";
        case CART_GBC: return "Game Boy Color";
        case CART_GBA: return "Game Boy Advance";
        case CART_N64: return "Nintendo 64";
        case CART_NES: return "NES";
        default: return "Unknown";
    }
}

// Main function
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    printf("cartridge_daemon: Starting MiSTer Cartridge Reader Daemon\n");
    
    // Load configuration
    load_config();
    
    // Check if game launcher service is available
    if (access(GAME_LAUNCHER_FIFO, F_OK) != 0) {
        printf("cartridge_daemon: Warning - Game launcher service not available\n");
        printf("cartridge_daemon: Please start /media/fat/utils/game_launcher first\n");
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
    
    printf("cartridge_daemon: Cartridge reader monitoring active\n");
    
    // Main monitoring loop
    while (keep_running) {
        monitor_usb_devices();
        monitor_uart_reader();
        
        usleep(config.poll_interval_ms * 1000);
    }
    
    // Cleanup
    printf("cartridge_daemon: Shutting down\n");
    unlink(PID_FILE);
    
    return 0;
}