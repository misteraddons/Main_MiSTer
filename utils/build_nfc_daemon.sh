#!/bin/bash
#
# Build script for NFC daemon with PN532 support
#

echo "Building MiSTer NFC Daemon..."

# Build flags
CFLAGS="-Wall -Wextra -std=c++11 -O2"
LDFLAGS="-lpthread"

# Build NFC daemon
echo "Building nfc_daemon..."
g++ $CFLAGS -o nfc_daemon nfc_daemon.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ NFC Daemon built successfully"
    
    # Strip binary
    strip nfc_daemon
    
    # Show file info
    echo ""
    echo "Binary information:"
    ls -la nfc_daemon
    echo ""
    
    echo "Installation:"
    echo "  sudo cp nfc_daemon /media/fat/utils/"
    echo "  sudo cp nfc_daemon.conf /media/fat/utils/"
    echo ""
    echo "Usage:"
    echo "  /media/fat/utils/nfc_daemon &"
    echo ""
    echo "Hardware Setup:"
    echo "  - Connect PN532 via USB-to-serial adapter to /dev/ttyUSB0"
    echo "  - Or connect PN532 via I2C to /dev/i2c-0 (address 0x24)"
    echo "  - Daemon will auto-detect interface on startup"
    echo ""
    echo "Tag Programming:"
    echo "  - Write 32-byte structure: Magic(4) + Core(8) + Game_ID(16) + Type(1) + Reserved(3)"
    echo "  - Example: 'NFC1PSX\\0\\0\\0\\0\\0SLUS-00067\\0\\0\\0\\0\\0\\0\\0\\0\\0'"
    
else
    echo "✗ Failed to build NFC Daemon"
    exit 1
fi