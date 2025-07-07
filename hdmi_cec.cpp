#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "hdmi_cec.h"
#include "smbus.h"
#include "cfg.h"
#include "hardware.h"
#include <linux/input.h>

static bool cec_enabled = false;
static int cec_fd = -1;
static uint8_t cec_logical_addr = CEC_LOG_ADDR_PLAYBACK1;
static uint16_t cec_physical_addr = 0x1000; // Default 1.0.0.0

// Button state tracking
static uint8_t current_pressed_button = 0xFF;
static uint16_t current_linux_key = 0;
static uint32_t button_press_time = 0;
static const uint32_t BUTTON_TIMEOUT_MS = 500; // Auto-release after 500ms

// External function from input.cpp for sending virtual key events
extern void input_cec_send_key(uint16_t key, bool pressed);

// Debug helper functions
static const char* cec_opcode_name(uint8_t opcode);
static const char* cec_user_control_name(uint8_t control_code);
static const char* cec_device_type_name(uint8_t device_type);
static void cec_debug_message(const char* direction, const cec_message_t *msg);

// Calculate CEC clock divider based on 12MHz crystal
// CEC requires 0.05ms time quanta (20kHz)
// Clock divider = Input Clock / (20kHz * 10) - 1
// For 12MHz: 12000000 / (20000 * 10) - 1 = 59
#define CEC_CLOCK_DIV_12MHZ 59

static bool cec_write_register(uint8_t reg, uint8_t value)
{
    if (cec_fd < 0) return false;
    int res = i2c_smbus_write_byte_data(cec_fd, reg, value);
    if (res < 0) {
        printf("CEC: Failed to write register 0x%02X\n", reg);
        return false;
    }
    return true;
}

static uint8_t cec_read_register(uint8_t reg)
{
    if (cec_fd < 0) return 0;
    int res = i2c_smbus_read_byte_data(cec_fd, reg);
    if (res < 0) {
        printf("CEC: Failed to read register 0x%02X\n", reg);
        return 0;
    }
    return (uint8_t)res;
}

bool cec_init(bool enable)
{
    if (!enable) {
        cec_deinit();
        return true;
    }

    printf("CEC INIT: Initializing CEC with 12MHz crystal\n");
    
    // Open I2C device for CEC
    cec_fd = i2c_open(CEC_I2C_ADDR, 0);
    if (cec_fd < 0) {
        printf("CEC INIT: Failed to open I2C device at address 0x%02X\n", CEC_I2C_ADDR);
        return false;
    }
    
    printf("CEC INIT: Opened CEC I2C device successfully\n");

    // ADV7513 main register configuration for CEC
    int main_fd = i2c_open(0x39, 0);
    if (main_fd < 0) {
        printf("CEC INIT: Failed to open main ADV7513 device\n");
        i2c_close(cec_fd);
        cec_fd = -1;
        return false;
    }

    printf("CEC INIT: Configuring ADV7513 main registers\n");
    
    // Enable CEC clock
    i2c_smbus_write_byte_data(main_fd, 0x96, 0x20); // CEC powered up
    printf("CEC INIT: CEC clock enabled (reg 0x96 = 0x20)\n");
    
    // Set logical address valid bits
    i2c_smbus_write_byte_data(main_fd, 0x98, 0x03); // Enable logical address 0 and 1
    printf("CEC INIT: Logical address bits enabled (reg 0x98 = 0x03)\n");
    
    i2c_close(main_fd);

    // Soft reset CEC
    printf("CEC INIT: Performing soft reset\n");
    cec_write_register(CEC_SOFT_RESET, 0x01);
    usleep(5000); // Wait 5ms
    cec_write_register(CEC_SOFT_RESET, 0x00);

    // Configure CEC clock divider for 12MHz crystal
    printf("CEC INIT: Setting clock divider to %d for 12MHz crystal\n", CEC_CLOCK_DIV_12MHZ);
    cec_write_register(CEC_CLK_DIV, CEC_CLOCK_DIV_12MHZ);

    // Set logical address
    printf("CEC INIT: Setting logical address to %d (Playback Device)\n", cec_logical_addr);
    cec_write_register(CEC_LOGICAL_ADDR0, cec_logical_addr | 0x10); // Enable logical address 0
    cec_write_register(CEC_LOGICAL_ADDR_MASK, 0x01); // Enable only logical address 0

    // Configure TX
    printf("CEC INIT: Configuring transmission parameters\n");
    cec_write_register(CEC_TX_RETRY, 0x03); // 3 retries
    cec_write_register(CEC_TX_LOW_DRIVE_COUNTER, 0x14); // Low drive counter

    // Enable RX
    printf("CEC INIT: Enabling reception\n");
    cec_write_register(CEC_RX_ENABLE_REG, 0x01);

    // Clear all interrupts
    cec_write_register(CEC_INT_CLEAR, 0x7F);

    // Enable interrupts (we'll poll instead)
    cec_write_register(CEC_INT_ENABLE, 0x00);

    cec_enabled = true;
    printf("CEC INIT: Initialization completed successfully\n");
    
    // Wait a bit then announce ourselves
    usleep(500000); // 500ms
    
    // Send Report Physical Address to announce our presence
    cec_send_report_physical_address();
    
    return true;
}

void cec_deinit(void)
{
    if (cec_fd >= 0) {
        // Send standby before disabling
        if (cec_enabled) {
            cec_send_standby();
            usleep(100000); // 100ms
        }
        
        // Disable CEC in ADV7513
        int main_fd = i2c_open(0x39, 0);
        if (main_fd >= 0) {
            i2c_smbus_write_byte_data(main_fd, 0x96, 0x00); // CEC powered down
            i2c_close(main_fd);
        }
        
        i2c_close(cec_fd);
        cec_fd = -1;
    }
    cec_enabled = false;
}

bool cec_send_message(const cec_message_t *msg)
{
    if (!cec_enabled || cec_fd < 0 || !msg) return false;

    // Debug: Log the outgoing message
    cec_debug_message("TX", msg);

    // Wait for TX ready
    int timeout = 100; // 100ms timeout
    while (timeout-- > 0) {
        uint8_t status = cec_read_register(CEC_INT_STATUS);
        if (status & CEC_INT_TX_RDY) {
            cec_write_register(CEC_INT_CLEAR, CEC_INT_TX_RDY);
            break;
        }
        usleep(1000); // 1ms
    }
    
    if (timeout <= 0) {
        printf("CEC TX: Timeout waiting for TX ready\n");
        return false;
    }

    // Write header
    cec_write_register(CEC_TX_FRAME_HEADER, msg->header);
    
    // Write data if present
    if (msg->length > 1) {
        cec_write_register(CEC_TX_FRAME_DATA0, msg->opcode);
        
        for (int i = 0; i < msg->length - 2 && i < 14; i++) {
            cec_write_register(CEC_TX_FRAME_DATA1 + i, msg->data[i]);
        }
    }
    
    // Set frame length (includes header)
    cec_write_register(CEC_TX_FRAME_LENGTH, msg->length);
    
    // Enable transmission
    cec_write_register(CEC_TX_ENABLE_REG, 0x01);
    
    // Wait for transmission complete
    timeout = 200; // 200ms timeout
    while (timeout-- > 0) {
        uint8_t status = cec_read_register(CEC_INT_STATUS);
        if (status & CEC_INT_TX_DONE) {
            cec_write_register(CEC_INT_CLEAR, CEC_INT_TX_DONE);
            return true;
        }
        if (status & (CEC_INT_TX_ARBITRATION | CEC_INT_TX_RETRY_TIMEOUT)) {
            cec_write_register(CEC_INT_CLEAR, CEC_INT_TX_ARBITRATION | CEC_INT_TX_RETRY_TIMEOUT);
            printf("CEC TX: Failed - %s%s\n", 
                (status & CEC_INT_TX_ARBITRATION) ? "Arbitration Lost " : "",
                (status & CEC_INT_TX_RETRY_TIMEOUT) ? "Retry Timeout" : "");
            return false;
        }
        usleep(1000); // 1ms
    }
    
    printf("CEC TX: Timeout waiting for transmission complete\n");
    return false;
}

bool cec_receive_message(cec_message_t *msg)
{
    if (!cec_enabled || cec_fd < 0 || !msg) return false;
    
    uint8_t status = cec_read_register(CEC_INT_STATUS);
    
    // Check if any RX buffer has data
    if (!(status & (CEC_INT_RX_RDY1 | CEC_INT_RX_RDY2 | CEC_INT_RX_RDY3))) {
        return false;
    }
    
    // Read from first available buffer
    uint8_t rx_num = 0;
    if (status & CEC_INT_RX_RDY1) {
        rx_num = 0;
        cec_write_register(CEC_INT_CLEAR, CEC_INT_RX_RDY1);
    } else if (status & CEC_INT_RX_RDY2) {
        rx_num = 1;
        cec_write_register(CEC_INT_CLEAR, CEC_INT_RX_RDY2);
    } else if (status & CEC_INT_RX_RDY3) {
        rx_num = 2;
        cec_write_register(CEC_INT_CLEAR, CEC_INT_RX_RDY3);
    }
    
    // Get message length
    msg->length = cec_read_register(CEC_RX_FRAME_LENGTH + rx_num * 0x10) & 0x1F;
    if (msg->length == 0 || msg->length > 16) {
        printf("CEC RX: Invalid message length %d from buffer %d\n", msg->length, rx_num);
        return false;
    }
    
    // Read header
    msg->header = cec_read_register(CEC_RX_FRAME_HEADER + rx_num * 0x10);
    
    // Read opcode and data if present
    if (msg->length > 1) {
        msg->opcode = cec_read_register(CEC_RX_FRAME_DATA0 + rx_num * 0x10);
        
        for (int i = 0; i < msg->length - 2 && i < 14; i++) {
            msg->data[i] = cec_read_register(CEC_RX_FRAME_DATA1 + rx_num * 0x10 + i);
        }
    } else {
        msg->opcode = 0; // Polling message
    }
    
    return true;
}

void cec_poll(void)
{
    if (!cec_enabled) return;
    
    cec_message_t msg;
    while (cec_receive_message(&msg)) {
        uint8_t source = (msg.header >> 4) & 0x0F;
        uint8_t dest = msg.header & 0x0F;
        
        // Debug: Log the incoming message
        cec_debug_message("RX", &msg);
        
        // Handle messages addressed to us or broadcast
        if (dest == cec_logical_addr || dest == CEC_LOG_ADDR_BROADCAST) {
            printf("CEC HANDLE: Processing command %s from device %X\n", cec_opcode_name(msg.opcode), source);
            
            switch (msg.opcode) {
                case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS:
                    printf("CEC HANDLE: Responding to Give Physical Address request\n");
                    cec_send_report_physical_address();
                    break;
                    
                case CEC_OPCODE_GIVE_OSD_NAME:
                    printf("CEC HANDLE: Responding to Give OSD Name request\n");
                    cec_send_set_osd_name("MiSTer");
                    break;
                    
                case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID:
                    printf("CEC HANDLE: Responding to Give Device Vendor ID request\n");
                    cec_send_device_vendor_id();
                    break;
                    
                case CEC_OPCODE_GET_CEC_VERSION:
                    printf("CEC HANDLE: Responding to Get CEC Version request\n");
                    cec_send_cec_version(source);
                    break;
                    
                case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS:
                    {
                        printf("CEC HANDLE: Responding to Give Device Power Status request\n");
                        cec_message_t reply;
                        reply.header = (cec_logical_addr << 4) | source;
                        reply.opcode = CEC_OPCODE_REPORT_POWER_STATUS;
                        reply.data[0] = CEC_POWER_STATUS_ON;
                        reply.length = 3;
                        cec_send_message(&reply);
                    }
                    break;
                    
                case CEC_OPCODE_SET_STREAM_PATH:
                    if (msg.length >= 4) {
                        uint16_t addr = (msg.data[0] << 8) | msg.data[1];
                        printf("CEC HANDLE: Set Stream Path request for address %d.%d.%d.%d\n",
                            (addr >> 12) & 0xF, (addr >> 8) & 0xF, (addr >> 4) & 0xF, addr & 0xF);
                        if (addr == cec_physical_addr) {
                            printf("CEC HANDLE: Address matches ours, sending Active Source\n");
                            cec_send_active_source();
                        }
                    }
                    break;
                    
                case CEC_OPCODE_REQUEST_ACTIVE_SOURCE:
                    printf("CEC HANDLE: Request Active Source received (not responding)\n");
                    // We could respond with active source if we want to take over
                    break;
                    
                case CEC_OPCODE_USER_CONTROL_PRESSED:
                    if (msg.length >= 3) {
                        // Handle remote control button presses
                        uint8_t button = msg.data[0];
                        printf("CEC HANDLE: User Control Pressed - %s (0x%02X)\n", 
                            cec_user_control_name(button), button);
                        cec_handle_remote_button(button, true);
                    }
                    break;
                    
                case CEC_OPCODE_USER_CONTROL_RELEASED:
                    printf("CEC HANDLE: User Control Released\n");
                    // Handle button release - release last pressed button
                    cec_handle_remote_button(0, false);
                    break;
                    
                case CEC_OPCODE_MENU_REQUEST:
                    if (msg.length >= 3) {
                        uint8_t menu_type = msg.data[0];
                        const char* menu_names[] = {"Activate", "Deactivate", "Query"};
                        printf("CEC HANDLE: Menu Request - %s (%d)\n", 
                            menu_type < 3 ? menu_names[menu_type] : "Unknown", menu_type);
                        // 0 = Activate, 1 = Deactivate, 2 = Query
                        cec_send_menu_status(source, menu_type == 0 ? 0 : 1);
                    }
                    break;
                    
                default:
                    printf("CEC HANDLE: Unhandled command %s (0x%02X)\n", 
                        cec_opcode_name(msg.opcode), msg.opcode);
                    break;
            }
        }
    }
}

bool cec_is_enabled(void)
{
    return cec_enabled;
}

void cec_send_image_view_on(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
    msg.opcode = CEC_OPCODE_IMAGE_VIEW_ON;
    msg.length = 2;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Image View On sent successfully\n");
    } else {
        printf("CEC CMD: Failed to send Image View On\n");
    }
}

void cec_send_active_source(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
    msg.opcode = CEC_OPCODE_ACTIVE_SOURCE;
    msg.data[0] = (cec_physical_addr >> 8) & 0xFF;
    msg.data[1] = cec_physical_addr & 0xFF;
    msg.length = 4;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Active Source sent successfully\n");
    } else {
        printf("CEC CMD: Failed to send Active Source\n");
    }
}

void cec_send_standby(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
    msg.opcode = CEC_OPCODE_STANDBY;
    msg.length = 2;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Standby sent successfully\n");
    } else {
        printf("CEC CMD: Failed to send Standby\n");
    }
}

void cec_send_report_physical_address(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
    msg.opcode = CEC_OPCODE_REPORT_PHYSICAL_ADDRESS;
    msg.data[0] = (cec_physical_addr >> 8) & 0xFF;
    msg.data[1] = cec_physical_addr & 0xFF;
    msg.data[2] = 4; // Device type: Playback Device
    msg.length = 5;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Report Physical Address sent successfully\n");
    } else {
        printf("CEC CMD: Failed to send Report Physical Address\n");
    }
}

void cec_send_device_vendor_id(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
    msg.opcode = CEC_OPCODE_DEVICE_VENDOR_ID;
    // Using generic vendor ID (0x000000)
    msg.data[0] = 0x00;
    msg.data[1] = 0x00;
    msg.data[2] = 0x00;
    msg.length = 5;
    cec_send_message(&msg);
}

void cec_send_cec_version(uint8_t destination)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_CEC_VERSION;
    msg.data[0] = 0x05; // CEC version 1.4
    msg.length = 3;
    cec_send_message(&msg);
}

void cec_send_set_osd_name(const char* name)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
    msg.opcode = CEC_OPCODE_SET_OSD_NAME;
    
    int name_len = strlen(name);
    if (name_len > 14) name_len = 14;
    
    memcpy(msg.data, name, name_len);
    msg.length = 2 + name_len;
    cec_send_message(&msg);
}

void cec_send_menu_status(uint8_t destination, uint8_t status)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_MENU_STATUS;
    msg.data[0] = status; // 0 = activated, 1 = deactivated
    msg.length = 3;
    cec_send_message(&msg);
}

void cec_send_user_control_pressed(uint8_t destination, uint8_t control_code)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_USER_CONTROL_PRESSED;
    msg.data[0] = control_code;
    msg.length = 3;
    cec_send_message(&msg);
}

void cec_send_user_control_released(uint8_t destination)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_USER_CONTROL_RELEASED;
    msg.length = 2;
    cec_send_message(&msg);
}

void cec_set_logical_address(uint8_t addr)
{
    cec_logical_addr = addr & 0x0F;
    if (cec_enabled && cec_fd >= 0) {
        cec_write_register(CEC_LOGICAL_ADDR0, cec_logical_addr | 0x10);
    }
}

uint8_t cec_get_logical_address(void)
{
    return cec_logical_addr;
}

void cec_set_physical_address(uint16_t addr)
{
    cec_physical_addr = addr;
    if (cec_enabled) {
        // Announce new address
        cec_send_report_physical_address();
    }
}

uint16_t cec_get_physical_address(void)
{
    return cec_physical_addr;
}

void cec_send_virtual_key(uint16_t key_code, bool pressed)
{
    input_cec_send_key(key_code, pressed);
}

void cec_handle_remote_button(uint8_t button_code, bool pressed)
{
    if (!cec_enabled) return;
    
    // Handle button release
    if (!pressed) {
        if (current_linux_key != 0) {
            printf("CEC: Remote button released: 0x%02X\n", current_pressed_button);
            cec_send_virtual_key(current_linux_key, false);
            current_linux_key = 0;
            current_pressed_button = 0xFF;
            button_press_time = 0;
        }
        return;
    }
    
    // Simple debounce: ignore repeated button within 50ms
    uint32_t current_time = GetTimer(0);
    if (button_code == current_pressed_button && 
        (current_time - button_press_time) < 50) {
        return;
    }
    
    printf("CEC: Remote button pressed: 0x%02X\n", button_code);
    
    // Map CEC remote control codes to Linux input key codes
    uint16_t linux_key = 0;
    switch (button_code) {
        case CEC_USER_CONTROL_UP:
            linux_key = KEY_UP;
            break;
        case CEC_USER_CONTROL_DOWN:
            linux_key = KEY_DOWN;
            break;
        case CEC_USER_CONTROL_LEFT:
            linux_key = KEY_LEFT;
            break;
        case CEC_USER_CONTROL_RIGHT:
            linux_key = KEY_RIGHT;
            break;
        case CEC_USER_CONTROL_SELECT:
            linux_key = KEY_ENTER;
            break;
        case CEC_USER_CONTROL_ROOT_MENU:
        case CEC_USER_CONTROL_SETUP_MENU:
            linux_key = KEY_F12; // Menu button
            break;
        case CEC_USER_CONTROL_EXIT:
            linux_key = KEY_ESC;
            break;
        case CEC_USER_CONTROL_PLAY:
            linux_key = KEY_SPACE;
            break;
        case CEC_USER_CONTROL_PAUSE:
            linux_key = KEY_SPACE;
            break;
        case CEC_USER_CONTROL_STOP:
            linux_key = KEY_S;
            break;
        case CEC_USER_CONTROL_FAST_FORWARD:
            linux_key = KEY_F;
            break;
        case CEC_USER_CONTROL_REWIND:
            linux_key = KEY_R;
            break;
        case CEC_USER_CONTROL_VOLUME_UP:
            linux_key = KEY_EQUAL; // + key
            break;
        case CEC_USER_CONTROL_VOLUME_DOWN:
            linux_key = KEY_MINUS; // - key
            break;
        case CEC_USER_CONTROL_MUTE:
            linux_key = KEY_M;
            break;
        case CEC_USER_CONTROL_POWER:
            linux_key = KEY_P;
            break;
        default:
            printf("CEC: Unmapped button code: 0x%02X\n", button_code);
            return;
    }
    
    if (linux_key != 0) {
        // Release previous button if a different one was pressed
        if (current_linux_key != 0 && current_linux_key != linux_key) {
            cec_send_virtual_key(current_linux_key, false);
        }
        
        current_pressed_button = button_code;
        current_linux_key = linux_key;
        button_press_time = current_time;
        cec_send_virtual_key(linux_key, true);
    }
}

void cec_check_button_timeout(void)
{
    if (!cec_enabled || current_linux_key == 0) return;
    
    uint32_t current_time = GetTimer(0);
    if ((current_time - button_press_time) >= BUTTON_TIMEOUT_MS) {
        printf("CEC: Auto-releasing button 0x%02X after timeout\n", current_pressed_button);
        cec_send_virtual_key(current_linux_key, false);
        current_linux_key = 0;
        current_pressed_button = 0xFF;
        button_press_time = 0;
    }
}

// Debug helper functions
static const char* cec_opcode_name(uint8_t opcode)
{
    switch (opcode) {
        case CEC_OPCODE_FEATURE_ABORT: return "FEATURE_ABORT";
        case CEC_OPCODE_IMAGE_VIEW_ON: return "IMAGE_VIEW_ON";
        case CEC_OPCODE_TUNER_STEP_INCREMENT: return "TUNER_STEP_INCREMENT";
        case CEC_OPCODE_TUNER_STEP_DECREMENT: return "TUNER_STEP_DECREMENT";
        case CEC_OPCODE_TUNER_DEVICE_STATUS: return "TUNER_DEVICE_STATUS";
        case CEC_OPCODE_GIVE_TUNER_DEVICE_STATUS: return "GIVE_TUNER_DEVICE_STATUS";
        case CEC_OPCODE_RECORD_ON: return "RECORD_ON";
        case CEC_OPCODE_RECORD_STATUS: return "RECORD_STATUS";
        case CEC_OPCODE_RECORD_OFF: return "RECORD_OFF";
        case CEC_OPCODE_TEXT_VIEW_ON: return "TEXT_VIEW_ON";
        case CEC_OPCODE_RECORD_TV_SCREEN: return "RECORD_TV_SCREEN";
        case CEC_OPCODE_GIVE_DECK_STATUS: return "GIVE_DECK_STATUS";
        case CEC_OPCODE_DECK_STATUS: return "DECK_STATUS";
        case CEC_OPCODE_SET_MENU_LANGUAGE: return "SET_MENU_LANGUAGE";
        case CEC_OPCODE_CLEAR_ANALOGUE_TIMER: return "CLEAR_ANALOGUE_TIMER";
        case CEC_OPCODE_SET_ANALOGUE_TIMER: return "SET_ANALOGUE_TIMER";
        case CEC_OPCODE_TIMER_STATUS: return "TIMER_STATUS";
        case CEC_OPCODE_STANDBY: return "STANDBY";
        case CEC_OPCODE_PLAY: return "PLAY";
        case CEC_OPCODE_DECK_CONTROL: return "DECK_CONTROL";
        case CEC_OPCODE_TIMER_CLEARED_STATUS: return "TIMER_CLEARED_STATUS";
        case CEC_OPCODE_USER_CONTROL_PRESSED: return "USER_CONTROL_PRESSED";
        case CEC_OPCODE_USER_CONTROL_RELEASED: return "USER_CONTROL_RELEASED";
        case CEC_OPCODE_GIVE_OSD_NAME: return "GIVE_OSD_NAME";
        case CEC_OPCODE_SET_OSD_NAME: return "SET_OSD_NAME";
        case CEC_OPCODE_SET_OSD_STRING: return "SET_OSD_STRING";
        case CEC_OPCODE_SET_TIMER_PROGRAM_TITLE: return "SET_TIMER_PROGRAM_TITLE";
        case CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST: return "SYSTEM_AUDIO_MODE_REQUEST";
        case CEC_OPCODE_GIVE_AUDIO_STATUS: return "GIVE_AUDIO_STATUS";
        case CEC_OPCODE_SET_SYSTEM_AUDIO_MODE: return "SET_SYSTEM_AUDIO_MODE";
        case CEC_OPCODE_REPORT_AUDIO_STATUS: return "REPORT_AUDIO_STATUS";
        case CEC_OPCODE_GIVE_SYSTEM_AUDIO_MODE_STATUS: return "GIVE_SYSTEM_AUDIO_MODE_STATUS";
        case CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS: return "SYSTEM_AUDIO_MODE_STATUS";
        case CEC_OPCODE_ROUTING_CHANGE: return "ROUTING_CHANGE";
        case CEC_OPCODE_ROUTING_INFORMATION: return "ROUTING_INFORMATION";
        case CEC_OPCODE_ACTIVE_SOURCE: return "ACTIVE_SOURCE";
        case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS: return "GIVE_PHYSICAL_ADDRESS";
        case CEC_OPCODE_REPORT_PHYSICAL_ADDRESS: return "REPORT_PHYSICAL_ADDRESS";
        case CEC_OPCODE_REQUEST_ACTIVE_SOURCE: return "REQUEST_ACTIVE_SOURCE";
        case CEC_OPCODE_SET_STREAM_PATH: return "SET_STREAM_PATH";
        case CEC_OPCODE_DEVICE_VENDOR_ID: return "DEVICE_VENDOR_ID";
        case CEC_OPCODE_VENDOR_COMMAND: return "VENDOR_COMMAND";
        case CEC_OPCODE_VENDOR_REMOTE_BUTTON_DOWN: return "VENDOR_REMOTE_BUTTON_DOWN";
        case CEC_OPCODE_VENDOR_REMOTE_BUTTON_UP: return "VENDOR_REMOTE_BUTTON_UP";
        case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID: return "GIVE_DEVICE_VENDOR_ID";
        case CEC_OPCODE_MENU_REQUEST: return "MENU_REQUEST";
        case CEC_OPCODE_MENU_STATUS: return "MENU_STATUS";
        case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS: return "GIVE_DEVICE_POWER_STATUS";
        case CEC_OPCODE_REPORT_POWER_STATUS: return "REPORT_POWER_STATUS";
        case CEC_OPCODE_GET_MENU_LANGUAGE: return "GET_MENU_LANGUAGE";
        case CEC_OPCODE_SELECT_ANALOGUE_SERVICE: return "SELECT_ANALOGUE_SERVICE";
        case CEC_OPCODE_SELECT_DIGITAL_SERVICE: return "SELECT_DIGITAL_SERVICE";
        case CEC_OPCODE_SET_DIGITAL_TIMER: return "SET_DIGITAL_TIMER";
        case CEC_OPCODE_CLEAR_DIGITAL_TIMER: return "CLEAR_DIGITAL_TIMER";
        case CEC_OPCODE_SET_AUDIO_RATE: return "SET_AUDIO_RATE";
        case CEC_OPCODE_INACTIVE_SOURCE: return "INACTIVE_SOURCE";
        case CEC_OPCODE_CEC_VERSION: return "CEC_VERSION";
        case CEC_OPCODE_GET_CEC_VERSION: return "GET_CEC_VERSION";
        case CEC_OPCODE_VENDOR_COMMAND_WITH_ID: return "VENDOR_COMMAND_WITH_ID";
        case CEC_OPCODE_CLEAR_EXTERNAL_TIMER: return "CLEAR_EXTERNAL_TIMER";
        case CEC_OPCODE_SET_EXTERNAL_TIMER: return "SET_EXTERNAL_TIMER";
        case CEC_OPCODE_REPORT_SHORT_AUDIO_DESCRIPTOR: return "REPORT_SHORT_AUDIO_DESCRIPTOR";
        case CEC_OPCODE_REQUEST_SHORT_AUDIO_DESCRIPTOR: return "REQUEST_SHORT_AUDIO_DESCRIPTOR";
        case CEC_OPCODE_INITIATE_ARC: return "INITIATE_ARC";
        case CEC_OPCODE_REPORT_ARC_INITIATED: return "REPORT_ARC_INITIATED";
        case CEC_OPCODE_REPORT_ARC_TERMINATED: return "REPORT_ARC_TERMINATED";
        case CEC_OPCODE_REQUEST_ARC_INITIATION: return "REQUEST_ARC_INITIATION";
        case CEC_OPCODE_REQUEST_ARC_TERMINATION: return "REQUEST_ARC_TERMINATION";
        case CEC_OPCODE_TERMINATE_ARC: return "TERMINATE_ARC";
        case CEC_OPCODE_CDC_MESSAGE: return "CDC_MESSAGE";
        case CEC_OPCODE_ABORT: return "ABORT";
        default: return "UNKNOWN_OPCODE";
    }
}

static const char* cec_user_control_name(uint8_t control_code)
{
    switch (control_code) {
        case CEC_USER_CONTROL_SELECT: return "SELECT";
        case CEC_USER_CONTROL_UP: return "UP";
        case CEC_USER_CONTROL_DOWN: return "DOWN";
        case CEC_USER_CONTROL_LEFT: return "LEFT";
        case CEC_USER_CONTROL_RIGHT: return "RIGHT";
        case CEC_USER_CONTROL_RIGHT_UP: return "RIGHT_UP";
        case CEC_USER_CONTROL_RIGHT_DOWN: return "RIGHT_DOWN";
        case CEC_USER_CONTROL_LEFT_UP: return "LEFT_UP";
        case CEC_USER_CONTROL_LEFT_DOWN: return "LEFT_DOWN";
        case CEC_USER_CONTROL_ROOT_MENU: return "ROOT_MENU";
        case CEC_USER_CONTROL_SETUP_MENU: return "SETUP_MENU";
        case CEC_USER_CONTROL_CONTENTS_MENU: return "CONTENTS_MENU";
        case CEC_USER_CONTROL_FAVORITE_MENU: return "FAVORITE_MENU";
        case CEC_USER_CONTROL_EXIT: return "EXIT";
        case CEC_USER_CONTROL_VOLUME_UP: return "VOLUME_UP";
        case CEC_USER_CONTROL_VOLUME_DOWN: return "VOLUME_DOWN";
        case CEC_USER_CONTROL_MUTE: return "MUTE";
        case CEC_USER_CONTROL_PLAY: return "PLAY";
        case CEC_USER_CONTROL_STOP: return "STOP";
        case CEC_USER_CONTROL_PAUSE: return "PAUSE";
        case CEC_USER_CONTROL_RECORD: return "RECORD";
        case CEC_USER_CONTROL_REWIND: return "REWIND";
        case CEC_USER_CONTROL_FAST_FORWARD: return "FAST_FORWARD";
        case CEC_USER_CONTROL_EJECT: return "EJECT";
        case CEC_USER_CONTROL_FORWARD: return "FORWARD";
        case CEC_USER_CONTROL_BACKWARD: return "BACKWARD";
        case CEC_USER_CONTROL_POWER: return "POWER";
        default: return "UNKNOWN_CONTROL";
    }
}

static const char* cec_device_type_name(uint8_t device_type)
{
    switch (device_type) {
        case 0: return "TV";
        case 1: return "Recording Device";
        case 2: return "Reserved";
        case 3: return "Tuner";
        case 4: return "Playback Device";
        case 5: return "Audio System";
        case 6: return "Pure CEC Switch";
        case 7: return "Video Processor";
        default: return "Unknown Device";
    }
}

static void cec_debug_message(const char* direction, const cec_message_t *msg)
{
    if (!msg) return;
    
    uint8_t source = (msg->header >> 4) & 0x0F;
    uint8_t dest = msg->header & 0x0F;
    
    printf("CEC %s: [%X->%X] ", direction, source, dest);
    
    if (msg->length == 1) {
        printf("POLL\n");
        return;
    }
    
    printf("%s(0x%02X)", cec_opcode_name(msg->opcode), msg->opcode);
    
    // Add specific parameter decoding for common commands
    switch (msg->opcode) {
        case CEC_OPCODE_USER_CONTROL_PRESSED:
            if (msg->length >= 3) {
                printf(" - %s(0x%02X)", cec_user_control_name(msg->data[0]), msg->data[0]);
            }
            break;
            
        case CEC_OPCODE_REPORT_PHYSICAL_ADDRESS:
            if (msg->length >= 5) {
                uint16_t addr = (msg->data[0] << 8) | msg->data[1];
                printf(" - Addr:%d.%d.%d.%d Type:%s", 
                    (addr >> 12) & 0xF, (addr >> 8) & 0xF, 
                    (addr >> 4) & 0xF, addr & 0xF,
                    cec_device_type_name(msg->data[2]));
            }
            break;
            
        case CEC_OPCODE_ACTIVE_SOURCE:
        case CEC_OPCODE_SET_STREAM_PATH:
            if (msg->length >= 4) {
                uint16_t addr = (msg->data[0] << 8) | msg->data[1];
                printf(" - Addr:%d.%d.%d.%d", 
                    (addr >> 12) & 0xF, (addr >> 8) & 0xF, 
                    (addr >> 4) & 0xF, addr & 0xF);
            }
            break;
            
        case CEC_OPCODE_DEVICE_VENDOR_ID:
            if (msg->length >= 5) {
                uint32_t vendor = (msg->data[0] << 16) | (msg->data[1] << 8) | msg->data[2];
                printf(" - Vendor:0x%06X", vendor);
            }
            break;
            
        case CEC_OPCODE_SET_OSD_NAME:
            if (msg->length > 2) {
                printf(" - Name:");
                for (int i = 0; i < msg->length - 2 && i < 14; i++) {
                    printf("%c", msg->data[i]);
                }
            }
            break;
            
        case CEC_OPCODE_REPORT_POWER_STATUS:
            if (msg->length >= 3) {
                const char* power_status[] = {"ON", "STANDBY", "STANDBY->ON", "ON->STANDBY"};
                uint8_t status = msg->data[0];
                printf(" - %s(%d)", status < 4 ? power_status[status] : "UNKNOWN", status);
            }
            break;
            
        case CEC_OPCODE_CEC_VERSION:
            if (msg->length >= 3) {
                printf(" - Version:1.%d", msg->data[0] - 4);
            }
            break;
            
        case CEC_OPCODE_MENU_REQUEST:
            if (msg->length >= 3) {
                const char* menu_req[] = {"Activate", "Deactivate", "Query"};
                uint8_t req = msg->data[0];
                printf(" - %s(%d)", req < 3 ? menu_req[req] : "UNKNOWN", req);
            }
            break;
            
        case CEC_OPCODE_MENU_STATUS:
            if (msg->length >= 3) {
                const char* menu_stat[] = {"Activated", "Deactivated"};
                uint8_t stat = msg->data[0];
                printf(" - %s(%d)", stat < 2 ? menu_stat[stat] : "UNKNOWN", stat);
            }
            break;
    }
    
    // Show raw data if there are additional parameters
    if (msg->length > 2) {
        printf(" Data:[");
        for (int i = 0; i < msg->length - 2 && i < 14; i++) {
            printf("%02X", msg->data[i]);
            if (i < msg->length - 3) printf(" ");
        }
        printf("]");
    }
    
    printf(" Len:%d\n", msg->length);
}