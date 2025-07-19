#!/bin/bash
#
# Build script for MiSTer Game Launcher System
# Builds all components of the modular launcher system
#

echo "Building MiSTer Game Launcher System..."
echo "======================================="

# Create build directory
mkdir -p build

# Build flags
CFLAGS="-Wall -Wextra -std=c99 -O2"
LDFLAGS="-lpthread"

# Build Game Launcher Service
echo "Building Game Launcher Service..."
if command -v pkg-config &> /dev/null && pkg-config --exists json-c; then
    JSON_CFLAGS=$(pkg-config --cflags json-c)
    JSON_LIBS=$(pkg-config --libs json-c)
    
    gcc $CFLAGS $JSON_CFLAGS -o build/game_launcher_service game_launcher_service.c $LDFLAGS $JSON_LIBS
    
    if [ $? -eq 0 ]; then
        echo "✓ Game Launcher Service built successfully"
    else
        echo "✗ Failed to build Game Launcher Service"
        exit 1
    fi
else
    echo "⚠ Warning: json-c not found, skipping Game Launcher Service"
fi

# Build NFC Daemon
echo "Building NFC Daemon..."
if command -v pkg-config &> /dev/null && pkg-config --exists libnfc json-c; then
    NFC_CFLAGS=$(pkg-config --cflags libnfc json-c)
    NFC_LIBS=$(pkg-config --libs libnfc json-c)
    
    gcc $CFLAGS $NFC_CFLAGS -o build/nfc_daemon nfc_daemon.c $LDFLAGS $NFC_LIBS
    
    if [ $? -eq 0 ]; then
        echo "✓ NFC Daemon built successfully"
    else
        echo "✗ Failed to build NFC Daemon"
    fi
else
    echo "⚠ Warning: libnfc not found, skipping NFC Daemon"
fi

# Build Network Daemon
echo "Building Network Daemon..."
if command -v pkg-config &> /dev/null && pkg-config --exists json-c; then
    NET_CFLAGS=$(pkg-config --cflags json-c)
    NET_LIBS=$(pkg-config --libs json-c)
    
    gcc $CFLAGS $NET_CFLAGS -o build/network_daemon network_daemon.c $LDFLAGS $NET_LIBS
    
    if [ $? -eq 0 ]; then
        echo "✓ Network Daemon built successfully"
    else
        echo "✗ Failed to build Network Daemon"
    fi
else
    echo "⚠ Warning: json-c not found, skipping Network Daemon"
fi

# Build GPIO Daemon
echo "Building GPIO Daemon..."
gcc $CFLAGS -o build/gpio_daemon gpio_daemon.c $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ GPIO Daemon built successfully"
else
    echo "✗ Failed to build GPIO Daemon"
fi

# Build existing CD-ROM Daemon
echo "Building CD-ROM Daemon..."
gcc $CFLAGS -o build/cdrom_daemon cdrom_daemon.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ CD-ROM Daemon built successfully"
else
    echo "✗ Failed to build CD-ROM Daemon"
fi

# Strip binaries
echo "Stripping binaries..."
strip build/* 2>/dev/null

echo ""
echo "Build Summary:"
echo "=============="
ls -la build/

echo ""
echo "Installation:"
echo "============="
echo "Run ./install_launcher_system.sh to install all components"

echo ""
echo "Components built in build/ directory:"
echo "- game_launcher_service  # Core GameDB/MGL service"
echo "- nfc_daemon            # NFC card reader support"
echo "- network_daemon        # HTTP API for remote control"
echo "- gpio_daemon           # GPIO button support"
echo "- cdrom_daemon          # Physical CD-ROM support"