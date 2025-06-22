#include "i2c_joystick.h"
#include "smbus.h"
#include "input.h"
#include "user_io.h"
#include "spi.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// MCP23017 registers
#define MCP23017_IODIRA   0x00
#define MCP23017_IODIRB   0x01
#define MCP23017_GPPUA    0x0C
#define MCP23017_GPPUB    0x0D
#define MCP23017_GPIOA    0x12
#define MCP23017_GPIOB    0x13

// Global instance
I2CJoystick i2c_joystick;

I2CJoystick::I2CJoystick() : initialized(false) {
}

I2CJoystick::~I2CJoystick() {
    cleanup();
}

bool I2CJoystick::init() {
    if (initialized) {
        return true;
    }
    
    // Load configuration if it exists
    char config_file[1024];
    snprintf(config_file, sizeof(config_file), "%s/i2c_joystick.cfg", user_io_get_core_path("config"));
    load_config(config_file);
    
    initialized = true;
    printf("I2C Joystick: Initialized with %zu expanders\n", expanders.size());
    return true;
}

bool I2CJoystick::add_expander(uint8_t address, uint8_t type) {
    i2c_expander exp;
    exp.address = address;
    exp.type = type;
    exp.last_state = 0;
    
    // Open I2C device
    exp.fd = i2c_open(address, 1);
    if (exp.fd < 0) {
        printf("I2C Joystick: Failed to open I2C device at address 0x%02X\n", address);
        return false;
    }
    
    // Configure expander based on type
    if (type == 0) {
        // PCF8574 - 8-bit expander, all pins as inputs by default
        // No specific initialization needed
    } else if (type == 1) {
        // MCP23017 - 16-bit expander, configure as inputs with pull-ups
        i2c_smbus_write_byte_data(exp.fd, MCP23017_IODIRA, 0xFF); // All port A as inputs
        i2c_smbus_write_byte_data(exp.fd, MCP23017_IODIRB, 0xFF); // All port B as inputs
        i2c_smbus_write_byte_data(exp.fd, MCP23017_GPPUA, 0xFF);  // Enable pull-ups on port A
        i2c_smbus_write_byte_data(exp.fd, MCP23017_GPPUB, 0xFF);  // Enable pull-ups on port B
    }
    
    // Read initial state
    if (type == 0) {
        exp.last_state = i2c_smbus_read_byte(exp.fd);
    } else if (type == 1) {
        uint8_t port_a = i2c_smbus_read_byte_data(exp.fd, MCP23017_GPIOA);
        uint8_t port_b = i2c_smbus_read_byte_data(exp.fd, MCP23017_GPIOB);
        exp.last_state = (port_b << 8) | port_a;
    }
    
    expanders.push_back(exp);
    printf("I2C Joystick: Added expander at address 0x%02X, type %d\n", address, type);
    return true;
}

bool I2CJoystick::map_button(uint8_t expander_idx, uint8_t button_bit, uint16_t ps2_scancode, bool active_low) {
    if (expander_idx >= expanders.size()) {
        return false;
    }
    
    i2c_button_mapping mapping;
    mapping.button_bit = button_bit;
    mapping.ps2_scancode = ps2_scancode;
    mapping.active_low = active_low;
    
    expanders[expander_idx].mappings.push_back(mapping);
    printf("I2C Joystick: Mapped button %d on expander %d to PS2 code 0x%02X\n", 
           button_bit, expander_idx, ps2_scancode);
    return true;
}

void I2CJoystick::poll() {
    if (!initialized) {
        return;
    }
    
    for (auto& exp : expanders) {
        uint16_t current_state = 0;
        
        // Read current state based on expander type
        if (exp.type == 0) {
            // PCF8574 - 8-bit
            int val = i2c_smbus_read_byte(exp.fd);
            if (val >= 0) {
                current_state = val & 0xFF;
            } else {
                continue; // Skip on error
            }
        } else if (exp.type == 1) {
            // MCP23017 - 16-bit
            int port_a = i2c_smbus_read_byte_data(exp.fd, MCP23017_GPIOA);
            int port_b = i2c_smbus_read_byte_data(exp.fd, MCP23017_GPIOB);
            if (port_a >= 0 && port_b >= 0) {
                current_state = ((port_b & 0xFF) << 8) | (port_a & 0xFF);
            } else {
                continue; // Skip on error
            }
        }
        
        // Check for state changes
        uint16_t changed_bits = current_state ^ exp.last_state;
        if (changed_bits) {
            // Process each mapped button
            for (const auto& mapping : exp.mappings) {
                if (changed_bits & (1 << mapping.button_bit)) {
                    bool pressed = (current_state & (1 << mapping.button_bit)) ? true : false;
                    if (mapping.active_low) {
                        pressed = !pressed;
                    }
                    
                    // Generate PS2 event
                    uint32_t ps2_code = mapping.ps2_scancode;
                    if (!pressed) {
                        ps2_code |= UPSTROKE;
                    }
                    
                    // Send PS2 code to the system
                    spi_uio_cmd8(UIO_KEYBOARD, ps2_code & 0xFF);
                    if (ps2_code & 0xFF00) {
                        // Send extended scancode if needed
                        spi_uio_cmd8(UIO_KEYBOARD, (ps2_code >> 8) & 0xFF);
                    }
                }
            }
            
            exp.last_state = current_state;
        }
    }
}

bool I2CJoystick::load_config(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        return false;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        
        // Parse expander definition: EXPANDER <address> <type>
        uint8_t address, type;
        if (sscanf(line, "EXPANDER 0x%hhx %hhu", &address, &type) == 2) {
            add_expander(address, type);
            continue;
        }
        
        // Parse button mapping: MAP <expander_idx> <button_bit> <ps2_code> <active_low>
        uint8_t exp_idx, button_bit, active_low;
        uint16_t ps2_code;
        if (sscanf(line, "MAP %hhu %hhu 0x%hx %hhu", &exp_idx, &button_bit, &ps2_code, &active_low) == 4) {
            map_button(exp_idx, button_bit, ps2_code, active_low != 0);
        }
    }
    
    fclose(fp);
    return true;
}

bool I2CJoystick::save_config(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        return false;
    }
    
    fprintf(fp, "# I2C Joystick Configuration\n");
    fprintf(fp, "# Format:\n");
    fprintf(fp, "# EXPANDER <i2c_address> <type>\n");
    fprintf(fp, "#   type: 0=PCF8574 (8-bit), 1=MCP23017 (16-bit)\n");
    fprintf(fp, "# MAP <expander_index> <button_bit> <ps2_scancode> <active_low>\n");
    fprintf(fp, "#   active_low: 1=button pulls to ground, 0=button pulls to VCC\n\n");
    
    for (size_t i = 0; i < expanders.size(); i++) {
        const auto& exp = expanders[i];
        fprintf(fp, "EXPANDER 0x%02X %d\n", exp.address, exp.type);
        
        for (const auto& mapping : exp.mappings) {
            fprintf(fp, "MAP %zu %d 0x%02X %d\n", 
                    i, mapping.button_bit, mapping.ps2_scancode, mapping.active_low ? 1 : 0);
        }
    }
    
    fclose(fp);
    return true;
}

void I2CJoystick::cleanup() {
    for (auto& exp : expanders) {
        if (exp.fd >= 0) {
            i2c_close(exp.fd);
        }
    }
    expanders.clear();
    initialized = false;
}

// C interface functions
void i2c_joystick_init() {
    i2c_joystick.init();
}

void i2c_joystick_poll() {
    i2c_joystick.poll();
}

void i2c_joystick_cleanup() {
    i2c_joystick.cleanup();
}