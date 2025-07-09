# Neo Geo CD Detection Implementation

## Overview
Added comprehensive Neo Geo CD detection to MiSTer's CD-ROM subsystem based on the GameID.py `identify_neogeocd` function reference.

## Implementation Details

### 1. Magic Word Detection (`detect_neogeocd_magic_word`)
**Location**: `cdrom.cpp` lines 587-678

**Detection Methods**:
- **Sector 0 analysis**: Searches for Neo Geo CD magic words in the first sector
- **Volume descriptor check**: Examines sector 16 (ISO 9660 Primary Volume Descriptor)
- **TOC structure analysis**: Validates track structure (audio track 1 + data track 2, 2-4 total tracks)

**Magic Words Detected**:
- `"NEO-GEO"`
- `"NEOGEO"`
- `"SNK"`
- `"COPYRIGHT SNK"`
- `"SYSTEM ROM"`
- `"BIOS"`

**Track Structure Validation**:
- Track 1: Audio (warning track)
- Track 2: Data (game data)
- Total tracks: 2-4 (typical for Neo Geo CD)

### 2. Filesystem Detection
**Location**: `cdrom.cpp` lines 331-341, 382-393

**Filesystem Indicators**:
- `"NEO-GEO.CDZ"` - Compressed Neo Geo CD format
- `"NEO-GEO.CD"` - Standard Neo Geo CD format
- `"IPL.TXT"` - Initial Program Loader text file
- `"PRG"` - Program data directory
- `"FIX"` - Fixed graphics data
- `"SPR"` - Sprite graphics data
- `"PCM"` - Audio data
- `"PAT"` - Pattern data

### 3. Disc ID Extraction (`extract_neogeocd_disc_id`)
**Location**: `cdrom.cpp` lines 1147-1285

**Identification Strategy** (based on GameID.py):
1. **Volume ID extraction**: Reads ISO 9660 volume identifier from sector 16
2. **Title file parsing**: Reads `IPL.TXT` or `TITLE.TXT` for game title
3. **Filesystem validation**: Confirms presence of Neo Geo CD file structure
4. **Generic fallback**: Uses `"NEOGEOCD"` if structure confirmed but no specific ID found

**Process Flow**:
```
1. Mount disc as ISO 9660
2. Read Primary Volume Descriptor (sector 16)
3. Extract 32-byte volume ID (offset 40)
4. Trim trailing spaces
5. If volume ID valid → use as disc ID
6. Otherwise → search for IPL.TXT/TITLE.TXT
7. Parse title files for game name
8. Fallback to "NEOGEOCD" if Neo Geo files detected
```

### 4. System Integration
**Detection Flow**:
```
detect_disc_format_and_system()
├── detect_neogeocd_magic_word() [Magic word detection]
├── Filesystem mounting and analysis
└── System assignment: "NeoGeoCD"

extract_disc_id()
├── System detection
└── extract_neogeocd_disc_id() [ID extraction]
```

## System Detection Results

### Successful Detection Returns:
- **System**: `"NeoGeoCD"`
- **Disc ID**: Volume ID, title from IPL.TXT, or "NEOGEOCD"
- **Detection Method**: Magic word or filesystem structure

### Database Lookup:
- **Database File**: `/media/fat/GameDB/NeoGeoCD.data.json`
- **Key Format**: Volume ID or title string
- **Fallback**: Generic "NEOGEOCD" identifier

## Neo Geo CD Technical Details

### Typical Structure:
- **Track 1**: Audio (warning/intro)
- **Track 2**: Data (game files)
- **Optional tracks**: Additional audio/data
- **Total tracks**: Usually 2-4

### Filesystem Layout:
```
/
├── NEO-GEO.CDZ or NEO-GEO.CD
├── IPL.TXT (game title)
├── PRG/ (program data)
├── FIX/ (fixed graphics)
├── SPR/ (sprite graphics)
├── PCM/ (audio samples)
└── PAT/ (pattern data)
```

### Volume Identifier:
- **Location**: Sector 16, offset 40
- **Length**: 32 bytes
- **Format**: ASCII, space-padded
- **Usage**: Primary identification method

## Integration with GameID.py

The implementation follows the `identify_neogeocd` function pattern:

```python
def identify_neogeocd(fn, db, user_uuid=None, user_volume_ID=None, prefer_gamedb=False):
    # Uses UUID (volume serial) and Volume ID for identification
    out = {
        'uuid': iso.get_uuid(),
        'volume_ID': iso.get_volume_ID(),
    }
    serial = (out['uuid'], out['volume_ID'])
    
    # Database lookup by serial tuple or volume ID
    if serial in db['NeoGeoCD']:
        gamedb_entry = db['NeoGeoCD'][serial]
    elif out['volume_ID'] in db['NeoGeoCD']:
        gamedb_entry = db['NeoGeoCD'][out['volume_ID']]
```

## Testing Scenarios

### Should Detect:
1. **Authentic Neo Geo CD** - Magic words + proper track structure
2. **ISO with Neo Geo filesystem** - NEO-GEO.CDZ + directory structure
3. **Homebrew Neo Geo CD** - Proper volume ID + file structure

### Should NOT Detect:
1. **Standard audio CD** - No magic words or filesystem
2. **Other console CDs** - Different magic words/structure
3. **Data CD without Neo Geo files** - Missing required files

## Build Status
- ✅ **Compiled successfully**
- ✅ **All functions implemented**
- ✅ **Header declarations added**
- ✅ **System integration complete**
- ⚠️ **Minor warnings** (format specifiers, signed/unsigned comparisons)

## Usage
1. Insert Neo Geo CD
2. MiSTer automatically detects system as "NeoGeoCD"
3. Extracts disc ID (volume ID or title)
4. Looks up game in `/media/fat/GameDB/NeoGeoCD.data.json`
5. Displays game information in menu

The implementation provides robust Neo Geo CD detection using multiple detection methods and follows the established patterns for other CD-ROM systems in MiSTer.