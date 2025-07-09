#include "cmd_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

// Command registry
static std::vector<cmd_definition_t> registered_commands;
static bool cmd_bridge_initialized = false;
static const char* MISTER_CMD_DEVICE = "/dev/MiSTer_cmd";

// Forward declarations for built-in handlers
static void register_builtin_commands();

void cmd_bridge_init()
{
    if (cmd_bridge_initialized) return;
    
    printf("CMD: Initializing command bridge system\n");
    
    // Clear any existing registrations
    registered_commands.clear();
    
    // Register built-in commands
    register_builtin_commands();
    
    // Check if MiSTer_cmd device is available
    if (cmd_bridge_is_mister_cmd_available())
    {
        printf("CMD: /dev/MiSTer_cmd is available\n");
    }
    else
    {
        printf("CMD: /dev/MiSTer_cmd not found - some commands may not work\n");
    }
    
    cmd_bridge_initialized = true;
}

bool cmd_bridge_register(const char* command, cmd_handler_func handler, const char* description)
{
    if (!command || !handler) return false;
    
    // Check if command already exists
    for (const auto& cmd : registered_commands)
    {
        if (strcasecmp(cmd.command, command) == 0)
        {
            printf("CMD: Command '%s' already registered\n", command);
            return false;
        }
    }
    
    // Add new command
    cmd_definition_t new_cmd = {
        command,
        handler,
        description ? description : ""
    };
    
    registered_commands.push_back(new_cmd);
    printf("CMD: Registered command '%s'\n", command);
    
    return true;
}

cmd_result_t cmd_bridge_process(const char* command_line)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!cmd_bridge_initialized)
    {
        cmd_bridge_init();
    }
    
    if (!command_line || !command_line[0])
    {
        strcpy(result.message, "Empty command");
        return result;
    }
    
    // Make a copy for parsing
    char cmd_copy[512];
    strncpy(cmd_copy, command_line, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    // Remove leading/trailing whitespace
    char* cmd_start = cmd_copy;
    while (*cmd_start && (*cmd_start == ' ' || *cmd_start == '\t')) cmd_start++;
    
    char* cmd_end = cmd_start + strlen(cmd_start) - 1;
    while (cmd_end > cmd_start && (*cmd_end == ' ' || *cmd_end == '\t' || *cmd_end == '\n' || *cmd_end == '\r'))
    {
        *cmd_end = '\0';
        cmd_end--;
    }
    
    if (!*cmd_start)
    {
        strcpy(result.message, "Empty command after trimming");
        return result;
    }
    
    // Split command and arguments
    char* space = strchr(cmd_start, ' ');
    char* args = NULL;
    
    if (space)
    {
        *space = '\0';
        args = space + 1;
        while (*args && (*args == ' ' || *args == '\t')) args++;
    }
    
    printf("CMD: Processing command='%s' args='%s'\n", cmd_start, args ? args : "(none)");
    
    // Look for registered handler
    for (const auto& cmd : registered_commands)
    {
        if (strcasecmp(cmd.command, cmd_start) == 0)
        {
            printf("CMD: Found handler for '%s'\n", cmd_start);
            return cmd.handler(args);
        }
    }
    
    // If not found, try sending to MiSTer_cmd if it looks like a raw command
    if (cmd_bridge_is_mister_cmd_available())
    {
        printf("CMD: No handler found, forwarding to MiSTer_cmd\n");
        if (cmd_bridge_send_to_mister(command_line))
        {
            result.success = true;
            snprintf(result.message, sizeof(result.message), "Forwarded to MiSTer_cmd: %s", command_line);
            result.result_code = 0;
        }
        else
        {
            snprintf(result.message, sizeof(result.message), "Failed to forward to MiSTer_cmd");
        }
    }
    else
    {
        snprintf(result.message, sizeof(result.message), "Unknown command: %s", cmd_start);
    }
    
    return result;
}

bool cmd_bridge_send_to_mister(const char* command)
{
    if (!command || !command[0]) return false;
    
    int fd = open(MISTER_CMD_DEVICE, O_WRONLY);
    if (fd < 0)
    {
        printf("CMD: Failed to open %s: %s\n", MISTER_CMD_DEVICE, strerror(errno));
        return false;
    }
    
    size_t len = strlen(command);
    ssize_t written = write(fd, command, len);
    
    if (written < 0)
    {
        printf("CMD: Failed to write to %s: %s\n", MISTER_CMD_DEVICE, strerror(errno));
        close(fd);
        return false;
    }
    
    // Add newline if not present
    if (command[len - 1] != '\n')
    {
        write(fd, "\n", 1);
    }
    
    close(fd);
    
    printf("CMD: Sent to MiSTer_cmd: %s\n", command);
    return true;
}

bool cmd_bridge_is_mister_cmd_available()
{
    struct stat st;
    return (stat(MISTER_CMD_DEVICE, &st) == 0);
}

void cmd_bridge_list_commands()
{
    printf("CMD: Available commands:\n");
    printf("CMD: %-20s %s\n", "Command", "Description");
    printf("CMD: %-20s %s\n", "-------", "-----------");
    
    for (const auto& cmd : registered_commands)
    {
        printf("CMD: %-20s %s\n", cmd.command, cmd.description);
    }
    
    if (cmd_bridge_is_mister_cmd_available())
    {
        printf("CMD: (Additional MiSTer_cmd commands available)\n");
    }
}

// Built-in command implementations

cmd_result_t cmd_load_core(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: load_core <rbf_name>");
        return result;
    }
    
    // Construct load_core command for MiSTer_cmd
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "load_core %s", args);
    
    if (cmd_bridge_send_to_mister(cmd))
    {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Loading core: %s", args);
        result.result_code = 0;
    }
    else
    {
        strcpy(result.message, "Failed to send load_core command");
    }
    
    return result;
}

cmd_result_t cmd_load_game(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: load_game <path_to_game>");
        return result;
    }
    
    // This will be enhanced by smart_game_loading branch
    // For now, just forward to MiSTer_cmd
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "load_rom %s", args);
    
    if (cmd_bridge_send_to_mister(cmd))
    {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Loading game: %s", args);
        result.result_code = 0;
    }
    else
    {
        strcpy(result.message, "Failed to send load_rom command");
    }
    
    return result;
}

cmd_result_t cmd_mount_image(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: mount_image <index> <path>");
        return result;
    }
    
    // Parse index and path
    char* endptr;
    int index = strtol(args, &endptr, 10);
    
    if (endptr == args || !*endptr)
    {
        strcpy(result.message, "Invalid mount_image arguments");
        return result;
    }
    
    // Skip whitespace
    while (*endptr && (*endptr == ' ' || *endptr == '\t')) endptr++;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mount %d %s", index, endptr);
    
    if (cmd_bridge_send_to_mister(cmd))
    {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Mounting image %d: %s", index, endptr);
        result.result_code = 0;
    }
    else
    {
        strcpy(result.message, "Failed to send mount command");
    }
    
    return result;
}

cmd_result_t cmd_reset_core(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    // Reset can take optional parameter (cold/warm)
    const char* reset_type = args && args[0] ? args : "cold";
    
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "reset %s", reset_type);
    
    if (cmd_bridge_send_to_mister(cmd))
    {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Core reset (%s)", reset_type);
        result.result_code = 0;
    }
    else
    {
        strcpy(result.message, "Failed to send reset command");
    }
    
    return result;
}

cmd_result_t cmd_set_option(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: set_option <option> <value>");
        return result;
    }
    
    // Forward to MiSTer_cmd as config command
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "config %s", args);
    
    if (cmd_bridge_send_to_mister(cmd))
    {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Setting option: %s", args);
        result.result_code = 0;
    }
    else
    {
        strcpy(result.message, "Failed to send config command");
    }
    
    return result;
}

cmd_result_t cmd_screenshot(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    // Screenshot can optionally take a filename
    if (args && args[0])
    {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "screenshot %s", args);
        
        if (cmd_bridge_send_to_mister(cmd))
        {
            result.success = true;
            snprintf(result.message, sizeof(result.message), "Screenshot saved: %s", args);
            result.result_code = 0;
        }
    }
    else
    {
        if (cmd_bridge_send_to_mister("screenshot"))
        {
            result.success = true;
            strcpy(result.message, "Screenshot saved");
            result.result_code = 0;
        }
    }
    
    if (!result.success)
    {
        strcpy(result.message, "Failed to take screenshot");
    }
    
    return result;
}

cmd_result_t cmd_menu_navigate(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: menu <up|down|left|right|ok|back>");
        return result;
    }
    
    // Convert navigation commands to MiSTer_cmd format
    char cmd[64];
    
    if (strcasecmp(args, "up") == 0)
        strcpy(cmd, "key up");
    else if (strcasecmp(args, "down") == 0)
        strcpy(cmd, "key down");
    else if (strcasecmp(args, "left") == 0)
        strcpy(cmd, "key left");
    else if (strcasecmp(args, "right") == 0)
        strcpy(cmd, "key right");
    else if (strcasecmp(args, "ok") == 0 || strcasecmp(args, "enter") == 0)
        strcpy(cmd, "key enter");
    else if (strcasecmp(args, "back") == 0 || strcasecmp(args, "esc") == 0)
        strcpy(cmd, "key esc");
    else if (strcasecmp(args, "menu") == 0)
        strcpy(cmd, "key f12");
    else
    {
        snprintf(result.message, sizeof(result.message), "Unknown menu command: %s", args);
        return result;
    }
    
    if (cmd_bridge_send_to_mister(cmd))
    {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Menu: %s", args);
        result.result_code = 0;
    }
    else
    {
        strcpy(result.message, "Failed to send menu command");
    }
    
    return result;
}

// Helper function for help command
static cmd_result_t cmd_help(const char* args)
{
    cmd_result_t result = { true, "Available commands listed to console", 0 };
    cmd_bridge_list_commands();
    return result;
}

// Register all built-in commands
static void register_builtin_commands()
{
    cmd_bridge_register("help", cmd_help, "List available commands");
    cmd_bridge_register("load_core", cmd_load_core, "Load an RBF core file");
    cmd_bridge_register("load_game", cmd_load_game, "Load a game/ROM file");
    cmd_bridge_register("mount_image", cmd_mount_image, "Mount disk image to index");
    cmd_bridge_register("reset", cmd_reset_core, "Reset the current core");
    cmd_bridge_register("set_option", cmd_set_option, "Set core configuration option");
    cmd_bridge_register("screenshot", cmd_screenshot, "Take a screenshot");
    cmd_bridge_register("menu", cmd_menu_navigate, "Navigate OSD menu (up/down/left/right/ok/back)");
}