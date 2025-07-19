# MiSTer Game Launcher Architecture

## Overview
A modular system that separates game identification, GameDB lookup, and MGL creation from specific input sources. This allows multiple peripherals and input methods to trigger game loading.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Input Sources                                │
├─────────────────────────────────────────────────────────────────┤
│ CD-ROM Daemon │ NFC Reader │ Network API │ GPIO Buttons │ etc.  │
└─────────────────┬───────────┬─────────────┬──────────────┬──────┘
                  │           │             │              │
                  v           v             v              v
┌─────────────────────────────────────────────────────────────────┐
│                Game Launcher Service                            │
│                /dev/MiSTer_game_launcher                        │
├─────────────────────────────────────────────────────────────────┤
│  • GameDB Lookup        • Fuzzy Search                         │
│  • MGL Creation         • File Discovery                       │
│  • Launch Coordination  • OSD Notifications                    │
└─────────────────┬───────────────────────────────────────────────┘
                  │
                  v
┌─────────────────────────────────────────────────────────────────┐
│                   MiSTer Main Process                           │
│                  /dev/MiSTer_cmd                                │
├─────────────────────────────────────────────────────────────────┤
│  • Core Loading         • Menu Management                      │
│  • File Mounting        • OSD Display                          │
│  • Input Handling       • System Control                       │
└─────────────────────────────────────────────────────────────────┘
```

## Components

### 1. **Game Launcher Service** (`game_launcher_service`)
**Purpose**: Centralized game identification and MGL creation
**Location**: `/media/fat/utils/game_launcher_service`
**Interface**: `/dev/MiSTer_game_launcher` (FIFO)

**Functions**:
- GameDB lookup by serial, title, hash, UUID
- Fuzzy search with weighted scoring
- MGL file creation (single/multiple)
- File path resolution
- OSD notification management

### 2. **Input Source Daemons** (Multiple processes)
**Purpose**: Detect input events and send game requests
**Examples**:
- `cdrom_daemon` - Physical CD detection
- `nfc_daemon` - NFC card reading
- `network_daemon` - Network API requests
- `gpio_daemon` - Button/switch monitoring

### 3. **Communication Protocol**
**Method**: JSON messages over named pipes
**Commands**:
```json
{
  "command": "find_game",
  "system": "PSX",
  "id_type": "serial",
  "identifier": "SLUS-00067",
  "source": "cdrom",
  "auto_launch": true
}
```

## Game Launch Methods

### 1. **Physical CD-ROM**
```
CD Insert → cdrom_daemon → game_launcher_service → MGL creation → Launch
```

### 2. **NFC Card Tap**
```
NFC Tap → nfc_daemon → UUID lookup → game_launcher_service → Launch
```

### 3. **Network API**
```
HTTP Request → network_daemon → game lookup → game_launcher_service → Launch
```

### 4. **GPIO Buttons**
```
Button Press → gpio_daemon → predefined game → game_launcher_service → Launch
```

### 5. **QR Code Scan**
```
QR Scan → camera_daemon → game code → game_launcher_service → Launch
```

### 6. **Voice Command**
```
"Load Castlevania" → voice_daemon → title search → game_launcher_service → Launch
```

### 7. **Mobile App**
```
Phone App → bluetooth_daemon → game selection → game_launcher_service → Launch
```

### 8. **Barcode Scanner**
```
Barcode Scan → barcode_daemon → UPC lookup → game_launcher_service → Launch
```

## Directory Structure

```
/media/fat/utils/
├── game_launcher_service           # Core service
├── gamedb/                         # GameDB files
├── launchers/                      # Input source daemons
│   ├── cdrom_daemon
│   ├── nfc_daemon
│   ├── network_daemon
│   ├── gpio_daemon
│   └── voice_daemon
├── configs/                        # Configuration files
│   ├── game_launcher.conf
│   ├── nfc_cards.db
│   └── gpio_mappings.conf
└── scripts/                        # Helper scripts
    ├── install_launcher.sh
    └── setup_nfc.sh
```

## Configuration

### Game Launcher Service Config (`game_launcher.conf`)
```ini
[Service]
gamedb_dir = /media/fat/utils/gamedb
games_dir = /media/fat/games
temp_dir = /tmp
log_level = info

[Search]
fuzzy_threshold = 30
max_results = 10
region_priority = USA,Europe,Japan

[MGL]
auto_launch_threshold = 90
show_selection_menu = true
mgl_cleanup_timeout = 300

[OSD]
show_notifications = true
notification_timeout = 3000
```

### NFC Card Database (`nfc_cards.db`)
```json
{
  "cards": [
    {
      "uid": "04:A3:22:B2:C4:58:80",
      "title": "Castlevania SOTN",
      "system": "PSX",
      "serial": "SLUS-00067",
      "region": "USA"
    },
    {
      "uid": "04:B1:33:C2:D4:69:91",
      "title": "Panzer Dragoon Saga",
      "system": "Saturn",
      "serial": "T-8109H",
      "region": "USA"
    }
  ]
}
```

### GPIO Mappings (`gpio_mappings.conf`)
```ini
[GPIO_Buttons]
# Pin 18: Load last played game
pin_18 = load_last_game

# Pin 19: Random game selection
pin_19 = random_game

# Pin 20: Specific game shortcut
pin_20 = system=PSX,serial=SLUS-00067
```

## Benefits

1. **Modularity**: Each input source is independent
2. **Extensibility**: Easy to add new input methods
3. **Reusability**: GameDB logic shared across all sources
4. **Maintainability**: Clear separation of concerns
5. **Scalability**: Can handle multiple simultaneous requests
6. **Consistency**: Uniform game launching experience

## Implementation Priority

1. **Core Service**: Extract GameDB/MGL logic to standalone service
2. **CD-ROM Adaptation**: Modify existing daemon to use service
3. **NFC Support**: Implement NFC card reading
4. **Network API**: HTTP/JSON interface for remote control
5. **GPIO Buttons**: Physical button integration
6. **Additional Methods**: Voice, QR, barcode, etc.

This modular approach transforms the current CD-ROM specific daemon into a powerful, extensible game launching platform that can be triggered by virtually any input source!