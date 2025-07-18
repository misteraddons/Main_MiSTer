#include "cdrom.h"
#include "file_io.h"
#include "cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

// Global CD-ROM state
static bool cdrom_initialized = false;
static bool cdrom_drive_detected = false;
static char cdrom_device_path[256] = "/dev/sr0";

void cdrom_init()
{
    if (cdrom_initialized) return;
    
    printf("CD-ROM: Initializing CD-ROM subsystem\n");
    
    // Create necessary directories
    system("mkdir -p /media/fat/gameID");
    system("mkdir -p /media/fat/Scripts/_GameID");
    
    // Setup GameID environment
    gameid_setup_environment();
    
    // Detect CD-ROM drive
    cdrom_drive_detected = cdrom_detect_drive();
    
    if (cdrom_drive_detected)
    {
        printf("CD-ROM: Drive detected at %s\n", cdrom_device_path);
    }
    else
    {
        printf("CD-ROM: No drive detected\n");
    }
    
    cdrom_initialized = true;
}

void cdrom_cleanup()
{
    cdrom_initialized = false;
    cdrom_drive_detected = false;
}

bool cdrom_detect_drive()
{
    // Check if /dev/sr0 exists and is accessible
    struct stat st;
    if (stat(cdrom_device_path, &st) == 0)
    {
        // Try to open the device
        int fd = open(cdrom_device_path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0)
        {
            close(fd);
            return true;
        }
    }
    
    // Check for other potential CD-ROM devices
    const char* potential_devices[] = {
        "/dev/sr1", "/dev/sr2", "/dev/cdrom", "/dev/dvd", NULL
    };
    
    for (int i = 0; potential_devices[i]; i++)
    {
        if (stat(potential_devices[i], &st) == 0)
        {
            int fd = open(potential_devices[i], O_RDONLY | O_NONBLOCK);
            if (fd >= 0)
            {
                close(fd);
                strcpy(cdrom_device_path, potential_devices[i]);
                return true;
            }
        }
    }
    
    return false;
}

bool cdrom_is_disc_inserted()
{
    if (!cdrom_drive_detected) return false;
    
    // Try to open and read from the device
    int fd = open(cdrom_device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;
    
    // Try to read a small amount of data
    char buffer[2048];
    ssize_t result = read(fd, buffer, sizeof(buffer));
    close(fd);
    
    return (result > 0);
}

bool gameid_setup_environment()
{
    // Check if GameID script exists
    if (!FileExists("/media/fat/Scripts/_GameID/GameID.py"))
    {
        printf("CD-ROM: GameID script not found, please install GameID to /media/fat/Scripts/_GameID/\n");
        return false;
    }
    
    // Check if database exists
    if (!FileExists("/media/fat/gameID/db.pkl.gz"))
    {
        printf("CD-ROM: GameID database not found, please install database to /media/fat/gameID/\n");
        return false;
    }
    
    return true;
}

bool gameid_identify_disc(const char* device_path, const char* system, CDRomGameInfo* result)
{
    if (!result) return false;
    
    // Clear result structure
    memset(result, 0, sizeof(CDRomGameInfo));
    
    // Construct GameID command
    char command[1024];
    snprintf(command, sizeof(command), 
        "cd /media/fat/Scripts/_GameID && python ./GameID.py -d /media/fat/gameID/db.pkl.gz -c %s -i %s 2>/dev/null",
        system, device_path);
    
    printf("CD-ROM: Running GameID command: %s\n", command);
    
    // Execute GameID and capture output
    FILE* fp = popen(command, "r");
    if (!fp)
    {
        printf("CD-ROM: Failed to execute GameID command\n");
        return false;
    }
    
    char line[512];
    bool found_data = false;
    
    while (fgets(line, sizeof(line), fp))
    {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Parse GameID output format: "field_name    value"
        char* separator = strstr(line, "    ");
        if (!separator) continue;
        
        *separator = '\0';
        char* field = line;
        char* value = separator + 4;
        
        // Trim whitespace from value
        while (*value == ' ' || *value == '\t') value++;
        
        // Map fields to result structure
        if (strcmp(field, "manufacturer_ID") == 0)
        {
            strncpy(result->manufacturer_id, value, sizeof(result->manufacturer_id) - 1);
            found_data = true;
        }
        else if (strcmp(field, "ID") == 0)
        {
            strncpy(result->id, value, sizeof(result->id) - 1);
        }
        else if (strcmp(field, "version") == 0)
        {
            strncpy(result->version, value, sizeof(result->version) - 1);
        }
        else if (strcmp(field, "device_info") == 0)
        {
            strncpy(result->device_info, value, sizeof(result->device_info) - 1);
        }
        else if (strcmp(field, "internal_title") == 0)
        {
            strncpy(result->internal_title, value, sizeof(result->internal_title) - 1);
        }
        else if (strcmp(field, "release_date") == 0)
        {
            strncpy(result->release_date, value, sizeof(result->release_date) - 1);
        }
        else if (strcmp(field, "device_support") == 0)
        {
            strncpy(result->device_support, value, sizeof(result->device_support) - 1);
        }
        else if (strcmp(field, "target_area") == 0)
        {
            strncpy(result->target_area, value, sizeof(result->target_area) - 1);
        }
        else if (strcmp(field, "title") == 0)
        {
            strncpy(result->title, value, sizeof(result->title) - 1);
        }
        else if (strcmp(field, "language") == 0)
        {
            strncpy(result->language, value, sizeof(result->language) - 1);
        }
        else if (strcmp(field, "redump_name") == 0)
        {
            strncpy(result->redump_name, value, sizeof(result->redump_name) - 1);
        }
        else if (strcmp(field, "region") == 0)
        {
            strncpy(result->region, value, sizeof(result->region) - 1);
        }
    }
    
    int exit_code = pclose(fp);
    
    if (found_data && exit_code == 0)
    {
        result->valid = true;
        strncpy(result->system, system, sizeof(result->system) - 1);
        printf("CD-ROM: Game identified: %s (%s)\n", result->redump_name, result->region);
        return true;
    }
    
    printf("CD-ROM: Failed to identify disc (exit code: %d)\n", exit_code);
    return false;
}

bool cdrom_identify_game(const char* device_path, const char* system, CDRomGameInfo* game_info)
{
    if (!cdrom_initialized)
    {
        cdrom_init();
    }
    
    if (!cdrom_is_disc_inserted())
    {
        printf("CD-ROM: No disc inserted\n");
        return false;
    }
    
    return gameid_identify_disc(device_path, system, game_info);
}

bool cdrom_create_cue_bin(const char* device_path, const char* output_dir, const char* game_name)
{
    char bin_path[1024];
    char cue_path[1024];
    
    // Create output paths
    snprintf(bin_path, sizeof(bin_path), "%s/%s.bin", output_dir, game_name);
    snprintf(cue_path, sizeof(cue_path), "%s/%s.cue", output_dir, game_name);
    
    printf("CD-ROM: Creating disc image...\n");
    printf("CD-ROM: BIN: %s\n", bin_path);
    printf("CD-ROM: CUE: %s\n", cue_path);
    
    // Create BIN file using native C I/O (more reliable than dd)
    FILE* src_file = fopen(device_path, "rb");
    if (!src_file)
    {
        printf("CD-ROM: Failed to open CD device: %s\n", strerror(errno));
        return false;
    }
    
    FILE* dst_file = fopen(bin_path, "wb");
    if (!dst_file)
    {
        printf("CD-ROM: Failed to create BIN file: %s\n", strerror(errno));
        fclose(src_file);
        return false;
    }
    
    printf("CD-ROM: Reading disc data...\n");
    
    // Copy disc data in 2048-byte sectors (CD-ROM standard)
    char buffer[2048];
    size_t bytes_read;
    long total_bytes = 0;
    int sector_count = 0;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_file)) > 0)
    {
        size_t bytes_written = fwrite(buffer, 1, bytes_read, dst_file);
        if (bytes_written != bytes_read)
        {
            printf("CD-ROM: Write error at sector %d\n", sector_count);
            break;
        }
        
        total_bytes += bytes_written;
        sector_count++;
        
        // Progress indicator every 1000 sectors (~2MB)
        if (sector_count % 1000 == 0)
        {
            printf("CD-ROM: Read %d sectors (%.1f MB)...\n", 
                   sector_count, total_bytes / (1024.0 * 1024.0));
        }
        
        // Handle partial sector reads for damaged discs
        if (bytes_read < sizeof(buffer))
        {
            printf("CD-ROM: Partial read at sector %d, padding with zeros\n", sector_count);
            memset(buffer + bytes_read, 0, sizeof(buffer) - bytes_read);
            fwrite(buffer + bytes_read, 1, sizeof(buffer) - bytes_read, dst_file);
            total_bytes += sizeof(buffer) - bytes_read;
        }
    }
    
    fclose(src_file);
    fclose(dst_file);
    
    printf("CD-ROM: Disc copy complete - %d sectors (%.1f MB)\n", 
           sector_count, total_bytes / (1024.0 * 1024.0));
    
    if (total_bytes == 0)
    {
        printf("CD-ROM: Failed to read any data from disc\n");
        return false;
    }
    
    // Create CUE file
    FILE* cue_file = fopen(cue_path, "w");
    if (!cue_file)
    {
        printf("CD-ROM: Failed to create CUE file: %s\n", strerror(errno));
        return false;
    }
    
    fprintf(cue_file, "FILE \"%s.bin\" BINARY\n", game_name);
    fprintf(cue_file, "  TRACK 01 MODE1/2048\n");
    fprintf(cue_file, "    INDEX 01 00:00:00\n");
    
    fclose(cue_file);
    
    printf("CD-ROM: Disc image created successfully\n");
    return true;
}

bool cdrom_create_image(const char* device_path, const char* output_path, const char* game_name)
{
    // Extract directory from output_path
    char output_dir[1024];
    strcpy(output_dir, output_path);
    char* last_slash = strrchr(output_dir, '/');
    if (last_slash)
    {
        *last_slash = '\0';
    }
    
    // Create output directory if it doesn't exist
    char mkdir_command[1024];
    snprintf(mkdir_command, sizeof(mkdir_command), "mkdir -p \"%s\"", output_dir);
    system(mkdir_command);
    
    return cdrom_create_cue_bin(device_path, output_dir, game_name);
}

const char* cdrom_get_system_from_detection()
{
    // Try different systems in order of likelihood
    const char* systems[] = {"PSX", "Saturn", "MegaCD", "PCECD", NULL};
    
    for (int i = 0; systems[i]; i++)
    {
        CDRomGameInfo game_info;
        if (gameid_identify_disc(cdrom_device_path, systems[i], &game_info))
        {
            return systems[i];
        }
    }
    
    return "PSX"; // Default fallback
}

char* cdrom_sanitize_filename(const char* name)
{
    static char sanitized[256];
    int j = 0;
    
    if (!name) return NULL;
    
    for (int i = 0; name[i] && j < 255; i++)
    {
        char c = name[i];
        
        // Replace problematic characters with safe alternatives
        if (c == '/' || c == '\\' || c == ':' || c == '*' || 
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            sanitized[j++] = '_';
        }
        else if (c >= 32 && c <= 126) // Printable ASCII
        {
            sanitized[j++] = c;
        }
    }
    
    sanitized[j] = '\0';
    
    // Remove trailing spaces/dots
    while (j > 0 && (sanitized[j-1] == ' ' || sanitized[j-1] == '.'))
    {
        sanitized[--j] = '\0';
    }
    
    return sanitized;
}

bool cdrom_store_game_to_library(const char* device_path, const char* system, CDRomGameInfo* game_info)
{
    if (!game_info || !game_info->valid)
    {
        printf("CD-ROM: Invalid game info, cannot store to library\n");
        return false;
    }
    
    // Use title if available, otherwise use redump_name
    const char* game_name = game_info->title[0] ? game_info->title : game_info->redump_name;
    if (!game_name[0])
    {
        printf("CD-ROM: No valid game name found\n");
        return false;
    }
    
    // Sanitize the game name for filesystem use
    char* safe_name = cdrom_sanitize_filename(game_name);
    if (!safe_name || !safe_name[0])
    {
        printf("CD-ROM: Failed to create safe filename\n");
        return false;
    }
    
    // Create system directory
    char system_dir[1024];
    snprintf(system_dir, sizeof(system_dir), "/media/fat/games/%s", system);
    
    char mkdir_command[1024];
    snprintf(mkdir_command, sizeof(mkdir_command), "mkdir -p \"%s\"", system_dir);
    system(mkdir_command);
    
    printf("CD-ROM: Storing game '%s' to library at %s\n", safe_name, system_dir);
    
    // Create disc image in the games directory
    bool result = cdrom_create_cue_bin(device_path, system_dir, safe_name);
    
    if (result)
    {
        printf("CD-ROM: Successfully stored game to library\n");
        
        // Create a metadata file with game information
        char metadata_path[1024];
        snprintf(metadata_path, sizeof(metadata_path), "%s/%s.info", system_dir, safe_name);
        
        FILE* info_file = fopen(metadata_path, "w");
        if (info_file)
        {
            fprintf(info_file, "Title: %s\n", game_info->title);
            fprintf(info_file, "System: %s\n", game_info->system);
            fprintf(info_file, "Region: %s\n", game_info->region);
            fprintf(info_file, "Game Name: %s\n", game_info->redump_name);
            fprintf(info_file, "Internal Title: %s\n", game_info->internal_title);
            fprintf(info_file, "Release Date: %s\n", game_info->release_date);
            fprintf(info_file, "Language: %s\n", game_info->language);
            fprintf(info_file, "Device Info: %s\n", game_info->device_info);
            fclose(info_file);
        }
    }
    
    return result;
}

bool cdrom_load_disc_auto()
{
    if (!cdrom_initialized)
    {
        cdrom_init();
    }
    
    if (!cdrom_is_disc_inserted())
    {
        printf("CD-ROM: No disc inserted\n");
        return false;
    }
    
    printf("CD-ROM: Auto-detecting disc system...\n");
    
    // Try to detect the system automatically
    const char* detected_system = cdrom_get_system_from_detection();
    if (!detected_system)
    {
        printf("CD-ROM: Failed to auto-detect system\n");
        return false;
    }
    
    return cdrom_load_disc_with_system(detected_system);
}

bool cdrom_load_disc_with_system(const char* system)
{
    if (!cdrom_initialized)
    {
        cdrom_init();
    }
    
    if (!cdrom_is_disc_inserted())
    {
        printf("CD-ROM: No disc inserted\n");
        return false;
    }
    
    printf("CD-ROM: Loading disc as %s system...\n", system);
    
    // Identify the game using GameID
    CDRomGameInfo game_info;
    if (!cdrom_identify_game(cdrom_device_path, system, &game_info))
    {
        printf("CD-ROM: Failed to identify game\n");
        return false;
    }
    
    // Store the game to the library
    if (!cdrom_store_game_to_library(cdrom_device_path, system, &game_info))
    {
        printf("CD-ROM: Failed to store game to library\n");
        return false;
    }
    
    printf("CD-ROM: Successfully loaded disc '%s' (%s)\n", 
           game_info.title[0] ? game_info.title : game_info.redump_name, 
           game_info.region);
    
    return true;
}

void cdrom_print_status()
{
    printf("CD-ROM System Status:\n");
    printf("====================\n");
    printf("Initialized: %s\n", cdrom_initialized ? "Yes" : "No");
    printf("Drive detected: %s\n", cdrom_drive_detected ? "Yes" : "No");
    printf("Device path: %s\n", cdrom_device_path);
    
    if (cdrom_drive_detected)
    {
        printf("Disc inserted: %s\n", cdrom_is_disc_inserted() ? "Yes" : "No");
    }
    
    // Check GameID environment
    printf("GameID script: %s\n", 
           FileExists("/media/fat/Scripts/_GameID/GameID.py") ? "Found" : "Missing");
    printf("GameID database: %s\n", 
           FileExists("/media/fat/gameID/db.pkl.gz") ? "Found" : "Missing");
    
    // Check directories
    printf("Games directory: %s\n", 
           FileExists("/media/fat/games") ? "Exists" : "Missing");
    
    printf("====================\n");
}

bool cdrom_test_device(const char* device_path)
{
    printf("Testing CD-ROM device: %s\n", device_path);
    
    // Test 1: Check if device exists
    struct stat st;
    if (stat(device_path, &st) != 0)
    {
        printf("✗ Device does not exist\n");
        return false;
    }
    printf("✓ Device exists\n");
    
    // Test 2: Try to open device
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        printf("✗ Cannot open device: %s\n", strerror(errno));
        return false;
    }
    printf("✓ Device can be opened\n");
    
    // Test 3: Try to read first sector
    char buffer[2048];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
    close(fd);
    
    if (bytes_read < 0)
    {
        printf("✗ Cannot read from device: %s\n", strerror(errno));
        return false;
    }
    else if (bytes_read == 0)
    {
        printf("✗ No data read (empty/no disc)\n");
        return false;
    }
    else
    {
        printf("✓ Read %ld bytes from device\n", bytes_read);
        
        // Check for ISO 9660 signature
        if (bytes_read >= 5 && strncmp(buffer + 1, "CD001", 5) == 0)
        {
            printf("✓ ISO 9660 filesystem detected\n");
        }
        else
        {
            printf("? Non-ISO filesystem or audio CD\n");
        }
    }
    
    return true;
}