#!/bin/bash
echo "Building NFC Tag Writer..."

# Build for ARM
/opt/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-g++ \
    -Wall -Wextra -std=c++11 -O2 \
    -o write_current_game write_current_game.cpp \
    -lpthread

if [ $? -eq 0 ]; then
    echo "✓ NFC Tag Writer built successfully"
    echo ""
    echo "Deploy to MiSTer:"
    echo "  scp write_current_game root@192.168.1.121:/media/fat/utils/"
    echo ""
    echo "Usage on MiSTer:"
    echo "  /media/fat/utils/write_current_game [game_name]"
    echo ""
    echo "Examples:"
    echo "  /media/fat/utils/write_current_game SLUS-00067"
    echo "  /media/fat/utils/write_current_game \"Final Fantasy VII\""
else
    echo "✗ Build failed"
    exit 1
fi