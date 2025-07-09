# NFC Tag Programming Guide for MiSTer

## Overview

MiSTer now supports programming NFC tags directly from the system without external tools. This allows you to create a physical game library where each NFC tag represents a game, core, or file.

## Programming Methods

### 1. Command Line Interface

#### Basic Commands
```bash
# Setup NFC reader first
nfc_setup

# Program a game tag (searches for game)
nfc_program_game "Sonic" "Genesis"

# Program a core tag (loads specific core)
nfc_program_core "Genesis_MiSTer"

# Program a file tag (loads specific file)
nfc_program_file "/media/fat/games/sonic.bin"

# Write custom text to tag
nfc_write "GAME:Sonic The Hedgehog:Genesis"

# Format/clear a tag
nfc_format
```

#### Advanced Examples
```bash
# Program game without specifying core (searches all cores)
nfc_program_game "Mario"

# Program with spaces in names
nfc_program_game "Super Mario Bros" "NES"

# Program direct file paths
nfc_program_file "/media/fat/_Games/Genesis/sonic.zip"

# Write custom command
nfc_write "CMD:screenshot game_screenshot.png"
```

### 2. OSD Menu Integration

The NFC programming can be integrated into the MiSTer OSD menu system:

#### Menu Structure
```
System Menu
└── NFC Programming
    ├── Setup NFC Reader
    ├── Program Current Game
    ├── Program Current Core
    ├── Program Custom Tag
    ├── Format Tag
    └── Tag Information
```

#### Implementation in menu.cpp
```cpp
case MENU_NFC_PROGRAMMING1:
    OsdSetTitle("NFC Programming", 0);
    OsdWrite(0, "  Setup NFC Reader", menusub == 0);
    OsdWrite(1, "  Program Current Game", menusub == 1);
    OsdWrite(2, "  Program Current Core", menusub == 2);
    OsdWrite(3, "  Program Custom Tag", menusub == 3);
    OsdWrite(4, "  Format Tag", menusub == 4);
    OsdWrite(5, "  Tag Information", menusub == 5);
    OsdWrite(6, "");
    OsdWrite(7, "  Exit", menusub == 6);
    menustate = MENU_NFC_PROGRAMMING2;
    break;

case MENU_NFC_PROGRAMMING2:
    if (select) {
        switch (menusub) {
            case 0: // Setup NFC Reader
                cmd_bridge_process("nfc_setup");
                break;
            case 1: // Program Current Game
                if (strlen(last_filename) > 0) {
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "nfc_program_file %s", last_filename);
                    cmd_bridge_process(cmd);
                }
                break;
            case 2: // Program Current Core
                if (strlen(CoreName) > 0) {
                    char cmd[512];
                    snprintf(cmd, sizeof(cmd), "nfc_program_core %s", CoreName);
                    cmd_bridge_process(cmd);
                }
                break;
            case 3: // Program Custom Tag
                menustate = MENU_NFC_CUSTOM_TAG;
                break;
            case 4: // Format Tag
                cmd_bridge_process("nfc_format");
                break;
            case 5: // Tag Information
                cmd_bridge_process("nfc_poll");
                break;
            case 6: // Exit
                menustate = MENU_NONE1;
                break;
        }
    }
    break;
```

### 3. File Browser Integration

Add NFC programming to the file browser context menu:

```cpp
// In file browser, when file is selected
if (user_button_pressed()) {
    // Show context menu with NFC programming option
    context_menu_items[] = {
        "Load File",
        "Program NFC Tag",
        "Add to Favorites",
        "Cancel"
    };
    
    if (selected_item == 1) { // Program NFC Tag
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "nfc_program_file %s", selected_file_path);
        cmd_bridge_process(cmd);
    }
}
```

## Tag Data Formats

### Game Tags
```
Format: GAME:<game_name>[:<core_name>]
Examples:
- GAME:Sonic:Genesis
- GAME:Mario:NES
- GAME:Street Fighter II:Arcade
```

### Core Tags
```
Format: CORE:<core_name>
Examples:
- CORE:Genesis_MiSTer
- CORE:SNES_MiSTer
- CORE:Arcade_MiSTer
```

### File Tags
```
Format: LOAD:<file_path>
Examples:
- LOAD:/media/fat/games/sonic.bin
- LOAD:/media/fat/_Games/Genesis/sonic.zip
- LOAD:/media/fat/cheats/genesis_cheats.xml
```

### Custom Command Tags
```
Format: CMD:<command>
Examples:
- CMD:screenshot
- CMD:reset cold
- CMD:popup_browse /media/fat/games
```

## Hardware Requirements

### NFC Module Setup
- **Module**: PN532 NFC breakout board
- **Connection**: I2C to MiSTer GPIO header
- **Power**: 3.3V from MiSTer
- **Address**: 0x24 (default)

### Wiring
```
PN532    MiSTer GPIO
VCC   -> 3.3V (Pin 1)
GND   -> GND (Pin 6)
SDA   -> GPIO 2 (Pin 3)
SCL   -> GPIO 3 (Pin 5)
IRQ   -> GPIO 4 (Pin 7) [optional]
```

## NFC Tag Types

### Recommended Tags
1. **NTAG213** (180 bytes)
   - Cost: ~$0.50 each
   - Capacity: ~48 bytes usable
   - Good for simple game/core tags

2. **NTAG215** (540 bytes)
   - Cost: ~$0.75 each
   - Capacity: ~137 bytes usable
   - Good for games with long names

3. **NTAG216** (928 bytes)
   - Cost: ~$1.00 each
   - Capacity: ~924 bytes usable
   - Good for complex commands/metadata

### Tag Formats
- **Stickers**: Easy to apply to game boxes
- **Cards**: Durable, good for frequent use
- **Keychains**: Convenient for cores/systems
- **Coins**: Compact, good for embedding

## Usage Workflows

### Workflow 1: Game Collection
1. **Setup**: Place NFC reader near MiSTer
2. **Browse**: Use file browser to select game
3. **Program**: Right-click → "Program NFC Tag"
4. **Apply**: Stick programmed tag to game box
5. **Play**: Touch tag to NFC reader to instant-load

### Workflow 2: Core Switching
1. **Create Core Tags**: Program one tag per core
2. **Organize**: Keep core tags in a holder/stand
3. **Switch**: Touch core tag to switch systems
4. **Game**: Touch game tag to load specific game

### Workflow 3: Favorites System
1. **Program Favorites**: Create tags for most-played games
2. **Quick Access**: Keep favorite tags near MiSTer
3. **Instant Play**: Touch for immediate loading
4. **Share**: Program duplicate tags for friends

## Advanced Features

### Batch Programming
```bash
# Program multiple tags from file list
for game in games.txt; do
    echo "Place tag for $game and press Enter"
    read
    nfc_program_game "$game"
done
```

### Tag Database
```bash
# Store tag mappings in database
echo "$(nfc_poll):GAME:Sonic:Genesis" >> /media/fat/nfc_tags.db
```

### Conditional Programming
```bash
# Program tag based on current context
if [[ "$CoreName" == "Genesis" ]]; then
    nfc_program_game "$(basename "$current_file")" "Genesis"
else
    nfc_program_file "$current_file"
fi
```

## Error Handling

### Common Issues
1. **"NFC reader not initialized"**
   - Solution: Run `nfc_setup` first

2. **"No tag present for writing"**
   - Solution: Place writable NFC tag on reader

3. **"Failed to write to NFC tag"**
   - Solutions: 
     - Check tag is writable (not locked)
     - Verify tag capacity is sufficient
     - Try different tag type

4. **"Text too long for tag"**
   - Solution: Use shorter text or larger tag (NTAG215/216)

### Debug Commands
```bash
# Check NFC reader status
nfc_setup

# Test tag detection
nfc_poll

# Verify tag capacity
nfc_poll  # Shows tag info including estimated capacity
```

## Security Considerations

### Tag Validation
- Only accept known command formats
- Validate file paths exist
- Prevent directory traversal attacks
- Sanitize game/core names

### Physical Security
- Keep NFC reader in controlled area
- Consider read-only mode for public setups
- Use tag authentication for sensitive operations

## Future Enhancements

### Planned Features
1. **Tag Encryption**: Encrypt sensitive tag data
2. **User Profiles**: Different tag sets per user
3. **Tag Sharing**: Import/export tag databases
4. **Visual Programming**: Drag-and-drop tag creation
5. **Tag Analytics**: Track most-used tags
6. **Backup System**: Automatic tag backup/restore

### Integration Opportunities
1. **Mobile App**: Program tags from phone
2. **Web Interface**: Manage tag database online
3. **Community**: Share tag databases with others
4. **Automation**: Auto-program tags during file operations

This comprehensive NFC programming system transforms MiSTer into a modern gaming platform with physical-digital integration, making game selection as simple as touching a tag.