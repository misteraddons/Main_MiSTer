# GameDB Format for MiSTer CD-ROM Support

## Overview

The MiSTer CD-ROM system now uses GameDB JSON format instead of the original GameID Python script. This provides better performance and easier maintenance.

## Directory Structure

```
/media/fat/GameDB/
├── PSX.data.json          # PlayStation games database
├── Saturn.data.json       # Sega Saturn games database
├── SegaCD.data.json       # Sega CD games database
└── PCECD.data.json        # PC Engine CD games database
```

## JSON Format

The GameDB uses JSON format from the GameDB repositories:

- **PSX**: https://github.com/niemasd/GameDB-PSX
- **Saturn**: https://github.com/niemasd/GameDB-Saturn
- **SegaCD**: https://github.com/niemasd/GameDB-SegaCD
- **PCECD**: https://github.com/niemasd/GameDB-PCECD

## Installation

1. Download the `.data.json` files from the respective GameDB repositories
2. Place them in `/media/fat/GameDB/` directory
3. Ensure files are named correctly (e.g., `PSX.data.json`)

## Usage

The CD-ROM system will automatically:
1. Detect the disc type (PSX, Saturn, SegaCD, PCECD)
2. Look up the corresponding GameDB file
3. Match the disc ID with the database
4. Extract game information (title, region, etc.)
5. Store the game with proper naming

## Current Status

- ✅ GameDB directory support implemented
- ✅ JSON file detection working
- ✅ Auto-mounting functionality implemented
- ⚠️ JSON parsing not yet implemented (TODO)
- ⚠️ Disc ID extraction not yet implemented (TODO)

## Auto-Mounting

The system now automatically handles CD-ROM mounting:

1. **Detection**: When a CD-ROM device is found but not readable
2. **Auto-Mount**: Automatically runs `/media/fat/Scripts/cdrom/cdrom_mount.sh`  
3. **Retry**: Attempts to access the device again after mounting
4. **Fallback**: Checks multiple device paths (`/dev/sr0`, `/dev/sr1`, etc.)

## TODO

1. Implement JSON parsing library or simple parser
2. Add disc ID extraction from CD-ROM
3. Implement matching algorithm
4. Add fallback for unknown games
5. Support for multiple disc games

## Migration from GameID

The system no longer requires:
- Python GameID script
- `/media/fat/Scripts/_GameID/` directory
- `/media/fat/gameID/db.pkl.gz` database file

Instead, it uses:
- `/media/fat/GameDB/` directory
- Direct JSON database files
- Native C++ parsing