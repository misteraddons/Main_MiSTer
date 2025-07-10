#include "cmd_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>

#ifndef TEST_BUILD
#include "menu.h"
#include "file_io.h"
#include "support/arcade/mra_loader.h"
#include "cdrom.h"
#endif

// Command registry
#define MAX_COMMANDS 50
static cmd_definition_t registered_commands[MAX_COMMANDS];
static int num_registered_commands = 0;
static bool cmd_bridge_initialized = false;
static const char* MISTER_CMD_DEVICE = "/dev/MiSTer_cmd";

// Forward declarations for built-in handlers
static void register_builtin_commands();

void cmd_bridge_init()
{
    if (cmd_bridge_initialized) return;
    
    printf("CMD: Initializing command bridge system\n");
    
    // Clear any existing registrations
    num_registered_commands = 0;
    
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
    for (int i = 0; i < num_registered_commands; i++)
    {
        if (strcasecmp(registered_commands[i].command, command) == 0)
        {
            printf("CMD: Command '%s' already registered\n", command);
            return false;
        }
    }
    
    // Check if we have space for more commands
    if (num_registered_commands >= MAX_COMMANDS)
    {
        printf("CMD: Maximum number of commands reached\n");
        return false;
    }
    
    // Add new command
    registered_commands[num_registered_commands].command = command;
    registered_commands[num_registered_commands].handler = handler;
    registered_commands[num_registered_commands].description = description ? description : "";
    num_registered_commands++;
    
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
    for (int i = 0; i < num_registered_commands; i++)
    {
        if (strcasecmp(registered_commands[i].command, cmd_start) == 0)
        {
            printf("CMD: Found handler for '%s'\n", cmd_start);
            return registered_commands[i].handler(args);
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
    printf("CMD: cmd_bridge_send_to_mister called with: %s\n", command ? command : "NULL");
    
    if (!command || !command[0]) {
        printf("CMD: Empty command, returning false\n");
        return false;
    }
    
    printf("CMD: Attempting to open %s\n", MISTER_CMD_DEVICE);
    
    // Use O_RDWR which seems to work better with MiSTer_cmd
    int fd = open(MISTER_CMD_DEVICE, O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        printf("CMD: Failed to open %s with O_RDWR: %s (errno=%d)\n", MISTER_CMD_DEVICE, strerror(errno), errno);
        return false;
    }
    
    printf("CMD: Device opened successfully, fd=%d\n", fd);
    
    size_t len = strlen(command);
    printf("CMD: Writing %zu bytes to device\n", len);
    ssize_t written = write(fd, command, len);
    
    if (written < 0)
    {
        printf("CMD: Failed to write to %s: %s (errno=%d)\n", MISTER_CMD_DEVICE, strerror(errno), errno);
        close(fd);
        return false;
    }
    
    printf("CMD: Wrote %zd bytes successfully\n", written);
    
    // Add newline if not present
    if (command[len - 1] != '\n')
    {
        printf("CMD: Adding newline\n");
        write(fd, "\n", 1);
    }
    
    printf("CMD: Closing device\n");
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
    
    for (int i = 0; i < num_registered_commands; i++)
    {
        printf("CMD: %-20s %s\n", registered_commands[i].command, registered_commands[i].description);
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
    
    printf("CMD: cmd_load_game called with args: '%s'\n", args);
    
    // Detect system from path
    const char* system = NULL;
    if (strstr(args, "/Saturn/") || strstr(args, "/saturn/")) {
        system = "Saturn";
    } else if (strstr(args, "/PSX/") || strstr(args, "/psx/")) {
        system = "PSX";
    } else if (strstr(args, "/MegaCD/") || strstr(args, "/megacd/")) {
        system = "MegaCD";
    } else if (strstr(args, "/NeoGeo/") || strstr(args, "/neogeo/")) {
        system = "NeoGeo";
    }
    
    printf("CMD: Detected system: %s\n", system ? system : "unknown");
    
    // For CD-based systems, create an MGL file
    if (system && (strcmp(system, "Saturn") == 0 || strcmp(system, "PSX") == 0 || 
                   strcmp(system, "MegaCD") == 0 || strcmp(system, "NeoGeo") == 0)) {
        
        printf("CMD: Creating MGL file for CD-based system\n");
        
        // Create MGL file in /media/fat where MiSTer expects it
        char mgl_path[512];
        snprintf(mgl_path, sizeof(mgl_path), "/media/fat/autoload_%s.mgl", system);
        printf("CMD: MGL path: %s\n", mgl_path);
        
        FILE* mgl = fopen(mgl_path, "w");
        if (!mgl) {
            printf("CMD: ERROR - Failed to create MGL file at %s\n", mgl_path);
            strcpy(result.message, "Failed to create MGL file");
            return result;
        }
        
        printf("CMD: Writing MGL content...\n");
        
        // Write MGL content based on system
        fprintf(mgl, "<mistergamedescription>\n");
        fprintf(mgl, "    <rbf>_Console/%s</rbf>\n", system);
        
        if (strcmp(system, "Saturn") == 0) {
            // Saturn uses index 0 for CD mounting with 1 second delay
            fprintf(mgl, "    <file delay=\"1\" type=\"s\" index=\"0\" path=\"%s\"/>\n", args);
            printf("CMD: Wrote Saturn CD mount entry\n");
        } else if (strcmp(system, "PSX") == 0) {
            // PSX also uses index 0
            fprintf(mgl, "    <file delay=\"1\" type=\"s\" index=\"0\" path=\"%s\"/>\n", args);
        } else if (strcmp(system, "MegaCD") == 0) {
            // MegaCD might use different index
            fprintf(mgl, "    <file delay=\"1\" type=\"s\" index=\"0\" path=\"%s\"/>\n", args);
        } else if (strcmp(system, "NeoGeo") == 0) {
            // NeoGeo CD
            fprintf(mgl, "    <file delay=\"1\" type=\"s\" index=\"0\" path=\"%s\"/>\n", args);
        }
        
        fprintf(mgl, "</mistergamedescription>\n");
        fclose(mgl);
        
        printf("CMD: MGL file created successfully\n");
        
        // Verify file was created and show content
        FILE* verify = fopen(mgl_path, "r");
        if (verify) {
            printf("CMD: MGL file verified to exist\n");
            printf("CMD: MGL file content:\n");
            char line[256];
            while (fgets(line, sizeof(line), verify)) {
                printf("CMD:   %s", line);
            }
            fclose(verify);
        } else {
            printf("CMD: ERROR - MGL file not found after creation!\n");
        }
        
        // Check if MiSTer_cmd is available
        if (!cmd_bridge_is_mister_cmd_available()) {
            printf("CMD: WARNING - /dev/MiSTer_cmd not available\n");
            printf("CMD: Attempting alternative method - direct MGL execution\n");
            
            // Try to execute MGL directly using the menu system
            printf("CMD: TODO: Need to implement direct MGL loading via menu system\n");
            result.success = false;
            strcpy(result.message, "MiSTer_cmd not available - direct MGL loading not yet implemented");
            return result;
        }
        
        // Also copy to a standard location that MiSTer might auto-execute
        char autorun_path[512];
        snprintf(autorun_path, sizeof(autorun_path), "/media/fat/autorun.mgl");
        
        FILE* src = fopen(mgl_path, "r");
        FILE* dst = fopen(autorun_path, "w");
        if (src && dst) {
            char buffer[1024];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                fwrite(buffer, 1, bytes, dst);
            }
            fclose(src);
            fclose(dst);
            printf("CMD: Also copied MGL to autorun location: %s\n", autorun_path);
        }
        
        // Load the MGL file using load_core command
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "load_core %s", mgl_path);
        printf("CMD: Sending load_core command for MGL: %s\n", cmd);
        
        bool send_result = cmd_bridge_send_to_mister(cmd);
        printf("CMD: cmd_bridge_send_to_mister returned: %s\n", send_result ? "true" : "false");
        
        // Try direct MGL loading using MiSTer's internal xml_load function
        printf("CMD: Trying direct MGL loading using xml_load function\n");
        
#ifndef TEST_BUILD
        int xml_result = xml_load(mgl_path);
        printf("CMD: xml_load returned: %d\n", xml_result);
        
        if (xml_result != 0) {
            // If that failed, try the autorun path
            printf("CMD: Trying xml_load with autorun path\n");
            xml_result = xml_load(autorun_path);
            printf("CMD: xml_load (autorun) returned: %d\n", xml_result);
        }
#else
        printf("CMD: xml_load not available in test build\n");
#endif
        
        if (send_result) {
            result.success = true;
            snprintf(result.message, sizeof(result.message), "Loading %s game: %s", system, args);
            result.result_code = 0;
            printf("CMD: MiSTer_cmd command sent successfully\n");
            
            // Give MiSTer time to process the command and load the core
            printf("CMD: Waiting 5 seconds for MiSTer to process MGL and load core...\n");
            usleep(5000000); // 5 seconds
            
            // Don't remove the MGL file immediately - let it stay for debugging
            printf("CMD: Keeping MGL file for debugging: %s\n", mgl_path);
        } else {
            strcpy(result.message, "Failed to load MGL file");
            printf("CMD: ERROR - Failed to send command to MiSTer_cmd\n");
        }
    } else {
        // For non-CD systems, try direct load (may not work)
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "load_rom %s", args);
        
        if (cmd_bridge_send_to_mister(cmd)) {
            result.success = true;
            snprintf(result.message, sizeof(result.message), "Loading game: %s", args);
            result.result_code = 0;
        } else {
            strcpy(result.message, "Failed to send load_rom command");
        }
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

// CD-ROM auto-load command
static cmd_result_t cmd_cdrom_autoload(const char* args)
{
    cmd_result_t result = { false, "CD-ROM auto-load failed", -1 };
    
    printf("CMD: Starting CD-ROM auto-load process...\n");
    
    // Check if device is accessible
    if (access("/dev/sr0", F_OK) != 0) {
        strcpy(result.message, "CD-ROM device not found");
        return result;
    }
    
    // Initialize CD-ROM subsystem if not already done
    cdrom_init();
    
    // Try to identify the game
    CDRomGameInfo game_info;
    const char* detected_system = cdrom_get_system_from_detection();
    
    if (!detected_system) {
        strcpy(result.message, "Could not detect disc system type");
        return result;
    }
    
    printf("CMD: Detected system: %s\n", detected_system);
    
    if (!cdrom_identify_game("/dev/sr0", detected_system, &game_info)) {
        strcpy(result.message, "Could not identify game on disc");
        return result;
    }
    
    printf("CMD: Game identified: %s\n", game_info.title);
    
    // Check if we've already processed this game
    char flag_file[512];
    snprintf(flag_file, sizeof(flag_file), "/tmp/cdrom_processed_%s_%s", detected_system, game_info.title);
    
    if (access(flag_file, F_OK) == 0) {
        strcpy(result.message, "Game already processed, skipping auto-load");
        return result;
    }
    
    // Create flag file to prevent reprocessing
    FILE* flag = fopen(flag_file, "w");
    if (flag) {
        fprintf(flag, "%s\n", game_info.title);
        fclose(flag);
    }
    
    // Search for the game
    char search_cmd[512];
    snprintf(search_cmd, sizeof(search_cmd), "search_games \"%s\" %s", game_info.title, detected_system);
    
    cmd_result_t search_result = cmd_bridge_process(search_cmd);
    if (!search_result.success || search_result.result_code <= 0) {
        snprintf(result.message, sizeof(result.message), "Game '%s' not found in library", game_info.title);
        return result;
    }
    
    // Load the first search result
    cmd_result_t load_result = cmd_bridge_process("search_load 1");
    if (load_result.success) {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Game loaded: %s", game_info.title);
        result.result_code = 0;
    } else {
        strcpy(result.message, "Failed to load game");
    }
    
    return result;
}

// Search results storage for selection
#define MAX_SEARCH_RESULTS 50
static char search_results[MAX_SEARCH_RESULTS][512];
static int search_results_count = 0;
static char last_search_type[64] = "";

// Forward declaration for helper function
static void store_search_results(const char* search_type);

// Search command implementations
#ifndef TEST_BUILD
// Forward declarations are in file_io.h - no need to redeclare
#else
// Mock implementations for testing
int ScanDirectory(char* path, int mode, const char *extension, int options, 
                  const char *prefix, const char *filter) {
    printf("MOCK: ScanDirectory called with path=%s, filter=%s\n", path, filter ? filter : "none");
    return 0;  // Success
}

int flist_nDirEntries() {
    return 3;  // Mock 3 entries
}

direntext_t* flist_DirItem(int n) {
    static direntext_t mock_items[3];
    static bool initialized = false;
    
    if (!initialized) {
        strcpy(mock_items[0].de.d_name, "mock_file1.bin");
        strcpy(mock_items[1].de.d_name, "mock_file2.rbf");
        strcpy(mock_items[2].de.d_name, "mock_file3.rom");
        initialized = true;
    }
    
    if (n >= 0 && n < 3) {
        return &mock_items[n];
    }
    return NULL;
}

void flist_ScanDir(const char *path, const char *extension, int options, 
                   const char *prefix, const char *filter) {
    printf("MOCK: flist_ScanDir called\n");
}
#endif

cmd_result_t cmd_search_files(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: search_files <pattern> [path]");
        return result;
    }
    
    // Parse arguments - pattern is required, path is optional
    char pattern[256];
    char search_path[512];
    
    // Initialize search_path to empty
    search_path[0] = '\0';
    
    if (sscanf(args, "%s %s", pattern, search_path) < 1)
    {
        strcpy(result.message, "Invalid search pattern");
        return result;
    }
    
    // Default to games directory if no path specified
    if (strlen(search_path) == 0)
    {
        strcpy(search_path, "/media/fat/games");
    }
    
    printf("CMD: Searching for files matching '%s' in '%s'\n", pattern, search_path);
    
    // Use existing file browser infrastructure
    if (ScanDirectory(search_path, 0, "", 0, NULL, pattern) >= 0)
    {
        int count = flist_nDirEntries();
        
        if (count > 0)
        {
            result.success = true;
            snprintf(result.message, sizeof(result.message), 
                     "Found %d files matching '%s'. Use 'search_select <number>' to select or 'search_load <number>' to load.", 
                     count, pattern);
            result.result_code = count;
            
            // Store results for selection
            store_search_results("files");
            
            // List all results with numbers
            printf("CMD: Search results:\n");
            for (int i = 0; i < count && i < MAX_SEARCH_RESULTS; i++)
            {
                direntext_t* dir_item = flist_DirItem(i);
                if (!dir_item) continue;
                char* item = dir_item->de.d_name;
                if (item)
                {
                    printf("CMD: %d: %s\n", i + 1, item);
                }
            }
            if (count > MAX_SEARCH_RESULTS)
            {
                printf("CMD: ... and %d more (showing first %d)\n", count - MAX_SEARCH_RESULTS, MAX_SEARCH_RESULTS);
            }
        }
        else
        {
            snprintf(result.message, sizeof(result.message), 
                     "No files found matching '%s'", pattern);
        }
    }
    else
    {
        snprintf(result.message, sizeof(result.message), 
                 "Failed to search directory '%s'", search_path);
    }
    
    return result;
}

cmd_result_t cmd_search_games(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: search_games <game_name> [core_name]");
        return result;
    }
    
    // Parse arguments with proper quote handling
    char game_name[256] = "";
    char core_name[256] = "";
    
    const char* p = args;
    
    // Skip leading whitespace
    while (*p && (*p == ' ' || *p == '\t')) p++;
    
    // Parse game name (may be quoted)
    if (*p == '"') {
        // Quoted string
        p++; // Skip opening quote
        int i = 0;
        while (*p && *p != '"' && i < 255) {
            game_name[i++] = *p++;
        }
        game_name[i] = '\0';
        if (*p == '"') p++; // Skip closing quote
    } else {
        // Unquoted string - take until space
        int i = 0;
        while (*p && *p != ' ' && *p != '\t' && i < 255) {
            game_name[i++] = *p++;
        }
        game_name[i] = '\0';
    }
    
    // Skip whitespace before core name
    while (*p && (*p == ' ' || *p == '\t')) p++;
    
    // Parse core name (rest of string)
    int i = 0;
    while (*p && i < 255) {
        core_name[i++] = *p++;
    }
    core_name[i] = '\0';
    
    printf("CMD: Searching for games matching '%s'\n", game_name);
    
    // Search in games directory (use relative path since ScanDirectory prepends /media/fat/)
    const char* games_base = "games";
    
    if (strlen(core_name) > 0)
    {
        // Search in specific core directory
        char core_path[512];
        snprintf(core_path, sizeof(core_path), "%s/%s", games_base, core_name);
        
        // Manual recursive search since SCANO_DIR doesn't do deep recursion
        printf("CMD: Performing manual recursive search in '%s' for '%s'\n", core_path, game_name);
        
        int match_count = 0;
        
        // Convert search term to lowercase once
        char search_lower[256];
        strncpy(search_lower, game_name, sizeof(search_lower) - 1);
        search_lower[sizeof(search_lower) - 1] = '\0';
        for (int j = 0; search_lower[j]; j++) {
            if (search_lower[j] >= 'A' && search_lower[j] <= 'Z') {
                search_lower[j] = search_lower[j] + 32;
            }
        }
        
        // Use simpler approach with find command to avoid ScanDirectory recursion issues
        printf("CMD: Using find command approach for deep search\n");
        char find_cmd[1024];
        snprintf(find_cmd, sizeof(find_cmd), "find /media/fat/%s -type f \\( -iname '*%s*.cue' -o -iname '*%s*.chd' \\) 2>/dev/null | head -20", core_path, game_name, game_name);
        
        FILE* fp = popen(find_cmd, "r");
        if (fp) {
            char line[512];
            // Reset search results
            search_results_count = 0;
            strcpy(last_search_type, "games");
            
            while (fgets(line, sizeof(line), fp) && search_results_count < MAX_SEARCH_RESULTS) {
                // Remove newline
                line[strcspn(line, "\n")] = 0;
                if (strlen(line) == 0) continue;
                
                printf("CMD: Found file: %s\n", line);
                
                // Store this result
                strncpy(search_results[search_results_count], line, sizeof(search_results[0]) - 1);
                search_results[search_results_count][sizeof(search_results[0]) - 1] = '\0';
                search_results_count++;
                match_count++;
            }
            pclose(fp);
        } else {
            printf("CMD: Find command failed, falling back to basic search\n");
            
            // Fallback to simple single-level search
            if (ScanDirectory(core_path, 0, "", 0, NULL, game_name) >= 0) {
                int count = flist_nDirEntries();
                printf("CMD: Basic search found %d entries\n", count);
                
                for (int i = 0; i < count; i++) {
                    direntext_t* item = flist_DirItem(i);
                    if (!item) continue;
                    char* name = item->de.d_name;
                    if (!name) continue;
                    
                    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
                    
                    char name_lower[256];
                    strncpy(name_lower, name, sizeof(name_lower) - 1);
                    name_lower[sizeof(name_lower) - 1] = '\0';
                    for (int j = 0; name_lower[j]; j++) {
                        if (name_lower[j] >= 'A' && name_lower[j] <= 'Z') {
                            name_lower[j] = name_lower[j] + 32;
                        }
                    }
                    
                    if (strstr(name_lower, search_lower) != NULL) {
                        printf("CMD: Match: %s\n", name);
                        match_count++;
                    }
                }
            }
        }
        
        if (match_count > 0)
        {
            result.success = true;
            snprintf(result.message, sizeof(result.message), 
                     "Found %d games matching '%s' in %s. Use 'search_select <number>' to select or 'search_load <number>' to load.", 
                     match_count, game_name, core_name);
            result.result_code = match_count;
            
            // Results already stored during find command execution
        }
        else
        {
            snprintf(result.message, sizeof(result.message), 
                     "No games found matching '%s' in %s", game_name, core_name);
        }
    }
    else
    {
        // Search across all cores in games directory (with subdirectories)
        if (ScanDirectory((char*)games_base, 0, "", SCANO_DIR, NULL, game_name) >= 0)
        {
            int count = flist_nDirEntries();
            
            if (count > 0)
            {
                result.success = true;
                snprintf(result.message, sizeof(result.message), 
                         "Found %d games matching '%s'. Use 'search_select <number>' to select or 'search_load <number>' to load.", count, game_name);
                result.result_code = count;
                
                // Store results for selection
                store_search_results("games");
                
                printf("CMD: Games found:\n");
                for (int i = 0; i < count && i < MAX_SEARCH_RESULTS; i++)
                {
                    direntext_t* dir_item = flist_DirItem(i);
                if (!dir_item) continue;
                char* item = dir_item->de.d_name;
                    if (item)
                    {
                        printf("CMD: %d: %s\n", i + 1, item);
                    }
                }
                if (count > MAX_SEARCH_RESULTS)
                {
                    printf("CMD: ... and %d more (showing first %d)\n", count - MAX_SEARCH_RESULTS, MAX_SEARCH_RESULTS);
                }
            }
            else
            {
                snprintf(result.message, sizeof(result.message), 
                         "No games found matching '%s'", game_name);
            }
        }
        else
        {
            snprintf(result.message, sizeof(result.message), 
                     "Failed to search games directory");
        }
    }
    
    return result;
}

cmd_result_t cmd_search_cores(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    char pattern[256] = "";
    
    // Optional pattern argument
    if (args && args[0])
    {
        sscanf(args, "%s", pattern);
    }
    
    printf("CMD: Searching for cores%s%s\n", 
           strlen(pattern) > 0 ? " matching '" : "", 
           strlen(pattern) > 0 ? pattern : "");
    
    // Search for RBF files in the root directory
    const char* cores_path = "/media/fat";
    
    if (ScanDirectory((char*)cores_path, 0, "rbf", 0, NULL, 
                      strlen(pattern) > 0 ? pattern : NULL) >= 0)
    {
        int count = flist_nDirEntries();
        
        if (count > 0)
        {
            result.success = true;
            if (strlen(pattern) > 0)
            {
                snprintf(result.message, sizeof(result.message), 
                         "Found %d cores matching '%s'. Use 'search_select <number>' to select or 'search_load <number>' to load.", count, pattern);
            }
            else
            {
                snprintf(result.message, sizeof(result.message), 
                         "Found %d cores. Use 'search_select <number>' to select or 'search_load <number>' to load.", count);
            }
            result.result_code = count;
            
            // Store results for selection
            store_search_results("cores");
            
            printf("CMD: Cores found:\n");
            for (int i = 0; i < count && i < MAX_SEARCH_RESULTS; i++)
            {
                direntext_t* dir_item = flist_DirItem(i);
                if (!dir_item) continue;
                char* item = dir_item->de.d_name;
                if (item)
                {
                    printf("CMD: %d: %s\n", i + 1, item);
                }
            }
            if (count > MAX_SEARCH_RESULTS)
            {
                printf("CMD: ... and %d more (showing first %d)\n", count - MAX_SEARCH_RESULTS, MAX_SEARCH_RESULTS);
            }
        }
        else
        {
            if (strlen(pattern) > 0)
            {
                snprintf(result.message, sizeof(result.message), 
                         "No cores found matching '%s'", pattern);
            }
            else
            {
                strcpy(result.message, "No cores found");
            }
        }
    }
    else
    {
        strcpy(result.message, "Failed to search cores directory");
    }
    
    return result;
}

cmd_result_t cmd_search_select(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: search_select <number>");
        return result;
    }
    
    // Check if we have search results
    if (search_results_count == 0)
    {
        strcpy(result.message, "No search results available. Run a search command first.");
        return result;
    }
    
    // Parse selection number
    int selection = atoi(args);
    
    if (selection < 1 || selection > search_results_count)
    {
        snprintf(result.message, sizeof(result.message), 
                 "Invalid selection. Choose 1-%d", search_results_count);
        return result;
    }
    
    // Get selected item (convert to 0-based index)
    char* selected_item = search_results[selection - 1];
    
    result.success = true;
    snprintf(result.message, sizeof(result.message), 
             "Selected: %s", selected_item);
    result.result_code = selection;
    
    printf("CMD: Selected item %d: %s\n", selection, selected_item);
    
    return result;
}

cmd_result_t cmd_search_load(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    if (!args || !args[0])
    {
        strcpy(result.message, "Usage: search_load <number>");
        return result;
    }
    
    // Check if we have search results
    if (search_results_count == 0)
    {
        strcpy(result.message, "No search results available. Run a search command first.");
        return result;
    }
    
    // Parse selection number
    int selection = atoi(args);
    
    if (selection < 1 || selection > search_results_count)
    {
        snprintf(result.message, sizeof(result.message), 
                 "Invalid selection. Choose 1-%d", search_results_count);
        return result;
    }
    
    // Get selected item (convert to 0-based index)
    char* selected_item = search_results[selection - 1];
    
    printf("CMD: Loading selected item %d: %s\n", selection, selected_item);
    
    // Determine what to do based on search type and file extension
    if (strstr(selected_item, ".rbf"))
    {
        // Load core
        return cmd_load_core(selected_item);
    }
    else if (strstr(last_search_type, "games") || strstr(last_search_type, "files"))
    {
        // Load game/ROM
        return cmd_load_game(selected_item);
    }
    else
    {
        // Default to loading as game
        return cmd_load_game(selected_item);
    }
}

// Enhanced search functions that store results for selection
static void store_search_results(const char* search_type)
{
    // Store search type for later use
    strncpy(last_search_type, search_type, sizeof(last_search_type) - 1);
    last_search_type[sizeof(last_search_type) - 1] = '\0';
    
    // Store results for selection
    search_results_count = flist_nDirEntries();
    if (search_results_count > MAX_SEARCH_RESULTS)
    {
        search_results_count = MAX_SEARCH_RESULTS;
    }
    
    for (int i = 0; i < search_results_count; i++)
    {
        direntext_t* dir_item = flist_DirItem(i);
        if (!dir_item) continue;
        char* item = dir_item->de.d_name;
        if (item)
        {
            strncpy(search_results[i], item, sizeof(search_results[i]) - 1);
            search_results[i][sizeof(search_results[i]) - 1] = '\0';
        }
    }
}

// Popup file browser command implementation
cmd_result_t cmd_popup_browse(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
    // Parse arguments: [path] [extensions] [flags]
    char path[512] = "/media/fat/games";
    char extensions[256] = "";
    char flags[64] = "";
    
    if (args && args[0])
    {
        // Parse path and optional extensions/flags
        if (sscanf(args, "%s %s %s", path, extensions, flags) < 1)
        {
            strcpy(result.message, "Usage: popup_browse [path] [extensions] [flags]");
            return result;
        }
    }
    
    printf("CMD: Opening popup file browser at '%s'\n", path);
    
#ifndef TEST_BUILD
    // TODO: Implement SelectFilePopup integration when available
    result.success = false;
    strcpy(result.message, "popup_browse not yet implemented - SelectFilePopup not available");
    return result;
    
    /*
    // Call the popup file browser directly
    extern void SelectFilePopup(const char* path, const char* pFileExt, int Options);
    
    // Convert extensions string to options
    int options = 0;
    if (strstr(flags, "cores")) options |= SCANO_CORES;
    if (strstr(flags, "dirs")) options |= SCANO_DIR;
    if (strstr(flags, "umount")) options |= SCANO_UMOUNT;
    
    // Use default extensions if none specified
    const char* ext = (strlen(extensions) > 0) ? extensions : "";
    
    // Trigger the popup file browser
    SelectFilePopup(path, ext, options);
    
    result.success = true;
    snprintf(result.message, sizeof(result.message), 
             "Popup file browser opened at: %s", path);
    */
    result.result_code = 0;
#else
    // Mock implementation for testing
    printf("MOCK: Popup file browser would open at path=%s, extensions=%s, flags=%s\n", 
           path, extensions, flags);
    
    result.success = true;
    snprintf(result.message, sizeof(result.message), 
             "Mock popup browser opened at: %s", path);
    result.result_code = 0;
#endif
    
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
    cmd_bridge_register("search_files", cmd_search_files, "Search for files by name pattern");
    cmd_bridge_register("search_games", cmd_search_games, "Search for games in games directory");
    cmd_bridge_register("search_cores", cmd_search_cores, "Search for available cores");
    cmd_bridge_register("search_select", cmd_search_select, "Select item from search results");
    cmd_bridge_register("search_load", cmd_search_load, "Load selected item from search results");
    cmd_bridge_register("popup_browse", cmd_popup_browse, "Open popup file browser");
    cmd_bridge_register("cdrom_autoload", cmd_cdrom_autoload, "Auto-detect and load CD-ROM game");
}