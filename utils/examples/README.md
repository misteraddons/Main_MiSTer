# MiSTer Network Game Launcher - Client Examples

This directory contains example client implementations for the MiSTer Network Game Launcher API. These examples demonstrate how to remotely launch games on your MiSTer using HTTP requests.

## Prerequisites

1. MiSTer with the game launcher system installed
2. Network daemon running on MiSTer (`/media/fat/utils/network_daemon`)
3. Both devices on the same network

## Available Examples

### 1. Web Client (`web_client.html`)

A simple HTML/JavaScript web interface that runs in your browser.

**Features:**
- Auto-detects MiSTer IP address
- Dropdown menus for cores and ID types
- Example games with one-click auto-fill
- Real-time status feedback
- No installation required

**Usage:**
1. Open `web_client.html` in any modern web browser
2. Enter your MiSTer's IP address (auto-detection will try common addresses)
3. Select core, ID type, and enter game identifier
4. Click "Launch Game"

### 2. Python Client (`python_client.py`)

A complete Python client library with command-line interface.

**Features:**
- Full API client library
- Command-line interface
- Connection testing
- Status monitoring
- Error handling

**Installation:**
```bash
pip install requests
```

**Usage:**
```bash
# Test connection
python3 python_client.py --host 192.168.1.100 test

# Get system status
python3 python_client.py status

# Launch a game
python3 python_client.py launch --core PSX --id-type serial --identifier SLUS-00067

# With authentication
python3 python_client.py --api-key mykey launch --core Saturn --id-type title --identifier "Panzer Dragoon"
```

**Library Usage:**
```python
from python_client import MiSTerClient

client = MiSTerClient("192.168.1.100")
if client.test_connection():
    result = client.launch_game("PSX", "serial", "SLUS-00067")
    print(f"Success: {result['message']}")
```

### 3. Shell Script Client (`mister_launcher.sh`)

A bash script for command-line usage on Linux/macOS.

**Features:**
- Simple command-line interface
- Environment variable support
- Colored output
- Connection testing
- No external dependencies (except curl)

**Installation:**
```bash
chmod +x mister_launcher.sh
```

**Usage:**
```bash
# Test connection
./mister_launcher.sh test

# Get status
./mister_launcher.sh status

# Launch games
./mister_launcher.sh launch PSX serial SLUS-00067
./mister_launcher.sh launch Saturn title "Panzer Dragoon"

# With custom host
./mister_launcher.sh -h 192.168.0.100 launch PSX serial SCUS-94455

# With API key
MISTER_API_KEY=mykey ./mister_launcher.sh launch PSX title "Metal Gear Solid"
```

## API Reference

### Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET    | `/status` | Get system status |
| POST   | `/launch` | Launch a game |
| GET    | `/api`    | Get API information |

### Launch Request Format

```json
{
    "core": "PSX",
    "id_type": "serial",
    "identifier": "SLUS-00067"
}
```

**Fields:**
- `core`: Core name (PSX, Saturn, MegaCD, Arcade, etc.)
- `id_type`: Either "serial" or "title"
- `identifier`: Game serial number or title

### Response Format

**Success:**
```json
{
    "success": true,
    "message": "Game launch request sent",
    "core": "PSX",
    "id_type": "serial",
    "identifier": "SLUS-00067"
}
```

**Error:**
```json
{
    "error": "Invalid JSON format"
}
```

## Common Game Examples

### PlayStation (PSX)
- Castlevania: Symphony of the Night - `SLUS-00067`
- Final Fantasy VII - `SCUS-94455`
- Metal Gear Solid - `SLUS-00594`

### Sega Saturn
- Panzer Dragoon - `T-8107G`
- Nights into Dreams - `T-8106G`
- Virtua Fighter 2 - `T-8104G`

### Sega CD (MegaCD)
- Sonic CD - `T-25013`
- Lunar: Silver Star Story - `T-25033`

## Authentication

If the network daemon is configured with an API key, include it in requests:

**HTTP Header:**
```
Authorization: your_api_key_here
```

**Query Parameter:**
```
?api_key=your_api_key_here
```

## Troubleshooting

### Connection Issues
1. Ensure MiSTer is on the same network
2. Check that network daemon is running (`ps aux | grep network_daemon`)
3. Verify port 8080 is not blocked by firewall
4. Try the status endpoint first: `curl http://mister-ip:8080/status`

### Game Launch Issues
1. Verify game launcher service is running
2. Check GameDB files are in `/media/fat/utils/gamedb/`
3. Ensure game files exist in correct core directories
4. Check daemon logs for error messages

### CORS Issues (Web Client)
If using the web client from a different domain, ensure CORS is enabled in `network_daemon.conf`:
```
enable_cors=true
allowed_origins=*
```

## Integration Ideas

- Home automation systems (Home Assistant, etc.)
- Mobile apps with game library browsers
- Voice assistants ("Hey Google, play Castlevania on MiSTer")
- Physical control panels with arcade buttons
- NFC tag alternatives for systems without PN532
- Web-based game library managers
- Retro gaming frontend integration