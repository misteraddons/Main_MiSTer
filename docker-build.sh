#!/bin/bash

# Docker-based ARM cross-compilation for MiSTer
# This script runs the build in a Ubuntu 20.04 container with proper ARM cross-compiler

set -e

echo "Building MiSTer with Docker ARM cross-compiler..."

docker run --rm --platform linux/amd64 \
  -v "$(pwd)":/work \
  -w /work \
  ubuntu:20.04 \
  bash -c "
    # Install dependencies if not already present
    which arm-linux-gnueabihf-gcc > /dev/null 2>&1 || {
      apt update > /dev/null 2>&1
      apt install -y gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf make > /dev/null 2>&1
    }
    
    # Create temporary Makefile with correct cross-compiler
    cp Makefile Makefile.bak
    sed 's/BASE.*=.*/BASE = arm-linux-gnueabihf/' Makefile.bak > Makefile
    
    # Verify compiler is working
    arm-linux-gnueabihf-gcc --version
    
    # Show the change
    echo 'Makefile updated:'
    grep 'BASE.*=' Makefile
    
    # Clean and build
    echo 'Running: make clean && make'
    make clean
    echo 'Starting compilation...'
    timeout 180 make -j1 || echo 'Build timed out or failed'
    
    echo 'Build attempt completed!'
    ls -la bin/MiSTer 2>/dev/null && echo '✅ SUCCESS: Binary created!' && file bin/MiSTer || echo 'No binary found'
  "