#!/bin/bash
#
# Build script for CD-ROM daemon
# This builds the standalone daemon separately from main MiSTer
#

BASE=arm-none-linux-gnueabihf
CC=${BASE}-gcc
STRIP=${BASE}-strip

echo "Building CD-ROM daemon..."

# Build daemon
${CC} -o cdrom_daemon cdrom_daemon.cpp -lpthread

if [ $? -eq 0 ]; then
    echo "Stripping binary..."
    ${STRIP} cdrom_daemon
    echo "CD-ROM daemon built successfully: utils/cdrom_daemon"
    ls -la cdrom_daemon
else
    echo "Build failed!"
    exit 1
fi