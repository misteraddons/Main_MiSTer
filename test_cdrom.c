#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "cdrom.h"

// Test functions that simulate various scenarios
void test_drive_detection()
{
    printf("=== Testing CD-ROM Drive Detection ===\n");
    
    if (cdrom_detect_drive())
    {
        printf("✓ CD-ROM drive detected\n");
    }
    else
    {
        printf("✗ No CD-ROM drive detected\n");
        printf("  This is expected without actual hardware\n");
    }
    printf("\n");
}

void test_gameid_setup()
{
    printf("=== Testing GameID Setup ===\n");
    
    // Create mock directories
    system("mkdir -p /tmp/test_mister/Scripts/_GameID");
    system("mkdir -p /tmp/test_mister/gameID");
    
    // Create mock GameID script
    FILE* script = fopen("/tmp/test_mister/Scripts/_GameID/GameID.py", "w");
    if (script)
    {
        fprintf(script, "#!/usr/bin/env python3\n");
        fprintf(script, "# Mock GameID script for testing\n");
        fprintf(script, "import sys\n");
        fprintf(script, "print('manufacturer_ID    Sony Computer Entertainment')\n");
        fprintf(script, "print('ID    SLUS-01484')\n");
        fprintf(script, "print('title    Crash Bandicoot 3 - Warped')\n");
        fprintf(script, "print('region    USA')\n");
        fprintf(script, "print('redump_name    Crash Bandicoot 3 - Warped (USA)')\n");
        fclose(script);
        system("chmod +x /tmp/test_mister/Scripts/_GameID/GameID.py");
        printf("✓ Mock GameID script created\n");
    }
    
    // Create mock database
    FILE* db = fopen("/tmp/test_mister/gameID/db.pkl.gz", "w");
    if (db)
    {
        fprintf(db, "mock_database_data");
        fclose(db);
        printf("✓ Mock GameID database created\n");
    }
    
    printf("\n");
}

void test_filename_sanitization()
{
    printf("=== Testing Filename Sanitization ===\n");
    
    struct {
        const char* input;
        const char* expected;
    } test_cases[] = {
        {"Crash Bandicoot 3: Warped", "Crash Bandicoot 3_ Warped"},
        {"Game/Name\\With:Bad*Chars", "Game_Name_With_Bad_Chars"},
        {"Normal Game Name", "Normal Game Name"},
        {"Name.with.dots...", "Name.with.dots"},
        {"  Spaced Name  ", "  Spaced Name"},
        {NULL, NULL}
    };
    
    for (int i = 0; test_cases[i].input; i++)
    {
        char* result = cdrom_sanitize_filename(test_cases[i].input);
        printf("Input: '%s' -> Output: '%s'\n", test_cases[i].input, result);
        
        // Basic validation - no path separators
        if (strchr(result, '/') || strchr(result, '\\'))
        {
            printf("✗ Still contains path separators!\n");
        }
        else
        {
            printf("✓ Safe filename\n");
        }
    }
    printf("\n");
}

void test_disc_image_creation()
{
    printf("=== Testing Disc Image Creation (Mock) ===\n");
    
    // Create a mock CD device file with test data
    const char* mock_device = "/tmp/mock_cdrom.bin";
    FILE* mock_cd = fopen(mock_device, "wb");
    if (mock_cd)
    {
        // Write some test sectors (2048 bytes each)
        char sector[2048];
        memset(sector, 0, sizeof(sector));
        
        // Write mock CD header
        strcpy(sector, "CD001"); // ISO 9660 signature
        fwrite(sector, 1, sizeof(sector), mock_cd);
        
        // Write a few more sectors with test data
        for (int i = 1; i < 10; i++)
        {
            sprintf(sector, "TEST_SECTOR_%04d", i);
            fwrite(sector, 1, sizeof(sector), mock_cd);
        }
        
        fclose(mock_cd);
        printf("✓ Mock CD device created: %s\n", mock_device);
        
        // Test the image creation function
        system("mkdir -p /tmp/test_output");
        if (cdrom_create_cue_bin(mock_device, "/tmp/test_output", "Test_Game"))
        {
            printf("✓ Disc image creation succeeded\n");
            
            // Verify output files
            struct stat st;
            if (stat("/tmp/test_output/Test_Game.bin", &st) == 0)
            {
                printf("✓ BIN file created (size: %ld bytes)\n", st.st_size);
            }
            if (stat("/tmp/test_output/Test_Game.cue", &st) == 0)
            {
                printf("✓ CUE file created\n");
                
                // Show CUE contents
                FILE* cue = fopen("/tmp/test_output/Test_Game.cue", "r");
                if (cue)
                {
                    char line[256];
                    printf("CUE file contents:\n");
                    while (fgets(line, sizeof(line), cue))
                    {
                        printf("  %s", line);
                    }
                    fclose(cue);
                }
            }
        }
        else
        {
            printf("✗ Disc image creation failed\n");
        }
        
        // Cleanup
        unlink(mock_device);
    }
    printf("\n");
}

void test_system_detection()
{
    printf("=== Testing System Detection ===\n");
    
    const char* detected = cdrom_get_system_from_detection();
    printf("Detected system: %s\n", detected);
    printf("Note: Without real disc, defaults to PSX\n");
    printf("\n");
}

int main()
{
    printf("MiSTer CD-ROM System Test Suite\n");
    printf("===============================\n\n");
    
    // Initialize the CD-ROM subsystem
    cdrom_init();
    
    // Run all tests
    test_drive_detection();
    test_gameid_setup();
    test_filename_sanitization();
    test_disc_image_creation();
    test_system_detection();
    
    // Cleanup
    cdrom_cleanup();
    system("rm -rf /tmp/test_mister /tmp/test_output");
    
    printf("Test suite complete!\n\n");
    printf("Real Hardware Testing Instructions:\n");
    printf("==================================\n");
    printf("1. Connect USB CD-ROM drive to MiSTer\n");
    printf("2. Insert a PlayStation, Saturn, or Sega CD disc\n");
    printf("3. Install GameID to /media/fat/Scripts/_GameID/\n");
    printf("4. Call cdrom_load_disc_auto() from menu or UART\n");
    printf("5. Check /media/fat/games/[system]/ for disc image\n\n");
    
    return 0;
}