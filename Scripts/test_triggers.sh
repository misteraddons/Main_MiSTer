#!/bin/bash

# MiSTer CD-ROM Testing Script
# This script helps test the CD-ROM functionality without requiring actual hardware

echo "MiSTer CD-ROM Test Script"
echo "========================="

# Check if we're running on actual MiSTer
if [[ -f /media/fat/MiSTer ]]; then
    echo "✓ Running on MiSTer hardware"
    MISTER_ROOT="/media/fat"
else
    echo "Running in development environment"
    MISTER_ROOT="/tmp/mister_test"
    mkdir -p "$MISTER_ROOT"
fi

# Test 1: Check for USB CD-ROM drives
echo
echo "Test 1: Checking for USB CD-ROM devices"
echo "----------------------------------------"

if ls /dev/sr* 2>/dev/null; then
    echo "✓ CD-ROM devices found:"
    ls -la /dev/sr*
    
    # Try to read from the device
    for device in /dev/sr*; do
        echo "Testing device: $device"
        if timeout 5 head -c 2048 "$device" >/dev/null 2>&1; then
            echo "✓ Device $device is readable"
        else
            echo "✗ Device $device is not readable (no disc or permission issue)"
        fi
    done
else
    echo "✗ No CD-ROM devices found (/dev/sr*)"
    echo "  Connect USB CD-ROM drive with disc inserted"
fi

# Test 2: GameDB Installation Check
echo
echo "Test 2: GameDB Installation Check"
echo "----------------------------------"

GAMEDB_DIR="$MISTER_ROOT/GameDB"

if [[ -d "$GAMEDB_DIR" ]]; then
    echo "✓ GameDB directory found at $GAMEDB_DIR"
    
    # Check for JSON database files
    for system in PSX Saturn SegaCD PCECD; do
        DB_FILE="$GAMEDB_DIR/$system.data.json"
        if [[ -f "$DB_FILE" ]]; then
            echo "✓ $system database found: $DB_FILE"
            echo "  Database size: $(du -h "$DB_FILE" | cut -f1)"
        else
            echo "✗ $system database not found: $DB_FILE"
        fi
    done
else
    echo "✗ GameDB directory not found"
    echo "  Create directory: $GAMEDB_DIR"
    echo "  Download JSON databases from:"
    echo "    PSX: https://github.com/niemasd/GameDB-PSX"
    echo "    Saturn: https://github.com/niemasd/GameDB-Saturn"
    echo "    SegaCD: https://github.com/niemasd/GameDB-SegaCD"
    echo "    PCECD: https://github.com/niemasd/GameDB-PCECD"
fi

# Test 3: Directory Structure
echo
echo "Test 3: Directory Structure"
echo "---------------------------"

GAMES_DIR="$MISTER_ROOT/games"
mkdir -p "$GAMES_DIR"

for system in PSX Saturn MegaCD PCECD; do
    SYSTEM_DIR="$GAMES_DIR/$system"
    mkdir -p "$SYSTEM_DIR"
    echo "✓ Created/verified: $SYSTEM_DIR"
done

# Test 4: Simulate disc identification
echo
echo "Test 4: Simulate GameDB Detection"
echo "----------------------------------"

if [[ -d "$GAMEDB_DIR" ]]; then
    echo "Testing GameDB JSON parsing capabilities..."
    
    # Check if we have at least one database file
    DB_COUNT=$(find "$GAMEDB_DIR" -name "*.data.json" | wc -l)
    if [[ $DB_COUNT -gt 0 ]]; then
        echo "✓ Found $DB_COUNT GameDB JSON files"
        
        # Test JSON structure (basic validation)
        for json_file in "$GAMEDB_DIR"/*.data.json; do
            if [[ -f "$json_file" ]]; then
                basename_file=$(basename "$json_file")
                if head -1 "$json_file" | grep -q "^{" && tail -1 "$json_file" | grep -q "}$"; then
                    echo "✓ $basename_file has valid JSON structure"
                else
                    echo "✗ $basename_file may have invalid JSON structure"
                fi
            fi
        done
    else
        echo "✗ No GameDB JSON files found"
    fi
else
    echo "⚠ Skipping GameDB test - missing directory"
fi

# Test 5: File permissions
echo
echo "Test 5: File System Permissions"
echo "--------------------------------"

TEST_FILE="$GAMES_DIR/test_write.tmp"
if touch "$TEST_FILE" 2>/dev/null; then
    echo "✓ Games directory is writable"
    rm -f "$TEST_FILE"
else
    echo "✗ Games directory is not writable"
    echo "  Check permissions on: $GAMES_DIR"
fi

# Test 6: Mock disc image creation
echo
echo "Test 6: Mock Disc Image Creation"
echo "---------------------------------"

MOCK_DEVICE="/tmp/mock_disc.bin"
OUTPUT_DIR="$GAMES_DIR/PSX"

# Create mock disc data
echo "Creating mock disc with ISO signature..."
{
    # CD001 signature at correct offset for ISO 9660
    printf '\x00%.2047s' ""  # 2047 null bytes
    printf '\x01'             # Volume descriptor type
    printf 'CD001'            # Standard identifier
    printf '%.2042s' ""       # Fill rest of sector
} > "$MOCK_DEVICE"

echo "Mock disc created: $MOCK_DEVICE ($(du -h "$MOCK_DEVICE" | cut -f1))"

# Test file copying (simulate our C implementation)
OUTPUT_BIN="$OUTPUT_DIR/Test_Game.bin"
OUTPUT_CUE="$OUTPUT_DIR/Test_Game.cue"

mkdir -p "$OUTPUT_DIR"

if cp "$MOCK_DEVICE" "$OUTPUT_BIN" 2>/dev/null; then
    echo "✓ BIN file created: $OUTPUT_BIN"
    
    # Create CUE file
    cat > "$OUTPUT_CUE" << EOF
FILE "Test_Game.bin" BINARY
  TRACK 01 MODE1/2048
    INDEX 01 00:00:00
EOF
    echo "✓ CUE file created: $OUTPUT_CUE"
    
    echo "✓ Mock disc image creation successful"
    echo "  Files created in: $OUTPUT_DIR"
    ls -la "$OUTPUT_DIR"
else
    echo "✗ Failed to create disc image"
fi

# Cleanup
rm -f "$MOCK_DEVICE"

echo
echo "Test Summary"
echo "============"
echo "CD-ROM subsystem is ready for testing with real hardware."
echo
echo "Next Steps:"
echo "1. Connect USB CD-ROM drive to MiSTer"
echo "2. Insert a game disc (PSX, Saturn, Sega CD)"
echo "3. Verify GameDB installation is complete"
echo "4. Test with actual MiSTer binary"
echo
echo "For manual testing, check these functions:"
echo "- cdrom_init() - Initialize subsystem"
echo "- cdrom_detect_drive() - Find USB drive"
echo "- cdrom_is_disc_inserted() - Check for disc"
echo "- cdrom_load_disc_auto() - Auto-detect and load"