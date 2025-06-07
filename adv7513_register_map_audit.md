# ADV7513 Register Map Address Issue Analysis

## Critical Finding: I2C Register Map Addressing Problem

Based on analysis of the MiSTer CEC implementation, the root cause of the 30-minute failure cycle appears to be **incorrect I2C register map addressing**. The ADV7513 chip has four separate I2C register maps that must be accessed through different I2C addresses.

## ADV7513 Register Map Structure

The ADV7513 has four distinct I2C register maps:

1. **Main Register Map** - Base address configurable, default 0x72/0x7A
2. **CEC Memory Map** - Base address controlled by main register 0xE1, default 0x78  
3. **Packet Memory Map** - Base address controlled by main register 0x45, default 0x70
4. **EDID Memory Map** - Base address controlled by main register 0x43, default 0x7E

## Current Implementation Issues

### 1. Missing Register Map Base Address Programming

The current code uses hardcoded I2C addresses:
- `ADV7513_MAIN_I2C_ADDR = 0x39` (main chip registers)
- `ADV7513_CEC_I2C_ADDR = 0x3C` (CEC-specific registers)

However, the code **never programs the base address control registers** (0x43, 0x45, 0xE1) that tell the ADV7513 which I2C addresses to use for each register map.

### 2. Critical Missing Code

The current `cec_init()` function sets register 0xE1 but doesn't verify it worked:

```cpp
// Current code - line 708:
if (i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE1, ADV7513_CEC_I2C_ADDR << 1) < 0) {
    // Error handling...
}
```

But this code:
1. **Never programs registers 0x43 and 0x45** for other memory maps
2. **Doesn't verify** that 0xE1 was successfully written
3. **Assumes** the main map is already at the correct address

### 3. Potential Register Conflicts

Without proper address mapping:
- Some CEC registers might be accessed through the wrong I2C address
- Register writes might fail silently 
- Over time (30 minutes), register corruption could accumulate
- The CEC subsystem eventually becomes unresponsive

## Root Cause Hypothesis

The 30-minute failure pattern suggests:

1. **Initial Success**: CEC works initially because some registers are accessible through multiple maps
2. **Gradual Degradation**: As the system runs, unmapped register accesses cause internal state corruption
3. **Complete Failure**: After ~30 minutes, the CEC subsystem becomes completely unresponsive
4. **Restart Recovery**: A full system restart resets the ADV7513 internal state

## Required Fix

### Phase 1: Proper Register Map Initialization

Add comprehensive register map base address programming:

```cpp
// Program all four register map base addresses
i2c_smbus_write_byte_data(main_fd, 0x43, EDID_I2C_ADDR << 1);   // EDID Memory
i2c_smbus_write_byte_data(main_fd, 0x45, PACKET_I2C_ADDR << 1); // Packet Memory  
i2c_smbus_write_byte_data(main_fd, 0xE1, CEC_I2C_ADDR << 1);    // CEC Memory
```

### Phase 2: Verify Address Mapping

Add verification that address mapping succeeded:

```cpp
// Verify each address mapping worked
uint8_t edid_verify = i2c_smbus_read_byte_data(main_fd, 0x43);
uint8_t packet_verify = i2c_smbus_read_byte_data(main_fd, 0x45);
uint8_t cec_verify = i2c_smbus_read_byte_data(main_fd, 0xE1);
```

### Phase 3: Register Access Audit

Audit all register accesses to ensure they use the correct I2C address for each register range.

## Expected Results

Implementing proper register map addressing should:
1. **Eliminate the 30-minute failure cycle**
2. **Improve CEC reliability and responsiveness**
3. **Prevent register corruption over time**
4. **Ensure proper hardware register separation**

This fix addresses the core architectural issue rather than working around symptoms.
