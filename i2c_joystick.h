#ifndef I2C_JOYSTICK_H
#define I2C_JOYSTICK_H

#include <stdint.h>
#include <vector>

#define I2C_JOY_MAX_BUTTONS    32
#define I2C_JOY_MAX_EXPANDERS  4

struct i2c_button_mapping {
    uint8_t button_bit;      // Which bit on the I2C expander (0-7 for 8-bit, 0-15 for 16-bit)
    uint16_t ps2_scancode;   // PS2 scan code to generate when pressed
    bool active_low;         // True if button pulls to ground when pressed
};

struct i2c_expander {
    int fd;                  // File descriptor for I2C device
    uint8_t address;         // I2C address of the expander
    uint8_t type;            // 0 = PCF8574 (8-bit), 1 = MCP23017 (16-bit)
    uint16_t last_state;     // Last read state of the expander
    std::vector<i2c_button_mapping> mappings;
};

class I2CJoystick {
private:
    std::vector<i2c_expander> expanders;
    bool initialized;
    
public:
    I2CJoystick();
    ~I2CJoystick();
    
    // Initialize I2C joystick system
    bool init();
    
    // Add an I2C expander to monitor
    bool add_expander(uint8_t address, uint8_t type);
    
    // Map a button on an expander to a PS2 scancode
    bool map_button(uint8_t expander_idx, uint8_t button_bit, uint16_t ps2_scancode, bool active_low = true);
    
    // Poll all expanders and generate PS2 events for state changes
    void poll();
    
    // Load configuration from file
    bool load_config(const char* filename);
    
    // Save configuration to file
    bool save_config(const char* filename);
    
    // Cleanup
    void cleanup();
};

// Global instance
extern I2CJoystick i2c_joystick;

// Initialize the I2C joystick system
void i2c_joystick_init();

// Poll I2C joysticks (call this from main input polling loop)
void i2c_joystick_poll();

// Cleanup I2C joystick system
void i2c_joystick_cleanup();

#endif // I2C_JOYSTICK_H