#include "cmd_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <time.h>
#include "cfg.h"
#include "fuzzy_match.h"

#ifndef TEST_BUILD
#include "menu.h"
#include "file_io.h"
#include "support/arcade/mra_loader.h"
#include "cdrom.h"

// External menu variables for refreshing directory view
// Note: These may not be available in all build configurations
#endif

// Global variable to track current MGL file for cleanup
static char current_mgl_path[512] = "";

// Command registry
#define MAX_COMMANDS 50
static cmd_definition_t registered_commands[MAX_COMMANDS];
static int num_registered_commands = 0;
static bool cmd_bridge_initialized = false;
static const char* MISTER_CMD_DEVICE = "/dev/MiSTer_cmd";

// Forward declarations for built-in handlers
static void register_builtin_commands();
static void refresh_menu_directory();

void cmd_bridge_init()
{
    if (cmd_bridge_initialized) return;
    
    printf("CMD: Initializing command bridge system\n");
    
    // Clean up any leftover MGL files from previous session
    printf("CMD: Cleaning up previous CD-ROM MGL files\n");
    system("rm -f /media/fat/*.mgl 2>/dev/null");
    system("rm -f /media/fat/[0-9]*.mgl 2>/dev/null"); // Clean numbered selection files
    cmd_bridge_clear_current_mgl_path();
    
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
        
        // Extract game name from file path for better MGL naming
        const char* filename_start = strrchr(args, '/');
        if (filename_start) {
            filename_start++; // Skip the '/'
        } else {
            filename_start = args;
        }
        
        // Extract name without extension
        char game_name[256];
        strncpy(game_name, filename_start, sizeof(game_name) - 1);
        game_name[sizeof(game_name) - 1] = '\0';
        
        // Remove file extension
        char* ext = strrchr(game_name, '.');
        if (ext) {
            *ext = '\0';
        }
        
        // Sanitize for filename (keep spaces and parentheses)
        for (int i = 0; game_name[i]; i++) {
            if (game_name[i] == '[' || game_name[i] == ']' || game_name[i] == ',' ||
                game_name[i] == '\'' || game_name[i] == '"' || game_name[i] == ':') {
                game_name[i] = '_';
            }
            // Keep spaces and parentheses for better readability
        }
        
        snprintf(mgl_path, sizeof(mgl_path), "/media/fat/%s.mgl", game_name);
        printf("CMD: MGL path: %s\n", mgl_path);
        
        // Store MGL path for cleanup when disc is ejected
        strncpy(current_mgl_path, mgl_path, sizeof(current_mgl_path) - 1);
        current_mgl_path[sizeof(current_mgl_path) - 1] = '\0';
        
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
        
        // Check if we should actually load the game or just create MGL
        if (cfg.cdrom_autoload) {
            // Load the MGL file using direct xml_load function
            printf("CMD: Loading MGL file using xml_load function\n");
            
#ifndef TEST_BUILD
            int xml_result = xml_load(mgl_path);
            printf("CMD: xml_load returned: %d\n", xml_result);
            
            if (xml_result == 0) {
#else
        printf("CMD: xml_load not available in test build\n");
        bool xml_result = false;
        if (false) {
#endif
            result.success = true;
            snprintf(result.message, sizeof(result.message), "Loading %s game: %s", system, args);
            result.result_code = 0;
            printf("CMD: MGL loaded successfully\n");
            
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
            // Autoload disabled - MGL created but not loaded
            printf("CMD: CD-ROM autoload disabled - MGL created but not loaded\n");
            result.success = true;
            snprintf(result.message, sizeof(result.message), "MGL created for %s but autoload disabled", system);
            result.result_code = 0;
        }
        
        // Refresh menu directory to show the newly created MGL file
        refresh_menu_directory();
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

// CD audio player commands
static cmd_result_t cmd_cdaudio_play(const char* args)
{
    cmd_result_t result = { false, "CD audio playback failed", -1 };
    
    // Check if CD-ROM device is available
    if (access("/dev/sr0", F_OK) != 0) {
        strcpy(result.message, "CD-ROM device not found");
        return result;
    }
    
    // Check if this is an audio CD
    int fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        strcpy(result.message, "Cannot open CD-ROM device");
        return result;
    }
    
    // Check disc status
    int status = ioctl(fd, CDROM_DISC_STATUS);
    if (status != CDS_DISC_OK && status != CDS_AUDIO && status != CDS_MIXED) {
        close(fd);
        strcpy(result.message, "No audio CD detected");
        return result;
    }
    
    // Get track information
    struct cdrom_tochdr tochdr;
    if (ioctl(fd, CDROMREADTOCHDR, &tochdr) != 0) {
        close(fd);
        strcpy(result.message, "Cannot read CD table of contents");
        return result;
    }
    
    close(fd);
    
    // Parse track argument (default to track 1)
    int track = 1;
    if (args && args[0]) {
        track = atoi(args);
        if (track < 1 || track > tochdr.cdth_trk1) {
            snprintf(result.message, sizeof(result.message), 
                     "Invalid track number %d (available: 1-%d)", track, tochdr.cdth_trk1);
            return result;
        }
    }
    
    // Send ATAPI PLAY AUDIO command via MiSTer_cmd
    char play_cmd[128];
    snprintf(play_cmd, sizeof(play_cmd), "cdaudio_play %d", track);
    
    if (cmd_bridge_send_to_mister(play_cmd)) {
        result.success = true;
        snprintf(result.message, sizeof(result.message), 
                 "Playing CD audio track %d of %d", track, tochdr.cdth_trk1);
        result.result_code = track;
    } else {
        strcpy(result.message, "Failed to send audio playback command");
    }
    
    return result;
}

static cmd_result_t cmd_cdaudio_stop(const char* args)
{
    cmd_result_t result = { false, "CD audio stop failed", -1 };
    
    // Send ATAPI STOP command via MiSTer_cmd
    if (cmd_bridge_send_to_mister("cdaudio_stop")) {
        result.success = true;
        strcpy(result.message, "CD audio playback stopped");
        result.result_code = 0;
    } else {
        strcpy(result.message, "Failed to send audio stop command");
    }
    
    return result;
}

static cmd_result_t cmd_cdaudio_pause(const char* args)
{
    cmd_result_t result = { false, "CD audio pause failed", -1 };
    
    // Send ATAPI PAUSE/RESUME command via MiSTer_cmd
    if (cmd_bridge_send_to_mister("cdaudio_pause")) {
        result.success = true;
        strcpy(result.message, "CD audio playback paused/resumed");
        result.result_code = 0;
    } else {
        strcpy(result.message, "Failed to send audio pause command");
    }
    
    return result;
}

static cmd_result_t cmd_cdaudio_info(const char* args)
{
    cmd_result_t result = { false, "CD audio info failed", -1 };
    
    // Check if CD-ROM device is available
    if (access("/dev/sr0", F_OK) != 0) {
        strcpy(result.message, "CD-ROM device not found");
        return result;
    }
    
    int fd = open("/dev/sr0", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        strcpy(result.message, "Cannot open CD-ROM device");
        return result;
    }
    
    // Check disc status
    int status = ioctl(fd, CDROM_DISC_STATUS);
    if (status != CDS_DISC_OK && status != CDS_AUDIO && status != CDS_MIXED) {
        close(fd);
        strcpy(result.message, "No audio CD detected");
        return result;
    }
    
    // Get track information
    struct cdrom_tochdr tochdr;
    if (ioctl(fd, CDROMREADTOCHDR, &tochdr) != 0) {
        close(fd);
        strcpy(result.message, "Cannot read CD table of contents");
        return result;
    }
    
    printf("CMD: CD Audio Information:\n");
    printf("CMD: First track: %d\n", tochdr.cdth_trk0);
    printf("CMD: Last track: %d\n", tochdr.cdth_trk1);
    printf("CMD: Total tracks: %d\n", tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1);
    
    // Get track details
    for (int track = tochdr.cdth_trk0; track <= tochdr.cdth_trk1; track++) {
        struct cdrom_tocentry tocentry;
        tocentry.cdte_track = track;
        tocentry.cdte_format = CDROM_MSF;
        
        if (ioctl(fd, CDROMREADTOCENTRY, &tocentry) == 0) {
            printf("CMD: Track %d: %02d:%02d:%02d (%s)\n", 
                   track,
                   tocentry.cdte_addr.msf.minute,
                   tocentry.cdte_addr.msf.second,
                   tocentry.cdte_addr.msf.frame,
                   (tocentry.cdte_ctrl & CDROM_DATA_TRACK) ? "Data" : "Audio");
        }
    }
    
    close(fd);
    
    result.success = true;
    snprintf(result.message, sizeof(result.message), 
             "CD has %d tracks (%d-%d)", 
             tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1,
             tochdr.cdth_trk0, tochdr.cdth_trk1);
    result.result_code = tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1;
    
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
    
    // Create MGL filename based on game title (sanitized for filesystem)
    char mgl_filename[256];
    char sanitized_title[256];
    
    // Sanitize game title for filename (replace spaces and special chars with underscores)
    strncpy(sanitized_title, game_info.title, sizeof(sanitized_title) - 1);
    sanitized_title[sizeof(sanitized_title) - 1] = '\0';
    
    for (int i = 0; sanitized_title[i]; i++) {
        if (sanitized_title[i] == '[' || sanitized_title[i] == ']' || sanitized_title[i] == ',' ||
            sanitized_title[i] == '\'' || sanitized_title[i] == '"' || sanitized_title[i] == ':' ||
            sanitized_title[i] == '/' || sanitized_title[i] == '\\') {
            sanitized_title[i] = '_';
        }
        // Keep spaces and parentheses for better readability
    }
    
    snprintf(mgl_filename, sizeof(mgl_filename), "/media/fat/%s.mgl", sanitized_title);
    
    printf("CMD: Checking for existing MGL: %s\n", mgl_filename);
    
    if (access(mgl_filename, F_OK) == 0) {
        strcpy(result.message, "Game MGL already exists, skipping auto-load");
        return result;
    }
    
    // Search for the game to get the file path for MGL creation
    char search_cmd[512];
    snprintf(search_cmd, sizeof(search_cmd), "search_games \"%s\" %s", game_info.title, detected_system);
    
    cmd_result_t search_result = cmd_bridge_process(search_cmd);
    if (!search_result.success || search_result.result_code <= 0) {
        snprintf(result.message, sizeof(result.message), "Game '%s' not found in library", game_info.title);
        return result;
    }
    
    // Check if multiple results found and auto_select setting
    if (search_result.result_code > 1 && cfg.cdrom_auto_select == 0) {
        printf("CMD: Multiple games found (%d matches), showing selection popup (auto_select disabled)\n", search_result.result_code);
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Multiple matches found for '%s' - check OSD for selection", game_info.title);
        result.result_code = search_result.result_code;
        return result;
    }
    
    // Always create MGL file by calling search_load (which creates the MGL)
    printf("CMD: Creating MGL file for detected game (using best match)\n");
    cmd_result_t load_result = cmd_bridge_process("search_load 1");
    
    // If autoload is disabled, create MGL but don't actually load the game
    if (!cfg.cdrom_autoload) {
        printf("CMD: CD-ROM autoload disabled in configuration, MGL created but not loaded\n");
        result.success = true;
        snprintf(result.message, sizeof(result.message), "MGL created for '%s' but autoload disabled", game_info.title);
        result.result_code = 0;
        return result;
    }
    
    printf("CMD: CD-ROM autoload enabled, MGL created and game loaded\n");
    if (load_result.success) {
        result.success = true;
        snprintf(result.message, sizeof(result.message), "Game loaded: %s", game_info.title);
        result.result_code = 0;
    } else {
        strcpy(result.message, "Failed to load game");
    }
    
    return result;
}

// Enhanced search results storage with fuzzy match scores
#define MAX_SEARCH_RESULTS 50
typedef struct {
    char path[512];
    char title[256];
    int fuzzy_score;
    int region_score;
    int total_score;
} search_result_entry_t;

static search_result_entry_t search_results_enhanced[MAX_SEARCH_RESULTS];
static char search_results[MAX_SEARCH_RESULTS][512]; // Keep for backward compatibility
static int search_results_count = 0;
static char last_search_type[64] = "";

// Forward declaration for helper functions
static void store_search_results(const char* search_type);
static void extract_game_title_from_path(const char* path, char* title, size_t title_size);
static void sort_search_results_by_score();
static void add_enhanced_search_result(const char* path, const char* search_term, const char* preferred_region);
static void show_game_selection_popup();

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
        // Map system names to directory names
        const char* dir_name = core_name;
        if (strcmp(core_name, "SegaCD") == 0) {
            dir_name = "MegaCD";  // Database uses SegaCD, but directory is MegaCD
        }
        
        // Search in specific core directory
        char core_path[512];
        snprintf(core_path, sizeof(core_path), "%s/%s", games_base, dir_name);
        
        printf("CMD: Mapped system '%s' to directory '%s'\n", core_name, dir_name);
        
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
        
        // Use broader search to find more potential matches for fuzzy matching
        printf("CMD: Using enhanced fuzzy search approach\n");
        
        // Extract base name from search term for broader initial search
        char base_search[256];
        extract_base_name(game_name, base_search, sizeof(base_search));
        
        // Also try searching with just the first word for very broad matching
        char first_word[64] = "";
        const char* space = strchr(base_search, ' ');
        if (space) {
            int word_len = space - base_search;
            if (word_len < (int)sizeof(first_word) - 1) {
                strncpy(first_word, base_search, word_len);
                first_word[word_len] = '\0';
            }
        } else {
            strncpy(first_word, base_search, sizeof(first_word) - 1);
            first_word[sizeof(first_word) - 1] = '\0';
        }
        
        char find_cmd[1024];
        snprintf(find_cmd, sizeof(find_cmd), "find /media/fat/%s -type f \\( -iname '*%s*.cue' -o -iname '*%s*.chd' -o -iname '*%s*.cue' -o -iname '*%s*.chd' \\) 2>/dev/null | head -50", 
                 core_path, game_name, game_name, first_word, first_word);
        
        FILE* fp = popen(find_cmd, "r");
        if (fp) {
            char line[512];
            // Reset search results
            search_results_count = 0;
            strcpy(last_search_type, "games");
            
            printf("CMD: Fuzzy matching results for '%s' (preferred region: %s):\n", game_name, cfg.cdrom_preferred_region);
            
            while (fgets(line, sizeof(line), fp) && search_results_count < MAX_SEARCH_RESULTS) {
                // Remove newline
                line[strcspn(line, "\n")] = 0;
                if (strlen(line) == 0) continue;
                
                // Add to enhanced search results with fuzzy matching
                add_enhanced_search_result(line, game_name, cfg.cdrom_preferred_region);
                
                // Only count as a match if fuzzy score is reasonable (>= 30)
                search_result_entry_t* entry = &search_results_enhanced[search_results_count - 1];
                if (entry->fuzzy_score >= 30) {
                    match_count++;
                } else {
                    // Remove this result if fuzzy score is too low
                    search_results_count--;
                }
            }
            pclose(fp);
            
            // Sort results by score
            sort_search_results_by_score();
            
            // Display ranked results with scores
            printf("CMD: Search results ranked by relevance:\n");
            for (int i = 0; i < search_results_count; i++) {
                search_result_entry_t* entry = &search_results_enhanced[i];
                printf("CMD: %d. [Score:%d F:%d R:%d] %s -> %s\n", 
                       i + 1, entry->total_score, entry->fuzzy_score, entry->region_score,
                       entry->title, entry->path);
            }
            
            // Show OSD selection if multiple matches and auto_select is disabled
            printf("CMD: Checking selection conditions: results=%d, auto_select=%d\n", 
                   search_results_count, cfg.cdrom_auto_select);
            if (search_results_count > 1 && cfg.cdrom_auto_select == 0) {
                printf("CMD: Conditions met, showing selection popup\n");
                show_game_selection_popup();
            } else {
                printf("CMD: Conditions not met for popup\n");
            }
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
    cmd_bridge_register("cdaudio_play", cmd_cdaudio_play, "Play CD audio track (cdaudio_play [track_number])");
    cmd_bridge_register("cdaudio_stop", cmd_cdaudio_stop, "Stop CD audio playback");
    cmd_bridge_register("cdaudio_pause", cmd_cdaudio_pause, "Pause/resume CD audio playback");
    cmd_bridge_register("cdaudio_info", cmd_cdaudio_info, "Show CD audio disc information");
}

// Utility functions to manage current MGL path
const char* cmd_bridge_get_current_mgl_path()
{
    return current_mgl_path;
}

void cmd_bridge_clear_current_mgl_path()
{
    current_mgl_path[0] = '\0';
}

void cmd_bridge_set_current_mgl_path(const char* path)
{
    if (path && strlen(path) < sizeof(current_mgl_path)) {
        strcpy(current_mgl_path, path);
    } else {
        current_mgl_path[0] = '\0';
    }
}

// Enhanced search helper functions
static void extract_game_title_from_path(const char* path, char* title, size_t title_size)
{
    // Extract filename from path
    const char* filename = strrchr(path, '/');
    if (filename) {
        filename++; // Skip the '/'
    } else {
        filename = path;
    }
    
    // Copy filename and remove extension
    strncpy(title, filename, title_size - 1);
    title[title_size - 1] = '\0';
    
    // Remove file extension
    char* ext = strrchr(title, '.');
    if (ext) {
        *ext = '\0';
    }
}

static void sort_search_results_by_score()
{
    // Simple bubble sort by total score (descending)
    for (int i = 0; i < search_results_count - 1; i++) {
        for (int j = 0; j < search_results_count - i - 1; j++) {
            if (search_results_enhanced[j].total_score < search_results_enhanced[j + 1].total_score) {
                // Swap entries
                search_result_entry_t temp = search_results_enhanced[j];
                search_results_enhanced[j] = search_results_enhanced[j + 1];
                search_results_enhanced[j + 1] = temp;
            }
        }
    }
    
    // Update backward compatibility array
    for (int i = 0; i < search_results_count; i++) {
        strncpy(search_results[i], search_results_enhanced[i].path, sizeof(search_results[i]) - 1);
        search_results[i][sizeof(search_results[i]) - 1] = '\0';
    }
}

static void add_enhanced_search_result(const char* path, const char* search_term, const char* preferred_region)
{
    if (search_results_count >= MAX_SEARCH_RESULTS) return;
    
    search_result_entry_t* entry = &search_results_enhanced[search_results_count];
    
    // Store path
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->path[sizeof(entry->path) - 1] = '\0';
    
    // Extract title from path
    extract_game_title_from_path(path, entry->title, sizeof(entry->title));
    
    // Calculate fuzzy match score
    entry->fuzzy_score = fuzzy_match_score(entry->title, search_term);
    
    // Calculate region score by extracting region from title
    char base_title[256];
    extract_base_name(entry->title, base_title, sizeof(base_title));
    
    // Enhanced region extraction (look for known regions in parentheses)
    entry->region_score = 50; // Default for unknown/no region
    
    const char* known_regions[] = {"USA", "US", "Europe", "EUR", "Japan", "JPN", "JP", "World", "Asia", NULL};
    
    // Check all parentheses for known regions
    const char* search_pos = entry->title;
    while ((search_pos = strchr(search_pos, '(')) != NULL) {
        const char* region_end = strchr(search_pos, ')');
        if (region_end) {
            char region[64];
            int region_len = region_end - search_pos - 1;
            if (region_len > 0 && region_len < (int)sizeof(region) - 1) {
                strncpy(region, search_pos + 1, region_len);
                region[region_len] = '\0';
                
                // Check if this is a known region
                for (int i = 0; known_regions[i]; i++) {
                    if (strcasecmp(region, known_regions[i]) == 0) {
                        entry->region_score = region_priority_score(region, preferred_region);
                        break;
                    }
                }
            }
        }
        search_pos++;
    }
    
    // Calculate total score (weighted: 70% fuzzy match, 30% region preference)
    entry->total_score = (entry->fuzzy_score * 7 + entry->region_score * 3) / 10;
    
    search_results_count++;
}

// Show OSD selection popup for multiple game matches
static void show_game_selection_popup()
{
#ifndef TEST_BUILD
    if (search_results_count <= 1) return;
    
    // Create a visible file listing on screen for manual selection
    // This creates MGL files for each option that appear in the file browser
    printf("CMD: Creating numbered MGL files for manual selection\n");
    
    for (int i = 0; i < search_results_count && i < 9; i++) { // Limit to 9 to avoid clutter
        search_result_entry_t* entry = &search_results_enhanced[i];
        
        // Extract just the filename for cleaner display
        char clean_title[256];
        extract_game_title_from_path(entry->path, clean_title, sizeof(clean_title));
        
        // Create numbered selection MGL file
        char selection_mgl[512];
        snprintf(selection_mgl, sizeof(selection_mgl), 
                 "/media/fat/%d-%s.mgl", 
                 i + 1, clean_title);
        
        // Create MGL content
        FILE* mgl = fopen(selection_mgl, "w");
        if (mgl) {
            fprintf(mgl, "<mistergamedescription>\n");
            fprintf(mgl, "    <rbf>_Console/MegaCD</rbf>\n");
            fprintf(mgl, "    <file delay=\"1\" type=\"s\" index=\"0\" path=\"%s\"/>\n", entry->path);
            fprintf(mgl, "</mistergamedescription>\n");
            fclose(mgl);
            
            printf("CMD: Created selection MGL: %s\n", selection_mgl);
        }
    }
    
    // Show info message about the selection files
    char message[512];
    snprintf(message, sizeof(message), 
             "Multiple CD games found!\n\n"
             "Check main menu for numbered\n"
             "selection files (1-%d).\n\n"
             "Best matches are listed first.", 
             search_results_count > 9 ? 9 : search_results_count);
    
    InfoMessage(message, 8000, "CD-ROM Auto-Detection");
    
    // Trigger menu refresh to show new files
    refresh_menu_directory();
    
    printf("CMD: Selection MGLs created and menu refreshed\n");
#else
    printf("CMD: Game selection popup not available in test build\n");
#endif
}

// Function to refresh the menu directory view after creating MGL
static void refresh_menu_directory()
{
#ifndef TEST_BUILD
    // Check if menu is currently visible
    if (menu_present()) {
        printf("CMD: Triggering menu refresh to show new MGL file\n");
        
        // Give file system a moment to sync the new MGL file
        usleep(100000); // 100ms delay
        
        // Use HOME key to trigger menu refresh (this is the actual refresh key in MiSTer)
        // KEY_HOME is 102 in linux/input.h
        menu_key_set(102); // HOME key
        
        printf("CMD: Menu refresh triggered with HOME key\n");
    } else {
        printf("CMD: Menu not visible, skipping refresh\n");
    }
#else
    printf("CMD: Menu refresh not available in test build\n");
#endif
}