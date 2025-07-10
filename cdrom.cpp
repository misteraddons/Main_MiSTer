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
#include <ctype.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>

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

bool cdrom_mount_device(const char* device_path)
{
    printf("CD-ROM: Attempting to mount device %s\n", device_path);
    
    // Check if device exists
    if (access(device_path, F_OK) != 0) {
        printf("CD-ROM: Device %s does not exist\n", device_path);
        return false;
    }
    
    // Check if mount script exists
    const char* mount_script = "/media/fat/Scripts/cdrom/cdrom_mount.sh";
    if (access(mount_script, X_OK) != 0) {
        printf("CD-ROM: Mount script not found or not executable: %s\n", mount_script);
        return false;
    }
    
    // Execute mount script
    char mount_cmd[512];
    snprintf(mount_cmd, sizeof(mount_cmd), "%s %s 2>/dev/null", mount_script, device_path);
    
    printf("CD-ROM: Running mount command: %s\n", mount_cmd);
    int result = ::system(mount_cmd);
    
    if (result == 0) {
        printf("CD-ROM: Device mounted successfully\n");
        return true;
    } else {
        printf("CD-ROM: Mount failed (exit code: %d)\n", result);
        return false;
    }
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
        else
        {
            // Device exists but not readable - try mounting
            printf("CD-ROM: Device %s exists but not readable, attempting mount\n", cdrom_device_path);
            if (cdrom_mount_device(cdrom_device_path))
            {
                // Try opening again after mount
                fd = open(cdrom_device_path, O_RDONLY | O_NONBLOCK);
                if (fd >= 0)
                {
                    close(fd);
                    return true;
                }
            }
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
            else
            {
                // Device exists but not readable - try mounting
                printf("CD-ROM: Device %s exists but not readable, attempting mount\n", potential_devices[i]);
                if (cdrom_mount_device(potential_devices[i]))
                {
                    // Try opening again after mount
                    fd = open(potential_devices[i], O_RDONLY | O_NONBLOCK);
                    if (fd >= 0)
                    {
                        close(fd);
                        strcpy(cdrom_device_path, potential_devices[i]);
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

bool cdrom_is_disc_inserted()
{
    if (!cdrom_drive_detected) {
        printf("CD-ROM: Drive not detected, cannot check for disc\n");
        return false;
    }
    
    printf("CD-ROM: Checking for disc insertion at %s\n", cdrom_device_path);
    
    // Try to open the device
    int fd = open(cdrom_device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("CD-ROM: Failed to open device %s: %s\n", cdrom_device_path, strerror(errno));
        return false;
    }
    
    // Force cache flush and ensure we read from physical disc
    printf("CD-ROM: Flushing drive cache...\n");
    ioctl(fd, CDROM_MEDIA_CHANGED);
    
    // First try to read from sector 0 (data disc)
    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));
    
    // Ensure we start from beginning of disc
    if (lseek(fd, 0, SEEK_SET) < 0) {
        printf("CD-ROM: Failed to seek to sector 0\n");
        close(fd);
        return false;
    }
    
    ssize_t result = read(fd, buffer, sizeof(buffer));
    
    printf("CD-ROM: Sector 0 read result: %d bytes\n", (int)result);
    
    if (result > 0) {
        printf("CD-ROM: Data disc detected at sector 0\n");
        close(fd);
        return true; // Data disc detected
    }
    
    printf("CD-ROM: Sector 0 failed, checking for audio CD using simple ioctl\n");
    
    // Try a simple disc status check
    int disc_status = ioctl(fd, CDROM_DISC_STATUS);
    printf("CD-ROM: Disc status ioctl result: %d\n", disc_status);
    
    // If ioctl succeeds with any positive result, there's likely a disc
    if (disc_status >= 0) {
        printf("CD-ROM: Disc detected via ioctl (status=%d)\n", disc_status);
        
        // For PC Engine CDs and other audio/mixed discs, try Table of Contents
        struct cdrom_tochdr toc_header;
        if (ioctl(fd, CDROMREADTOCHDR, &toc_header) == 0) {
            printf("CD-ROM: TOC read successful - first track: %d, last track: %d\n", 
                   toc_header.cdth_trk0, toc_header.cdth_trk1);
            
            // Any disc with a valid TOC is a real disc
            close(fd);
            return true;
        }
    }
    
    // Final fallback: if the device opened but no reads work, 
    // it might still be an audio disc - try one more basic check
    struct cdrom_tochdr toc_header;
    if (ioctl(fd, CDROMREADTOCHDR, &toc_header) == 0) {
        printf("CD-ROM: TOC fallback successful - disc detected\n");
        close(fd);
        return true;
    }
    
    printf("CD-ROM: All detection methods failed\n");
    close(fd);
    return false;
}

typedef struct {
    char system[32];
    char format[32];
    bool detected;
} DiscInfo;

bool detect_disc_format_and_system(const char* device_path, DiscInfo* disc_info)
{
    printf("CD-ROM: Analyzing disc format and system type\n");
    
    FILE* device = fopen(device_path, "rb");
    if (!device) {
        printf("CD-ROM: Failed to open device for analysis\n");
        return false;
    }
    
    // Check for ISO 9660 signature at sector 16
    if (fseek(device, 16 * 2048, SEEK_SET) == 0) {
        char buffer[6];
        if (fread(buffer, 1, 6, device) == 6) {
            buffer[5] = '\0';
            if (strncmp(buffer + 1, "CD001", 5) == 0) {
                strcpy(disc_info->format, "ISO9660");
                printf("CD-ROM: Detected ISO 9660 format\n");
            }
        }
    }
    
    fclose(device);
    
    // Try header-based magic word detection first (more reliable for Saturn/SegaCD)
    if (detect_saturn_magic_word(device_path)) {
        printf("CD-ROM: Detected Saturn system via magic word\n");
        strcpy(disc_info->system, "Saturn");
        disc_info->detected = true;
        return true;
    }
    
    if (detect_segacd_magic_word(device_path)) {
        printf("CD-ROM: Detected SegaCD system via magic word\n");
        strcpy(disc_info->system, "SegaCD");
        disc_info->detected = true;
        return true;
    }
    
    if (detect_pcecd_magic_word(device_path)) {
        printf("CD-ROM: Detected PCE-CD system via magic word\n");
        strcpy(disc_info->system, "PCECD");
        disc_info->detected = true;
        return true;
    }
    
    if (detect_neogeocd_magic_word(device_path)) {
        printf("CD-ROM: Detected Neo Geo CD system via magic word\n");
        strcpy(disc_info->system, "NeoGeoCD");
        disc_info->detected = true;
        return true;
    }
    
    // If magic word detection fails, try filesystem-based detection
    // Mount the disc to analyze filesystem structure
    char mount_cmd[256];
    char temp_mount_point[] = "/tmp/cdrom_mount";
    
    ::system("mkdir -p /tmp/cdrom_mount");
    snprintf(mount_cmd, sizeof(mount_cmd), "mount -t iso9660 -o ro %s %s 2>/dev/null", device_path, temp_mount_point);
    
    if (::system(mount_cmd) != 0) {
        printf("CD-ROM: Failed to mount disc for filesystem analysis\n");
        return false;
    }
    
    printf("CD-ROM: Mounted disc, analyzing filesystem structure\n");
    
    // List contents of disc for debugging
    char ls_cmd[256];
    snprintf(ls_cmd, sizeof(ls_cmd), "ls -la %s", temp_mount_point);
    printf("CD-ROM: Disc contents:\n");
    ::system(ls_cmd);
    
    // Check for PlayStation indicators (both upper and lowercase)
    char psx_indicators[][32] = {
        "SYSTEM.CNF",
        "system.cnf",
        "PSX.EXE", 
        "psx.exe",
        "SCUS_",
        "scus_",
        "SLUS_",
        "slus_",
        "SCES_",
        "sces_",
        "SLED_",
        "sled_"
    };
    
    // Check for Saturn indicators  
    char saturn_indicators[][32] = {
        "0.BIN",
        "ABS.TXT",
        "BIB.TXT",
        "CPY.TXT"
    };
    
    // Check for Sega CD indicators
    char segacd_indicators[][32] = {
        "_BOOT",
        "FILESYSTEM.BIN",
        "IP.BIN"
    };
    
    // Check for Neo Geo CD indicators
    char neogeocd_indicators[][32] = {
        "NEO-GEO.CDZ",
        "NEO-GEO.CD",
        "IPL.TXT",
        "PRG",
        "FIX",
        "SPR",
        "PCM",
        "PAT"
    };
    
    bool is_psx = false, is_saturn = false, is_segacd = false, is_neogeocd = false;
    
    // Check PlayStation indicators
    for (int i = 0; i < sizeof(psx_indicators)/sizeof(psx_indicators[0]); i++) {
        char check_path[512];
        snprintf(check_path, sizeof(check_path), "%s/%s", temp_mount_point, psx_indicators[i]);
        if (access(check_path, F_OK) == 0) {
            printf("CD-ROM: Found PlayStation indicator: %s\n", psx_indicators[i]);
            is_psx = true;
            break;
        }
    }
    
    // Check Saturn indicators
    if (!is_psx) {
        for (int i = 0; i < sizeof(saturn_indicators)/sizeof(saturn_indicators[0]); i++) {
            char check_path[512];
            snprintf(check_path, sizeof(check_path), "%s/%s", temp_mount_point, saturn_indicators[i]);
            if (access(check_path, F_OK) == 0) {
                printf("CD-ROM: Found Saturn indicator: %s\n", saturn_indicators[i]);
                is_saturn = true;
                break;
            }
        }
    }
    
    // Check Sega CD indicators
    if (!is_psx && !is_saturn) {
        for (int i = 0; i < sizeof(segacd_indicators)/sizeof(segacd_indicators[0]); i++) {
            char check_path[512];
            snprintf(check_path, sizeof(check_path), "%s/%s", temp_mount_point, segacd_indicators[i]);
            if (access(check_path, F_OK) == 0) {
                printf("CD-ROM: Found Sega CD indicator: %s\n", segacd_indicators[i]);
                is_segacd = true;
                break;
            }
        }
    }
    
    // Check Neo Geo CD indicators
    if (!is_psx && !is_saturn && !is_segacd) {
        for (int i = 0; i < sizeof(neogeocd_indicators)/sizeof(neogeocd_indicators[0]); i++) {
            char check_path[512];
            snprintf(check_path, sizeof(check_path), "%s/%s", temp_mount_point, neogeocd_indicators[i]);
            if (access(check_path, F_OK) == 0) {
                printf("CD-ROM: Found Neo Geo CD indicator: %s\n", neogeocd_indicators[i]);
                is_neogeocd = true;
                break;
            }
        }
    }
    
    // Determine system
    if (is_psx) {
        strcpy(disc_info->system, "PSX");
        disc_info->detected = true;
    } else if (is_saturn) {
        strcpy(disc_info->system, "Saturn");
        disc_info->detected = true;
    } else if (is_segacd) {
        strcpy(disc_info->system, "SegaCD");
        disc_info->detected = true;
    } else if (is_neogeocd) {
        strcpy(disc_info->system, "NeoGeoCD");
        disc_info->detected = true;
    } else {
        strcpy(disc_info->system, "Unknown");
        disc_info->detected = false;
    }
    
    // Unmount
    snprintf(mount_cmd, sizeof(mount_cmd), "umount %s 2>/dev/null", temp_mount_point);
    ::system(mount_cmd);
    ::system("rmdir /tmp/cdrom_mount 2>/dev/null");
    
    printf("CD-ROM: Detected system: %s, format: %s\n", disc_info->system, disc_info->format);
    return disc_info->detected;
}

bool detect_saturn_magic_word(const char* device_path)
{
    printf("CD-ROM: Checking for Saturn magic word\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    // Read first 256 bytes where Saturn magic word should be
    char header[256];
    if (read(fd, header, sizeof(header)) != sizeof(header)) {
        close(fd);
        return false;
    }
    
    // Saturn magic word: "SEGA SEGASATURN " (15 bytes)
    const char saturn_magic[] = "SEGA SEGASATURN ";
    
    // Search for magic word in first 256 bytes
    for (int i = 0; i <= (int)(sizeof(header) - sizeof(saturn_magic) + 1); i++) {
        if (memcmp(&header[i], saturn_magic, sizeof(saturn_magic) - 1) == 0) {
            printf("CD-ROM: Found Saturn magic word at offset %d\n", i);
            close(fd);
            return true;
        }
    }
    
    close(fd);
    return false;
}

bool detect_segacd_magic_word(const char* device_path)
{
    printf("CD-ROM: Checking for Sega CD magic word\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    // Read first 256 bytes where Sega CD magic words should be
    char header[256];
    if (read(fd, header, sizeof(header)) != sizeof(header)) {
        close(fd);
        return false;
    }
    
    // Common Sega CD magic words
    const char* segacd_magic_words[] = {
        "SEGADISCSYSTEM",
        "SEGA_CD_",
        "SEGA CD"
    };
    
    // Search for any magic word in first 256 bytes
    for (int magic = 0; magic < 3; magic++) {
        const char* magic_word = segacd_magic_words[magic];
        size_t magic_len = strlen(magic_word);
        
        for (int i = 0; i <= (int)(sizeof(header) - magic_len); i++) {
            if (memcmp(&header[i], magic_word, magic_len) == 0) {
                printf("CD-ROM: Found Sega CD magic word '%s' at offset %d\n", magic_word, i);
                close(fd);
                return true;
            }
        }
    }
    
    close(fd);
    return false;
}

bool detect_pcecd_magic_word(const char* device_path)
{
    printf("CD-ROM: Checking for PC Engine CD magic word\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    // For PC Engine CDs, use track structure detection since raw reads fail
    // PC Engine CDs typically have 10+ tracks with track 1 being audio
    struct cdrom_tochdr toc_header;
    if (ioctl(fd, CDROMREADTOCHDR, &toc_header) == 0) {
        int num_tracks = toc_header.cdth_trk1 - toc_header.cdth_trk0 + 1;
        printf("CD-ROM: TOC shows %d tracks (first=%d, last=%d)\n", 
               num_tracks, toc_header.cdth_trk0, toc_header.cdth_trk1);
        
        // Display complete TOC information
        printf("CD-ROM: Complete TOC Analysis:\n");
        printf("CD-ROM: Track | Type  | Start LBA | Length (MM:SS:FF)\n");
        printf("CD-ROM: ------|-------|-----------|------------------\n");
        
        struct cdrom_tocentry toc_entry;
        bool has_audio = false;
        bool has_data = false;
        
        // Read all tracks
        for (int track = toc_header.cdth_trk0; track <= toc_header.cdth_trk1; track++) {
            toc_entry.cdte_track = track;
            toc_entry.cdte_format = CDROM_LBA;
            
            if (ioctl(fd, CDROMREADTOCENTRY, &toc_entry) == 0) {
                const char* track_type;
                if (toc_entry.cdte_ctrl & CDROM_DATA_TRACK) {
                    has_data = true;
                    track_type = "DATA ";
                } else {
                    has_audio = true;
                    track_type = "AUDIO";
                }
                
                // Get track start position
                int lba = toc_entry.cdte_addr.lba;
                
                // Convert LBA to MM:SS:FF format (75 frames per second, 60 seconds per minute)
                int frames = lba % 75;
                int seconds = (lba / 75) % 60;
                int minutes = (lba / 75) / 60;
                
                printf("CD-ROM: %5d | %s | %9d | %02d:%02d:%02d\n", 
                       track, track_type, lba, minutes, seconds, frames);
            }
        }
        
        // Also show lead-out track (end of disc)
        toc_entry.cdte_track = CDROM_LEADOUT;
        toc_entry.cdte_format = CDROM_LBA;
        if (ioctl(fd, CDROMREADTOCENTRY, &toc_entry) == 0) {
            int lba = toc_entry.cdte_addr.lba;
            int frames = lba % 75;
            int seconds = (lba / 75) % 60;
            int minutes = (lba / 75) / 60;
            
            printf("CD-ROM: Lead-out        | %9d | %02d:%02d:%02d\n", 
                   lba, minutes, seconds, frames);
        }
        
        // PC Engine CDs typically have multiple tracks (usually 10+)
        // and start with track 1 (audio warning track)
        if (num_tracks >= 5 && toc_header.cdth_trk0 == 1) {
            
            // PC Engine CD pattern: audio track 1 + data tracks + multiple tracks
            if (has_audio && has_data && num_tracks >= 5) {
                printf("CD-ROM: Mixed audio/data disc with %d tracks - likely PC Engine CD\n", num_tracks);
                close(fd);
                return true;
            }
            
            // Even if we can't determine track types, multiple tracks starting at 1 
            // with an audio CD structure could be PC Engine
            if (num_tracks >= 8) {
                printf("CD-ROM: Multi-track audio disc (%d tracks) - possibly PC Engine CD\n", num_tracks);
                close(fd);
                return true;
            }
        }
    }
    
    close(fd);
    return false;
}

bool detect_neogeocd_magic_word(const char* device_path)
{
    printf("CD-ROM: Checking for Neo Geo CD magic word\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    
    // Neo Geo CD has a unique structure - try to read the first data sector
    // Neo Geo CD structure: Track 1 (audio), Track 2+ (data)
    char buffer[2048];
    
    // Try to read sector 0 first
    if (read(fd, buffer, sizeof(buffer)) == sizeof(buffer)) {
        // Look for Neo Geo CD magic words in the first sector
        const char* neogeo_magic_words[] = {
            "NEO-GEO",
            "NEOGEO",
            "SNK",
            "COPYRIGHT SNK",
            "SYSTEM ROM",
            "BIOS"
        };
        
        for (int i = 0; i < 6; i++) {
            if (strstr(buffer, neogeo_magic_words[i]) != NULL) {
                printf("CD-ROM: Found Neo Geo CD magic word '%s' in sector 0\n", neogeo_magic_words[i]);
                close(fd);
                return true;
            }
        }
    }
    
    // Try reading from sector 16 (ISO 9660 Primary Volume Descriptor)
    if (lseek(fd, 16 * 2048, SEEK_SET) >= 0) {
        if (read(fd, buffer, sizeof(buffer)) == sizeof(buffer)) {
            // Check for Neo Geo CD identifiers in the volume descriptor
            if (strstr(buffer, "NEO-GEO") != NULL || 
                strstr(buffer, "NEOGEO") != NULL ||
                strstr(buffer, "SNK") != NULL) {
                printf("CD-ROM: Found Neo Geo CD identifier in volume descriptor\n");
                close(fd);
                return true;
            }
        }
    }
    
    // Check Table of Contents for Neo Geo CD signature
    // Neo Geo CD typically has a specific track structure
    struct cdrom_tochdr toc_header;
    if (ioctl(fd, CDROMREADTOCHDR, &toc_header) == 0) {
        int num_tracks = toc_header.cdth_trk1 - toc_header.cdth_trk0 + 1;
        printf("CD-ROM: TOC shows %d tracks (first=%d, last=%d)\n", 
               num_tracks, toc_header.cdth_trk0, toc_header.cdth_trk1);
        
        // Neo Geo CD typically has 2-4 tracks
        if (num_tracks >= 2 && num_tracks <= 4) {
            struct cdrom_tocentry toc_entry;
            bool has_audio_track1 = false;
            bool has_data_track2 = false;
            
            // Check track 1 (should be audio)
            toc_entry.cdte_track = 1;
            toc_entry.cdte_format = CDROM_LBA;
            if (ioctl(fd, CDROMREADTOCENTRY, &toc_entry) == 0) {
                if (!(toc_entry.cdte_ctrl & CDROM_DATA_TRACK)) {
                    has_audio_track1 = true;
                }
            }
            
            // Check track 2 (should be data)
            toc_entry.cdte_track = 2;
            toc_entry.cdte_format = CDROM_LBA;
            if (ioctl(fd, CDROMREADTOCENTRY, &toc_entry) == 0) {
                if (toc_entry.cdte_ctrl & CDROM_DATA_TRACK) {
                    has_data_track2 = true;
                }
            }
            
            // Neo Geo CD pattern: audio track 1 + data track 2 + low track count
            if (has_audio_track1 && has_data_track2 && num_tracks <= 4) {
                printf("CD-ROM: Audio+Data structure with %d tracks - likely Neo Geo CD\n", num_tracks);
                close(fd);
                return true;
            }
        }
    }
    
    close(fd);
    return false;
}

bool extract_psx_disc_id_from_header(const char* device_path, char* disc_id, size_t disc_id_size)
{
    printf("CD-ROM: Attempting hex-based PSX disc ID extraction\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        printf("CD-ROM: Failed to open device for hex reading\n");
        return false;
    }
    
    // Read sector 16 (0x10) where ISO 9660 Primary Volume Descriptor is located
    // PlayStation game IDs are often found in specific offsets within this area
    char buffer[2048];
    if (lseek(fd, 16 * 2048, SEEK_SET) < 0) {
        printf("CD-ROM: Failed to seek to sector 16\n");
        close(fd);
        return false;
    }
    
    if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
        printf("CD-ROM: Failed to read sector 16\n");
        close(fd);
        return false;
    }
    
    // Look for PlayStation-specific patterns in the hex data
    // PlayStation discs often have the game ID at specific offsets
    // Common locations: offset 0x28, 0x50, or within the volume identifier
    
    // Check for SLUS, SCUS, SCES, SLED patterns in the hex data
    const char* id_patterns[] = {"SLUS", "SCUS", "SCES", "SLED"};
    
    for (int pattern = 0; pattern < 4; pattern++) {
        for (int i = 0; i < (int)(sizeof(buffer) - 10); i++) {
            if (strncmp(&buffer[i], id_patterns[pattern], 4) == 0) {
                // Found potential game ID, extract it
                char temp_id[16] = "";
                int pos = 0;
                
                // Copy the 4-letter prefix
                strncpy(temp_id, id_patterns[pattern], 4);
                temp_id[4] = '-';
                pos = 5;
                
                // Look for numbers after the prefix (may have underscore or space)
                int src = i + 4;
                if (buffer[src] == '_' || buffer[src] == ' ') src++;
                
                // Extract digits
                for (int j = 0; j < 5 && src < (int)sizeof(buffer) && pos < 10; j++, src++) {
                    if (buffer[src] >= '0' && buffer[src] <= '9') {
                        temp_id[pos++] = buffer[src];
                    } else if (buffer[src] == '.' && j == 3) {
                        // Skip the dot in SLUS_123.45 format
                        continue;
                    } else if (j < 3) {
                        // If we don't have enough digits, this isn't a valid ID
                        break;
                    }
                }
                
                temp_id[pos] = '\0';
                
                if (pos >= 9) { // Should be at least SLUS-12345
                    strncpy(disc_id, temp_id, disc_id_size - 1);
                    disc_id[disc_id_size - 1] = '\0';
                    printf("CD-ROM: Found PSX game ID in hex data: %s\n", disc_id);
                    close(fd);
                    return true;
                }
            }
        }
    }
    
    close(fd);
    printf("CD-ROM: No PSX game ID found in hex data\n");
    return false;
}

bool extract_saturn_disc_id(const char* device_path, char* disc_id, size_t disc_id_size)
{
    printf("CD-ROM: Attempting Saturn disc ID extraction\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        printf("CD-ROM: Failed to open device for Saturn ID extraction\n");
        return false;
    }
    
    // Read first 256 bytes where Saturn header is located
    char header[256];
    if (read(fd, header, sizeof(header)) != sizeof(header)) {
        printf("CD-ROM: Failed to read Saturn header\n");
        close(fd);
        return false;
    }
    
    // Find Saturn magic word first
    const char saturn_magic[] = "SEGA SEGASATURN ";
    char* magic_pos = NULL;
    
    for (int i = 0; i <= (int)(sizeof(header) - sizeof(saturn_magic) + 1); i++) {
        if (memcmp(&header[i], saturn_magic, sizeof(saturn_magic) - 1) == 0) {
            magic_pos = &header[i];
            break;
        }
    }
    
    if (!magic_pos) {
        printf("CD-ROM: Saturn magic word not found\n");
        close(fd);
        return false;
    }
    
    // Saturn game ID is typically found at offset 0x20 from start of header
    // Format is usually manufacturer ID + product code
    char* id_start = header + 0x20;
    
    // Debug: Show raw data at ID location
    printf("CD-ROM: Raw data at offset 0x20: ");
    for (int i = 0; i < 16; i++) {
        if (id_start + i < header + sizeof(header)) {
            printf("%02X ", (unsigned char)(id_start[i]));
        }
    }
    printf("\n");
    printf("CD-ROM: ASCII at offset 0x20: ");
    for (int i = 0; i < 16; i++) {
        if (id_start + i < header + sizeof(header)) {
            char c = id_start[i];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
    }
    printf("\n");
    
    // Extract product code (usually 8-10 characters)
    char temp_id[32] = "";
    int pos = 0;
    
    // Skip any leading spaces or null bytes
    while (id_start < header + sizeof(header) && (*id_start == ' ' || *id_start == '\0')) {
        id_start++;
    }
    
    // Extract alphanumeric characters for the game ID
    for (int i = 0; i < 20 && (id_start + i) < (header + sizeof(header)); i++) {
        char c = id_start[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') {
            temp_id[pos++] = c;
        } else if (c == ' ' && pos > 0) {
            break; // Stop at first space after we have some characters
        }
    }
    
    temp_id[pos] = '\0';
    
    if (pos >= 4) { // Should have at least 4 characters
        strncpy(disc_id, temp_id, disc_id_size - 1);
        disc_id[disc_id_size - 1] = '\0';
        printf("CD-ROM: Extracted Saturn ID: %s\n", disc_id);
        close(fd);
        return true;
    } else {
        printf("CD-ROM: Could not extract valid Saturn ID\n");
        close(fd);
        return false;
    }
}

bool extract_segacd_disc_id(const char* device_path, char* disc_id, size_t disc_id_size)
{
    printf("CD-ROM: Attempting SegaCD disc ID extraction\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        printf("CD-ROM: Failed to open device for SegaCD ID extraction\n");
        return false;
    }
    
    // Read first 768 bytes to ensure we get the full SegaCD header (ID is at offset 0x180)
    char header[768];
    if (read(fd, header, sizeof(header)) != sizeof(header)) {
        printf("CD-ROM: Failed to read SegaCD header\n");
        close(fd);
        return false;
    }
    
    // Find SegaCD magic word first
    const char* segacd_magic_words[] = {
        "SEGADISCSYSTEM",
        "SEGA_CD_",
        "SEGA CD"
    };
    
    char* magic_pos = NULL;
    for (int magic = 0; magic < 3; magic++) {
        const char* magic_word = segacd_magic_words[magic];
        size_t magic_len = strlen(magic_word);
        
        for (int i = 0; i <= (int)(sizeof(header) - magic_len); i++) {
            if (memcmp(&header[i], magic_word, magic_len) == 0) {
                magic_pos = &header[i];
                break;
            }
        }
        if (magic_pos) break;
    }
    
    if (!magic_pos) {
        printf("CD-ROM: SegaCD magic word not found\n");
        close(fd);
        return false;
    }
    
    // Use GameID approach: extract ID from magic_word + 0x180
    int magic_word_offset = -1;
    for (int i = 0; i <= (int)(sizeof(header) - strlen("SEGADISCSYSTEM")); i++) {
        if (memcmp(&header[i], "SEGADISCSYSTEM", 14) == 0) {
            magic_word_offset = i;
            break;
        }
    }
    
    if (magic_word_offset == -1) {
        printf("CD-ROM: Could not find SEGADISCSYSTEM magic word in header\n");
        close(fd);
        return false;
    }
    
    // Extract ID from magic_word + 0x180 (GameID approach)
    int id_offset = magic_word_offset + 0x180;
    if (id_offset + 16 > (int)sizeof(header)) {
        printf("CD-ROM: ID offset 0x%x beyond header size\n", id_offset);
        close(fd);
        return false;
    }
    
    // Extract 16 bytes from ID location and clean it up
    char raw_id[17];
    memcpy(raw_id, &header[id_offset], 16);
    raw_id[16] = '\0';
    
    // Debug: show raw ID bytes
    printf("CD-ROM: Raw ID bytes at offset 0x%x: ", id_offset);
    for (int i = 0; i < 16; i++) {
        printf("%02x ", (unsigned char)raw_id[i]);
    }
    printf("| ");
    for (int i = 0; i < 16; i++) {
        char c = raw_id[i];
        printf("%c", (c >= 32 && c <= 126) ? c : '.');
    }
    printf("\n");
    
    // Clean up the ID: keep spaces and hyphens, remove nulls and non-printable chars
    char temp_id[32] = "";
    int pos = 0;
    for (int i = 0; i < 16; i++) {
        char c = raw_id[i];
        if (c >= 32 && c <= 126) { // printable chars including spaces
            temp_id[pos++] = c;
        } else if (c == '\0') {
            break; // stop at null terminator
        }
    }
    temp_id[pos] = '\0';
    
    // Trim trailing spaces
    while (pos > 0 && temp_id[pos-1] == ' ') {
        temp_id[--pos] = '\0';
    }
    
    if (strlen(temp_id) >= 4) {
        strncpy(disc_id, temp_id, disc_id_size - 1);
        disc_id[disc_id_size - 1] = '\0';
        printf("CD-ROM: Extracted SegaCD ID: %s (GameID method at offset 0x%x)\n", disc_id, id_offset);
        close(fd);
        return true;
    }
    
    printf("CD-ROM: Could not extract valid SegaCD ID\n");
    close(fd);
    return false;
}

bool extract_pcecd_disc_id(const char* device_path, char* disc_id, size_t disc_id_size)
{
    printf("CD-ROM: Attempting PCE-CD disc ID extraction using MD5 hash method\n");
    
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        printf("CD-ROM: Failed to open device for PCE-CD ID extraction\n");
        return false;
    }
    
    // PC Engine CDs don't use ISO 9660 - they use IPL headers and direct sector addressing
    // We'll identify games using MD5 hash of first 8KB of the first data track
    
    // Find the first data track (usually track 2)
    struct cdrom_tochdr toc_header;
    if (ioctl(fd, CDROMREADTOCHDR, &toc_header) != 0) {
        printf("CD-ROM: Failed to read TOC for PCE-CD ID extraction\n");
        close(fd);
        return false;
    }
    
    int data_track_lba = -1;
    for (int track = toc_header.cdth_trk0; track <= toc_header.cdth_trk1; track++) {
        struct cdrom_tocentry toc_entry;
        toc_entry.cdte_track = track;
        toc_entry.cdte_format = CDROM_LBA;
        
        if (ioctl(fd, CDROMREADTOCENTRY, &toc_entry) == 0) {
            if (toc_entry.cdte_ctrl & CDROM_DATA_TRACK) {
                data_track_lba = toc_entry.cdte_addr.lba;
                printf("CD-ROM: Found first data track %d at LBA %d\n", track, data_track_lba);
                break;
            }
        }
    }
    
    if (data_track_lba < 0) {
        printf("CD-ROM: No data track found for PCE-CD\n");
        close(fd);
        return false;
    }
    
    // Read first 4 sectors (8KB) for MD5 calculation
    char data_buffer[8192];
    int bytes_read = 0;
    
    for (int sector = 0; sector < 4; sector++) {
        struct cdrom_read_audio audio_read;
        char raw_buffer[2352];
        
        audio_read.addr.lba = data_track_lba + sector;
        audio_read.addr_format = CDROM_LBA;
        audio_read.nframes = 1;
        audio_read.buf = (unsigned char*)raw_buffer;
        
        if (ioctl(fd, CDROMREADRAW, &audio_read) == 0) {
            // Copy data portion (skip sync/header)
            memcpy(data_buffer + bytes_read, raw_buffer + 16, 2048);
            bytes_read += 2048;
        } else {
            printf("CD-ROM: Failed to read sector %d for MD5 calculation\n", sector);
            break;
        }
    }
    
    close(fd);
    
    if (bytes_read < 8192) {
        printf("CD-ROM: Could not read enough data for PCE-CD identification\n");
        return false;
    }
    
    // Calculate MD5 hash using system command (simple approach)
    char temp_file[] = "/tmp/pcecd_hash_data";
    FILE* temp = fopen(temp_file, "wb");
    if (temp) {
        fwrite(data_buffer, 1, 8192, temp);
        fclose(temp);
        
        char md5_cmd[256];
        snprintf(md5_cmd, sizeof(md5_cmd), "md5sum %s | cut -d' ' -f1", temp_file);
        
        FILE* md5_pipe = popen(md5_cmd, "r");
        if (md5_pipe) {
            char md5_hash[64] = "";
            if (fgets(md5_hash, sizeof(md5_hash), md5_pipe)) {
                // Remove newline
                for (int i = 0; md5_hash[i]; i++) {
                    if (md5_hash[i] == '\n') md5_hash[i] = '\0';
                }
                
                strncpy(disc_id, md5_hash, disc_id_size - 1);
                disc_id[disc_id_size - 1] = '\0';
                
                printf("CD-ROM: Extracted PCE-CD ID (MD5): %s\n", disc_id);
                pclose(md5_pipe);
                unlink(temp_file);
                return true;
            }
            pclose(md5_pipe);
        }
        unlink(temp_file);
    }
    
    printf("CD-ROM: Failed to calculate MD5 hash for PCE-CD identification\n");
    return false;
}

bool extract_psx_disc_id(const char* device_path, char* disc_id, size_t disc_id_size)
{
    // Try hex-based extraction first (more reliable)
    if (extract_psx_disc_id_from_header(device_path, disc_id, disc_id_size)) {
        return true;
    }
    
    printf("CD-ROM: Falling back to system.cnf method\n");
    
    char mount_cmd[256];
    char temp_mount_point[] = "/tmp/cdrom_mount";
    
    ::system("mkdir -p /tmp/cdrom_mount");
    snprintf(mount_cmd, sizeof(mount_cmd), "mount -t iso9660 -o ro %s %s 2>/dev/null", device_path, temp_mount_point);
    
    if (::system(mount_cmd) != 0) {
        return false;
    }
    
    bool found_id = false;
    
    // Try SYSTEM.CNF first (uppercase), then system.cnf (lowercase)
    char system_cnf_path[512];
    snprintf(system_cnf_path, sizeof(system_cnf_path), "%s/SYSTEM.CNF", temp_mount_point);
    
    FILE* system_cnf = fopen(system_cnf_path, "r");
    if (!system_cnf) {
        // Try lowercase version
        snprintf(system_cnf_path, sizeof(system_cnf_path), "%s/system.cnf", temp_mount_point);
        system_cnf = fopen(system_cnf_path, "r");
    }
    
    if (system_cnf) {
        printf("CD-ROM: Parsing SYSTEM.CNF for PlayStation game ID\n");
        
        char line[256];
        while (fgets(line, sizeof(line), system_cnf)) {
            if (strstr(line, "BOOT") && strstr(line, "cdrom:")) {
                char* cdrom_pos = strstr(line, "cdrom:");
                if (cdrom_pos) {
                    cdrom_pos += 6;
                    while (*cdrom_pos == '\\' || *cdrom_pos == '/') cdrom_pos++;
                    
                    char filename[32] = "";
                    int i = 0;
                    while (cdrom_pos[i] && cdrom_pos[i] != ';' && cdrom_pos[i] != '\n' && i < 31) {
                        filename[i] = cdrom_pos[i];
                        i++;
                    }
                    filename[i] = '\0';
                    
                    printf("CD-ROM: Found executable: %s\n", filename);
                    
                    // Convert SLUS_123.45 to SLUS-12345
                    char* underscore = strchr(filename, '_');
                    if (underscore && strlen(filename) >= 8) {
                        char temp_id[16] = "";
                        strncpy(temp_id, filename, 4);
                        temp_id[4] = '-';
                        
                        char* num_start = underscore + 1;
                        int pos = 5;
                        for (int j = 0; num_start[j] && pos < 10; j++) {
                            if (num_start[j] >= '0' && num_start[j] <= '9') {
                                temp_id[pos++] = num_start[j];
                            }
                        }
                        temp_id[pos] = '\0';
                        
                        if (strlen(temp_id) >= 9) {
                            strncpy(disc_id, temp_id, disc_id_size - 1);
                            disc_id[disc_id_size - 1] = '\0';
                            printf("CD-ROM: Extracted PlayStation ID: %s\n", disc_id);
                            found_id = true;
                            break;
                        }
                    }
                }
            }
        }
        fclose(system_cnf);
    }
    
    // Unmount
    snprintf(mount_cmd, sizeof(mount_cmd), "umount %s 2>/dev/null", temp_mount_point);
    ::system(mount_cmd);
    ::system("rmdir /tmp/cdrom_mount 2>/dev/null");
    
    return found_id;
}

bool extract_neogeocd_disc_id(const char* device_path, char* disc_id, size_t disc_id_size)
{
    printf("CD-ROM: Attempting Neo Geo CD disc ID extraction\n");
    
    // Neo Geo CD identification based on GameID.py identify_neogeocd function
    // Uses UUID (volume serial) and Volume ID for identification
    
    // Mount the disc to read volume information
    char mount_cmd[256];
    char temp_mount_point[] = "/tmp/cdrom_mount";
    
    ::system("mkdir -p /tmp/cdrom_mount");
    snprintf(mount_cmd, sizeof(mount_cmd), "mount -t iso9660 -o ro %s %s 2>/dev/null", device_path, temp_mount_point);
    
    if (::system(mount_cmd) != 0) {
        printf("CD-ROM: Failed to mount Neo Geo CD for ID extraction\n");
        return false;
    }
    
    printf("CD-ROM: Mounted Neo Geo CD, extracting volume information\n");
    
    // Try to read volume ID from the mounted filesystem
    // First, try to get volume ID from the ISO 9660 volume descriptor
    int fd = open(device_path, O_RDONLY);
    if (fd >= 0) {
        char buffer[2048];
        
        // Read Primary Volume Descriptor at sector 16
        if (lseek(fd, 16 * 2048, SEEK_SET) >= 0) {
            if (read(fd, buffer, sizeof(buffer)) == sizeof(buffer)) {
                // Volume ID is at offset 40, 32 bytes long
                if (strncmp(buffer + 1, "CD001", 5) == 0) {
                    char volume_id[33];
                    strncpy(volume_id, buffer + 40, 32);
                    volume_id[32] = '\0';
                    
                    // Trim trailing spaces
                    for (int i = 31; i >= 0; i--) {
                        if (volume_id[i] == ' ') {
                            volume_id[i] = '\0';
                        } else {
                            break;
                        }
                    }
                    
                    printf("CD-ROM: Neo Geo CD Volume ID: '%s'\n", volume_id);
                    
                    // For Neo Geo CD, we'll use the volume ID as the disc ID
                    if (strlen(volume_id) > 0) {
                        strncpy(disc_id, volume_id, disc_id_size - 1);
                        disc_id[disc_id_size - 1] = '\0';
                        
                        close(fd);
                        
                        // Unmount
                        snprintf(mount_cmd, sizeof(mount_cmd), "umount %s 2>/dev/null", temp_mount_point);
                        ::system(mount_cmd);
                        ::system("rmdir /tmp/cdrom_mount 2>/dev/null");
                        
                        return true;
                    }
                }
            }
        }
        close(fd);
    }
    
    // Fallback: Look for specific Neo Geo CD files to identify the game
    char check_paths[][64] = {
        "NEO-GEO.CDZ",
        "NEO-GEO.CD", 
        "IPL.TXT",
        "TITLE.TXT",
        "PRG",
        "FIX",
        "SPR"
    };
    
    bool found_neogeo_files = false;
    
    for (int i = 0; i < 7; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", temp_mount_point, check_paths[i]);
        
        if (access(full_path, F_OK) == 0) {
            printf("CD-ROM: Found Neo Geo CD file: %s\n", check_paths[i]);
            found_neogeo_files = true;
            
            // Try to read the file for identification
            if (strcmp(check_paths[i], "IPL.TXT") == 0 || strcmp(check_paths[i], "TITLE.TXT") == 0) {
                FILE* f = fopen(full_path, "r");
                if (f) {
                    char line[256];
                    if (fgets(line, sizeof(line), f)) {
                        // Remove newline
                        char* nl = strchr(line, '\n');
                        if (nl) *nl = '\0';
                        
                        if (strlen(line) > 0) {
                            printf("CD-ROM: Neo Geo CD game title: %s\n", line);
                            strncpy(disc_id, line, disc_id_size - 1);
                            disc_id[disc_id_size - 1] = '\0';
                            
                            fclose(f);
                            
                            // Unmount
                            snprintf(mount_cmd, sizeof(mount_cmd), "umount %s 2>/dev/null", temp_mount_point);
                            ::system(mount_cmd);
                            ::system("rmdir /tmp/cdrom_mount 2>/dev/null");
                            
                            return true;
                        }
                    }
                    fclose(f);
                }
            }
        }
    }
    
    // If we found Neo Geo files but no specific ID, use a generic identifier
    if (found_neogeo_files) {
        strncpy(disc_id, "NEOGEOCD", disc_id_size - 1);
        disc_id[disc_id_size - 1] = '\0';
        
        // Unmount
        snprintf(mount_cmd, sizeof(mount_cmd), "umount %s 2>/dev/null", temp_mount_point);
        ::system(mount_cmd);
        ::system("rmdir /tmp/cdrom_mount 2>/dev/null");
        
        return true;
    }
    
    // Unmount
    snprintf(mount_cmd, sizeof(mount_cmd), "umount %s 2>/dev/null", temp_mount_point);
    ::system(mount_cmd);
    ::system("rmdir /tmp/cdrom_mount 2>/dev/null");
    
    return false;
}

bool extract_disc_id(const char* device_path, char* disc_id, size_t disc_id_size)
{
    printf("CD-ROM: Starting systematic disc analysis\n");
    
    // Step 1: Detect disc format and system
    DiscInfo disc_info = {0};
    if (!detect_disc_format_and_system(device_path, &disc_info)) {
        printf("CD-ROM: Could not determine disc system type\n");
        return false;
    }
    
    // Step 2: Extract ID based on detected system
    if (strcmp(disc_info.system, "PSX") == 0) {
        return extract_psx_disc_id(device_path, disc_id, disc_id_size);
    } else if (strcmp(disc_info.system, "Saturn") == 0) {
        return extract_saturn_disc_id(device_path, disc_id, disc_id_size);
    } else if (strcmp(disc_info.system, "SegaCD") == 0) {
        return extract_segacd_disc_id(device_path, disc_id, disc_id_size);
    } else if (strcmp(disc_info.system, "PCECD") == 0) {
        return extract_pcecd_disc_id(device_path, disc_id, disc_id_size);
    } else if (strcmp(disc_info.system, "NeoGeoCD") == 0) {
        return extract_neogeocd_disc_id(device_path, disc_id, disc_id_size);
    } else {
        printf("CD-ROM: Unknown system type, cannot extract ID\n");
        return false;
    }
}

// Helper function to skip whitespace
static const char* skip_whitespace(const char* ptr) {
    while (*ptr && isspace(*ptr)) ptr++;
    return ptr;
}

// Helper function to find next quote (handles escaped quotes)
static const char* find_next_quote(const char* ptr) {
    const char* start = ptr;
    while (*ptr) {
        if (*ptr == '"' && (ptr == start || *(ptr-1) != '\\')) {
            return ptr;
        }
        ptr++;
    }
    return NULL;
}

// Extract string value from JSON
static bool extract_json_string(const char* json_str, const char* key, char* value, size_t value_size) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    const char* key_pos = strstr(json_str, search_key);
    if (!key_pos) return false;
    
    // Skip past key and find colon
    const char* ptr = key_pos + strlen(search_key);
    ptr = skip_whitespace(ptr);
    
    if (*ptr != ':') return false;
    ptr++; // Skip colon
    ptr = skip_whitespace(ptr);
    
    // Value should start with quote
    if (*ptr != '"') return false;
    ptr++; // Skip opening quote
    
    // Find closing quote
    const char* end_quote = find_next_quote(ptr);
    if (!end_quote) return false;
    
    // Copy value
    size_t len = end_quote - ptr;
    if (len >= value_size) len = value_size - 1;
    
    strncpy(value, ptr, len);
    value[len] = '\0';
    
    return true;
}

bool search_gamedb_for_disc(const char* db_path, const char* disc_id, CDRomGameInfo* result)
{
    FILE* db_file = fopen(db_path, "r");
    if (!db_file) {
        printf("CD-ROM: Failed to open GameDB file: %s\n", db_path);
        return false;
    }
    
    printf("CD-ROM: Searching for disc ID '%s' in %s\n", disc_id, db_path);
    
    // Read entire file into memory for JSON parsing
    fseek(db_file, 0, SEEK_END);
    long file_size = ftell(db_file);
    fseek(db_file, 0, SEEK_SET);
    
    if (file_size > 10 * 1024 * 1024) { // 10MB limit
        printf("CD-ROM: GameDB file too large (%ld bytes)\n", file_size);
        fclose(db_file);
        return false;
    }
    
    char* json_buffer = (char*)malloc(file_size + 1);
    if (!json_buffer) {
        printf("CD-ROM: Failed to allocate memory for JSON\n");
        fclose(db_file);
        return false;
    }
    
    size_t bytes_read = fread(json_buffer, 1, file_size, db_file);
    json_buffer[bytes_read] = '\0';
    fclose(db_file);
    
    // Search for disc ID as a JSON key
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\"", disc_id);
    
    char* entry_start = strstr(json_buffer, search_key);
    if (!entry_start) {
        printf("CD-ROM: Disc ID '%s' not found in database\n", disc_id);
        free(json_buffer);
        return false;
    }
    
    // Find the colon and opening brace for this entry
    char* colon = strchr(entry_start, ':');
    if (!colon) {
        free(json_buffer);
        return false;
    }
    
    const char* obj_start_const = skip_whitespace(colon + 1);
    char* obj_start = (char*)obj_start_const;
    if (*obj_start != '{') {
        free(json_buffer);
        return false;
    }
    
    // Find the matching closing brace
    int brace_count = 1;
    char* ptr = obj_start + 1;
    char* obj_end = NULL;
    bool in_string = false;
    
    while (*ptr && brace_count > 0) {
        if (*ptr == '"' && (ptr == obj_start + 1 || *(ptr-1) != '\\')) {
            in_string = !in_string;
        } else if (!in_string) {
            if (*ptr == '{') brace_count++;
            else if (*ptr == '}') {
                brace_count--;
                if (brace_count == 0) {
                    obj_end = ptr;
                    break;
                }
            }
        }
        ptr++;
    }
    
    if (!obj_end) {
        printf("CD-ROM: Failed to parse JSON entry\n");
        free(json_buffer);
        return false;
    }
    
    // Create a substring containing only this disc's entry
    size_t entry_length = obj_end - obj_start + 1;
    char* entry_json = (char*)malloc(entry_length + 1);
    if (!entry_json) {
        free(json_buffer);
        return false;
    }
    
    strncpy(entry_json, obj_start, entry_length);
    entry_json[entry_length] = '\0';
    
    printf("CD-ROM: Parsing entry: %.200s...\n", entry_json);
    
    // Extract fields from the specific JSON entry
    bool found = false;
    
    // Extract title
    if (extract_json_string(entry_json, "title", result->title, sizeof(result->title))) {
        printf("CD-ROM: Title: %s\n", result->title);
        found = true;
    }
    
    // Extract region
    if (extract_json_string(entry_json, "region", result->region, sizeof(result->region))) {
        printf("CD-ROM: Region: %s\n", result->region);
    } else {
        strncpy(result->region, "Unknown", sizeof(result->region) - 1);
    }
    
    // Extract publisher
    if (extract_json_string(entry_json, "publisher", result->publisher, sizeof(result->publisher))) {
        printf("CD-ROM: Publisher: %s\n", result->publisher);
    }
    
    // Extract year
    if (extract_json_string(entry_json, "year", result->year, sizeof(result->year))) {
        printf("CD-ROM: Year: %s\n", result->year);
    }
    
    // Extract product code
    if (extract_json_string(entry_json, "product_code", result->product_code, sizeof(result->product_code))) {
        printf("CD-ROM: Product Code: %s\n", result->product_code);
    }
    
    // Clean up entry JSON
    free(entry_json);
    
    // Set the disc ID
    strncpy(result->id, disc_id, sizeof(result->id) - 1);
    
    // If we didn't find a title, use the disc ID
    if (!found) {
        strncpy(result->title, disc_id, sizeof(result->title) - 1);
    }
    
    // Clean up
    free(json_buffer);
    
    return found;
}

bool gameid_setup_environment()
{
    // Check if GameDB directory exists
    if (!PathIsDir("/media/fat/GameDB"))
    {
        printf("CD-ROM: GameDB directory not found, please install GameDB to /media/fat/GameDB/\n");
        return false;
    }
    
    return true;
}

bool gameid_identify_disc(const char* device_path, const char* system, CDRomGameInfo* result)
{
    if (!result) return false;
    
    // Clear result structure
    memset(result, 0, sizeof(CDRomGameInfo));
    
    // Extract disc ID from the CD-ROM first
    char disc_id[32] = "";
    if (!extract_disc_id(device_path, disc_id, sizeof(disc_id))) {
        printf("CD-ROM: Failed to extract disc ID from %s\n", device_path);
        strncpy(disc_id, "UNKNOWN", sizeof(disc_id) - 1);
    }
    
    printf("CD-ROM: Extracted disc ID: %s\n", disc_id);
    
    // Check if GameDB file exists for this system
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "/media/fat/GameDB/%s.data.json", system);
    
    if (!FileExists(db_path))
    {
        printf("CD-ROM: GameDB file not found: %s\n", db_path);
        // Still return some basic info even without database
        strncpy(result->system, system, sizeof(result->system) - 1);
        strncpy(result->title, disc_id, sizeof(result->title) - 1);
        strncpy(result->id, disc_id, sizeof(result->id) - 1);
        strncpy(result->region, "Unknown", sizeof(result->region) - 1);
        result->valid = false;
        return true; // Return true so we still get the extracted ID
    }
    
    // Search GameDB for this disc ID
    if (search_gamedb_for_disc(db_path, disc_id, result)) {
        result->valid = true;
        strncpy(result->system, system, sizeof(result->system) - 1);
        printf("CD-ROM: Game identified: %s (%s)\n", result->title, result->region);
        return true;
    } else {
        printf("CD-ROM: Game not found in database\n");
        // Set fallback information
        strncpy(result->system, system, sizeof(result->system) - 1);
        strncpy(result->title, "Unknown Game", sizeof(result->title) - 1);
        strncpy(result->id, disc_id, sizeof(result->id) - 1);
        result->valid = false;
        return false;
    }
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
    ::system(mkdir_command);
    
    return cdrom_create_cue_bin(device_path, output_dir, game_name);
}

const char* cdrom_get_system_from_detection()
{
    // Use proper disc detection that includes magic word detection
    DiscInfo disc_info = {0};
    if (detect_disc_format_and_system(cdrom_device_path, &disc_info) && disc_info.detected)
    {
        // Return the detected system type
        if (strcmp(disc_info.system, "Saturn") == 0) return "Saturn";
        if (strcmp(disc_info.system, "SegaCD") == 0) return "SegaCD";
        if (strcmp(disc_info.system, "PCECD") == 0) return "PCECD";
        if (strcmp(disc_info.system, "PSX") == 0) return "PSX";
    }
    
    // Fallback: try different systems in order of likelihood
    const char* systems[] = {"PSX", "Saturn", "SegaCD", "PCECD", NULL};
    
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
    ::system(mkdir_command);
    
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
        printf("✓ Read %d bytes from device\n", (int)bytes_read);
        
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