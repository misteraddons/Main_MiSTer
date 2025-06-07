/*
 * Copyright (C) 2024 MiSTer CEC Implementation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef CEC_H
#define CEC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// CEC message structure
struct cec_message {
    uint8_t src;                // Source logical address
    uint8_t dest;               // Destination logical address
    uint8_t opcode;             // CEC opcode
    uint8_t params[14];         // Message parameters (max 14 bytes)
    size_t param_len;           // Number of parameter bytes
};

// Callback function type for CEC messages
typedef void (*cec_callback_t)(const struct cec_message* msg, void* context);

// Initialize CEC subsystem
// device_name: OSD name to advertise (max 14 chars)
// auto_power: Enable automatic TV power on when MiSTer starts
// remote_control: Enable TV remote control support
// Returns 0 on success, -1 on failure
int cec_init(const char* device_name, bool auto_power, bool remote_control);

// Configure CEC with physical address from EDID
// physical_addr: Physical address extracted from EDID (e.g. 0x1000 for 1.0.0.0)
// Returns 0 on success, -1 on failure
int cec_configure(uint16_t physical_addr);

// Shutdown CEC subsystem
void cec_shutdown(void);

// Perform One Touch Play (wake TV and switch to MiSTer input)
// Returns 0 on success, -1 on failure
int cec_one_touch_play(void);

// Send TV to standby mode
// Returns 0 on success, -1 on failure
int cec_standby_tv(void);

// Set callback function for received CEC messages
// callback: Function to call when CEC messages are received
// context: User context passed to callback
void cec_set_callback(cec_callback_t callback, void* context);

// Check if CEC is enabled and operational
bool cec_is_enabled(void);

// Get assigned logical address
uint8_t cec_get_logical_address(void);

// Get physical address
uint16_t cec_get_physical_address(void);

// ADV7513 Register Map Management (internal functions for 30-minute fix)
int adv7513_init_register_maps(int main_i2c_fd);
int adv7513_verify_register_maps(int main_i2c_fd);
int adv7513_reset_register_maps(int main_i2c_fd);

#ifdef __cplusplus
}
#endif

#endif // CEC_H