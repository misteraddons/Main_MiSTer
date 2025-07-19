/*
 * MiSTer UART Daemon
 * 
 * Serial interface for remote game launching
 * Another input source for the modular game launcher system
 * 
 * Features:
 * - UART/Serial communication interface
 * - Simple text protocol for game requests
 * - Integration with game_launcher service
 * - Auto-detection of serial ports
 * - Configurable baud rates and settings
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include <glob.h>
#include <time.h>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define ANNOUNCEMENT_FIFO "/dev/MiSTer_announcements"
#define CONFIG_FILE "/media/fat/utils/uart_daemon.conf"
#define PID_FILE "/tmp/uart_daemon.pid"
#define DEFAULT_DEVICE "/dev/ttyUSB0"
#define DEFAULT_BAUD 115200
#define MAX_LINE_LENGTH 256
#define MAX_RESPONSE_LENGTH 512

// Configuration
typedef struct {
    char device[64];
    int baud_rate;
    bool show_notifications;
    bool echo_commands;
    bool auto_detect;
    bool forward_announcements;
    int timeout_ms;
} uart_config_t;

// Global variables
static volatile int keep_running = 1;
static uart_config_t config;
static int uart_fd = -1;
static int announcement_fd = -1;

// Forward declarations
void send_uart_response(const char* response);

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
    if (uart_fd >= 0) {
        close(uart_fd);
    }
}

// Initialize default configuration
void init_config_defaults() {
    strcpy(config.device, DEFAULT_DEVICE);
    config.baud_rate = DEFAULT_BAUD;
    config.show_notifications = true;
    config.echo_commands = true;
    config.auto_detect = true;
    config.forward_announcements = true;
    config.timeout_ms = 5000;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("uart_daemon: Using default configuration\n");
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
        
        if (strcmp(key, "device") == 0) {
            strncpy(config.device, value, sizeof(config.device) - 1);
        } else if (strcmp(key, "baud_rate") == 0) {
            config.baud_rate = atoi(value);
        } else if (strcmp(key, "show_notifications") == 0) {
            config.show_notifications = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "echo_commands") == 0) {
            config.echo_commands = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "auto_detect") == 0) {
            config.auto_detect = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "forward_announcements") == 0) {
            config.forward_announcements = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "timeout_ms") == 0) {
            config.timeout_ms = atoi(value);
        }
    }
    
    fclose(fp);
    printf("uart_daemon: Configuration loaded (device: %s, baud: %d)\n", 
           config.device, config.baud_rate);
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
    snprintf(command, sizeof(command), "%s:%s:%s:uart", system, id_type, identifier);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    return written > 0;
}

// Convert baud rate to termios constant
speed_t get_baud_constant(int baud_rate) {
    switch (baud_rate) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B115200;
    }
}

// Configure UART port
bool configure_uart(int fd) {
    struct termios tty;
    
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return false;
    }
    
    // Set baud rate
    speed_t baud = get_baud_constant(config.baud_rate);
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    
    // Configure 8N1
    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // One stop bit
    tty.c_cflag &= ~CSIZE;    // Clear size bits
    tty.c_cflag |= CS8;       // 8 data bits
    tty.c_cflag &= ~CRTSCTS;  // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;  // Enable receiver, ignore modem lines
    
    // Configure input flags
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);  // Disable software flow control
    tty.c_iflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // Raw input
    
    // Configure output flags
    tty.c_oflag &= ~OPOST;  // Raw output
    
    // Configure local flags
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // Raw mode
    
    // Set timeouts
    tty.c_cc[VMIN] = 0;   // Non-blocking reads
    tty.c_cc[VTIME] = 1;  // 0.1 second timeout
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return false;
    }
    
    return true;
}

// Auto-detect available serial ports
char* auto_detect_serial_port() {
    static char detected_port[64];
    
    // Common serial device patterns
    const char* patterns[] = {
        "/dev/ttyUSB*",
        "/dev/ttyACM*", 
        "/dev/ttyAMA*",
        "/dev/ttyS*",
        NULL
    };
    
    for (int i = 0; patterns[i]; i++) {
        glob_t glob_result;
        
        if (glob(patterns[i], GLOB_NOSORT, NULL, &glob_result) == 0) {
            for (size_t j = 0; j < glob_result.gl_pathc; j++) {
                const char* port = glob_result.gl_pathv[j];
                
                // Try to open the port
                int test_fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
                if (test_fd >= 0) {
                    close(test_fd);
                    strncpy(detected_port, port, sizeof(detected_port) - 1);
                    detected_port[sizeof(detected_port) - 1] = '\0';
                    globfree(&glob_result);
                    printf("uart_daemon: Auto-detected serial port: %s\n", detected_port);
                    return detected_port;
                }
            }
        }
        
        globfree(&glob_result);
    }
    
    return NULL;
}

// Open announcement FIFO for reading
bool open_announcement_fifo() {
    if (!config.forward_announcements) return true;
    
    announcement_fd = open(ANNOUNCEMENT_FIFO, O_RDONLY | O_NONBLOCK);
    if (announcement_fd < 0) {
        printf("uart_daemon: Warning - Cannot open announcement FIFO (announcer not running?)\n");
        return false;
    }
    
    printf("uart_daemon: Listening for game announcements\n");
    return true;
}

// Read and forward announcements
void check_announcements() {
    if (!config.forward_announcements || announcement_fd < 0) return;
    
    char buffer[512];
    ssize_t bytes_read = read(announcement_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        // Remove trailing newlines
        while (bytes_read > 0 && (buffer[bytes_read - 1] == '\n' || buffer[bytes_read - 1] == '\r')) {
            buffer[--bytes_read] = '\0';
        }
        
        if (bytes_read > 0) {
            printf("uart_daemon: Forwarding announcement: %s\n", buffer);
            send_uart_response(buffer);
        }
    } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // FIFO was closed, try to reopen
        close(announcement_fd);
        announcement_fd = -1;
        usleep(100000); // Wait 100ms before retry
        open_announcement_fifo();
    }
}

// Open UART connection
bool open_uart() {
    const char* device = config.device;
    
    // Auto-detect if enabled and default device doesn't exist
    if (config.auto_detect && access(config.device, F_OK) != 0) {
        char* detected = auto_detect_serial_port();
        if (detected) {
            device = detected;
        }
    }
    
    printf("uart_daemon: Opening UART device: %s\n", device);
    
    uart_fd = open(device, O_RDWR | O_NOCTTY);
    if (uart_fd < 0) {
        perror("Failed to open UART device");
        return false;
    }
    
    if (!configure_uart(uart_fd)) {
        close(uart_fd);
        uart_fd = -1;
        return false;
    }
    
    printf("uart_daemon: UART configured at %d baud\n", config.baud_rate);
    return true;
}

// Send response over UART
void send_uart_response(const char* response) {
    if (uart_fd >= 0 && response) {
        write(uart_fd, response, strlen(response));
        write(uart_fd, "\r\n", 2);
    }
}

// Parse command from UART
bool parse_uart_command(const char* line, char* core, char* id_type, char* identifier) {
    // Expected format: "LAUNCH core id_type identifier"
    // Examples:
    //   LAUNCH PSX serial SLUS-00067
    //   LAUNCH Saturn title "Panzer Dragoon"
    
    char cmd_buffer[MAX_LINE_LENGTH];
    strncpy(cmd_buffer, line, sizeof(cmd_buffer) - 1);
    cmd_buffer[sizeof(cmd_buffer) - 1] = '\0';
    
    // Remove trailing whitespace
    char* end = cmd_buffer + strlen(cmd_buffer) - 1;
    while (end > cmd_buffer && (*end == '\r' || *end == '\n' || *end == ' ')) {
        *end-- = '\0';
    }
    
    char* token = strtok(cmd_buffer, " ");
    if (!token || strcmp(token, "LAUNCH") != 0) {
        return false;
    }
    
    // Core
    token = strtok(NULL, " ");
    if (!token) return false;
    strncpy(core, token, 15);
    core[15] = '\0';
    
    // ID type
    token = strtok(NULL, " ");
    if (!token) return false;
    strncpy(id_type, token, 15);
    id_type[15] = '\0';
    
    // Identifier (rest of line, may contain spaces)
    token = strtok(NULL, "");
    if (!token) return false;
    
    // Remove quotes if present
    if (token[0] == '"') {
        token++;
        char* quote = strrchr(token, '"');
        if (quote) *quote = '\0';
    }
    
    strncpy(identifier, token, 63);
    identifier[63] = '\0';
    
    return true;
}

// Process UART command
void process_uart_command(const char* line) {
    char core[16], id_type[16], identifier[64];
    
    // Echo command if enabled
    if (config.echo_commands) {
        printf("uart_daemon: Received: %s", line);
    }
    
    // Handle built-in commands
    if (strncmp(line, "STATUS", 6) == 0) {
        bool game_launcher_available = (access(GAME_LAUNCHER_FIFO, F_OK) == 0);
        char response[256];
        snprintf(response, sizeof(response), "OK STATUS game_launcher=%s uart_baud=%d",
                game_launcher_available ? "true" : "false", config.baud_rate);
        send_uart_response(response);
        return;
    }
    
    if (strncmp(line, "PING", 4) == 0) {
        send_uart_response("OK PONG");
        return;
    }
    
    if (strncmp(line, "VERSION", 7) == 0) {
        send_uart_response("OK MiSTer-UART-Daemon/1.0");
        return;
    }
    
    // Parse launch command
    if (parse_uart_command(line, core, id_type, identifier)) {
        printf("uart_daemon: Launch request - Core: %s, ID Type: %s, Identifier: %s\n",
               core, id_type, identifier);
        
        if (send_game_launcher_command(core, id_type, identifier)) {
            char response[128];
            snprintf(response, sizeof(response), "OK LAUNCHED %s %s %s", core, id_type, identifier);
            send_uart_response(response);
            
            // Send OSD notification
            char osd_msg[128];
            snprintf(osd_msg, sizeof(osd_msg), "UART: Loading %s game", core);
            send_osd_message(osd_msg);
        } else {
            send_uart_response("ERROR Failed to communicate with game launcher service");
        }
    } else {
        send_uart_response("ERROR Invalid command format");
    }
}

// Read line from UART
int read_uart_line(char* buffer, size_t buffer_size) {
    static char line_buffer[MAX_LINE_LENGTH];
    static int line_pos = 0;
    
    fd_set read_fds;
    struct timeval timeout;
    
    FD_ZERO(&read_fds);
    FD_SET(uart_fd, &read_fds);
    
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // 100ms
    
    int ready = select(uart_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (ready <= 0) {
        return 0; // Timeout or error
    }
    
    char byte;
    ssize_t bytes_read = read(uart_fd, &byte, 1);
    
    if (bytes_read <= 0) {
        return 0;
    }
    
    if (byte == '\n' || byte == '\r') {
        if (line_pos > 0) {
            line_buffer[line_pos] = '\0';
            strncpy(buffer, line_buffer, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            line_pos = 0;
            return 1; // Complete line received
        }
    } else if (line_pos < MAX_LINE_LENGTH - 1) {
        line_buffer[line_pos++] = byte;
    }
    
    return 0; // Incomplete line
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
    
    printf("uart_daemon: Starting UART Game Launcher Daemon\n");
    
    // Load configuration
    load_config();
    
    // Check if game launcher service is available
    if (access(GAME_LAUNCHER_FIFO, F_OK) != 0) {
        printf("uart_daemon: Warning - Game launcher service not available\n");
        printf("uart_daemon: Please start /media/fat/utils/game_launcher first\n");
    }
    
    // Open UART connection
    if (!open_uart()) {
        fprintf(stderr, "uart_daemon: Failed to open UART connection\n");
        return 1;
    }
    
    // Open announcement FIFO
    open_announcement_fifo();
    
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
    
    printf("uart_daemon: UART interface ready on %s at %d baud\n", 
           config.device, config.baud_rate);
    printf("uart_daemon: Send 'LAUNCH core id_type identifier' to launch games\n");
    
    // Send startup message
    send_uart_response("OK MiSTer UART Game Launcher Ready");
    
    // Main loop
    char line[MAX_LINE_LENGTH];
    while (keep_running) {
        if (read_uart_line(line, sizeof(line))) {
            process_uart_command(line);
        }
        
        // Check for announcements to forward
        check_announcements();
    }
    
    // Cleanup
    printf("uart_daemon: Shutting down\n");
    if (uart_fd >= 0) {
        send_uart_response("OK SHUTDOWN");
        close(uart_fd);
    }
    if (announcement_fd >= 0) {
        close(announcement_fd);
    }
    unlink(PID_FILE);
    
    return 0;
}