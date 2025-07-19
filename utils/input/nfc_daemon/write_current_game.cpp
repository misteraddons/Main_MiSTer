/*
 * Write Current Game to NFC Tag
 * 
 * This utility detects the currently running game on MiSTer
 * and writes it to an NFC tag using the PN532
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdint>
#include <errno.h>

#define PN532_DEVICE "/dev/ttyUSB1"
#define MGL_PATH "/tmp/CORENAME"
#define LAST_GAME_PATH "/tmp/LASTGAME"

// PN532 Commands
#define PN532_COMMAND_SAMCONFIGURATION 0x14
#define PN532_COMMAND_INLISTPASSIVETARGET 0x4A
#define PN532_COMMAND_INDATAEXCHANGE 0x40

// NFC Tag Data Format
typedef struct __attribute__((packed)) {
    char magic[4];       // "NFC1"
    char core[8];        // "PSX\0\0\0\0\0"
    char game_id[16];    // "SLUS-00067\0\0\0\0\0"
    uint8_t tag_type;    // SINGLE_GAME = 0
    uint8_t reserved[3]; // Future use
} nfc_tag_data_t;

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
    
    printf("PN532 configured for tag writing\n");
    return true;
}

// Wait for NFC tag
bool wait_for_tag() {
    printf("Place NFC tag on reader...\n");
    
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

// Get current core name
bool get_current_core(char* core_name, size_t max_len) {
    FILE* fp = fopen(MGL_PATH, "r");
    if (!fp) {
        printf("No core currently running\n");
        return false;
    }
    
    if (fgets(core_name, max_len, fp)) {
        // Remove newline
        char* newline = strchr(core_name, '\n');
        if (newline) *newline = '\0';
        fclose(fp);
        return true;
    }
    
    fclose(fp);
    return false;
}

// Get current game from command line arguments or last game file
bool get_current_game(int argc, char* argv[], char* game_name, size_t max_len) {
    if (argc > 1) {
        // Game name provided as argument
        strncpy(game_name, argv[1], max_len - 1);
        game_name[max_len - 1] = '\0';
        return true;
    }
    
    // Try to read from LASTGAME file
    FILE* fp = fopen(LAST_GAME_PATH, "r");
    if (!fp) {
        printf("No game specified and no last game found\n");
        printf("Usage: %s [game_name_or_serial]\n", argv[0]);
        return false;
    }
    
    if (fgets(game_name, max_len, fp)) {
        char* newline = strchr(game_name, '\n');
        if (newline) *newline = '\0';
        fclose(fp);
        return true;
    }
    
    fclose(fp);
    return false;
}

// Write tag data
bool write_tag_data(const nfc_tag_data_t* tag_data) {
    // This is a simplified implementation
    // Real implementation would use NDEF commands to write to the tag
    
    uint8_t write_data[3 + 32];  // Command + block + data
    write_data[0] = 0x01;  // Target
    write_data[1] = 0xA0;  // MIFARE Write command  
    write_data[2] = 0x04;  // Block 4 (start of user data)
    
    memcpy(&write_data[3], tag_data, sizeof(nfc_tag_data_t));
    
    uint8_t response[16];
    size_t response_len;
    
    if (!pn532_send_command(PN532_COMMAND_INDATAEXCHANGE, write_data, sizeof(write_data), response, &response_len)) {
        printf("Failed to write tag data\n");
        return false;
    }
    
    printf("Tag written successfully!\n");
    return true;
}

int main(int argc, char* argv[]) {
    printf("MiSTer NFC Tag Writer - Current Game\n");
    printf("===================================\n");
    
    // Get current core
    char core_name[32];
    if (!get_current_core(core_name, sizeof(core_name))) {
        strcpy(core_name, "Unknown");
    }
    printf("Current core: %s\n", core_name);
    
    // Get current game
    char game_name[64];
    if (!get_current_game(argc, argv, game_name, sizeof(game_name))) {
        return 1;
    }
    printf("Game to write: %s\n", game_name);
    
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
    
    // Prepare tag data
    nfc_tag_data_t tag_data = {0};
    memcpy(tag_data.magic, "NFC1", 4);
    strncpy(tag_data.core, core_name, sizeof(tag_data.core) - 1);
    strncpy(tag_data.game_id, game_name, sizeof(tag_data.game_id) - 1);
    tag_data.tag_type = 0;  // SINGLE_GAME
    
    printf("Writing tag with:\n");
    printf("  Core: %s\n", tag_data.core);
    printf("  Game: %s\n", tag_data.game_id);
    
    // Write tag
    if (write_tag_data(&tag_data)) {
        printf("Success! Tag can now launch this game.\n");
    } else {
        printf("Failed to write tag.\n");
    }
    
    close(pn532_fd);
    return 0;
}