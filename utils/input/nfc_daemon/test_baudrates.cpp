/*
 * Test different baud rates for PN532
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <cstdint>
#include <errno.h>

void test_baud_rate(const char* device, speed_t baud_rate, const char* baud_name) {
    printf("\n=== Testing %s at %s baud ===\n", device, baud_name);
    
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        printf("❌ Cannot open device\n");
        return;
    }
    
    // Configure UART
    struct termios tio;
    tcgetattr(fd, &tio);
    cfsetispeed(&tio, baud_rate);
    cfsetospeed(&tio, baud_rate);
    tio.c_cflag = baud_rate | CS8 | CLOCAL | CREAD;
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VTIME] = 5;  // 0.5 second timeout
    tio.c_cc[VMIN] = 0;
    tcsetattr(fd, TCSANOW, &tio);
    
    // Clear any existing data
    tcflush(fd, TCIOFLUSH);
    
    // Send PN532 wake-up
    uint8_t wakeup[] = {
        0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    write(fd, wakeup, sizeof(wakeup));
    usleep(50000);
    
    // Clear response to wake-up
    uint8_t dummy[64];
    read(fd, dummy, sizeof(dummy));
    
    // Send GetFirmwareVersion command
    uint8_t get_version[] = {
        0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00
    };
    
    ssize_t written = write(fd, get_version, sizeof(get_version));
    if (written != sizeof(get_version)) {
        printf("❌ Write failed\n");
        close(fd);
        return;
    }
    
    usleep(100000);  // Wait 100ms
    
    uint8_t response[64];
    ssize_t bytes_read = read(fd, response, sizeof(response));
    if (bytes_read > 0) {
        printf("✅ Response (%zd bytes): ", bytes_read);
        for (ssize_t i = 0; i < bytes_read; i++) {
            printf("%02X ", response[i]);
        }
        printf("\n");
        
        // Check if this looks like a PN532 response
        if (bytes_read >= 6 && response[0] == 0x00 && response[1] == 0x00 && response[2] == 0xFF) {
            printf("🎉 PN532 FOUND at %s baud!\n", baud_name);
        }
    } else {
        printf("⚠️  No response\n");
    }
    
    close(fd);
}

int main(int argc, char* argv[]) {
    const char* device = "/dev/ttyUSB1";
    if (argc > 1) {
        device = argv[1];
    }
    
    printf("Testing PN532 at different baud rates on %s\n", device);
    
    // Test common baud rates
    test_baud_rate(device, B9600, "9600");
    test_baud_rate(device, B19200, "19200");
    test_baud_rate(device, B38400, "38400");
    test_baud_rate(device, B57600, "57600");
    test_baud_rate(device, B115200, "115200");
    test_baud_rate(device, B230400, "230400");
    
    return 0;
}