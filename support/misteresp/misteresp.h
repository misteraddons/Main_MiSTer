#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define STARTOFPACKET 0xAA
#define ENDOFPACKET 0xBB
#define CMD_BUTTON_ACTION 0x01
#define CMD_REQUEST_MISTER_STATUS 0x02
#define CMD_SEND_IR_CODE 0x03
#define CMD_SET_MISTER_CONFIG 0x04
#define CMD_INPUT_EVENT 0x05
#define CMD_MISTER_HEARTBEAT 0x0A
#define CMD_MISTER_STATUS_UPDATE 0x81
#define CMD_ACKNOWLEDGE 0x82
#define CMD_ACK CMD_ACKNOWLEDGE
#define CMD_ERROR 0x83
#define CMD_ERR 0x83
#define CMD_MISTER_CONFIG_DATA 0x84
#define PACKET_HEADER_SIZE 3
#define PACKET_TRAILER_SIZE 2
#define PACKET_OVERHEAD (PACKET_HEADER_SIZE + PACKET_TRAILER_SIZE)
#define MAX_PAYLOAD_SIZE 250
#define MAX_PACKET_SIZE 255

typedef struct Misterpacket {
  uint8_t cmd, len, *payload;
} Misterpacket;

uint8_t *serialize_packet(Misterpacket);
Misterpacket *deserialize_packet(uint8_t *mp);
ssize_t uart_read_packet(int fd, uint8_t *buffer, size_t buffer_size);
ssize_t uart_send_packet(int fd, const uint8_t *packet, size_t len);
