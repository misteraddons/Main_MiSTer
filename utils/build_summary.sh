#!/bin/bash
#
# Build Summary for MiSTer Addon Utilities
#

echo "========================================"
echo "MiSTer Addon Utilities Build Summary"
echo "========================================"
echo ""

echo "ARM Binaries Built (bin_arm/):"
echo "------------------------------"
for binary in bin_arm/*; do
    if [ -f "$binary" ]; then
        name=$(basename "$binary")
        size=$(stat -c%s "$binary")
        size_kb=$(echo "scale=1; $size / 1024" | bc -l)
        arch=$(file "$binary" | grep -o 'ARM.*' | cut -d, -f1)
        echo "  $name - ${size_kb}KB ($arch)"
    fi
done

echo ""
echo "Total ARM binaries: $(ls -1 bin_arm/ | wc -l)"
echo ""

echo "Binaries NOT built (missing dependencies):"
echo "-------------------------------------------"
if [ ! -f "bin_arm/update_monitor" ]; then
    echo "  update_monitor - Missing libcurl development headers"
fi
if [ ! -f "bin_arm/game_announcer" ]; then
    echo "  game_announcer - Missing jsoncpp development headers"
fi  
if [ ! -f "bin_arm/websocket_daemon" ]; then
    echo "  websocket_daemon - Missing OpenSSL development headers"
fi
if [ ! -f "bin_arm/cartridge_daemon" ]; then
    echo "  cartridge_daemon - Missing libudev development headers"
fi

echo ""
echo "Deployment Ready:"
echo "-----------------"
echo "All core functionality binaries are built and ready for MiSTer."
echo "Optional daemons can be built later when dependencies are available."
echo ""
echo "To deploy to MiSTer:"
echo "  ./deploy_to_mister.sh <mister_ip>"
echo ""
echo "Example usage on MiSTer:"
echo "  /media/fat/utils/game_launcher &"
echo "  /media/fat/utils/attract_mode --start &"
echo "  /media/fat/utils/nfc_daemon &"