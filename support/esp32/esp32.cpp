#include "esp32.h"
#include <stdio.h>
#include <string.h>
#include "../../user_io.h"  // For PS2 keyboard functions
#include "../../input.h"    // For input handling
#include "../../hardware.h" // For GetTimer

// Modifier key bitmasks
#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_RALT  0x08

// Key repeat settings
#define KEY_REPEAT_DELAY 250  // ms before repeat starts (changed from 500ms)
#define KEY_REPEAT_RATE  50   // ms between repeats

// Emulation mode
typedef enum {
    EMU_MODE_KEYBOARD = 0,
    EMU_MODE_MOUSE,
    EMU_MODE_JOY1,
    EMU_MODE_JOY2
} emu_mode_t;

// Forward declarations
static void send_ps2_scancode(uint8_t scancode, bool is_release, bool is_extended, uint8_t modifiers);
static void switch_emu_mode(emu_mode_t new_mode);
static void handle_joystick_button(uint8_t scancode, bool is_release);

static emu_mode_t current_mode = EMU_MODE_KEYBOARD;
static uint32_t last_key_time = 0;
static uint8_t last_key = 0;
static bool last_key_release = false;
static uint8_t last_modifiers = 0;

// Joystick button state
static uint32_t joy_buttons = 0;

void esp32_init(void) {
    // Initialize protocol
    esp32_proto_init();
}

void esp32_update(void) {
    // Check for received packets
    esp32_packet_t packet;
    if (esp32_proto_receive_packet(&packet)) {
        esp32_handle_packet(&packet);
    }

    // Handle key repeat
    if (last_key && !last_key_release) {
        uint32_t current_time = GetTimer(0);
        if (current_time - last_key_time >= KEY_REPEAT_DELAY) {
            // Send repeated key
            send_ps2_scancode(last_key, false, false, last_modifiers);
            last_key_time = current_time;
        }
    }

    // Update joystick state if in joystick mode
    if (current_mode == EMU_MODE_JOY1 || current_mode == EMU_MODE_JOY2) {
        user_io_send_buttons(joy_buttons);  // Only send button state
    }
}

// Helper function to switch emulation mode
static void switch_emu_mode(emu_mode_t new_mode) {
    if (new_mode == current_mode) return;

    // Send Num Lock/Scroll Lock sequence
    switch (new_mode) {
        case EMU_MODE_MOUSE:
            user_io_kbd(0x77, 1);  // Num Lock press
            user_io_kbd(0x77, 0);  // Num Lock release
            break;
        case EMU_MODE_JOY1:
            user_io_kbd(0x77, 1);  // Num Lock press
            user_io_kbd(0x77, 0);  // Num Lock release
            break;
        case EMU_MODE_JOY2:
            user_io_kbd(0x7E, 1);  // Scroll Lock press
            user_io_kbd(0x7E, 0);  // Scroll Lock release
            break;
        case EMU_MODE_KEYBOARD:
            user_io_kbd(0x7E, 1);  // Scroll Lock press
            user_io_kbd(0x7E, 0);  // Scroll Lock release
            break;
    }
    current_mode = new_mode;
}

// Helper function to handle joystick button mapping
static void handle_joystick_button(uint8_t scancode, bool is_release) {
    uint32_t button_mask = 0;

    // Map scancode to button mask
    switch (scancode) {
        case ESP32_JOY_UP:     button_mask = 0x0001; break;
        case ESP32_JOY_DOWN:   button_mask = 0x0002; break;
        case ESP32_JOY_LEFT:   button_mask = 0x0004; break;
        case ESP32_JOY_RIGHT:  button_mask = 0x0008; break;
        case ESP32_JOY_A:      button_mask = 0x0010; break;
        case ESP32_JOY_B:      button_mask = 0x0020; break;
        case ESP32_JOY_X:      button_mask = 0x0040; break;
        case ESP32_JOY_Y:      button_mask = 0x0080; break;
        case ESP32_JOY_L:      button_mask = 0x0100; break;
        case ESP32_JOY_R:      button_mask = 0x0200; break;
        case ESP32_JOY_START:  button_mask = 0x0400; break;
        case ESP32_JOY_SELECT: button_mask = 0x0800; break;
        case ESP32_JOY_MENU:   button_mask = 0x1000; break;
    }

    // Update button state
    if (is_release) {
        joy_buttons &= ~button_mask;
    } else {
        joy_buttons |= button_mask;
    }
}

// Helper function to send PS2 scancode with modifiers and repeat handling
static void send_ps2_scancode(uint8_t scancode, bool is_release, bool is_extended, uint8_t modifiers) {
    // Handle emulation mode switch keys
    if (scancode == 0x77) {  // Num Lock
        if (!is_release) {
            switch_emu_mode(current_mode == EMU_MODE_KEYBOARD ? EMU_MODE_MOUSE : 
                          current_mode == EMU_MODE_MOUSE ? EMU_MODE_JOY1 : EMU_MODE_KEYBOARD);
        }
        return;
    }
    if (scancode == 0x7E) {  // Scroll Lock
        if (!is_release) {
            switch_emu_mode(current_mode == EMU_MODE_KEYBOARD ? EMU_MODE_JOY2 : EMU_MODE_KEYBOARD);
        }
        return;
    }

    // Handle joystick mode
    if (current_mode == EMU_MODE_JOY1 || current_mode == EMU_MODE_JOY2) {
        handle_joystick_button(scancode, is_release);
        return;
    }

    // Store key info for repeat handling
    if (!is_release) {
        last_key = scancode;
        last_key_release = false;
        last_modifiers = modifiers;
        last_key_time = GetTimer(0);
    } else {
        last_key = 0;
        last_key_release = true;
    }

    // Send modifier keys first
    if (!is_release) {
        if (modifiers & MOD_CTRL) {
            user_io_kbd(PS2_LCTRL, 1);
        }
        if (modifiers & MOD_SHIFT) {
            user_io_kbd(PS2_LSHIFT, 1);
        }
        if (modifiers & MOD_ALT) {
            user_io_kbd(PS2_LALT, 1);
        }
        if (modifiers & MOD_RALT) {
            user_io_kbd(PS2_FLAG_EXTENDED, 1);
            user_io_kbd(PS2_RALT, 1);
        }
    }

    // Send the main scancode
    if (is_extended) {
        user_io_kbd(PS2_FLAG_EXTENDED, 1);
    }
    if (is_release) {
        user_io_kbd(PS2_FLAG_RELEASE, 1);
    }
    user_io_kbd(scancode, 1);

    // Release modifier keys if this was a key press
    if (!is_release) {
        if (modifiers & MOD_RALT) {
            user_io_kbd(PS2_FLAG_EXTENDED, 1);
            user_io_kbd(PS2_FLAG_RELEASE, 1);
            user_io_kbd(PS2_RALT, 1);
        }
        if (modifiers & MOD_ALT) {
            user_io_kbd(PS2_FLAG_RELEASE, 1);
            user_io_kbd(PS2_LALT, 1);
        }
        if (modifiers & MOD_SHIFT) {
            user_io_kbd(PS2_FLAG_RELEASE, 1);
            user_io_kbd(PS2_LSHIFT, 1);
        }
        if (modifiers & MOD_CTRL) {
            user_io_kbd(PS2_FLAG_RELEASE, 1);
            user_io_kbd(PS2_LCTRL, 1);
        }
    }
}

void esp32_send_now_playing(const char* uuid, const char* title, const char* core,
                           const char* genre, uint16_t year, uint8_t players,
                           bool is_favorite) {
    esp32_now_playing_t info;
    memset(&info, 0, sizeof(info));
    
    strncpy(info.uuid, uuid, sizeof(info.uuid) - 1);
    strncpy(info.title, title, sizeof(info.title) - 1);
    strncpy(info.core, core, sizeof(info.core) - 1);
    strncpy(info.genre, genre, sizeof(info.genre) - 1);
    info.year = year;
    info.players = players;
    info.is_favorite = is_favorite;
    
    esp32_proto_send_now_playing(&info);
}

void esp32_send_menu_state(const char* uuid, uint8_t menu_type, uint16_t index,
                          bool in_menu) {
    esp32_menu_state_t state;
    memset(&state, 0, sizeof(state));
    
    strncpy(state.uuid, uuid, sizeof(state.uuid) - 1);
    state.menu_type = menu_type;
    state.index = index;
    state.in_menu = in_menu;
    
    esp32_proto_send_menu_state(&state);
}

void esp32_send_controller(uint32_t buttons, uint8_t player) {
    esp32_controller_t controller;
    controller.buttons = buttons;
    controller.player = player;
    
    esp32_proto_send_controller(&controller);
}

void esp32_send_debug(const char* message) {
    // Filter debug messages here if needed
    esp32_proto_send_debug_info(message);
}

void esp32_handle_packet(const esp32_packet_t* packet) {
    switch (packet->type) {
        case ESP32_PROTO_LAUNCH_GAME: {
            const esp32_launch_game_t* launch = (const esp32_launch_game_t*)packet->payload;
            // TODO: Implement game launching
            printf("Launch game: %s\n", launch->uuid);
            break;
        }
        
        case ESP32_PROTO_REMOTE_CMD: {
            const esp32_remote_cmd_t* cmd = (const esp32_remote_cmd_t*)packet->payload;
            // Convert to PS2 scancode and send with modifiers
            send_ps2_scancode(cmd->scancode, cmd->is_release, cmd->is_extended, cmd->modifiers);
            break;
        }
        
        case ESP32_PROTO_RUN_SCRIPT: {
            const char* script = (const char*)packet->payload;
            // TODO: Implement script execution
            printf("Run script: %s\n", script);
            break;
        }
        
        case ESP32_PROTO_IMAGE_REQ: {
            const char* uuid = (const char*)packet->payload;
            // TODO: Implement image request handling
            printf("Image request: %s\n", uuid);
            break;
        }
        
        case ESP32_PROTO_WIFI_CONFIG: {
            // TODO: Implement WiFi config
            printf("WiFi config update\n");
            break;
        }
        
        default:
            printf("Unknown packet type: %d\n", packet->type);
            break;
    }
} 