/*
 * Redump Database Integration for MiSTer CD-ROM Daemon
 * 
 * Provides disc verification and metadata from Redump.org databases
 */

#ifndef REDUMP_INTEGRATION_H
#define REDUMP_INTEGRATION_H

#include <stdint.h>
#include <stdbool.h>

// Redump disc entry structure
typedef struct {
    char game_name[256];
    char disc_title[256];
    char disc_id[64];
    char region[32];
    char languages[128];
    char version[32];
    char edition[64];
    
    // Disc verification data
    uint32_t crc32;
    char md5[33];
    char sha1[41];
    uint64_t size_bytes;
    
    // Track information
    int track_count;
    struct {
        int track_num;
        char type[16];  // "data" or "audio"
        uint32_t pregap;
        uint32_t length;
        uint32_t crc32;
        char md5[33];
    } tracks[99];
    
    // Dump information
    char dumper[64];
    char date_dumped[32];
    int dump_count;  // Number of verified dumps
    bool verified;
    
    // Ring codes and serials
    char ring_code[128];
    char barcode[64];
    char serial[64];
} redump_disc_t;

// Redump database functions
bool redump_load_database(const char* system);
bool redump_find_disc_by_serial(const char* system, const char* serial, redump_disc_t* disc);
bool redump_find_disc_by_crc32(const char* system, uint32_t crc32, redump_disc_t* disc);
bool redump_verify_disc_image(const char* image_path, redump_disc_t* disc);
bool redump_verify_physical_disc(const char* device, redump_disc_t* disc);

// Disc checksum calculation
uint32_t redump_calc_crc32_track(const char* device, int track);
bool redump_calc_disc_checksums(const char* device, uint32_t* crc32, char* md5, char* sha1);

// Database paths
#define REDUMP_DB_PATH "/media/fat/utils/redump"
#define REDUMP_PSX_DB  REDUMP_DB_PATH "/psx_redump.db"
#define REDUMP_SAT_DB  REDUMP_DB_PATH "/saturn_redump.db"
#define REDUMP_MCD_DB  REDUMP_DB_PATH "/megacd_redump.db"
#define REDUMP_PCE_DB  REDUMP_DB_PATH "/pcecd_redump.db"

#endif // REDUMP_INTEGRATION_H