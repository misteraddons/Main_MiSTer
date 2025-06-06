#include "misteresp.h"
#include <stddef.h>
#include <unistd.h>

ssize_t uart_read_packet(int fd, uint8_t *buffer, size_t buffer_size) {
  if (fd < 0 || buffer == NULL || buffer_size < MAX_PACKET_SIZE) {
    return -1;
  }

  uint8_t byte;
  size_t pos = 0;
  int in_packet = 0;

  while (read(fd, &byte, 1) == 1) {
    if (!in_packet) {
      if (byte == STARTOFPACKET) {
        in_packet = 1;
        buffer[0] = byte;
        pos = 1;
      }
    } else {
      buffer[pos++] = byte;
      if (byte == ENDOFPACKET) {
        return pos; // Return total packet size
      }
      if (pos >= buffer_size) {
        return -1; // Buffer overflow (invalid packet)
      }
    }
  }

  return -1; // Read error or EOF
}

ssize_t uart_send_packet(int fd, const uint8_t *packet, size_t len) {
  if (fd < 0 || packet == NULL || len == 0) {
    return -1;
  }

  ssize_t written = write(fd, packet, len);
  if (written != len) {
    return -1; // Partial or failed write
  }
  return written;
}
