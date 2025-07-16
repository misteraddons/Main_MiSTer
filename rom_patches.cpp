#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>

#include "rom_patches.h"
#include "file_io.h"
#include "user_io.h"
#include "osd.h"

static patch_progress_callback_t progress_callback = NULL;

// CRC32 lookup table
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void init_crc32_table(void)
{
    if (crc32_table_initialized) return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

static uint32_t calculate_crc32(const uint8_t* data, size_t length)
{
    init_crc32_table();
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}


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
        rom_ext = (char*)"rom"; // Default extension
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

// BPS patch format implementation - exact reference implementation
enum bps_result { BPS_OK, BPS_SRCSUM, BPS_TGTSUM, BPS_RANGE };

struct bps_info {
    int64_t srclen, tgtlen;
    size_t metaoff, metalen;
    uint32_t srcsum, tgtsum, bpssum;
};

// Read a 32-bit little endian integer.
static uint32_t u32le(uint8_t *buf)
{
    return (uint32_t)buf[0] <<  0 | (uint32_t)buf[1] <<  8 |
           (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
}

// Append bytes to a CRC32 checksum. Initialize to zero.
static uint32_t crc32(uint32_t crc, uint8_t *buf, size_t len)
{
    static const uint32_t crc32_table[] = {
        0x00000000,0x77073096,0xee0e612c,0x990951ba,0x076dc419,0x706af48f,
        0xe963a535,0x9e6495a3,0x0edb8832,0x79dcb8a4,0xe0d5e91e,0x97d2d988,
        0x09b64c2b,0x7eb17cbd,0xe7b82d07,0x90bf1d91,0x1db71064,0x6ab020f2,
        0xf3b97148,0x84be41de,0x1adad47d,0x6ddde4eb,0xf4d4b551,0x83d385c7,
        0x136c9856,0x646ba8c0,0xfd62f97a,0x8a65c9ec,0x14015c4f,0x63066cd9,
        0xfa0f3d63,0x8d080df5,0x3b6e20c8,0x4c69105e,0xd56041e4,0xa2677172,
        0x3c03e4d1,0x4b04d447,0xd20d85fd,0xa50ab56b,0x35b5a8fa,0x42b2986c,
        0xdbbbc9d6,0xacbcf940,0x32d86ce3,0x45df5c75,0xdcd60dcf,0xabd13d59,
        0x26d930ac,0x51de003a,0xc8d75180,0xbfd06116,0x21b4f4b5,0x56b3c423,
        0xcfba9599,0xb8bda50f,0x2802b89e,0x5f058808,0xc60cd9b2,0xb10be924,
        0x2f6f7c87,0x58684c11,0xc1611dab,0xb6662d3d,0x76dc4190,0x01db7106,
        0x98d220bc,0xefd5102a,0x71b18589,0x06b6b51f,0x9fbfe4a5,0xe8b8d433,
        0x7807c9a2,0x0f00f934,0x9609a88e,0xe10e9818,0x7f6a0dbb,0x086d3d2d,
        0x91646c97,0xe6635c01,0x6b6b51f4,0x1c6c6162,0x856530d8,0xf262004e,
        0x6c0695ed,0x1b01a57b,0x8208f4c1,0xf50fc457,0x65b0d9c6,0x12b7e950,
        0x8bbeb8ea,0xfcb9887c,0x62dd1ddf,0x15da2d49,0x8cd37cf3,0xfbd44c65,
        0x4db26158,0x3ab551ce,0xa3bc0074,0xd4bb30e2,0x4adfa541,0x3dd895d7,
        0xa4d1c46d,0xd3d6f4fb,0x4369e96a,0x346ed9fc,0xad678846,0xda60b8d0,
        0x44042d73,0x33031de5,0xaa0a4c5f,0xdd0d7cc9,0x5005713c,0x270241aa,
        0xbe0b1010,0xc90c2086,0x5768b525,0x206f85b3,0xb966d409,0xce61e49f,
        0x5edef90e,0x29d9c998,0xb0d09822,0xc7d7a8b4,0x59b33d17,0x2eb40d81,
        0xb7bd5c3b,0xc0ba6cad,0xedb88320,0x9abfb3b6,0x03b6e20c,0x74b1d29a,
        0xead54739,0x9dd277af,0x04db2615,0x73dc1683,0xe3630b12,0x94643b84,
        0x0d6d6a3e,0x7a6a5aa8,0xe40ecf0b,0x9309ff9d,0x0a00ae27,0x7d079eb1,
        0xf00f9344,0x8708a3d2,0x1e01f268,0x6906c2fe,0xf762575d,0x806567cb,
        0x196c3671,0x6e6b06e7,0xfed41b76,0x89d32be0,0x10da7a5a,0x67dd4acc,
        0xf9b9df6f,0x8ebeeff9,0x17b7be43,0x60b08ed5,0xd6d6a3e8,0xa1d1937e,
        0x38d8c2c4,0x4fdff252,0xd1bb67f1,0xa6bc5767,0x3fb506dd,0x48b2364b,
        0xd80d2bda,0xaf0a1b4c,0x36034af6,0x41047a60,0xdf60efc3,0xa867df55,
        0x316e8eef,0x4669be79,0xcb61b38c,0xbc66831a,0x256fd2a0,0x5268e236,
        0xcc0c7795,0xbb0b4703,0x220216b9,0x5505262f,0xc5ba3bbe,0xb2bd0b28,
        0x2bb45a92,0x5cb36a04,0xc2d7ffa7,0xb5d0cf31,0x2cd99e8b,0x5bdeae1d,
        0x9b64c2b0,0xec63f226,0x756aa39c,0x026d930a,0x9c0906a9,0xeb0e363f,
        0x72076785,0x05005713,0x95bf4a82,0xe2b87a14,0x7bb12bae,0x0cb61b38,
        0x92d28e9b,0xe5d5be0d,0x7cdcefb7,0x0bdbdf21,0x86d3d2d4,0xf1d4e242,
        0x68ddb3f8,0x1fda836e,0x81be16cd,0xf6b9265b,0x6fb077e1,0x18b74777,
        0x88085ae6,0xff0f6a70,0x66063bca,0x11010b5c,0x8f659eff,0xf862ae69,
        0x616bffd3,0x166ccf45,0xa00ae278,0xd70dd2ee,0x4e048354,0x3903b3c2,
        0xa7672661,0xd06016f7,0x4969474d,0x3e6e77db,0xaed16a4a,0xd9d65adc,
        0x40df0b66,0x37d83bf0,0xa9bcae53,0xdebb9ec5,0x47b2cf7f,0x30b5ffe9,
        0xbdbdf21c,0xcabac28a,0x53b39330,0x24b4a3a6,0xbad03605,0xcdd70693,
        0x54de5729,0x23d967bf,0xb3667a2e,0xc4614ab8,0x5d681b02,0x2a6f2b94,
        0xb40bbe37,0xc30c8ea1,0x5a05df1b,0x2d02ef8d
    };
    crc ^= 0xffffffff;
    for (size_t n = 0; n < len; n++) {
        crc = crc32_table[(crc ^ buf[n])&0xff] ^ crc>>8;
    }
    return crc ^ 0xffffffff;
}

// Read a varint in [0 .. 72,624,976,668,147,839] (57 bits), returning
// the number of bytes consumed ([0 .. 8]). For invalid input (out of
// range, truncated), sets the value to -1 and returns zero.
static int bps_number(uint8_t *buf, size_t len, int64_t *r)
{
    int64_t v = 0;
    uint8_t *p = buf, *e = buf + len;
    for (int s = 0; p<e && s<=49; s += 7) {
        v += (int64_t)(*p & 0x7f) << s;
        if (*p++ & 0x80) {
            *r = v;
            return p - buf;
        }
        v += (int64_t)1 << (s+7);
    }
    *r = -1;
    return 0;
}

// Validate and extract basic information about a BPS patch. Returns
// zero on error.
static int bps_info(uint8_t *bps, size_t len, struct bps_info *info)
{
    // A minimal, empty patch is 19 bytes
    // "BPS1"  80 80 80  00 00 00 00  00 00 00 00  93 1f d8 5e
    if (len < 4+1+1+1+12 || memcmp(bps, "BPS1", 4)) {
        return 0;
    }

    info->srcsum = u32le(bps + len - 12);
    info->tgtsum = u32le(bps + len -  8);
    info->bpssum = u32le(bps + len -  4);
    if (info->bpssum != crc32(0, bps, len-4)) {
        return 0;
    }

    int off = 4;
    off += bps_number(bps+off, len-12-off, &info->srclen);
    off += bps_number(bps+off, len-12-off, &info->tgtlen);
    if (info->srclen<0 || info->tgtlen<0) {
        return 0;
    }

    int64_t metalen;
    off += bps_number(bps+off, len-12-off, &metalen);
    if (metalen<0 || metalen>(int64_t)len-12-off) {
        return 0;
    }
    info->metaoff = off;
    info->metalen = metalen;
    return 1;
}

// Apply the patch to the zero-initialized target buffer. The source and
// targets must match the sizes in bps_info. Returns non-zero if the patch
// failed. Includes checksum validation.
static enum bps_result bps_apply(uint8_t *bps, size_t len, uint8_t *src, uint8_t *tgt)
{
    // These offsets/lengths have already been validated
    int64_t bp=4, sp=0, tp=0, op=0, bn=len-12, sn, tn, r;
    bp += bps_number(bps+bp, bn-bp, &sn);
    bp += bps_number(bps+bp, bn-bp, &tn);
    bp += bps_number(bps+bp, bn-bp, &r);
    bp += r;  // skip metadata

    // First validate the source checksum
    if (crc32(0, src, sn) != u32le(bps+len-12)) {
        return BPS_SRCSUM;
    }

    while (bp < bn) {
        bp += bps_number(bps+bp, bn, &r);
        if (r < 0) {
            return BPS_RANGE;
        }

        int64_t n = (r>>2) + 1;
        switch (r&3) {
        case 0: // SourceRead
            if (n>tn-op || n>sn-op) {
                return BPS_RANGE;
            }
            memcpy(tgt+op, src+op, n);
            op += n;
            break;
        case 1: // TargetRead
            if (n>tn-op || n>bn-bp) {
                return BPS_RANGE;
            }
            memcpy(tgt+op, bps+bp, n);
            op += n;
            bp += n;
            break;
        case 2: // SourceCopy
            bp += bps_number(bps+bp, bn, &r);
            if (r<0 || r>>1>sn) {
                return BPS_RANGE;
            }
            sp += r&1 ? -(r>>1) : r>>1;
            if (sp<0 || n>sn-sp || n>tn-op) {
                return BPS_RANGE;
            }
            memcpy(tgt+op, src+sp, n);
            op += n;
            sp += n;
            break;
        case 3: // TargetCopy
            bp += bps_number(bps+bp, bn, &r);
            if (r<0 || r>>1>tn) {
                return BPS_RANGE;
            }
            tp += r&1 ? -(r>>1) : r>>1;
            if (tp<0 || n>tn-tp || n>tn-op) {
                return BPS_RANGE;
            }
            for (int64_t i = 0; i < (int64_t)n; i++) {
                tgt[(int64_t)op+i] = tgt[(int64_t)tp+i];
            }
            op += n;
            tp += n;
            break;
        }
    }
    return crc32(0, tgt, tn) == u32le(bps+len-8) ? BPS_OK : BPS_TGTSUM;
}

// BPS patch application using exact reference implementation
static bool apply_bps_patch(const char* rom_path, const char* patch_path, const char* output_path)
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
    
    // Get patch size
    fseek(patch_file, 0, SEEK_END);
    long patch_size = ftell(patch_file);
    fseek(patch_file, 0, SEEK_SET);
    
    // Read entire patch into memory
    uint8_t* patch_data = (uint8_t*)malloc(patch_size);
    if (!patch_data) {
        printf("ROM Patches: Could not allocate patch buffer\n");
        fclose(rom_file);
        fclose(patch_file);
        return false;
    }
    
    if (fread(patch_data, 1, patch_size, patch_file) != (size_t)patch_size) {
        printf("ROM Patches: Could not read patch data\n");
        free(patch_data);
        fclose(rom_file);
        fclose(patch_file);
        return false;
    }
    fclose(patch_file);
    
    report_progress(10, "BPS patch loaded");
    
    // Parse patch info using reference implementation
    struct bps_info info;
    if (!bps_info(patch_data, patch_size, &info)) {
        printf("ROM Patches: Invalid BPS patch\n");
        free(patch_data);
        fclose(rom_file);
        return false;
    }
    
    printf("ROM Patches: BPS source=%lld target=%lld metadata=%zu\n", 
           (long long)info.srclen, (long long)info.tgtlen, info.metalen);
    
    // Handle SNES header detection - be more flexible like rompatcher.js
    long effective_rom_size = rom_size;
    long rom_offset = 0;
    
    if (info.srclen != rom_size) {
        long size_diff = rom_size - info.srclen;
        printf("ROM Patches: ROM size difference: %ld bytes\n", size_diff);
        
        // Try to detect and skip common header sizes
        if (size_diff > 0 && size_diff <= 32768) {
            // Common header sizes: 512, 1024, or any small header
            // Just use the size difference as the header offset
            printf("ROM Patches: Detected %ld-byte header, skipping for patch\n", size_diff);
            rom_offset = size_diff;
            effective_rom_size = rom_size - size_diff;
        } else if (size_diff < 0 && size_diff >= -32768) {
            // ROM is smaller than expected - patch might expect a header
            printf("ROM Patches: ROM is %ld bytes smaller than expected\n", -size_diff);
            // Try without any offset adjustment
            effective_rom_size = rom_size;
            rom_offset = 0;
        } else {
            printf("ROM Patches: BPS source length mismatch: expected %lld, got %ld (diff: %ld)\n", 
                   (long long)info.srclen, rom_size, size_diff);
            free(patch_data);
            fclose(rom_file);
            return false;
        }
    }
    
    // Final check with effective size
    if (info.srclen != effective_rom_size) {
        printf("ROM Patches: BPS source length mismatch after header adjustment: expected %lld, got %ld\n", 
               (long long)info.srclen, effective_rom_size);
        printf("ROM Patches: Trying to apply patch anyway...\n");
        // Don't fail here - let the BPS checksum validation handle it
    }
    
    report_progress(20, "BPS info parsed");
    
    // Read ROM data (skip header if present)
    uint8_t* rom_data = (uint8_t*)malloc(effective_rom_size);
    if (!rom_data) {
        printf("ROM Patches: Could not allocate ROM buffer\n");
        free(patch_data);
        fclose(rom_file);
        return false;
    }
    
    if (rom_offset > 0) {
        fseek(rom_file, rom_offset, SEEK_SET);
    }
    
    if (fread(rom_data, 1, effective_rom_size, rom_file) != (size_t)effective_rom_size) {
        printf("ROM Patches: Could not read ROM data\n");
        free(patch_data);
        free(rom_data);
        fclose(rom_file);
        return false;
    }
    fclose(rom_file);
    
    // Allocate target buffer (zero-initialized as required by reference implementation)
    uint8_t* target_data = (uint8_t*)calloc(1, info.tgtlen);
    if (!target_data) {
        printf("ROM Patches: Could not allocate target buffer\n");
        free(patch_data);
        free(rom_data);
        return false;
    }
    
    report_progress(40, "BPS buffers allocated, applying patch");
    
    // Apply patch using reference implementation
    enum bps_result result = bps_apply(patch_data, patch_size, rom_data, target_data);
    
    free(patch_data);
    free(rom_data);
    
    switch (result) {
        case BPS_OK:
            printf("ROM Patches: BPS patch applied successfully\n");
            break;
        case BPS_SRCSUM:
            printf("ROM Patches: BPS source checksum failed - patch is for different ROM\n");
            free(target_data);
            return false;
        case BPS_TGTSUM:
            printf("ROM Patches: BPS target checksum failed - patch is corrupted\n");
            free(target_data);
            return false;
        case BPS_RANGE:
            printf("ROM Patches: BPS patch is invalid or corrupted\n");
            free(target_data);
            return false;
        default:
            printf("ROM Patches: BPS patch failed with unknown error\n");
            free(target_data);
            return false;
    }
    
    report_progress(80, "BPS patch applied, writing output");
    
    // Write the patched ROM
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        printf("ROM Patches: Could not create output file: %s\n", output_path);
        free(target_data);
        return false;
    }
    
    if (fwrite(target_data, 1, info.tgtlen, output_file) != (size_t)info.tgtlen) {
        printf("ROM Patches: Could not write output file\n");
        fclose(output_file);
        free(target_data);
        return false;
    }
    
    fclose(output_file);
    free(target_data);
    
    printf("ROM Patches: BPS patched ROM size: %lld bytes (original: %ld bytes)\n", 
           (long long)info.tgtlen, rom_size);
    
    report_progress(100, "BPS patch applied successfully");
    return true;
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
    
    // Allocate ROM buffer with extra space for potential expansion
    long max_rom_size = rom_size * 4; // Allow up to 4x expansion
    uint8_t* rom_data = (uint8_t*)malloc(max_rom_size);
    if (!rom_data) {
        printf("ROM Patches: Could not allocate ROM buffer\n");
        fclose(rom_file);
        fclose(patch_file);
        return false;
    }
    
    // Initialize the buffer with zeros for the expanded area
    memset(rom_data, 0, max_rom_size);
    
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
    
    // Track the final ROM size (may be larger than original)
    long final_rom_size = rom_size;
    
    // Apply patch records
    int progress = 30;
    while (1) {
        uint8_t record[3];
        if (fread(record, 1, 3, patch_file) != 3) break;
        
        // Check for EOF marker
        if (memcmp(record, "EOF", 3) == 0) {
            // Check if there's a truncation size after EOF
            uint8_t trunc_bytes[3];
            if (fread(trunc_bytes, 1, 3, patch_file) == 3) {
                uint32_t trunc_size = (trunc_bytes[0] << 16) | (trunc_bytes[1] << 8) | trunc_bytes[2];
                if (trunc_size > 0 && trunc_size < (uint32_t)max_rom_size) {
                    final_rom_size = trunc_size;
                    printf("ROM Patches: IPS truncation to %d bytes\n", trunc_size);
                }
            }
            report_progress(90, "IPS patching complete");
            break;
        }
        
        // Parse patch record - offset is in the 3 bytes we just read
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
            for (int i = 0; i < rle_size && offset + i < (uint32_t)max_rom_size; i++) {
                rom_data[offset + i] = rle_byte;
            }
            
            // Update final ROM size if this patch extends beyond original
            if (offset + rle_size > (uint32_t)final_rom_size) {
                final_rom_size = offset + rle_size;
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
            for (int i = 0; i < size && offset + i < (uint32_t)max_rom_size; i++) {
                rom_data[offset + i] = patch_data[i];
            }
            
            // Update final ROM size if this patch extends beyond original
            if (offset + size > (uint32_t)final_rom_size) {
                final_rom_size = offset + size;
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
    
    if (fwrite(rom_data, 1, final_rom_size, output_file) != (size_t)final_rom_size) {
        printf("ROM Patches: Could not write patched ROM\n");
        free(rom_data);
        fclose(output_file);
        return false;
    }
    
    printf("ROM Patches: Patched ROM size: %ld bytes (original: %ld bytes)\n", final_rom_size, rom_size);
    
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
            return apply_bps_patch(rom_path, patch_path, output_path);
            
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
    if (!rom_path || !patches || max_patches <= 0) {
        return 0;
    }
    
    // Calculate CRC32 of the ROM
    FILE* rom_file = fopen(rom_path, "rb");
    uint32_t rom_crc = 0;
    if (rom_file) {
        fseek(rom_file, 0, SEEK_END);
        long file_size = ftell(rom_file);
        fseek(rom_file, 0, SEEK_SET);
        
        if (file_size > 0 && file_size < 16*1024*1024) // Max 16MB for CRC calc
        {
            uint8_t* buffer = (uint8_t*)malloc(file_size);
            if (buffer && fread(buffer, 1, file_size, rom_file) == (size_t)file_size) {
                rom_crc = calculate_crc32(buffer, file_size);
                printf("ROM Patches: Calculated CRC32: %08X for %s\n", rom_crc, rom_path);
            }
            if (buffer) free(buffer);
        }
        fclose(rom_file);
    }
    
    // Extract game name from ROM path
    char game_name[256];
    const char* filename = strrchr(rom_path, '/');
    if (filename) {
        filename++;
    } else {
        filename = rom_path;
    }
    
    // Remove extension from game name
    strncpy(game_name, filename, sizeof(game_name) - 1);
    game_name[sizeof(game_name) - 1] = '\0';
    char* ext = strrchr(game_name, '.');
    if (ext) {
        *ext = '\0';
    }

    // Sanitize game name for directory path
    for (int i = 0; game_name[i] != '\0'; i++) {
        if (strchr("<>:\"/\\|?*", game_name[i])) {
            game_name[i] = '_';
        }
    }
    
    // Get core name
    const char* core_name = user_io_get_core_name();
    if (!core_name) {
        return 0;
    }
    
    // Build patch directory path: /media/fat/rom_patches/core/game [crc32]/
    char patch_dir[1024];
    snprintf(patch_dir, sizeof(patch_dir), "/media/fat/rom_patches/%s/%s [%08X]", core_name, game_name, rom_crc);
    
    printf("ROM Patches: Looking for patches in %s\n", patch_dir);
    
    // Check if patch directory exists
    struct stat st;
    if (stat(patch_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("ROM Patches: No patch directory found\n");
        return 0;
    }
    
    // Scan directory for patch files
    DIR* dir = opendir(patch_dir);
    if (!dir) {
        return 0;
    }
    
    int count = 0;
    struct dirent* entry;
    static patch_info_t patch_storage[32]; // Static storage for patch info
    
    while ((entry = readdir(dir)) != NULL && count < max_patches) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        
        // Check if it's a patch file
        if (!patches_is_patch_file(entry->d_name)) {
            continue;
        }
        
        // Fill patch info
        patch_info_t* patch = &patch_storage[count];
        snprintf(patch->filepath, sizeof(patch->filepath), "%s/%s", patch_dir, entry->d_name);
        
        // Extract patch name (remove extension)
        strncpy(patch->name, entry->d_name, sizeof(patch->name) - 1);
        patch->name[sizeof(patch->name) - 1] = '\0';
        char* patch_ext = strrchr(patch->name, '.');
        if (patch_ext) {
            *patch_ext = '\0';
        }
        
        patch->format = patches_detect_format(patch->filepath);
        
        // Get file size
        struct stat patch_st;
        if (stat(patch->filepath, &patch_st) == 0) {
            patch->size = patch_st.st_size;
        } else {
            patch->size = 0;
        }
        
        patch->validated = false;
        
        patches[count] = patch;
        count++;
        
        printf("ROM Patches: Found patch: %s\n", patch->name);
    }
    
    closedir(dir);
    
    // Sort patches alphabetically by name (case-insensitive)
    if (count > 1) {
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (strcasecmp(patches[i]->name, patches[j]->name) > 0) {
                    // Swap patches
                    patch_info_t* temp = patches[i];
                    patches[i] = patches[j];
                    patches[j] = temp;
                }
            }
        }
    }
    
    printf("ROM Patches: Found %d patches (sorted A-Z)\n", count);
    return count;
}

char* patches_find_original_rom(const char* patch_path)
{
    static char rom_path[1024];
    
    // Extract the patch folder path to find the corresponding ROM
    // Patch path format: /media/fat/rom_patches/core/game [crc32]/patchname.ips
    // We need to find the original ROM that matches this patch
    
    char patch_dir[1024];
    strncpy(patch_dir, patch_path, sizeof(patch_dir) - 1);
    patch_dir[sizeof(patch_dir) - 1] = '\0';
    
    // Get the directory containing the patch
    char* last_slash = strrchr(patch_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
    
    // Extract the game name and CRC from the directory name
    // Format: game[crc32]
    char* dir_name = strrchr(patch_dir, '/');
    if (dir_name) {
        dir_name++;
    } else {
        dir_name = patch_dir;
    }
    
    // Find the CRC in brackets (format: "game name [CRC32]")
    char* crc_start = strchr(dir_name, '[');
    char* crc_end = strchr(dir_name, ']');
    
    if (crc_start && crc_end && crc_end > crc_start) {
        // Extract game name (everything before the bracket)
        int game_name_len = crc_start - dir_name;
        // Remove trailing space if present
        if (game_name_len > 0 && dir_name[game_name_len - 1] == ' ') {
            game_name_len--;
        }
        char game_name[256];
        strncpy(game_name, dir_name, game_name_len);
        game_name[game_name_len] = '\0';
        
        // Extract CRC (everything between brackets)
        char crc_str[16];
        int crc_len = crc_end - crc_start - 1;
        strncpy(crc_str, crc_start + 1, crc_len);
        crc_str[crc_len] = '\0';
        
        // TODO: Search for ROM with matching CRC
        // For now, construct a likely path
        // This is a placeholder - should actually search for the ROM
        snprintf(rom_path, sizeof(rom_path), "games/%s/%s.rom", game_name, game_name);
        
        // Check if file exists
        struct stat st;
        if (stat(rom_path, &st) == 0) {
            return rom_path;
        }
    }
    
    return NULL;
}