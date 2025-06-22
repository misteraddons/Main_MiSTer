# I2C Joystick Support for MiSTer

This branch adds support for connecting arcade buttons and joysticks to MiSTer using I2C expander chips. The buttons are mapped to PS2 keyboard scan codes, making them appear as keyboard inputs to cores.

## Supported I2C Expanders

- **PCF8574**: 8-bit I/O expander (addresses 0x20-0x27 or 0x38-0x3F)
- **MCP23017**: 16-bit I/O expander (addresses 0x20-0x27)

## Hardware Setup

1. Connect your I2C expander to the MiSTer's I2C bus:
   - SDA: User Port pin 3
   - SCL: User Port pin 4
   - VCC: 3.3V
   - GND: Ground

2. Connect arcade buttons between the I2C expander's I/O pins and ground (for active-low configuration)

3. Use pull-up resistors (10K typical) on each button input if not built into the expander

## Configuration

The system looks for a configuration file at `/media/fat/config/i2c_joystick.cfg`

### Configuration Format

```
# Define an I2C expander
EXPANDER <i2c_address> <type>
  i2c_address: Hex address (e.g., 0x20)
  type: 0=PCF8574, 1=MCP23017

# Map a button to a PS2 scancode
MAP <expander_index> <button_bit> <ps2_scancode> <active_low>
  expander_index: 0-based index of expander
  button_bit: Bit number (0-7 for PCF8574, 0-15 for MCP23017)
  ps2_scancode: PS2 scan code in hex
  active_low: 1=pulls to ground when pressed, 0=pulls to VCC
```

### Example Configuration

```
# PCF8574 for action buttons
EXPANDER 0x20 0
MAP 0 0 0x1C 1  # Button 1 -> Enter
MAP 0 1 0x39 1  # Button 2 -> Space
MAP 0 2 0x01 1  # Button 3 -> Escape

# MCP23017 for joystick
EXPANDER 0x21 1
MAP 1 0 0x48 1  # Up
MAP 1 1 0x50 1  # Down
MAP 1 2 0x4B 1  # Left
MAP 1 3 0x4D 1  # Right
```

## Common PS2 Scan Codes

- Arrow Keys: Up=0x48, Down=0x50, Left=0x4B, Right=0x4D
- Enter: 0x1C
- Space: 0x39
- Escape: 0x01
- A-Z: 0x1E-0x2C (see example config for full list)
- Function Keys: F1=0x3B through F10=0x44

## Implementation Details

- The I2C expanders are polled during the main input polling loop
- Button state changes generate PS2 keyboard events
- Multiple expanders can be used simultaneously
- The system supports up to 4 I2C expanders

## Building

```bash
make clean && make
```

## Deployment

Copy the compiled binary to your MiSTer:
```bash
sshpass -p 1 scp bin/MiSTer 192.168.1.79:/media/fat
```

Then restart MiSTer on the device.