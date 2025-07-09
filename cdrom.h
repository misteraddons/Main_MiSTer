#ifndef CDROM_H
#define CDROM_H

#include <stdbool.h>

// CD-ROM game identification result
typedef struct CDRomGameInfo {
    char manufacturer_id[64];
    char id[64]; 
    char version[64];
    char device_info[64];
    char internal_title[128];
    char release_date[32];
    char device_support[64];
    char target_area[64];
    char title[256];
    char language[64];
    char redump_name[256];
    char region[64];
    char system[32];
    bool valid;
} CDRomGameInfo;

// CD-ROM detection and management
bool cdrom_detect_drive();
bool cdrom_mount_device(const char* device_path);
bool cdrom_is_disc_inserted();
bool cdrom_identify_game(const char* device_path, const char* system, CDRomGameInfo* game_info);
bool cdrom_create_image(const char* device_path, const char* output_path, const char* game_name);
void cdrom_init();
void cdrom_cleanup();

// GameID integration
bool gameid_setup_environment();
bool gameid_identify_disc(const char* device_path, const char* system, CDRomGameInfo* result);

// Utility functions
const char* cdrom_get_system_from_detection();
bool cdrom_create_cue_bin(const char* device_path, const char* output_dir, const char* game_name);
bool cdrom_store_game_to_library(const char* device_path, const char* system, CDRomGameInfo* game_info);
char* cdrom_sanitize_filename(const char* name);

// High-level functions
bool cdrom_load_disc_auto();
bool cdrom_load_disc_with_system(const char* system);

// Debug/testing functions
void cdrom_print_status();
bool cdrom_test_device(const char* device_path);

#endif // CDROM_H