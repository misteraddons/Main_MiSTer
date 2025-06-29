# I2C Joystick PS2 Scan Code Mapping Reference

This document provides the complete PS2 scan code mappings for I2C joystick/arcade controls on MiSTer, based on the JTCORES standard used by JOTEGO's arcade cores. **Supports up to 4 players** for MAME and arcade games.

## Hardware Support

- **PCF8574**: 8-bit I/O expander (addresses 0x20-0x27, 0x38-0x3F)
- **MCP23017**: 16-bit I/O expander (addresses 0x20-0x27)
- **Maximum**: 4 I2C expanders for complete 4-player arcade setup

## MCP23017 Pin Layout

The MCP23017 has 16 I/O pins organized as:
- **Port A (GPA0-GPA7)**: button_bit 0-7
- **Port B (GPB0-GPB7)**: button_bit 8-15

## Player Support Overview

| Players | Configuration | Notes |
|---------|---------------|--------|
| **2-Player** | Standard arcade setup | Full independent control |
| **3-Player** | Extended arcade setup | Full independent control |
| **4-Player** | MAME/arcade cabinet | Player 4 shares P1 directions* |

*See limitations section below for details.

## PS2 Scan Code Reference

### Standard Arcade Controls (JTCORES Compatible)

#### Player 1 - Traditional Arcade Layout
```
# MCP23017 at I2C address 0x20
EXPANDER 0x20 1

# Joystick Directions (Port A: pins 0-3)
MAP 0 0 0x75 1   # GPA0 -> Up Arrow    (PS2: 0x75)
MAP 0 1 0x72 1   # GPA1 -> Down Arrow  (PS2: 0x72)
MAP 0 2 0x6B 1   # GPA2 -> Left Arrow  (PS2: 0x6B)
MAP 0 3 0x74 1   # GPA3 -> Right Arrow (PS2: 0x74)

# Action Buttons (Port A: pins 4-7)
MAP 0 4 0x14 1   # GPA4 -> Button 1 (Left Ctrl)  (PS2: 0x14)
MAP 0 5 0x11 1   # GPA5 -> Button 2 (Left Alt)   (PS2: 0x11)
MAP 0 6 0x29 1   # GPA6 -> Button 3 (Space)      (PS2: 0x29)
MAP 0 7 0x12 1   # GPA7 -> Button 4 (Left Shift) (PS2: 0x12)

# Extra Buttons (Port B: pins 8-9)
MAP 0 8 0x1A 1   # GPB0 -> Button 5 (Z)          (PS2: 0x1A)
MAP 0 9 0x22 1   # GPB1 -> Button 6 (X)          (PS2: 0x22)
```

#### Player 2 - WASD Layout
```
# MCP23017 at I2C address 0x21
EXPANDER 0x21 1

# Joystick Directions (Port A: pins 0-3)
MAP 1 0 0x2D 1   # GPA0 -> Up (R)    (PS2: 0x2D)
MAP 1 1 0x2B 1   # GPA1 -> Down (F)  (PS2: 0x2B)
MAP 1 2 0x23 1   # GPA2 -> Left (D)  (PS2: 0x23)
MAP 1 3 0x34 1   # GPA3 -> Right (G) (PS2: 0x34)

# Action Buttons (Port A: pins 4-7)
MAP 1 4 0x1C 1   # GPA4 -> Button 1 (A) (PS2: 0x1C)
MAP 1 5 0x1B 1   # GPA5 -> Button 2 (S) (PS2: 0x1B)
MAP 1 6 0x15 1   # GPA6 -> Button 3 (Q) (PS2: 0x15)
MAP 1 7 0x1D 1   # GPA7 -> Button 4 (W) (PS2: 0x1D)

# Extra Button (Port B: pin 8)
MAP 1 8 0x24 1   # GPB0 -> Button 5 (E) (PS2: 0x24)
```

#### Player 3 - IJKL Layout
```
# MCP23017 at I2C address 0x22
EXPANDER 0x22 1

# Joystick Directions (Port A: pins 0-3)
MAP 2 0 0x43 1   # GPA0 -> Up (I)    (PS2: 0x43)
MAP 2 1 0x42 1   # GPA1 -> Down (K)  (PS2: 0x42)
MAP 2 2 0x3B 1   # GPA2 -> Left (J)  (PS2: 0x3B)
MAP 2 3 0x4B 1   # GPA3 -> Right (L) (PS2: 0x4B)

# Action Buttons (Port A: pins 4-6)
MAP 2 4 0x1A 1   # GPA4 -> Button 1 (Z substitute) (PS2: 0x1A)
MAP 2 5 0x59 1   # GPA5 -> Button 2 (Right Shift)  (PS2: 0x59)
MAP 2 6 0x5A 1   # GPA6 -> Button 3 (Return)       (PS2: 0x5A)
```

#### Player 4 - Numpad Layout (⚠️ Limited)
```
# MCP23017 at I2C address 0x23
EXPANDER 0x23 1

# Joystick Directions (⚠️ SHARES WITH PLAYER 1)
MAP 3 0 0x75 1   # GPA0 -> Up Arrow    (PS2: 0x75) - SHARED WITH P1
MAP 3 1 0x72 1   # GPA1 -> Down Arrow  (PS2: 0x72) - SHARED WITH P1
MAP 3 2 0x6B 1   # GPA2 -> Left Arrow  (PS2: 0x6B) - SHARED WITH P1
MAP 3 3 0x74 1   # GPA3 -> Right Arrow (PS2: 0x74) - SHARED WITH P1

# Action Buttons (Port A: pins 4-6)
MAP 3 4 0x70 1   # GPA4 -> Button 1 (Numpad 0)     (PS2: 0x70)
MAP 3 5 0x5A 1   # GPA5 -> Button 2 (Numpad Enter) (PS2: 0x5A)
MAP 3 6 0x79 1   # GPA6 -> Button 3 (Numpad +)     (PS2: 0x79)
```

#### System/Cabinet Controls
```
# MCP23017 at I2C address 0x22
EXPANDER 0x22 1

# Coin/Service Controls (Port A: pins 0-2)
MAP 2 0 0x46 1   # GPA0 -> Service (9)   (PS2: 0x46)
MAP 2 1 0x16 1   # GPA1 -> Coin 1 (1)    (PS2: 0x16)
MAP 2 2 0x1E 1   # GPA2 -> Coin 2 (2)    (PS2: 0x1E)

# System Controls (Port A: pins 4-7)
MAP 2 4 0x06 1   # GPA4 -> Test (F2)     (PS2: 0x06)
MAP 2 5 0x04 1   # GPA5 -> Reset (F3)    (PS2: 0x04)
MAP 2 6 0x2C 1   # GPA6 -> Tilt (T)      (PS2: 0x2C)
MAP 2 7 0x4D 1   # GPA7 -> Pause (P)     (PS2: 0x4D)

# Volume Controls (Port B: pins 8-9)
MAP 2 8 0x0C 1   # GPB0 -> Volume Up (F4)   (PS2: 0x0C)
MAP 2 9 0x03 1   # GPB1 -> Volume Down (F5) (PS2: 0x03)
```

## Complete PS2 Scan Code Reference

### Directional Controls
| Key | PS2 Code | Description |
|-----|----------|-------------|
| Up Arrow | 0x75 | Player 1 Up |
| Down Arrow | 0x72 | Player 1 Down |
| Left Arrow | 0x6B | Player 1 Left |
| Right Arrow | 0x74 | Player 1 Right |

### Action Buttons - Player 1
| Key | PS2 Code | Description |
|-----|----------|-------------|
| Left Ctrl | 0x14 | Button 1 |
| Left Alt | 0x11 | Button 2 |
| Space | 0x29 | Button 3 |
| Left Shift | 0x12 | Button 4 |
| Z | 0x1A | Button 5 |
| X | 0x22 | Button 6 |

### Directional Controls - Player 2
| Key | PS2 Code | Description |
|-----|----------|-------------|
| R | 0x2D | Player 2 Up |
| F | 0x2B | Player 2 Down |
| D | 0x23 | Player 2 Left |
| G | 0x34 | Player 2 Right |

### Action Buttons - Player 2
| Key | PS2 Code | Description |
|-----|----------|-------------|
| A | 0x1C | Button 1 |
| S | 0x1B | Button 2 |
| Q | 0x15 | Button 3 |
| W | 0x1D | Button 4 |
| E | 0x24 | Button 5 |

### Directional Controls - Player 3
| Key | PS2 Code | Description |
|-----|----------|-------------|
| I | 0x43 | Player 3 Up |
| K | 0x42 | Player 3 Down |
| J | 0x3B | Player 3 Left |
| L | 0x4B | Player 3 Right |

### Action Buttons - Player 3
| Key | PS2 Code | Description |
|-----|----------|-------------|
| Z (substitute) | 0x1A | Button 1 |
| Right Shift | 0x59 | Button 2 |
| Return | 0x5A | Button 3 |

### Player 4 Controls (⚠️ Limited)
| Key | PS2 Code | Description |
|-----|----------|-------------|
| Up Arrow | 0x75 | Player 4 Up (shared with P1) |
| Down Arrow | 0x72 | Player 4 Down (shared with P1) |
| Left Arrow | 0x6B | Player 4 Left (shared with P1) |
| Right Arrow | 0x74 | Player 4 Right (shared with P1) |
| Numpad 0 | 0x70 | Button 1 |
| Numpad Enter | 0x5A | Button 2 |
| Numpad + | 0x79 | Button 3 |

### System Controls
| Key | PS2 Code | Description |
|-----|----------|-------------|
| 1 | 0x16 | Coin 1 |
| 2 | 0x1E | Coin 2 |
| 9 | 0x46 | Service |
| F2 | 0x06 | Test |
| F3 | 0x04 | Reset |
| F4 | 0x0C | Volume Up |
| F5 | 0x03 | Volume Down |
| T | 0x2C | Tilt |
| P | 0x4D | Pause |

### Additional Common Keys
| Key | PS2 Code | Description |
|-----|----------|-------------|
| Enter | 0x5A | Enter/Start |
| Escape | 0x76 | Exit/Back |
| Tab | 0x0D | Tab |
| Backspace | 0x66 | Backspace |

### Function Keys
| Key | PS2 Code | Description |
|-----|----------|-------------|
| F1 | 0x05 | Function 1 |
| F6 | 0x0B | Function 6 |
| F7 | 0x83 | Function 7 |
| F8 | 0x0A | Function 8 |
| F9 | 0x01 | Function 9 |
| F10 | 0x09 | Function 10 |
| F11 | 0x78 | Function 11 |
| F12 | 0x07 | Function 12 |

### Number Keys
| Key | PS2 Code | Description |
|-----|----------|-------------|
| 0 | 0x45 | Number 0 |
| 3 | 0x26 | Number 3 |
| 4 | 0x25 | Number 4 |
| 5 | 0x2E | Number 5 |
| 6 | 0x36 | Number 6 |
| 7 | 0x3D | Number 7 |
| 8 | 0x3E | Number 8 |

### Letter Keys (A-Z)
| Key | PS2 Code | Key | PS2 Code | Key | PS2 Code |
|-----|----------|-----|----------|-----|----------|
| B | 0x32 | C | 0x21 | H | 0x33 |
| I | 0x43 | J | 0x3B | K | 0x42 |
| L | 0x4B | M | 0x3A | N | 0x31 |
| O | 0x44 | U | 0x3C | V | 0x2A |
| Y | 0x35 |

## Hardware Connections

### MCP23017 Wiring
- **SDA**: User Port pin 3
- **SCL**: User Port pin 4
- **VCC**: 3.3V
- **GND**: Ground
- **A0,A1,A2**: Address selection pins (set I2C address)

### Button Connections
- Connect one side of each button to the appropriate MCP23017 pin
- Connect the other side to ground (for active-low configuration)
- Internal pull-ups in MCP23017 can be enabled, or use external 10kΩ pull-ups

## Configuration File Location
Place configuration at: `/media/fat/config/i2c_joystick.cfg`

## 4-Player MAME Standard Mappings

Based on JTCORES implementation, the standard 4-player MAME layout is:

### Complete 4-Player Button Support
| Player | Directions | Button 1 | Button 2 | Button 3 | Button 4+ |
|--------|------------|----------|----------|----------|-----------|
| **P1** | Arrow Keys | L.Ctrl | L.Alt | Space | L.Shift, Z, X |
| **P2** | R,F,D,G | A | S | Q | W, E |
| **P3** | I,K,J,L | R.Ctrl* | R.Shift | Return | - |
| **P4** | Arrows** | Num0 | NumEnter | Num+ | - |

*R.Ctrl uses Z (0x1A) as substitute due to scan code limitations  
**Player 4 shares directional scan codes with Player 1

### 4-Player Game Compatibility
- **3 buttons per player** is the standard for most 4-player arcade games
- **Classic 4-player games**: Gauntlet, Simpsons, TMNT, X-Men, etc.
- **Button layout**: Most 4-player cabinets use 3-4 buttons maximum per player
- **MAME standard**: Follows traditional arcade cabinet button counts

### Hardware Limitations & Workarounds

#### Player 4 Direction Sharing Issue
The JTCORES PS2 decoder has Player 4 sharing directional scan codes with Player 1. Solutions:

1. **3-Player Setup** (Recommended)
   - Use Players 1, 2, 3 for full independent control
   - Most arcade games work perfectly with 3 players

2. **Alternating Players** 
   - Player 1 and Player 4 cannot move simultaneously
   - Works for turn-based or single-active-player games

3. **Custom Core Modifications**
   - Modify arcade cores to use different scan codes for Player 4
   - Use numpad directional keys if core supports them

### Recommended 4-Player Setup

For maximum compatibility, use this configuration:
- **2x MCP23017** for Players 1 & 2 (full control)
- **1x MCP23017** for Player 3 + system controls
- **Optional 4th expander** for Player 4 (with limitations noted)

## Compatibility
This mapping is compatible with:
- **JTCORES** arcade cores by JOTEGO
- **MiSTer FPGA** standard arcade controls  
- **MAME** 4-player input conventions
- **Classic arcade cabinets** (Gauntlet, Simpsons, TMNT, etc.)
- **Modern homebrew** 4-player games

## Notes
- All mappings use `active_low = 1` (buttons pull to ground when pressed)
- **4-player support**: 3 buttons per player is standard for arcade games
- **Player 4 limitation**: Shares directional controls with Player 1 in JTCORES
- Extended scan codes are not currently supported
- Maximum 4 I2C expanders can be configured simultaneously
- Each MCP23017 provides 16 inputs (enough for 1 complete player + extras)