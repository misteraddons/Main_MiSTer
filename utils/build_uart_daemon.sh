#!/bin/bash
#
# Build script for UART Daemon
#

echo "Building MiSTer UART Daemon..."

# Build flags
CFLAGS="-Wall -Wextra -std=c++11 -O2"
LDFLAGS=""

# Build UART daemon
echo "Building uart_daemon..."
g++ $CFLAGS -o uart_daemon uart_daemon.cpp $LDFLAGS

if [ $? -eq 0 ]; then
    echo "✓ UART Daemon built successfully"
    strip uart_daemon
else
    echo "✗ Failed to build UART Daemon"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "Installation:"
echo "  sudo cp uart_daemon /media/fat/utils/"
echo "  sudo cp uart_daemon.conf /media/fat/utils/"
echo ""
echo "Usage:"
echo "  # Start UART daemon"
echo "  /media/fat/utils/uart_daemon &"
echo ""
echo "  # Start in foreground (for debugging)"
echo "  /media/fat/utils/uart_daemon -f"
echo ""
echo "Serial Communication:"
echo "  Baud Rate: 115200 (configurable)"
echo "  Format: 8N1 (8 data bits, no parity, 1 stop bit)"
echo "  Line Ending: CR+LF"
echo ""
echo "Protocol Commands:"
echo "  LAUNCH PSX serial SLUS-00067"
echo "  LAUNCH Saturn title \"Panzer Dragoon\""
echo "  STATUS"
echo "  PING"
echo "  VERSION"
echo ""
echo "See UART_PROTOCOL.md for complete protocol specification."