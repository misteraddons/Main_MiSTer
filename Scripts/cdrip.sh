#!/bin/bash

# CD-ROM Ripping Script for MiSTer
# Automatically detects game using GameID and rips to proper folder structure
# Usage: ./cdrip.sh [output_directory]

# Configuration
CDROM_DEVICE="/dev/sr0"
DEFAULT_OUTPUT_DIR="/media/fat/games"
GAMEID_DIR="/media/fat/gameID"
SCRIPTS_DIR="/media/fat/Scripts/_GameID"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to sanitize filename
sanitize_filename() {
    local name="$1"
    
    # Check if name is empty
    if [ -z "$name" ]; then
        echo "Unknown_Game"
        return
    fi
    
    # Remove or replace problematic characters
    local sanitized=$(echo "$name" | sed 's/[<>:"/\\|?*]/_/g' | sed 's/[[:space:]]\+/_/g' | sed 's/_\+/_/g' | sed 's/^_\|_$//g')
    
    # Ensure we don't end up with empty string
    if [ -z "$sanitized" ]; then
        echo "Unknown_Game"
    else
        echo "$sanitized"
    fi
}

# Function to detect disc system using magic words
detect_system() {
    local device="$1"
    
    # Try to read first sector
    local sector_data=$(dd if="$device" bs=2048 count=1 2>/dev/null | od -A n -t x1 | tr -d ' \n')
    
    if [ -z "$sector_data" ]; then
        print_error "Could not read from CD-ROM device"
        return 1
    fi
    
    # Check for Saturn magic word (SEGA SEGASATURN)
    if dd if="$device" bs=1 skip=0 count=32 2>/dev/null | grep -q "SEGA SEGASATURN"; then
        echo "Saturn"
        return 0
    fi
    
    # Check for PCE-CD magic word
    if dd if="$device" bs=1 skip=32 count=32 2>/dev/null | grep -q "PC Engine CD-ROM"; then
        echo "PCECD"
        return 0
    fi
    
    # Check for Sega CD magic word
    if dd if="$device" bs=1 skip=0 count=32 2>/dev/null | grep -q "SEGADISCSYSTEM"; then
        echo "MegaCD"
        return 0
    fi
    
    # Check for Neo Geo CD
    if dd if="$device" bs=1 skip=0 count=16 2>/dev/null | grep -q "NEO-GEO"; then
        echo "NeoGeoCD"
        return 0
    fi
    
    # Default fallback
    print_warning "Could not detect system type, defaulting to Saturn"
    echo "Saturn"
    return 0
}

# Function to extract disc ID
extract_disc_id() {
    local device="$1"
    local system="$2"
    local disc_id=""
    
    print_status "extract_disc_id: device=$device, system=$system" >&2
    
    case "$system" in
        "Saturn")
            print_status "Extracting Saturn disc ID from offset 32-43" >&2
            # Saturn disc ID is at offset 32-43
            disc_id=$(dd if="$device" bs=1 skip=32 count=12 2>/dev/null | tr -d '\0' | tr -d ' ')
            print_status "Raw Saturn disc ID: '$disc_id'" >&2
            ;;
        "PCECD")
            print_status "Extracting PCE-CD disc ID from offset 128" >&2
            # PCE-CD uses different method - try to find product code
            disc_id=$(dd if="$device" bs=1 skip=128 count=16 2>/dev/null | tr -d '\0' | tr -d ' ')
            print_status "Raw PCE-CD disc ID: '$disc_id'" >&2
            ;;
        "MegaCD")
            print_status "Extracting Sega CD disc ID from offset 16" >&2
            # Sega CD disc ID
            disc_id=$(dd if="$device" bs=1 skip=16 count=16 2>/dev/null | tr -d '\0' | tr -d ' ')
            print_status "Raw Sega CD disc ID: '$disc_id'" >&2
            ;;
        *)
            print_status "Unknown system, using UNKNOWN" >&2
            disc_id="UNKNOWN"
            ;;
    esac
    
    print_status "Final disc_id: '$disc_id'" >&2
    
    if [ -n "$disc_id" ] && [ "$disc_id" != "UNKNOWN" ]; then
        echo "$disc_id"
        print_status "extract_disc_id returning 0 (success)" >&2
        return 0
    else
        print_status "extract_disc_id returning 1 (failure)" >&2
        return 1
    fi
}

# Function to lookup game in GameDB
lookup_game() {
    local system="$1"
    local disc_id="$2"
    local gamedb_file="/media/fat/GameDB/${system}.data.json"
    
    print_status "Looking for GameDB file: $gamedb_file" >&2
    print_status "Searching for disc ID: '$disc_id'" >&2
    
    if [ ! -f "$gamedb_file" ]; then
        print_warning "GameDB file not found: $gamedb_file" >&2
        return 1
    fi
    
    # Basic GameDB file info
    print_status "GameDB file has $(wc -l < "$gamedb_file") lines" >&2
    
    # Try exact match first, then variations
    local patterns=("$disc_id" "${disc_id%V*}" "${disc_id%-*}" "${disc_id%% *}")
    
    for pattern in "${patterns[@]}"; do
        print_status "Trying pattern: '$pattern'" >&2
        
        # Search for exact disc ID match first (quoted strings in JSON)
        local exact_matches=$(grep -i "\"$pattern\"" "$gamedb_file")
        
        if [ -n "$exact_matches" ]; then
            print_status "Found exact matches for '$pattern'" >&2
            
            # Try to extract title from the exact matches
            local title=$(echo "$exact_matches" | grep -i '"title"' | head -1 | sed 's/.*"title"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
            
            if [ -n "$title" ]; then
                print_status "Extracted title from exact match: '$title'" >&2
                echo "$title"
                return 0
            fi
            
            # Look for title in surrounding context for exact matches
            local line_num=$(grep -n -i "\"$pattern\"" "$gamedb_file" | head -1 | cut -d: -f1)
            if [ -n "$line_num" ]; then
                local context=$(sed -n "$((line_num-10)),$((line_num+10))p" "$gamedb_file")
                title=$(echo "$context" | grep -i '"title"' | head -1 | sed 's/.*"title"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
                
                if [ -n "$title" ]; then
                    print_status "Found title in exact match context: '$title'" >&2
                    echo "$title"
                    return 0
                fi
            fi
        fi
        
        # If no exact match, try partial match (but be more careful)
        local partial_matches=$(grep -i "$pattern" "$gamedb_file")
        
        if [ -n "$partial_matches" ]; then
            print_status "Found partial matches for '$pattern'" >&2
            
            # Only use partial matches if we haven't found exact matches
            local title=$(echo "$partial_matches" | grep -i '"title"' | head -1 | sed 's/.*"title"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
            
            if [ -n "$title" ]; then
                print_status "Extracted title from partial match: '$title'" >&2
                echo "$title"
                return 0
            fi
        fi
        
        print_status "No matches found for pattern: '$pattern'" >&2
    done
    
    print_warning "No matching game found in GameDB" >&2
    return 1
}

# Function to check if tools are available
check_tools() {
    if ! command -v dd >/dev/null 2>&1; then
        print_error "dd command not found - this is required for ripping"
        exit 1
    fi
    
    if command -v cdrdao >/dev/null 2>&1; then
        print_status "cdrdao found - will use professional ripping method"
        return 0
    else
        print_status "cdrdao not found - will use dd-based ripping method"
        return 1
    fi
}

# Function to rip CD using dd
rip_with_dd() {
    local output_file="$1"
    local cue_file="$2"
    local toc_file="$3"
    local game_name="$4"
    
    print_status "Starting CD rip using dd method..."
    print_status "Output file: $output_file"
    
    # Rip the disc
    if dd if="$CDROM_DEVICE" of="$output_file" bs=2352 conv=noerror,sync status=progress 2>&1; then
        print_success "CD rip completed successfully"
        
        # Create CUE file
        cat > "$cue_file" << EOF
FILE "$(basename "$output_file")" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
EOF
        
        # Create basic TOC file
        cat > "$toc_file" << EOF
CD_DA
TRACK MODE1/2352
DATAFILE "$(basename "$output_file")"
EOF
        
        print_success "Created CUE and TOC files"
        return 0
    else
        print_error "CD rip failed"
        return 1
    fi
}

# Function to rip CD using cdrdao
rip_with_cdrdao() {
    local bin_file="$1"
    local cue_file="$2"
    local toc_file="$3"
    local game_name="$4"
    
    print_status "Starting CD rip using cdrdao method..."
    
    if cdrdao read-cd --read-raw --datafile "$bin_file" --device "$CDROM_DEVICE" "$toc_file" 2>&1; then
        print_success "CD rip completed successfully with cdrdao"
        
        # Convert TOC to CUE if toc2cue is available
        if command -v toc2cue >/dev/null 2>&1; then
            toc2cue "$toc_file" "$cue_file" 2>/dev/null
        else
            # Create basic CUE file
            cat > "$cue_file" << EOF
FILE "$(basename "$bin_file")" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
EOF
        fi
        
        print_success "Created CUE file"
        return 0
    else
        print_error "CD rip failed with cdrdao"
        return 1
    fi
}

# Main function
main() {
    local output_dir="${1:-$DEFAULT_OUTPUT_DIR}"
    
    print_status "CD-ROM Ripping Script for MiSTer"
    print_status "=================================="
    
    # Check if CD-ROM device exists
    if [ ! -e "$CDROM_DEVICE" ]; then
        print_error "CD-ROM device $CDROM_DEVICE not found"
        exit 1
    fi
    
    # Check if disc is inserted
    if ! dd if="$CDROM_DEVICE" bs=1 count=1 >/dev/null 2>&1; then
        print_error "No disc inserted or disc not readable"
        exit 1
    fi
    
    print_success "CD-ROM device found and disc is readable"
    
    # Detect system
    print_status "Detecting disc system..."
    local system=$(detect_system "$CDROM_DEVICE")
    print_success "Detected system: $system"
    
    # Extract disc ID
    print_status "Extracting disc ID..."
    local disc_id
    disc_id=$(extract_disc_id "$CDROM_DEVICE" "$system")
    local extract_result=$?
    
    print_status "extract_disc_id returned: code=$extract_result, disc_id='$disc_id'"
    
    if [ $extract_result -eq 0 ] && [ -n "$disc_id" ]; then
        print_success "Extracted disc ID: $disc_id"
        
        # First, let's manually check if the GameDB file exists
        local gamedb_file="/media/fat/GameDB/${system}.data.json"
        print_status "Checking GameDB file: $gamedb_file"
        if [ -f "$gamedb_file" ]; then
            print_success "GameDB file exists"
            print_status "File size: $(wc -l < "$gamedb_file") lines"
            print_status "First few lines:"
            head -5 "$gamedb_file"
        else
            print_error "GameDB file not found: $gamedb_file"
            print_status "Available files in /media/fat/GameDB/:"
            ls -la /media/fat/GameDB/ 2>/dev/null || print_error "GameDB directory not found"
        fi
        
        # Manual search for the disc ID
        print_status "Manual search for disc ID '$disc_id' in GameDB..."
        if [ -f "$gamedb_file" ]; then
            local manual_matches=$(grep -i "$disc_id" "$gamedb_file" | head -3)
            if [ -n "$manual_matches" ]; then
                print_success "Manual search found matches:"
                echo "$manual_matches"
            else
                print_warning "Manual search found no matches for '$disc_id'"
                # Try without version suffix
                local clean_id="${disc_id%V*}"
                print_status "Trying without version suffix: '$clean_id'"
                manual_matches=$(grep -i "$clean_id" "$gamedb_file" | head -3)
                if [ -n "$manual_matches" ]; then
                    print_success "Found matches for '$clean_id':"
                    echo "$manual_matches"
                else
                    print_warning "No matches found for '$clean_id' either"
                fi
            fi
        fi
        
        # Now try the function
        print_status "Calling lookup_game function..."
        local game_title
        game_title=$(lookup_game "$system" "$disc_id")
        local lookup_result=$?
        
        print_status "Function returned: code=$lookup_result, title='$game_title'"
        
        if [ $lookup_result -ne 0 ] || [ -z "$game_title" ]; then
            print_warning "Game not found in database, using disc ID as name"
            game_title="$disc_id"
        fi
    else
        print_warning "Could not extract disc ID, using generic name"
        game_title="Unknown_Game_$(date +%Y%m%d_%H%M%S)"
    fi
    
    # Final check to ensure we have a valid game title
    if [ -z "$game_title" ]; then
        print_warning "Game title is empty, using fallback name"
        game_title="CD_Game_$(date +%Y%m%d_%H%M%S)"
    fi
    
    # Sanitize game name
    local sanitized_name=$(sanitize_filename "$game_title")
    print_status "Sanitized filename: $sanitized_name"
    
    # Create output directory
    local game_dir="$output_dir/$system/$sanitized_name"
    print_status "Creating output directory: $game_dir"
    
    if ! mkdir -p "$game_dir"; then
        print_error "Failed to create output directory"
        exit 1
    fi
    
    # Define output files
    local bin_file="$game_dir/$sanitized_name.bin"
    local cue_file="$game_dir/$sanitized_name.cue"
    local toc_file="$game_dir/$sanitized_name.toc"
    
    # Check available tools and rip
    if check_tools; then
        # Use cdrdao
        if rip_with_cdrdao "$bin_file" "$cue_file" "$toc_file" "$sanitized_name"; then
            print_success "Ripping completed successfully!"
        else
            print_warning "cdrdao failed, falling back to dd method"
            if rip_with_dd "$bin_file" "$cue_file" "$toc_file" "$sanitized_name"; then
                print_success "Ripping completed successfully with dd!"
            else
                print_error "All ripping methods failed"
                exit 1
            fi
        fi
    else
        # Use dd only
        if rip_with_dd "$bin_file" "$cue_file" "$toc_file" "$sanitized_name"; then
            print_success "Ripping completed successfully!"
        else
            print_error "Ripping failed"
            exit 1
        fi
    fi
    
    # Display results
    print_success "=== RIPPING COMPLETED ==="
    print_status "Game: $game_title"
    print_status "System: $system"
    print_status "Output directory: $game_dir"
    print_status "Files created:"
    ls -la "$game_dir"
    
    print_success "Game is ready to play!"
}

# Run main function with all arguments
main "$@"