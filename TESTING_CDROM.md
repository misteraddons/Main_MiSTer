# MiSTer CD-ROM System Testing Guide

## Overview

The CD-ROM system allows MiSTer to read physical discs and automatically create disc images for supported systems (PlayStation, Saturn, Sega CD, PC Engine CD).

## Testing Methods

### 1. Without Hardware (Development Testing)

**Run the test script:**
```bash
./Scripts/test_triggers.sh
```

**Compile and run test utility:**
```bash
gcc -I. test_cdrom.c cdrom.cpp file_io.cpp -o test_cdrom
./test_cdrom
```

This will test:
- Filename sanitization
- Mock disc image creation
- Directory structure
- Basic function validation

### 2. With Real MiSTer Hardware

#### Prerequisites

1. **USB CD-ROM Drive**
   - Connect USB CD-ROM/DVD drive to MiSTer
   - Ensure it appears as `/dev/sr0` (or `/dev/sr1`, etc.)

2. **GameID Installation**
   ```bash
   # Download GameID from https://github.com/thefatrat/GameID
   mkdir -p /media/fat/Scripts/_GameID
   # Place GameID.py in /media/fat/Scripts/_GameID/
   
   # Download the database
   mkdir -p /media/fat/gameID
   # Place db.pkl.gz in /media/fat/gameID/
   ```

3. **Test Disc**
   - Insert a supported game disc (PSX, Saturn, Sega CD)
   - Avoid scratched or damaged discs for initial testing

#### Step-by-Step Testing

**Step 1: Check System Status**
```cpp
// Add to menu or UART command
cdrom_print_status();
```

Expected output:
```
CD-ROM System Status:
====================
Initialized: Yes
Drive detected: Yes
Device path: /dev/sr0
Disc inserted: Yes
GameID script: Found
GameID database: Found
Games directory: Exists
====================
```

**Step 2: Test Device Access**
```cpp
// Test specific device
cdrom_test_device("/dev/sr0");
```

Expected output:
```
Testing CD-ROM device: /dev/sr0
✓ Device exists
✓ Device can be opened
✓ Read 2048 bytes from device
✓ ISO 9660 filesystem detected
```

**Step 3: Test Auto-Detection**
```cpp
// Auto-detect and load disc
if (cdrom_load_disc_auto()) {
    printf("Success!\n");
} else {
    printf("Failed!\n");
}
```

**Step 4: Verify Output**
Check these locations:
- `/media/fat/games/PSX/` (or Saturn, MegaCD, PCECD)
- Look for `.bin` and `.cue` files
- Check `.info` file for game metadata

### 3. Manual Testing Without GameID

If GameID is not available, you can test the core functionality:

```cpp
// Test basic disc reading
CDRomGameInfo dummy_info;
strcpy(dummy_info.title, "Test Game");
strcpy(dummy_info.system, "PSX");
strcpy(dummy_info.region, "USA");
dummy_info.valid = true;

// Test disc image creation
if (cdrom_store_game_to_library("/dev/sr0", "PSX", &dummy_info)) {
    printf("Disc image created successfully!\n");
}
```

## Troubleshooting

### Common Issues

**1. No CD-ROM Device Detected**
```
✗ No CD-ROM drive detected
```
- Check USB connection
- Verify drive compatibility (most USB drives work)
- Check `dmesg` for USB enumeration
- Try different USB port

**2. Permission Denied**
```
✗ Cannot open device: Permission denied
```
- MiSTer should run as root (normal)
- Check device permissions: `ls -la /dev/sr*`
- Verify disc is inserted properly

**3. GameID Not Found**
```
GameID script: Missing
```
- Download from: https://github.com/thefatrat/GameID
- Install to: `/media/fat/Scripts/_GameID/GameID.py`
- Make executable: `chmod +x GameID.py`
- Install Python dependencies if needed

**4. Read Errors**
```
CD-ROM: Partial read at sector 1234, padding with zeros
```
- Normal for damaged/scratched discs
- Try cleaning the disc
- Test with different disc

**5. No Space Available**
```
CD-ROM: Failed to create BIN file
```
- Check available space: `df -h /media/fat`
- CD images can be 650-700MB each
- Clean up old files if needed

### Debug Commands

Add these to your testing:

```cpp
// Basic system check
cdrom_init();
cdrom_print_status();

// Test specific device
cdrom_test_device("/dev/sr0");

// Check disc insertion
if (cdrom_is_disc_inserted()) {
    printf("Disc is ready for reading\n");
}

// Manual system detection
const char* system = cdrom_get_system_from_detection();
printf("Detected system: %s\n", system);
```

## Expected File Output

After successful disc loading, you should see:

```
/media/fat/games/PSX/
├── Game_Title.bin      # Disc image (650MB typical)
├── Game_Title.cue      # Cue sheet
└── Game_Title.info     # Metadata file
```

**Sample .info file:**
```
Title: Crash Bandicoot 3 - Warped
System: PSX
Region: USA
Game Name: Crash Bandicoot 3 - Warped (USA)
Internal Title: CRASH3
Release Date: 1998-10-31
Language: English
Device Info: CD-ROM/XA
```

## Performance Notes

- **Read Speed**: ~1-4x depending on USB drive
- **Time**: 10-30 minutes for full disc (650MB)
- **Progress**: Updates every 1000 sectors (~2MB)
- **Error Handling**: Automatically pads damaged sectors

## Integration Points

The CD-ROM system can be integrated with:

1. **UART Commands**
   ```
   CMD:cdrom_load_auto
   CMD:cdrom_load PSX
   CMD:cdrom_status
   ```

2. **Menu System**
   - Add "Load from CD-ROM" option
   - Show progress during reading
   - Display detected game info

3. **Automatic Detection**
   - Monitor USB insertion events
   - Auto-prompt when disc inserted
   - Background disc identification

## Future Enhancements

- CHD format support for smaller files
- Multi-session disc support
- Audio track extraction
- Real-time disc streaming (no image creation)
- Automatic core loading after disc read