#!/bin/bash
#
# Build script for NFC tools
#

echo "Building MiSTer NFC Tools..."

# Build flags
CFLAGS="-Wall -Wextra -std=c++11 -O2"
LDFLAGS="-lpthread"

# Build NFC tag writer
echo "Building nfc_tag_writer..."
g++ $CFLAGS -o nfc_tag_writer nfc_tag_writer.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ NFC Tag Writer built successfully"
    strip nfc_tag_writer
else
    echo "✗ Failed to build NFC Tag Writer"
    exit 1
fi

# Build updated NFC daemon
echo "Building nfc_daemon..."
g++ $CFLAGS -o nfc_daemon nfc_daemon.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ NFC Daemon built successfully"
    strip nfc_daemon
else
    echo "✗ Failed to build NFC Daemon"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "Installation:"
echo "  sudo cp nfc_daemon /media/fat/utils/"
echo "  sudo cp nfc_tag_writer /media/fat/utils/"
echo "  sudo cp nfc_daemon.conf /media/fat/utils/"
echo ""
echo "Usage:"
echo "  # Start NFC daemon"
echo "  /media/fat/utils/nfc_daemon &"
echo ""
echo "  # Write a tag"
echo "  /media/fat/utils/nfc_tag_writer -w -c PSX -g \"SLUS-00067\""
echo ""
echo "  # Read a tag"  
echo "  /media/fat/utils/nfc_tag_writer -r"
echo ""
echo "  # Erase a tag"
echo "  /media/fat/utils/nfc_tag_writer -e"