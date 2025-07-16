#!/bin/bash

# MiSTer Command Bridge Test Script

echo "MiSTer Command Bridge Test"
echo "========================="

# Build the test utility
echo "Building test utility..."
make test_cmd_bridge

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "✓ Test utility built successfully"
echo

# Test individual commands
echo "Testing individual commands:"
echo "----------------------------"

./test_cmd_bridge "help"
./test_cmd_bridge "load_core Genesis"
./test_cmd_bridge "load_game /games/sonic.bin"
./test_cmd_bridge "mount_image 0 /games/disk.img"
./test_cmd_bridge "reset cold"
./test_cmd_bridge "menu up"
./test_cmd_bridge "screenshot test.png"

echo
echo "Testing error conditions:"
echo "------------------------"

./test_cmd_bridge ""
./test_cmd_bridge "nonexistent_command"
./test_cmd_bridge "load_core"

echo
echo "Testing custom command:"
echo "----------------------"

./test_cmd_bridge "test_custom hello world"

echo
echo "All tests completed!"
echo
echo "To run interactively:"
echo "  ./test_cmd_bridge"
echo
echo "To test specific commands:"
echo "  ./test_cmd_bridge \"command arg1 arg2\""