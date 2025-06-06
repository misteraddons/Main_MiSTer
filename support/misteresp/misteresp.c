#include "misteresp.h"

static inline uint8_t calc_crc(Misterpacket *mp);

uint8_t *serialize_packet(Misterpacket mp) {
  if (mp.len > MAX_PAYLOAD_SIZE) {
    return NULL;
  }

  uint8_t *packet = malloc(PACKET_OVERHEAD + mp.len);
  packet[0] = STARTOFPACKET;
  packet[1] = mp.cmd;
  packet[2] = mp.len;
  for (int i = 0; i != mp.len; i++) {
    packet[3 + i] = mp.payload[i];
  }
  packet[3 + mp.len] = calc_crc(&mp);
  packet[4 + mp.len] = ENDOFPACKET;
  return packet;
}

Misterpacket *deserialize_packet(uint8_t *mp) {
  Misterpacket *packet = malloc(sizeof(Misterpacket));
  packet->cmd = mp[1];
  packet->len = mp[2];
  if (packet->len > MAX_PAYLOAD_SIZE) {
    return NULL;
  }
  packet->payload = malloc(packet->len);
  for (int i = 0; i != packet->len; i++) {
    packet->payload[i] = mp[3 + i];
  }
  if (calc_crc(packet) != mp[3 + packet->len]) {
    return NULL;
  }
  return packet;
}

static inline uint8_t calc_crc(Misterpacket *mp) {
  uint8_t result = mp->cmd ^ mp->len;
  for (int i = 0; i < mp->len; i++) {
    result ^= mp->payload[i];
  }
  return result;
}

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
