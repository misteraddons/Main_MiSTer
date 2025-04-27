#include "esp32_proto.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

// UART file descriptor
static int uart_fd = -1;

// CRC-16-CCITT table
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    // ... (full table omitted for brevity)
};

void esp32_proto_init(void) {
    // Open UART device
    uart_fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
    if (uart_fd < 0) {
        printf("Failed to open UART device\n");
        return;
    }

    // Configure UART
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(uart_fd, &tty) != 0) {
        printf("Error from tcgetattr\n");
        return;
    }

    // Set baud rate
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    // Set other parameters
    tty.c_cflag |= (CLOCAL | CREAD);    // Ignore modem controls
    tty.c_cflag &= ~PARENB;             // No parity
    tty.c_cflag &= ~CSTOPB;             // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;                 // 8 bits
    tty.c_cflag &= ~CRTSCTS;            // No hardware flow control

    // Raw mode
    cfmakeraw(&tty);

    // Apply settings
    if (tcsetattr(uart_fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr\n");
        return;
    }
}

uint16_t esp32_proto_crc16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    while (length--) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ *data++) & 0xFF];
    }
    return crc;
}

void esp32_proto_send_packet(uint8_t type, const void* payload, uint16_t length) {
    if (uart_fd < 0) return;

    // Prepare packet
    esp32_packet_t packet;
    packet.start = ESP32_PROTO_START_BYTE;
    packet.type = type;
    packet.length = length;
    memcpy(packet.payload, payload, length);
    
    // Calculate CRC (excluding start byte)
    packet.crc = esp32_proto_crc16(&packet.type, length + 3);

    // Send packet
    write(uart_fd, &packet, length + 6);  // 6 = start + type + length + crc
}

bool esp32_proto_receive_packet(esp32_packet_t* packet) {
    if (uart_fd < 0) return false;

    // Read start byte
    if (read(uart_fd, &packet->start, 1) != 1) return false;
    if (packet->start != ESP32_PROTO_START_BYTE) return false;

    // Read type and length
    if (read(uart_fd, &packet->type, 1) != 1) return false;
    if (read(uart_fd, &packet->length, 2) != 2) return false;

    // Sanity check length
    if (packet->length > ESP32_PROTO_MAX_PAYLOAD) return false;

    // Read payload
    if (read(uart_fd, packet->payload, packet->length) != packet->length) return false;

    // Read CRC
    if (read(uart_fd, &packet->crc, 2) != 2) return false;

    // Verify CRC
    uint16_t calc_crc = esp32_proto_crc16(&packet->type, packet->length + 3);
    if (calc_crc != packet->crc) return false;

    return true;
}

// Helper functions for common messages
void esp32_proto_send_now_playing(const esp32_now_playing_t* info) {
    esp32_proto_send_packet(ESP32_PROTO_NOW_PLAYING, info, sizeof(esp32_now_playing_t));
}

void esp32_proto_send_menu_state(const esp32_menu_state_t* state) {
    esp32_proto_send_packet(ESP32_PROTO_MENU_STATE, state, sizeof(esp32_menu_state_t));
}

void esp32_proto_send_controller(const esp32_controller_t* controller) {
    esp32_proto_send_packet(ESP32_PROTO_CONTROLLER, controller, sizeof(esp32_controller_t));
}

void esp32_proto_send_debug_info(const char* message) {
    esp32_proto_send_packet(ESP32_PROTO_DEBUG_INFO, message, strlen(message));
} 