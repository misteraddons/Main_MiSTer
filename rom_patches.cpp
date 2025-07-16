#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

#include "rom_patches.h"
#include "file_io.h"
#include "user_io.h"
#include "osd.h"

static patch_progress_callback_t progress_callback = NULL;

void patches_init()
{
    // Create /tmp directory if it doesn't exist
    mkdir("/tmp", 0755);
    
    printf("ROM Patches: Initialized\n");
}

void patches_cleanup()
{
    // Clean up any temporary patched ROM files
    system("rm -f /tmp/*.sfc /tmp/*.smc /tmp/*.bin /tmp/*.md /tmp/*.nes /tmp/*.gb /tmp/*.gbc /tmp/*.gba 2>/dev/null");
    printf("ROM Patches: Cleaned up temporary files\n");
}

bool patches_is_patch_file(const char* filename)
{
    if (!filename) return false;
    
    const char* ext = strrchr(filename, '.');
    if (!ext) return false;
    
    return (!strcasecmp(ext, ".ips") ||
            !strcasecmp(ext, ".bps") ||
            !strcasecmp(ext, ".ups") ||
            !strcasecmp(ext, ".xdelta"));
}

patch_format_t patches_detect_format(const char* patch_path)
{
    const char* ext = strrchr(patch_path, '.');
    if (!ext) return PATCH_FORMAT_UNKNOWN;
    
    if (!strcasecmp(ext, ".ips")) return PATCH_FORMAT_IPS;
    if (!strcasecmp(ext, ".bps")) return PATCH_FORMAT_BPS;
    if (!strcasecmp(ext, ".ups")) return PATCH_FORMAT_UPS;
    if (!strcasecmp(ext, ".xdelta")) return PATCH_FORMAT_XDELTA;
    
    return PATCH_FORMAT_UNKNOWN;
}

void patches_extract_game_name(const char* patch_path, char* game_name, size_t game_name_size)
{
    // Extract game name from patch path
    // /media/fat/Rom Patches/SNES/Super Mario World (USA)/patch.ips
    // → Super Mario World (USA)
    
    char path_copy[1024];
    strncpy(path_copy, patch_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = 0;
    
    // Get directory name (game name)
    char* dir = dirname(path_copy);
    char* game = basename(dir);
    
    strncpy(game_name, game, game_name_size - 1);
    game_name[game_name_size - 1] = 0;
}

static char patch_folder[1024] = {};

static int find_patch_by_crc(const char* core_name, uint32_t romcrc)
{
    if (!romcrc) return 0;
    snprintf(patch_folder, sizeof(patch_folder), "/media/fat/rom_patches/%s", core_name);
    DIR* d = opendir(patch_folder);
    if (!d) {
        printf("ROM Patches: Could not open directory %s\n", patch_folder);
        return 0;
    }
    
    struct dirent* de;
    while ((de = readdir(d))) {
        if (de->d_type != DT_DIR) continue;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        
        // Check if folder name contains the CRC32
        char crc_str[16];
        snprintf(crc_str, sizeof(crc_str), "%08X", romcrc);
        if (strstr(de->d_name, crc_str)) {
            snprintf(patch_folder, sizeof(patch_folder), "/media/fat/rom_patches/%s/%s", core_name, de->d_name);
            closedir(d);
            return 1;
        }
    }
    
    closedir(d);
    return 0;
}

static int find_patch_by_name(const char* core_name, const char* rom_name)
{
    snprintf(patch_folder, sizeof(patch_folder), "/media/fat/rom_patches/%s/%s", core_name, rom_name);
    
    // Check if directory exists
    DIR* d = opendir(patch_folder);
    if (d) {
        closedir(d);
        return 1;
    }
    return 0;
}

static void create_empty_patch_folder(const char* core_name, const char* rom_name, uint32_t romcrc)
{
    // Create folder with format: [ROM_NAME] [CRC32]
    char folder_name[512];
    snprintf(folder_name, sizeof(folder_name), "%s [%08X]", rom_name, romcrc);
    
    // Create full path
    snprintf(patch_folder, sizeof(patch_folder), "/media/fat/rom_patches/%s/%s", core_name, folder_name);
    
    // Create directory (recursive)
    char mkdir_cmd[1024];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", patch_folder);
    system(mkdir_cmd);
    
    // Create README.md with instructions
    char readme_path[1024];
    snprintf(readme_path, sizeof(readme_path), "%s/README.md", patch_folder);
    
    FILE* readme = fopen(readme_path, "w");
    if (readme) {
        fprintf(readme, "# ROM Patches for %s\n\n", rom_name);
        fprintf(readme, "**Platform**: %s\n", core_name);
        fprintf(readme, "**CRC32**: %08X\n\n", romcrc);
        fprintf(readme, "## How to add patches:\n\n");
        fprintf(readme, "1. Download ROM hack files (.ips, .bps, .ups, .xdelta) from:\n");
        fprintf(readme, "   - https://www.romhacking.net\n");
        fprintf(readme, "   - https://romhackplaza.org\n");
        fprintf(readme, "   - Platform-specific communities\n\n");
        fprintf(readme, "2. Place patch files in this folder\n\n");
        fprintf(readme, "3. Patches will be automatically detected by MiSTer\n\n");
        fprintf(readme, "## Supported formats:\n");
        fprintf(readme, "- .ips (International Patching System)\n");
        fprintf(readme, "- .bps (Binary Patching System)\n");
        fprintf(readme, "- .ups (Universal Patching System)\n");
        fprintf(readme, "- .xdelta (Delta compression)\n\n");
        fprintf(readme, "## Search tips:\n");
        fprintf(readme, "- Search by game name: \"%s\"\n", rom_name);
        fprintf(readme, "- Search by CRC32: \"%08X\"\n", romcrc);
        fprintf(readme, "- Browse by platform: \"%s\"\n", core_name);
        fclose(readme);
    }
    
    printf("ROM Patches: Created empty patch folder: %s\n", patch_folder);
}

char* patches_find_patch_folder(const char* rom_path, const char* core_name, uint32_t romcrc)
{
    // Try to find patch folder
    // 1. Try by ROM filename: /media/fat/rom_patches/[CORE]/[ROM_NAME]/
    // 2. Try by CRC32: /media/fat/rom_patches/[CORE]/[GAME_NAME] [CRC32]/
    
    const char* rom_name = strrchr(rom_path, '/');
    if (rom_name) {
        rom_name++; // Skip the '/'
        
        // Remove extension from ROM name
        char name_no_ext[256];
        strncpy(name_no_ext, rom_name, sizeof(name_no_ext) - 1);
        name_no_ext[sizeof(name_no_ext) - 1] = 0;
        
        char* dot = strrchr(name_no_ext, '.');
        if (dot) *dot = 0;
        
        // Try by ROM name first
        if (find_patch_by_name(core_name, name_no_ext)) {
            printf("ROM Patches: Found patch folder by name: %s\n", patch_folder);
            return patch_folder;
        }
    }
    
    // Try by CRC32
    if (find_patch_by_crc(core_name, romcrc)) {
        printf("ROM Patches: Found patch folder by CRC32: %s\n", patch_folder);
        return patch_folder;
    }
    
    // No existing folder found - create empty one
    const char* rom_filename = strrchr(rom_path, '/');
    if (rom_filename) {
        rom_filename++; // Skip the '/'
        
        // Remove extension from ROM filename
        char clean_name[256];
        strncpy(clean_name, rom_filename, sizeof(clean_name) - 1);
        clean_name[sizeof(clean_name) - 1] = 0;
        
        char* dot = strrchr(clean_name, '.');
        if (dot) *dot = 0;
        
        create_empty_patch_folder(core_name, clean_name, romcrc);
        return patch_folder;
    }
    
    printf("ROM Patches: Could not determine ROM name\n");
    return NULL;
}

void patches_get_temp_path(const char* patch_name, const char* rom_extension, char* temp_path, size_t temp_path_size)
{
    // Extract patch filename (not full path)
    const char* patch_filename = strrchr(patch_name, '/');
    if (patch_filename) {
        patch_filename++; // Skip the '/'
    } else {
        patch_filename = patch_name;
    }
    
    // Extract patch name without extension
    char patch_basename[256];
    strncpy(patch_basename, patch_filename, sizeof(patch_basename) - 1);
    patch_basename[sizeof(patch_basename) - 1] = 0;
    
    char* dot = strrchr(patch_basename, '.');
    if (dot) *dot = 0;
    
    // Create temp path: /tmp/[patch_name].[rom_extension]
    snprintf(temp_path, temp_path_size, "/tmp/%s%s", patch_basename, rom_extension);
}

void patches_get_descriptive_temp_path(const char* patch_name, const char* rom_name, char* temp_path, size_t temp_path_size)
{
    // Extract just the ROM name without path and extension
    const char* rom_filename = strrchr(rom_name, '/');
    if (rom_filename) {
        rom_filename++; // Skip the '/'
    } else {
        rom_filename = rom_name;
    }
    
    char rom_basename[256];
    strncpy(rom_basename, rom_filename, sizeof(rom_basename) - 1);
    rom_basename[sizeof(rom_basename) - 1] = 0;
    
    char* rom_ext = strrchr(rom_basename, '.');
    if (rom_ext) {
        *rom_ext = 0; // Remove extension but save it
        rom_ext++; // Point to extension
    } else {
        rom_ext = "rom"; // Default extension
    }
    
    // Extract patch filename
    const char* patch_filename = strrchr(patch_name, '/');
    if (patch_filename) {
        patch_filename++; // Skip the '/'
    } else {
        patch_filename = patch_name;
    }
    
    // Extract patch name without extension
    char patch_basename[256];
    strncpy(patch_basename, patch_filename, sizeof(patch_basename) - 1);
    patch_basename[sizeof(patch_basename) - 1] = 0;
    
    char* dot = strrchr(patch_basename, '.');
    if (dot) *dot = 0;
    
    // Create descriptive temp path: /tmp/[patch_name].sfc
    // This gives clear indication of what hack is loaded
    snprintf(temp_path, temp_path_size, "/tmp/%s.%s", patch_basename, rom_ext);
}

void patches_set_progress_callback(patch_progress_callback_t callback)
{
    progress_callback = callback;
}

static void report_progress(int percent, const char* message)
{
    if (progress_callback) {
        progress_callback(percent, message);
    }
    printf("ROM Patches: %s (%d%%)\n", message, percent);
}

// IPS patch format implementation
static bool apply_ips_patch(const char* rom_path, const char* patch_path, const char* output_path)
{
    FILE* rom_file = fopen(rom_path, "rb");
    if (!rom_file) {
        printf("ROM Patches: Could not open ROM file: %s\n", rom_path);
        return false;
    }
    
    FILE* patch_file = fopen(patch_path, "rb");
    if (!patch_file) {
        printf("ROM Patches: Could not open patch file: %s\n", patch_path);
        fclose(rom_file);
        return false;
    }
    
    // Get ROM size
    fseek(rom_file, 0, SEEK_END);
    long rom_size = ftell(rom_file);
    fseek(rom_file, 0, SEEK_SET);
    
    // Allocate ROM buffer
    uint8_t* rom_data = (uint8_t*)malloc(rom_size);
    if (!rom_data) {
        printf("ROM Patches: Could not allocate ROM buffer\n");
        fclose(rom_file);
        fclose(patch_file);
        return false;
    }
    
    // Read ROM data
    if (fread(rom_data, 1, rom_size, rom_file) != (size_t)rom_size) {
        printf("ROM Patches: Could not read ROM data\n");
        free(rom_data);
        fclose(rom_file);
        fclose(patch_file);
        return false;
    }
    fclose(rom_file);
    
    report_progress(20, "ROM loaded, applying IPS patch");
    
    // Read and verify IPS header
    char header[5];
    if (fread(header, 1, 5, patch_file) != 5 || memcmp(header, "PATCH", 5) != 0) {
        printf("ROM Patches: Invalid IPS patch header\n");
        free(rom_data);
        fclose(patch_file);
        return false;
    }
    
    report_progress(30, "IPS header verified, applying patches");
    
    // Apply patch records
    int progress = 30;
    while (1) {
        uint8_t record[3];
        if (fread(record, 1, 3, patch_file) != 3) break;
        
        // Check for EOF marker
        if (memcmp(record, "EOF", 3) == 0) {
            report_progress(90, "IPS patching complete");
            break;
        }
        
        // Parse patch record
        uint32_t offset = (record[0] << 16) | (record[1] << 8) | record[2];
        
        uint8_t size_bytes[2];
        if (fread(size_bytes, 1, 2, patch_file) != 2) {
            printf("ROM Patches: Unexpected end of patch file\n");
            free(rom_data);
            fclose(patch_file);
            return false;
        }
        
        uint16_t size = (size_bytes[0] << 8) | size_bytes[1];
        
        if (size == 0) {
            // RLE record
            if (fread(size_bytes, 1, 2, patch_file) != 2) {
                printf("ROM Patches: Unexpected end of patch file\n");
                free(rom_data);
                fclose(patch_file);
                return false;
            }
            
            uint16_t rle_size = (size_bytes[0] << 8) | size_bytes[1];
            uint8_t rle_byte;
            if (fread(&rle_byte, 1, 1, patch_file) != 1) {
                printf("ROM Patches: Unexpected end of patch file\n");
                free(rom_data);
                fclose(patch_file);
                return false;
            }
            
            // Apply RLE patch
            for (int i = 0; i < rle_size && offset + i < (uint32_t)rom_size; i++) {
                rom_data[offset + i] = rle_byte;
            }
        } else {
            // Normal record
            uint8_t* patch_data = (uint8_t*)malloc(size);
            if (!patch_data || fread(patch_data, 1, size, patch_file) != size) {
                printf("ROM Patches: Could not read patch data\n");
                if (patch_data) free(patch_data);
                free(rom_data);
                fclose(patch_file);
                return false;
            }
            
            // Apply normal patch
            for (int i = 0; i < size && offset + i < (uint32_t)rom_size; i++) {
                rom_data[offset + i] = patch_data[i];
            }
            
            free(patch_data);
        }
        
        // Update progress occasionally
        if (progress < 85) {
            progress += 5;
            report_progress(progress, "Applying IPS records");
        }
    }
    
    fclose(patch_file);
    
    report_progress(95, "Writing patched ROM");
    
    // Write patched ROM
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        printf("ROM Patches: Could not create output file: %s\n", output_path);
        free(rom_data);
        return false;
    }
    
    if (fwrite(rom_data, 1, rom_size, output_file) != (size_t)rom_size) {
        printf("ROM Patches: Could not write patched ROM\n");
        free(rom_data);
        fclose(output_file);
        return false;
    }
    
    fclose(output_file);
    free(rom_data);
    
    report_progress(100, "IPS patch applied successfully");
    return true;
}

bool patches_apply_patch(const char* rom_path, const char* patch_path, const char* output_path)
{
    patch_format_t format = patches_detect_format(patch_path);
    
    report_progress(0, "Starting patch application");
    
    switch (format) {
        case PATCH_FORMAT_IPS:
            return apply_ips_patch(rom_path, patch_path, output_path);
            
        case PATCH_FORMAT_BPS:
        case PATCH_FORMAT_UPS:
        case PATCH_FORMAT_XDELTA:
            printf("ROM Patches: Format not yet implemented: %d\n", format);
            return false;
            
        default:
            printf("ROM Patches: Unknown patch format\n");
            return false;
    }
}

int patches_find_for_rom(const char* rom_path, patch_info_t** patches, int max_patches)
{
    // This function would search for patches associated with a ROM
    // For now, return 0 (no patches found) as this is for the reverse case
    // where we start with a patch and find the ROM
    return 0;
}