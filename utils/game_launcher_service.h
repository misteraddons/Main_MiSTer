/*
 * MiSTer Game Launcher Service
 * 
 * Unified service for game identification, GameDB lookup, and MGL creation
 * Supports multiple input sources: CD-ROM, NFC, network, GPIO, etc.
 */

#ifndef GAME_LAUNCHER_SERVICE_H
#define GAME_LAUNCHER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

// Game identification methods
typedef enum {
    GAME_ID_SERIAL,      // Game serial number (PSX: SLUS-00067, Saturn: T-8109H)
    GAME_ID_TITLE,       // Game title for fuzzy search
    GAME_ID_UUID,        // UUID stored on NFC card
    GAME_ID_HASH,        // Disc/ROM hash for verification
    GAME_ID_BARCODE,     // Physical barcode scanning
    GAME_ID_CUSTOM       // Custom identification string
} game_id_type_t;

// Game request structure
typedef struct {
    char system[32];           // Core system: PSX, Saturn, MegaCD, etc.
    game_id_type_t id_type;    // How to identify the game
    char identifier[256];      // Serial, title, UUID, etc.
    char source[64];           // Where request came from: "cdrom", "nfc", "network"
    
    // Optional metadata
    char region[32];           // Preferred region
    char language[32];         // Preferred language
    int min_score;             // Minimum fuzzy match score
    bool auto_launch;          // Launch immediately or show selection
    
    // Source-specific data
    union {
        struct {
            char device_path[256];  // /dev/sr0
            bool physical_disc;     // true if physical disc
        } cdrom;
        
        struct {
            char card_uid[64];      // NFC card UID
            char card_data[512];    // Additional card data
        } nfc;
        
        struct {
            char client_ip[64];     // Network client IP
            uint16_t client_port;   // Network client port
        } network;
        
        struct {
            int gpio_pin;           // GPIO pin number
            int button_state;       // Button state
        } gpio;
    } source_data;
} game_request_t;

// Game response structure
typedef struct {
    bool success;
    char error_message[256];
    
    // Found game info
    char game_title[256];
    char game_region[32];
    char game_language[32];
    char disc_id[64];
    
    // File information
    char file_path[512];
    char mgl_path[512];
    int match_score;
    
    // Multiple matches
    int match_count;
    struct {
        char title[256];
        char path[512];
        int score;
    } matches[10];
    
} game_response_t;

// Service API
bool game_launcher_init(const char* config_path);
void game_launcher_cleanup();

// Main game lookup function
bool game_launcher_find_game(const game_request_t* request, game_response_t* response);

// MGL creation functions
bool game_launcher_create_mgl(const char* system, const char* title, const char* file_path, char* mgl_path);
bool game_launcher_create_selection_mgls(const game_response_t* response);

// Launch functions
bool game_launcher_launch_game(const char* mgl_path);
bool game_launcher_show_selection(const game_response_t* response);

// Utility functions
bool game_launcher_validate_system(const char* system);
const char* game_launcher_get_system_core(const char* system);
bool game_launcher_send_osd_message(const char* message);

// Service control
#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define GAME_LAUNCHER_PID_FILE "/tmp/game_launcher.pid"

// Command protocol
typedef enum {
    CMD_FIND_GAME,
    CMD_LAUNCH_GAME,
    CMD_CREATE_MGL,
    CMD_GET_STATUS,
    CMD_RELOAD_CONFIG,
    CMD_SHUTDOWN
} launcher_command_t;

#endif // GAME_LAUNCHER_SERVICE_H