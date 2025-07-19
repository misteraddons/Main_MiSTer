#!/bin/bash
#
# Build script for Game Announcer
#

echo "Building MiSTer Game Announcer..."

# Build flags
CFLAGS="-Wall -Wextra -std=c++11 -O2"
LDFLAGS="-ljsoncpp"

# Check for JsonCpp library
if ! pkg-config --exists jsoncpp; then
    echo "Warning: JsonCpp library not found. GameDB lookup may not work."
    echo "Install with: sudo apt install libjsoncpp-dev"
    LDFLAGS=""
fi

# Build game announcer
echo "Building game_announcer..."
g++ $CFLAGS -o game_announcer game_announcer.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ Game Announcer built successfully"
    strip game_announcer
else
    echo "✗ Failed to build Game Announcer"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "Installation:"
echo "  sudo cp game_announcer /media/fat/utils/"
echo "  sudo cp game_announcer.conf /media/fat/utils/"
echo ""
echo "Usage:"
echo "  # Start game announcer"
echo "  /media/fat/utils/game_announcer &"
echo ""
echo "  # Start in foreground (for debugging)"
echo "  /media/fat/utils/game_announcer -f"
echo ""
echo "Features:"
echo "  - Monitors MGL files for game changes"
echo "  - GameDB integration for rich game info"
echo "  - Real-time announcements via FIFO"
echo "  - Automatic game detection"
echo ""
echo "Announcements are sent to /dev/MiSTer_announcements"
echo "Other daemons can read this FIFO to forward announcements."