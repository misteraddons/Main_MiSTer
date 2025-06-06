#include "misteresp.h"
#include <stdio.h>
#include <string.h>

int main() {
  const char *payload = "MiSTer";
  Misterpacket mypacket, *upacket;
  mypacket.cmd = CMD_BUTTON_ACTION;
  mypacket.len = strlen(payload) + 1;
  mypacket.payload = (uint8_t *)payload;
  uint8_t *spacket = serialize_packet(mypacket);
  upacket = deserialize_packet(spacket);
  if (upacket != NULL) {
    printf("%d %d %s\n", upacket->cmd, upacket->len, upacket->payload);
  }
  return 0;
}
