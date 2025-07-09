#ifndef CMD_BRIDGE_H
#define CMD_BRIDGE_H

#include <stdbool.h>

// Command result structure
typedef struct {
    bool success;
    char message[256];
    int result_code;
} cmd_result_t;

// Command callback function type
typedef cmd_result_t (*cmd_handler_func)(const char* args);

// Command definition structure
typedef struct {
    const char* command;
    cmd_handler_func handler;
    const char* description;
} cmd_definition_t;

// Initialize the command bridge system
void cmd_bridge_init();

// Register a custom command handler
bool cmd_bridge_register(const char* command, cmd_handler_func handler, const char* description);

// Process a command string (main entry point for all sources)
cmd_result_t cmd_bridge_process(const char* command_line);

// Send command to /dev/MiSTer_cmd
bool cmd_bridge_send_to_mister(const char* command);

// Built-in command handlers
cmd_result_t cmd_load_core(const char* args);
cmd_result_t cmd_load_game(const char* args);
cmd_result_t cmd_mount_image(const char* args);
cmd_result_t cmd_reset_core(const char* args);
cmd_result_t cmd_set_option(const char* args);
cmd_result_t cmd_screenshot(const char* args);
cmd_result_t cmd_menu_navigate(const char* args);
cmd_result_t cmd_search_files(const char* args);
cmd_result_t cmd_search_games(const char* args);
cmd_result_t cmd_search_cores(const char* args);
cmd_result_t cmd_search_select(const char* args);
cmd_result_t cmd_search_load(const char* args);
cmd_result_t cmd_popup_browse(const char* args);
cmd_result_t cmd_nfc_setup(const char* args);
cmd_result_t cmd_nfc_poll(const char* args);

// Utility functions
bool cmd_bridge_is_mister_cmd_available();
void cmd_bridge_list_commands();

#endif // CMD_BRIDGE_H