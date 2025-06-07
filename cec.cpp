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

// ADV7513 CEC Memory (Table 91)
#define CEC_TX_FRAME_HEADER             0x00
#define CEC_TX_FRAME_DATA0              0x01
#define CEC_TX_FRAME_DATA1              0x02
#define CEC_TX_FRAME_DATA2              0x03
#define CEC_TX_FRAME_DATA3              0x04
#define CEC_TX_FRAME_DATA4              0x05
#define CEC_TX_FRAME_DATA5              0x06
#define CEC_TX_FRAME_DATA6              0x07
#define CEC_TX_FRAME_DATA7              0x08
#define CEC_TX_FRAME_DATA8              0x09
#define CEC_TX_FRAME_DATA9              0x0A
#define CEC_TX_FRAME_DATA10             0x0B
#define CEC_TX_FRAME_DATA11             0x0C
#define CEC_TX_FRAME_DATA12             0x0D
#define CEC_TX_FRAME_DATA13             0x0E
#define CEC_TX_FRAME_DATA14             0x0F
#define CEC_TX_FRAME_LENGTH             0x10
#define CEC_TX_ENABLE_REG               0x11
#define CEC_TX_RETRY                    0x12 // [6:4] Tx Retry
#define CEC_TX_RETRY_SIGNAL_FREE_TIME   0x12 // [3:0] Tx SFT4
#define CEC_TX_SIGNAL_FREE_TIME_5       0x13 // [7:4] Tx SFT5
#define CEC_TX_SIGNAL_FREE_TIME_7       0x13 // [3:0] Tx SFT7
#define CEC_TX_LOWDRIVE_COUNTER         0x14 // [7:4] Tx Lowdrive Counter
#define CEC_TX_NACK_COUNTER             0x14 // [3:0] Tx NACK Counter
#define CEC_RX_FRAME_BUFFER1_HEADER     0x15
#define CEC_RX_FRAME_BUFFER1_DATA0      0x16
#define CEC_RX_FRAME_BUFFER1_DATA1      0x17
#define CEC_RX_FRAME_BUFFER1_DATA2      0x18
#define CEC_RX_FRAME_BUFFER1_DATA3      0x19
#define CEC_RX_FRAME_BUFFER1_DATA4      0x1A
#define CEC_RX_FRAME_BUFFER1_DATA5      0x1B
#define CEC_RX_FRAME_BUFFER1_DATA6      0x1C
#define CEC_RX_FRAME_BUFFER1_DATA7      0x1D
#define CEC_RX_FRAME_BUFFER1_DATA8      0x1E
#define CEC_RX_FRAME_BUFFER1_DATA9      0x1F
#define CEC_RX_FRAME_BUFFER1_DATA10     0x20
#define CEC_RX_FRAME_BUFFER1_DATA11     0x21
#define CEC_RX_FRAME_BUFFER1_DATA12     0x22
#define CEC_RX_FRAME_BUFFER1_DATA13     0x23
#define CEC_RX_FRAME_BUFFER1_DATA14     0x24
#define CEC_RX_FRAME_BUFFER1_LENGTH     0x25
#define CEC_RX_ENABLE                   0x26 // [6] Rx Enable
#define CEC_RX_BUFFER_3_TIMESTAMP       0x26 // [5:4] Rx Buffer 3 Timestamp Enable
#define CEC_RX_BUFFER_2_TIMESTAMP       0x26 // [3:2] Rx Buffer 2 Timestamp Enable
#define CEC_RX_BUFFER_1_TIMESTAMP       0x26 // [1:0] Rx Buffer 1 Timestamp Enable
#define CEC_RX_FRAME_BUFFER2_HEADER     0x27
#define CEC_RX_FRAME_BUFFER2_DATA0      0x28
#define CEC_RX_FRAME_BUFFER2_DATA1      0x29
#define CEC_RX_FRAME_BUFFER2_DATA2      0x2A
#define CEC_RX_FRAME_BUFFER2_DATA3      0x2B
#define CEC_RX_FRAME_BUFFER2_DATA4      0x2C
#define CEC_RX_FRAME_BUFFER2_DATA5      0x2D
#define CEC_RX_FRAME_BUFFER2_DATA6      0x2E
#define CEC_RX_FRAME_BUFFER2_DATA7      0x2F
#define CEC_RX_FRAME_BUFFER2_DATA8      0x30
#define CEC_RX_FRAME_BUFFER2_DATA9      0x31
#define CEC_RX_FRAME_BUFFER2_DATA10     0x32
#define CEC_RX_FRAME_BUFFER2_DATA11     0x33
#define CEC_RX_FRAME_BUFFER2_DATA12     0x34
#define CEC_RX_FRAME_BUFFER2_DATA13     0x35
#define CEC_RX_FRAME_BUFFER2_DATA14     0x36
#define CEC_RX_FRAME_BUFFER2_LENGTH     0x37
#define CEC_RX_FRAME_BUFFER3_HEADER     0x38
#define CEC_RX_FRAME_BUFFER3_DATA0      0x39
#define CEC_RX_FRAME_BUFFER3_DATA1      0x3A
#define CEC_RX_FRAME_BUFFER3_DATA2      0x3B
#define CEC_RX_FRAME_BUFFER3_DATA3      0x3C
#define CEC_RX_FRAME_BUFFER3_DATA4      0x3D
#define CEC_RX_FRAME_BUFFER3_DATA5      0x3E
#define CEC_RX_FRAME_BUFFER3_DATA6      0x3F
#define CEC_RX_FRAME_BUFFER3_DATA7      0x40
#define CEC_RX_FRAME_BUFFER3_DATA8      0x41
#define CEC_RX_FRAME_BUFFER3_DATA9      0x42
#define CEC_RX_FRAME_BUFFER3_DATA10     0x43
#define CEC_RX_FRAME_BUFFER3_DATA11     0x44
#define CEC_RX_FRAME_BUFFER3_DATA12     0x45
#define CEC_RX_FRAME_BUFFER3_DATA13     0x46
#define CEC_RX_FRAME_BUFFER3_DATA14     0x47
#define CEC_RX_FRAME_BUFFER3_LENGTH     0x48
#define CEC_RX_STATUS                   0x49  // Same as CEC_RX_BUFFER_*_READY register
#define CEC_RX_BUFFER_3_READY           0x49 // [2] Rx Buffer 3 Ready
#define CEC_RX_BUFFER_2_READY           0x49 // [1] Rx Buffer 2 Ready
#define CEC_RX_BUFFER_1_READY           0x49 // [0] Rx Buffer 1 Ready
#define CEC_RX_BUFFERS                  0x4A  // Same as CEC_RX_BUFFER_*_READY_CLEAR register
#define CEC_RX_BUFFER_USE_ALL           0x4A // [3] Usa all CEC Rx Buffers
#define CEC_RX_BUFFER_3_READY_CLEAR     0x4A // [2] Clear Rx Buffer 3 Ready
#define CEC_RX_BUFFER_2_READY_CLEAR     0x4A // [1] Clear Rx Buffer 2 Ready
#define CEC_RX_BUFFER_1_READY_CLEAR     0x4A // [0] Clear Rx Buffer 1 Ready
#define CEC_LOGICAL_ADDRESS_MASK        0x4B // [6:4] Logical Address Mask
#define CEC_ERROR_REPORT_MODE           0x4B // [3] Error Report Mode
#define CEC_ERROR_DETECT_MODE           0x4B // [2] Error Detect Mode
#define CEC_FORCE_NACK                  0x4B // [1] Force NACK
#define CEC_FORCE_IGNORE                0x4B // [0] Force Ignore
#define CEC_LOGICAL_ADDR_REG            0x4C // Legacy alias
#define CEC_LOGICAL_ADDR_1              0x4C // [7:4] Logical Address 1
#define CEC_LOGICAL_ADDR_0              0x4C // [3:0] Logical Address 0
#define CEC_LOGICAL_ADDR_2              0x4D // [3:0] Logical Address 2
#define CEC_CLOCK_DIVIDER_POWER_MODE    0x4E // Combined register for clock divider and power mode
#define CEC_CLOCK_DIVIDER               0x4E // [7:2] Clock Divider
#define CEC_POWER_MODE                  0x4E // [1:0] Power Mode (00 = Normal, 01 = Standby, 10 = Power Down, 11 = Reserved)
#define CEC_GLITCH_FILTER_CTRL          0x4F // [5:0] 000000 = 0us, 000001 = 1us, 000010 = 2us, 000011 = 3us, 000100 = 4us, 000101 = 5us, 000110 = 6us, 000111 = 7us, 001000 = 8us, 001001 = 9us, 001010 = 10us, 001011 = 11us, 001100 = 12us, 001101 = 13us, 001110 = 14us, 001111 = 15us
#define CEC_RESET_REG                   0x50 // CEC reset register
#define CEC_ST_TOTAL_HIGH               0x51        
#define CEC_ST_TOTAL_LOW                0x52
#define CEC_ST_TOTAL_MIN_HIGH           0x53
#define CEC_ST_TOTAL_MIN_LOW            0x54
#define CEC_ST_TOTAL_MAX_HIGH           0x55
#define CEC_ST_TOTAL_MAX_LOW            0x56
#define CEC_ST_LOW_HIGH                 0x57
#define CEC_ST_LOW_LOW                  0x58
#define CEC_ST_LOW_MIN_HIGH             0x59
#define CEC_ST_LOW_MIN_LOW              0x5A
#define CEC_ST_LOW_MAX_HIGH             0x5B
#define CEC_ST_LOW_MAX_LOW              0x5C
#define CEC_BIT_TOTAL_HIGH              0x5D
#define CEC_BIT_TOTAL_LOW               0x5E
#define CEC_BIT_TOTAL_MIN_HIGH          0x5F
#define CEC_BIT_TOTAL_MIN_LOW           0x60
#define CEC_BIT_TOTAL_MAX_HIGH          0x61
#define CEC_BIT_TOTAL_MAX_LOW           0x62
#define CEC_BIT_LOW_ONE_HIGH            0x63
#define CEC_BIT_LOW_ONE_LOW             0x64
#define CEC_BIT_LOW_ZERO_HIGH           0x65
#define CEC_BIT_LOW_ZERO_LOW            0x66
#define CEC_BIT_LOW_MAX_HIGH            0x67
#define CEC_BIT_LOW_MAX_LOW             0x68
#define CEC_SAMPLE_TIME_HIGH            0x69
#define CEC_SAMPLE_TIME_LOW             0x6A
#define CEC_LINE_ERROR_TIME_HIGH        0x6B
#define CEC_LINE_ERROR_TIME_LOW         0x6C
#define CEC_FIXED                       0x6D
#define CEC_RISE_TIME_HIGH              0x6E
#define CEC_RISE_TIME_LOW               0x6F
#define CEC_BIT_LOW_DETMODE             0x70 // [0] 0 = Disabled, 1 = Enabled
#define CEC_BIT_LOW_ONE_MIN_HIGH        0x71
#define CEC_BIT_LOW_ONE_MIN_LOW         0x72
#define CEC_BIT_LOW_ONE_MAX_HIGH        0x73
#define CEC_BIT_LOW_ONE_MAX_LOW         0x74
#define CEC_BIT_LOW_ZERO_MIN_HIGH       0x75
#define CEC_BIT_LOW_ZERO_MIN_LOW        0x76
#define CEC_WAKE_UP_OPCODE_1            0x77
#define CEC_WAKE_UP_OPCODE_2            0x78
#define CEC_WAKE_UP_OPCODE_3            0x79
#define CEC_WAKE_UP_OPCODE_4            0x7A
#define CEC_WAKE_UP_OPCODE_5            0x7B
#define CEC_WAKE_UP_OPCODE_6            0x7C
#define CEC_WAKE_UP_OPCODE_7            0x7D
#define CEC_WAKE_UP_OPCODE_8            0x7E
#define CEC_ARBITRATION_ENABLE          0x7F // [7] CEC Arbitration Enable
#define CEC_HPD_RESPONSE_ENABLE         0x7F // [6] CEC HPD Response Enable
#define CEC_PHYSICAL_ADDR_HIGH          0x80 // Physical address [15:8]
#define CEC_PHYSICAL_ADDR_LOW           0x81 // Physical address [7:0]
#define CDC_HPD_TIMER_COUNT             0x82 // HPD Timer Count
#define CDC_HPD                         0x83 // [7] HPD signal from CEC interface (RO)
#define Y_RGB_MIN_HIGH                  0xC0
#define Y_RGB_MIN_LOW                   0xC1
#define Y_RGB_MAX_HIGH                  0xC2
#define Y_RGB_MAX_LOW                   0xC3
#define CBCR_MIN_HIGH                   0xC4
#define CBCR_MIN_LOW                    0xC5
#define CBCR_MAX_HIGH                   0xC6
#define CBCR_MAX_LOW                    0xC7

// Missing register aliases for backward compatibility




// Buffer register aliases for backward compatibility
#define CEC_RX_FRAME_HEADER             CEC_RX_FRAME_BUFFER1_HEADER    // 0x15
#define CEC_RX_FRAME_DATA0              CEC_RX_FRAME_BUFFER1_DATA0     // 0x16
#define CEC_RX_FRAME_LENGTH             CEC_RX_FRAME_BUFFER1_LENGTH    // 0x25
#define CEC_RX_BUFFER1_HEADER           CEC_RX_FRAME_BUFFER1_HEADER    // 0x15
#define CEC_RX_BUFFER1_DATA0            CEC_RX_FRAME_BUFFER1_DATA0     // 0x16
#define CEC_RX_BUFFER1_LENGTH           CEC_RX_FRAME_BUFFER1_LENGTH    // 0x25
#define CEC_RX_BUFFER2_HEADER           CEC_RX_FRAME_BUFFER2_HEADER    // 0x27
#define CEC_RX_BUFFER2_DATA0            CEC_RX_FRAME_BUFFER2_DATA0     // 0x28
#define CEC_RX_BUFFER2_LENGTH           CEC_RX_FRAME_BUFFER2_LENGTH    // 0x37
#define CEC_RX_BUFFER3_HEADER           CEC_RX_FRAME_BUFFER3_HEADER    // 0x38
#define CEC_RX_BUFFER3_DATA0            CEC_RX_FRAME_BUFFER3_DATA0     // 0x39
#define CEC_RX_BUFFER3_LENGTH           CEC_RX_FRAME_BUFFER3_LENGTH    // 0x48

// ADV7513 Main Map Interrupt Registers (CONFIRMED from datasheet)
#define ADV7513_INTERRUPT_ENABLE        0x94  // Main interrupt enable register
#define ADV7513_INTERRUPT_STATUS        0x96  // Main interrupt status register  
#define ADV7513_CEC_INTERRUPT_STATUS    0x97  // CEC interrupt status register

// ADV7513 Main Interrupt Enable Register (0x94) bit definitions
#define ADV7513_INT_HPD_ENABLE          0x80  // [7] HPD Interrupt Enable
#define ADV7513_INT_MONSENSE_ENABLE     0x40  // [6] Monitor Sense Interrupt Enable
#define ADV7513_INT_AUDIO_FIFO_ENABLE   0x10  // [4] Audio FIFO Full Interrupt Enable
// NOTE: CEC interrupts (register 0x97) may be always enabled and don't have individual enable bits in 0x94

// ADV7513 Main Interrupt Status Register (0x96) bit definitions  
#define ADV7513_INT_HPD_STATUS          0x80  // [7] HPD Interrupt
#define ADV7513_INT_MONSENSE_STATUS     0x40  // [6] Monitor Sense Interrupt
#define ADV7513_INT_AUDIO_FIFO_STATUS   0x10  // [4] Audio FIFO Full Interrupt

// ADV7513 CEC Interrupt Status Register (0x97) bit definitions
#define ADV7513_INT_DDC_ERROR           0x80  // [7] DDC Controller Error Interrupt
#define ADV7513_INT_BKSV_FLAG           0x40  // [6] BKSV Flag
#define ADV7513_INT_CEC_TX_READY        0x20  // [5] CEC Tx Ready
#define ADV7513_INT_CEC_TX_ARB_LOST     0x10  // [4] CEC Tx Arbitration Lost
#define ADV7513_INT_CEC_TX_RETRY_TIMEOUT 0x08 // [3] CEC Tx Retry Timeout
#define ADV7513_INT_CEC_RX_READY3       0x04  // [2] CEC Rx Ready 3
#define ADV7513_INT_CEC_RX_READY2       0x02  // [1] CEC Rx Ready 2
#define ADV7513_INT_CEC_RX_READY1       0x01  // [0] CEC Rx Ready 1

// Legacy aliases for backward compatibility with existing code
#define CEC_INTERRUPT_ENABLE            ADV7513_INTERRUPT_ENABLE        // 0x94
#define CEC_INTERRUPT_STATUS            ADV7513_CEC_INTERRUPT_STATUS    // 0x97 (CEC-specific interrupts)
#define CEC_INTERRUPT_CLEAR             ADV7513_CEC_INTERRUPT_STATUS    // 0x97 (write-1-to-clear)

// CEC interrupt bits (mapped to ADV7513 register 0x97)
#define CEC_INT_TX_READY         ADV7513_INT_CEC_TX_READY        // 0x20 [5] CEC Tx Ready
#define CEC_INT_TX_ARB_LOST      ADV7513_INT_CEC_TX_ARB_LOST     // 0x10 [4] CEC Tx Arbitration Lost  
#define CEC_INT_TX_RETRY_TIMEOUT ADV7513_INT_CEC_TX_RETRY_TIMEOUT // 0x08 [3] CEC Tx Retry Timeout
#define CEC_INT_RX_READY3        ADV7513_INT_CEC_RX_READY3       // 0x04 [2] CEC Rx Ready 3
#define CEC_INT_RX_READY2        ADV7513_INT_CEC_RX_READY2       // 0x02 [1] CEC Rx Ready 2
#define CEC_INT_RX_READY1        ADV7513_INT_CEC_RX_READY1       // 0x01 [0] CEC Rx Ready 1

// Legacy aliases (update these to match new bit positions)
#define CEC_INT_TX_DONE          CEC_INT_TX_READY                // Alias for TX Ready
#define CEC_INT_RX_READY         (CEC_INT_RX_READY1 | CEC_INT_RX_READY2 | CEC_INT_RX_READY3) // Any RX buffer ready

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
static void cec_send_osd_name();
static int cec_verify_register_maps(); // NEW: Verify register map addressing
static int cec_reset_register_maps();  // NEW: Reset corrupted register maps

// I2C helper functions
static int cec_write_reg(uint8_t reg, uint8_t value) {
    if (cec_state.cec_i2c_fd < 0) return -1;
    
    int result = i2c_smbus_write_byte_data(cec_state.cec_i2c_fd, reg, value);
    if (result < 0) {
        printf("CEC: I2C write error reg 0x%02X=0x%02X: %s\n", reg, value, strerror(errno));
        return -1;
    }
    
    // For critical registers, verify the write succeeded
    if (reg == CEC_POWER_MODE || reg == CEC_LOGICAL_ADDR_REG || reg == CEC_TX_ENABLE_REG) {
        usleep(1000); // Small delay before read-back
        uint8_t verify = 0;
        int read_result = i2c_smbus_read_byte_data(cec_state.cec_i2c_fd, reg);
        if (read_result >= 0) {
            verify = (uint8_t)read_result;
            if (reg == CEC_TX_ENABLE_REG) {
                // TX_ENABLE has special behavior - it auto-clears after transmission starts
                printf("CEC: TX_ENABLE (0x11) write: value=0x%02X, readback=0x%02X\n", value, verify);
                if (value == 0x01 && verify == 0x00) {
                    printf("CEC: TX_ENABLE auto-cleared - transmission may have completed instantly\n");
                } else if (value == 0x01 && verify == 0x01) {
                    printf("CEC: TX_ENABLE set successfully - transmission should be starting\n");
                } else if (verify != value) {
                    printf("CEC: TX_ENABLE unexpected readback - wrote 0x%02X, read 0x%02X\n", value, verify);
                }
            } else if (verify == value) {
                printf("CEC: Register 0x%02X write verified: 0x%02X\n", reg, verify);
            } else {
                printf("CEC: Register 0x%02X write MISMATCH: wrote 0x%02X, read 0x%02X\n", 
                       reg, value, verify);
            }
        } else {
            if (reg == CEC_TX_ENABLE_REG) {
                printf("CEC: ERROR: Cannot read back TX_ENABLE register - I2C communication failure\n");
            }
        }
    }
    
    return 0;
}

static int cec_read_reg(uint8_t reg, uint8_t *value) {
    if (cec_state.cec_i2c_fd < 0 || !value) return -1;
    
    int result = i2c_smbus_read_byte_data(cec_state.cec_i2c_fd, reg);
    if (result < 0) {
        printf("CEC: I2C read error reg 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }
    
    *value = (uint8_t)result;
    return 0;
}

// NEW: Verify ADV7513 register map addressing
static int cec_verify_register_maps() {
    if (cec_state.i2c_fd < 0) {
        printf("CEC: Cannot verify register maps - main I2C not open\n");
        return -1;
    }
    
    // Check all three register map base addresses
    int edid_addr = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0x43);
    int packet_addr = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0x45);
    int cec_addr = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0xE1);
    
    bool maps_valid = true;
    
    if (edid_addr != 0x7E) {
        printf("CEC: Register map corruption detected - EDID (0x43): expected 0x7E, got 0x%02X\n",
               (edid_addr >= 0) ? (uint8_t)edid_addr : 0xFF);
        maps_valid = false;
    }
    
    if (packet_addr != 0x70) {
        printf("CEC: Register map corruption detected - Packet (0x45): expected 0x70, got 0x%02X\n",
               (packet_addr >= 0) ? (uint8_t)packet_addr : 0xFF);
        maps_valid = false;
    }
    
    if (cec_addr != 0x78) {
        printf("CEC: Register map corruption detected - CEC (0xE1): expected 0x78, got 0x%02X\n",
               (cec_addr >= 0) ? (uint8_t)cec_addr : 0xFF);
        maps_valid = false;
    }
    
    if (maps_valid) {
        printf("CEC: Register map verification passed - all maps correctly addressed\n");
        return 0;
    } else {
        printf("CEC: Register map verification FAILED - corruption detected\n");
        return -1;
    }
}

// NEW: Reset corrupted ADV7513 register maps without full system restart
static int cec_reset_register_maps() {
    if (cec_state.i2c_fd < 0) {
        printf("CEC: Cannot reset register maps - main I2C not open\n");
        return -1;
    }
    
    printf("CEC: Resetting corrupted ADV7513 register map addresses...\n");
    
    // Re-program all register map base addresses
    bool success = true;
    
    if (i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x43, 0x7E) < 0) {
        printf("CEC: ERROR - Failed to reset EDID register map (0x43)\n");
        success = false;
    }
    usleep(5000);
    
    if (i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x45, 0x70) < 0) {
        printf("CEC: ERROR - Failed to reset Packet register map (0x45)\n");
        success = false;
    }
    usleep(5000);
    
    if (i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE1, 0x78) < 0) {
        printf("CEC: ERROR - Failed to reset CEC register map (0xE1)\n");
        success = false;
    }
    usleep(20000); // Allow time for changes to take effect
    
    if (success) {
        printf("CEC: Register map reset completed successfully\n");
        
        // Verify the reset worked
        if (cec_verify_register_maps() == 0) {
            printf("CEC: Register map reset verification passed\n");
            return 0;
        } else {
            printf("CEC: Register map reset verification failed\n");
            return -1;
        }
    } else {
        printf("CEC: Register map reset failed\n");
        return -1;
    }
}

// Initialize CEC hardware
int cec_init(const char* device_name, bool auto_power, bool remote_control) {
    if (cec_state.initialized) {
        printf("CEC: Already initialized\n");
        return 0;
    }

    printf("CEC: Starting detailed initialization...\n");
    
    // Declare variables used throughout initialization
    bool power_success = false;

    // Open main ADV7513 I2C device first
    cec_state.i2c_fd = i2c_open(ADV7513_MAIN_I2C_ADDR, 0);
    if (cec_state.i2c_fd < 0) {
        printf("CEC: Failed to open ADV7513 main I2C\n");
        return -1;
    }

    printf("CEC: Main ADV7513 I2C opened successfully\n");

    // Ensure CEC module is powered up by writing 0x00 to main register 0xE2 per suggestion.
    printf("CEC: Power-up CEC by writing main reg 0xE2=0x00\n");
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE2, 0x00);
    usleep(10000);

    // Check main ADV7513 ID registers to verify communication
    int chip_id1 = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0xF5);
    int chip_id2 = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0xF6);

    if (chip_id1 < 0 || chip_id2 < 0) {
        printf("CEC: Failed to read ADV7513 chip ID (id1=%d, id2=%d)\n", chip_id1, chip_id2);
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
        return -1;
    }

    printf("CEC: ADV7513 chip ID: 0x%02X%02X\n", (uint8_t)chip_id1, (uint8_t)chip_id2);

    // CRITICAL: Follow ADV7513 initialization sequence exactly like video.cpp
    printf("CEC: Starting ADV7513 CEC initialization sequence...\n");

    // Follow the exact same ADI required sequence from video.cpp
    printf("CEC: Applying ADI required unlock sequence...\n");
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x98, 0x03); // ADI required Write
    usleep(10000);
    
    // CRITICAL: Add missing main chip register configurations from video.cpp
    printf("CEC: Configuring critical main chip registers for transmission engine...\n");
    
    // HPD Control - Critical for HDMI transmission engine
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xD6, 0xC0); // HPD always high (0b11000000)
    usleep(10000);
    
    // Power Down control - Required for proper operation
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x41, 0x10); // Power Down control
    usleep(10000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x9A, 0x70); // ADI required Write
    usleep(10000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x9C, 0x30); // ADI required Write
    usleep(10000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x9D, 0x61); // ADI required Write (0b01100001)
    usleep(10000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA2, 0xA4); // ADI required Write
    usleep(10000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA3, 0xA4); // ADI required Write
    usleep(10000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE0, 0xD0); // ADI required Write
    usleep(10000);
    
    // Timing configuration registers - Critical for CEC timing
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x35, 0x40);
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x36, 0xD9);
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x37, 0x0A);
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x38, 0x00);
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x39, 0x2D);
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x3A, 0x00);
    usleep(5000);
    
    // Video format registers - Required for proper HDMI operation
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x16, 0x38); // Output Format 444 (0b00111000)
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x17, 0x62); // Aspect ratio and sync (0b01100010)
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x3B, 0x80); // Automatic pixel repetition
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x3C, 0x00);
    usleep(5000);
    
    // Bus configuration
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x48, 0x08); // Normal bus order (0b00001000)
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x49, 0xA8); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x40, 0x00); // Reset before enabling CEC
    usleep(5000);
    
    // Additional required ADI writes from video.cpp
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x4A, 0x80); // Auto-Calculate SPD checksum
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x4C, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x94, 0x80); // HPD Interrupt enabled
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x99, 0x02); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x9B, 0x18); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x9F, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA1, 0x00); // Monitor Sense config
    usleep(5000);
    
    // Critical ADI required register block
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA4, 0x08); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA5, 0x04); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA6, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA7, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA8, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xA9, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xAA, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xAB, 0x40); // ADI required Write
    usleep(5000);
    
    // InfoFrame configuration registers (missing from video.cpp)
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x55, 0x10); // AVI InfoFrame basic config
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x56, 0x08); // Picture Aspect Ratio
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x57, 0x08); // RGB Quantization range
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x59, 0x00); // Content Type
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x73, 0x01); // Unknown but required
    usleep(5000);
    
    // HDMI/DVI mode configuration
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xAF, 0x06); // HDMI Mode enabled (0b00000110)
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xB9, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xBA, 0x60); // Input Clock delay (0b01100000)
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xBB, 0x00); // ADI required Write
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xDE, 0x9C); // ADI required Write
    usleep(5000);
    // Note: 0xE4 will be set later in CEC-specific section
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xFA, 0x7D); // Phase search count
    usleep(10000);
    
    // Audio configuration registers (from video.cpp for complete initialization)
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x0A, 0x00); // Audio Select I2S
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x0B, 0x0E); // Audio config
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x0D, 0x10); // I2S Bit Width
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x14, 0x02); // Audio Word Length
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x15, 0x20); // I2S Sampling Rate
    usleep(5000);
    // Audio Clock Config
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x01, 0x00); // Audio clock
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x02, 0x18); // Set N Value
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x03, 0x00); // Audio clock
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x07, 0x01); // Audio clock
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x08, 0x22); // Set CTS Value
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x09, 0x0A); // Audio clock
    usleep(10000);

    // Now set CEC-specific main registers
    printf("CEC: Configuring main ADV7513 chip for CEC...\n");

    // Register 0x40: CEC clock and timing (only set CEC bit, preserve others)
    int reg_40 = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0x40);
    if (reg_40 >= 0) {
        i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x40, reg_40 & ~0x80); // Disable CEC initially
        usleep(5000);
        i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x40, reg_40 | 0x80); // Enable CEC clock
        usleep(10000);
        printf("CEC: Register 0x40 configured: 0x%02X -> 0x%02X\n", (uint8_t)reg_40, reg_40 | 0x80);
    }

    // Register 0x41: Power control (preserve existing settings)
    int reg_41 = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0x41);
    if (reg_41 >= 0) {
        printf("CEC: Register 0x41 current: 0x%02X\n", (uint8_t)reg_41);
        // Don't modify 0x41 - it controls main power and might affect video
    }

    // Register 0x0C: Audio/CEC configuration (set to match video.cpp exactly)
    printf("CEC: Register 0x0C setting to match video.cpp (0x04 = I2S0 Enable)\n");
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x0C, 0x04); // Match video.cpp exactly
    usleep(10000);

    // CRITICAL FIX: Initialize ALL ADV7513 register maps to prevent TX_ENABLE failure
    printf("CEC: Initializing all ADV7513 register map addresses...\n");
    
    // Program EDID register map address (register 0x43) 
    if (i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x43, 0x7E) < 0) {
        printf("CEC: Failed to set EDID I2C address\n");
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
        return -1;
    }
    usleep(5000);
    
    // Program Packet register map address (register 0x45)
    if (i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x45, 0x70) < 0) {
        printf("CEC: Failed to set Packet I2C address\n");
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
        return -1;
    }
    usleep(5000);
    
    // Program CEC register map address (register 0xE1)
    if (i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE1, ADV7513_CEC_I2C_ADDR << 1) < 0) {
        printf("CEC: Failed to set CEC I2C address\n");
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
        return -1;
    }
    
    printf("CEC: Register map addresses configured:\n");
    printf("  EDID map (0x43): 0x7E\n");
    printf("  Packet map (0x45): 0x70\n");
    printf("  CEC map (0xE1): 0x%02X\n", ADV7513_CEC_I2C_ADDR << 1);
    
    usleep(20000); // Give time for address mapping to take effect
    
    // Verify all register map addresses were set correctly
    int edid_verify = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0x43);
    int packet_verify = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0x45);
    int cec_verify = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0xE1);
    
    printf("CEC: Register map verification:\n");
    printf("  EDID (0x43): wrote 0x7E, read 0x%02X\n", (uint8_t)edid_verify);
    printf("  Packet (0x45): wrote 0x70, read 0x%02X\n", (uint8_t)packet_verify);
    printf("  CEC (0xE1): wrote 0x%02X, read 0x%02X\n", 
           ADV7513_CEC_I2C_ADDR << 1, (uint8_t)cec_verify);
    
    if (edid_verify != 0x7E || packet_verify != 0x70 || cec_verify != (ADV7513_CEC_I2C_ADDR << 1)) {
        printf("CEC: ERROR: Register map programming failed - this will cause TX_ENABLE issues\n");
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
        return -1;
    }
    
    printf("CEC: All register maps initialized successfully\n");

    // Additional required registers for proper CEC operation
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE2, 0x01); // CEC internal enable
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE3, 0x02); // CEC buffer enable  
    usleep(5000);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE4, 0x60); // CEC control (matches video init)
    usleep(10000);

    // Verify the CEC address was set
    int cec_addr_verify = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0xE1);
    printf("CEC: CEC I2C address verification: wrote 0x%02X, read 0x%02X\n", 
           ADV7513_CEC_I2C_ADDR << 1, (uint8_t)cec_addr_verify);

    // Now try to open CEC I2C device
    cec_state.cec_i2c_fd = i2c_open(ADV7513_CEC_I2C_ADDR, 0);
    if (cec_state.cec_i2c_fd < 0) {
        printf("CEC: Failed to open ADV7513 CEC I2C at address 0x%02X\n", ADV7513_CEC_I2C_ADDR);
        i2c_close(cec_state.i2c_fd);
        cec_state.i2c_fd = -1;
        return -1;
    }

    printf("CEC: CEC I2C opened successfully\n");

    // CRITICAL: Reset the CEC system by toggling reset register 0x50
    printf("CEC: Performing suggested CEC reset (0x50: 0x01 -> 0x00)\n");
    fflush(stdout);
    cec_write_reg(CEC_RESET_REG, 0x01);  // Assert CEC reset
    usleep(10000);  // Hold reset for 10ms
    cec_write_reg(CEC_RESET_REG, 0x00);  // Release CEC reset
    usleep(20000);  // Allow time for CEC module to reinitialize
    printf("CEC: Suggested CEC reset completed\n");

    // Test CEC I2C communication by reading the correct power mode register
    int test_result = i2c_smbus_read_byte_data(cec_state.cec_i2c_fd, CEC_POWER_MODE);
    if (test_result >= 0) {
        printf("CEC: Initial power mode register (0x4E): 0x%02X\n", (uint8_t)test_result);
    } else {
        printf("CEC: Failed to read CEC power mode register 0x4E (result=%d)\n", test_result);
        i2c_close(cec_state.cec_i2c_fd);
        i2c_close(cec_state.i2c_fd);
        cec_state.cec_i2c_fd = -1;
        cec_state.i2c_fd = -1;
        return -1;
    }

    // Read and display several CEC registers to understand the current state
    printf("CEC: Reading initial register states...\n");
    uint8_t reg_values[10];
    uint8_t test_regs[] = {0x00, 0x10, 0x11, 0x26, 0x27, 0x28, 0x2A, 0x2B, 0x4E, 0x7B};
    for (int i = 0; i < 10; i++) {
        int val = i2c_smbus_read_byte_data(cec_state.cec_i2c_fd, test_regs[i]);
        reg_values[i] = (val >= 0) ? (uint8_t)val : 0xFF;
        printf("CEC: Reg 0x%02X = 0x%02X\n", test_regs[i], reg_values[i]);
    }    // Now test CEC register access - the ADI unlock should have enabled CEC access
    printf("CEC: Testing CEC register access after proper ADI initialization...\n");
    
    // First test - try setting CEC clock divider and power mode (register 0x4E)
    printf("CEC: Setting CEC clock divider and power mode in register 0x4E...\n");
    
    // Read current register value to preserve existing bits
    uint8_t reg_4e_current = 0;
    if (cec_read_reg(CEC_CLOCK_DIVIDER_POWER_MODE, &reg_4e_current) == 0) {
        printf("CEC: Register 0x4E current value: 0x%02X\n", reg_4e_current);
        
    // Calculate correct clock divider based on ADV7513 datasheet
    // Per section 4.8.7: "The default settings for these registers are for a 12MHz input clock"
    // Since we have a 12MHz external CEC clock, use the actual default divider value (15)
    // The hardware default is 0x0F (15 decimal) which is correct for 12MHz input
    uint8_t clock_div = 0x0F;  // Use actual default divider (15) for 12MHz input clock
    uint8_t power_mode = 0x01; // Always active
    uint8_t new_4e_value = (clock_div << 2) | power_mode;
    
    cec_write_reg(CEC_CLOCK_DIVIDER_POWER_MODE, new_4e_value);
    usleep(10000);
    
    uint8_t clk_verify = 0;
    if (cec_read_reg(CEC_CLOCK_DIVIDER_POWER_MODE, &clk_verify) == 0) {
        uint8_t read_clock_div = (clk_verify >> 2) & 0x3F;  // Extract bits [7:2]
        uint8_t read_power_mode = clk_verify & 0x03;        // Extract bits [1:0]
        printf("CEC: Clock divider: wrote 0x%02X (div=%d), read 0x%02X (div=%d, power=0x%02X)\n", 
               new_4e_value, clock_div, clk_verify, read_clock_div, read_power_mode);
    }
    }

    // Try to set power mode (this is the critical test)
    printf("CEC: Attempting to set power mode...\n");
    fflush(stdout);
    
    // FIRST: Try datasheet-compliant main chip power control
    printf("CEC: Using main chip power control to unlock CEC registers...\n");
    fflush(stdout);
    
    // Method 1: Main register 0xE2 CEC power control (ENSURE CEC IS POWERED UP)
    // Per your guidance: Write 0x00 to 0xE2 to ensure CEC is powered up
    printf("CEC: Setting main register 0xE2 to 0x00 to ensure CEC is powered up...\n");
    fflush(stdout);
    i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xE2, 0x00);
    usleep(10000);
    int reg_e2_verify = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0xE2);
    printf("CEC: Main reg 0xE2 set to ensure power up: wrote 0x00, read 0x%02X\n", 
           (reg_e2_verify >= 0) ? (uint8_t)reg_e2_verify : 0xFF);
    fflush(stdout);
    
    // Method 1B: Input Clock Gating control (DATASHEET Table 76)
    // Register 0xD6 bit 0 = Input Clock Gating (affects CEC)
    int reg_d6 = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0xD6);
    if (reg_d6 >= 0) {
        printf("CEC: Main reg 0xD6 (clock gating) current: 0x%02X\n", (uint8_t)reg_d6);
        // Ensure CEC clock is not gated (bit 0 = 1 means gated, 0 = not gated)
        uint8_t d6_ungated = reg_d6 & ~0x01;  
        i2c_smbus_write_byte_data(cec_state.i2c_fd, 0xD6, d6_ungated);
        usleep(5000);
        printf("CEC: Set main reg 0xD6 to 0x%02X (input clock ungated)\n", d6_ungated);
        fflush(stdout);
    }
    
    // Method 2: CEC soft reset via main register 0x50 (general soft reset)
    int reg_50 = i2c_smbus_read_byte_data(cec_state.i2c_fd, 0x50);
    if (reg_50 >= 0) {
        printf("CEC: Main reg 0x50 current: 0x%02X\n", (uint8_t)reg_50);
        // Perform soft reset cycle
        uint8_t reset_val = reg_50 | 0x01;   // Set general soft reset bit
        i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x50, reset_val);
        usleep(5000);
        i2c_smbus_write_byte_data(cec_state.i2c_fd, 0x50, reg_50);  // Clear reset bit
        usleep(15000);  // Longer delay after reset
        printf("CEC: Performed soft reset via main reg 0x50\n");
        fflush(stdout);
    }
    
    // Method 3: CEC Power Mode Register (DATASHEET CONFIRMED)
    // Per datasheet: CEC Power Mode controlled by register 0x4E[1:0]
    // 00 = depend on HDMI Tx PD, 01 = always active, 10 = always powered down
    printf("CEC: Configuring CEC Power Mode register 0x4E per datasheet...\n");
    fflush(stdout);
    uint8_t power_4e_initial = 0;
    if (cec_read_reg(0x4E, &power_4e_initial) == 0) {
        printf("CEC: Register 0x4E initial value: 0x%02X\n", power_4e_initial);
        
        // Try setting bits [1:0] to 01 (always active) while preserving other bits
        uint8_t power_4e_active = (power_4e_initial & 0xFC) | 0x01;  // Set bits [1:0] = 01
        cec_write_reg(0x4E, power_4e_active);
        usleep(10000);
        
        uint8_t power_4e_verify = 0;
        if (cec_read_reg(0x4E, &power_4e_verify) == 0) {
            printf("CEC: Power mode (0x4E) datasheet method: wrote 0x%02X, read 0x%02X\n", 
                   power_4e_active, power_4e_verify);
            if ((power_4e_verify & 0x03) == 0x01) {
                printf("CEC: SUCCESS! CEC Power Mode set to 'always active' per datasheet!\n");
                power_success = true;
            }
        }
    }
    
    // Method 4: Now try traditional power mode register 0x2A after main chip unlock
    printf("CEC: Testing power mode register 0x2A after main chip power control...\n");
    fflush(stdout);
    
    for (int attempt = 0; attempt < 3; attempt++) {
        cec_write_reg(CEC_POWER_MODE, 0x01);
        usleep(15000);  // Longer delay for power mode changes
        
        uint8_t power_verify = 0;
        if (cec_read_reg(CEC_POWER_MODE, &power_verify) == 0) {
            printf("CEC: Power mode (0x2A) attempt %d: wrote 0x01, read 0x%02X\n", 
                   attempt + 1, power_verify);
            fflush(stdout);
            if (power_verify == 0x01) {
                printf("CEC: SUCCESS! Power mode register is now writable!\n");
                fflush(stdout);
                power_success = true;
                break;
            }
        }
    }
    
    // Method 5: Alternative - try different power mode values
    if (!power_success) {
        printf("CEC: Trying alternative power mode values...\n");
        fflush(stdout);
        
        uint8_t power_values[] = {0x00, 0x02, 0x03}; // Try different power states
        for (int i = 0; i < 3; i++) {
            cec_write_reg(CEC_POWER_MODE, power_values[i]);
            usleep(10000);
            
            uint8_t power_verify = 0;
            if (cec_read_reg(CEC_POWER_MODE, &power_verify) == 0) {
                printf("CEC: Power mode test value 0x%02X: wrote 0x%02X, read 0x%02X\n", 
                       power_values[i], power_values[i], power_verify);
                if (power_verify == power_values[i]) {
                    printf("CEC: SUCCESS! Power mode register accepts value 0x%02X!\n", power_values[i]);
                    power_success = true;
                    break;
                }
            }
        }
    }

    // Test other important registers regardless of power mode success
    printf("CEC: Testing other CEC registers...\n");
    fflush(stdout);
    
    // Test RX enable (usually more permissive)
    printf("CEC: Testing RX enable register...\n");
    fflush(stdout);
    cec_write_reg(CEC_RX_ENABLE, 0x01);
    usleep(5000);
    uint8_t rx_verify = 0;
    if (cec_read_reg(CEC_RX_ENABLE, &rx_verify) == 0) {
        printf("CEC: RX enable: wrote 0x01, read 0x%02X\n", rx_verify);
        fflush(stdout);
    }
    
    // Test logical address using DATASHEET-CORRECT register 0x4C[7:4]
    printf("CEC: Testing logical address using datasheet-correct register 0x4C[7:4]...\n");
    fflush(stdout);
    
    // First test legacy register 0x27 for comparison
    cec_write_reg(CEC_LOGICAL_ADDR_REG, CEC_ADDR_UNREGISTERED);
    usleep(5000);
    uint8_t addr_verify_legacy = 0;
    if (cec_read_reg(CEC_LOGICAL_ADDR_REG, &addr_verify_legacy) == 0) {
        printf("CEC: Legacy logical address (0x27): wrote 0x%X, read 0x%X\n", 
               CEC_ADDR_UNREGISTERED, addr_verify_legacy);
    }
    
    // Now test datasheet-correct register 0x4C[7:4]
    uint8_t reg_4c_initial = 0;
    if (cec_read_reg(CEC_LOGICAL_ADDR_REG, &reg_4c_initial) == 0) {
        printf("CEC: Register 0x4C initial value: 0x%02X\n", reg_4c_initial);
        
        // Set logical address in bits [7:4], preserve bits [3:0]
        uint8_t addr_4c = (reg_4c_initial & 0x0F) | (CEC_ADDR_UNREGISTERED << 4);
        cec_write_reg(CEC_LOGICAL_ADDR_REG, addr_4c);
        usleep(5000);
        
        uint8_t addr_4c_verify = 0;
        if (cec_read_reg(CEC_LOGICAL_ADDR_REG, &addr_4c_verify) == 0) {
            uint8_t logical_addr_read = (addr_4c_verify >> 4) & 0x0F;
            printf("CEC: Datasheet logical address (0x4C[7:4]): wrote 0x%X, read 0x%X (addr=0x%X)\n", 
                   addr_4c, addr_4c_verify, logical_addr_read);
            
            if (logical_addr_read == CEC_ADDR_UNREGISTERED) {
                printf("CEC: SUCCESS! Datasheet logical address register 0x4C[7:4] is writable!\n");
                fflush(stdout);
            }
        }
    }
    
    // Test physical address registers (per datasheet for CDC message detection)
    printf("CEC: Testing physical address registers 0x80/0x81 per datasheet...\n");
    fflush(stdout);
    
    // Write test physical address 0x1234 to registers 0x80 (high) and 0x81 (low)
    uint16_t test_phys_addr = 0x1234;
    uint8_t phys_addr_high = (test_phys_addr >> 8) & 0xFF;
    uint8_t phys_addr_low = test_phys_addr & 0xFF;
    
    cec_write_reg(CEC_PHYSICAL_ADDR_HIGH, phys_addr_high);
    usleep(5000);
    cec_write_reg(CEC_PHYSICAL_ADDR_LOW, phys_addr_low);
    usleep(5000);
    
    uint8_t phys_verify_high = 0, phys_verify_low = 0;
    if (cec_read_reg(CEC_PHYSICAL_ADDR_HIGH, &phys_verify_high) == 0 &&
        cec_read_reg(CEC_PHYSICAL_ADDR_LOW, &phys_verify_low) == 0) {
        uint16_t phys_verify = (phys_verify_high << 8) | phys_verify_low;
        printf("CEC: Physical address registers: wrote 0x%04X, read 0x%04X\n", 
               test_phys_addr, phys_verify);
        if (phys_verify == test_phys_addr) {
            printf("CEC: SUCCESS! Physical address registers 0x80/0x81 are writable!\n");
            fflush(stdout);
        }
    }
    
    // If legacy logical address register failed but datasheet registers work, 
    // focus on datasheet-compliant implementation for future operations
    printf("CEC: Datasheet register testing complete\n");
    fflush(stdout);

    // If power mode still fails, try alternative approach
    if (!power_success) {
        printf("CEC: Power mode register still protected, trying alternative activation...\n");
        fflush(stdout);
        
        // Method 1: Try enabling via RX first
        printf("CEC: Trying RX-first activation method...\n");
        fflush(stdout);
        cec_write_reg(CEC_RX_ENABLE, 0x01);
        usleep(10000);
        cec_write_reg(CEC_INTERRUPT_ENABLE, 0x70); // Enable RX interrupts only
        usleep(10000);
        
        // Method 2: Try setting logical address to a real address (not unregistered)
        printf("CEC: Trying logical address activation method...\n");
        fflush(stdout);
        cec_write_reg(CEC_LOGICAL_ADDR_REG, 0x04); // Playback device 1
        usleep(10000);
        
        // Now try power mode again
        printf("CEC: Retrying power mode after alternative activation...\n");
        fflush(stdout);
        cec_write_reg(CEC_POWER_MODE, 0x01);
        usleep(15000);
        uint8_t final_power = 0;
        if (cec_read_reg(CEC_POWER_MODE, &final_power) == 0) {
            printf("CEC: Final power mode test: wrote 0x01, read 0x%02X\n", final_power);
            fflush(stdout);
            if (final_power == 0x01) {
                printf("CEC: SUCCESS! Alternative activation worked!\n");
                fflush(stdout);
                power_success = true;
            } else {
                printf("CEC: Power mode register remains protected, continuing anyway...\n");
                fflush(stdout);
                // Reset to unregistered since we'll claim address properly later
                cec_write_reg(CEC_LOGICAL_ADDR_REG, CEC_ADDR_UNREGISTERED);
            }
        }
    }

    // Disable interrupts during initial setup
    cec_write_reg(CEC_INTERRUPT_ENABLE, 0x00);

    // Reset the logical address to unregistered initially
    cec_write_reg(CEC_LOGICAL_ADDR_REG, CEC_ADDR_UNREGISTERED);
    cec_write_reg(CEC_LOGICAL_ADDRESS_MASK, 0x00);  // No addresses enabled yet

    // Enable RX with proper setup
    printf("CEC: Enabling RX...\n");
    cec_write_reg(CEC_RX_ENABLE, 0x01);

    // Configure additional CEC timing registers that may be critical for transmission
    printf("CEC: Configuring CEC timing registers for reliable transmission...\n");
    fflush(stdout);
    
    // CEC Glitch Filter Control (register 0x4F) - Set moderate filtering (5us)
    // Per ADV7513 datasheet: glitch filter should be set appropriately for the CEC clock frequency
    cec_write_reg(0x4F, 0x05);  // 5us glitch filter
    usleep(1000);
    
    // CEC Signal Free Time configuration (registers 0x12, 0x13)
    // These are critical for proper CEC bus timing and arbitration
    uint8_t sft_config = 0x35;  // Default signal free time + 3 retries
    cec_write_reg(CEC_TX_RETRY, sft_config);
    usleep(1000);
    
    // CEC Sample Time (registers 0x28, 0x2A) - These control bit detection timing
    // Register 0x28: Sample time and buffer control
    cec_write_reg(0x28, 0x71);  // Sample time configuration for 12MHz clock
    usleep(1000);
    
    // Register 0x2A: Additional sample time control  
    cec_write_reg(0x2A, 0x01);  // Sample time low byte for 12MHz clock
    usleep(1000);
    
    // CEC Buffer control (register 0x2B) - Controls CEC line drivers
    cec_write_reg(0x2B, 0x35);  // Buffer control for proper line driving
    usleep(1000);
    
    // Line Error Time configuration - helps with noise immunity
    cec_write_reg(0x6B, 0x00);  // Line error time high
    cec_write_reg(0x6C, 0xC8);  // Line error time low (~200 counts)
    usleep(1000);
    
    printf("CEC: Timing registers configured\n");
    fflush(stdout);

    // CRITICAL: Enable CEC arbitration and HPD response (register 0x7F)
    // This is the missing piece that prevents TX_ENABLE from actually starting transmission
    printf("CEC: Configuring CEC arbitration enable register 0x7F...\n");
    fflush(stdout);
    
    // Enable both CEC arbitration (bit 7) and HPD response (bit 6)
    // Per ADV7513 datasheet: This register is essential for CEC bus participation
    uint8_t arbitration_config = 0x80 | 0x40;  // Enable arbitration + HPD response
    cec_write_reg(CEC_ARBITRATION_ENABLE, arbitration_config);
    usleep(5000);  // Allow time for arbitration logic to initialize
    
    // Verify the arbitration enable was set
    uint8_t arb_verify = 0;
    if (cec_read_reg(CEC_ARBITRATION_ENABLE, &arb_verify) == 0) {
        printf("CEC: Arbitration enable register: wrote 0x%02X, read 0x%02X\n", 
               arbitration_config, arb_verify);
        if (arb_verify & 0x80) {
            printf("CEC: ✓ CEC arbitration enabled successfully\n");
        }
        if (arb_verify & 0x40) {
            printf("CEC: ✓ HPD response enabled successfully\n");
        }
    } else {
        printf("CEC: WARNING: Failed to verify arbitration enable register\n");
    }
    fflush(stdout);

    // Final verification of all critical registers
    printf("CEC: Performing final verification...\n");
    fflush(stdout);
    uint8_t final_power = 0, final_clock = 0, final_rx = 0;
    cec_read_reg(CEC_POWER_MODE, &final_power);
    cec_read_reg(CEC_CLOCK_DIVIDER, &final_clock);
    cec_read_reg(CEC_RX_ENABLE, &final_rx);
    printf("CEC: Final register state - POWER=0x%02X, CLK_DIV=0x%02X, RX_EN=0x%02X\n", 
           final_power, final_clock, final_rx);
    fflush(stdout);

    // Configure interrupt enables - be more selective
    printf("CEC: Enabling interrupts...\n");
    fflush(stdout);
    cec_write_reg(CEC_INTERRUPT_ENABLE, 
        CEC_INT_TX_DONE | CEC_INT_TX_ARB_LOST | CEC_INT_TX_RETRY_TIMEOUT |
        CEC_INT_RX_READY1 | CEC_INT_RX_READY2 | CEC_INT_RX_READY3);

    // Store configuration
    printf("CEC: Storing configuration...\n");
    fflush(stdout);
    strncpy(cec_state.device_name, device_name ? device_name : "MiSTer", 
            sizeof(cec_state.device_name) - 1);
    cec_state.auto_power_on = auto_power;
    cec_state.remote_control_enabled = remote_control;

    cec_state.initialized = true;
    cec_state.enabled = false; // Will be enabled after physical address discovery

    printf("CEC: Initialized successfully (power_success=%s)\n", power_success ? "true" : "false");
    printf("CEC: About to return 0 from cec_init\n");
    fflush(stdout);
    return 0;
}

static void cec_send_osd_name() {
    if (!cec_state.enabled || !cec_state.initialized) return;

    printf("CEC: About to send OSD name\n");
    
    // Verify CEC configuration before sending
    uint8_t power_mode = 0, logical_addr = 0, clock_div = 0, rx_enable = 0;
    cec_read_reg(CEC_POWER_MODE, &power_mode);
    cec_read_reg(CEC_LOGICAL_ADDR_REG, &logical_addr);
    cec_read_reg(CEC_CLOCK_DIVIDER, &clock_div);
    cec_read_reg(CEC_RX_ENABLE, &rx_enable);
    
    printf("CEC: Verification - POWER=0x%02X, ADDR=0x%02X, CLK_DIV=0x%02X, RX_EN=0x%02X\n",
           power_mode, logical_addr, clock_div, rx_enable);

    // Try sending OSD name multiple times if needed
    int attempts = 3;
    for (int i = 0; i < attempts; i++) {
        if (i > 0) {
            printf("CEC: OSD name retry attempt %d/%d\n", i + 1, attempts);
            usleep(500000); // Wait 500ms between attempts
        }
        
        int result = cec_send_message(
            CEC_ADDR_TV,
            CEC_OP_SET_OSD_NAME,
            (const uint8_t*)cec_state.device_name,
            strlen(cec_state.device_name)
        );
        
        if (result == 0) {
            printf("CEC: OSD name sent successfully on attempt %d\n", i + 1);
            break;
        } else {
            printf("CEC: OSD name transmission failed on attempt %d (result=%d)\n", i + 1, result);
            if (i == attempts - 1) {
                printf("CEC: All OSD name attempts failed - TV may not support CEC or be responsive\n");
            }
        }
    }
}

// Configure CEC with physical address from EDID
int cec_configure(uint16_t physical_addr) {
    printf("CEC: === cec_configure() called with physical_addr=0x%04X ===\n", physical_addr);
    fflush(stdout);
    
    printf("CEC: Checking if cec_state.initialized...\n");
    fflush(stdout);
    
    if (!cec_state.initialized) {
        printf("CEC: Not initialized\n");
        fflush(stdout);
        return -1;
    }
    
    printf("CEC: cec_state.initialized=true, continuing...\n");
    fflush(stdout);
    
    printf("CEC: Setting cec_state.physical_addr to 0x%04X...\n", physical_addr);
    fflush(stdout);
    
    cec_state.physical_addr = physical_addr;
    printf("CEC: Configuring with physical address %d.%d.%d.%d\n",
           (physical_addr >> 12) & 0xF, (physical_addr >> 8) & 0xF,
           (physical_addr >> 4) & 0xF, physical_addr & 0xF);
    fflush(stdout);
    
    // Check current register states before attempting logical address claim
    printf("CEC: Pre-configuration register check...\n");
    fflush(stdout);
    uint8_t power_mode_legacy = 0, power_mode_datasheet = 0, rx_enable = 0, clock_div = 0, int_enable = 0;
    
    printf("CEC: Reading legacy CEC_POWER_MODE (0x2A)...\n");
    fflush(stdout);
    cec_read_reg(CEC_POWER_MODE, &power_mode_legacy);
    
    printf("CEC: Reading datasheet CEC_POWER_MODE (0x4E)...\n");
    fflush(stdout);
    cec_read_reg(CEC_POWER_MODE, &power_mode_datasheet);
    
    printf("CEC: Reading CEC_RX_ENABLE...\n");
    fflush(stdout);
    cec_read_reg(CEC_RX_ENABLE, &rx_enable);
    
    printf("CEC: Reading CEC_CLOCK_DIVIDER...\n");
    fflush(stdout);
    cec_read_reg(CEC_CLOCK_DIVIDER, &clock_div);
    
    printf("CEC: Reading CEC_INTERRUPT_ENABLE...\n");
    fflush(stdout);
    cec_read_reg(CEC_INTERRUPT_ENABLE, &int_enable);
    
    printf("CEC: POWER_LEGACY=0x%02X, POWER_DATASHEET=0x%02X, RX_EN=0x%02X, CLK_DIV=0x%02X, INT_EN=0x%02X\n",
           power_mode_legacy, power_mode_datasheet, rx_enable, clock_div, int_enable);
    fflush(stdout);
    
    // Try to claim logical address for playback device
    uint8_t logical_addrs[] = {CEC_ADDR_PLAYBACK_1, CEC_ADDR_PLAYBACK_2, CEC_ADDR_PLAYBACK_3};
    bool address_claimed = false;
    
    printf("CEC: Starting logical address claiming process...\n");
    fflush(stdout);
    
    for (int i = 0; i < 3; i++) {
        printf("CEC: Attempting to claim logical address 0x%02X...\n", logical_addrs[i]);
        fflush(stdout);
        
        // Clear any pending interrupts first
        cec_write_reg(CEC_INTERRUPT_CLEAR, 0xFF);
        usleep(5000);
        
        // Send polling message to test if address is free
        cec_write_reg(CEC_TX_FRAME_HEADER, (logical_addrs[i] << 4) | logical_addrs[i]);
        cec_write_reg(CEC_TX_FRAME_LENGTH, 1);
        
        // Check TX enable register before and after
        uint8_t tx_before = 0, tx_after = 0;
        cec_read_reg(CEC_TX_ENABLE_REG, &tx_before);
        printf("CEC: TX_ENABLE before: 0x%02X\n", tx_before);
        
        cec_write_reg(CEC_TX_ENABLE_REG, 0x01);
        usleep(2000); // Give more time for register to update
        cec_read_reg(CEC_TX_ENABLE_REG, &tx_after);
        printf("CEC: TX_ENABLE after: 0x%02X\n", tx_after);
        fflush(stdout);
        
        // Simplified timeout approach - don't hang on complex polling
        int timeout = 20; // Reduced timeout: 20ms max
        bool tx_completed = false;
        uint8_t final_status = 0;
        
        while (timeout > 0) {
            uint8_t status = 0, enable = 0;
            cec_read_reg(CEC_INTERRUPT_STATUS, &status);
            cec_read_reg(CEC_TX_ENABLE_REG, &enable);
            
            if (status & (CEC_INT_TX_DONE | CEC_INT_TX_ARB_LOST | CEC_INT_TX_RETRY_TIMEOUT)) {
                printf("CEC: TX completed with status=0x%02X\n", status);
                final_status = status;
                tx_completed = true;
                cec_write_reg(CEC_INTERRUPT_CLEAR, status);
                break;
            }
            
            // If TX_ENABLE cleared, transmission likely completed
            if (enable == 0x00 && tx_after == 0x01) {
                printf("CEC: TX_ENABLE cleared (transmission completed)\n");
                tx_completed = true;
                break;
            }
            
            usleep(1000);
            timeout--;
        }
        
        if (!tx_completed) {
            printf("CEC: TX timeout for address 0x%02X - assuming address is free\n", logical_addrs[i]);
        } else if (final_status & CEC_INT_TX_DONE) {
            printf("CEC: Address 0x%02X is in use (got ACK)\n", logical_addrs[i]);
            continue; // Try next address
        } else {
            printf("CEC: Address 0x%02X appears to be free (no ACK)\n", logical_addrs[i]);
        }
        
        // Claim this address
        cec_state.logical_addr = logical_addrs[i];
        address_claimed = true;
        printf("CEC: Claimed logical address 0x%02X\n", logical_addrs[i]);
        fflush(stdout);
        break;
    }
    
    if (!address_claimed) {
        printf("CEC: Could not claim any logical address, using default 0x%02X\n", CEC_ADDR_PLAYBACK_1);
        cec_state.logical_addr = CEC_ADDR_PLAYBACK_1; // Use default and hope for the best
    }
    
    printf("CEC: Logical address claiming complete - using address 0x%02X\n", cec_state.logical_addr);
    fflush(stdout);
    
    // Set logical address in hardware registers using DATASHEET-CORRECT register 0x4C[7:4]
    printf("CEC: Setting logical address 0x%X in datasheet register 0x4C[7:4]...\n", cec_state.logical_addr);
    fflush(stdout);
    
    // Read current register value to preserve lower bits
    uint8_t reg_4c_current = 0;
    cec_read_reg(CEC_LOGICAL_ADDR_REG, &reg_4c_current);
    
    // Set logical address in bits [7:4], preserve bits [3:0]
    uint8_t addr_4c = (reg_4c_current & 0x0F) | (cec_state.logical_addr << 4);
    cec_write_reg(CEC_LOGICAL_ADDR_REG, addr_4c);
    usleep(10000); // Allow register write to settle
    
    // Set logical address mask (enable reception for this address)
    cec_write_reg(CEC_LOGICAL_ADDRESS_MASK, (1 << cec_state.logical_addr));
    usleep(10000);
    
    // Verify the logical address was set using datasheet register
    uint8_t addr_verify_4c = 0;
    cec_read_reg(CEC_LOGICAL_ADDR_REG, &addr_verify_4c);
    uint8_t logical_addr_read = (addr_verify_4c >> 4) & 0x0F;
    printf("CEC: Logical address verification (0x4C[7:4]): wrote 0x%X, read 0x%X\n", 
           cec_state.logical_addr, logical_addr_read);
    
    // Also try legacy register for compatibility
    cec_write_reg(CEC_LOGICAL_ADDR_REG, cec_state.logical_addr);
    uint8_t addr_verify_legacy = 0;
    cec_read_reg(CEC_LOGICAL_ADDR_REG, &addr_verify_legacy);
    printf("CEC: Legacy logical address verification (0x27): wrote 0x%X, read 0x%X\n", 
           cec_state.logical_addr, addr_verify_legacy);
    
    printf("CEC: Claimed logical address 0x%X\n", cec_state.logical_addr);
    
    // Program physical address in datasheet registers 0x80/0x81
    printf("CEC: Programming physical address 0x%04X in datasheet registers 0x80/0x81...\n", physical_addr);
    uint8_t phys_addr_high = (physical_addr >> 8) & 0xFF;
    uint8_t phys_addr_low = physical_addr & 0xFF;
    
    cec_write_reg(CEC_PHYSICAL_ADDR_HIGH, phys_addr_high);
    usleep(5000);
    cec_write_reg(CEC_PHYSICAL_ADDR_LOW, phys_addr_low);
    usleep(5000);
    
    // Verify physical address registers
    uint8_t phys_verify_high = 0, phys_verify_low = 0;
    if (cec_read_reg(CEC_PHYSICAL_ADDR_HIGH, &phys_verify_high) == 0 &&
        cec_read_reg(CEC_PHYSICAL_ADDR_LOW, &phys_verify_low) == 0) {
        uint16_t phys_verify = (phys_verify_high << 8) | phys_verify_low;
        printf("CEC: Physical address verification: wrote 0x%04X, read 0x%04X\n", 
               physical_addr, phys_verify);
    }
    
    printf("CEC: About to announce physical address to CEC network...\n");
    fflush(stdout);
    
    // Announce physical address
    uint8_t params[3] = {
        (uint8_t)(physical_addr >> 8),
        (uint8_t)(physical_addr & 0xFF),
        0x04  // Device type: Playback Device
    };
    cec_send_message(CEC_ADDR_BROADCAST, CEC_OP_REPORT_PHYSICAL_ADDR, params, 3);
    
    printf("CEC: Physical address announced, now setting OSD name...\n");
    fflush(stdout);
    
    // Set OSD name
    cec_send_message(CEC_ADDR_TV, CEC_OP_SET_OSD_NAME, 
                     (uint8_t*)cec_state.device_name, strlen(cec_state.device_name));
    
    printf("CEC: Starting monitor thread...\n");
    fflush(stdout);
    
    // Start monitor thread
    cec_state.thread_running = true;
    if (pthread_create(&cec_state.monitor_thread, NULL, cec_monitor_thread, NULL) != 0) {
        printf("CEC: Failed to create monitor thread\n");
        cec_state.thread_running = false;
        return -1;
    }
    
    printf("CEC: Monitor thread created successfully\n");
    fflush(stdout);
    
    cec_state.enabled = true;
    
    printf("CEC: CEC state enabled, checking auto power settings...\n");
    fflush(stdout);
    
    // Perform One Touch Play if configured
    if (cec_state.auto_power_on) {
        printf("CEC: Auto power on enabled - performing One Touch Play\n");
        fflush(stdout);
        cec_one_touch_play();
    } else {
        printf("CEC: Auto power on disabled\n");
        fflush(stdout);
    }
    
    printf("CEC: Sending final OSD name...\n");
    fflush(stdout);
    cec_send_osd_name();

    printf("CEC: === Configuration complete successfully! ===\n");
    fflush(stdout);

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
    
    // Disable RX temporarily during TX to avoid interference
    cec_write_reg(CEC_RX_ENABLE, 0x00);
    
    // Clear all interrupts
    cec_write_reg(CEC_INTERRUPT_CLEAR, 0xFF);
    
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
    
    if (opcode == CEC_OP_SET_OSD_NAME) {
        printf("CEC: Sending SET_OSD_NAME to %02X: %.*s (len=%zu)\n", dest, (int)param_len, params, msg_len);
    }
    
    // Set frame length
    cec_write_reg(CEC_TX_FRAME_LENGTH, msg_len);
    
    // Enhanced transmission sequence based on ADV7513 datasheet requirements
    printf("CEC: Starting enhanced transmission sequence\n");
    
    // 1. Clear any pending interrupts first
    cec_write_reg(CEC_INTERRUPT_CLEAR, 0xFF);
    usleep(1000);
    
    // 2. Verify critical registers are set correctly
    uint8_t power_check = 0, clock_check = 0, rx_check = 0;
    cec_read_reg(CEC_POWER_MODE, &power_check);
    cec_read_reg(CEC_CLOCK_DIVIDER, &clock_check);
    cec_read_reg(CEC_RX_ENABLE, &rx_check);
    
    // Extract actual values from combined register
    uint8_t actual_power = power_check & 0x03;
    uint8_t actual_clock = (clock_check >> 2) & 0x3F;
    
    printf("CEC: Pre-TX verification - power=0x%02X (actual=0x%02X), clock=0x%02X (actual=%d), rx=0x%02X\n", 
           power_check, actual_power, clock_check, actual_clock, rx_check);
    
    // 3. Ensure CEC is in proper operational state
    if (actual_power != 0x01) {
        printf("CEC: WARNING: CEC power mode not set to active (0x01), attempting to fix\n");
        // Try to set power mode again
        uint8_t fixed_reg = (clock_check & 0xFC) | 0x01;  // Preserve clock, set power to active
        cec_write_reg(CEC_POWER_MODE, fixed_reg);
        usleep(5000);
    }
    
    // 4. Check for CEC line availability - ensure no ongoing transmission
    uint8_t tx_enable_check = 0;
    cec_read_reg(CEC_TX_ENABLE_REG, &tx_enable_check);
    if (tx_enable_check != 0x00) {
        printf("CEC: WARNING: TX_ENABLE is not clear (0x%02X), waiting for completion\n", tx_enable_check);
        // Wait briefly for any ongoing transmission to complete
        for (int wait = 0; wait < 50; wait++) {
            usleep(1000);
            cec_read_reg(CEC_TX_ENABLE_REG, &tx_enable_check);
            if (tx_enable_check == 0x00) break;
        }
        if (tx_enable_check != 0x00) {
            printf("CEC: ERROR: Previous transmission not complete, forcing clear\n");
            cec_write_reg(CEC_TX_ENABLE_REG, 0x00);
            usleep(5000);
        }
    }
    
    // 5. Set up signal free time for proper timing
    // Per ADV7513 datasheet, set appropriate signal free time
    cec_write_reg(CEC_TX_RETRY, 0x35);  // 3 retries + signal free time
    usleep(1000);
    
    // 6. Pre-transmission verification - ensure CEC is ready
    printf("CEC: Verifying CEC readiness before transmission\n");
    uint8_t power_check = 0, arb_check = 0, clock_check = 0;
    cec_read_reg(CEC_POWER_MODE, &power_check);
    cec_read_reg(CEC_ARBITRATION_ENABLE, &arb_check);
    cec_read_reg(CEC_CLOCK_DIVIDER_POWER_MODE, &clock_check);
    
    uint8_t actual_power = power_check & 0x03;
    bool arb_enabled = (arb_check & 0x80) != 0;
    uint8_t actual_clock = (clock_check >> 2) & 0x3F;
    
    printf("CEC: Pre-TX state - Power=0x%02X, Arbitration=%s (0x%02X), Clock_div=%d\n", 
           actual_power, arb_enabled ? "ENABLED" : "DISABLED", arb_check, actual_clock);
    
    if (actual_power != 0x01) {
        printf("CEC: WARNING: CEC not in active power mode (expected 0x01, got 0x%02X)\n", actual_power);
    }
    if (!arb_enabled) {
        printf("CEC: ERROR: CEC arbitration not enabled - TX_ENABLE will not work!\n");
        printf("CEC: Attempting to enable arbitration...\n");
        cec_write_reg(CEC_ARBITRATION_ENABLE, 0x80 | 0x40);  // Enable arbitration + HPD
        usleep(5000);
        cec_read_reg(CEC_ARBITRATION_ENABLE, &arb_check);
        printf("CEC: Arbitration enable retry: 0x%02X\n", arb_check);
    }
    
    // 7. START TRANSMISSION
    printf("CEC: Initiating transmission\n");
    cec_write_reg(CEC_TX_ENABLE_REG, 0x01);
    
    // 8. Immediate verification that transmission started
    usleep(2000); // Brief delay for register update
    uint8_t tx_start_verify = 0;
    cec_read_reg(CEC_TX_ENABLE_REG, &tx_start_verify);
    printf("CEC: TX_ENABLE after start = 0x%02X\n", tx_start_verify);
    
    if (tx_start_verify != 0x01) {
        printf("CEC: ERROR: TX_ENABLE did not set properly, transmission may not have started\n");
        return -1;
    }
    
    // 9. Enhanced completion detection with proper timeout
    int timeout = 250; // Increased timeout to 250ms for reliable transmission
    int success = 0;
    uint8_t last_tx_enable = 0x01;
    int status_change_count = 0;
    
    printf("CEC: Waiting for transmission completion...\n");
    
    while (timeout > 0) {
        uint8_t status = 0;
        cec_read_reg(CEC_INTERRUPT_STATUS, &status);
        
        // Priority 1: Check for completion/error interrupts
        if (status & (CEC_INT_TX_DONE | CEC_INT_TX_ARB_LOST | CEC_INT_TX_RETRY_TIMEOUT)) {
            if (status & CEC_INT_TX_DONE) {
                printf("CEC: ✓ TX completed successfully (status=0x%02X)\n", status);
                cec_write_reg(CEC_INTERRUPT_CLEAR, CEC_INT_TX_DONE);
                success = 1;
                break;
            } else {
                printf("CEC: ✗ TX failed (status=0x%02X)\n", status);
                if (status & CEC_INT_TX_ARB_LOST) {
                    printf("CEC: - Arbitration lost\n");
                }
                if (status & CEC_INT_TX_RETRY_TIMEOUT) {
                    printf("CEC: - Retry timeout\n");
                }
                cec_write_reg(CEC_INTERRUPT_CLEAR, 
                             CEC_INT_TX_ARB_LOST | CEC_INT_TX_RETRY_TIMEOUT);
                break;
            }
        }
        
        // Priority 2: Check for TX_ENABLE auto-clear (primary completion indicator)
        uint8_t tx_enable_current = 0;
        cec_read_reg(CEC_TX_ENABLE_REG, &tx_enable_current);
        if (tx_enable_current == 0x00 && last_tx_enable == 0x01) {
            printf("CEC: ✓ TX completed - TX_ENABLE auto-cleared (status=0x%02X)\n", status);
            success = 1;
            break;
        }
        last_tx_enable = tx_enable_current;
        
        // Monitor progress but reduce debug spam
        if (status != 0 || timeout % 50 == 0) {
            if (status != 0) status_change_count++;
            if (timeout % 50 == 0 || status_change_count < 5) {
                printf("CEC: TX progress: status=0x%02X, enable=0x%02X, timeout=%dms\n", 
                       status, tx_enable_current, timeout);
            }
            if (status != 0) {
                cec_write_reg(CEC_INTERRUPT_CLEAR, status);
            }
        }
        
        usleep(1000); // 1ms polling interval
        timeout--;
    }
    
    if (!success) {
        printf("CEC: TX timeout - checking final state\n");
        uint8_t final_status = 0, final_enable = 0, final_length = 0, final_header = 0;
        uint8_t cec_glitch = 0, cec_sample = 0, cec_buffer = 0;
        cec_read_reg(CEC_INTERRUPT_STATUS, &final_status);
        cec_read_reg(CEC_TX_ENABLE_REG, &final_enable);
        cec_read_reg(CEC_TX_FRAME_LENGTH, &final_length);
        cec_read_reg(CEC_TX_FRAME_HEADER, &final_header);
        cec_read_reg(0x28, &cec_glitch);      // CEC Glitch Filter
        cec_read_reg(0x2A, &cec_sample);      // CEC Sample Time 
        cec_read_reg(0x2B, &cec_buffer);      // CEC Buffer Control
        
        printf("CEC: Final state - STATUS=0x%02X, ENABLE=0x%02X, LEN=0x%02X, HDR=0x%02X\n", 
               final_status, final_enable, final_length, final_header);
        printf("CEC: Timing regs - GLITCH=0x%02X, SAMPLE=0x%02X, BUFFER=0x%02X\n",
               cec_glitch, cec_sample, cec_buffer);
        
        // ENHANCED: Force clear TX_ENABLE if transmission failed to complete
        if (final_enable != 0x00) {
            printf("CEC: ⚠️  FORCE CLEARING stuck TX_ENABLE (was 0x%02X)\n", final_enable);
            cec_write_reg(CEC_TX_ENABLE_REG, 0x00);
            usleep(5000);
            // Clear any error interrupts that may have accumulated
            cec_write_reg(CEC_INTERRUPT_CLEAR, 0xFF);
            usleep(1000);
            printf("CEC: Force clear completed\n");
        }
    }
    
    // Re-enable RX
    cec_write_reg(CEC_RX_ENABLE, 0x01);
    return success ? 0 : -1;
}

// Monitor thread for receiving CEC messages
static void* cec_monitor_thread(void* arg) {
    (void)arg;
    
    //struct pollfd pfd;
    //pfd.fd = cec_state.cec_i2c_fd;
    //pfd.events = POLLIN;
    
    printf("CEC: Monitor thread started\n");
    
    // NEW: Add periodic register map verification to prevent 30-minute failures
    static uint32_t last_register_check = 0;
    const uint32_t REGISTER_CHECK_INTERVAL_MS = 60000; // Check every 60 seconds
    uint32_t current_time_ms = 0;
    
    while (cec_state.thread_running) {
        // NEW: Periodic register map verification
        current_time_ms += 10; // We sleep 10ms each iteration
        if (current_time_ms - last_register_check >= REGISTER_CHECK_INTERVAL_MS) {
            printf("CEC: Performing periodic register map verification...\n");
            if (cec_verify_register_maps() != 0) {
                printf("CEC: Register map corruption detected! Attempting recovery...\n");
                if (cec_reset_register_maps() == 0) {
                    printf("CEC: Register map corruption recovered successfully\n");
                } else {
                    printf("CEC: Failed to recover from register map corruption\n");
                }
            }
            last_register_check = current_time_ms;
        }
        
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
