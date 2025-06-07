#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "smbus.h"

// Test program to verify TX_ENABLE register behavior after fix
#define ADV7513_CEC_I2C_ADDR     0x3C
#define CEC_TX_ENABLE_REG        0x11

int main() {
    printf("=== CEC TX_ENABLE Register Test ===\n");
    
    // Open CEC I2C device
    int fd = i2c_open(ADV7513_CEC_I2C_ADDR, 0);
    if (fd < 0) {
        printf("ERROR: Failed to open CEC I2C device at 0x%02X\n", ADV7513_CEC_I2C_ADDR);
        return -1;
    }
    
    printf("CEC I2C device opened successfully\n");
    
    // Read initial TX_ENABLE register state
    int initial_tx_enable = i2c_smbus_read_byte_data(fd, CEC_TX_ENABLE_REG);
    if (initial_tx_enable < 0) {
        printf("ERROR: Failed to read TX_ENABLE register\n");
        i2c_close(fd);
        return -1;
    }
    
    printf("Initial TX_ENABLE (0x11): 0x%02X\n", (uint8_t)initial_tx_enable);
    
    // Test TX_ENABLE register behavior
    printf("\n=== Testing TX_ENABLE Register Behavior ===\n");
    
    // Write 0x01 to TX_ENABLE (should trigger transmission and auto-clear)
    printf("Writing 0x01 to TX_ENABLE register...\n");
    int write_result = i2c_smbus_write_byte_data(fd, CEC_TX_ENABLE_REG, 0x01);
    if (write_result < 0) {
        printf("ERROR: Failed to write to TX_ENABLE register\n");
        i2c_close(fd);
        return -1;
    }
    
    // Monitor TX_ENABLE register for auto-clear behavior
    printf("Monitoring TX_ENABLE register for auto-clear...\n");
    
    for (int i = 0; i < 20; i++) {
        usleep(50000); // 50ms delay
        
        int tx_enable = i2c_smbus_read_byte_data(fd, CEC_TX_ENABLE_REG);
        if (tx_enable < 0) {
            printf("ERROR: Failed to read TX_ENABLE register at iteration %d\n", i);
            break;
        }
        
        printf("Time %3dms: TX_ENABLE = 0x%02X\n", (i+1)*50, (uint8_t)tx_enable);
        
        // Check if TX_ENABLE auto-cleared (should go from 0x01 to 0x00)
        if (tx_enable == 0x00) {
            printf("SUCCESS: TX_ENABLE auto-cleared after %dms!\n", (i+1)*50);
            printf("This indicates the transmission engine is working properly.\n");
            break;
        }
        
        if (i == 19) {
            printf("WARNING: TX_ENABLE did not auto-clear after 1000ms\n");
            printf("This suggests the transmission engine may still be stuck.\n");
        }
    }
    
    // Read some related registers for diagnostic info
    printf("\n=== Additional Diagnostic Information ===\n");
    
    struct {
        uint8_t reg;
        const char* name;
    } diagnostic_regs[] = {
        {0x10, "TX_FRAME_LENGTH"},
        {0x12, "TX_RETRY"},
        {0x26, "RX_ENABLE"},
        {0x27, "LOGICAL_ADDR"},
        {0x2A, "POWER_MODE"},
        {0x4E, "CLOCK_DIVIDER_POWER"},
        {0x7F, "ARBITRATION_ENABLE"}
    };
    
    for (int i = 0; i < 7; i++) {
        int val = i2c_smbus_read_byte_data(fd, diagnostic_regs[i].reg);
        if (val >= 0) {
            printf("Register 0x%02X (%s): 0x%02X\n", 
                   diagnostic_regs[i].reg, diagnostic_regs[i].name, (uint8_t)val);
        }
    }
    
    i2c_close(fd);
    printf("\n=== Test Complete ===\n");
    return 0;
}
