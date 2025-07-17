#!/bin/bash
# Installation script for ROM patch extraction tools

echo "ROM Patch Extraction Tools Installer"
echo "====================================="

# Check what's available
echo
echo "Checking current tools..."
python3 organize_rom_patches.py --check-tools

echo
echo "Installing extraction tools..."

# Install Python libraries
echo "Installing Python libraries..."
pip3 install py7zr rarfile

# Try to install 7z command line tool
echo
echo "Installing 7z command line tool..."

if command -v apt >/dev/null 2>&1; then
    # Ubuntu/Debian
    echo "Detected Ubuntu/Debian - installing p7zip-full"
    sudo apt update && sudo apt install -y p7zip-full unrar
elif command -v yum >/dev/null 2>&1; then
    # CentOS/RHEL
    echo "Detected CentOS/RHEL - installing p7zip"
    sudo yum install -y p7zip p7zip-plugins unrar
elif command -v pacman >/dev/null 2>&1; then
    # Arch Linux
    echo "Detected Arch Linux - installing p7zip"
    sudo pacman -S p7zip unrar
elif command -v brew >/dev/null 2>&1; then
    # macOS with Homebrew
    echo "Detected macOS with Homebrew - installing p7zip"
    brew install p7zip unrar
else
    echo "Unknown system - please manually install:"
    echo "  - 7z/p7zip: Download from https://7-zip.org/"
    echo "  - unrar: Download from https://www.rarlab.com/"
fi

echo
echo "Installation complete! Checking tools again..."
echo
python3 organize_rom_patches.py --check-tools

echo
echo "You can now run the ROM patch organizer with improved RAR support!"