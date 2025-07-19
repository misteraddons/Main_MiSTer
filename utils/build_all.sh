#!/bin/bash
#
# Build all MiSTer addon utilities
# This script compiles all daemons and utilities for the MiSTer platform
#

set -e  # Exit on error

echo "================================="
echo "Building MiSTer Addon Utilities"
echo "================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print status
print_status() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Check if we're cross-compiling
if [ "$1" == "cross" ]; then
    print_status "Cross-compiling for ARM"
    export CC=arm-linux-gnueabihf-gcc
    export CXX=arm-linux-gnueabihf-g++
    export STRIP=arm-linux-gnueabihf-strip
else
    print_status "Building for native platform"
    export CC=gcc
    export CXX=g++
    export STRIP=strip
fi

# Create build directory
mkdir -p build
cd build

# Build flags
CFLAGS="-O2 -Wall -Wextra"
LDFLAGS=""

# 1. Build CD-ROM daemon
print_status "Building cdrom_daemon..."
$CXX $CFLAGS ../cdrom_daemon.cpp -o cdrom_daemon $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP cdrom_daemon
    print_status "cdrom_daemon built successfully"
else
    print_error "Failed to build cdrom_daemon"
    exit 1
fi

# 2. Build Game Launcher
print_status "Building game_launcher..."
$CXX $CFLAGS ../game_launcher.cpp -o game_launcher -lpthread $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP game_launcher
    print_status "game_launcher built successfully"
else
    print_error "Failed to build game_launcher"
    exit 1
fi

# 3. Build NFC daemon
print_status "Building nfc_daemon..."
$CXX $CFLAGS ../nfc_daemon.cpp -o nfc_daemon -lpthread $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP nfc_daemon
    print_status "nfc_daemon built successfully"
else
    print_error "Failed to build nfc_daemon"
    exit 1
fi

# 4. Build NFC tag writer
print_status "Building nfc_tag_writer..."
$CXX $CFLAGS ../nfc_tag_writer.cpp -o nfc_tag_writer $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP nfc_tag_writer
    print_status "nfc_tag_writer built successfully"
else
    print_error "Failed to build nfc_tag_writer"
    exit 1
fi

# 5. Build Network daemon
print_status "Building network_daemon..."
$CXX $CFLAGS ../network_daemon.cpp -o network_daemon -lpthread $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP network_daemon
    print_status "network_daemon built successfully"
else
    print_error "Failed to build network_daemon"
    exit 1
fi

# 6. Build UART daemon
print_status "Building uart_daemon..."
$CXX $CFLAGS ../uart_daemon.cpp -o uart_daemon -lpthread $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP uart_daemon
    print_status "uart_daemon built successfully"
else
    print_error "Failed to build uart_daemon"
    exit 1
fi

# 7. Build Game Announcer
print_status "Building game_announcer..."
# Check for jsoncpp
if pkg-config --exists jsoncpp; then
    JSON_CFLAGS=$(pkg-config --cflags jsoncpp)
    JSON_LIBS=$(pkg-config --libs jsoncpp)
    $CXX $CFLAGS $JSON_CFLAGS ../game_announcer.cpp -o game_announcer -lpthread $JSON_LIBS $LDFLAGS
    if [ $? -eq 0 ]; then
        $STRIP game_announcer
        print_status "game_announcer built successfully"
    else
        print_error "Failed to build game_announcer"
        exit 1
    fi
else
    print_warning "jsoncpp not found, skipping game_announcer"
fi

# 8. Build Cartridge daemon
print_status "Building cartridge_daemon..."
# Check for libudev
if pkg-config --exists libudev; then
    UDEV_CFLAGS=$(pkg-config --cflags libudev)
    UDEV_LIBS=$(pkg-config --libs libudev)
    $CXX $CFLAGS $UDEV_CFLAGS ../cartridge_daemon.cpp -o cartridge_daemon -lpthread $UDEV_LIBS $LDFLAGS
    if [ $? -eq 0 ]; then
        $STRIP cartridge_daemon
        print_status "cartridge_daemon built successfully"
    else
        print_error "Failed to build cartridge_daemon"
        exit 1
    fi
else
    print_warning "libudev not found, skipping cartridge_daemon"
fi

# 9. Build GPIO daemon
print_status "Building gpio_daemon..."
$CXX $CFLAGS ../gpio_daemon.cpp -o gpio_daemon -lpthread $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP gpio_daemon
    print_status "gpio_daemon built successfully"
else
    print_error "Failed to build gpio_daemon"
    exit 1
fi

# 10. Build Filesystem daemon
print_status "Building filesystem_daemon..."
$CXX $CFLAGS ../filesystem_daemon.cpp -o filesystem_daemon -lpthread $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP filesystem_daemon
    print_status "filesystem_daemon built successfully"
else
    print_error "Failed to build filesystem_daemon"
    exit 1
fi

# 11. Build WebSocket daemon
print_status "Building websocket_daemon..."
# Check for OpenSSL
if pkg-config --exists openssl; then
    SSL_CFLAGS=$(pkg-config --cflags openssl)
    SSL_LIBS=$(pkg-config --libs openssl)
    $CXX $CFLAGS $SSL_CFLAGS ../websocket_daemon.cpp -o websocket_daemon -lpthread $SSL_LIBS $LDFLAGS
    if [ $? -eq 0 ]; then
        $STRIP websocket_daemon
        print_status "websocket_daemon built successfully"
    else
        print_error "Failed to build websocket_daemon"
        exit 1
    fi
else
    print_warning "OpenSSL not found, skipping websocket_daemon"
fi

# 12. Build Update Monitor
print_status "Building update_monitor..."
# Check for required libraries
if pkg-config --exists libcurl json-c; then
    CURL_CFLAGS=$(pkg-config --cflags libcurl)
    CURL_LIBS=$(pkg-config --libs libcurl)
    JSON_CFLAGS=$(pkg-config --cflags json-c)
    JSON_LIBS=$(pkg-config --libs json-c)
    $CXX $CFLAGS $CURL_CFLAGS $JSON_CFLAGS ../update_monitor.cpp -o update_monitor $CURL_LIBS $JSON_LIBS $LDFLAGS
    if [ $? -eq 0 ]; then
        $STRIP update_monitor
        print_status "update_monitor built successfully"
    else
        print_error "Failed to build update_monitor"
        exit 1
    fi
else
    print_warning "libcurl or json-c not found, skipping update_monitor"
fi

# 13. Build Attract Mode daemon
print_status "Building attract_mode..."
$CXX $CFLAGS ../attract_mode.cpp -o attract_mode -lpthread $LDFLAGS
if [ $? -eq 0 ]; then
    $STRIP attract_mode
    print_status "attract_mode built successfully"
else
    print_error "Failed to build attract_mode"
    exit 1
fi

# Create directory for binaries
mkdir -p ../bin

# Move all binaries to bin directory
print_status "Moving binaries to bin directory..."
mv cdrom_daemon ../bin/
mv game_launcher ../bin/
mv nfc_daemon ../bin/
mv nfc_tag_writer ../bin/
mv network_daemon ../bin/
mv uart_daemon ../bin/
mv gpio_daemon ../bin/
mv filesystem_daemon ../bin/
mv attract_mode ../bin/

# Move optional binaries if they exist
[ -f game_announcer ] && mv game_announcer ../bin/
[ -f cartridge_daemon ] && mv cartridge_daemon ../bin/
[ -f websocket_daemon ] && mv websocket_daemon ../bin/
[ -f update_monitor ] && mv update_monitor ../bin/

# List built binaries
echo ""
echo "================================="
echo "Build Summary"
echo "================================="
print_status "Successfully built binaries:"
ls -la ../bin/

# Calculate total size
TOTAL_SIZE=$(du -sh ../bin | cut -f1)
print_status "Total size: $TOTAL_SIZE"

# Clean up build directory
cd ..
rm -rf build

echo ""
print_status "Build complete! All binaries are in the 'bin' directory."
print_status "To install on MiSTer, run: ./install_all.sh"

# Create simple test script
cat > test_binaries.sh << 'EOF'
#!/bin/bash
# Test all binaries to ensure they run

echo "Testing MiSTer addon binaries..."

for binary in bin/*; do
    if [ -x "$binary" ]; then
        echo -n "Testing $(basename $binary)... "
        if $binary --help >/dev/null 2>&1 || $binary -h >/dev/null 2>&1; then
            echo "OK"
        else
            # Try running with no args briefly
            timeout 1s $binary >/dev/null 2>&1
            if [ $? -eq 124 ]; then
                echo "OK (runs)"
            else
                echo "FAIL"
            fi
        fi
    fi
done
EOF

chmod +x test_binaries.sh

echo ""
print_status "Run ./test_binaries.sh to test all binaries"