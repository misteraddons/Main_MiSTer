#!/bin/bash
#
# Build script for MiSTer Game Launcher Service
#

echo "Building MiSTer Game Launcher Service..."

# Build flags
CFLAGS="-Wall -Wextra -std=c++11 -O2"
LDFLAGS="-lpthread"

# Build game launcher
echo "Building game_launcher..."
g++ $CFLAGS -o game_launcher game_launcher.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ Game Launcher built successfully"
    
    # Strip binary
    strip game_launcher
    
    # Show file info
    echo ""
    echo "Binary information:"
    ls -la game_launcher
    echo ""
    
    echo "Installation:"
    echo "  sudo cp game_launcher /media/fat/utils/"
    echo "  sudo cp game_launcher.conf /media/fat/utils/"
    echo ""
    echo "Usage:"
    echo "  /media/fat/utils/game_launcher &"
    echo ""
    echo "Test commands:"
    echo "  echo 'PSX:serial:SLUS-00067:test' > /dev/MiSTer_game_launcher"
    echo "  echo 'Saturn:title:Panzer Dragoon Saga:test' > /dev/MiSTer_game_launcher"
    
else
    echo "✗ Failed to build Game Launcher"
    exit 1
fi