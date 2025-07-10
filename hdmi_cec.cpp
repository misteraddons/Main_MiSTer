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
// CEC requires 750kHz clock from 12MHz input
// Clock divider = Input Clock / Output Clock - 1
// For 12MHz: Divide by 16 to get 750kHz CEC clock
// Clock divider value = 15 (0x0F) -> Division = 16
// 12MHz / 16 = 750kHz
#define CEC_CLOCK_DIV_12MHZ 0x0F

static bool cec_write_register(uint8_t reg, uint8_t value)
{
    if (cec_fd < 0) {
        printf("CEC: Write register 0x%02X failed - invalid file descriptor %d\n", reg, cec_fd);
        return false;
    }
    int res = i2c_smbus_write_byte_data(cec_fd, reg, value);
    if (res < 0) {
        printf("CEC: Failed to write register 0x%02X (value 0x%02X) - i2c error %d\n", reg, value, res);
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

    // Deinitialize first if already initialized to avoid conflicts
    if (cec_enabled || cec_fd >= 0) {
        printf("CEC INIT: Deinitializing previous CEC instance\n");
        cec_deinit();
        usleep(100000); // Wait 100ms for hardware to settle
    }

    printf("CEC INIT: Starting STEP-BY-STEP ADV7513 CEC debug procedure\n");
    
    printf("\n=== 2.1 Power & Device ID ===\n");
    
    // Open main ADV7513 device first
    int main_fd = i2c_open(0x39, 0);
    if (main_fd < 0) {
        printf("CEC INIT: Failed to open main ADV7513 device\n");
        return false;
    }
    
    // Step 2: Check device ID (0xF5/0xF6 must return 0x75/0x11)
    int chip_id1 = i2c_smbus_read_byte_data(main_fd, 0xF5);
    int chip_id2 = i2c_smbus_read_byte_data(main_fd, 0xF6);
    printf("Step 2: Device ID - 0xF5=0x%02X, 0xF6=0x%02X (should be 0x75, 0x11)\n", chip_id1, chip_id2);
    
    // Step 3: Check and ensure power state (0x41 bit 6 should be 0 = powered)
    int reg41 = i2c_smbus_read_byte_data(main_fd, 0x41);
    printf("Step 3: Power state - 0x41=0x%02X (bit 6=%d, should be 0=powered)\n", 
           reg41, (reg41 >> 6) & 1);
    
    // Step 3b: Force enable CEC power domain if needed
    if (reg41 & 0x40) {
        printf("Step 3b: Enabling CEC power domain - clearing 0x41[6]\n");
        i2c_smbus_write_byte_data(main_fd, 0x41, reg41 & ~0x40);
        usleep(5000); // 5ms delay for power stabilization
        reg41 = i2c_smbus_read_byte_data(main_fd, 0x41);
        printf("Step 3b: Power state after enable - 0x41=0x%02X\n", reg41);
    }
    
    printf("\n=== 2.2 Cable Detect ===\n");
    
    // Step 5: Check HPD and RxSense from INT_STATUS register 0x42
    int int42_initial = i2c_smbus_read_byte_data(main_fd, 0x42);
    bool hpd_initial = int42_initial & 0x40;   // bit 6
    bool rsen_initial = int42_initial & 0x20;  // bit 5
    printf("Step 5-6: Cable detect - INT_STATUS 0x42=0x%02X (HPD bit6=%d, RxSense bit5=%d)\n", 
           int42_initial, hpd_initial ? 1 : 0, rsen_initial ? 1 : 0);
    
    // Step 5b: Apply friend's exact override sequence
    printf("Step 5b: Applying complete override sequence\n");
    
    // 1. Force HPD high
    printf("Step 5b1: Setting 0xD6=0xC0 (HPD always high)\n");
    i2c_smbus_write_byte_data(main_fd, 0xD6, 0xC0);
    
    // 2. Force RxSense high AND HDMI mode
    printf("Step 5b2: Setting 0xAF=0x44 (RxSense force high + HDMI mode)\n");
    i2c_smbus_write_byte_data(main_fd, 0xAF, 0x44);
    
    // 3. Disable monitor sense power-down
    printf("Step 5b3: Setting 0xA1 bit 6 (disable monitor sense power-down)\n");
    i2c_smbus_write_byte_data(main_fd, 0xA1, 0x40);
    
    // 4. Clear INT to latch new sense
    printf("Step 5b4: Reading INT_STATUS to latch new sense\n");
    int int_status = i2c_smbus_read_byte_data(main_fd, 0x42);
    printf("Step 5b4: INT_STATUS 0x42=0x%02X\n", int_status);
    
    usleep(20000); // 20ms delay
    
    // Verify overrides are working - check INT_STATUS 0x42 for actual HPD/RxSense
    int int42_after = i2c_smbus_read_byte_data(main_fd, 0x42);
    bool hpd_after = int42_after & 0x40;   // bit 6
    bool rsen_after = int42_after & 0x20;  // bit 5
    int reg96_after = i2c_smbus_read_byte_data(main_fd, 0x96);
    int regAF = i2c_smbus_read_byte_data(main_fd, 0xAF);
    int regA1 = i2c_smbus_read_byte_data(main_fd, 0xA1);
    int regD6 = i2c_smbus_read_byte_data(main_fd, 0xD6);
    
    printf("Step 5b5: After overrides:\n");
    printf("  INT_STATUS 0x42=0x%02X (HPD bit6=%d, RxSense bit5=%d) - ACTUAL STATUS\n", 
           int42_after, hpd_after ? 1 : 0, rsen_after ? 1 : 0);
    printf("  0x96=0x%02X (CEC clock bit5=%d)\n", reg96_after, (reg96_after >> 5) & 1);
    printf("  Override regs: 0xAF=0x%02X, 0xA1=0x%02X, 0xD6=0x%02X\n", regAF, regA1, regD6);
    
    printf("\n=== 2.3 CEC Map Visibility ===\n");
    
    // Step 7: Check CEC I2C address setting
    int regE1 = i2c_smbus_read_byte_data(main_fd, 0xE1);
    printf("Step 7: CEC I2C address - 0xE1=0x%02X (default 0x78, 7-bit=0x3C)\n", regE1);
    
    // Open I2C device for CEC using address from 0xE1
    cec_fd = i2c_open(CEC_I2C_ADDR, 0);
    if (cec_fd < 0) {
        printf("CEC INIT: Failed to open I2C device at address 0x%02X\n", CEC_I2C_ADDR);
        i2c_close(main_fd);
        return false;
    }
    
    // Step 8: Dump CEC map 0x00-0x0F to confirm visibility
    printf("Step 8: CEC map dump 0x00-0x0F (non-zero = map visible):\n");
    for (int i = 0; i <= 0x0F; i++) {
        uint8_t val = cec_read_register(i);
        printf("  CEC[0x%02X] = 0x%02X\n", i, val);
    }
    
    printf("\n=== 2.4 Power-up CEC Engine ===\n");
    
    // Step 9: Enable CEC engine in 0xE2 (try force-enable mode to bypass HPD)
    int regE2_before = i2c_smbus_read_byte_data(main_fd, 0xE2);
    printf("Step 9: CEC control - 0xE2=0x%02X (bit 0=%d, should be 0=powered)\n", 
           regE2_before, regE2_before & 1);
    
    // Step 9: Power-up CEC before using it (friend's recommended method)
    printf("Step 9: Powering up CEC engine\n");
    uint8_t e2 = i2c_smbus_read_byte_data(main_fd, 0xE2);
    e2 |= 0x03;  // bit0 = CEC_PDN_N (power up), bit1 = CEC_HPD_BYPASS
    printf("Step 9: Writing 0xE2=0x%02X (power + bypass)\n", e2);
    i2c_smbus_write_byte_data(main_fd, 0xE2, e2);
    usleep(5000); // 5ms delay for CEC engine to power up
    
    int regE2_after = i2c_smbus_read_byte_data(main_fd, 0xE2);
    printf("Step 9: After enable - 0xE2=0x%02X (bit0=%d PDN_N, bit1=%d HPD_BYPASS)\n", 
           regE2_after, regE2_after & 1, (regE2_after >> 1) & 1);
    
    usleep(5000); // 5ms delay for CEC engine to power up
    
    printf("\n=== 2.5 Enable & Reset Block ===\n");
    
    // Step 10: Enable CEC clock first (some ADV7513 variants require this order)
    int reg96_before = i2c_smbus_read_byte_data(main_fd, 0x96);
    printf("Step 10: Before CEC clock enable - 0x96=0x%02X (bit 5=%d)\n", 
           reg96_before, (reg96_before >> 5) & 1);
    
    int reg96_new = reg96_before | 0x20; // Set bit 5
    printf("Step 10: Setting CEC clock enable bit - writing 0x%02X\n", reg96_new);
    i2c_smbus_write_byte_data(main_fd, 0x96, reg96_new);
    
    // Step 11: Program clock divider AFTER enabling clock
    printf("Step 11: Programming clock divider for 12MHz → 750kHz\n");
    
    // Apply comprehensive CEC timing registers
    printf("Step 11a: Applying comprehensive CEC timing configuration\n");
    
    // Structure for register settings
    struct {
        uint8_t addr;
        uint8_t value;
        const char* desc;
    } cec_timing_regs[] = {
        // Clock divider (bits 7:2 = 0x0F, keep bits 1:0)
        // Note: We'll handle 0x4E specially to preserve bits 1:0
        
        // CEC timing registers
        {0x51, 0x0D, "CEC timing 1"},
        {0x52, 0x2F, "CEC timing 2"},
        {0x53, 0x0C, "CEC timing 3"},
        {0x54, 0x4E, "CEC timing 4"},
        {0x55, 0x0E, "CEC timing 5"},
        {0x56, 0x10, "CEC timing 6"},
        {0x57, 0x0A, "CEC timing 7"},
        {0x58, 0xD7, "CEC timing 8"},
        {0x59, 0x09, "CEC timing 9"},
        {0x5A, 0xF6, "CEC timing 10"},
        {0x5B, 0x0B, "CEC timing 11"},
        {0x5C, 0xB8, "CEC timing 12"},
        {0x5D, 0x07, "CEC timing 13"},
        {0x5E, 0x08, "CEC timing 14"},
        {0x5F, 0x05, "CEC timing 15"},
        {0x60, 0xB7, "CEC timing 16"},
        {0x61, 0x08, "CEC timing 17"},
        {0x62, 0x5A, "CEC timing 18"},
        {0x63, 0x01, "CEC timing 19"},
        {0x64, 0xC2, "CEC timing 20"},
        {0x65, 0x04, "CEC timing 21"},
        {0x66, 0x65, "CEC timing 22"},
        {0x67, 0x05, "CEC timing 23"},
        {0x68, 0x46, "CEC timing 24"},
        {0x69, 0x03, "CEC timing 25"},
        {0x6A, 0x14, "CEC timing 26"},
        {0x6B, 0x0A, "CEC timing 27"},
        {0x6C, 0x8C, "CEC timing 28"},
        {0x6E, 0x00, "CEC timing 29"},
        {0x6F, 0xBC, "CEC timing 30"},
        {0x71, 0x00, "CEC timing 31"},
        {0x72, 0xE1, "CEC timing 32"},
        {0x73, 0x02, "CEC timing 33"},
        {0x74, 0xA3, "CEC timing 34"},
        {0x75, 0x03, "CEC timing 35"},
        {0x76, 0x84, "CEC timing 36"},
    };
    
    // Handle register 0x4E - CEC Clock Divider and Power Mode
    // Bits 7:2 = Clock divider (0x0F for divide-by-16)
    // Bits 1:0 = Power mode:
    //   00 = Completely Power Down
    //   01 = Always Active
    //   10 = Depend on HPD status
    //   11 = Depend on HPD status
    uint8_t reg4E_current = cec_read_register(0x4E);
    uint8_t cec_power_mode = 0x01;  // Always Active - most reliable for CEC
    uint8_t reg4E_new = (cec_power_mode & 0x03) | (0x0F << 2);  // Power mode in bits 1:0, divider in bits 7:2
    printf("  CEC[0x4E]: Current=0x%02X (power mode=%d), New=0x%02X (power=Always Active, divider=15)\n", 
           reg4E_current, (reg4E_current & 0x03), reg4E_new);
    cec_write_register(0x4E, reg4E_new);
    uint8_t reg4E_verify = cec_read_register(0x4E);
    if (reg4E_verify != reg4E_new) {
        printf("  CEC[0x4E]: wrote 0x%02X, read 0x%02X - MISMATCH - Clock divider\n", 
               reg4E_new, reg4E_verify);
    } else {
        printf("  CEC[0x4E]: 0x%02X - OK - Clock divider\n", reg4E_verify);
    }
    
    // Apply all other timing registers
    for (int i = 0; i < sizeof(cec_timing_regs)/sizeof(cec_timing_regs[0]); i++) {
        cec_write_register(cec_timing_regs[i].addr, cec_timing_regs[i].value);
        uint8_t verify = cec_read_register(cec_timing_regs[i].addr);
        if (verify != cec_timing_regs[i].value) {
            printf("  CEC[0x%02X]: wrote 0x%02X, read 0x%02X - MISMATCH - %s\n", 
                   cec_timing_regs[i].addr, cec_timing_regs[i].value, verify, cec_timing_regs[i].desc);
        } else if (i < 5 || i == sizeof(cec_timing_regs)/sizeof(cec_timing_regs[0])-1) {
            // Show first few and last for brevity
            printf("  CEC[0x%02X]: 0x%02X - OK - %s\n", 
                   cec_timing_regs[i].addr, verify, cec_timing_regs[i].desc);
        } else if (i == 5) {
            printf("  ... (applying remaining timing registers) ...\n");
        }
    }
    
    printf("Step 11b: CEC timing configuration complete\n");
    
    // Step 12: Wait for clock to stabilize and verify enable bit
    printf("Step 12: Waiting for CEC clock to stabilize (20ms)...\n");
    usleep(20000); // 20ms delay for clock stability
    
    // Verify clock enable bit after divider setup
    int reg96_immediate = i2c_smbus_read_byte_data(main_fd, 0x96);
    printf("Step 12: After divider setup - 0x96=0x%02X (bit 5=%d)\n", 
           reg96_immediate, (reg96_immediate >> 5) & 1);
    
    if (!(reg96_immediate & 0x20)) {
        printf("*** STEP 12 FAILED: CEC clock bit cleared after divider! ***\n");
        printf("*** This indicates: clock missing or divider wrong ***\n");
        // Continue for more diagnostics
    } else {
        printf("Step 12: SUCCESS - CEC clock bit latched properly\n");
    }
    
    // Wait and test again
    usleep(10000); // 10ms
    int reg96_delayed = i2c_smbus_read_byte_data(main_fd, 0x96);
    printf("Step 12: After final delay - 0x96=0x%02X (bit 5=%d)\n", 
           reg96_delayed, (reg96_delayed >> 5) & 1);
    
    if (!(reg96_delayed & 0x20)) {
        printf("*** STEP 12 FAILED: CEC clock bit cleared after delay! ***\n");
        printf("*** This indicates: HPD went low or other power issue ***\n");
    }
    
    // Step 13: Soft reset CEC state machine (per ADI procedure 2.5)
    printf("Step 13: Soft-resetting CEC state machine (ADI procedure 2.5)\n");
    cec_write_register(CEC_SOFT_RESET, 0x01);
    printf("Step 13: Reset pulse high - CEC[0x50]=0x01\n");
    usleep(2000); // 2ms
    cec_write_register(CEC_SOFT_RESET, 0x00);
    printf("Step 13: Reset pulse low - CEC[0x50]=0x00\n");
    usleep(10000); // 10ms delay for CEC state machine to stabilize
    
    // Step 13b: Re-apply ALL overrides after soft reset (they may have been cleared)
    printf("Step 13b: Re-applying all overrides after soft reset\n");
    i2c_smbus_write_byte_data(main_fd, 0xD6, 0xC0);  // HPD override
    i2c_smbus_write_byte_data(main_fd, 0xAF, 0x44);  // RxSense override + HDMI mode
    i2c_smbus_write_byte_data(main_fd, 0xA1, 0x40);  // Disable monitor sense power-down
    int int_status2 = i2c_smbus_read_byte_data(main_fd, 0x42);
    printf("Step 13b: Read INT_STATUS 0x42=0x%02X to latch HPD state\n", int_status2);
    usleep(20000); // 20ms delay for overrides to take effect
    
    // Verify clock, divider, and all overrides after reset
    int int42_after_reset = i2c_smbus_read_byte_data(main_fd, 0x42);
    bool hpd_reset = int42_after_reset & 0x40;   // bit 6
    bool rsen_reset = int42_after_reset & 0x20;  // bit 5
    int reg96_after_reset = i2c_smbus_read_byte_data(main_fd, 0x96);
    uint8_t clk_after_reset = cec_read_register(CEC_CLK_DIV);
    int regD6_after_reset = i2c_smbus_read_byte_data(main_fd, 0xD6);
    int regAF_after_reset = i2c_smbus_read_byte_data(main_fd, 0xAF);
    int regA1_after_reset = i2c_smbus_read_byte_data(main_fd, 0xA1);
    
    printf("Step 13b: After reset - INT_STATUS 0x42=0x%02X (HPD=%d, RxSense=%d)\n", 
           int42_after_reset, hpd_reset ? 1 : 0, rsen_reset ? 1 : 0);
    printf("Step 13b: 0x96=0x%02X (CEC_CLK bit5=%d)\n", 
           reg96_after_reset, (reg96_after_reset >> 5) & 1);
    printf("Step 13b: Override regs - 0xD6=0x%02X, 0xAF=0x%02X, 0xA1=0x%02X, CLK_DIV=0x%02X\n", 
           regD6_after_reset, regAF_after_reset, regA1_after_reset, clk_after_reset);
    
    printf("\n=== REGISTER DUMP BEFORE LOGICAL ADDRESS SETUP ===\n");
    printf("Main map key registers:\n");
    printf("  0x41 (Power): 0x%02X\n", i2c_smbus_read_byte_data(main_fd, 0x41));
    printf("  0x94 (Int Mask): 0x%02X\n", i2c_smbus_read_byte_data(main_fd, 0x94));
    printf("  0x96 (Status): 0x%02X\n", i2c_smbus_read_byte_data(main_fd, 0x96));
    printf("  0x97 (Int Status): 0x%02X\n", i2c_smbus_read_byte_data(main_fd, 0x97));
    printf("  0xE1 (CEC I2C): 0x%02X\n", i2c_smbus_read_byte_data(main_fd, 0xE1));
    printf("  0xE2 (CEC Control): 0x%02X\n", i2c_smbus_read_byte_data(main_fd, 0xE2));
    
    printf("CEC map key registers:\n");
    printf("  0x4E (Clock Div): 0x%02X\n", cec_read_register(0x4E));
    printf("  0x50 (Soft Reset): 0x%02X\n", cec_read_register(0x50));
    printf("  0x26 (RX Enable): 0x%02X\n", cec_read_register(0x26));
    printf("  0x11 (TX Enable): 0x%02X\n", cec_read_register(0x11));
    printf("  0x12 (TX Retry): 0x%02X\n", cec_read_register(0x12));
    printf("  0x27 (Logical Addr): 0x%02X\n", cec_read_register(0x27));
    printf("  0x2A (Addr Mask): 0x%02X\n", cec_read_register(0x2A));
    printf("  0x40 (CEC Int Mask): 0x%02X\n", cec_read_register(0x40));
    printf("  0x41 (CEC Int Status): 0x%02X\n", cec_read_register(0x41));
    
    printf("\n=== 2.6 Logical Address & Path Setup ===\n");
    
    // Step 14: Clear logical address first, then set it (ADI procedure)
    printf("Step 14a: Clearing logical address register first\n");
    cec_write_register(CEC_LOGICAL_ADDR0, 0x00); // Clear first
    usleep(2000);
    
    // Step 14: Configure logical address and verify it sticks (per ADI procedure 2.6)
    printf("Step 14: Setting logical address to %d (Playback Device)\n", cec_logical_addr);
    uint8_t logical_addr_val = cec_logical_addr | 0x10; // Upper nibble = 1 to enable
    printf("Step 14: Writing 0x%02X to CEC[0x27] (addr=%d, enable=1)\n", 
           logical_addr_val, cec_logical_addr);
    
    // Try setting logical address multiple times with delays
    bool addr_success = false;
    for (int retry = 0; retry < 5; retry++) {
        cec_write_register(CEC_LOGICAL_ADDR0, logical_addr_val);
        usleep(5000); // 5ms delay between attempts
        uint8_t addr_readback = cec_read_register(CEC_LOGICAL_ADDR0);
        printf("Step 14: Attempt %d - Readback CEC[0x27]=0x%02X (should be 0x%02X)\n", 
               retry + 1, addr_readback, logical_addr_val);
        
        if (addr_readback == logical_addr_val) {
            printf("Step 14: SUCCESS - Logical address set on attempt %d\n", retry + 1);
            addr_success = true;
            break;
        }
    }
    
    if (!addr_success) {
        printf("*** STEP 14 FAILED: Logical address register didn't stick after 5 attempts! ***\n");
        printf("*** This indicates: CEC engine not responding to writes ***\n");
    }
    
    // Set logical address mask
    printf("Step 14: Setting logical address mask - CEC[0x2A]=0x01\n");
    cec_write_register(CEC_LOGICAL_ADDR_MASK, 0x01);
    uint8_t mask_readback = cec_read_register(CEC_LOGICAL_ADDR_MASK);
    printf("Step 14: Readback CEC[0x2A]=0x%02X (should be 0x01)\n", mask_readback);
    
    // Step 15: Enable RX and TX (try bit-wise approach for RX register)
    printf("Step 15: Reading current RX register value\n");
    uint8_t rx_current = cec_read_register(CEC_RX_ENABLE_REG);
    printf("Step 15: Current RX=0x%02X, enabling receive with OR operation\n", rx_current);
    
    printf("Step 15: Setting TX=0x01, Retry=0x03 per ADI procedure\n");
    cec_write_register(CEC_RX_ENABLE_REG, rx_current | 0x01);  // OR with enable bit
    cec_write_register(CEC_TX_ENABLE_REG, 0x01);  // ADI: "and TX (0x11 = 0x01)"
    cec_write_register(CEC_TX_RETRY, 0x03);       // ADI: "retry count 0x03"
    
    uint8_t rx_readback = cec_read_register(CEC_RX_ENABLE_REG);
    uint8_t tx_readback = cec_read_register(CEC_TX_ENABLE_REG);
    uint8_t retry_readback = cec_read_register(CEC_TX_RETRY);
    
    printf("Step 15: Readback - RX=0x%02X, TX=0x%02X, Retry=0x%02X\n", 
           rx_readback, tx_readback, retry_readback);
    
    printf("\n=== 2.7 Final Register Verification ===\n");
    
    // Check interrupt status registers
    int reg94 = i2c_smbus_read_byte_data(main_fd, 0x94); // Interrupt mask
    int reg97 = i2c_smbus_read_byte_data(main_fd, 0x97); // Interrupt status
    printf("Step 16: Interrupt registers - mask[0x94]=0x%02X, status[0x97]=0x%02X\n", reg94, reg97);
    
    // Final verification: Check if 0x96[5] is still set
    int reg96_final = i2c_smbus_read_byte_data(main_fd, 0x96);
    printf("Final check: 0x96=0x%02X (CEC clock bit 5=%d)\n", 
           reg96_final, (reg96_final >> 5) & 1);
    
    // Final CEC map verification
    printf("Final CEC map verification:\n");
    printf("  CEC[0x4E] Clock Div = 0x%02X\n", cec_read_register(CEC_CLK_DIV));
    printf("  CEC[0x27] Logical Addr = 0x%02X\n", cec_read_register(CEC_LOGICAL_ADDR0));
    printf("  CEC[0x2A] Addr Mask = 0x%02X\n", cec_read_register(CEC_LOGICAL_ADDR_MASK));
    printf("  CEC[0x26] RX Enable = 0x%02X\n", cec_read_register(CEC_RX_ENABLE_REG));
    printf("  CEC[0x11] TX Enable = 0x%02X\n", cec_read_register(CEC_TX_ENABLE_REG));
    printf("  CEC[0x12] TX Retry = 0x%02X\n", cec_read_register(CEC_TX_RETRY));
    
    // Summary diagnosis
    printf("\n=== DIAGNOSIS SUMMARY ===\n");
    if (chip_id1 == 0x75 && chip_id2 == 0x11) {
        printf("✓ Device ID: ADV7513 detected correctly\n");
    } else {
        printf("✗ Device ID: Wrong chip (0x%02X, 0x%02X)\n", chip_id1, chip_id2);
    }
    
    if (!(reg41 & 0x40)) {
        printf("✓ Power: Chip powered up correctly\n");
    } else {
        printf("✗ Power: Chip in power-down mode\n");
    }
    
    if (reg96_final & 0x20) {
        printf("✓ CEC Clock: Bit 5 latched - clock working\n");
    } else {
        printf("✗ CEC Clock: Bit 5 cleared - clock/divider problem\n");
    }
    
    if (addr_success) {
        printf("✓ CEC Registers: Logical address stuck correctly\n");
    } else {
        printf("✗ CEC Registers: Values not sticking - clock problem\n");
    }
    
    cec_enabled = true;
    i2c_close(main_fd);
    
    printf("\n=== CEC INITIALIZATION COMPLETE ===\n");
    printf("CEC initialization completed with comprehensive diagnostics\n");
    printf("Check the step-by-step results above to identify any issues\n");
    
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

    // Clear any pending interrupts first
    cec_write_register(CEC_INT_CLEAR, 0x7F);
    
    // Debug: Check various registers before attempting transmission
    uint8_t initial_status = cec_read_register(CEC_INT_STATUS);
    printf("CEC DEBUG: Initial status before TX: 0x%02X\n", initial_status);
    
    // Try different approach: Set up frame first, then enable TX
    // Some CEC controllers need the frame setup before TX_RDY is valid

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
    
    // Now check if TX ready becomes available after frame setup
    printf("CEC DEBUG: Frame setup complete, checking TX ready\n");
    uint8_t status_after_setup = cec_read_register(CEC_INT_STATUS);
    printf("CEC DEBUG: Status after frame setup: 0x%02X\n", status_after_setup);
    
    // If TX_RDY is not set, try waiting a bit more
    if (!(status_after_setup & CEC_INT_TX_RDY)) {
        printf("CEC DEBUG: TX not ready after frame setup, waiting...\n");
        int wait_timeout = 100; // 100ms
        while (wait_timeout-- > 0) {
            uint8_t status = cec_read_register(CEC_INT_STATUS);
            if (status & CEC_INT_TX_RDY) {
                printf("CEC DEBUG: TX ready detected after frame setup + %d ms\n", 100 - wait_timeout);
                break;
            }
            usleep(1000); // 1ms
        }
    }
    
    // Final check and proceed with transmission
    uint8_t final_status = cec_read_register(CEC_INT_STATUS);
    if (!(final_status & CEC_INT_TX_RDY)) {
        printf("CEC DEBUG: TX_RDY never set (final status: 0x%02X), attempting transmission anyway\n", final_status);
    } else {
        printf("CEC DEBUG: TX_RDY confirmed, proceeding with transmission\n");
        // Clear the TX_RDY flag since we're about to use it
        cec_write_register(CEC_INT_CLEAR, CEC_INT_TX_RDY);
    }
    
    // Enable transmission
    cec_write_register(CEC_TX_ENABLE_REG, 0x01);
    
    // Wait for transmission complete
    int timeout = 200; // 200ms timeout
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

bool cec_send_image_view_on(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
    msg.opcode = CEC_OPCODE_IMAGE_VIEW_ON;
    msg.length = 2;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Image View On sent successfully\n");
        return true;
    } else {
        printf("CEC CMD: Failed to send Image View On\n");
        return false;
    }
}

bool cec_send_active_source(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
    msg.opcode = CEC_OPCODE_ACTIVE_SOURCE;
    msg.data[0] = (cec_physical_addr >> 8) & 0xFF;
    msg.data[1] = cec_physical_addr & 0xFF;
    msg.length = 4;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Active Source sent successfully\n");
        return true;
    } else {
        printf("CEC CMD: Failed to send Active Source\n");
        return false;
    }
}

bool cec_send_standby(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
    msg.opcode = CEC_OPCODE_STANDBY;
    msg.length = 2;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Standby sent successfully\n");
        return true;
    } else {
        printf("CEC CMD: Failed to send Standby\n");
        return false;
    }
}

bool cec_send_report_physical_address(void)
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
        return true;
    } else {
        printf("CEC CMD: Failed to send Report Physical Address\n");
        return false;
    }
}

bool cec_send_device_vendor_id(void)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
    msg.opcode = CEC_OPCODE_DEVICE_VENDOR_ID;
    // Using generic vendor ID (0x000000)
    msg.data[0] = 0x00;
    msg.data[1] = 0x00;
    msg.data[2] = 0x00;
    msg.length = 5;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Device Vendor ID sent successfully\n");
        return true;
    } else {
        printf("CEC CMD: Failed to send Device Vendor ID\n");
        return false;
    }
}

bool cec_send_cec_version(uint8_t destination)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_CEC_VERSION;
    msg.data[0] = 0x05; // CEC version 1.4
    msg.length = 3;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: CEC Version sent successfully to device %d\n", destination);
        return true;
    } else {
        printf("CEC CMD: Failed to send CEC Version to device %d\n", destination);
        return false;
    }
}

bool cec_send_set_osd_name(const char* name)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
    msg.opcode = CEC_OPCODE_SET_OSD_NAME;
    
    int name_len = strlen(name);
    if (name_len > 14) name_len = 14;
    
    memcpy(msg.data, name, name_len);
    msg.length = 2 + name_len;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: OSD Name '%s' sent successfully\n", name);
        return true;
    } else {
        printf("CEC CMD: Failed to send OSD Name '%s'\n", name);
        return false;
    }
}

bool cec_send_menu_status(uint8_t destination, uint8_t status)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_MENU_STATUS;
    msg.data[0] = status; // 0 = activated, 1 = deactivated
    msg.length = 3;
    if (cec_send_message(&msg)) {
        printf("CEC CMD: Menu Status (%s) sent successfully to device %d\n", 
               status ? "deactivated" : "activated", destination);
        return true;
    } else {
        printf("CEC CMD: Failed to send Menu Status to device %d\n", destination);
        return false;
    }
}

bool cec_send_user_control_pressed(uint8_t destination, uint8_t control_code)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_USER_CONTROL_PRESSED;
    msg.data[0] = control_code;
    msg.length = 3;
    if (cec_send_message(&msg)) {
        return true;
    } else {
        printf("CEC CMD: Failed to send User Control Pressed (0x%02X) to device %d\n", 
               control_code, destination);
        return false;
    }
}

bool cec_send_user_control_released(uint8_t destination)
{
    cec_message_t msg;
    msg.header = (cec_logical_addr << 4) | destination;
    msg.opcode = CEC_OPCODE_USER_CONTROL_RELEASED;
    msg.length = 2;
    if (cec_send_message(&msg)) {
        return true;
    } else {
        printf("CEC CMD: Failed to send User Control Released to device %d\n", destination);
        return false;
    }
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