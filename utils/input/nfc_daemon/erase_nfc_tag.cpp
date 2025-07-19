/*
 * Erase NFC Tag
 * 
 * This utility erases/clears an NFC tag by writing zeros to all user data blocks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <cstdint>
#include <errno.h>

#define PN532_DEVICE "/dev/ttyUSB1"

// PN532 Commands
#define PN532_COMMAND_SAMCONFIGURATION 0x14
#define PN532_COMMAND_INLISTPASSIVETARGET 0x4A
#define PN532_COMMAND_INDATAEXCHANGE 0x40

static int pn532_fd = -1;

// Initialize PN532
bool init_pn532() {
    pn532_fd = open(PN532_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (pn532_fd < 0) {
        printf("Failed to open %s\n", PN532_DEVICE);
        return false;
    }
    
    // Configure UART
    struct termios tio;
    if (tcgetattr(pn532_fd, &tio) == 0) {
        cfsetispeed(&tio, B115200);
        cfsetospeed(&tio, B115200);
        tio.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
        tio.c_iflag = 0;
        tio.c_oflag = 0;
        tio.c_lflag = 0;
        tio.c_cc[VTIME] = 10;
        tio.c_cc[VMIN] = 0;
        tcsetattr(pn532_fd, TCSANOW, &tio);
    }
    
    // Send wake-up sequence
    uint8_t wakeup[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    write(pn532_fd, wakeup, sizeof(wakeup));
    usleep(50000);
    
    // Clear buffer
    uint8_t dummy[32];
    read(pn532_fd, dummy, sizeof(dummy));
    
    return true;
}

// Send PN532 command
bool pn532_send_command(uint8_t command, const uint8_t* data, size_t data_len, uint8_t* response, size_t* response_len) {
    if (pn532_fd < 0) return false;
    
    // Build UART frame
    uint8_t frame[256];
    size_t frame_len = 0;
    
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
    
    // Send frame
    if (write(pn532_fd, frame, frame_len) != (ssize_t)frame_len) {
        return false;
    }
    
    // Read response
    usleep(50000);  // Wait for ACK
    uint8_t resp_buffer[256];
    ssize_t bytes_read = read(pn532_fd, resp_buffer, sizeof(resp_buffer));
    
    if (bytes_read == 6) {
        // Got ACK, wait for actual response
        usleep(200000);
        bytes_read = read(pn532_fd, resp_buffer, sizeof(resp_buffer));
    }
    
    if (bytes_read > 6) {
        *response_len = bytes_read - 6;
        memcpy(response, resp_buffer + 6, *response_len);
        return true;
    }
    
    return false;
}

// Configure PN532
bool configure_pn532() {
    uint8_t sam_config[] = {0x01, 0x14, 0x01};
    uint8_t response[16];
    size_t response_len;
    
    if (!pn532_send_command(PN532_COMMAND_SAMCONFIGURATION, sam_config, sizeof(sam_config), response, &response_len)) {
        printf("Failed to configure SAM\n");
        return false;
    }
    
    printf("PN532 configured for tag erasing\n");
    return true;
}

// Wait for NFC tag
bool wait_for_tag() {
    printf("Place NFC tag on reader to erase...\n");
    
    for (int tries = 0; tries < 50; tries++) {  // 10 second timeout
        uint8_t target_data[] = {0x01, 0x00};
        uint8_t response[64];
        size_t response_len;
        
        if (pn532_send_command(PN532_COMMAND_INLISTPASSIVETARGET, target_data, sizeof(target_data), response, &response_len)) {
            if (response_len >= 6 && response[0] == 0x01) {
                printf("Tag detected!\n");
                return true;
            }
        }
        
        usleep(200000);  // 200ms delay
    }
    
    printf("No tag detected within timeout\n");
    return false;
}

// Erase tag by writing zeros to user data blocks
bool erase_tag() {
    printf("Erasing tag data...\n");
    
    // MIFARE Classic 1K has 16 sectors, each with 4 blocks
    // We'll erase sectors 1-15 (sector 0 contains manufacturer data)
    // For each sector, we erase blocks 0, 1, 2 (block 3 is sector trailer)
    
    bool success = true;
    int blocks_erased = 0;
    
    // Start from block 4 (first user data block)
    for (uint8_t block = 4; block < 64; block++) {
        // Skip sector trailers (blocks 7, 11, 15, 19, etc.)
        if ((block + 1) % 4 == 0) {
            continue;
        }
        
        uint8_t write_data[19];  // Command + block + 16 bytes data
        write_data[0] = 0x01;    // Target
        write_data[1] = 0xA0;    // MIFARE Write command  
        write_data[2] = block;   // Block number
        
        // Write 16 bytes of zeros
        memset(&write_data[3], 0x00, 16);
        
        uint8_t response[16];
        size_t response_len;
        
        if (pn532_send_command(PN532_COMMAND_INDATAEXCHANGE, write_data, sizeof(write_data), response, &response_len)) {
            blocks_erased++;
            if (blocks_erased % 10 == 0) {
                printf(".");
                fflush(stdout);
            }
        } else {
            printf("\nWarning: Failed to erase block %d\n", block);
            success = false;
        }
        
        usleep(10000);  // Small delay between writes
    }
    
    printf("\n");
    
    if (success) {
        printf("Tag erased successfully! (%d blocks cleared)\n", blocks_erased);
    } else {
        printf("Tag partially erased (%d blocks cleared, some failures)\n", blocks_erased);
    }
    
    return success;
}

// Quick erase - just clear the first few user data blocks
bool quick_erase_tag() {
    printf("Quick erasing tag (first 8 user blocks)...\n");
    
    bool success = true;
    int blocks_erased = 0;
    
    // Clear blocks 4-11 (first 8 user data blocks)
    for (uint8_t block = 4; block < 12; block++) {
        // Skip block 7 (sector trailer)
        if (block == 7) {
            continue;
        }
        
        uint8_t write_data[19];  // Command + block + 16 bytes data
        write_data[0] = 0x01;    // Target
        write_data[1] = 0xA0;    // MIFARE Write command  
        write_data[2] = block;   // Block number
        
        // Write 16 bytes of zeros
        memset(&write_data[3], 0x00, 16);
        
        uint8_t response[16];
        size_t response_len;
        
        if (pn532_send_command(PN532_COMMAND_INDATAEXCHANGE, write_data, sizeof(write_data), response, &response_len)) {
            blocks_erased++;
            printf(".");
            fflush(stdout);
        } else {
            printf("\nWarning: Failed to erase block %d\n", block);
            success = false;
        }
        
        usleep(10000);  // Small delay between writes
    }
    
    printf("\n");
    
    if (success) {
        printf("Tag quick-erased successfully! (%d blocks cleared)\n", blocks_erased);
    } else {
        printf("Tag partially erased (%d blocks cleared, some failures)\n", blocks_erased);
    }
    
    return success;
}

void print_usage(const char* program_name) {
    printf("MiSTer NFC Tag Eraser\n");
    printf("====================\n");
    printf("\n");
    printf("Usage: %s [options]\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("  -q, --quick    Quick erase (first 8 user blocks only)\n");
    printf("  -f, --full     Full erase (all user data blocks)\n");
    printf("  -h, --help     Show this help\n");
    printf("\n");
    printf("Default: Quick erase\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s              # Quick erase\n", program_name);
    printf("  %s -q           # Quick erase\n", program_name);
    printf("  %s -f           # Full erase\n", program_name);
}

int main(int argc, char* argv[]) {
    bool full_erase = false;
    bool show_help = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--full") == 0) {
            full_erase = true;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quick") == 0) {
            full_erase = false;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help = true;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            show_help = true;
        }
    }
    
    if (show_help) {
        print_usage(argv[0]);
        return 0;
    }
    
    printf("MiSTer NFC Tag Eraser\n");
    printf("====================\n");
    printf("Mode: %s erase\n", full_erase ? "Full" : "Quick");
    printf("\n");
    
    // Initialize PN532
    if (!init_pn532()) {
        return 1;
    }
    
    if (!configure_pn532()) {
        close(pn532_fd);
        return 1;
    }
    
    // Wait for tag
    if (!wait_for_tag()) {
        close(pn532_fd);
        return 1;
    }
    
    // Erase tag
    bool success;
    if (full_erase) {
        success = erase_tag();
    } else {
        success = quick_erase_tag();
    }
    
    if (success) {
        printf("\n✓ Tag is now blank and ready for reuse!\n");
    } else {
        printf("\n⚠ Tag erase completed with some errors\n");
    }
    
    close(pn532_fd);
    return success ? 0 : 1;
}