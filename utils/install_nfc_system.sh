#!/bin/bash
#
# MiSTer NFC System Installation Script
#

echo "Installing MiSTer NFC Game Launcher System..."
echo "============================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

# Create utils directory if it doesn't exist
mkdir -p /media/fat/utils
mkdir -p /media/fat/utils/gameid

echo "Building components..."

# Build all components
if [ ! -f "./build_game_launcher.sh" ]; then
    echo "Error: Run this script from the utils directory"
    exit 1
fi

# Build game launcher
echo "Building game launcher service..."
./build_game_launcher.sh
if [ $? -ne 0 ]; then
    echo "Failed to build game launcher"
    exit 1
fi

# Build CD-ROM daemon
echo "Building CD-ROM daemon..."
./build_cdrom_daemon.sh
if [ $? -ne 0 ]; then
    echo "Failed to build CD-ROM daemon"
    exit 1
fi

# Build NFC tools
echo "Building NFC tools..."
./build_nfc_tools.sh
if [ $? -ne 0 ]; then
    echo "Failed to build NFC tools"
    exit 1
fi

# Build network daemon
echo "Building network daemon..."
./build_network_daemon.sh
if [ $? -ne 0 ]; then
    echo "Failed to build network daemon"
    exit 1
fi

# Build UART daemon
echo "Building UART daemon..."
./build_uart_daemon.sh
if [ $? -ne 0 ]; then
    echo "Failed to build UART daemon"
    exit 1
fi

# Build game announcer
echo "Building game announcer..."
./build_game_announcer.sh
if [ $? -ne 0 ]; then
    echo "Failed to build game announcer"
    exit 1
fi

# Build cartridge daemon
echo "Building cartridge daemon..."
./build_cartridge_daemon.sh
if [ $? -ne 0 ]; then
    echo "Failed to build cartridge daemon"
    exit 1
fi

echo ""
echo "Installing files..."

# Install binaries
cp game_launcher /media/fat/utils/
cp cdrom_daemon /media/fat/utils/
cp nfc_daemon /media/fat/utils/
cp nfc_tag_writer /media/fat/utils/
cp network_daemon /media/fat/utils/
cp uart_daemon /media/fat/utils/
cp game_announcer /media/fat/utils/
cp cartridge_daemon /media/fat/utils/

# Install configuration files
cp game_launcher.conf /media/fat/utils/
cp cdrom_daemon.conf.sample /media/fat/utils/cdrom_daemon.conf
cp nfc_daemon.conf /media/fat/utils/
cp network_daemon.conf /media/fat/utils/
cp uart_daemon.conf /media/fat/utils/
cp game_announcer.conf /media/fat/utils/
cp cartridge_daemon.conf /media/fat/utils/

# Install documentation
cp UART_PROTOCOL.md /media/fat/utils/

# Set permissions
chmod +x /media/fat/utils/game_launcher
chmod +x /media/fat/utils/cdrom_daemon
chmod +x /media/fat/utils/nfc_daemon
chmod +x /media/fat/utils/nfc_tag_writer
chmod +x /media/fat/utils/network_daemon
chmod +x /media/fat/utils/uart_daemon
chmod +x /media/fat/utils/game_announcer
chmod +x /media/fat/utils/cartridge_daemon
chmod 644 /media/fat/utils/*.conf
chmod 644 /media/fat/utils/*.md

echo ""
echo "Creating startup scripts..."

# Create startup script
cat > /media/fat/utils/start_nfc_system.sh << 'EOF'
#!/bin/bash
#
# Start MiSTer NFC Game Launcher System
#

echo "Starting MiSTer NFC System..."

# Start game launcher service first
echo "Starting game launcher service..."
/media/fat/utils/game_launcher &
GAME_LAUNCHER_PID=$!

# Start game announcer
echo "Starting game announcer..."
/media/fat/utils/game_announcer &
GAME_ANNOUNCER_PID=$!

# Wait for services to initialize
sleep 2

# Start CD-ROM daemon
echo "Starting CD-ROM daemon..."
/media/fat/utils/cdrom_daemon &
CDROM_DAEMON_PID=$!

# Start NFC daemon (will auto-detect PN532)
echo "Starting NFC daemon..."
/media/fat/utils/nfc_daemon &
NFC_DAEMON_PID=$!

# Start network daemon
echo "Starting network daemon..."
/media/fat/utils/network_daemon &
NETWORK_DAEMON_PID=$!

# Start UART daemon
echo "Starting UART daemon..."
/media/fat/utils/uart_daemon &
UART_DAEMON_PID=$!

# Start cartridge daemon
echo "Starting cartridge daemon..."
/media/fat/utils/cartridge_daemon &
CARTRIDGE_DAEMON_PID=$!

echo "Game launcher system started successfully!"
echo "Game Launcher PID: $GAME_LAUNCHER_PID"
echo "Game Announcer PID: $GAME_ANNOUNCER_PID"
echo "CD-ROM Daemon PID: $CDROM_DAEMON_PID"
echo "NFC Daemon PID: $NFC_DAEMON_PID"
echo "Network Daemon PID: $NETWORK_DAEMON_PID"
echo "UART Daemon PID: $UART_DAEMON_PID"
echo "Cartridge Daemon PID: $CARTRIDGE_DAEMON_PID"

# Save PIDs for stopping
echo "$GAME_LAUNCHER_PID" > /tmp/game_launcher.pid
echo "$GAME_ANNOUNCER_PID" > /tmp/game_announcer.pid
echo "$CDROM_DAEMON_PID" > /tmp/cdrom_daemon.pid
echo "$NFC_DAEMON_PID" > /tmp/nfc_daemon.pid
echo "$NETWORK_DAEMON_PID" > /tmp/network_daemon.pid
echo "$UART_DAEMON_PID" > /tmp/uart_daemon.pid
echo "$CARTRIDGE_DAEMON_PID" > /tmp/cartridge_daemon.pid

echo ""
echo "System ready! You can now:"
echo "1. Insert CD-ROMs for automatic game detection"
echo "2. Use NFC tags to launch games instantly"
echo "3. Use HTTP API for remote game launching"
echo "4. Use UART/Serial interface for embedded control"
echo "5. Connect UART cartridge readers for physical game detection"
echo "6. All input sources use the centralized GameID for lookup"
EOF

# Create stop script
cat > /media/fat/utils/stop_nfc_system.sh << 'EOF'
#!/bin/bash
#
# Stop MiSTer NFC Game Launcher System
#

echo "Stopping MiSTer NFC System..."

# Kill processes if PID files exist
for service in game_launcher game_announcer cdrom_daemon nfc_daemon network_daemon uart_daemon cartridge_daemon; do
    if [ -f "/tmp/${service}.pid" ]; then
        PID=$(cat "/tmp/${service}.pid")
        if kill -0 "$PID" 2>/dev/null; then
            echo "Stopping $service (PID: $PID)..."
            kill "$PID"
        fi
        rm -f "/tmp/${service}.pid"
    fi
done

# Also kill by process name as backup
pkill -f game_launcher
pkill -f game_announcer
pkill -f cdrom_daemon  
pkill -f nfc_daemon
pkill -f network_daemon
pkill -f uart_daemon
pkill -f cartridge_daemon

echo "NFC system stopped."
EOF

chmod +x /media/fat/utils/start_nfc_system.sh
chmod +x /media/fat/utils/stop_nfc_system.sh

echo ""
echo "Creating GameID directory structure..."

# Create GameID directories
mkdir -p /media/fat/utils/gameid

# Create sample GameID file
cat > /media/fat/utils/gameid/README.txt << 'EOF'
GameID Directory
================

Place your GameID JSON files here:
- PSX.data.json
- Saturn.data.json
- SegaCD.data.json (note: MegaCD games use SegaCD database)

Format example:
[
  {
    "id": "SLUS-00067",
    "title": "Castlevania: Symphony of the Night",
    "region": "USA",
    "system": "PSX"
  }
]

The game launcher service will automatically load these files for
game identification and fuzzy matching.

GameID honors the GameID project: https://github.com/niemasd/GameID
EOF

echo ""
echo "Installation completed successfully!"
echo ""
echo "Next steps:"
echo "1. Copy your GameID JSON files to /media/fat/utils/gameid/"
echo "2. Connect your PN532 NFC module (USB or I2C)"
echo "3. Start the system: /media/fat/utils/start_nfc_system.sh"
echo ""
echo "Usage:"
echo "- Start system: /media/fat/utils/start_nfc_system.sh"
echo "- Stop system:  /media/fat/utils/stop_nfc_system.sh"
echo "- Write NFC tag: /media/fat/utils/nfc_tag_writer -w -c PSX -g \"SLUS-00067\""
echo "- Read NFC tag:  /media/fat/utils/nfc_tag_writer -r"
echo "- HTTP API:      curl -X POST http://<mister-ip>:8080/launch -H \"Content-Type: application/json\" -d '{\"core\":\"PSX\",\"id_type\":\"serial\",\"identifier\":\"SLUS-00067\"}'"
echo "- UART/Serial:   echo \"LAUNCH PSX serial SLUS-00067\" > /dev/ttyUSB0"
echo ""
echo "Hardware setup:"
echo "- PN532 via USB-to-serial: Connect to any USB port"
echo "- PN532 via I2C: Connect to /dev/i2c-0 (address 0x24)"
echo "- CD-ROM drive: Should be /dev/sr0 (automatically detected)"
echo "- UART devices: USB-to-Serial adapters, Arduino, ESP32, etc."
echo "- Cartridge readers: Arduino-based readers on /dev/ttyUSB0 or /dev/ttyACM0"
echo ""
echo "The system provides a unified game launching experience across:"
echo "- Physical CD-ROMs (auto-detection)"
echo "- NFC tags (PN532 module)"
echo "- Network API (HTTP REST interface)"
echo "- UART/Serial interface (Arduino, ESP32, etc.)"
echo "- UART cartridge readers (SNES, Game Boy, Genesis, etc.)"
echo "All using the centralized GameID for game identification!"