#!/bin/bash
#
# Build all MiSTer addon daemons from organized structure
#

set -e

echo "================================="
echo "Building Organized MiSTer Daemons"
echo "================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_status() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Build function
build_daemon() {
    local category=$1
    local daemon=$2
    local source=$3
    local output=$4
    local extra_flags=$5
    
    print_status "Building $daemon..."
    
    if [ -f "$category/$daemon/$source" ]; then
        mkdir -p bin
        if g++ -O2 -Wall -Wextra "$category/$daemon/$source" -o "bin/$output" -lpthread $extra_flags; then
            strip "bin/$output"
            print_status "$daemon built successfully"
        else
            print_error "Failed to build $daemon"
            return 1
        fi
    else
        print_warning "$daemon source not found"
    fi
}

# Build all daemons
build_daemon core game_launcher game_launcher.cpp game_launcher ""
build_daemon core cdrom_daemon cdrom_daemon.cpp cdrom_daemon ""

build_daemon input nfc_daemon nfc_daemon.cpp nfc_daemon ""
build_daemon input nfc_daemon nfc_tag_writer.cpp nfc_tag_writer ""
build_daemon input uart_daemon uart_daemon.cpp uart_daemon ""
build_daemon input gpio_daemon gpio_daemon.cpp gpio_daemon ""
build_daemon input filesystem_daemon filesystem_daemon.cpp filesystem_daemon ""

# Optional builds with dependency checks
if pkg-config --exists libudev; then
    UDEV_FLAGS="$(pkg-config --cflags --libs libudev)"
    build_daemon input cartridge_daemon cartridge_daemon.cpp cartridge_daemon "$UDEV_FLAGS"
fi

build_daemon network network_daemon network_daemon.cpp network_daemon ""

if pkg-config --exists openssl; then
    SSL_FLAGS="$(pkg-config --cflags --libs openssl)"
    build_daemon network websocket_daemon websocket_daemon.cpp websocket_daemon "$SSL_FLAGS"
fi

if pkg-config --exists jsoncpp; then
    JSON_FLAGS="$(pkg-config --cflags --libs jsoncpp)"
    build_daemon monitors game_announcer game_announcer.cpp game_announcer "$JSON_FLAGS"
fi

if pkg-config --exists libcurl json-c; then
    CURL_FLAGS="$(pkg-config --cflags --libs libcurl json-c)"
    build_daemon monitors update_monitor update_monitor.cpp update_monitor "$CURL_FLAGS"
fi

build_daemon monitors attract_mode attract_mode.cpp attract_mode ""

echo ""
echo "================================="
echo "Build Summary"
echo "================================="
print_status "Binaries built in bin/ directory:"
ls -la bin/ 2>/dev/null || print_warning "No binaries built"
