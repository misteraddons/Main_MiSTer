#!/bin/bash
#
# Build script for Network Daemon
#

echo "Building MiSTer Network Daemon..."

# Build flags
CFLAGS="-Wall -Wextra -std=c++11 -O2"
LDFLAGS="-lpthread"

# Build network daemon
echo "Building network_daemon..."
g++ $CFLAGS -o network_daemon network_daemon.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ Network Daemon built successfully"
    strip network_daemon
else
    echo "✗ Failed to build Network Daemon"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "Installation:"
echo "  sudo cp network_daemon /media/fat/utils/"
echo "  sudo cp network_daemon.conf /media/fat/utils/"
echo ""
echo "Usage:"
echo "  # Start network daemon"
echo "  /media/fat/utils/network_daemon &"
echo ""
echo "  # Start in foreground (for debugging)"
echo "  /media/fat/utils/network_daemon -f"
echo ""
echo "API Endpoints:"
echo "  GET  http://<mister-ip>:8080/status"
echo "  POST http://<mister-ip>:8080/launch"
echo "  GET  http://<mister-ip>:8080/api"
echo ""
echo "Example launch request:"
echo '  curl -X POST http://<mister-ip>:8080/launch \'
echo '    -H "Content-Type: application/json" \'
echo '    -d '"'"'{"core":"PSX","id_type":"serial","identifier":"SLUS-00067"}'"'"''