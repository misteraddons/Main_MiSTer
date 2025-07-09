# JSON Parser Update Summary

## Overview
The CD-ROM JSON parser in `cdrom.cpp` has been updated to support traditional multi-line formatted JSON files instead of requiring single-line JSON format.

## Changes Made

### 1. Enhanced JSON Parser Implementation
- **File**: `cdrom.cpp`
- **Function**: `search_gamedb_for_disc()`
- **Key improvements**:
  - Reads entire JSON file into memory (10MB limit for safety)
  - Properly parses multi-line, indented JSON
  - Handles nested objects and escaped quotes
  - Extracts complete JSON objects for each disc ID

### 2. Updated Data Structure
- **File**: `cdrom.h`
- **Structure**: `CDRomGameInfo`
- **Added fields**:
  - `char publisher[128]` - Publisher name
  - `char year[16]` - Release year  
  - `char product_code[32]` - Product code (e.g., HCD9008)

### 3. Helper Functions Added
- `skip_whitespace()` - Skips whitespace characters
- `find_next_quote()` - Finds next quote handling escapes
- `extract_json_string()` - Extracts string values from JSON

## JSON Format Support

### Old Format (Single-line)
```json
{"SLUS-00152":{"title":"Tomb Raider","region":"NTSC-U"},"SLUS-00594":{"title":"Tomb Raider II","region":"NTSC-U"}}
```

### New Format (Multi-line, properly formatted)
```json
{
  "SLUS-00152": {
    "title": "Tomb Raider",
    "region": "NTSC-U",
    "publisher": "Eidos Interactive",
    "year": "1996"
  },
  "SLUS-00594": {
    "title": "Tomb Raider II",
    "region": "NTSC-U",
    "publisher": "Eidos Interactive",
    "year": "1997"
  }
}
```

## Additional Files Created

1. **convert_json_format.py** - Converts single-line JSON to properly formatted multi-line JSON
2. **test_json_parser.sh** - Creates test JSON files for verification
3. **PC Engine CD Support Scripts**:
   - `pcecd_database_builder.py` - Builds PC Engine CD database from Redump sets
   - `GameID_PCECD_enhancement.py` - PC Engine CD support for GameID.py
   - `GameID_pcecd_patch.py` - Integration patch for GameID.py

## Build Status
- Successfully compiled with warnings (format specifiers and sign comparisons)
- Binary created: `MiSTer`

## Usage
1. Place properly formatted JSON files in `/media/fat/GameDB/`
2. Files should be named: `<System>.data.json` (e.g., `PSX.data.json`, `PCECD.data.json`)
3. Insert CD and the system will parse the multi-line JSON correctly

## Converting Existing Databases
```bash
python3 convert_json_format.py PSX.data.json
# Creates PSX_formatted.json with proper formatting
```

## Benefits
- Standard JSON formatting (readable, maintainable)
- Compatible with JSON editors and tools
- Easier to manage and update game databases
- Supports additional metadata fields