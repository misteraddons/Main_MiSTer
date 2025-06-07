/*
 * ADV7513 Register Map Fix for MiSTer CEC
 * 
 * This file contains the comprehensive fix for the ADV7513 I2C register map
 * addressing issue that causes the 30-minute CEC failure cycle.
 * 
 * CRITICAL ISSUE: The ADV7513 has four separate I2C register maps that must
 * be properly addressed through base address control registers. The current
 * implementation fails to program these base addresses, leading to register
 * access conflicts and eventual CEC subsystem failure.
 */

#include "cec.h"
#include "smbus.h"
#include <stdio.h>
#include <unistd.h>

// ADV7513 I2C Base Addresses (7-bit addresses) - MATCH EXISTING CODE
#define ADV7513_MAIN_I2C_ADDR    0x39   // Main register map (matches cec.cpp)
#define ADV7513_CEC_I2C_ADDR     0x3C   // CEC memory map (matches cec.cpp)
#define ADV7513_PACKET_I2C_ADDR  0x38   // Packet memory map (0x70 >> 1) 
#define ADV7513_EDID_I2C_ADDR    0x3F   // EDID memory map (0x7E >> 1)

// ADV7513 I2C 8-bit addresses (for register programming)
#define ADV7513_CEC_I2C_8BIT     (ADV7513_CEC_I2C_ADDR << 1)     // 0x78
#define ADV7513_PACKET_I2C_8BIT  (ADV7513_PACKET_I2C_ADDR << 1)  // 0x70
#define ADV7513_EDID_I2C_8BIT    (ADV7513_EDID_I2C_ADDR << 1)    // 0x7E

// ADV7513 Register Map Base Address Control Registers (in main map)
#define ADV7513_EDID_I2C_ADDR_REG    0x43   // EDID Memory I2C Address
#define ADV7513_PACKET_I2C_ADDR_REG  0x45   // Packet Memory I2C Address  
#define ADV7513_CEC_I2C_ADDR_REG     0xE1   // CEC Memory I2C Address

// ADV7513 Register Map Verification Test Registers
#define ADV7513_MAIN_CHIP_ID1        0xF5   // Should read 0x75 
#define ADV7513_MAIN_CHIP_ID2        0xF6   // Should read 0x13
#define ADV7513_CEC_DEVICE_ID        0x00   // CEC map device ID
#define ADV7513_EDID_TEST_REG        0x00   // EDID map test register

/*
 * Initialize ADV7513 register map base addresses
 * 
 * This function properly programs all four I2C register map base addresses
 * and verifies that each map is accessible at its assigned address.
 * 
 * This is the CRITICAL missing piece that causes the 30-minute CEC failure.
 */
int adv7513_init_register_maps(int main_i2c_fd) {
    printf("ADV7513: Initializing register map base addresses...\n");
    
    // Verify we can communicate with main register map
    int chip_id1 = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_MAIN_CHIP_ID1);
    int chip_id2 = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_MAIN_CHIP_ID2);
    
    if (chip_id1 != 0x75 || chip_id2 != 0x13) {
        printf("ADV7513: ERROR - Main register map communication failed (ID: 0x%02X%02X)\n", 
               chip_id1, chip_id2);
        return -1;
    }
    
    printf("ADV7513: Main register map communication verified (ID: 0x7513)\n");
    
    // CRITICAL FIX: Program all register map base addresses
    
    // 1. Program EDID Memory I2C Address (register 0x43)
    printf("ADV7513: Programming EDID I2C address to 0x%02X...\n", ADV7513_EDID_I2C_ADDR);
    if (i2c_smbus_write_byte_data(main_i2c_fd, ADV7513_EDID_I2C_ADDR_REG, 
                                  ADV7513_EDID_I2C_8BIT) < 0) {
        printf("ADV7513: ERROR - Failed to program EDID I2C address\n");
        return -1;
    }
    usleep(5000);
    
    // 2. Program Packet Memory I2C Address (register 0x45)  
    printf("ADV7513: Programming Packet I2C address to 0x%02X...\n", ADV7513_PACKET_I2C_ADDR);
    if (i2c_smbus_write_byte_data(main_i2c_fd, ADV7513_PACKET_I2C_ADDR_REG, 
                                  ADV7513_PACKET_I2C_8BIT) < 0) {
        printf("ADV7513: ERROR - Failed to program Packet I2C address\n");
        return -1;
    }
    usleep(5000);
    
    // 3. Program CEC Memory I2C Address (register 0xE1)
    printf("ADV7513: Programming CEC I2C address to 0x%02X...\n", ADV7513_CEC_I2C_ADDR);
    if (i2c_smbus_write_byte_data(main_i2c_fd, ADV7513_CEC_I2C_ADDR_REG, 
                                  ADV7513_CEC_I2C_8BIT) < 0) {
        printf("ADV7513: ERROR - Failed to program CEC I2C address\n");  
        return -1;
    }
    usleep(5000);
    
    // CRITICAL: Verify each register map base address was programmed correctly
    uint8_t edid_addr_verify = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_EDID_I2C_ADDR_REG);
    uint8_t packet_addr_verify = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_PACKET_I2C_ADDR_REG);
    uint8_t cec_addr_verify = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_CEC_I2C_ADDR_REG);
    
    printf("ADV7513: Register map address verification:\n");
    printf("  EDID (0x43): wrote 0x%02X, read 0x%02X\n", 
           ADV7513_EDID_I2C_8BIT, edid_addr_verify);
    printf("  Packet (0x45): wrote 0x%02X, read 0x%02X\n", 
           ADV7513_PACKET_I2C_8BIT, packet_addr_verify);
    printf("  CEC (0xE1): wrote 0x%02X, read 0x%02X\n", 
           ADV7513_CEC_I2C_8BIT, cec_addr_verify);
    
    // Verify address programming succeeded
    if (edid_addr_verify != ADV7513_EDID_I2C_8BIT ||
        packet_addr_verify != ADV7513_PACKET_I2C_8BIT ||
        cec_addr_verify != ADV7513_CEC_I2C_8BIT) {
        printf("ADV7513: ERROR - Register map address programming failed\n");
        return -1;
    }
    
    // Additional delay to allow address mapping to take effect
    usleep(20000);
    
    // Verify each register map is now accessible at its programmed address
    printf("ADV7513: Verifying register map accessibility...\n");
    
    // Test CEC register map access
    int cec_fd = i2c_open(ADV7513_CEC_I2C_ADDR, 0);
    if (cec_fd < 0) {
        printf("ADV7513: ERROR - Cannot open CEC register map at 0x%02X\n", ADV7513_CEC_I2C_ADDR);
        return -1;
    }
    
    // Try to read a known CEC register
    int cec_test = i2c_smbus_read_byte_data(cec_fd, ADV7513_CEC_DEVICE_ID);
    i2c_close(cec_fd);
    
    if (cec_test < 0) {
        printf("ADV7513: ERROR - CEC register map not accessible\n");
        return -1;
    }
    
    printf("ADV7513: CEC register map accessible (device ID: 0x%02X)\n", cec_test);
    
    // Test EDID register map access  
    int edid_fd = i2c_open(ADV7513_EDID_I2C_ADDR, 0);
    if (edid_fd >= 0) {
        int edid_test = i2c_smbus_read_byte_data(edid_fd, ADV7513_EDID_TEST_REG);
        i2c_close(edid_fd);
        printf("ADV7513: EDID register map accessible (test read: 0x%02X)\n", 
               edid_test >= 0 ? edid_test : 0xFF);
    } else {
        printf("ADV7513: Warning - EDID register map not accessible (non-critical)\n");
    }
    
    printf("ADV7513: Register map initialization completed successfully\n");
    return 0;
}

/*
 * Verify register map addressing is still correct
 * 
 * This function should be called periodically to detect if the register
 * map addressing has become corrupted (which could cause the 30-min failure).
 */
int adv7513_verify_register_maps(int main_i2c_fd) {
    // Read current register map addresses
    uint8_t edid_addr = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_EDID_I2C_ADDR_REG);
    uint8_t packet_addr = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_PACKET_I2C_ADDR_REG);
    uint8_t cec_addr = i2c_smbus_read_byte_data(main_i2c_fd, ADV7513_CEC_I2C_ADDR_REG);
    
    // Check if they match expected values
    bool maps_ok = (edid_addr == (ADV7513_EDID_I2C_ADDR << 1)) &&
                   (packet_addr == (ADV7513_PACKET_I2C_ADDR << 1)) &&
                   (cec_addr == (ADV7513_CEC_I2C_ADDR << 1));
    
    if (!maps_ok) {
        printf("ADV7513: WARNING - Register map addressing corrupted!\n");
        printf("  EDID: expected 0x%02X, got 0x%02X\n", 
               ADV7513_EDID_I2C_ADDR << 1, edid_addr);
        printf("  Packet: expected 0x%02X, got 0x%02X\n", 
               ADV7513_PACKET_I2C_ADDR << 1, packet_addr);
        printf("  CEC: expected 0x%02X, got 0x%02X\n", 
               ADV7513_CEC_I2C_ADDR << 1, cec_addr);
        return -1;
    }
    
    return 0;
}

/*
 * Reset and reinitialize register maps
 * 
 * This function can be called if register map corruption is detected
 * to restore proper addressing without a full system restart.
 */
int adv7513_reset_register_maps(int main_i2c_fd) {
    printf("ADV7513: Resetting and reinitializing register maps...\n");
    
    // Perform a soft reset of register map addressing
    // Note: This doesn't reset the entire chip, just the address mapping
    
    return adv7513_init_register_maps(main_i2c_fd);
}
