#ifndef ESP32_H
#define ESP32_H

#include "esp32_proto.h"

// Initialize ESP32 communication
void esp32_init(void);

// Update functions to be called from main loop
void esp32_update(void);

// Send current game/core info
void esp32_send_now_playing(const char* uuid, const char* title, const char* core,
                           const char* genre, uint16_t year, uint8_t players,
                           bool is_favorite);

// Send menu state
void esp32_send_menu_state(const char* uuid, uint8_t menu_type, uint16_t index,
                          bool in_menu);

// Send controller state
void esp32_send_controller(uint32_t buttons, uint8_t player);

// Send debug info (filtered)
void esp32_send_debug(const char* message);

// Handle received packets
void esp32_handle_packet(const esp32_packet_t* packet);

#endif // ESP32_H 