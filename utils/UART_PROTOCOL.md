# MiSTer UART Game Launcher Protocol

This document describes the serial communication protocol for the MiSTer UART daemon, which provides remote game launching capabilities via serial/UART interface.

## Connection Settings

- **Baud Rate**: 115200 (configurable)
- **Data Bits**: 8
- **Parity**: None  
- **Stop Bits**: 1
- **Flow Control**: None
- **Line Ending**: CR+LF (`\r\n`)

## Protocol Overview

The protocol uses simple ASCII text commands with space-separated parameters. All commands are case-sensitive and must be terminated with CR+LF.

### Command Format

```
COMMAND [parameters...]
```

### Response Format

```
STATUS MESSAGE
```

Where STATUS is either `OK` or `ERROR`, followed by a status-specific message.

## Commands

### Game Announcements (Server to Client)

The UART daemon can forward real-time game announcements from MiSTer when games are launched or changed. These are automatically sent to connected clients when `forward_announcements=true` in the configuration.

**Announcement Format:**
```
GAME_CHANGED <core> "<game_name>" "<file_path>"
```

**Examples:**
```
GAME_CHANGED PSX "Castlevania: Symphony of the Night" "/media/fat/games/PSX/Castlevania SOTN.cue"
GAME_CHANGED Saturn "Panzer Dragoon" "/media/fat/games/Saturn/Panzer Dragoon.cue"
GAME_STOPPED
```

**Detailed Announcements:**
```
GAME_DETAILS core="PSX" name="Castlevania: Symphony of the Night" path="/media/fat/games/PSX/Castlevania SOTN.cue" serial="SLUS-00067" timestamp=1642678901
```

### LAUNCH - Launch a Game

Launch a game using the centralized GameDB system.

**Syntax:**
```
LAUNCH <core> <id_type> <identifier>
```

**Parameters:**
- `core`: Core name (PSX, Saturn, MegaCD, Arcade, etc.)
- `id_type`: Either `serial` or `title`
- `identifier`: Game serial number or title (use quotes for titles with spaces)

**Examples:**
```
LAUNCH PSX serial SLUS-00067
LAUNCH Saturn serial T-8107G
LAUNCH PSX title "Metal Gear Solid"
LAUNCH MegaCD serial T-25013
```

**Response:**
- Success: `OK LAUNCHED <core> <id_type> <identifier>`
- Error: `ERROR <error_message>`

### STATUS - Get System Status

Get current system status and service availability.

**Syntax:**
```
STATUS
```

**Response:**
```
OK STATUS game_launcher=<true|false> uart_baud=<baud_rate>
```

**Example:**
```
> STATUS
< OK STATUS game_launcher=true uart_baud=115200
```

### PING - Test Connection

Simple connectivity test.

**Syntax:**
```
PING
```

**Response:**
```
OK PONG
```

### VERSION - Get Daemon Version

Get UART daemon version information.

**Syntax:**
```
VERSION
```

**Response:**
```
OK MiSTer-UART-Daemon/<version>
```

## Connection Lifecycle

### 1. Initial Connection
When the daemon starts or a client connects, it sends:
```
OK MiSTer UART Game Launcher Ready
```

### 2. Normal Operation
Clients can send commands and receive responses. The daemon echoes received commands if `echo_commands=true` in configuration.

### 3. Shutdown
When the daemon shuts down, it sends:
```
OK SHUTDOWN
```

## Error Handling

### Common Error Responses

- `ERROR Invalid command format` - Command syntax is incorrect
- `ERROR Failed to communicate with game launcher service` - GameDB service unavailable
- `ERROR Unknown command` - Command not recognized

### Connection Errors

If the daemon loses connection to the game launcher service, LAUNCH commands will return errors, but status commands will still work.

## Hardware Setup

### USB-to-Serial Adapters
Most common setup using FTDI, CH340, or similar adapters:
- Connect adapter to MiSTer USB port
- Device appears as `/dev/ttyUSB0` or similar
- Use auto-detection or specify device in config

### Direct UART Connection
For embedded projects connecting directly to MiSTer GPIO:
- Use `/dev/ttyAMA0` or appropriate device
- Ensure proper voltage levels (3.3V)
- Disable auto-detection in config

### Supported Devices
- FTDI USB-to-Serial adapters
- CH340/CH341 USB-to-Serial
- Arduino boards (Leonardo, Uno + USB-to-Serial)
- ESP32/ESP8266 with USB-to-Serial
- Raspberry Pi UART pins
- Direct GPIO UART connections

## Client Implementation Examples

### Arduino Example
```cpp
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Test connection
    Serial.println("PING");
}

void launchGame(String core, String idType, String identifier) {
    Serial.print("LAUNCH ");
    Serial.print(core);
    Serial.print(" ");
    Serial.print(idType);
    Serial.print(" ");
    Serial.println(identifier);
}

void handleAnnouncement(String message) {
    if (message.startsWith("GAME_CHANGED")) {
        Serial.println("Game changed detected!");
        // Parse and display on LCD, etc.
    } else if (message.startsWith("GAME_STOPPED")) {
        Serial.println("Game stopped");
    }
}

void loop() {
    // Check for incoming announcements
    if (Serial.available()) {
        String message = Serial.readStringUntil('\n');
        message.trim();
        
        if (message.startsWith("GAME_")) {
            handleAnnouncement(message);
        } else {
            Serial.print("Response: ");
            Serial.println(message);
        }
    }
    
    delay(100);
}
```

### Python Example
```python
import serial
import time
import threading

def setup_serial():
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(2)  # Wait for connection
    return ser

def send_command(ser, command):
    ser.write(f"{command}\r\n".encode())
    response = ser.readline().decode().strip()
    return response

def launch_game(ser, core, id_type, identifier):
    cmd = f"LAUNCH {core} {id_type} {identifier}"
    return send_command(ser, cmd)

def handle_announcement(message):
    if message.startswith("GAME_CHANGED"):
        parts = message.split('"')
        if len(parts) >= 3:
            game_name = parts[1]
            print(f"Now playing: {game_name}")
    elif message.startswith("GAME_STOPPED"):
        print("Game stopped")

def monitor_announcements(ser):
    while True:
        try:
            line = ser.readline().decode().strip()
            if line and line.startswith("GAME_"):
                handle_announcement(line)
        except:
            pass

# Usage
ser = setup_serial()
print(send_command(ser, "PING"))  # Test connection

# Start announcement monitoring in background
monitor_thread = threading.Thread(target=monitor_announcements, args=(ser,), daemon=True)
monitor_thread.start()

# Launch a game
print(launch_game(ser, "PSX", "serial", "SLUS-00067"))
```

### Shell Script Example
```bash
#!/bin/bash
DEVICE="/dev/ttyUSB0"
BAUD="115200"

# Setup serial port
stty -F $DEVICE $BAUD cs8 -cstopb -parity -echo

# Send command
echo "LAUNCH PSX serial SLUS-00067" > $DEVICE

# Read response
timeout 5 cat $DEVICE
```

## Protocol Extensions

The protocol is designed to be extensible. Future versions may add:

- `LIST` command to enumerate available games
- `SEARCH` command for GameDB queries  
- `CONFIG` command for runtime configuration
- Async notifications for system events
- Binary protocol mode for faster communication

## Troubleshooting

### Connection Issues
1. Check device permissions: `sudo chmod 666 /dev/ttyUSB0`
2. Verify baud rate matches configuration
3. Ensure device is not in use by another application
4. Try auto-detection mode

### Communication Issues
1. Check line endings (must be CR+LF)
2. Verify command syntax and case sensitivity
3. Monitor daemon logs for error messages
4. Test with simple PING command first

### Game Launch Issues
1. Ensure game launcher service is running
2. Verify GameDB files are present
3. Check game files exist in correct directories
4. Test game launch via other input methods first