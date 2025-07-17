#!/bin/bash
# ROM Patch Organizer for MiSTer (Bash version)
# Converts scraped ROM hack structure to proper MiSTer ROM patches format

set -e

ROM_PATCHES_DIR=""
GAMES_DIR=""
OUTPUT_DIR=""
DRY_RUN=false

usage() {
    echo "Usage: $0 -s <source_rom_patches_dir> -g <games_dir> -o <output_dir> [-d]"
    echo "  -s: Source ROM patches directory"
    echo "  -g: Games directory to match ROMs against"
    echo "  -o: Output directory for organized patches"
    echo "  -d: Dry run (show what would be done)"
    exit 1
}

while getopts "s:g:o:dh" opt; do
    case $opt in
        s) ROM_PATCHES_DIR="$OPTARG" ;;
        g) GAMES_DIR="$OPTARG" ;;
        o) OUTPUT_DIR="$OPTARG" ;;
        d) DRY_RUN=true ;;
        h) usage ;;
        *) usage ;;
    esac
done

if [[ -z "$ROM_PATCHES_DIR" || -z "$GAMES_DIR" || -z "$OUTPUT_DIR" ]]; then
    usage
fi

# Function to extract CRC32 from scraped_info.txt
extract_crc32() {
    local info_file="$1"
    grep "Checksum (CRC32):" "$info_file" | sed 's/.*0x\([0-9a-fA-F]*\).*/\1/' | tr '[:lower:]' '[:upper:]'
}

# Function to extract ROM name from scraped_info.txt
extract_rom_name() {
    local info_file="$1"
    grep "^\s*\*" "$info_file" | head -1 | sed 's/^\s*\*\s*//' | sed 's/\s*$//'
}

# Function to find best matching ROM file
find_best_rom_match() {
    local rom_name="$1"
    local games_dir="$2"
    
    # Clean ROM name for matching (remove special characters)
    local clean_name=$(echo "$rom_name" | sed 's/[^a-zA-Z0-9 ]//g' | tr '[:upper:]' '[:lower:]')
    
    # Find ROM files and calculate similarity
    local best_match=""
    local best_score=0
    
    find "$games_dir" -type f \( -iname "*.sfc" -o -iname "*.smc" -o -iname "*.nes" -o -iname "*.gb" -o -iname "*.gbc" -o -iname "*.gba" -o -iname "*.md" -o -iname "*.gen" \) | while read -r rom_file; do
        local basename=$(basename "$rom_file")
        local clean_basename=$(echo "${basename%.*}" | sed 's/[^a-zA-Z0-9 ]//g' | tr '[:upper:]' '[:lower:]')
        
        # Simple similarity check using common words
        local common_words=0
        local total_words=0
        
        for word in $clean_name; do
            if [[ ${#word} -gt 2 ]]; then
                total_words=$((total_words + 1))
                if echo "$clean_basename" | grep -q "$word"; then
                    common_words=$((common_words + 1))
                fi
            fi
        done
        
        if [[ $total_words -gt 0 ]]; then
            local score=$((common_words * 100 / total_words))
            if [[ $score -gt $best_score && $score -ge 70 ]]; then
                best_score=$score
                best_match="$rom_file"
            fi
        fi
    done
    
    echo "$best_match"
}

# Main processing
PROCESSED=0
SKIPPED=0

echo "ROM Patch Organizer for MiSTer"
echo "Source: $ROM_PATCHES_DIR"
echo "Games: $GAMES_DIR"
echo "Output: $OUTPUT_DIR"
echo "Dry run: $DRY_RUN"
echo ""

if [[ "$DRY_RUN" == "false" ]]; then
    mkdir -p "$OUTPUT_DIR"
fi

# Process each core directory
for core_dir in "$ROM_PATCHES_DIR"/*; do
    if [[ ! -d "$core_dir" ]]; then
        continue
    fi
    
    core_name=$(basename "$core_dir")
    echo "Processing core: $core_name"
    
    # Process each ROM directory
    for rom_dir in "$core_dir"/*; do
        if [[ ! -d "$rom_dir" ]]; then
            continue
        fi
        
        rom_name=$(basename "$rom_dir")
        echo "  Processing ROM: $rom_name"
        
        # Process each patch directory
        for patch_dir in "$rom_dir"/*; do
            if [[ ! -d "$patch_dir" ]]; then
                continue
            fi
            
            patch_name=$(basename "$patch_dir")
            echo "    Processing patch: $patch_name"
            
            # Check for scraped_info.txt
            if [[ ! -f "$patch_dir/scraped_info.txt" ]]; then
                echo "      Warning: No scraped_info.txt found, skipping"
                SKIPPED=$((SKIPPED + 1))
                continue
            fi
            
            # Extract ROM name and CRC32
            scraped_rom_name=$(extract_rom_name "$patch_dir/scraped_info.txt")
            crc32=$(extract_crc32 "$patch_dir/scraped_info.txt")
            
            if [[ -z "$scraped_rom_name" || -z "$crc32" ]]; then
                echo "      Warning: Could not parse ROM name or CRC32, skipping"
                SKIPPED=$((SKIPPED + 1))
                continue
            fi
            
            echo "      ROM: $scraped_rom_name, CRC32: $crc32"
            
            # Find matching ROM file
            rom_match=$(find_best_rom_match "$scraped_rom_name" "$GAMES_DIR")
            
            if [[ -z "$rom_match" ]]; then
                echo "      Warning: No matching ROM found, skipping"
                SKIPPED=$((SKIPPED + 1))
                continue
            fi
            
            echo "      Best match: $(basename "$rom_match")"
            
            # Create proper folder name
            rom_basename=$(basename "$rom_match")
            rom_basename="${rom_basename%.*}"  # Remove extension
            proper_folder_name="${rom_basename} [${crc32}]"
            
            # Create output directory
            output_core_dir="$OUTPUT_DIR/$core_name"
            output_rom_dir="$output_core_dir/$proper_folder_name"
            
            echo "      Output: $output_rom_dir"
            
            if [[ "$DRY_RUN" == "false" ]]; then
                mkdir -p "$output_rom_dir"
            fi
            
            # Process patch files
            patch_files_found=false
            
            # Extract ZIP files and copy patch files
            for item in "$patch_dir"/*; do
                if [[ -f "$item" ]]; then
                    case "$item" in
                        *.zip)
                            echo "        Extracting: $(basename "$item")"
                            if [[ "$DRY_RUN" == "false" ]]; then
                                # Extract patch files from ZIP
                                temp_dir=$(mktemp -d)
                                unzip -q "$item" -d "$temp_dir"
                                find "$temp_dir" -type f \( -name "*.bps" -o -name "*.ips" -o -name "*.ups" -o -name "*.xdelta" -o -name "*.delta" \) -exec cp {} "$output_rom_dir/" \;
                                rm -rf "$temp_dir"
                                patch_files_found=true
                            fi
                            ;;
                        *.bps|*.ips|*.ups|*.xdelta|*.delta)
                            echo "        Copying patch: $(basename "$item")"
                            if [[ "$DRY_RUN" == "false" ]]; then
                                cp "$item" "$output_rom_dir/"
                                patch_files_found=true
                            fi
                            ;;
                    esac
                fi
            done
            
            # Copy and rename info files
            if [[ "$DRY_RUN" == "false" ]]; then
                cp "$patch_dir/scraped_info.txt" "$output_rom_dir/${patch_name}_info.txt"
                
                if [[ -f "$patch_dir/og_readme.txt" ]]; then
                    cp "$patch_dir/og_readme.txt" "$output_rom_dir/${patch_name}_readme.txt"
                fi
            fi
            
            if [[ "$patch_files_found" == "true" || "$DRY_RUN" == "true" ]]; then
                PROCESSED=$((PROCESSED + 1))
                echo "      ✓ Processed successfully"
            else
                echo "      Warning: No patch files found"
                SKIPPED=$((SKIPPED + 1))
            fi
        done
    done
done

echo ""
echo "=== Summary ==="
echo "Processed: $PROCESSED"
echo "Skipped: $SKIPPED"
if [[ "$DRY_RUN" == "true" ]]; then
    echo "DRY RUN - No files were actually moved/copied"
fi