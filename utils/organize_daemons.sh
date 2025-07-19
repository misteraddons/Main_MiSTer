#!/bin/bash
#
# Organize MiSTer addon utilities into daemon-specific subfolders
#

set -e

echo "================================="
echo "Organizing MiSTer Addon Daemons"
echo "================================="

# Create directory structure
echo "Creating directory structure..."

# Core services
mkdir -p core/game_launcher
mkdir -p core/cdrom_daemon

# Input methods
mkdir -p input/nfc_daemon
mkdir -p input/uart_daemon  
mkdir -p input/gpio_daemon
mkdir -p input/filesystem_daemon
mkdir -p input/cartridge_daemon

# Network services
mkdir -p network/network_daemon
mkdir -p network/websocket_daemon

# System monitors
mkdir -p monitors/game_announcer
mkdir -p monitors/update_monitor
mkdir -p monitors/attract_mode

# Move source files and related configs
echo "Moving daemon files to their directories..."

# Core services
echo "  - Core services..."
[ -f game_launcher.cpp ] && mv game_launcher.cpp core/game_launcher/
[ -f game_launcher.conf ] && mv game_launcher.conf core/game_launcher/
[ -f game_control.sh ] && mv game_control.sh core/game_launcher/

[ -f cdrom_daemon.cpp ] && mv cdrom_daemon.cpp core/cdrom_daemon/
[ -f cdrom_daemon.conf ] && mv cdrom_daemon.conf core/cdrom_daemon/

# Input methods
echo "  - Input method daemons..."
[ -f nfc_daemon.cpp ] && mv nfc_daemon.cpp input/nfc_daemon/
[ -f nfc_daemon.conf ] && mv nfc_daemon.conf input/nfc_daemon/
[ -f nfc_tag_writer.cpp ] && mv nfc_tag_writer.cpp input/nfc_daemon/
[ -f nfc_install.sh ] && mv nfc_install.sh input/nfc_daemon/
[ -f nfc_write_game.sh ] && mv nfc_write_game.sh input/nfc_daemon/

[ -f uart_daemon.cpp ] && mv uart_daemon.cpp input/uart_daemon/
[ -f uart_daemon.conf ] && mv uart_daemon.conf input/uart_daemon/
[ -f uart_client.py ] && mv uart_client.py input/uart_daemon/
[ -f uart_esp32_client.ino ] && mv uart_esp32_client.ino input/uart_daemon/

[ -f gpio_daemon.cpp ] && mv gpio_daemon.cpp input/gpio_daemon/
[ -f gpio_daemon.conf ] && mv gpio_daemon.conf input/gpio_daemon/

[ -f filesystem_daemon.cpp ] && mv filesystem_daemon.cpp input/filesystem_daemon/
[ -f filesystem_daemon.conf ] && mv filesystem_daemon.conf input/filesystem_daemon/

[ -f cartridge_daemon.cpp ] && mv cartridge_daemon.cpp input/cartridge_daemon/
[ -f cartridge_daemon.conf ] && mv cartridge_daemon.conf input/cartridge_daemon/

# Network services
echo "  - Network services..."
[ -f network_daemon.cpp ] && mv network_daemon.cpp network/network_daemon/
[ -f network_daemon.conf ] && mv network_daemon.conf network/network_daemon/
[ -f network_client.py ] && mv network_client.py network/network_daemon/
[ -f network_client.sh ] && mv network_client.sh network/network_daemon/
[ -f network_client_advanced.py ] && mv network_client_advanced.py network/network_daemon/

[ -f websocket_daemon.cpp ] && mv websocket_daemon.cpp network/websocket_daemon/
[ -f websocket_daemon.conf ] && mv websocket_daemon.conf network/websocket_daemon/

# System monitors
echo "  - System monitor daemons..."
[ -f game_announcer.cpp ] && mv game_announcer.cpp monitors/game_announcer/
[ -f game_announcer.conf ] && mv game_announcer.conf monitors/game_announcer/

[ -f update_monitor.cpp ] && mv update_monitor.cpp monitors/update_monitor/
[ -f update_monitor.conf ] && mv update_monitor.conf monitors/update_monitor/

[ -f attract_mode.cpp ] && mv attract_mode.cpp monitors/attract_mode/
[ -f attract_mode.conf ] && mv attract_mode.conf monitors/attract_mode/

# Move build scripts and common files to root
echo "Moving build scripts and documentation..."
[ -f build_all.sh ] && cp build_all.sh .build_all.sh.bak

# Create README for each daemon category
echo "Creating README files..."

cat > core/README.md << 'EOF'
# Core Services

## game_launcher
Central game launching service that handles all game launch requests from various input sources.
- Manages GameID lookups
- Creates MGL files
- Handles favorites, ratings, playtime tracking
- Provides smart recommendations

## cdrom_daemon
Monitors CD-ROM drive for disc changes and automatically launches games.
- Extracts serial numbers from PSX/Saturn/SegaCD discs
- Integrates with game_launcher for game identification
- Supports automatic CD ripping with cdrdao
EOF

cat > input/README.md << 'EOF'
# Input Method Daemons

Various input methods for launching games on MiSTer.

## nfc_daemon
NFC/RFID tag reader support using PN532 module.
- Read game information from NFC tags
- Launch games by tapping tags
- Includes tag writer utility

## uart_daemon
Serial communication daemon for external device control.
- Accepts game launch commands via UART
- Supports ESP32/Arduino integration
- Bidirectional communication with announcements

## gpio_daemon
GPIO button and rotary encoder support.
- Map physical buttons to games
- Rotary encoder for game selection
- Direct GPIO control

## filesystem_daemon
File system trigger monitoring.
- Launch games by dropping files into watched directories
- Automatic game detection from file patterns

## cartridge_daemon
USB/UART cartridge reader support.
- Detect physical game cartridges
- Extract game information and launch
- Support for multiple cartridge formats
EOF

cat > network/README.md << 'EOF'
# Network Services

## network_daemon
HTTP/TCP server for remote game control.
- RESTful API for game launching
- Web interface support
- Remote control from any device

## websocket_daemon
WebSocket server for real-time communication.
- Bidirectional messaging
- Real-time game status updates
- JavaScript client support
EOF

cat > monitors/README.md << 'EOF'
# System Monitor Daemons

## game_announcer
Monitors game launches and broadcasts announcements.
- Detects currently playing game
- Sends notifications via UART/network
- MGL file monitoring

## update_monitor
Monitors for core updates on GitHub.
- Checks for new releases
- OSD notifications for available updates
- Configurable check intervals

## attract_mode
Automatic game cycling daemon.
- Random game selection
- Configurable play times
- System weighting
EOF

# Create a master build script that builds from subdirectories
cat > build_organized.sh << 'EOF'
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
EOF

chmod +x build_organized.sh

# Create a deployment script
cat > deploy_to_mister.sh << 'EOF'
#!/bin/bash
#
# Deploy MiSTer addon daemons to MiSTer device
#

MISTER_IP=${1:-}

if [ -z "$MISTER_IP" ]; then
    echo "Usage: $0 <mister_ip>"
    echo "Example: $0 192.168.1.100"
    exit 1
fi

echo "Deploying to MiSTer at $MISTER_IP..."

# Create directory structure on MiSTer
ssh root@$MISTER_IP "mkdir -p /media/fat/utils/{core,input,network,monitors}"

# Copy binaries
echo "Copying binaries..."
scp -r bin/* root@$MISTER_IP:/media/fat/utils/

# Copy configs
echo "Copying configuration files..."
for dir in core input network monitors; do
    for daemon in $dir/*/; do
        if [ -d "$daemon" ]; then
            daemon_name=$(basename $daemon)
            scp $daemon/*.conf root@$MISTER_IP:/media/fat/utils/ 2>/dev/null
        fi
    done
done

echo "Deployment complete!"
EOF

chmod +x deploy_to_mister.sh

echo ""
echo "================================="
echo "Organization Complete!"
echo "================================="
echo ""
echo "Directory structure created:"
echo "  core/        - Core services (game_launcher, cdrom_daemon)"
echo "  input/       - Input method daemons"
echo "  network/     - Network services"
echo "  monitors/    - System monitor daemons"
echo ""
echo "Scripts created:"
echo "  build_organized.sh    - Build all daemons from new structure"
echo "  deploy_to_mister.sh   - Deploy to MiSTer device"
echo ""
echo "To build all daemons: ./build_organized.sh"
echo "To deploy: ./deploy_to_mister.sh <mister_ip>"