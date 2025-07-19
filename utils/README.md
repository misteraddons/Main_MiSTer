# MiSTer Addon Utilities

A comprehensive collection of daemons and utilities that extend MiSTer FPGA functionality with modern features like NFC support, network control, game tracking, and automated game launching.

## Overview

This repository contains modular daemons that work together to provide advanced game launching and management capabilities for MiSTer FPGA. The system is designed around a central `game_launcher` service that handles all game launch requests from various input sources.

## Directory Structure

```
utils/
├── core/           # Core services
│   ├── game_launcher/    # Central game launching service
│   └── cdrom_daemon/     # CD-ROM monitoring and auto-launch
├── input/          # Input method daemons
│   ├── nfc_daemon/       # NFC/RFID tag support
│   ├── uart_daemon/      # Serial communication
│   ├── gpio_daemon/      # GPIO buttons/encoders
│   ├── filesystem_daemon/# File system triggers
│   └── cartridge_daemon/ # Physical cartridge readers
├── network/        # Network services
│   ├── network_daemon/   # HTTP/REST API server
│   └── websocket_daemon/ # WebSocket real-time communication
└── monitors/       # System monitor daemons
    ├── game_announcer/   # Game launch announcements
    ├── update_monitor/   # Core update checking
    └── attract_mode/     # Automatic game cycling
```

## Features

- **Multiple Input Methods**: NFC tags, UART, GPIO buttons, network API, file triggers
- **Game Tracking**: Favorites, ratings, completion percentage, playtime monitoring
- **Smart Features**: AI-powered recommendations, random game selection, collections
- **Network Control**: HTTP REST API, WebSocket real-time updates
- **Automation**: Attract mode, CD-ROM auto-launch, update monitoring

## Quick Start

```bash
# Build all daemons
./build_organized.sh

# Deploy to MiSTer
./deploy_to_mister.sh 192.168.1.100
```

See individual daemon directories for specific documentation and configuration options.
EOF < /dev/null
