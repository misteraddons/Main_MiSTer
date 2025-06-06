/*
 * Copyright (C) 2024 MiSTer CEC Implementation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <poll.h>
#include <time.h>

#include "cec.h"
#include "hardware.h"
#include "cfg.h"
#include "input.h"
#include "fpga_io.h"
#include "user_io.h"
#include "menu.h"
#include "smbus.h"  // This has i2c_open, i2c_close, i2c_smbus_write_byte_data
#include "user_io.h"

// Function declaration for inject_key
extern "C" {
    void inject_key(int key, int press);
}

// ADV7513 I2C addresses
#define ADV7513_MAIN_I2C_ADDR    0x39
#define ADV7513_CEC_I2C_ADDR     0x3C  // Default CEC I2C address (0x78 >> 1)

// ADV7513 CEC register definitions
#define CEC_TX_FRAME_HEADER      0x00
#define CEC_TX_FRAME_DATA0       0x01
#define CEC_TX_FRAME_LENGTH      0x10
#define CEC_TX_ENABLE_REG        0x11
#define CEC_TX_RETRY             0x12
#define CEC_TX_LOW_DRV_CNT       0x13
#define CEC_RX_FRAME_HEADER      0x15
#define CEC_RX_FRAME_DATA0       0x16
#define CEC_RX_FRAME_LENGTH      0x25
#define CEC_RX_ENABLE_REG        0x26
#define CEC_LOGICAL_ADDR_REG     0x27
#define CEC_LOGICAL_ADDR_MASK    0x28
#define CEC_POWER_MODE           0x2A
#define CEC_INTERRUPT_ENABLE     0x2B
#define CEC_INTERRUPT_CLEAR      0x2C
#define CEC_INTERRUPT_STATUS     0x2D
#define CEC_INTERRUPT2_ENABLE    0x31
#define CEC_INTERRUPT2_STATUS    0x32
#define CEC_TX_BUFFERFREE_TIME   0x46
#define CEC_TX_ARBITER_TIME      0x47
#define CEC_RX_BUFFER1_HEADER    0x49
#define CEC_RX_BUFFER1_DATA0     0x4A
#define CEC_RX_BUFFER1_LENGTH    0x59
#define CEC_RX_BUFFER2_HEADER    0x5A
#define CEC_RX_BUFFER2_DATA0     0x5B
#define CEC_RX_BUFFER2_LENGTH    0x6A
#define CEC_RX_BUFFER3_HEADER    0x6B
#define CEC_RX_BUFFER3_DATA0     0x6C
#define CEC_RX_BUFFER3_LENGTH    0x7A
#define CEC_RX_STATUS            0x7B
#define CEC_RX_BUFFERS           0x7D
#define CEC_CLK_DIV              0x4E

// CEC interrupt bits
#define CEC_INT_TX_DONE          0x01
#define CEC_INT_TX_ARB_LOST      0x02
#define CEC_INT_TX_RETRY_TIMEOUT 0x04
#define CEC_INT_TX_READY         0x08
#define CEC_INT_RX_READY1        0x10
#define CEC_INT_RX_READY2        0x20
#define CEC_INT_RX_READY3        0x40

// CEC opcodes
#define CEC_OP_ACTIVE_SOURCE              0x82
#define CEC_OP_IMAGE_VIEW_ON              0x04
#define CEC_OP_TEXT_VIEW_ON               0x0D
#define CEC_OP_INACTIVE_SOURCE            0x9D
#define CEC_OP_REQUEST_ACTIVE_SOURCE      0x85
#define CEC_OP_ROUTING_CHANGE             0x80
#define CEC_OP_ROUTING_INFORMATION        0x81
#define CEC_OP_SET_STREAM_PATH            0x86
#define CEC_OP_STANDBY                    0x36
#define CEC_OP_RECORD_OFF                 0x0B
#define CEC_OP_RECORD_ON                  0x09
#define CEC_OP_RECORD_STATUS              0x0A
#define CEC_OP_RECORD_TV_SCREEN           0x0F
#define CEC_OP_CLEAR_ANALOGUE_TIMER       0x33
#define CEC_OP_CLEAR_DIGITAL_TIMER        0x99
#define CEC_OP_CLEAR_EXTERNAL_TIMER       0xA1
#define CEC_OP_SET_ANALOGUE_TIMER         0x34
#define CEC_OP_SET_DIGITAL_TIMER          0x97
#define CEC_OP_SET_EXTERNAL_TIMER         0xA2
#define CEC_OP_SET_TIMER_PROGRAM_TITLE    0x67
#define CEC_OP_TIMER_CLEARED_STATUS       0x43
#define CEC_OP_TIMER_STATUS               0x35
#define CEC_OP_CEC_VERSION                0x9E
#define CEC_OP_GET_CEC_VERSION            0x9F
#define CEC_OP_GIVE_PHYSICAL_ADDR         0x83
#define CEC_OP_GET_MENU_LANGUAGE          0x91
#define CEC_OP_REPORT_PHYSICAL_ADDR       0x84
#define CEC_OP_SET_MENU_LANGUAGE          0x32
#define CEC_OP_DECK_CONTROL               0x42
#define CEC_OP_DECK_STATUS                0x1B
#define CEC_OP_GIVE_DECK_STATUS           0x1A
#define CEC_OP_PLAY                       0x41
#define CEC_OP_GIVE_TUNER_DEVICE_STATUS   0x08
#define CEC_OP_SELECT_ANALOGUE_SERVICE    0x92
#define CEC_OP_SELECT_DIGITAL_SERVICE     0x93
#define CEC_OP_TUNER_DEVICE_STATUS        0x07
#define CEC_OP_TUNER_STEP_DECREMENT       0x06
#define CEC_OP_TUNER_STEP_INCREMENT       0x05
#define CEC_OP_DEVICE_VENDOR_ID           0x87
#define CEC_OP_GIVE_DEVICE_VENDOR_ID      0x8C
#define CEC_OP_VENDOR_COMMAND             0x89
#define CEC_OP_VENDOR_COMMAND_WITH_ID     0xA0
#define CEC_OP_VENDOR_REMOTE_BUTTON_DOWN  0x8A
#define CEC_OP_VENDOR_REMOTE_BUTTON_UP    0x8B
#define CEC_OP_SET_OSD_STRING             0x64
#define CEC_OP_GIVE_OSD_NAME              0x46
#define CEC_OP_SET_OSD_NAME               0x47
#define CEC_OP_MENU_REQUEST               0x8D
#define CEC_OP_MENU_STATUS                0x8E
#define CEC_OP_USER_CONTROL_PRESSED       0x44
#define CEC_OP_USER_CONTROL_RELEASED      0x45
#define CEC_OP_GIVE_DEVICE_POWER_STATUS   0x8F
#define CEC_OP_REPORT_POWER_STATUS        0x90
#define CEC_OP_FEATURE_ABORT              0x00
#define CEC_OP_ABORT                      0xFF
#define CEC_OP_GIVE_AUDIO_STATUS          0x71
#define CEC_OP_GIVE_SYSTEM_AUDIO_MODE_STATUS 0x7D
#define CEC_OP_REPORT_AUDIO_STATUS        0x7A
#define CEC_OP_SET_SYSTEM_AUDIO_MODE      0x72
#define CEC_OP_SYSTEM_AUDIO_MODE_REQUEST  0x70
#define CEC_OP_SYSTEM_AUDIO_MODE_STATUS   0x7E
#define CEC_OP_SET_AUDIO_RATE             0x9A
#define CEC_OP_POLLING_MESSAGE            0xFE  // Not a real opcode, internal use

// CEC user control codes
#define CEC_USER_CONTROL_SELECT           0x00
#define CEC_USER_CONTROL_UP               0x01
#define CEC_USER_CONTROL_DOWN             0x02
#define CEC_USER_CONTROL_LEFT             0x03
#define CEC_USER_CONTROL_RIGHT            0x04
#define CEC_USER_CONTROL_RIGHT_UP         0x05
#define CEC_USER_CONTROL_RIGHT_DOWN       0x06
#define CEC_USER_CONTROL_LEFT_UP          0x07
#define CEC_USER_CONTROL_LEFT_DOWN        0x08
#define CEC_USER_CONTROL_ROOT_MENU        0x09
#define CEC_USER_CONTROL_SETUP_MENU       0x0A
#define CEC_USER_CONTROL_CONTENTS_MENU    0x0B
#define CEC_USER_CONTROL_FAVORITE_MENU    0x0C
#define CEC_USER_CONTROL_EXIT             0x0D
#define CEC_USER_CONTROL_NUMBER_0         0x20
#define CEC_USER_CONTROL_NUMBER_1         0x21
#define CEC_USER_CONTROL_NUMBER_2         0x22
#define CEC_USER_CONTROL_NUMBER_3         0x23
#define CEC_USER_CONTROL_NUMBER_4         0x24
#define CEC_USER_CONTROL_NUMBER_5         0x25
#define CEC_USER_CONTROL_NUMBER_6         0x26
#define CEC_USER_CONTROL_NUMBER_7         0x27
#define CEC_USER_CONTROL_NUMBER_8         0x28
#define CEC_USER_CONTROL_NUMBER_9         0x29
#define CEC_USER_CONTROL_PLAY             0x44
#define CEC_USER_CONTROL_STOP             0x45
#define CEC_USER_CONTROL_PAUSE            0x46
#define CEC_USER_CONTROL_RECORD           0x47
#define CEC_USER_CONTROL_REWIND           0x48
#define CEC_USER_CONTROL_FAST_FORWARD     0x49
#define CEC_USER_CONTROL_EJECT            0x4A
#define CEC_USER_CONTROL_FORWARD          0x4B
#define CEC_USER_CONTROL_BACKWARD         0x4C
#define CEC_USER_CONTROL_VOLUME_UP        0x41
#define CEC_USER_CONTROL_VOLUME_DOWN      0x42
#define CEC_USER_CONTROL_MUTE             0x43
#define CEC_USER_CONTROL_F1_BLUE          0x71
#define CEC_USER_CONTROL_F2_RED           0x72
#define CEC_USER_CONTROL_F3_GREEN         0x73
#define CEC_USER_CONTROL_F4_YELLOW        0x74
#define CEC_USER_CONTROL_F5               0x75

// CEC logical addresses
#define CEC_ADDR_TV                       0x00
#define CEC_ADDR_RECORDING_1              0x01
#define CEC_ADDR_RECORDING_2              0x02
#define CEC_ADDR_TUNER_1                  0x03
#define CEC_ADDR_PLAYBACK_1               0x04
#define CEC_ADDR_AUDIO_SYSTEM             0x05
#define CEC_ADDR_TUNER_2                  0x06
#define CEC_ADDR_TUNER_3                  0x07
#define CEC_ADDR_PLAYBACK_2               0x08
#define CEC_ADDR_RECORDING_3              0x09
#define CEC_ADDR_TUNER_4                  0x0A
#define CEC_ADDR_PLAYBACK_3               0x0B
#define CEC_ADDR_FREE_USE                 0x0E
#define CEC_ADDR_BROADCAST                0x0F
#define CEC_ADDR_UNREGISTERED             0x0F

// CEC power status
#define CEC_POWER_STATUS_ON               0x00
#define CEC_POWER_STATUS_STANDBY          0x01
#define CEC_POWER_STATUS_TO_ON            0x02
#define CEC_POWER_STATUS_TO_STANDBY       0x03

// CEC abort reasons
#define CEC_ABORT_UNRECOGNIZED_OP         0x00
#define CEC_ABORT_INCORRECT_MODE          0x01
#define CEC_ABORT_NO_SOURCE               0x02
#define CEC_ABORT_INVALID_OP              0x03
#define CEC_ABORT_REFUSED                 0x04
#define CEC_ABORT_UNABLE_TO_DETERMINE     0x05

static struct {
    int i2c_fd;
    int cec_i2c_fd;
    uint8_t logical_addr;
    uint16_t physical_addr;
    bool enabled;
    bool initialized;
    pthread_t monitor_thread;
    bool thread_running;
    char device_name[32];
    uint8_t power_status;
    bool auto_power_on;
    bool remote_control_enabled;
    cec_callback_t callback;
    void* callback_context;
} cec_state = {
    .i2c_fd = -1,
    .cec_i2c_fd = -1,
    .logical_addr = CEC_ADDR_UNREGISTERED,
    .physical_addr = 0x0000,
    .enabled = false,
    .initialized = false,
    .monitor_thread = 0,  // Add this line
    .thread_running = false,
    .device_name = {0},
    .power_status = CEC_POWER_STATUS_ON,
    .auto_power_on = false,
    .remote_control_enabled = true,
    .callback = NULL,
    .callback_context = NULL
};

// Forward declarations
static int cec_write_reg(uint8_t reg, uint8_t value);
static int cec_read_reg(uint8_t reg, uint8_t *value);
static void* cec_monitor_thread(void* arg);
static int cec_send_message(uint8_t dest, uint8_t opcode, const uint8_t* params, size_t param_len);
static void cec_handle_message(uint8_t src, uint8_t dest, uint8_t opcode, const uint8_t* params, size_t param_len);

// I2C helper functions
static int cec_write_reg(uint8_t reg, uint8_t value) {
    if (cec_state.cec_i2c_fd < 0) return -1;
    
    uint8_t buf[2] = {reg, value};
    if (write(cec_state.cec_i2c_fd, buf, 2) != 2) {
        printf("CEC: I2C write error reg 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }
    return 0;
}

static int cec_read_reg(uint8_t reg, uint8_t *value) {
    if (cec_state.cec_i2c_fd < 0 || !value) return -1;
    
    if (write(cec_state.cec_i2c_fd, &reg, 1) != 1) {
        printf("CEC: I2C write error reg 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }
    
    if (read(cec_state.cec_i2c_fd, value, 1) != 1) {
        printf("CEC: I2C read error reg 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }
    return 0;
}

// Initialize CEC hardware
int cec_init(const char* device_name, bool auto_power, bool remote_control) {
    if (cec_state.initialized) {
        printf("CEC: Already initialized\n");
        return 0;
    }
    
    // Open main ADV7513 I2C device
    cec_state.i2c_fd = i2c_open(ADV7513_MAIN_I2C_ADDR, 0);
    if (cec_state.i2c_fd < 0) {
        printf("CEC: Failed to open ADV7513 main I2C\n");
        return -1;
    }
    
    // Set CEC I2C address in main register map
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE1, ADV7513_CEC_I2C_ADDR << 1);
    
    // Open CEC I2C device
    cec_state.cec_i2c_fd = i2c_open(ADV7513_CEC_I2C_ADDR, 0);
    if (cec_state.cec_i2c_fd < 0) {
        printf("CEC: Failed to open ADV7513 CEC I2C\n");
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
        return -1;
    }
    
    // Power up CEC module
    cec_write_reg(CEC_POWER_MODE, 0x01);
    usleep(10000); // 10ms delay
    
    // Soft reset
    cec_write_reg(CEC_INTERRUPT_CLEAR, 0x04);
    usleep(10000);
    
    // Configure CEC clock - assuming 28.636MHz reference clock for ~40kHz CEC clock
    // Clock divider = 28636000 / (40000 * 16) = ~44.7 ≈ 45
    cec_write_reg(CEC_CLK_DIV, 45);
    
    // Set up timing parameters
    cec_write_reg(CEC_TX_RETRY, 0x02);           // 2 retries
    cec_write_reg(CEC_TX_LOW_DRV_CNT, 0x3C);     // Standard drive strength
    cec_write_reg(CEC_TX_BUFFERFREE_TIME, 0x07); // 7 bit periods
    cec_write_reg(CEC_TX_ARBITER_TIME, 0x03);    // 3 bit periods
    
    // Enable RX
    cec_write_reg(CEC_RX_ENABLE_REG, 0x01);
    
    // Clear all interrupts
    cec_write_reg(CEC_INTERRUPT_CLEAR, 0xFF);
    cec_write_reg(0x30, 0xFF); // Clear interrupt 2
    
    // Enable interrupts
    cec_write_reg(CEC_INTERRUPT_ENABLE, 
        CEC_INT_TX_DONE | CEC_INT_TX_READY | 
        CEC_INT_RX_READY1 | CEC_INT_RX_READY2 | CEC_INT_RX_READY3);
    
    // Store configuration
    strncpy(cec_state.device_name, device_name ? device_name : "MiSTer", 
            sizeof(cec_state.device_name) - 1);
    cec_state.auto_power_on = auto_power;
    cec_state.remote_control_enabled = remote_control;
    
    cec_state.initialized = true;
    cec_state.enabled = false; // Will be enabled after physical address discovery
    
    printf("CEC: Initialized successfully\n");
    return 0;
}

// Configure CEC with physical address from EDID
int cec_configure(uint16_t physical_addr) {
    if (!cec_state.initialized) {
        printf("CEC: Not initialized\n");
        return -1;
    }
    
    cec_state.physical_addr = physical_addr;
    printf("CEC: Configuring with physical address %d.%d.%d.%d\n",
           (physical_addr >> 12) & 0xF, (physical_addr >> 8) & 0xF,
           (physical_addr >> 4) & 0xF, physical_addr & 0xF);
    
    // Try to claim logical address for playback device
    uint8_t logical_addrs[] = {CEC_ADDR_PLAYBACK_1, CEC_ADDR_PLAYBACK_2, CEC_ADDR_PLAYBACK_3};
    
    for (int i = 0; i < 3; i++) {
        // Send polling message
        cec_write_reg(CEC_TX_FRAME_HEADER, (logical_addrs[i] << 4) | logical_addrs[i]);
        cec_write_reg(CEC_TX_FRAME_LENGTH, 1);
        cec_write_reg(CEC_TX_ENABLE_REG, 0x01);
        
        // Wait for transmission
        usleep(50000); // 50ms
        
        // Check if ACK received (address in use)
        uint8_t status;
        cec_read_reg(CEC_INTERRUPT_STATUS, &status);
        
        if (!(status & CEC_INT_TX_DONE)) {
            // No ACK, address is free
            cec_state.logical_addr = logical_addrs[i];
            break;
        }
        
        // Clear interrupt
        cec_write_reg(CEC_INTERRUPT_CLEAR, CEC_INT_TX_DONE);
    }
    
    if (cec_state.logical_addr == CEC_ADDR_UNREGISTERED) {
        printf("CEC: Failed to claim logical address\n");
        return -1;
    }
    
    // Set logical address
    cec_write_reg(CEC_LOGICAL_ADDR_REG, cec_state.logical_addr);
    cec_write_reg(CEC_LOGICAL_ADDR_MASK, (1 << cec_state.logical_addr));
    
    printf("CEC: Claimed logical address 0x%X\n", cec_state.logical_addr);
    
    // Announce physical address
    uint8_t params[3] = {
        (uint8_t)(physical_addr >> 8),
        (uint8_t)(physical_addr & 0xFF),
        0x04  // Device type: Playback Device
    };
    cec_send_message(CEC_ADDR_BROADCAST, CEC_OP_REPORT_PHYSICAL_ADDR, params, 3);
    
    // Set OSD name
    cec_send_message(CEC_ADDR_TV, CEC_OP_SET_OSD_NAME, 
                     (uint8_t*)cec_state.device_name, strlen(cec_state.device_name));
    
    // Start monitor thread
    cec_state.thread_running = true;
    if (pthread_create(&cec_state.monitor_thread, NULL, cec_monitor_thread, NULL) != 0) {
        printf("CEC: Failed to create monitor thread\n");
        cec_state.thread_running = false;
        return -1;
    }
    
    cec_state.enabled = true;
    
    // Perform One Touch Play if configured
    if (cec_state.auto_power_on) {
        cec_one_touch_play();
    }
    
    return 0;
}

// Shutdown CEC
void cec_shutdown(void) {
    if (!cec_state.initialized) return;
    
    cec_state.enabled = false;
    
    // Stop monitor thread
    if (cec_state.thread_running) {
        cec_state.thread_running = false;
        pthread_join(cec_state.monitor_thread, NULL);
    }
    
    // Power down CEC
    if (cec_state.cec_i2c_fd >= 0) {
        cec_write_reg(CEC_POWER_MODE, 0x00);
    }
    
    // Close I2C devices
    if (cec_state.cec_i2c_fd >= 0) {
        i2c_close(cec_state.cec_i2c_fd);
        cec_state.cec_i2c_fd = -1;
    }
    
    if (cec_state.i2c_fd >= 0) {
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
    }
    
    cec_state.initialized = false;
    printf("CEC: Shutdown complete\n");
}

// Send CEC message
static int cec_send_message(uint8_t dest, uint8_t opcode, const uint8_t* params, size_t param_len) {
    if (!cec_state.enabled) return -1;
    
    // Build message
    uint8_t header = (cec_state.logical_addr << 4) | dest;
    
    // Write header
    cec_write_reg(CEC_TX_FRAME_HEADER, header);
    
    // Write opcode if not polling message
    size_t msg_len = 1;
    if (opcode != CEC_OP_POLLING_MESSAGE) {
        cec_write_reg(CEC_TX_FRAME_DATA0, opcode);
        msg_len++;
        
        // Write parameters
        for (size_t i = 0; i < param_len && i < 14; i++) {
            cec_write_reg(CEC_TX_FRAME_DATA0 + 1 + i, params[i]);
            msg_len++;
        }
    }
    
    // Set frame length
    cec_write_reg(CEC_TX_FRAME_LENGTH, msg_len);
    
    // Start transmission
    cec_write_reg(CEC_TX_ENABLE_REG, 0x01);
    
    // Wait for completion (with timeout)
    int timeout = 100; // 100ms
    while (timeout > 0) {
        uint8_t status;
        cec_read_reg(CEC_INTERRUPT_STATUS, &status);
        
        if (status & CEC_INT_TX_DONE) {
            cec_write_reg(CEC_INTERRUPT_CLEAR, CEC_INT_TX_DONE);
            return 0;
        }
        
        if (status & (CEC_INT_TX_ARB_LOST | CEC_INT_TX_RETRY_TIMEOUT)) {
            cec_write_reg(CEC_INTERRUPT_CLEAR, 
                         CEC_INT_TX_ARB_LOST | CEC_INT_TX_RETRY_TIMEOUT);
            printf("CEC: TX failed - arbitration lost or timeout\n");
            return -1;
        }
        
        usleep(1000);
        timeout--;
    }
    
    printf("CEC: TX timeout\n");
    return -1;
}

// Monitor thread for receiving CEC messages
static void* cec_monitor_thread(void* arg) {
    (void)arg;
    
    //struct pollfd pfd;
    //pfd.fd = cec_state.cec_i2c_fd;
    //pfd.events = POLLIN;
    
    printf("CEC: Monitor thread started\n");
    
    while (cec_state.thread_running) {
        // Check for RX messages
        uint8_t int_status;
        if (cec_read_reg(CEC_INTERRUPT_STATUS, &int_status) == 0) {
            // Check RX ready flags
            if (int_status & (CEC_INT_RX_READY1 | CEC_INT_RX_READY2 | CEC_INT_RX_READY3)) {
                uint8_t rx_status;
                cec_read_reg(CEC_RX_STATUS, &rx_status);
                
                // Process each buffer
                for (int buf = 0; buf < 3; buf++) {
                    if (rx_status & (1 << buf)) {
                        uint8_t header_reg = CEC_RX_FRAME_HEADER;
                        uint8_t data_reg = CEC_RX_FRAME_DATA0;
                        uint8_t len_reg = CEC_RX_FRAME_LENGTH;
                        
                        if (buf == 1) {
                            header_reg = CEC_RX_BUFFER1_HEADER;
                            data_reg = CEC_RX_BUFFER1_DATA0;
                            len_reg = CEC_RX_BUFFER1_LENGTH;
                        } else if (buf == 2) {
                            header_reg = CEC_RX_BUFFER2_HEADER;
                            data_reg = CEC_RX_BUFFER2_DATA0;
                            len_reg = CEC_RX_BUFFER2_LENGTH;
                        } else if (buf == 3) {
                            header_reg = CEC_RX_BUFFER3_HEADER;
                            data_reg = CEC_RX_BUFFER3_DATA0;
                            len_reg = CEC_RX_BUFFER3_LENGTH;
                        }
                        
                        uint8_t header, length;
                        cec_read_reg(header_reg, &header);
                        cec_read_reg(len_reg, &length);
                        
                        uint8_t src = (header >> 4) & 0x0F;
                        uint8_t dest = header & 0x0F;
                        
                        if (length > 1) {
                            uint8_t opcode;
                            uint8_t params[14];
                            
                            cec_read_reg(data_reg, &opcode);
                            
                            size_t param_len = 0;
                            if (length > 2) {
                                param_len = length - 2;
                                for (size_t i = 0; i < param_len; i++) {
                                    cec_read_reg(data_reg + 1 + i, &params[i]);
                                }
                            }
                            
                            // Handle message
                            cec_handle_message(src, dest, opcode, params, param_len);
                        }
                        
                        // Clear buffer
                        cec_write_reg(CEC_RX_BUFFERS, 1 << buf);
                    }
                }
                
                // Clear RX interrupts
                cec_write_reg(CEC_INTERRUPT_CLEAR, 
                             CEC_INT_RX_READY1 | CEC_INT_RX_READY2 | CEC_INT_RX_READY3);
            }
        }
        
        usleep(10000); // 10ms poll interval
    }
    
    printf("CEC: Monitor thread stopped\n");
    return NULL;
}

// Handle received CEC message
static void cec_handle_message(uint8_t src, uint8_t dest, uint8_t opcode, 
                               const uint8_t* params, size_t param_len) {
    printf("CEC RX: src=%X dest=%X op=%02X len=%zu\n", src, dest, opcode, param_len);
    
    // Call user callback if registered
    if (cec_state.callback) {
        struct cec_message msg = {
            .src = src,
            .dest = dest,
            .opcode = opcode,
            .params = {0},  // Initialize the entire array
            .param_len = 0
        };
        memcpy(msg.params, params, param_len);
        
        cec_state.callback(&msg, cec_state.callback_context);
    }
    
    // Handle standard CEC messages
    switch (opcode) {
        case CEC_OP_GIVE_OSD_NAME:
            cec_send_message(src, CEC_OP_SET_OSD_NAME, 
                           (uint8_t*)cec_state.device_name, strlen(cec_state.device_name));
            break;
            
        case CEC_OP_GIVE_DEVICE_VENDOR_ID:
            {
                uint8_t vendor_id[3] = {0x00, 0x00, 0x00}; // Unknown vendor
                cec_send_message(src, CEC_OP_DEVICE_VENDOR_ID, vendor_id, 3);
            }
            break;
            
        case CEC_OP_GIVE_PHYSICAL_ADDR:
            {
                uint8_t addr_params[3] = {
                    (uint8_t)(cec_state.physical_addr >> 8),
                    (uint8_t)(cec_state.physical_addr & 0xFF),
                    0x04  // Device type: Playback Device
                };
                cec_send_message(CEC_ADDR_BROADCAST, CEC_OP_REPORT_PHYSICAL_ADDR, 
                               addr_params, 3);
            }
            break;
            
        case CEC_OP_GET_CEC_VERSION:
            {
                uint8_t version = 0x05; // CEC Version 1.4
                cec_send_message(src, CEC_OP_CEC_VERSION, &version, 1);
            }
            break;
            
        case CEC_OP_GIVE_DEVICE_POWER_STATUS:
            cec_send_message(src, CEC_OP_REPORT_POWER_STATUS, 
                           &cec_state.power_status, 1);
            break;
            
        case CEC_OP_REQUEST_ACTIVE_SOURCE:
            // Only respond if we are the active source
            // This would be determined by higher-level logic
            break;
            
        case CEC_OP_SET_STREAM_PATH:
            if (param_len >= 2) {
                uint16_t addr = (params[0] << 8) | params[1];
                if (addr == cec_state.physical_addr) {
                    // We are being selected as active source
                    uint8_t active_params[2] = {
                        (uint8_t)(cec_state.physical_addr >> 8),
                        (uint8_t)(cec_state.physical_addr & 0xFF)
                    };
                    cec_send_message(CEC_ADDR_BROADCAST, CEC_OP_ACTIVE_SOURCE, 
                                   active_params, 2);
                }
            }
            break;
            
        case CEC_OP_STANDBY:
            // Enter standby mode
            cec_state.power_status = CEC_POWER_STATUS_STANDBY;
            printf("CEC: Entering standby mode\n");
            // Notify MiSTer to enter low power mode
            if (cec_state.callback) {
                struct cec_message msg = {
                    .src = src,
                    .dest = dest,
                    .opcode = opcode,
                    .params = {0},  // Initialize the entire array
                    .param_len = 0
                };
                cec_state.callback(&msg, cec_state.callback_context);
            }
            break;
            
        case CEC_OP_USER_CONTROL_PRESSED:
            if (param_len >= 1 && cec_state.remote_control_enabled) {
                uint8_t key_code = params[0];
                int mister_key = 0;
                
                // Map CEC keys to MiSTer keys
                switch (key_code) {
                    case CEC_USER_CONTROL_UP:        mister_key = KEY_UP; break;
                    case CEC_USER_CONTROL_DOWN:      mister_key = KEY_DOWN; break;
                    case CEC_USER_CONTROL_LEFT:      mister_key = KEY_LEFT; break;
                    case CEC_USER_CONTROL_RIGHT:     mister_key = KEY_RIGHT; break;
                    case CEC_USER_CONTROL_SELECT:    mister_key = KEY_ENTER; break;
                    case CEC_USER_CONTROL_EXIT:      mister_key = KEY_ESC; break;
                    case CEC_USER_CONTROL_ROOT_MENU: mister_key = KEY_F12; break;
                    case CEC_USER_CONTROL_PLAY:      mister_key = KEY_SPACE; break;
                    case CEC_USER_CONTROL_PAUSE:     mister_key = KEY_P; break;
                    case CEC_USER_CONTROL_STOP:      mister_key = KEY_S; break;
                    case CEC_USER_CONTROL_F1_BLUE:   mister_key = KEY_F1; break;
                    case CEC_USER_CONTROL_F2_RED:    mister_key = KEY_F2; break;
                    case CEC_USER_CONTROL_F3_GREEN:  mister_key = KEY_F3; break;
                    case CEC_USER_CONTROL_F4_YELLOW: mister_key = KEY_F4; break;
                    case CEC_USER_CONTROL_NUMBER_0:
                    case CEC_USER_CONTROL_NUMBER_1:
                    case CEC_USER_CONTROL_NUMBER_2:
                    case CEC_USER_CONTROL_NUMBER_3:
                    case CEC_USER_CONTROL_NUMBER_4:
                    case CEC_USER_CONTROL_NUMBER_5:
                    case CEC_USER_CONTROL_NUMBER_6:
                    case CEC_USER_CONTROL_NUMBER_7:
                    case CEC_USER_CONTROL_NUMBER_8:
                    case CEC_USER_CONTROL_NUMBER_9:
                        mister_key = KEY_0 + (key_code - CEC_USER_CONTROL_NUMBER_0);
                        break;
                }
                
                if (mister_key != 0) {
                    // Inject key press into MiSTer input system
                    printf("CEC: Remote key press 0x%02X -> MiSTer key %d\n", 
                           key_code, mister_key);
                    // Call input injection function
                    //user_io_kbd_inject(mister_key, 1); // Press
                }
            }
            break;
            
        case CEC_OP_USER_CONTROL_RELEASED:
            if (cec_state.remote_control_enabled) {
                // Log key release
                printf("CEC: Remote key released\n");
                // TODO: Implement key release handling
            }
            break;
            
        case CEC_OP_MENU_REQUEST:
            if (param_len >= 1) {
                uint8_t menu_state = 0x00; // Menu inactive
                if (is_menu()) {
                    menu_state = 0x01; // Menu active
                }
                cec_send_message(src, CEC_OP_MENU_STATUS, &menu_state, 1);
            }
            break;
            
        default:
            // Unknown opcode - send feature abort
            if (dest != CEC_ADDR_BROADCAST) {
                uint8_t abort_params[2] = {opcode, CEC_ABORT_UNRECOGNIZED_OP};
                cec_send_message(src, CEC_OP_FEATURE_ABORT, abort_params, 2);
            }
            break;
    }
}

// Public API functions

// Perform One Touch Play
int cec_one_touch_play(void) {
    if (!cec_state.enabled) return -1;
    
    printf("CEC: Performing One Touch Play\n");
    
    // Wake TV
    cec_send_message(CEC_ADDR_TV, CEC_OP_IMAGE_VIEW_ON, NULL, 0);
    
    // Small delay
    usleep(100000); // 100ms
    
    // Announce as active source
    uint8_t params[2] = {
        (uint8_t)(cec_state.physical_addr >> 8),
        (uint8_t)(cec_state.physical_addr & 0xFF)
    };
    cec_send_message(CEC_ADDR_BROADCAST, CEC_OP_ACTIVE_SOURCE, params, 2);
    
    cec_state.power_status = CEC_POWER_STATUS_ON;
    
    return 0;
}

// Send TV to standby
int cec_standby_tv(void) {
    if (!cec_state.enabled) return -1;
    
    printf("CEC: Sending TV to standby\n");
    return cec_send_message(CEC_ADDR_TV, CEC_OP_STANDBY, NULL, 0);
}

// Set callback for CEC messages
void cec_set_callback(cec_callback_t callback, void* context) {
    cec_state.callback = callback;
    cec_state.callback_context = context;
}

// Get CEC status
bool cec_is_enabled(void) {
    return cec_state.enabled;
}

// Get logical address
uint8_t cec_get_logical_address(void) {
    return cec_state.logical_addr;
}

// Get physical address
uint16_t cec_get_physical_address(void) {
    return cec_state.physical_addr;
}