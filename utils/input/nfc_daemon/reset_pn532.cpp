/*
 * PN532 Reset and Recovery Tool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <cstdint>

void try_pn532_recovery(const char* device) {
    printf("Attempting PN532 recovery on %s...\n", device);
    
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        printf("❌ Cannot open device\n");
        return;
    }
    
    // Configure UART
    struct termios tio;
    tcgetattr(fd, &tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VTIME] = 10;
    tio.c_cc[VMIN] = 0;
    tcsetattr(fd, TCSANOW, &tio);
    
    // Step 1: Extended wake-up sequence
    printf("1. Sending extended wake-up sequence...\n");
    uint8_t long_wakeup[32];
    memset(long_wakeup, 0x55, sizeof(long_wakeup));
    write(fd, long_wakeup, sizeof(long_wakeup));
    usleep(200000);  // Wait 200ms
    
    // Clear any response
    uint8_t dummy[128];
    read(fd, dummy, sizeof(dummy));
    
    // Step 2: Try different wake-up patterns
    printf("2. Trying alternative wake-up patterns...\n");
    uint8_t alt_wakeup1[] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
    write(fd, alt_wakeup1, sizeof(alt_wakeup1));
    usleep(100000);
    
    uint8_t alt_wakeup2[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    write(fd, alt_wakeup2, sizeof(alt_wakeup2));
    usleep(100000);
    
    read(fd, dummy, sizeof(dummy));
    
    // Step 3: Send break condition
    printf("3. Sending break condition...\n");
    tcsendbreak(fd, 0);
    usleep(100000);
    
    // Step 4: Reconfigure and try SAM config
    printf("4. Attempting SAM configuration...\n");
    tcsetattr(fd, TCSANOW, &tio);  // Reconfigure
    
    // PN532 SAM Configuration command
    uint8_t sam_config[] = {
        0x00, 0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00
    };
    
    write(fd, sam_config, sizeof(sam_config));
    usleep(200000);  // Wait longer
    
    uint8_t response[64];
    ssize_t bytes_read = read(fd, response, sizeof(response));
    if (bytes_read > 0) {
        printf("✅ SAM config response (%zd bytes): ", bytes_read);
        for (ssize_t i = 0; i < bytes_read; i++) {
            printf("%02X ", response[i]);
        }
        printf("\n");
    } else {
        printf("⚠️  No response to SAM config\n");
    }
    
    // Step 5: Try GetFirmwareVersion
    printf("5. Trying GetFirmwareVersion...\n");
    uint8_t get_version[] = {
        0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00
    };
    
    write(fd, get_version, sizeof(get_version));
    usleep(200000);
    
    bytes_read = read(fd, response, sizeof(response));
    if (bytes_read > 0) {
        printf("✅ Firmware version response (%zd bytes): ", bytes_read);
        for (ssize_t i = 0; i < bytes_read; i++) {
            printf("%02X ", response[i]);
        }
        printf("\n");
    } else {
        printf("⚠️  No response to firmware version\n");
    }
    
    close(fd);
}

int main(int argc, char* argv[]) {
    const char* device = "/dev/ttyUSB1";
    if (argc > 1) {
        device = argv[1];
    }
    
    printf("PN532 Reset and Recovery Tool\n");
    printf("============================\n");
    
    try_pn532_recovery(device);
    
    printf("\nIf no responses were received, try:\n");
    printf("1. Power cycle the PN532 module\n");
    printf("2. Check physical connections\n");
    printf("3. Try the other USB port (/dev/ttyUSB0)\n");
    
    return 0;
}