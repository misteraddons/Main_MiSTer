# MiSTer CD-ROM Daemon Directory Structure

## Overview
The CD-ROM daemon now uses a centralized directory structure under `/media/fat/utils/` to keep all related files organized.

## Directory Layout

```
/media/fat/utils/
├── cdrom_daemon                     # Main daemon executable
├── cdrom_daemon.conf                # User configuration (optional)
├── cdrom_daemon.conf.sample         # Sample configuration
├── migrate_gamedb.sh                # GameDB migration script
├── gamedb/                          # GameDB database files
│   ├── PSX.data.json               # PlayStation games database
│   ├── Saturn.data.json            # Sega Saturn games database
│   ├── SegaCD.data.json            # Sega CD games database
│   └── PCECD.data.json             # PC Engine CD games database
└── redump/                          # Redump database files (future)
    ├── psx_redump.db
    ├── saturn_redump.db
    ├── megacd_redump.db
    └── pcecd_redump.db
```

## Installation

This is a new system - just install the daemon and place your GameDB files in `/media/fat/utils/gamedb/`:

1. Install the daemon: `sudo ./install_cdrom_daemon.sh`
2. Place GameDB files in `/media/fat/utils/gamedb/`
3. Configure if needed: `cp cdrom_daemon.conf.sample cdrom_daemon.conf`

## Benefits of New Structure

1. **Organization**: All daemon files in one place
2. **Maintainability**: Easier to backup/restore daemon data
3. **Clarity**: Clear separation from core MiSTer files
4. **Expandability**: Room for future features (Redump, etc.)
5. **User-friendly**: Easier to find and manage configuration

## Configuration

The daemon works with default paths, but you can customize in `cdrom_daemon.conf`:

```ini
[Paths]
games_dir = /media/fat/games
gamedb_dir = /media/fat/utils/gamedb
temp_dir = /tmp
```

## Compatibility

The daemon automatically detects the directory structure and falls back gracefully if GameDB files are missing, displaying helpful error messages to guide users.