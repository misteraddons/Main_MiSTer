#include "misteresp.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

void hex_dump(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    printf("%02X ", data[i]);
  }
  printf("\n");
}

int read_exact(int fd, uint8_t *buf, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = read(fd, buf + total, len - total);
    if (n <= 0)
      return -1; // error or EOF
    total += n;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s /dev/pts/X\n", argv[0]);
    return 1;
  }

  int fd = open(argv[1], O_RDONLY | O_NOCTTY);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  struct termios tty;
  tcgetattr(fd, &tty);
  cfsetospeed(&tty, B115200);
  cfsetispeed(&tty, B115200);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_cflag |= CREAD | CLOCAL;
  tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
  tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_oflag &= ~OPOST;
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 1;
  tcsetattr(fd, TCSANOW, &tty);

  uint8_t byte;
  while (1) {
    // Step 1: Wait for STARTOFPACKET
    if (read(fd, &byte, 1) <= 0)
      break;
    if (byte != STARTOFPACKET)
      continue;

    // Step 2: Read CMD and LEN
    uint8_t header[2]; // CMD and LEN
    if (read_exact(fd, header, 2) < 0)
      break;
    uint8_t cmd = header[0];
    uint8_t len = header[1];

    if (len > MAX_PAYLOAD_SIZE) {
      fprintf(stderr, "Invalid LEN: %d\n", len);
      continue;
    }

    // Step 3: Read PAYLOAD, CRC, END
    uint8_t *packet = malloc(PACKET_OVERHEAD + len);
    packet[0] = STARTOFPACKET;
    packet[1] = cmd;
    packet[2] = len;

    if (read_exact(fd, &packet[3], len + 2) < 0) {
      free(packet);
      break;
    }

    // Step 4: Verify END byte
    if (packet[3 + len + 1] != ENDOFPACKET) {
      fprintf(stderr, "Invalid END byte: 0x%02X\n", packet[3 + len + 1]);
      free(packet);
      continue;
    }

    printf("Full packet received (%d bytes):\n", PACKET_OVERHEAD + len);
    // hex_dump(packet, PACKET_OVERHEAD + len);
    Misterpacket *incoming = deserialize_packet(packet);
    if (incoming) {
      printf("Command: %d\nLength: %d\nPayload: %s\n", incoming->cmd,
             incoming->len, incoming->payload);
    } else
      printf("CRC check failure.\n");
    free(packet);
  }

  close(fd);
  return 0;
}
