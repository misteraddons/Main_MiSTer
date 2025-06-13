// hdmi_cec.cpp - HDMI CEC implementation for MiSTer
// Following ADV7513 Programming Guide for CEC configuration

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "hdmi_cec.h"
#include "smbus.h"

// ADV7513 CEC I2C address (7-bit)
#define CEC_I2C_ADDR 0x3C

// CEC Register addresses from ADV7513 Programming Guide
#define CEC_TX_FRAME_HEADER     0x00
#define CEC_TX_FRAME_DATA0      0x01
#define CEC_TX_FRAME_LENGTH     0x10
#define CEC_TX_ENABLE_REG       0x11
#define CEC_TX_RETRY            0x12
#define CEC_RX_ENABLE_REG       0x26  // CEC RX Enable is 0x26[6] per datasheet
#define CEC_RX_FRAME_HEADER     0x15
#define CEC_RX_FRAME_DATA0      0x16
#define CEC_RX_FRAME_LENGTH     0x25
#define CEC_RX_READY_REG        0x4B
#define CEC_LOGICAL_ADDR_REG    0x4C
#define CEC_CLK_DIV_REG         0x4E
#define CEC_SOFT_RESET_REG      0x50

// Initialize CEC subsystem
int hdmi_cec_init(void)
{
    printf("CEC: Starting initialization...\n");
    
    int fd = i2c_open(CEC_I2C_ADDR, 0);
    if (fd < 0) {
        printf("CEC: Failed to open I2C device at 0x%02X\n", CEC_I2C_ADDR);
        return -1;
    }

    // Reset CEC subsystem as per friend's suggestion
    // Write 0x01 to 0x50, then 0x00 to the same register
    if (i2c_smbus_write_byte_data(fd, CEC_SOFT_RESET_REG, 0x01) < 0) {
        printf("CEC: Failed to set reset CEC subsystem\n");
        i2c_close(fd);
        return -1;
    }
    
    usleep(5000); // 5ms delay
    
    if (i2c_smbus_write_byte_data(fd, CEC_SOFT_RESET_REG, 0x00) < 0) {
        printf("CEC: Failed to clear reset CEC subsystem\n");
        i2c_close(fd);
        return -1;
    }
    
    printf("CEC: Reset CEC subsystem (0x01 then 0x00 to register 0x50)\n");
    usleep(5000); // 5ms delay for reset to complete
    
    // Configure CEC to use external oscillator (register 0x9C)
    // Bit 0: 0 = External oscillator, 1 = Internal oscillator
    if (i2c_smbus_write_byte_data(fd, 0x9C, 0x00) < 0) {
        printf("CEC: Failed to set external oscillator mode\n");
        i2c_close(fd);
        return -1;
    }
    printf("CEC: Configured for external oscillator\n");
    
    // Configure main ADV7513 chip for CEC operation
    int main_fd = i2c_open(0x39, 0); // ADV7513 main address
    if (main_fd >= 0) {
        // Check current 0xE2 value
        int e2_current = i2c_smbus_read_byte_data(main_fd, 0xE2);
        printf("CEC: Main chip 0xE2 current value: 0x%02X\n", e2_current);
        
        // Write 0x00 to 0xE2 to ensure CEC is powered up
        if (i2c_smbus_write_byte_data(main_fd, 0xE2, 0x00) < 0) {
            printf("CEC: Failed to power up CEC on main chip\n");
        } else {
            printf("CEC: Powered up CEC on main ADV7513 chip (0xE2=0x00)\n");
        }
        
        // Check main chip CEC configuration registers
        int reg_fa = i2c_smbus_read_byte_data(main_fd, 0xFA);
        int reg_fb = i2c_smbus_read_byte_data(main_fd, 0xFB);
        printf("CEC: Main chip diagnostic - 0xFA:0x%02X 0xFB:0x%02X\n", reg_fa, reg_fb);
        
        // Check and configure HPD and CEC pin mux settings
        int reg_d0 = i2c_smbus_read_byte_data(main_fd, 0xD0);
        printf("CEC: Main chip register 0xD0 (HPD/CEC control): 0x%02X\n", reg_d0);
        
        // Ensure CEC pin is properly configured (register 0xD0 bit 3 should be 0 for CEC function)
        if (reg_d0 & 0x08) {
            printf("CEC: Configuring CEC pin function (clearing bit 3 in 0xD0)\n");
            if (i2c_smbus_write_byte_data(main_fd, 0xD0, reg_d0 & ~0x08) < 0) {
                printf("CEC: Failed to configure CEC pin function\n");
            } else {
                int new_d0 = i2c_smbus_read_byte_data(main_fd, 0xD0);
                printf("CEC: Updated register 0xD0: 0x%02X\n", new_d0);
            }
        }
        
        // Check HPD status to ensure display is connected
        int reg_42 = i2c_smbus_read_byte_data(main_fd, 0x42);
        printf("CEC: HPD status (0x42): 0x%02X %s\n", reg_42, 
               (reg_42 & 0x40) ? "[HPD HIGH - Display Connected]" : "[HPD LOW - No Display]");
        
        // Check HDMI/DVI mode - CEC only works in HDMI mode
        int reg_af = i2c_smbus_read_byte_data(main_fd, 0xAF);
        printf("CEC: HDMI/DVI mode (0xAF): 0x%02X %s\n", reg_af,
               (reg_af & 0x02) ? "[HDMI Mode]" : "[DVI Mode - CEC not available]");
        
        // Ensure we're in HDMI mode for CEC to work
        if (!(reg_af & 0x02)) {
            printf("CEC: Setting HDMI mode (required for CEC)\n");
            if (i2c_smbus_write_byte_data(main_fd, 0xAF, reg_af | 0x02) < 0) {
                printf("CEC: Failed to set HDMI mode\n");
            } else {
                int new_af = i2c_smbus_read_byte_data(main_fd, 0xAF);
                printf("CEC: Updated HDMI/DVI mode: 0x%02X\n", new_af);
            }
        }
        
        // Check if TMDS is enabled (required for CEC)
        int reg_d5 = i2c_smbus_read_byte_data(main_fd, 0xD5);
        printf("CEC: TMDS termination (0xD5): 0x%02X\n", reg_d5);
        
        i2c_close(main_fd);
    }
    
    // Set CEC clock divider - let's double-check the calculation
    // ADV7513 CEC requires 750kHz ± 2% base frequency
    // With 12MHz external clock: 12,000,000 / 750,000 = 16
    // But ADV7513 uses (divider + 1), so we need divider = 15
    // Register 0x4E: [7:2] = Clock Divider (15), [1:0] = Power Mode (00)
    // Alternative: try slightly different divider to see if timing is the issue
    uint8_t clk_div_value = 0x3C; // (15 << 2) | 0x00 = 60 decimal
    printf("CEC: Using clock divider %d for 750kHz CEC clock\n", (clk_div_value >> 2));
    if (i2c_smbus_write_byte_data(fd, CEC_CLK_DIV_REG, clk_div_value) < 0) {
        printf("CEC: Failed to set clock divider\n");
        i2c_close(fd);
        return -1;
    }
    
    // Verify clock divider was set correctly
    int clk_readback = i2c_smbus_read_byte_data(fd, CEC_CLK_DIV_REG);
    if (clk_readback >= 0) {
        printf("CEC: Clock divider register readback: 0x%02X (expected 0x%02X)\n", clk_readback, clk_div_value);
    }
    
    // Set logical address to 4 (Playback Device) 
    // But first clear any existing address to ensure clean state
    if (i2c_smbus_write_byte_data(fd, CEC_LOGICAL_ADDR_REG, 0x0F) < 0) {
        printf("CEC: Failed to clear logical address\n");
        i2c_close(fd);
        return -1;
    }
    usleep(1000);
    
    if (i2c_smbus_write_byte_data(fd, CEC_LOGICAL_ADDR_REG, 0x04) < 0) {
        printf("CEC: Failed to set logical address\n");
        i2c_close(fd);
        return -1;
    }
    
    // Verify logical address was set
    int addr_readback = i2c_smbus_read_byte_data(fd, CEC_LOGICAL_ADDR_REG);
    printf("CEC: Logical address readback: 0x%02X (expected 0x04)\n", addr_readback);
    
    // Configure CEC RX timing parameters (register 0x51)
    // Set appropriate timing for CEC signal detection
    if (i2c_smbus_write_byte_data(fd, 0x51, 0x02) < 0) {
        printf("CEC: Failed to configure RX timing\n");
    } else {
        printf("CEC: Configured RX timing parameters\n");
    }
    
    // Configure CEC filter settings (register 0x52) 
    // Enable filtering for better signal integrity
    if (i2c_smbus_write_byte_data(fd, 0x52, 0x03) < 0) {
        printf("CEC: Failed to configure signal filtering\n");
    } else {
        printf("CEC: Configured signal filtering\n");
    }
    
    // Enable CEC TX - set bit 0 (0x01) in register 0x11
    printf("CEC: Writing TX enable (bit 0) to register 0x%02X\n", CEC_TX_ENABLE_REG);
    if (i2c_smbus_write_byte_data(fd, CEC_TX_ENABLE_REG, 0x01) < 0) {
        printf("CEC: Failed to enable CEC TX\n");
        i2c_close(fd);
        return -1;
    }
    
    // Verify TX write
    usleep(1000);
    int tx_readback = i2c_smbus_read_byte_data(fd, CEC_TX_ENABLE_REG);
    printf("CEC: TX register 0x%02X readback: 0x%02X\n", CEC_TX_ENABLE_REG, tx_readback);
    
    // Enable CEC RX - set bit 6 (0x40) in register 0x26
    printf("CEC: Writing RX enable (bit 6) to register 0x%02X\n", CEC_RX_ENABLE_REG);
    if (i2c_smbus_write_byte_data(fd, CEC_RX_ENABLE_REG, 0x40) < 0) {
        printf("CEC: Failed to enable CEC RX\n");
        i2c_close(fd);
        return -1;
    }
    
    // Configure CEC RX Buffer Control (register 0x4A)
    // Enable automatic buffer clearing and set proper timing
    if (i2c_smbus_write_byte_data(fd, 0x4A, 0x01) < 0) {
        printf("CEC: Failed to configure RX buffer control\n");
    } else {
        printf("CEC: Configured RX buffer control\n");
    }
    
    // Configure CEC interrupt mask (register 0x27)
    // Enable RX ready interrupt
    if (i2c_smbus_write_byte_data(fd, 0x27, 0x40) < 0) {
        printf("CEC: Failed to configure interrupt mask\n");
    } else {
        printf("CEC: Configured interrupt mask for RX ready\n");
    }
    
    // Verify RX write
    usleep(1000);
    int rx_readback = i2c_smbus_read_byte_data(fd, CEC_RX_ENABLE_REG);
    printf("CEC: RX register 0x%02X readback: 0x%02X\n", CEC_RX_ENABLE_REG, rx_readback);
    
    i2c_close(fd);
    printf("CEC: Initialized successfully\n");
    
    // Send device name to TV
    usleep(500000); // 500ms delay before sending messages
    hdmi_cec_send_device_name("MiSTer");
    
    // Send active source message
    usleep(200000); // 200ms delay between messages
    hdmi_cec_send_active_source();
    
    // Send report physical address message
    usleep(200000); // 200ms delay between messages
    hdmi_cec_report_physical_address();
    
    // Check initial status and verify hardware configuration
    usleep(100000); // 100ms delay before status check
    printf("CEC: Initial status check:\n");
    hdmi_cec_check_tx_status();
    
    // Perform complete hardware verification
    hdmi_cec_verify_hardware();
    
    // Test basic CEC controller before attempting real messages
    printf("CEC: Testing CEC controller basic functionality...\n");
    int test_fd = i2c_open(CEC_I2C_ADDR, 0);
    if (test_fd >= 0) {
        // Try writing and reading from a test register to verify I2C communication
        int original_val = i2c_smbus_read_byte_data(test_fd, 0x4C);
        printf("CEC: Original logical address: 0x%02X\n", original_val);
        
        // Test writing a different value and reading it back
        i2c_smbus_write_byte_data(test_fd, 0x4C, 0x05);
        usleep(1000);
        int test_val = i2c_smbus_read_byte_data(test_fd, 0x4C);
        printf("CEC: Test write/read: wrote 0x05, read 0x%02X\n", test_val);
        
        // Restore original value
        i2c_smbus_write_byte_data(test_fd, 0x4C, original_val);
        
        // Check if the arbitration lost bit is stuck
        printf("CEC: Checking if arbitration bit is stuck...\n");
        for (int i = 0; i < 3; i++) {
            int status = i2c_smbus_read_byte_data(test_fd, 0x12);
            printf("CEC: TX Status read %d: 0x%02X\n", i, status);
            
            // Try different methods to clear the bit
            if (status & 0x02) {
                // Method 1: Write the bit back
                i2c_smbus_write_byte_data(test_fd, 0x12, 0x02);
                usleep(1000);
                
                // Method 2: Toggle entire transmitter
                i2c_smbus_write_byte_data(test_fd, 0x11, 0x00);
                usleep(5000);
                i2c_smbus_write_byte_data(test_fd, 0x11, 0x01);
                usleep(5000);
                
                int new_status = i2c_smbus_read_byte_data(test_fd, 0x12);
                printf("CEC: Status after clear attempts: 0x%02X\n", new_status);
            }
        }
        
        // Try alternative clock divider values to see if timing is the issue
        printf("CEC: Testing alternative clock dividers...\n");
        int original_clk = i2c_smbus_read_byte_data(test_fd, 0x4E);
        
        // Try divider 14 (14+1=15, should give 800kHz)
        i2c_smbus_write_byte_data(test_fd, 0x4E, 0x38); // 14 << 2
        usleep(1000);
        int status_14 = i2c_smbus_read_byte_data(test_fd, 0x12);
        printf("CEC: With divider 14: TX status 0x%02X\n", status_14);
        
        // Try divider 16 (16+1=17, should give ~706kHz)  
        i2c_smbus_write_byte_data(test_fd, 0x4E, 0x40); // 16 << 2
        usleep(1000);
        int status_16 = i2c_smbus_read_byte_data(test_fd, 0x12);
        printf("CEC: With divider 16: TX status 0x%02X\n", status_16);
        
        // Restore original
        i2c_smbus_write_byte_data(test_fd, 0x4E, original_clk);
        printf("CEC: Restored original clock divider\n");
        
        i2c_close(test_fd);
    }
    
    // Check CEC line connectivity first
    printf("CEC: Checking CEC line connectivity...\n");
    int connectivity_fd = i2c_open(CEC_I2C_ADDR, 0);
    if (connectivity_fd >= 0) {
        // Check if CEC line is pulled high (idle state should be high)
        for (int i = 0; i < 5; i++) {
            int line_check = i2c_smbus_read_byte_data(connectivity_fd, 0x14);
            int status_check = i2c_smbus_read_byte_data(connectivity_fd, 0x12);
            int ready_check = i2c_smbus_read_byte_data(connectivity_fd, 0x4B);
            printf("CEC: Line check %d - Line:0x%02X Status:0x%02X Ready:0x%02X\n", 
                   i, line_check, status_check, ready_check);
            usleep(100000); // 100ms between checks
        }
        i2c_close(connectivity_fd);
    }
    
    // Send a polling message to TV (logical address 0)
    printf("CEC: Sending polling message to TV...\n");
    hdmi_cec_send_polling_message();
    usleep(200000);
    
    // Send Give Device Vendor ID to TV
    printf("CEC: Requesting TV vendor ID...\n");
    hdmi_cec_request_vendor_id();
    usleep(200000);
    
    // Poll for incoming CEC messages
    printf("CEC: Polling for incoming messages...\n");
    for (int i = 0; i < 5; i++) {
        hdmi_cec_poll_messages();
        usleep(500000); // 500ms between polls
    }
    
    return 0;
}

// Deinitialize CEC subsystem
void hdmi_cec_deinit(void)
{
    int fd = i2c_open(CEC_I2C_ADDR, 0);
    if (fd >= 0) {
        // Disable CEC TX and RX
        i2c_smbus_write_byte_data(fd, CEC_TX_ENABLE_REG, 0x00);
        i2c_smbus_write_byte_data(fd, CEC_RX_ENABLE_REG, 0x00);
        i2c_close(fd);
    }
}

// Check if CEC is connected
int hdmi_cec_is_connected(void)
{
    // For now, just return success if we can open the I2C device
    int fd = i2c_open(CEC_I2C_ADDR, 0);
    if (fd >= 0) {
        i2c_close(fd);
        return 1;
    }
    return 0;
}

// Get physical address from EDID
int hdmi_cec_get_physical_address(uint16_t* address)
{
    // Default physical address
    *address = 0x1000; // 1.0.0.0
    return 0;
}

// Send CEC message with retry logic for busy bus
int hdmi_cec_send_message(const uint8_t* data, uint8_t length)
{
    if (!data || length == 0 || length > 16) {
        printf("CEC: Invalid message parameters\n");
        return -1;
    }
    
    printf("CEC: Sending %d-byte message: ", length);
    for (int i = 0; i < length; i++) {
        printf("0x%02X ", data[i]);
    }
    printf("\n");
    
    // Try multiple times with better timing based on oscilloscope observation
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            // Based on oscilloscope: TV sends every ~1 second
            // Wait longer to find a truly quiet period
            int delay_ms = 1200 + (retry * 200); // 1200ms, 1400ms, 1600ms delays
            printf("CEC: Retry %d, waiting %dms to find bus quiet period\n", retry, delay_ms);
            usleep(delay_ms * 1000);
            
            // After delay, monitor bus for quiet period
            printf("CEC: Monitoring bus activity before transmission...\n");
            bool bus_quiet = true;
            int temp_fd = i2c_open(CEC_I2C_ADDR, 0);
            if (temp_fd >= 0) {
                for (int monitor = 0; monitor < 10; monitor++) {
                    int line_status = i2c_smbus_read_byte_data(temp_fd, 0x14);
                    int rx_ready = i2c_smbus_read_byte_data(temp_fd, 0x4B);
                    if ((line_status & 0x10) || (rx_ready & 0x01)) {
                        printf("CEC: Bus activity detected during monitor %d\n", monitor);
                        bus_quiet = false;
                        break;
                    }
                    usleep(10000); // 10ms between checks
                }
                i2c_close(temp_fd);
                
                if (!bus_quiet) {
                    printf("CEC: Bus still active, continuing to next retry\n");
                    continue;
                }
                printf("CEC: Bus appears quiet, proceeding with transmission\n");
            }
        }
        
        int fd = i2c_open(CEC_I2C_ADDR, 0);
        if (fd < 0) {
            printf("CEC: Failed to open I2C for message send\n");
            continue;
        }
        
        // Write header
        if (i2c_smbus_write_byte_data(fd, CEC_TX_FRAME_HEADER, data[0]) < 0) {
            printf("CEC: Failed to write header\n");
            i2c_close(fd);
            continue;
        }
        
        // Write data bytes
        bool write_failed = false;
        for (int i = 1; i < length; i++) {
            if (i2c_smbus_write_byte_data(fd, CEC_TX_FRAME_DATA0 + i - 1, data[i]) < 0) {
                printf("CEC: Failed to write data byte %d\n", i);
                write_failed = true;
                break;
            }
        }
        
        if (write_failed) {
            i2c_close(fd);
            continue;
        }
        
        // Clear any existing status flags before transmitting
        int clear_status = i2c_smbus_read_byte_data(fd, 0x12);
        if (clear_status != 0x01) { // Should be TX Ready (0x01) only
            printf("CEC: Clearing TX status: 0x%02X\n", clear_status);
            // Write back to clear status flags
            i2c_smbus_write_byte_data(fd, 0x12, clear_status);
            usleep(5000); // Wait longer for clear
            
            // Check if it cleared properly
            int new_status = i2c_smbus_read_byte_data(fd, 0x12);
            printf("CEC: TX status after clear: 0x%02X\n", new_status);
            
            // If still not ready, try resetting TX enable
            if (new_status != 0x01) {
                printf("CEC: TX not ready, toggling TX enable\n");
                i2c_smbus_write_byte_data(fd, CEC_TX_ENABLE_REG, 0x00);
                usleep(1000);
                i2c_smbus_write_byte_data(fd, CEC_TX_ENABLE_REG, 0x01);
                usleep(5000);
                
                int final_status = i2c_smbus_read_byte_data(fd, 0x12);
                printf("CEC: TX status after toggle: 0x%02X\n", final_status);
            }
        }
        
        // Check bus status before transmitting with detailed analysis
        int pre_line_status = i2c_smbus_read_byte_data(fd, 0x14);
        int pre_tx_status = i2c_smbus_read_byte_data(fd, 0x12);
        int cec_status = i2c_smbus_read_byte_data(fd, 0x4C);
        int buf_status = i2c_smbus_read_byte_data(fd, 0x4A);
        
        printf("CEC: Pre-TX Status - Line:0x%02X TX:0x%02X CEC:0x%02X Buf:0x%02X\n", 
               pre_line_status, pre_tx_status, cec_status, buf_status);
        
        // Analyze line status for issues
        if (pre_line_status & 0x01) printf("CEC: WARNING - Line shows TX Error before transmission\n");
        if (pre_line_status & 0x02) printf("CEC: WARNING - Line shows TX Arbitration Lost before transmission\n");
        if (pre_line_status & 0x10) printf("CEC: INFO - RX Ready flag set\n");
        
        // Check if CEC line is idle (should be high when idle)
        int line_state = i2c_smbus_read_byte_data(fd, 0x13);
        printf("CEC: CEC line state register 0x13: 0x%02X %s\n", line_state,
               (line_state & 0x01) ? "[Line HIGH - Idle]" : "[Line LOW - Busy/Error]");
        
        // Set frame length and trigger transmission
        uint8_t tx_length = (length - 1) | 0x80; // Set bit 7 to trigger TX
        printf("CEC: Writing TX length: 0x%02X\n", tx_length);
        if (i2c_smbus_write_byte_data(fd, CEC_TX_FRAME_LENGTH, tx_length) < 0) {
            printf("CEC: Failed to trigger transmission\n");
            i2c_close(fd);
            continue;
        }
        
        // Check status immediately after triggering
        usleep(1000); // 1ms
        int immediate_status = i2c_smbus_read_byte_data(fd, 0x12);
        printf("CEC: Immediate TX status: 0x%02X\n", immediate_status);
        
        // Wait for transmission to complete and check multiple times
        for (int check = 0; check < 10; check++) {
            usleep(5000); // 5ms intervals
            int tx_status = i2c_smbus_read_byte_data(fd, 0x12);
            int line_status = i2c_smbus_read_byte_data(fd, 0x14);
            
            printf("CEC: Check %d - TX:0x%02X Line:0x%02X\n", check, tx_status, line_status);
            
            if (tx_status & 0x08) { // TX Done
                printf("CEC: TX Done successfully on retry %d (check %d)\n", retry, check);
                i2c_close(fd);
                return 0;
            }
            if (tx_status & 0x02) { // Arbitration Lost
                printf("CEC: TX Arbitration Lost on retry %d (check %d)\n", retry, check);
                break;
            }
            if (tx_status & 0x04) { // Retry Exceeded
                printf("CEC: TX Retry Exceeded on retry %d (check %d)\n", retry, check);
                break;
            }
        }
        
        i2c_close(fd);
    }
    
    printf("CEC: All retries failed - bus too busy\n");
    return -1;
}

// Receive CEC message
int hdmi_cec_receive_message(uint8_t* data, uint8_t* length)
{
    if (!data || !length) {
        return -1;
    }
    
    int fd = i2c_open(CEC_I2C_ADDR, 0);
    if (fd < 0) {
        return -1;
    }
    
    // Check if message is ready
    int ready = i2c_smbus_read_byte_data(fd, CEC_RX_READY_REG);
    if (ready < 0) {
        printf("CEC: Failed to read RX ready register\n");
        i2c_close(fd);
        return -1;
    }
    
    if (!(ready & 0x01)) {
        i2c_close(fd);
        return -1; // No message ready
    }
    
    printf("CEC: RX ready, reading message (ready=0x%02X)\n", ready);
    
    // Read header
    int header = i2c_smbus_read_byte_data(fd, CEC_RX_FRAME_HEADER);
    if (header < 0) {
        i2c_close(fd);
        return -1;
    }
    data[0] = (uint8_t)header;
    
    // Read frame length
    int frame_length = i2c_smbus_read_byte_data(fd, CEC_RX_FRAME_LENGTH);
    if (frame_length < 0) {
        i2c_close(fd);
        return -1;
    }
    
    // Read data bytes
    for (int i = 0; i < frame_length && i < 15; i++) {
        int byte = i2c_smbus_read_byte_data(fd, CEC_RX_FRAME_DATA0 + i);
        if (byte < 0) {
            i2c_close(fd);
            return -1;
        }
        data[i + 1] = (uint8_t)byte;
    }
    
    *length = frame_length + 1;
    
    // Clear RX ready flag
    i2c_smbus_write_byte_data(fd, CEC_RX_READY_REG, 0x01);
    
    i2c_close(fd);
    return 0;
}

// Send device name (OSD Name) to TV
int hdmi_cec_send_device_name(const char* name)
{
    if (!name) return -1;
    
    // CEC message format: [Header][Opcode][Device Name...]
    // Header: Source=4 (Playback), Destination=0 (TV) = 0x40
    // Opcode: Set OSD Name = 0x47
    uint8_t message[16];
    message[0] = 0x40; // Header: from Playback(4) to TV(0)
    message[1] = 0x47; // Set OSD Name opcode
    
    // Copy device name (max 14 characters)
    int name_len = strlen(name);
    if (name_len > 14) name_len = 14;
    
    for (int i = 0; i < name_len; i++) {
        message[2 + i] = name[i];
    }
    
    printf("CEC: Sending device name '%s'\n", name);
    return hdmi_cec_send_message(message, 2 + name_len);
}

// Send active source message to announce ourselves
int hdmi_cec_send_active_source(void)
{
    // CEC message format: [Header][Opcode][Physical Address High][Physical Address Low]
    // Header: Source=4 (Playback), Destination=15 (Broadcast) = 0x4F
    // Opcode: Active Source = 0x82
    // Physical Address: 1.0.0.0 = 0x1000
    uint8_t message[4];
    message[0] = 0x4F; // Header: from Playback(4) to Broadcast(F)
    message[1] = 0x82; // Active Source opcode
    message[2] = 0x10; // Physical address high byte
    message[3] = 0x00; // Physical address low byte
    
    printf("CEC: Sending active source (1.0.0.0)\n");
    return hdmi_cec_send_message(message, 4);
}

// Send report physical address message 
int hdmi_cec_report_physical_address(void)
{
    // CEC message format: [Header][Opcode][Physical Address High][Physical Address Low][Device Type]
    // Header: Source=4 (Playback), Destination=15 (Broadcast) = 0x4F
    // Opcode: Report Physical Address = 0x84
    // Physical Address: 1.0.0.0 = 0x1000
    // Device Type: Playback Device = 0x04
    uint8_t message[5];
    message[0] = 0x4F; // Header: from Playback(4) to Broadcast(F)
    message[1] = 0x84; // Report Physical Address opcode
    message[2] = 0x10; // Physical address high byte
    message[3] = 0x00; // Physical address low byte
    message[4] = 0x04; // Device type: Playback Device
    
    printf("CEC: Reporting physical address (1.0.0.0) as Playback Device\n");
    return hdmi_cec_send_message(message, 5);
}

// Send polling message to check if TV responds
int hdmi_cec_send_polling_message(void)
{
    // Try a different approach - polling to our own address first
    // This should succeed if hardware is working
    uint8_t message[1];
    message[0] = 0x44; // Header: from Playback(4) to Playback(4) - self poll
    
    printf("CEC: Sending self-polling message (4->4)\n");
    int result = hdmi_cec_send_message(message, 1);
    
    if (result == 0) {
        printf("CEC: Self-poll succeeded, now trying TV poll\n");
        message[0] = 0x40; // Header: from Playback(4) to TV(0)
        printf("CEC: Sending polling message to TV\n");
        return hdmi_cec_send_message(message, 1);
    } else {
        printf("CEC: Self-poll failed, hardware issue detected\n");
        return result;
    }
}

// Request vendor ID from TV
int hdmi_cec_request_vendor_id(void)
{
    // Give Device Vendor ID message: [Header][Opcode]
    // Header: Source=4 (Playback), Destination=0 (TV) = 0x40
    // Opcode: Give Device Vendor ID = 0x8C
    uint8_t message[2];
    message[0] = 0x40; // Header: from Playback(4) to TV(0)
    message[1] = 0x8C; // Give Device Vendor ID opcode
    
    printf("CEC: Requesting vendor ID from TV\n");
    return hdmi_cec_send_message(message, 2);
}

// Check CEC transmit status
int hdmi_cec_check_tx_status(void)
{
    int fd = i2c_open(CEC_I2C_ADDR, 0);
    if (fd < 0) {
        return -1;
    }
    
    // Read TX enable register
    int tx_enable = i2c_smbus_read_byte_data(fd, CEC_TX_ENABLE_REG);
    
    // Read RX enable register  
    int rx_enable = i2c_smbus_read_byte_data(fd, CEC_RX_ENABLE_REG);
    
    // Read clock divider register
    int clk_div = i2c_smbus_read_byte_data(fd, CEC_CLK_DIV_REG);
    
    // Read logical address register
    int logical_addr = i2c_smbus_read_byte_data(fd, CEC_LOGICAL_ADDR_REG);
    
    i2c_close(fd);
    
    if (tx_enable >= 0 && rx_enable >= 0 && clk_div >= 0 && logical_addr >= 0) {
        printf("CEC Status: TX=%s(0x%02X) RX=%s(0x%02X) CLK=0x%02X ADDR=%d\n",
               (tx_enable & 0x01) ? "ON" : "OFF", tx_enable,
               (rx_enable & 0x40) ? "ON" : "OFF", rx_enable,
               clk_div, logical_addr);
        return (tx_enable & 0x01) ? 1 : 0;
    }
    
    printf("CEC Status: Failed to read registers\n");
    return -1;
}

// Poll for incoming CEC messages
void hdmi_cec_poll_messages(void)
{
    int fd = i2c_open(CEC_I2C_ADDR, 0);
    if (fd < 0) {
        return;
    }
    
    // Check interrupt status first (register 0x27)
    int interrupt_status = i2c_smbus_read_byte_data(fd, 0x27);
    if (interrupt_status >= 0) {
        printf("CEC: Interrupt status: 0x%02X\n", interrupt_status);
        if (interrupt_status & 0x40) {
            printf("CEC: RX interrupt detected\n");
        }
    }
    
    // Check CEC status register (register 0x4C)
    int cec_status = i2c_smbus_read_byte_data(fd, 0x4C);
    if (cec_status >= 0) {
        printf("CEC: Status register: 0x%02X\n", cec_status);
    }
    
    // Check CEC line status (register 0x14)
    int line_status = i2c_smbus_read_byte_data(fd, 0x14);
    if (line_status >= 0) {
        printf("CEC: Line status: 0x%02X", line_status);
        if (line_status & 0x01) printf(" [TX Error]");
        if (line_status & 0x02) printf(" [TX Arbitration Lost]");
        if (line_status & 0x04) printf(" [TX Retry Exceeded]");
        if (line_status & 0x08) printf(" [TX Done]");
        if (line_status & 0x10) printf(" [RX Ready]");
        if (line_status & 0x20) printf(" [RX EOM]");
        if (line_status & 0x40) printf(" [RX Error]");
        printf("\n");
    }
    
    // Also check TX status register (0x12)
    int tx_status = i2c_smbus_read_byte_data(fd, 0x12);
    if (tx_status >= 0) {
        printf("CEC: TX status: 0x%02X", tx_status);
        if (tx_status & 0x01) printf(" [TX Ready]");
        if (tx_status & 0x02) printf(" [TX Arbitration Lost]"); 
        if (tx_status & 0x04) printf(" [TX Retry Exceeded]");
        if (tx_status & 0x08) printf(" [TX Timeout]");
        printf("\n");
        
        // Clear status bits by writing back the same value
        if (tx_status & 0x0E) { // If any error bits are set
            i2c_smbus_write_byte_data(fd, 0x12, tx_status);
            printf("CEC: Cleared TX status flags\n");
        }
    }
    
    // Check if message is ready
    int ready = i2c_smbus_read_byte_data(fd, CEC_RX_READY_REG);
    if (ready >= 0) {
        printf("CEC: RX Ready register: 0x%02X\n", ready);
        
        if (ready & 0x01) {
            printf("CEC: Message available! Reading...\n");
            
            uint8_t message[16];
            uint8_t length;
            if (hdmi_cec_receive_message(message, &length) == 0) {
                printf("CEC: Received %d-byte message: ", length);
                for (int i = 0; i < length; i++) {
                    printf("0x%02X ", message[i]);
                }
                printf("\n");
                
                // Clear interrupt status after reading
                i2c_smbus_write_byte_data(fd, 0x27, interrupt_status);
            } else {
                printf("CEC: Failed to read incoming message\n");
            }
        } else {
            printf("CEC: No messages waiting (ready=0x%02X)\n", ready);
        }
    } else {
        printf("CEC: Failed to read RX Ready register\n");
    }
    
    i2c_close(fd);
}

// Verify complete CEC hardware configuration
void hdmi_cec_verify_hardware(void)
{
    printf("CEC: === Complete Hardware Verification ===\n");
    
    // Check main chip configuration
    int main_fd = i2c_open(0x39, 0);
    if (main_fd >= 0) {
        printf("CEC: Main ADV7513 chip registers:\n");
        
        // Key registers for CEC operation
        int regs[] = {0x42, 0xD0, 0xE2, 0xFA, 0xFB};
        const char* names[] = {"HPD Status", "Pin Config", "CEC Power", "Diag FA", "Diag FB"};
        
        for (int i = 0; i < 5; i++) {
            int val = i2c_smbus_read_byte_data(main_fd, regs[i]);
            printf("CEC:   0x%02X (%s): 0x%02X\n", regs[i], names[i], val);
        }
        
        i2c_close(main_fd);
    }
    
    // Check CEC chip configuration
    int cec_fd = i2c_open(CEC_I2C_ADDR, 0);
    if (cec_fd >= 0) {
        printf("CEC: CEC chip registers:\n");
        
        // Key CEC registers
        int cec_regs[] = {0x11, 0x12, 0x13, 0x14, 0x26, 0x4C, 0x4E, 0x50};
        const char* cec_names[] = {"TX Enable", "TX Status", "Line State", "Line Status", "RX Enable", "Logic Addr", "Clock Div", "Reset"};
        
        for (int i = 0; i < 8; i++) {
            int val = i2c_smbus_read_byte_data(cec_fd, cec_regs[i]);
            printf("CEC:   0x%02X (%s): 0x%02X\n", cec_regs[i], cec_names[i], val);
        }
        
        i2c_close(cec_fd);
    }
    
    printf("CEC: === End Hardware Verification ===\n");
}

// Monitor CEC status and periodically announce device
void hdmi_cec_monitor_status(void)
{
    static int announcement_counter = 0;
    
    // Check status every call
    int tx_status = hdmi_cec_check_tx_status();
    
    // Poll for messages every call
    hdmi_cec_poll_messages();
    
    // Re-announce device every 50 calls (adjust as needed)
    announcement_counter++;
    if (announcement_counter >= 50) {
        announcement_counter = 0;
        
        if (tx_status == 1) {
            printf("CEC: Periodic re-announcement\n");
            usleep(10000); // 10ms delay
            hdmi_cec_send_device_name("MiSTer");
            usleep(50000); // 50ms delay
            hdmi_cec_send_active_source();
        } else {
            printf("CEC: TX disabled, skipping re-announcement\n");
        }
    }
}