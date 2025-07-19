/*
 * PN532 Diagnostic Tool
 * 
 * This utility helps diagnose PN532 connection issues
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <cstdint>
#include <errno.h>

void test_uart_basic(const char* device) {
    printf("\n=== Testing %s ===\n", device);
    
    // Test 1: Can we open the device?
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        printf("❌ Cannot open device: %s\n", strerror(errno));
        return;
    }
    printf("✅ Device opened successfully\n");
    
    // Test 2: Can we configure UART?
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        printf("❌ Cannot get terminal attributes: %s\n", strerror(errno));
        close(fd);
        return;
    }
    printf("✅ Can read terminal attributes\n");
    
    // Configure UART settings
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VTIME] = 5;  // 0.5 second timeout
    tio.c_cc[VMIN] = 0;   // Non-blocking read
    
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        printf("❌ Cannot set terminal attributes: %s\n", strerror(errno));
        close(fd);
        return;
    }
    printf("✅ UART configured (115200 8N1)\n");
    
    // Test 3: Send a simple test pattern
    uint8_t test_pattern[] = {0x55, 0xAA, 0x55, 0xAA};
    ssize_t written = write(fd, test_pattern, sizeof(test_pattern));
    if (written != sizeof(test_pattern)) {
        printf("❌ Write failed: expected %zu bytes, wrote %zd\n", sizeof(test_pattern), written);
    } else {
        printf("✅ Can write data (%zd bytes)\n", written);
    }
    
    // Test 4: Try to read any response
    usleep(100000);  // Wait 100ms
    uint8_t read_buffer[32];
    ssize_t bytes_read = read(fd, read_buffer, sizeof(read_buffer));
    if (bytes_read > 0) {
        printf("✅ Received %zd bytes back: ", bytes_read);
        for (ssize_t i = 0; i < bytes_read; i++) {
            printf("%02X ", read_buffer[i]);
        }
        printf("\n");
    } else if (bytes_read == 0) {
        printf("⚠️  No response received (timeout)\n");
    } else {
        printf("❌ Read error: %s\n", strerror(errno));
    }
    
    close(fd);
}

void send_pn532_wakeup(const char* device) {
    printf("\n=== PN532 Wake-up Test on %s ===\n", device);
    
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
    
    // Send PN532 wake-up sequence
    uint8_t wakeup[] = {
        0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    printf("Sending PN532 wake-up sequence...\n");
    ssize_t written = write(fd, wakeup, sizeof(wakeup));
    if (written != sizeof(wakeup)) {
        printf("❌ Wake-up write failed\n");
        close(fd);
        return;
    }
    
    usleep(100000);  // Wait 100ms
    
    // Try to read any response
    uint8_t response[64];
    ssize_t bytes_read = read(fd, response, sizeof(response));
    if (bytes_read > 0) {
        printf("✅ Got %zd bytes response: ", bytes_read);
        for (ssize_t i = 0; i < bytes_read; i++) {
            printf("%02X ", response[i]);
        }
        printf("\n");
    } else {
        printf("⚠️  No response to wake-up\n");
    }
    
    // Send a simple PN532 command (GetFirmwareVersion)
    uint8_t get_version[] = {
        0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00
    };
    
    printf("Sending GetFirmwareVersion command...\n");
    written = write(fd, get_version, sizeof(get_version));
    if (written != sizeof(get_version)) {
        printf("❌ Command write failed\n");
        close(fd);
        return;
    }
    
    usleep(200000);  // Wait 200ms
    
    bytes_read = read(fd, response, sizeof(response));
    if (bytes_read > 0) {
        printf("✅ Command response (%zd bytes): ", bytes_read);
        for (ssize_t i = 0; i < bytes_read; i++) {
            printf("%02X ", response[i]);
        }
        printf("\n");
        
        // Check if this looks like a PN532 response
        if (bytes_read >= 6 && response[0] == 0x00 && response[1] == 0x00 && response[2] == 0xFF) {
            printf("🎉 This looks like a PN532 response!\n");
        }
    } else {
        printf("⚠️  No response to command\n");
    }
    
    close(fd);
}

int main(int argc, char* argv[]) {
    printf("PN532 Diagnostic Tool\n");
    printf("====================\n");
    
    const char* devices[] = {
        "/dev/ttyUSB0",
        "/dev/ttyUSB1",
        "/dev/ttyS0",
        "/dev/ttyS1"
    };
    
    const size_t num_devices = sizeof(devices) / sizeof(devices[0]);
    
    if (argc > 1) {
        // Test specific device
        printf("Testing specific device: %s\n", argv[1]);
        test_uart_basic(argv[1]);
        send_pn532_wakeup(argv[1]);
    } else {
        // Test all devices
        for (size_t i = 0; i < num_devices; i++) {
            test_uart_basic(devices[i]);
        }
        
        printf("\n" "Now testing PN532 communication:\n");
        for (size_t i = 0; i < num_devices; i++) {
            send_pn532_wakeup(devices[i]);
        }
    }
    
    printf("\n=== Summary ===\n");
    printf("If you see PN532 responses above, the hardware is working.\n");
    printf("If not, check:\n");
    printf("1. Power connections (3.3V and GND)\n");
    printf("2. UART wiring (TX, RX)\n");
    printf("3. Module compatibility\n");
    printf("4. Try different baud rates\n");
    
    return 0;
}