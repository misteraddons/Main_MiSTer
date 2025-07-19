/*
 * MiSTer CD-ROM Daemon
 * 
 * CD-ROM detection daemon that uses the game_launcher service
 * for GameDB lookup and MGL creation
 * 
 * This version focuses on:
 * - CD-ROM detection
 * - Disc serial extraction
 * - Communication with game_launcher service
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#define CD_DEVICE "/dev/sr0"
#define CD_CHECK_INTERVAL 2
#define CD_PRESENT_FLAG "/tmp/cdrom_present"
#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"

static volatile int keep_running = 1;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Check if CD is present  
bool is_cd_present() {
    int fd = open(CD_DEVICE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    // Try to read from the device
    char buffer[256];
    ssize_t result = read(fd, buffer, sizeof(buffer));
    close(fd);
    
    return result > 0;
}

// Get a simple disc identifier for change detection
bool get_disc_id(char* disc_id, size_t disc_id_size) {
    int fd = open(CD_DEVICE, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    char buffer[256];
    if (read(fd, buffer, 256) != 256) {
        close(fd);
        return false;
    }
    
    // Create a simple hash of first 256 bytes to detect disc changes
    unsigned int hash = 0;
    for (int i = 0; i < 256; i++) {
        hash = hash * 31 + (unsigned char)buffer[i];
    }
    
    snprintf(disc_id, disc_id_size, "%08x", hash);
    close(fd);
    return true;
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

// Send command to game launcher
bool send_game_launcher_command(const char* system, const char* id_type, const char* identifier) {
    int fd = open(GAME_LAUNCHER_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    char command[512];
    snprintf(command, sizeof(command), "%s:%s:%s:cdrom", system, id_type, identifier);
    
    ssize_t written = write(fd, command, strlen(command));
    close(fd);
    
    return written > 0;
}

// Detect CD system type
const char* detect_cd_system() {
    int fd = open(CD_DEVICE, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    
    char buffer[2048];
    
    // Check for Saturn magic word
    if (lseek(fd, 0, SEEK_SET) == 0 && read(fd, buffer, 256) == 256) {
        if (strstr(buffer, "SEGA SEGASATURN") != NULL) {
            close(fd);
            return "Saturn";
        }
    }
    
    // Check for Sega CD magic words
    if (lseek(fd, 0, SEEK_SET) == 0 && read(fd, buffer, 256) == 256) {
        if (strstr(buffer, "SEGADISCSYSTEM") != NULL || 
            strstr(buffer, "SEGA_CD_") != NULL ||
            strstr(buffer, "SEGA CD") != NULL) {
            close(fd);
            return "MegaCD";
        }
    }
    
    // Check for PSX (look for "PLAYSTATION" string)
    if (lseek(fd, 0, SEEK_SET) == 0 && read(fd, buffer, 1024) == 1024) {
        if (strstr(buffer, "PLAYSTATION") != NULL) {
            close(fd);
            return "PSX";
        }
    }
    
    close(fd);
    return "Unknown";
}

// Extract PSX disc serial
bool extract_psx_serial(char* serial, size_t serial_size) {
    int fd = open(CD_DEVICE, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    char buffer[2048];
    
    // Read sector 16 (where PSX serial is typically located)
    if (lseek(fd, 16 * 2048, SEEK_SET) != 16 * 2048) {
        close(fd);
        return false;
    }
    
    if (read(fd, buffer, 2048) != 2048) {
        close(fd);
        return false;
    }
    
    // Look for serial pattern (SLUS, SCUS, SCES, SLED, etc.)
    for (int i = 0; i < 2048 - 12; i++) {
        if ((strncmp(&buffer[i], "SLUS", 4) == 0 ||
             strncmp(&buffer[i], "SCUS", 4) == 0 ||
             strncmp(&buffer[i], "SCES", 4) == 0 ||
             strncmp(&buffer[i], "SLED", 4) == 0) &&
            buffer[i + 4] == '-' || buffer[i + 4] == '_') {
            
            // Extract serial (format: SLUS-00067)
            int j = 0;
            while (j < 11 && i + j < 2048 && 
                   (buffer[i + j] != ' ' && buffer[i + j] != '\0' && 
                    buffer[i + j] != '\n' && buffer[i + j] != '\r')) {
                serial[j] = buffer[i + j];
                j++;
            }
            serial[j] = '\0';
            
            close(fd);
            return true;
        }
    }
    
    close(fd);
    return false;
}

// Extract Saturn disc serial
bool extract_saturn_serial(char* serial, size_t serial_size) {
    int fd = open(CD_DEVICE, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    char buffer[256];
    
    // Read first sector
    if (read(fd, buffer, 256) != 256) {
        close(fd);
        return false;
    }
    
    // Saturn game ID is at offset 0x20
    if (buffer[0x20] != '\0') {
        char temp_serial[64];
        strncpy(temp_serial, &buffer[0x20], sizeof(temp_serial) - 1);
        temp_serial[sizeof(temp_serial) - 1] = '\0';
        
        // Extract just the product code (first part before spaces/version info)
        // Format: "MK-81014  V1.00519951113CD-1/1  U               J"
        // We want: "MK-81014"
        char* space_pos = strchr(temp_serial, ' ');
        if (space_pos) {
            *space_pos = '\0';  // Terminate at first space
        }
        
        strncpy(serial, temp_serial, serial_size - 1);
        serial[serial_size - 1] = '\0';
        
        close(fd);
        return true;
    }
    
    close(fd);
    return false;
}

// Extract Sega CD serial
bool extract_segacd_serial(char* serial, size_t serial_size) {
    int fd = open(CD_DEVICE, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    char buffer[512];
    
    // Read first sector
    if (read(fd, buffer, 512) != 512) {
        close(fd);
        return false;
    }
    
    // Verify this is a Sega CD disc by checking for magic word
    if (strstr(buffer, "SEGADISCSYSTEM") == NULL) {
        close(fd);
        return false;
    }
    
    // Product code is at fixed offset 0x180 (384 decimal) from start of sector
    if (strlen(&buffer[0x180]) > 0) {
        strncpy(serial, &buffer[0x180], serial_size - 1);
        serial[serial_size - 1] = '\0';
        
        // Clean up serial (remove trailing spaces and region codes)
        for (int i = strlen(serial) - 1; i >= 0 && (serial[i] == ' ' || serial[i] == 'J' || serial[i] == 'U' || serial[i] == 'E'); i--) {
            serial[i] = '\0';
        }
        // Remove any remaining trailing spaces
        for (int i = strlen(serial) - 1; i >= 0 && serial[i] == ' '; i--) {
            serial[i] = '\0';
        }
        
        close(fd);
        return true;
    }
    
    close(fd);
    return false;
}

// Extract disc serial based on system
bool extract_disc_serial(const char* system, char* serial, size_t serial_size) {
    if (strcmp(system, "PSX") == 0) {
        return extract_psx_serial(serial, serial_size);
    } else if (strcmp(system, "Saturn") == 0) {
        return extract_saturn_serial(serial, serial_size);
    } else if (strcmp(system, "MegaCD") == 0) {
        return extract_segacd_serial(serial, serial_size);
    }
    
    return false;
}

// Clean up MGL files
void cleanup_mgls() {
    system("rm -f /media/fat/[0-9]-*.mgl 2>/dev/null");
    system("find /media/fat -maxdepth 1 -name '*.mgl' ! -name '*_*.mgl' -delete 2>/dev/null");
}

// Main daemon loop
int main(int argc, char* argv[]) {
    bool last_cd_present = false;
    char last_disc_id[32] = "";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("cdrom_daemon: Starting CD-ROM Daemon (uses game_launcher service)\n");
    
    // Check if game launcher service is available
    if (access(GAME_LAUNCHER_FIFO, F_OK) != 0) {
        printf("cdrom_daemon: Warning - Game launcher service not available\n");
        printf("cdrom_daemon: Please start /media/fat/utils/game_launcher first\n");
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
        // Keep STDOUT and STDERR open for debug output
        // close(STDOUT_FILENO);
        // close(STDERR_FILENO);
    }
    
    // Main loop
    while (keep_running) {
        bool cd_present = is_cd_present();
        char current_disc_id[32] = "";
        bool disc_changed = false;
        
        printf("cdrom_daemon: Checking CD status - present: %s\n", cd_present ? "yes" : "no");
        fflush(stdout);
        
        if (cd_present) {
            // Get disc identifier to detect changes
            if (get_disc_id(current_disc_id, sizeof(current_disc_id))) {
                disc_changed = (strlen(last_disc_id) == 0 || strcmp(current_disc_id, last_disc_id) != 0);
                printf("cdrom_daemon: Current disc ID: %s, Last ID: %s, Changed: %s\n", 
                       current_disc_id, last_disc_id, disc_changed ? "yes" : "no");
                fflush(stdout);
            }
        }
        
        // Handle CD removal
        if (last_cd_present && !cd_present) {
            printf("cdrom_daemon: CD removed\n");
            fflush(stdout);
            send_osd_message("CD ejected");
            // Send cleanup command to game_launcher instead of doing it directly
            send_game_launcher_command("COMMAND", "cleanup_mgls", "cdrom_removed");
            strcpy(last_disc_id, ""); // Clear disc ID
        }
        
        // Handle CD insertion or change
        if (cd_present && disc_changed) {
            if (!last_cd_present) {
                printf("cdrom_daemon: CD inserted - starting identification\n");
                fflush(stdout);
                send_osd_message("CD inserted - Identifying...");
            } else {
                printf("cdrom_daemon: Different disc detected - starting identification\n");
                fflush(stdout);
                send_osd_message("New disc - Identifying...");
                // Send cleanup command to game_launcher instead of doing it directly
                send_game_launcher_command("COMMAND", "cleanup_mgls", "disc_changed");
            }
            
            sleep(2); // Let disc settle
            
            // Detect system
            const char* system = detect_cd_system();
            printf("cdrom_daemon: Detected system: %s\n", system);
            fflush(stdout);
            
            if (strcmp(system, "Unknown") != 0) {
                char system_msg[128];
                snprintf(system_msg, sizeof(system_msg), "Detected: %s disc", system);
                send_osd_message(system_msg);
                
                // Extract disc serial
                char serial[64] = "";
                if (extract_disc_serial(system, serial, sizeof(serial))) {
                    printf("cdrom_daemon: Extracted serial: %s\n", serial);
                    fflush(stdout);
                    
                    // Map system name for GameDB (MegaCD -> SegaCD for database lookup)
                    const char* db_system = system;
                    if (strcmp(system, "MegaCD") == 0) {
                        db_system = "SegaCD";
                    }
                    
                    // Send to game launcher service
                    if (send_game_launcher_command(db_system, "serial", serial)) {
                        printf("cdrom_daemon: Sent request to game launcher\n");
                        fflush(stdout);
                    } else {
                        printf("cdrom_daemon: Failed to communicate with game launcher\n");
                        fflush(stdout);
                        send_osd_message("Game launcher service unavailable");
                    }
                } else {
                    printf("cdrom_daemon: Could not extract disc serial\n");
                    fflush(stdout);
                    send_osd_message("Could not identify disc");
                }
            } else {
                send_osd_message("Unknown disc type");
            }
            
            // Update last known disc ID
            strcpy(last_disc_id, current_disc_id);
        }
        
        last_cd_present = cd_present;
        sleep(CD_CHECK_INTERVAL);
    }
    
    printf("cdrom_daemon: Shutting down\n");
    cleanup_mgls();
    unlink(CD_PRESENT_FLAG);
    
    return 0;
}