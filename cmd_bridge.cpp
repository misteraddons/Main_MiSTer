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
#include "nfc_reader.h"
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

// Search results storage for selection
#define MAX_SEARCH_RESULTS 50
static char search_results[MAX_SEARCH_RESULTS][512];
static int search_results_count = 0;
static char last_search_type[64] = "";

// Forward declaration for helper function
static void store_search_results(const char* search_type);

// Search command implementations
#ifndef TEST_BUILD
extern "C" {
    // Forward declarations for file_io functions
    int ScanDirectory(char* path, int mode, const char *extension, int options, 
                      const char *prefix, const char *filter);
    int flist_nDirEntries();
    char* flist_DirItem(int n);
    void flist_ScanDir(const char *path, const char *extension, int options, 
                       const char *prefix, const char *filter);
}
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

char* flist_DirItem(int n) {
    static char mock_items[3][64] = {
        "mock_file1.bin",
        "mock_file2.rbf", 
        "mock_file3.rom"
    };
    if (n >= 0 && n < 3) return mock_items[n];
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
                char* item = flist_DirItem(i);
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
    
    // Parse arguments
    char game_name[256];
    char core_name[256] = "";
    
    sscanf(args, "%s %s", game_name, core_name);
    
    printf("CMD: Searching for games matching '%s'\n", game_name);
    
    // Search in _Games directory first
    const char* games_path = "/media/fat/_Games";
    
    if (strlen(core_name) > 0)
    {
        // Search in specific core directory
        char core_path[512];
        snprintf(core_path, sizeof(core_path), "%s/%s", games_path, core_name);
        
        if (ScanDirectory(core_path, 0, "", 0, NULL, game_name) >= 0)
        {
            int count = flist_nDirEntries();
            
            if (count > 0)
            {
                result.success = true;
                snprintf(result.message, sizeof(result.message), 
                         "Found %d games matching '%s' in %s. Use 'search_select <number>' to select or 'search_load <number>' to load.", 
                         count, game_name, core_name);
                result.result_code = count;
                
                // Store results for selection
                store_search_results("games");
                
                printf("CMD: Games found in %s:\n", core_name);
                for (int i = 0; i < count && i < MAX_SEARCH_RESULTS; i++)
                {
                    char* item = flist_DirItem(i);
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
                         "No games found matching '%s' in %s", game_name, core_name);
            }
        }
        else
        {
            snprintf(result.message, sizeof(result.message), 
                     "Failed to search core directory '%s'", core_name);
        }
    }
    else
    {
        // Search across all cores in _Games directory
        if (ScanDirectory((char*)games_path, 0, "", 0, NULL, game_name) >= 0)
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
                    char* item = flist_DirItem(i);
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
                char* item = flist_DirItem(i);
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
        char* item = flist_DirItem(i);
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

// NFC command implementations
cmd_result_t cmd_nfc_setup(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
#ifndef TEST_BUILD
    // Parse NFC configuration: [module_type] [i2c_address] [poll_interval]
    nfc_config_t config = {0};
    config.module_type = NFC_MODULE_PN532;  // Default to PN532
    config.i2c_address = 0x24;              // Default PN532 I2C address
    config.enable_polling = true;
    config.poll_interval_ms = 500;          // Poll every 500ms
    
    if (args && args[0])
    {
        // Parse arguments
        char module_str[32] = "";
        int addr = 0, interval = 0;
        
        int parsed = sscanf(args, "%s %x %d", module_str, &addr, &interval);
        
        if (parsed >= 1) {
            if (strcasecmp(module_str, "pn532") == 0) {
                config.module_type = NFC_MODULE_PN532;
            } else if (strcasecmp(module_str, "rc522") == 0) {
                config.module_type = NFC_MODULE_RC522;
            }
        }
        
        if (parsed >= 2 && addr > 0 && addr < 0x80) {
            config.i2c_address = addr;
        }
        
        if (parsed >= 3 && interval > 0) {
            config.poll_interval_ms = interval;
        }
    }
    
    printf("CMD: Setting up NFC reader - Type: %s, Address: 0x%02X, Poll: %dms\n",
           (config.module_type == NFC_MODULE_PN532) ? "PN532" : "RC522",
           config.i2c_address, config.poll_interval_ms);
    
    if (nfc_init(&config)) {
        nfc_start_background_polling();
        
        result.success = true;
        snprintf(result.message, sizeof(result.message), 
                 "NFC reader initialized and polling started");
        result.result_code = 0;
    } else {
        strcpy(result.message, "Failed to initialize NFC reader");
    }
#else
    // Mock implementation
    printf("MOCK: NFC setup with args: %s\n", args ? args : "(none)");
    result.success = true;
    strcpy(result.message, "Mock NFC reader setup successful");
    result.result_code = 0;
#endif
    
    return result;
}

cmd_result_t cmd_nfc_poll(const char* args)
{
    cmd_result_t result = { false, "", -1 };
    
#ifndef TEST_BUILD
    if (!nfc_is_available()) {
        strcpy(result.message, "NFC reader not initialized. Use 'nfc_setup' first.");
        return result;
    }
    
    nfc_tag_data_t tag_data;
    if (nfc_poll_for_tag(&tag_data)) {
        char uid_str[64];
        nfc_format_uid_string(&tag_data, uid_str, sizeof(uid_str));
        
        result.success = true;
        snprintf(result.message, sizeof(result.message), 
                 "NFC tag detected: %s", uid_str);
        result.result_code = 1;
        
        // Process the tag
        nfc_process_tag(&tag_data);
    } else {
        result.success = true;
        strcpy(result.message, "No NFC tag detected");
        result.result_code = 0;
    }
#else
    // Mock implementation
    printf("MOCK: NFC poll - %s\n", args ? args : "scanning");
    
    // Simulate finding a tag occasionally
    static int poll_count = 0;
    poll_count++;
    
    if (poll_count % 5 == 0) {
        result.success = true;
        strcpy(result.message, "Mock NFC tag detected: AA:BB:CC:DD");
        result.result_code = 1;
        
        // Simulate processing a game tag
        printf("MOCK: Processing tag data: GAME:Sonic:Genesis\n");
    } else {
        result.success = true;
        strcpy(result.message, "No NFC tag detected");
        result.result_code = 0;
    }
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
    cmd_bridge_register("search_games", cmd_search_games, "Search for games in _Games directory");
    cmd_bridge_register("search_cores", cmd_search_cores, "Search for available cores");
    cmd_bridge_register("search_select", cmd_search_select, "Select item from search results");
    cmd_bridge_register("search_load", cmd_search_load, "Load selected item from search results");
    cmd_bridge_register("popup_browse", cmd_popup_browse, "Open popup file browser");
    cmd_bridge_register("nfc_setup", cmd_nfc_setup, "Setup NFC reader");
    cmd_bridge_register("nfc_poll", cmd_nfc_poll, "Poll for NFC tags");
}