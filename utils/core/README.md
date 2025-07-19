# Core Services

## game_launcher
Central game launching service that handles all game launch requests from various input sources.
- Manages GameID lookups
- Creates MGL files
- Handles favorites, ratings, playtime tracking
- Provides smart recommendations

## cdrom_daemon
Monitors CD-ROM drive for disc changes and automatically launches games.
- Extracts serial numbers from PSX/Saturn/SegaCD discs
- Integrates with game_launcher for game identification
- Supports automatic CD ripping with cdrdao
