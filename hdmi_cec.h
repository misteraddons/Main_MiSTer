#ifndef HDMI_CEC_H
#define HDMI_CEC_H

#include <stdint.h>
#include <stdbool.h>

// CEC I2C address for ADV7513
#define CEC_I2C_ADDR 0x3F

// CEC Register addresses
#define CEC_TX_FRAME_HEADER    0x00
#define CEC_TX_FRAME_DATA0     0x01
#define CEC_TX_FRAME_DATA1     0x02
#define CEC_TX_FRAME_DATA2     0x03
#define CEC_TX_FRAME_DATA3     0x04
#define CEC_TX_FRAME_DATA4     0x05
#define CEC_TX_FRAME_DATA5     0x06
#define CEC_TX_FRAME_DATA6     0x07
#define CEC_TX_FRAME_DATA7     0x08
#define CEC_TX_FRAME_DATA8     0x09
#define CEC_TX_FRAME_DATA9     0x0A
#define CEC_TX_FRAME_DATA10    0x0B
#define CEC_TX_FRAME_DATA11    0x0C
#define CEC_TX_FRAME_DATA12    0x0D
#define CEC_TX_FRAME_DATA13    0x0E
#define CEC_TX_FRAME_DATA14    0x0F

#define CEC_TX_FRAME_LENGTH    0x10
#define CEC_TX_ENABLE_REG      0x11
#define CEC_TX_RETRY           0x12
#define CEC_TX_LOW_DRIVE_COUNTER  0x14

#define CEC_RX_FRAME_HEADER    0x15
#define CEC_RX_FRAME_DATA0     0x16
#define CEC_RX_FRAME_DATA1     0x17
#define CEC_RX_FRAME_DATA2     0x18
#define CEC_RX_FRAME_DATA3     0x19
#define CEC_RX_FRAME_DATA4     0x1A
#define CEC_RX_FRAME_DATA5     0x1B
#define CEC_RX_FRAME_DATA6     0x1C
#define CEC_RX_FRAME_DATA7     0x1D
#define CEC_RX_FRAME_DATA8     0x1E
#define CEC_RX_FRAME_DATA9     0x1F
#define CEC_RX_FRAME_DATA10    0x20
#define CEC_RX_FRAME_DATA11    0x21
#define CEC_RX_FRAME_DATA12    0x22
#define CEC_RX_FRAME_DATA13    0x23
#define CEC_RX_FRAME_DATA14    0x24

#define CEC_RX_FRAME_LENGTH    0x25
#define CEC_RX_ENABLE_REG      0x26

#define CEC_LOGICAL_ADDR0      0x27
#define CEC_LOGICAL_ADDR1      0x28
#define CEC_LOGICAL_ADDR2      0x29
#define CEC_LOGICAL_ADDR_MASK  0x2A

#define CEC_CLK_DIV            0x2B

#define CEC_SOFT_RESET         0x2C

#define CEC_INT_ENABLE         0x40
#define CEC_INT_STATUS         0x41
#define CEC_INT_CLEAR          0x42

// CEC Logical Addresses
#define CEC_LOG_ADDR_TV            0
#define CEC_LOG_ADDR_RECORDER1     1
#define CEC_LOG_ADDR_RECORDER2     2
#define CEC_LOG_ADDR_TUNER1        3
#define CEC_LOG_ADDR_PLAYBACK1     4
#define CEC_LOG_ADDR_AUDIOSYSTEM   5
#define CEC_LOG_ADDR_TUNER2        6
#define CEC_LOG_ADDR_TUNER3        7
#define CEC_LOG_ADDR_PLAYBACK2     8
#define CEC_LOG_ADDR_RECORDER3     9
#define CEC_LOG_ADDR_TUNER4        10
#define CEC_LOG_ADDR_PLAYBACK3     11
#define CEC_LOG_ADDR_UNREGISTERED  15
#define CEC_LOG_ADDR_BROADCAST     15

// CEC Opcodes
#define CEC_OPCODE_FEATURE_ABORT              0x00
#define CEC_OPCODE_IMAGE_VIEW_ON              0x04
#define CEC_OPCODE_TUNER_STEP_INCREMENT       0x05
#define CEC_OPCODE_TUNER_STEP_DECREMENT       0x06
#define CEC_OPCODE_TUNER_DEVICE_STATUS        0x07
#define CEC_OPCODE_GIVE_TUNER_DEVICE_STATUS   0x08
#define CEC_OPCODE_RECORD_ON                  0x09
#define CEC_OPCODE_RECORD_STATUS              0x0A
#define CEC_OPCODE_RECORD_OFF                 0x0B
#define CEC_OPCODE_TEXT_VIEW_ON               0x0D
#define CEC_OPCODE_RECORD_TV_SCREEN           0x0F
#define CEC_OPCODE_GIVE_DECK_STATUS           0x1A
#define CEC_OPCODE_DECK_STATUS                0x1B
#define CEC_OPCODE_SET_MENU_LANGUAGE          0x32
#define CEC_OPCODE_CLEAR_ANALOGUE_TIMER       0x33
#define CEC_OPCODE_SET_ANALOGUE_TIMER         0x34
#define CEC_OPCODE_TIMER_STATUS               0x35
#define CEC_OPCODE_STANDBY                    0x36
#define CEC_OPCODE_PLAY                       0x41
#define CEC_OPCODE_DECK_CONTROL               0x42
#define CEC_OPCODE_TIMER_CLEARED_STATUS       0x43
#define CEC_OPCODE_USER_CONTROL_PRESSED       0x44
#define CEC_OPCODE_USER_CONTROL_RELEASED      0x45
#define CEC_OPCODE_GIVE_OSD_NAME              0x46
#define CEC_OPCODE_SET_OSD_NAME               0x47
#define CEC_OPCODE_SET_OSD_STRING             0x64
#define CEC_OPCODE_SET_TIMER_PROGRAM_TITLE    0x67
#define CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST  0x70
#define CEC_OPCODE_GIVE_AUDIO_STATUS          0x71
#define CEC_OPCODE_SET_SYSTEM_AUDIO_MODE      0x72
#define CEC_OPCODE_REPORT_AUDIO_STATUS        0x7A
#define CEC_OPCODE_GIVE_SYSTEM_AUDIO_MODE_STATUS 0x7D
#define CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS   0x7E
#define CEC_OPCODE_ROUTING_CHANGE             0x80
#define CEC_OPCODE_ROUTING_INFORMATION        0x81
#define CEC_OPCODE_ACTIVE_SOURCE              0x82
#define CEC_OPCODE_GIVE_PHYSICAL_ADDRESS      0x83
#define CEC_OPCODE_REPORT_PHYSICAL_ADDRESS    0x84
#define CEC_OPCODE_REQUEST_ACTIVE_SOURCE      0x85
#define CEC_OPCODE_SET_STREAM_PATH            0x86
#define CEC_OPCODE_DEVICE_VENDOR_ID           0x87
#define CEC_OPCODE_VENDOR_COMMAND             0x89
#define CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN  0x8A
#define CEC_OPCODE_VENDOR_REMOTE_BUTTON_UP    0x8B
#define CEC_OPCODE_GIVE_DEVICE_VENDOR_ID      0x8C
#define CEC_OPCODE_MENU_REQUEST               0x8D
#define CEC_OPCODE_MENU_STATUS                0x8E
#define CEC_OPCODE_GIVE_DEVICE_POWER_STATUS   0x8F
#define CEC_OPCODE_REPORT_POWER_STATUS        0x90
#define CEC_OPCODE_GET_MENU_LANGUAGE          0x91
#define CEC_OPCODE_SELECT_ANALOGUE_SERVICE    0x92
#define CEC_OPCODE_SELECT_DIGITAL_SERVICE     0x93
#define CEC_OPCODE_SET_DIGITAL_TIMER          0x97
#define CEC_OPCODE_CLEAR_DIGITAL_TIMER        0x99
#define CEC_OPCODE_SET_AUDIO_RATE             0x9A
#define CEC_OPCODE_INACTIVE_SOURCE            0x9D
#define CEC_OPCODE_CEC_VERSION                0x9E
#define CEC_OPCODE_GET_CEC_VERSION            0x9F
#define CEC_OPCODE_VENDOR_COMMAND_WITH_ID     0xA0
#define CEC_OPCODE_CLEAR_EXTERNAL_TIMER       0xA1
#define CEC_OPCODE_SET_EXTERNAL_TIMER         0xA2
#define CEC_OPCODE_REPORT_SHORT_AUDIO_DESCRIPTOR 0xA3
#define CEC_OPCODE_REQUEST_SHORT_AUDIO_DESCRIPTOR 0xA4
#define CEC_OPCODE_INITIATE_ARC               0xC0
#define CEC_OPCODE_REPORT_ARC_INITIATED       0xC1
#define CEC_OPCODE_REPORT_ARC_TERMINATED      0xC2
#define CEC_OPCODE_REQUEST_ARC_INITIATION     0xC3
#define CEC_OPCODE_REQUEST_ARC_TERMINATION    0xC4
#define CEC_OPCODE_TERMINATE_ARC              0xC5
#define CEC_OPCODE_CDC_MESSAGE                0xF8
#define CEC_OPCODE_ABORT                      0xFF

// User Control Codes
#define CEC_USER_CONTROL_SELECT         0x00
#define CEC_USER_CONTROL_UP             0x01
#define CEC_USER_CONTROL_DOWN           0x02
#define CEC_USER_CONTROL_LEFT           0x03
#define CEC_USER_CONTROL_RIGHT          0x04
#define CEC_USER_CONTROL_RIGHT_UP       0x05
#define CEC_USER_CONTROL_RIGHT_DOWN     0x06
#define CEC_USER_CONTROL_LEFT_UP        0x07
#define CEC_USER_CONTROL_LEFT_DOWN      0x08
#define CEC_USER_CONTROL_ROOT_MENU      0x09
#define CEC_USER_CONTROL_SETUP_MENU     0x0A
#define CEC_USER_CONTROL_CONTENTS_MENU  0x0B
#define CEC_USER_CONTROL_FAVORITE_MENU  0x0C
#define CEC_USER_CONTROL_EXIT           0x0D
#define CEC_USER_CONTROL_VOLUME_UP      0x41
#define CEC_USER_CONTROL_VOLUME_DOWN    0x42
#define CEC_USER_CONTROL_MUTE           0x43
#define CEC_USER_CONTROL_PLAY           0x44
#define CEC_USER_CONTROL_STOP           0x45
#define CEC_USER_CONTROL_PAUSE          0x46
#define CEC_USER_CONTROL_RECORD         0x47
#define CEC_USER_CONTROL_REWIND         0x48
#define CEC_USER_CONTROL_FAST_FORWARD   0x49
#define CEC_USER_CONTROL_EJECT          0x4A
#define CEC_USER_CONTROL_FORWARD        0x4B
#define CEC_USER_CONTROL_BACKWARD       0x4C
#define CEC_USER_CONTROL_POWER          0x40

// Power Status
#define CEC_POWER_STATUS_ON                0x00
#define CEC_POWER_STATUS_STANDBY           0x01
#define CEC_POWER_STATUS_STANDBY_TO_ON     0x02
#define CEC_POWER_STATUS_ON_TO_STANDBY     0x03

// CEC Interrupts
#define CEC_INT_TX_RDY         (1 << 0)
#define CEC_INT_TX_ARBITRATION (1 << 1)
#define CEC_INT_TX_RETRY_TIMEOUT (1 << 2)
#define CEC_INT_TX_DONE        (1 << 3)
#define CEC_INT_RX_RDY1        (1 << 4)
#define CEC_INT_RX_RDY2        (1 << 5)
#define CEC_INT_RX_RDY3        (1 << 6)

// CEC Message structure
typedef struct {
    uint8_t header;
    uint8_t opcode;
    uint8_t data[14];
    uint8_t length;
} cec_message_t;

// CEC Functions
bool cec_init(bool enable);
void cec_deinit(void);
bool cec_send_message(const cec_message_t *msg);
bool cec_receive_message(cec_message_t *msg);
void cec_poll(void);
bool cec_is_enabled(void);

// High-level CEC commands (return true on success, false on failure)
bool cec_send_image_view_on(void);
bool cec_send_active_source(void);
bool cec_send_standby(void);
bool cec_send_report_physical_address(void);
bool cec_send_device_vendor_id(void);
bool cec_send_cec_version(uint8_t destination);
bool cec_send_set_osd_name(const char* name);

// Menu navigation (return true on success, false on failure)
bool cec_send_menu_status(uint8_t destination, uint8_t status);
bool cec_send_user_control_pressed(uint8_t destination, uint8_t control_code);
bool cec_send_user_control_released(uint8_t destination);

// Get/Set functions
void cec_set_logical_address(uint8_t addr);
uint8_t cec_get_logical_address(void);
void cec_set_physical_address(uint16_t addr);
uint16_t cec_get_physical_address(void);

// Input integration
void cec_handle_remote_button(uint8_t button_code, bool pressed);
void cec_send_virtual_key(uint16_t key_code, bool pressed);
void cec_check_button_timeout(void);

#endif // HDMI_CEC_H