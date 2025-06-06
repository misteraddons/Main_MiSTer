#include "misteresp.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s /dev/pts/X\n", argv[0]);
    return 1;
  }

  int fd = open(argv[1], O_WRONLY | O_NOCTTY);
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

  const char *payload = "MiSTer";
  Misterpacket mypacket;
  mypacket.cmd = CMD_BUTTON_ACTION;
  mypacket.len = strlen(payload) + 1;
  mypacket.payload = (uint8_t *)payload;
  uint8_t *packet = serialize_packet(mypacket);
  ssize_t written = write(fd, packet, PACKET_OVERHEAD + mypacket.len);
  if (written < 0) {
    perror("write");
  } else {
    printf("Sent %zd bytes\n", written);
  }

  close(fd);
  return 0;
}
