#!/bin/bash
echo "Building PN532 Diagnostic Tool..."

# Build for ARM
/opt/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-g++ \
    -Wall -Wextra -std=c++11 -O2 \
    -o diagnose_pn532 diagnose_pn532.cpp \
    -lpthread

if [ $? -eq 0 ]; then
    echo "✓ PN532 Diagnostic Tool built successfully"
    echo ""
    echo "Deploy to MiSTer:"
    echo "  scp diagnose_pn532 root@192.168.1.121:/media/fat/utils/"
    echo ""
    echo "Usage on MiSTer:"
    echo "  /media/fat/utils/diagnose_pn532            # Test all devices"
    echo "  /media/fat/utils/diagnose_pn532 /dev/ttyUSB1  # Test specific device"
    echo ""
else
    echo "✗ Build failed"
    exit 1
fi