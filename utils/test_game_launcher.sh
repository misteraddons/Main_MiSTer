#!/bin/bash
#
# Test script for the modular game launcher system
# Demonstrates how different input sources can use the same service
#

echo "Testing MiSTer Game Launcher System"
echo "==================================="

# Check if game launcher is built
if [ ! -f "./game_launcher" ]; then
    echo "Error: game_launcher not found. Run ./build_game_launcher.sh first"
    exit 1
fi

# Create test directories
echo "Setting up test environment..."
mkdir -p /tmp/test_gamedb
mkdir -p /tmp/test_games/PSX
mkdir -p /tmp/test_games/Saturn
mkdir -p /tmp/test_games/MegaCD

# Create minimal test GameDB
cat > /tmp/test_gamedb/PSX.data.json << 'EOF'
[
  {
    "id": "SLUS-00067",
    "title": "Castlevania: Symphony of the Night",
    "region": "USA",
    "system": "PSX"
  }
]
EOF

cat > /tmp/test_gamedb/Saturn.data.json << 'EOF'
[
  {
    "id": "T-8109H",
    "title": "Panzer Dragoon Saga",
    "region": "USA",
    "system": "Saturn"
  }
]
EOF

# Create test game files
touch "/tmp/test_games/PSX/Castlevania Symphony of the Night (USA).chd"
touch "/tmp/test_games/Saturn/Panzer Dragoon Saga (USA).chd"

# Create test config
cat > /tmp/test_game_launcher.conf << 'EOF'
[Paths]
games_dir = /tmp/test_games
gamedb_dir = /tmp/test_gamedb
temp_dir = /tmp

[Search]
fuzzy_threshold = 30
max_results = 10
region_priority = USA,Europe,Japan,World

[OSD]
show_notifications = false
EOF

echo "Starting game launcher service..."
./game_launcher &
LAUNCHER_PID=$!

# Give it a moment to start
sleep 2

echo "Testing game launcher commands..."

# Test 1: PSX game by serial
echo "Test 1: PSX game by serial (SLUS-00067)"
echo "PSX:serial:SLUS-00067:test" > /dev/MiSTer_game_launcher

sleep 1

# Test 2: Saturn game by serial
echo "Test 2: Saturn game by serial (T-8109H)"
echo "Saturn:serial:T-8109H:test" > /dev/MiSTer_game_launcher

sleep 1

# Test 3: Game by title
echo "Test 3: PSX game by title"
echo "PSX:title:Castlevania:test" > /dev/MiSTer_game_launcher

sleep 1

# Test 4: Multiple input sources
echo "Test 4: Simulating multiple input sources"
echo "PSX:serial:SLUS-00067:cdrom" > /dev/MiSTer_game_launcher &
echo "Saturn:title:Panzer Dragoon:nfc" > /dev/MiSTer_game_launcher &
echo "PSX:title:Castlevania:network" > /dev/MiSTer_game_launcher &

sleep 2

echo "Stopping game launcher service..."
kill $LAUNCHER_PID

# Wait for cleanup
sleep 1

echo "Checking generated MGL files..."
if [ -f "/media/fat/Castlevania Symphony of the Night (USA).mgl" ]; then
    echo "✓ MGL file created successfully"
    echo "Contents:"
    cat "/media/fat/Castlevania Symphony of the Night (USA).mgl"
else
    echo "✗ No MGL file found"
fi

echo ""
echo "Test completed!"
echo ""
echo "Next steps:"
echo "1. Adapt existing cdrom_daemon to use game_launcher service"
echo "2. Create NFC daemon that sends commands to game_launcher"
echo "3. Create network daemon that sends commands to game_launcher"
echo "4. Create I2C daemon that sends commands to game_launcher"
echo ""
echo "All input sources will use the same GameDB/MGL creation logic!"

# Cleanup
rm -f /tmp/test_game_launcher.conf
rm -rf /tmp/test_gamedb
rm -rf /tmp/test_games
rm -f /media/fat/*.mgl 2>/dev/null