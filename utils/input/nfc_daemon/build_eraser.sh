#!/bin/bash
echo "Building NFC Tag Eraser..."

# Build for ARM
/opt/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-g++ \
    -Wall -Wextra -std=c++11 -O2 \
    -o erase_nfc_tag erase_nfc_tag.cpp \
    -lpthread

if [ $? -eq 0 ]; then
    echo "✓ NFC Tag Eraser built successfully"
    echo ""
    echo "Deploy to MiSTer:"
    echo "  scp erase_nfc_tag root@192.168.1.121:/media/fat/utils/"
    echo ""
    echo "Usage on MiSTer:"
    echo "  /media/fat/utils/erase_nfc_tag         # Quick erase"
    echo "  /media/fat/utils/erase_nfc_tag -f      # Full erase"
    echo ""
else
    echo "✗ Build failed"
    exit 1
fi