#ifndef ESP32_PROTO_H
#define ESP32_PROTO_H

#include <stdint.h>
#include <stdbool.h>

// Protocol constants
#define ESP32_PROTO_START_BYTE 0xAA
#define ESP32_PROTO_MAX_PAYLOAD 1024  // Maximum payload size in bytes

// Message types (MiSTer -> ESP32)
#define ESP32_PROTO_NOW_PLAYING    0x01  // Current game/core info
#define ESP32_PROTO_MENU_STATE     0x02  // Menu state and highlighted item
#define ESP32_PROTO_DEBUG_INFO     0x03  // Filtered debug information
#define ESP32_PROTO_CONTROLLER     0x04  // Controller button state
#define ESP32_PROTO_SYSTEM_STATE   0x05  // System state (volume, brightness, etc.)
#define ESP32_PROTO_WIFI_INFO      0x06  // WiFi SSID/password (if needed)

// Message types (ESP32 -> MiSTer)
#define ESP32_PROTO_LAUNCH_GAME    0x10  // Request to launch a game
#define ESP32_PROTO_REMOTE_CMD     0x11  // Remote control command
#define ESP32_PROTO_RUN_SCRIPT     0x12  // Request to run a script
#define ESP32_PROTO_IMAGE_REQ      0x13  // Request for missing image
#define ESP32_PROTO_WIFI_CONFIG    0x14  // Update WiFi configuration

// Remote command types (mapped to PS2 scancodes)
// Common functional keys/combos used in cores
#define ESP32_CMD_MENU            0x58  // F12 - open/close OSD menu/submenu
#define ESP32_CMD_CORE_SELECT     0x11  // Alt+F12 - quick core selection
#define ESP32_CMD_USER_BUTTON     0x14  // Left Ctrl+Left Alt+Right Alt - USER button (reset)
#define ESP32_CMD_MISTER_RESET    0x12  // Left Shift+Left Ctrl+Left Alt+Right Alt - MiSTer reset

// Navigation keys
#define ESP32_CMD_UP              0x75  // Up Arrow
#define ESP32_CMD_DOWN            0x72  // Down Arrow
#define ESP32_CMD_LEFT            0x6B  // Left Arrow
#define ESP32_CMD_RIGHT           0x74  // Right Arrow
#define ESP32_CMD_SELECT          0x5A  // Enter
#define ESP32_CMD_BACK            0x76  // Escape

// Additional function keys
#define ESP32_CMD_F1              0x3B  // F1
#define ESP32_CMD_F2              0x3C  // F2
#define ESP32_CMD_F3              0x3D  // F3
#define ESP32_CMD_F4              0x3E  // F4
#define ESP32_CMD_F5              0x3F  // F5
#define ESP32_CMD_F6              0x40  // F6
#define ESP32_CMD_F7              0x41  // F7
#define ESP32_CMD_F8              0x42  // F8
#define ESP32_CMD_F9              0x43  // F9
#define ESP32_CMD_F10             0x44  // F10
#define ESP32_CMD_F11             0x45  // F11
#define ESP32_CMD_F12             0x58  // F12

// Emulation mode switch
#define ESP32_CMD_MOUSE_MODE      0x77  // Num Lock - switch to mouse emulation
#define ESP32_CMD_JOY1_MODE       0x77  // Num Lock - switch to joystick 1 emulation
#define ESP32_CMD_JOY2_MODE       0x7E  // Scroll Lock - switch to joystick 2 emulation
#define ESP32_CMD_KBD_MODE        0x7E  // Scroll Lock - switch to keyboard mode

// Joystick button mappings (when in joystick mode)
#define ESP32_JOY_UP              0x75  // Up Arrow
#define ESP32_JOY_DOWN            0x72  // Down Arrow
#define ESP32_JOY_LEFT            0x6B  // Left Arrow
#define ESP32_JOY_RIGHT           0x74  // Right Arrow
#define ESP32_JOY_A               0x1C  // A
#define ESP32_JOY_B               0x32  // B
#define ESP32_JOY_X               0x21  // X
#define ESP32_JOY_Y               0x23  // Y
#define ESP32_JOY_L               0x2B  // L
#define ESP32_JOY_R               0x34  // R
#define ESP32_JOY_START           0x5A  // Enter
#define ESP32_JOY_SELECT          0x76  // Escape
#define ESP32_JOY_MENU            0x58  // F12

// PS2 scancode flags
#define PS2_FLAG_EXTENDED    0xE0  // Extended scancode prefix
#define PS2_FLAG_RELEASE     0xF0  // Key release prefix
#define PS2_FLAG_EXT_RELEASE 0xE0F0 // Extended key release prefix

// PS2 modifier key scancodes
#define PS2_LCTRL           0x14
#define PS2_LSHIFT          0x12
#define PS2_LALT            0x11
#define PS2_RALT            0x11  // Extended scancode

// Packet structure (packed to avoid alignment issues)
#pragma pack(push, 1)
typedef struct {
    uint8_t start;     // Start byte (0xAA)
    uint8_t type;      // Message type
    uint16_t length;   // Payload length (big endian)
    uint8_t payload[ESP32_PROTO_MAX_PAYLOAD];  // Variable length payload
    uint16_t crc;      // CRC-16-CCITT
} esp32_packet_t;

// Now Playing payload structure
typedef struct {
    char uuid[64];     // Game UUID
    char title[64];    // Game title
    char core[32];     // Core name
    char genre[32];    // Game genre
    uint16_t year;     // Release year
    uint8_t players;   // Number of players
    bool is_favorite;  // Favorite status
} esp32_now_playing_t;

// Menu State payload structure
typedef struct {
    char uuid[64];     // Currently highlighted game UUID
    uint8_t menu_type; // Menu type (core select, game select, etc.)
    uint16_t index;    // Highlighted index
    bool in_menu;      // Whether currently in a menu
} esp32_menu_state_t;

// Controller State payload structure
typedef struct {
    uint32_t buttons;  // Button state bitmask
    uint8_t player;    // Player number (0-3)
} esp32_controller_t;

// Launch Game payload structure
typedef struct {
    char uuid[64];     // Game UUID to launch
    bool force_core;   // Whether to force specific core
    char core[32];     // Core name (if force_core is true)
} esp32_launch_game_t;

// Remote Command payload structure (now using PS2 scancodes)
typedef struct {
    uint8_t scancode;  // PS2 scancode
    bool is_release;   // Whether this is a key release
    bool is_extended;  // Whether this is an extended scancode
    uint8_t modifiers; // Bitmask of modifier keys (CTRL, SHIFT, ALT)
} esp32_remote_cmd_t;
#pragma pack(pop)

// Function prototypes
void esp32_proto_init(void);
void esp32_proto_send_packet(uint8_t type, const void* payload, uint16_t length);
bool esp32_proto_receive_packet(esp32_packet_t* packet);
uint16_t esp32_proto_crc16(const uint8_t* data, uint16_t length);

// Helper functions for common messages
void esp32_proto_send_now_playing(const esp32_now_playing_t* info);
void esp32_proto_send_menu_state(const esp32_menu_state_t* state);
void esp32_proto_send_controller(const esp32_controller_t* controller);
void esp32_proto_send_debug_info(const char* message);

#endif // ESP32_PROTO_H 