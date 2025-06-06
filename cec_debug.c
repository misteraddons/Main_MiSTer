// CEC I2C Address Verification and Debug Tools

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Standard CEC I2C addresses
#define CEC_I2C_ADDR_PRIMARY    0x34    // Primary CEC address (most common)
#define CEC_I2C_ADDR_SECONDARY  0x35    // Secondary CEC address
#define CEC_I2C_ADDR_ALTERNATE  0x36    // Some devices use this

// HDMI transmitter common addresses
#define ADV7513_I2C_ADDR        0x39    // ADV7513 HDMI transmitter
#define SIL9022_I2C_ADDR        0x39    // SiI9022 HDMI transmitter
#define IT66121_I2C_ADDR        0x4C    // IT66121 HDMI transmitter

// CEC register addresses (varies by chip)
#define CEC_REG_DEVICE_ID       0x00
#define CEC_REG_REVISION        0x01
#define CEC_REG_LOGICAL_ADDR    0x04
#define CEC_REG_STATUS          0x05
#define CEC_REG_CONTROL         0x06

// Function to scan I2C bus for devices
void scan_i2c_bus(const char* i2c_device) 
{
    int file;
    char filename[20];
    
    printf("Scanning I2C bus %s for devices...\n", i2c_device);
    
    if ((file = open(i2c_device, O_RDWR)) < 0) {
        printf("Failed to open I2C bus %s\n", i2c_device);
        return;
    }
    
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    
    for (int addr = 0; addr < 128; addr++) {
        if (addr % 16 == 0) {
            printf("%02x: ", addr);
        }
        
        if (ioctl(file, I2C_SLAVE, addr) < 0) {
            printf("-- ");
        } else {
            // Try to read one byte
            if (read(file, filename, 1) == 1) {
                printf("%02x ", addr);
            } else {
                printf("-- ");
            }
        }
        
        if (addr % 16 == 15) {
            printf("\n");
        }
    }
    
    close(file);
}

// Function to probe specific CEC addresses
bool probe_cec_address(const char* i2c_device, uint8_t addr) 
{
    int file;
    uint8_t buffer[2];
    
    if ((file = open(i2c_device, O_RDWR)) < 0) {
        printf("Failed to open I2C bus %s\n", i2c_device);
        return false;
    }
    
    if (ioctl(file, I2C_SLAVE, addr) < 0) {
        printf("Failed to set I2C address 0x%02X\n", addr);
        close(file);
        return false;
    }
    
    // Try to read device ID register
    buffer[0] = CEC_REG_DEVICE_ID;
    if (write(file, buffer, 1) != 1) {
        close(file);
        return false;
    }
    
    if (read(file, buffer, 1) != 1) {
        close(file);
        return false;
    }
    
    printf("Found device at 0x%02X, ID: 0x%02X\n", addr, buffer[0]);
    close(file);
    return true;
}

// Function to check HDMI transmitter chip
void check_hdmi_transmitter(const char* i2c_device) 
{
    printf("\nChecking HDMI transmitter chips...\n");
    
    // Check for ADV7513
    if (probe_cec_address(i2c_device, ADV7513_I2C_ADDR)) {
        printf("Possible ADV7513 HDMI transmitter found at 0x%02X\n", ADV7513_I2C_ADDR);
    }
    
    // Check for IT66121
    if (probe_cec_address(i2c_device, IT66121_I2C_ADDR)) {
        printf("Possible IT66121 HDMI transmitter found at 0x%02X\n", IT66121_I2C_ADDR);
    }
}

// Function to verify CEC functionality
void verify_cec_functionality(const char* i2c_device, uint8_t cec_addr) 
{
    int file;
    uint8_t buffer[16];
    
    printf("\nVerifying CEC functionality at address 0x%02X...\n", cec_addr);
    
    if ((file = open(i2c_device, O_RDWR)) < 0) {
        printf("Failed to open I2C bus %s\n", i2c_device);
        return;
    }
    
    if (ioctl(file, I2C_SLAVE, cec_addr) < 0) {
        printf("Failed to set I2C address 0x%02X\n", cec_addr);
        close(file);
        return;
    }
    
    // Read device information
    buffer[0] = CEC_REG_DEVICE_ID;
    if (write(file, buffer, 1) == 1 && read(file, buffer, 1) == 1) {
        printf("Device ID: 0x%02X\n", buffer[0]);
    }
    
    buffer[0] = CEC_REG_REVISION;
    if (write(file, buffer, 1) == 1 && read(file, buffer, 1) == 1) {
        printf("Revision: 0x%02X\n", buffer[0]);
    }
    
    buffer[0] = CEC_REG_STATUS;
    if (write(file, buffer, 1) == 1 && read(file, buffer, 1) == 1) {
        printf("Status: 0x%02X\n", buffer[0]);
    }
    
    close(file);
}

// Function to set CEC logical address and device name
bool set_cec_device_info(const char* i2c_device, uint8_t cec_addr) 
{
    int file;
    uint8_t buffer[16];
    
    printf("\nSetting CEC device information...\n");
    
    if ((file = open(i2c_device, O_RDWR)) < 0) {
        printf("Failed to open I2C bus %s\n", i2c_device);
        return false;
    }
    
    if (ioctl(file, I2C_SLAVE, cec_addr) < 0) {
        printf("Failed to set I2C address 0x%02X\n", cec_addr);
        close(file);
        return false;
    }
    
    // Set logical address to 4 (Playback Device 1)
    buffer[0] = CEC_REG_LOGICAL_ADDR;
    buffer[1] = 0x04;  // Logical address 4
    if (write(file, buffer, 2) != 2) {
        printf("Failed to set logical address\n");
        close(file);
        return false;
    }
    
    printf("Set CEC logical address to 4 (Playback Device)\n");
    
    // Enable CEC functionality
    buffer[0] = CEC_REG_CONTROL;
    buffer[1] = 0x01;  // Enable CEC
    if (write(file, buffer, 2) != 2) {
        printf("Failed to enable CEC\n");
        close(file);
        return false;
    }
    
    printf("Enabled CEC functionality\n");
    
    close(file);
    return true;
}

// Function to send CEC "Report Physical Address" message
bool send_cec_report_physical_address(const char* i2c_device, uint8_t cec_addr) 
{
    int file;
    uint8_t buffer[16];
    
    if ((file = open(i2c_device, O_RDWR)) < 0) {
        return false;
    }
    
    if (ioctl(file, I2C_SLAVE, cec_addr) < 0) {
        close(file);
        return false;
    }
    
    // CEC message: Report Physical Address
    // Format: [Header][Opcode][Physical Address High][Physical Address Low][Device Type]
    buffer[0] = 0x4F;  // Header: Source=4, Destination=15 (broadcast)
    buffer[1] = 0x84;  // Opcode: Report Physical Address
    buffer[2] = 0x00;  // Physical Address High byte
    buffer[3] = 0x00;  // Physical Address Low byte  
    buffer[4] = 0x04;  // Device Type: Playback Device
    
    // Write CEC message (implementation depends on your CEC controller)
    // This is a generic example - you'll need to adapt for your specific chip
    
    close(file);
    return true;
}

// Function to send CEC "Set OSD Name" message
bool send_cec_set_osd_name(const char* i2c_device, uint8_t cec_addr, const char* name) 
{
    int file;
    uint8_t buffer[16];
    int name_len = strlen(name);
    
    if (name_len > 14) name_len = 14;  // CEC OSD name max length
    
    if ((file = open(i2c_device, O_RDWR)) < 0) {
        return false;
    }
    
    if (ioctl(file, I2C_SLAVE, cec_addr) < 0) {
        close(file);
        return false;
    }
    
    // CEC message: Set OSD Name
    // This would typically be sent in response to a "Give OSD Name" command
    buffer[0] = 0x40;  // Header: Source=4, Destination=0 (TV)
    buffer[1] = 0x47;  // Opcode: Set OSD Name
    
    // Copy device name
    for (int i = 0; i < name_len; i++) {
        buffer[2 + i] = name[i];
    }
    
    // Write CEC message (implementation depends on your CEC controller)
    
    close(file);
    return true;
}

// Main diagnostic function
void diagnose_cec_setup() 
{
    printf("=== MiSTer HDMI CEC Diagnostic ===\n\n");
    
    // Common I2C buses on MiSTer
    const char* i2c_buses[] = {
        "/dev/i2c-0",
        "/dev/i2c-1", 
        "/dev/i2c-2"
    };
    
    for (int i = 0; i < 3; i++) {
        printf("Checking I2C bus: %s\n", i2c_buses[i]);
        
        // Check if bus exists
        if (access(i2c_buses[i], F_OK) == 0) {
            scan_i2c_bus(i2c_buses[i]);
            check_hdmi_transmitter(i2c_buses[i]);
            
            // Check standard CEC addresses
            printf("\nChecking CEC addresses...\n");
            if (probe_cec_address(i2c_buses[i], CEC_I2C_ADDR_PRIMARY)) {
                verify_cec_functionality(i2c_buses[i], CEC_I2C_ADDR_PRIMARY);
                set_cec_device_info(i2c_buses[i], CEC_I2C_ADDR_PRIMARY);
            }
            if (probe_cec_address(i2c_buses[i], CEC_I2C_ADDR_SECONDARY)) {
                verify_cec_functionality(i2c_buses[i], CEC_I2C_ADDR_SECONDARY);
            }
            if (probe_cec_address(i2c_buses[i], CEC_I2C_ADDR_ALTERNATE)) {
                verify_cec_functionality(i2c_buses[i], CEC_I2C_ADDR_ALTERNATE);
            }
        } else {
            printf("Bus %s not found\n", i2c_buses[i]);
        }
        printf("\n");
    }
}

// Command line tool usage
int main(int argc, char *argv[]) 
{
    if (argc > 1 && strcmp(argv[1], "scan") == 0) {
        diagnose_cec_setup();
    } else if (argc > 1 && strcmp(argv[1], "test") == 0) {
        // Test CEC functionality
        const char* device = (argc > 2) ? argv[2] : "/dev/i2c-1";
        uint8_t addr = (argc > 3) ? strtol(argv[3], NULL, 16) : CEC_I2C_ADDR_PRIMARY;
        
        verify_cec_functionality(device, addr);
        set_cec_device_info(device, addr);
        send_cec_set_osd_name(device, addr, "MiSTer");
    } else {
        printf("Usage:\n");
        printf("  %s scan          - Scan for I2C devices and CEC controllers\n", argv[0]);
        printf("  %s test [device] [addr] - Test CEC at specific device/address\n", argv[0]);
        printf("\nExample:\n");
        printf("  %s scan\n", argv[0]);
        printf("  %s test /dev/i2c-1 0x34\n", argv[0]);
    }
    
    return 0;
}