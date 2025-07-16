#ifndef ROM_PATCHES_H
#define ROM_PATCHES_H

#include <stdint.h>
#include <stdbool.h>

// ROM patch format types
typedef enum {
    PATCH_FORMAT_IPS,
    PATCH_FORMAT_BPS,
    PATCH_FORMAT_UPS,
    PATCH_FORMAT_XDELTA,
    PATCH_FORMAT_UNKNOWN
} patch_format_t;

// ROM patch information structure
typedef struct {
    char name[256];             // Patch display name (derived from filename)
    char filepath[1024];        // Full path to patch file
    patch_format_t format;      // Patch format type
    uint32_t size;              // Patch file size
    bool validated;             // Whether patch has been validated
} patch_info_t;

// Function declarations
void patches_init();
void patches_cleanup();

// Patch discovery and validation
int patches_find_for_rom(const char* rom_path, patch_info_t** patches, int max_patches);
char* patches_find_patch_folder(const char* rom_path, const char* core_name, uint32_t romcrc);
bool patches_is_patch_file(const char* filename);

// Patch application
bool patches_apply_patch(const char* rom_path, const char* patch_path, const char* output_path);
patch_format_t patches_detect_format(const char* patch_path);

// Progress callback for patching operations
typedef void (*patch_progress_callback_t)(int percent, const char* message);
void patches_set_progress_callback(patch_progress_callback_t callback);

// Utility functions
void patches_extract_game_name(const char* patch_path, char* game_name, size_t game_name_size);
void patches_get_temp_path(const char* patch_name, const char* rom_extension, char* temp_path, size_t temp_path_size);
void patches_get_descriptive_temp_path(const char* patch_name, const char* rom_name, char* temp_path, size_t temp_path_size);

#endif // ROM_PATCHES_H