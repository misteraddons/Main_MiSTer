# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

### Compilation Commands
- `make` - Build the MiSTer binary using ARM cross-compiler
- `make clean` - Remove all build artifacts from bin/ directory
- `make DEBUG=1` - Build with debug symbols and optimizations disabled
- `make PROFILING=1` - Build with profiling support enabled
- `make V=1` - Build with verbose output

### Deployment
- `./build.sh` - Build and deploy to MiSTer device (requires 'host' file with IP address)
- `./setup_default_toolchain.sh` - Download and setup ARM cross-compilation toolchain

### Testing Scripts
- `Scripts/build_test.sh` - Build controller test programs
- `Scripts/test_triggers.sh` - Test analog trigger functionality on controllers

## Architecture Overview

### System Components
MiSTer is a complex retro computing platform that bridges ARM Linux with FPGA-based hardware emulation:

**Core Communication Flow:**
```
User Input → Input Layer → User I/O → SPI → FPGA Core → Hardware Emulation
```

**Key System Layers:**
- **Input Management** (`input.cpp/h`) - Linux input subsystem integration, controller mapping
- **User I/O System** (`user_io.cpp/h`) - Central hub for all I/O operations and FPGA communication
- **FPGA Interface** (`fpga_io.cpp/h`) - Low-level hardware abstraction, SPI protocols
- **Menu System** (`menu.cpp/h`) - OSD interface, file browser, configuration management
- **Video System** (`video.cpp/h`) - Video modes, scaling, filters, HDR support
- **Core Support** (`support/*/`) - Modular system-specific implementations

### Communication Protocols
- **SPI Communication**: All FPGA communication uses standardized UIO command codes
- **Configuration Management**: INI-based configuration files (MiSTer.ini)
- **File System**: Unified file I/O supporting ZIP archives, network storage, multiple formats
- **UART Metadata**: External device communication via serial port for core/game information

### Controller Support Architecture
- **NUMPLAYERS**: Defines maximum simultaneous controllers (currently 6)
- **Input Arrays**: All player data structures sized using NUMPLAYERS constant
- **SPI Routing**: Joystick data sent via UIO_JOYSTICK0-5 commands to FPGA
- **Analog Triggers**: Recent addition using UIO_TRIGGERS command for L2/R2 support
- **Configuration**: PLAYER_X_CONTROLLER entries in cfg.cpp for each player

## Key Constants and Limits

### Player/Controller Limits
- `NUMPLAYERS` in `input.cpp` - Maximum simultaneous players (6)
- `UIO_JOYSTICK0-5` in `user_io.h` - SPI command codes for controller communication
- `UIO_TRIGGERS` in `user_io.h` - SPI command for analog trigger data
- `player_controller[6][8][256]` in `cfg.h` - Configuration array for 6 players

### System Limits
- `NUMDEV 30` - Maximum input devices
- `NUMBUTTONS` - Maximum button mappings

### UART Metadata System
- **Configuration**: `UART_LOG` in `cfg.h` - Serial device path for metadata output
- **Protocol**: Simple text-based format (`CORE:name\n`, `GAME:name\n`, `PING\n`, `PONG\n`, `HELLO\n`, `STATUS:...\n`)
- **Baud Rate**: 115200, 8N1 configuration
- **Integration**: Hooks into core loading (`user_io_read_core_name()`) and game loading (`recent_update()`)

## Development Patterns

### Adding New Controller Features
1. Define new UIO command codes in `user_io.h`
2. Add data structures in `input.h` if needed
3. Implement input reading logic in `input.cpp`
4. Add SPI transmission in `user_io.cpp`
5. Update configuration parsing in `cfg.cpp` if needed

### Core-Specific Development
- Each supported system has its own module in `support/*/`
- Core-specific file handlers, configuration, and communication protocols
- MRA files for arcade systems, disk image handling for computers/consoles

### FPGA Communication
- All hardware communication uses SPI with standardized UIO commands
- Commands defined in `user_io.h` with corresponding handlers in `user_io.cpp`
- Memory-mapped register access through `fpga_io.cpp`

### UART Metadata Development
- **Configuration**: Add `UART_LOG=/dev/ttyUSB0` to MiSTer.ini
- **Functions**: `uart_log_init()`, `uart_log_send_core()`, `uart_log_send_game()`, `uart_log_process_commands()`, `uart_log_send_ack()`, `uart_log_heartbeat()`, `uart_log_is_connected()`, `uart_log_cleanup()`
- **Integration Points**: Core loading hooks, game loading hooks, command processing in main loop, heartbeat in main loop, initialization and cleanup
- **Bidirectional Communication**: Supports navigation commands from external devices
- **Navigation Commands**: UP, DOWN, LEFT, RIGHT, OK, BACK, MENU, VOLUP, VOLDOWN
- **Heartbeat System**: Periodic PING/PONG every 5 seconds for connection validation
- **Connection Status**: Tracks device connectivity with 10-second timeout
- **Future Commands**: LOAD (game loading), CORE (core switching) - TODO
- **Compatibility**: Works alongside MT32-Pi (uses User I/O port) and existing UART modes
- **External Devices**: Compatible with tty2oled, tty2pico, ESP32 devices

## Important Files

### Core System Files
- `main.cpp` - Entry point, CPU affinity management, scheduler initialization
- `user_io.cpp/h` - Central I/O hub, file mounting, core communication
- `input.cpp/h` - Input device management, controller mapping
- `menu.cpp/h` - OSD system, file browser, configuration interface
- `fpga_io.cpp/h` - Hardware abstraction, FPGA communication

### Configuration Files
- `MiSTer.ini` - Main system configuration
- `cfg.cpp/h` - Configuration parsing and management
- Controller mapping files for different devices

### Build System
- `Makefile` - Cross-compilation setup for ARM
- `build.sh` - Build and deployment script
- ARM toolchain in `gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/`

## Testing and Validation

The system runs on actual MiSTer hardware with real-time constraints. Changes should be tested on hardware when possible, particularly:
- Controller input changes
- FPGA communication modifications
- Video timing alterations
- Core-specific functionality
- UART metadata communication with external devices

### UART Metadata Testing
- **Configuration**: Set `uart_log=/dev/ttyUSB0` and `debug=1` in MiSTer.ini
- **Testing**: Load different cores and games, monitor serial output
- **Debug Output**: Check console for "UART:" messages when debug=1
- **External Device**: Connect tty2oled, tty2pico, or ESP32 to verify packet reception
- **Navigation Testing**: Send NAV commands from external device (e.g., `NAV:UP\n`)
- **Heartbeat Testing**: Monitor `PING\n` messages every 5 seconds, respond with `PONG\n`
- **Connection Status**: Watch for `HELLO\n` and `STATUS:...\n` messages on startup
- **Command Format**: All commands are newline-terminated, acknowledgments returned as `ACK:command\n`

## Architecture Notes

### Performance Considerations
- Main thread pinned to CPU core #1 for reduced latency
- Offload thread on CPU core #0 for background tasks
- SPI communication optimized for minimal overhead
- Memory-mapped register access for FPGA communication

### Execution Models
- **Scheduler-based**: Uses libco coroutines for cooperative multitasking
- **Traditional polling**: Fallback simple polling loop
- Both models support the same functionality with different performance characteristics