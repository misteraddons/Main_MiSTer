## Git Workflow
- Always create a new branch from the misteraddons/master branch, and publish the repository to my fork of Main MiSTer

## Development Preferences
- Use unix line endings

## Interaction Guidelines
- Never mention claude or coauthor

## CD-ROM Auto-Detection System TODO

### High Priority Tasks
- [x] Add configuration improvements (cdrom_autoload_delay, cdrom_auto_select, cdrom_preferred_region)
- [x] Implement fuzzy game matching with partial titles and typo tolerance
- [x] Create OSD selection popup for multiple game matches

### Medium Priority Tasks
- [x] Add CD audio player functionality for audio CDs
- [x] Implement CD reading/ripping functionality (cdrdao-like)
- [ ] Complete PC Engine CD detection and support
- [ ] Complete Neo Geo CD detection and support
- [ ] Add 3DO system detection and support
- [ ] Add CD-i system detection and support

### System Detection Status
- [x] SegaCD/MegaCD - Complete
- [x] Saturn - Complete
- [x] PlayStation (PSX) - Complete
- [ ] PC Engine CD - In progress
- [ ] Neo Geo CD - In progress
- [ ] 3DO - Planned
- [ ] CD-i - Planned

### Current Implementation Notes
- cdrom_autoload defaults to 0 (disabled)
- cdrom_autoload_delay defaults to 0 seconds (range: 0-10)
- cdrom_auto_select defaults to 0 (show selection menu)
- cdrom_preferred_region defaults to "USA"
- MGL files are created with game names and automatically cleaned up on disc eject
- Menu refresh triggers automatically after MGL creation
- CD-ROM detection runs every 5 seconds when in main menu only
- Fork-based background detection prevents input blocking

### Configuration Options Added
```ini
cdrom_autoload=1              # Enable/disable auto-loading (0/1, default: 0)
cdrom_autoload_delay=0        # Seconds to wait before auto-loading (0-10, default: 0)
cdrom_auto_select=1           # Auto-select first match (0=show selection, 1=auto-select, default: 0)
cdrom_preferred_region=USA    # Preferred region (No-Intro format, default: USA)
```

### Region Priority System
When multiple region versions exist, the system uses this priority:
1. User's preferred region (from cdrom_preferred_region)
2. USA (includes Canada)
3. Europe (includes Australia)
4. Japan
5. World (all regions)
6. Other regions alphabetically

Valid region values (No-Intro convention):
- Single regions: USA, Europe, Japan, Australia, Brazil, Canada, China, France, Germany, Hong Kong, Italy, Korea, Netherlands, Spain, Sweden
- Multi-regions: World, Asia, (Japan, USA), (Japan, Europe), (USA, Europe)

### Technical Notes
- Selection popup MGL files use simple numeric prefixes (1-GameName.mgl, 2-GameName.mgl, etc.)
- Audio CD MGL files use embedded metadata: "Artist - Album.mgl" or disc ID fallback
- Originally intended to use \x97 character for CD icon display, but not compatible with FAT32 filesystems
- Cleanup commands use [0-9]*.mgl pattern to remove numbered selection files and "Audio*.mgl" for audio CDs

### CD Audio Player Implementation
- Complete backend functionality with cdaudio_play, cdaudio_stop, cdaudio_pause, cdaudio_info commands
- Automatic audio CD detection and MGL creation with intelligent naming
- Embedded metadata extraction: CD-Text (artist/album), MCN/UPC, subchannel data
- Smart naming priority: Artist - Album > Disc ID > Generic fallback
- TOC-based disc fingerprinting generates unique CDDB-style disc IDs
- MGL files named as "Artist - Album.mgl" when CD-Text available
- References _Utility/CD_Audio_Player core (requires separate utility core installation)
- Direct device access via /dev/sr0 for real-time audio playback
- Extracts track count, timing, offsets, and disc length from Table of Contents (TOC)
- Generates CDDB-compatible disc IDs for potential metadata lookup integration
- Enhanced cleanup: tracks artist/album named MGLs for proper removal on disc eject
- OSD refresh triggered after cleanup to update file list immediately
- Persistent disc detection using /tmp/cd_present flag to skip rescans during MiSTer.ini reloads

### Known Issues and Solutions
- **Double scanning**: The system currently scans the disc twice - once for detection and once for identification. Future optimization should cache the identification results.
- **Game search depth**: Recursive search may not find games in deep subdirectories like `_Favorites/_Fighting/`. Consider flattening the search or increasing recursion depth.
- **Search pattern matching**: Games in subdirectories with different naming patterns may not be found. Ensure the search covers all common directory structures.

Note: The _Utility/CD_Audio_Player core is not included in standard MiSTer distribution. 
The infrastructure is ready but requires the audio player utility core to be developed.

### CD Reading/Ripping Tools
- Advanced CD reading capabilities beyond basic dd using Linux CD-ROM ioctls
- Audio track extraction to WAV format with proper headers
- Complete CUE/BIN disc imaging for mixed mode and multi-track CDs
- Raw sector reading with error handling and retry mechanisms
- Commands: cd_rip_track, cd_create_cue, cd_read_raw
- Progress indicators and sector-level error recovery
- TOC-based track boundary detection for precise extraction
- Supports both audio and data track reading with appropriate methods

## Current Known Issues

### Performance Issues
- **Slow game search**: Find command takes 15+ seconds even with optimizations
  - Current: `find /media/fat/games/PSX -maxdepth 3 -type f -iname '*.cue'`
  - Need to investigate alternative search strategies (caching, indexing, or different search tools)
  - Tested optimizations: maxdepth 5→3, head 100→50, .cue-only search with .chd fallback

### CD-ROM Detection Issues  
- **Autoload after eject persists**: Still triggers autoload despite 500k cycle cooldown
  - Ejection detection works correctly (removes flag, clears cache, sets cooldown)
  - Cooldown prevents immediate rescan but eventually expires and finds disc again
  - Root cause: Physical disc may still be present or re-inserted, triggering legitimate detection
  - Consider: Longer cooldown, better ejection detection, or user confirmation for re-detection

### OSD Refresh Issues
- **Menu refresh fails sometimes**: HOME key (102) doesn't always refresh the file list
  - Reduced delay from 2.5s to 0.5s to prevent input freeze
  - May need different refresh strategy or timing
  - Consider: Multiple HOME key presses, different key codes, or alternative refresh methods

### Search Clustering
- **Clustering gap**: Currently set to 10 points, may need fine-tuning based on score distributions
- **Score calculation**: 60% base + 30% series + 10% region weights may need adjustment

### Debug and Logging
- **Verbose cooldown messages**: Debug spam during cooldown period (reduced to every 100k cycles)
- **Missing timing info**: Need better performance profiling for search operations