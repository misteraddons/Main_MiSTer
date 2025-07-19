/*
 * MiSTer NFC Tag Writer Utility
 * 
 * Tool for writing game data to NFC tags for use with nfc_daemon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <getopt.h>
#include <cstdint>
#include "pn532_protocol.h"

#define PN532_I2C_ADDRESS 0x24
#define PN532_UART_BAUD 115200

// NFC Tag Data Format (32 bytes total)
typedef struct __attribute__((packed)) {
    char magic[4];       // "NFC1"
    char core[8];        // "PSX\0\0\0\0\0"
    char game_id[16];    // "SLUS-00067\0\0\0\0\0"
    uint8_t tag_type;    // SINGLE_GAME, PLAYLIST, etc.
    uint8_t reserved[3]; // Future use
} nfc_tag_data_t;

// Tag types
enum {
    SINGLE_GAME = 0,
    PLAYLIST = 1,
    RANDOM_GAME = 2,
    LAST_PLAYED = 3,
    FAVORITES = 4
};

static PN532Protocol pn532;
static int pn532_fd = -1;
static bool is_i2c = false;

// Initialize PN532 connection
bool init_pn532(const char* device) {
    if (strstr(device, "i2c")) {
        is_i2c = true;
        pn532_fd = open(device, O_RDWR);
        if (pn532_fd < 0) {
            perror("Failed to open I2C device");
            return false;
        }
        if (ioctl(pn532_fd, I2C_SLAVE, PN532_I2C_ADDRESS) < 0) {
            perror("Failed to set I2C address");
            close(pn532_fd);
            return false;
        }
    } else {
        is_i2c = false;
        pn532_fd = open(device, O_RDWR | O_NOCTTY);
        if (pn532_fd < 0) {
            perror("Failed to open UART device");
            return false;
        }
        // Configure UART (simplified)
    }
    
    return true;
}

// Send command to PN532
bool send_pn532_command(uint8_t command, const uint8_t* params, size_t params_len,
                       uint8_t* response, size_t* response_len) {
    uint8_t frame[256];
    size_t frame_len;
    
    // Build command frame
    if (!pn532.buildCommandFrame(frame, &frame_len, command, params, params_len)) {
        fprintf(stderr, "Failed to build command frame\n");
        return false;
    }
    
    // Send frame
    if (write(pn532_fd, frame, frame_len) != (ssize_t)frame_len) {
        fprintf(stderr, "Failed to write frame\n");
        return false;
    }
    
    // Read ACK
    uint8_t ack[6];
    usleep(10000); // 10ms delay
    if (read(pn532_fd, ack, 6) != 6) {
        fprintf(stderr, "Failed to read ACK\n");
        return false;
    }
    
    if (!pn532.isAckFrame(ack, 6)) {
        fprintf(stderr, "Invalid ACK received\n");
        return false;
    }
    
    // Read response
    usleep(50000); // 50ms delay
    uint8_t resp_frame[256];
    ssize_t resp_len = read(pn532_fd, resp_frame, sizeof(resp_frame));
    if (resp_len < 0) {
        fprintf(stderr, "Failed to read response\n");
        return false;
    }
    
    // Parse response
    uint8_t resp_command;
    return pn532.parseResponseFrame(resp_frame, resp_len, &resp_command, response, response_len);
}

// Initialize PN532 module
bool configure_pn532() {
    // Get firmware version
    uint8_t response[32];
    size_t response_len;
    
    printf("Getting PN532 firmware version...\n");
    if (!send_pn532_command(PN532_COMMAND_GETFIRMWAREVERSION, NULL, 0, response, &response_len)) {
        fprintf(stderr, "Failed to get firmware version\n");
        return false;
    }
    
    printf("PN532 Firmware: %d.%d\n", response[1], response[2]);
    
    // Configure SAM (Secure Access Module)
    uint8_t sam_config[] = {0x01, 0x14, 0x01}; // Normal mode, timeout 50ms * 20 = 1 second
    if (!send_pn532_command(PN532_COMMAND_SAMCONFIGURATION, sam_config, sizeof(sam_config), 
                           response, &response_len)) {
        fprintf(stderr, "Failed to configure SAM\n");
        return false;
    }
    
    printf("PN532 configured successfully\n");
    return true;
}

// Detect NFC tag
bool detect_tag(uint8_t* uid, uint8_t* uid_len, uint8_t* tag_type) {
    // InListPassiveTarget
    uint8_t params[] = {0x01, 0x00}; // Max 1 target, 106 kbps type A
    uint8_t response[64];
    size_t response_len;
    
    if (!send_pn532_command(PN532_COMMAND_INLISTPASSIVETARGET, params, sizeof(params),
                           response, &response_len)) {
        return false;
    }
    
    if (response[0] != 0x01) {
        return false; // No targets found
    }
    
    // Extract tag info
    uint8_t tag_number = response[0];
    uint8_t sens_res[2] = {response[1], response[2]};
    uint8_t sel_res = response[3];
    *uid_len = response[4];
    
    if (*uid_len > 0 && *uid_len <= 10) {
        memcpy(uid, &response[5], *uid_len);
    }
    
    // Detect tag type
    *tag_type = pn532.detectTagType(sens_res, sel_res);
    
    return true;
}

// Write data to NTAG
bool write_ntag_data(const nfc_tag_data_t* tag_data) {
    // NTAG memory layout:
    // Page 0-1: Serial number (read-only)
    // Page 2: Lock bytes
    // Page 3: Capability container
    // Page 4+: User data
    
    // We'll write our 32-byte structure starting at page 4
    const uint8_t start_page = 4;
    const uint8_t* data = (const uint8_t*)tag_data;
    
    // Write 4 bytes (1 page) at a time
    for (int i = 0; i < 8; i++) { // 32 bytes = 8 pages
        uint8_t page = start_page + i;
        uint8_t write_cmd[6];
        write_cmd[0] = 0x01; // Tag number
        write_cmd[1] = NTAG_CMD_WRITE;
        write_cmd[2] = page;
        memcpy(&write_cmd[3], &data[i * 4], 4);
        
        uint8_t response[16];
        size_t response_len;
        
        if (!send_pn532_command(PN532_COMMAND_INDATAEXCHANGE, write_cmd, sizeof(write_cmd),
                               response, &response_len)) {
            fprintf(stderr, "Failed to write page %d\n", page);
            return false;
        }
        
        if (response[0] != 0x00) {
            fprintf(stderr, "Write error on page %d: 0x%02X\n", page, response[0]);
            return false;
        }
        
        printf(".");
        fflush(stdout);
    }
    
    printf(" Done!\n");
    return true;
}

// Read data from NTAG
bool read_ntag_data(nfc_tag_data_t* tag_data) {
    const uint8_t start_page = 4;
    uint8_t* data = (uint8_t*)tag_data;
    
    // Read all 32 bytes using FAST_READ command
    uint8_t read_cmd[4];
    read_cmd[0] = 0x01; // Tag number
    read_cmd[1] = NTAG_CMD_FAST_READ;
    read_cmd[2] = start_page;
    read_cmd[3] = start_page + 7; // End page
    
    uint8_t response[36]; // 32 bytes data + status
    size_t response_len;
    
    if (!send_pn532_command(PN532_COMMAND_INDATAEXCHANGE, read_cmd, sizeof(read_cmd),
                           response, &response_len)) {
        fprintf(stderr, "Failed to read tag data\n");
        return false;
    }
    
    if (response[0] != 0x00) {
        fprintf(stderr, "Read error: 0x%02X\n", response[0]);
        return false;
    }
    
    // Copy data (skip status byte)
    memcpy(data, &response[1], 32);
    
    return true;
}

// Print usage
void print_usage(const char* prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -d, --device <path>    PN532 device (default: /dev/i2c-1)\n");
    printf("  -w, --write            Write mode\n");
    printf("  -r, --read             Read mode\n");
    printf("  -c, --core <name>      Core name (e.g., PSX, Saturn)\n");
    printf("  -g, --game <id>        Game ID (e.g., SLUS-00067)\n");
    printf("  -t, --type <type>      Tag type (0=single, 1=playlist)\n");
    printf("  -e, --erase            Erase tag\n");
    printf("  -h, --help             Show this help\n");
    printf("\nExamples:\n");
    printf("  Write tag:  %s -w -c PSX -g \"SLUS-00067\"\n", prog_name);
    printf("  Read tag:   %s -r\n", prog_name);
    printf("  Erase tag:  %s -e\n", prog_name);
}

int main(int argc, char* argv[]) {
    const char* device = "/dev/i2c-1";
    bool write_mode = false;
    bool read_mode = false;
    bool erase_mode = false;
    char core[8] = {0};
    char game_id[16] = {0};
    int tag_type = SINGLE_GAME;
    
    // Parse command line
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"write", no_argument, 0, 'w'},
        {"read", no_argument, 0, 'r'},
        {"core", required_argument, 0, 'c'},
        {"game", required_argument, 0, 'g'},
        {"type", required_argument, 0, 't'},
        {"erase", no_argument, 0, 'e'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "d:wrc:g:t:eh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                device = optarg;
                break;
            case 'w':
                write_mode = true;
                break;
            case 'r':
                read_mode = true;
                break;
            case 'c':
                strncpy(core, optarg, sizeof(core) - 1);
                break;
            case 'g':
                strncpy(game_id, optarg, sizeof(game_id) - 1);
                break;
            case 't':
                tag_type = atoi(optarg);
                break;
            case 'e':
                erase_mode = true;
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }
    
    // Validate mode
    if (!write_mode && !read_mode && !erase_mode) {
        fprintf(stderr, "Error: Must specify -w (write), -r (read), or -e (erase)\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (write_mode && (!core[0] || !game_id[0])) {
        fprintf(stderr, "Error: Write mode requires -c (core) and -g (game)\n");
        return 1;
    }
    
    // Initialize PN532
    printf("Initializing PN532 on %s...\n", device);
    if (!init_pn532(device)) {
        return 1;
    }
    
    if (!configure_pn532()) {
        close(pn532_fd);
        return 1;
    }
    
    // Wait for tag
    printf("Waiting for NFC tag...\n");
    
    uint8_t uid[10];
    uint8_t uid_len;
    uint8_t detected_tag_type;
    
    while (!detect_tag(uid, &uid_len, &detected_tag_type)) {
        usleep(250000); // 250ms
    }
    
    printf("Tag detected! UID: ");
    for (int i = 0; i < uid_len; i++) {
        printf("%02X ", uid[i]);
    }
    printf("\n");
    
    if (detected_tag_type != TAG_TYPE_NTAG) {
        fprintf(stderr, "Error: Only NTAG tags are currently supported\n");
        close(pn532_fd);
        return 1;
    }
    
    // Perform requested operation
    if (read_mode) {
        nfc_tag_data_t tag_data;
        printf("Reading tag data...\n");
        
        if (read_ntag_data(&tag_data)) {
            // Verify magic
            if (memcmp(tag_data.magic, "NFC1", 4) != 0) {
                printf("Tag is not formatted for MiSTer\n");
            } else {
                printf("Tag contents:\n");
                printf("  Core: %s\n", tag_data.core);
                printf("  Game: %s\n", tag_data.game_id);
                printf("  Type: %d\n", tag_data.tag_type);
            }
        }
    } else if (write_mode) {
        nfc_tag_data_t tag_data = {0};
        
        // Prepare tag data
        memcpy(tag_data.magic, "NFC1", 4);
        strncpy(tag_data.core, core, sizeof(tag_data.core) - 1);
        strncpy(tag_data.game_id, game_id, sizeof(tag_data.game_id) - 1);
        tag_data.tag_type = tag_type;
        
        printf("Writing tag data...\n");
        printf("  Core: %s\n", tag_data.core);
        printf("  Game: %s\n", tag_data.game_id);
        printf("  Type: %d\n", tag_data.tag_type);
        
        if (write_ntag_data(&tag_data)) {
            printf("Tag written successfully!\n");
        } else {
            fprintf(stderr, "Failed to write tag\n");
        }
    } else if (erase_mode) {
        nfc_tag_data_t empty_data = {0};
        printf("Erasing tag...\n");
        
        if (write_ntag_data(&empty_data)) {
            printf("Tag erased successfully!\n");
        } else {
            fprintf(stderr, "Failed to erase tag\n");
        }
    }
    
    close(pn532_fd);
    return 0;
}