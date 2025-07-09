#include "nfc_reader.h"
#include "smbus.h"
#include "cmd_bridge.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Global NFC state
static nfc_config_t g_nfc_config = {0};
static bool g_nfc_initialized = false;
static bool g_polling_enabled = false;
static nfc_tag_data_t g_last_tag = {0};
static clock_t g_last_poll_time = 0;
static nfc_tag_callback_t g_tag_callback = NULL;

// PN532 Commands
#define PN532_COMMAND_GETFIRMWAREVERSION    0x02
#define PN532_COMMAND_SAMCONFIGURATION      0x14
#define PN532_COMMAND_INLISTPASSIVETARGETS  0x4A
#define PN532_COMMAND_INDATAEXCHANGE        0x40

// PN532 I2C frame structure
#define PN532_PREAMBLE                      0x00
#define PN532_STARTCODE1                    0x00
#define PN532_STARTCODE2                    0xFF
#define PN532_POSTAMBLE                     0x00

// I2C communication helper functions
static bool nfc_i2c_write_command(const uint8_t* command, uint8_t length)
{
    if (!g_nfc_initialized || g_nfc_config.module_type != NFC_MODULE_PN532) {
        return false;
    }
    
    // PN532 I2C frame: [PREAMBLE][START1][START2][LEN][LCS][DATA...][DCS][POSTAMBLE]
    uint8_t frame[64];
    uint8_t frame_len = 0;
    
    frame[frame_len++] = PN532_PREAMBLE;
    frame[frame_len++] = PN532_STARTCODE1;
    frame[frame_len++] = PN532_STARTCODE2;
    frame[frame_len++] = length + 1; // +1 for command code
    frame[frame_len++] = ~(length + 1) + 1; // LCS (Length CheckSum)
    frame[frame_len++] = 0xD4; // Host to PN532
    
    uint8_t checksum = 0xD4;
    for (int i = 0; i < length; i++) {
        frame[frame_len++] = command[i];
        checksum += command[i];
    }
    
    frame[frame_len++] = ~checksum + 1; // DCS (Data CheckSum)
    frame[frame_len++] = PN532_POSTAMBLE;
    
    return smbus_write_block_data(g_nfc_config.i2c_address, 0, frame, frame_len) >= 0;
}

static bool nfc_i2c_read_response(uint8_t* response, uint8_t max_length, uint8_t* actual_length)
{
    if (!g_nfc_initialized) {
        return false;
    }
    
    // Wait for ready status
    uint8_t status;
    int retries = 10;
    do {
        if (smbus_read_byte(g_nfc_config.i2c_address, &status) < 0) {
            return false;
        }
        if (status == 0x01) break; // Ready
        usleep(1000); // 1ms delay
    } while (--retries > 0);
    
    if (retries == 0) return false;
    
    // Read response frame
    uint8_t frame[64];
    int bytes_read = smbus_read_block_data(g_nfc_config.i2c_address, 0, frame, sizeof(frame));
    if (bytes_read < 6) return false;
    
    // Validate frame structure
    if (frame[0] != PN532_PREAMBLE || frame[1] != PN532_STARTCODE1 || frame[2] != PN532_STARTCODE2) {
        return false;
    }
    
    uint8_t len = frame[3];
    if (len > max_length || bytes_read < len + 6) return false;
    
    // Extract data (skip header and response code)
    *actual_length = len - 2; // -2 for response code bytes
    memcpy(response, &frame[6], *actual_length);
    
    return true;
}

// PN532 specific functions
static bool pn532_get_firmware_version(void)
{
    uint8_t command[] = {PN532_COMMAND_GETFIRMWAREVERSION};
    if (!nfc_i2c_write_command(command, sizeof(command))) {
        return false;
    }
    
    uint8_t response[16];
    uint8_t response_len;
    if (!nfc_i2c_read_response(response, sizeof(response), &response_len)) {
        return false;
    }
    
    if (response_len >= 4 && response[0] == 0xD5 && response[1] == PN532_COMMAND_GETFIRMWAREVERSION + 1) {
        printf("NFC: PN532 firmware version %d.%d\n", response[2], response[3]);
        return true;
    }
    
    return false;
}

static bool pn532_configure_sam(void)
{
    // Configure SAM (Secure Application Module)
    uint8_t command[] = {PN532_COMMAND_SAMCONFIGURATION, 0x01, 0x14, 0x01};
    if (!nfc_i2c_write_command(command, sizeof(command))) {
        return false;
    }
    
    uint8_t response[16];
    uint8_t response_len;
    return nfc_i2c_read_response(response, sizeof(response), &response_len);
}

// Forward declarations for PN532 functions
static bool pn532_write_ndef_text(const char* text_data, const char* language);
static bool pn532_write_ndef_data(const uint8_t* ndef_data, uint8_t ndef_len);
static bool pn532_write_page(uint8_t page, const uint8_t* data);
static bool pn532_format_tag(void);

static bool pn532_read_passive_target(nfc_tag_data_t* tag_data)
{
    // Read passive target (NFC/RFID tag)
    uint8_t command[] = {PN532_COMMAND_INLISTPASSIVETARGETS, 0x01, 0x00}; // ISO14443A
    if (!nfc_i2c_write_command(command, sizeof(command))) {
        return false;
    }
    
    uint8_t response[64];
    uint8_t response_len;
    if (!nfc_i2c_read_response(response, sizeof(response), &response_len)) {
        return false;
    }
    
    if (response_len < 4 || response[0] != 0xD5 || response[1] != PN532_COMMAND_INLISTPASSIVETARGETS + 1) {
        return false;
    }
    
    uint8_t num_targets = response[2];
    if (num_targets == 0) {
        return false; // No tags found
    }
    
    // Parse first target
    uint8_t target_data_pos = 4; // Skip response header and target number
    if (target_data_pos + 4 > response_len) return false;
    
    tag_data->uid_length = response[target_data_pos + 4];
    if (tag_data->uid_length > 16 || target_data_pos + 5 + tag_data->uid_length > response_len) {
        return false;
    }
    
    memcpy(tag_data->uid, &response[target_data_pos + 5], tag_data->uid_length);
    
    // For now, just store basic UID data
    tag_data->data_length = 0;
    tag_data->tag_type = 0; // Will be determined later
    tag_data->text_payload[0] = '\0';
    
    return true;
}

// Public API implementation
bool nfc_init(const nfc_config_t* config)
{
    if (!config) return false;
    
    g_nfc_config = *config;
    g_nfc_initialized = false;
    
    printf("NFC: Initializing %s module at I2C address 0x%02X\n", 
           (config->module_type == NFC_MODULE_PN532) ? "PN532" : "Unknown",
           config->i2c_address);
    
    // Initialize I2C/SMBus
    if (smbus_open() < 0) {
        printf("NFC: Failed to open I2C bus\n");
        return false;
    }
    
    switch (config->module_type) {
        case NFC_MODULE_PN532:
            if (!pn532_get_firmware_version()) {
                printf("NFC: Failed to communicate with PN532\n");
                return false;
            }
            if (!pn532_configure_sam()) {
                printf("NFC: Failed to configure PN532 SAM\n");
                return false;
            }
            break;
            
        default:
            printf("NFC: Unsupported module type\n");
            return false;
    }
    
    g_nfc_initialized = true;
    printf("NFC: Initialization successful\n");
    
    return true;
}

void nfc_deinit(void)
{
    if (g_nfc_initialized) {
        nfc_stop_background_polling();
        smbus_close();
        g_nfc_initialized = false;
        printf("NFC: Deinitialized\n");
    }
}

bool nfc_is_available(void)
{
    return g_nfc_initialized;
}

bool nfc_poll_for_tag(nfc_tag_data_t* tag_data)
{
    if (!g_nfc_initialized || !tag_data) {
        return false;
    }
    
    switch (g_nfc_config.module_type) {
        case NFC_MODULE_PN532:
            return pn532_read_passive_target(tag_data);
            
        default:
            return false;
    }
}

bool nfc_read_tag(nfc_tag_data_t* tag_data)
{
    // For now, polling and reading are the same
    // In a full implementation, this would read the full NDEF data
    return nfc_poll_for_tag(tag_data);
}

void nfc_process_tag(const nfc_tag_data_t* tag_data)
{
    if (!tag_data) return;
    
    char uid_str[64];
    nfc_format_uid_string(tag_data, uid_str, sizeof(uid_str));
    
    printf("NFC: Tag detected - UID: %s\n", uid_str);
    
    // Try to parse as game tag
    if (strlen(tag_data->text_payload) > 0) {
        printf("NFC: Tag contains text: %s\n", tag_data->text_payload);
        
        // Parse command from tag data
        // Format could be: "GAME:Sonic The Hedgehog:Genesis"
        // or: "CORE:Genesis"
        // or: "LOAD:/media/fat/games/sonic.bin"
        
        if (strncmp(tag_data->text_payload, "GAME:", 5) == 0) {
            // Search for game
            char search_cmd[512];
            snprintf(search_cmd, sizeof(search_cmd), "search_games %s", tag_data->text_payload + 5);
            cmd_bridge_process(search_cmd);
        }
        else if (strncmp(tag_data->text_payload, "CORE:", 5) == 0) {
            // Load core
            char core_cmd[512];
            snprintf(core_cmd, sizeof(core_cmd), "load_core %s", tag_data->text_payload + 5);
            cmd_bridge_process(core_cmd);
        }
        else if (strncmp(tag_data->text_payload, "LOAD:", 5) == 0) {
            // Load specific file
            char load_cmd[512];
            snprintf(load_cmd, sizeof(load_cmd), "load_game %s", tag_data->text_payload + 5);
            cmd_bridge_process(load_cmd);
        }
    } else {
        // Use UID-based lookup
        // Could maintain a database of UID -> game mappings
        printf("NFC: No text data, using UID-based lookup\n");
    }
    
    // Call registered callback if any
    if (g_tag_callback) {
        g_tag_callback(tag_data);
    }
}

void nfc_start_background_polling(void)
{
    if (g_nfc_initialized) {
        g_polling_enabled = true;
        g_last_poll_time = clock();
        printf("NFC: Background polling started\n");
    }
}

void nfc_stop_background_polling(void)
{
    g_polling_enabled = false;
    printf("NFC: Background polling stopped\n");
}

void nfc_poll_worker(void)
{
    if (!g_polling_enabled || !g_nfc_initialized) {
        return;
    }
    
    clock_t now = clock();
    if ((now - g_last_poll_time) < (g_nfc_config.poll_interval_ms * CLOCKS_PER_SEC / 1000)) {
        return; // Not time to poll yet
    }
    
    g_last_poll_time = now;
    
    nfc_tag_data_t current_tag;
    if (nfc_poll_for_tag(&current_tag)) {
        // Check if this is a different tag from last time
        if (!nfc_uid_matches(&current_tag, &g_last_tag)) {
            g_last_tag = current_tag;
            nfc_process_tag(&current_tag);
        }
    } else {
        // No tag present, clear last tag
        memset(&g_last_tag, 0, sizeof(g_last_tag));
    }
}

void nfc_register_tag_callback(nfc_tag_callback_t callback)
{
    g_tag_callback = callback;
}

// Utility functions
bool nfc_parse_ndef_text(const uint8_t* data, uint16_t length, char* output, uint16_t max_output)
{
    // Simplified NDEF Text Record parsing
    // This would need to be expanded for full NDEF support
    if (!data || !output || length < 3) return false;
    
    // Look for Text Record (TNF=0x01, Type="T")
    for (uint16_t i = 0; i < length - 3; i++) {
        if (data[i] == 0x91 && data[i+1] == 0x01 && data[i+2] == 'T') {
            // Found text record
            uint8_t text_len = data[i+3];
            uint8_t lang_len = data[i+4] & 0x3F;
            
            if (i + 5 + lang_len + text_len <= length && text_len < max_output) {
                memcpy(output, &data[i + 5 + lang_len], text_len);
                output[text_len] = '\0';
                return true;
            }
        }
    }
    
    return false;
}

bool nfc_uid_matches(const nfc_tag_data_t* tag1, const nfc_tag_data_t* tag2)
{
    if (!tag1 || !tag2) return false;
    if (tag1->uid_length != tag2->uid_length) return false;
    if (tag1->uid_length == 0) return false;
    
    return memcmp(tag1->uid, tag2->uid, tag1->uid_length) == 0;
}

void nfc_format_uid_string(const nfc_tag_data_t* tag, char* output, uint16_t max_length)
{
    if (!tag || !output || max_length < 3) return;
    
    output[0] = '\0';
    
    for (uint8_t i = 0; i < tag->uid_length && strlen(output) + 3 < max_length; i++) {
        char hex_byte[4];
        snprintf(hex_byte, sizeof(hex_byte), "%02X", tag->uid[i]);
        if (i > 0) strcat(output, ":");
        strcat(output, hex_byte);
    }
}

// NFC tag programming functions
bool nfc_write_tag(const char* text_data)
{
    if (!text_data || !g_nfc_initialized) {
        return false;
    }
    
    // Write as NDEF text record with default language
    return nfc_write_ndef_text(text_data, "en");
}

bool nfc_write_ndef_text(const char* text_data, const char* language)
{
    if (!text_data || !language || !g_nfc_initialized) {
        return false;
    }
    
    switch (g_nfc_config.module_type) {
        case NFC_MODULE_PN532:
            return pn532_write_ndef_text(text_data, language);
        default:
            return false;
    }
}

bool nfc_format_tag(void)
{
    if (!g_nfc_initialized) {
        return false;
    }
    
    switch (g_nfc_config.module_type) {
        case NFC_MODULE_PN532:
            return pn532_format_tag();
        default:
            return false;
    }
}

bool nfc_is_tag_writable(const nfc_tag_data_t* tag_data)
{
    if (!tag_data || !g_nfc_initialized) {
        return false;
    }
    
    // Most NTAG and Mifare tags are writable
    // This is a simplified check - real implementation would check tag type
    return tag_data->uid_length > 0;
}

bool nfc_get_tag_capacity(const nfc_tag_data_t* tag_data, uint16_t* capacity)
{
    if (!tag_data || !capacity || !g_nfc_initialized) {
        return false;
    }
    
    // Estimate capacity based on tag type and UID length
    // This is simplified - real implementation would read tag specifications
    if (tag_data->uid_length == 4) {
        *capacity = 48;   // NTAG213 (180 bytes total, ~48 usable)
    } else if (tag_data->uid_length == 7) {
        *capacity = 137;  // NTAG215 (540 bytes total, ~137 usable)
    } else {
        *capacity = 924;  // NTAG216 (928 bytes total, ~924 usable)
    }
    
    return true;
}

// PN532 specific tag programming functions
static bool pn532_write_ndef_text(const char* text_data, const char* language)
{
    if (!text_data || !language) return false;
    
    uint8_t lang_len = strlen(language);
    uint8_t text_len = strlen(text_data);
    
    if (text_len > 200) {
        printf("NFC: Text too long for tag\n");
        return false;
    }
    
    // First, get tag info
    nfc_tag_data_t tag_info;
    if (!pn532_read_passive_target(&tag_info)) {
        printf("NFC: No tag present for writing\n");
        return false;
    }
    
    // Create NDEF Text Record
    uint8_t ndef_data[256];
    uint8_t ndef_len = 0;
    
    // NDEF Record Header
    ndef_data[ndef_len++] = 0xD1;        // TNF=0x01 (Well-known), SR=1, ME=1
    ndef_data[ndef_len++] = 0x01;        // Type Length = 1
    ndef_data[ndef_len++] = text_len + lang_len + 1; // Payload Length
    ndef_data[ndef_len++] = 'T';         // Type = "T" (Text)
    
    // Text Record Payload
    ndef_data[ndef_len++] = lang_len;    // Language code length
    
    // Language code
    for (uint8_t i = 0; i < lang_len; i++) {
        ndef_data[ndef_len++] = language[i];
    }
    
    // Text data
    for (uint8_t i = 0; i < text_len; i++) {
        ndef_data[ndef_len++] = text_data[i];
    }
    
    // Write NDEF data to tag
    return pn532_write_ndef_data(ndef_data, ndef_len);
}

static bool pn532_write_ndef_data(const uint8_t* ndef_data, uint8_t ndef_len)
{
    if (!ndef_data || ndef_len == 0) return false;
    
    // This is a simplified implementation
    // Real implementation would need to:
    // 1. Determine tag type (NTAG213/215/216, Mifare Classic, etc.)
    // 2. Calculate proper block/page addresses
    // 3. Handle authentication for Mifare Classic
    // 4. Write capability container and NDEF message
    
    printf("NFC: Writing NDEF data (%d bytes) to tag\n", ndef_len);
    
    // For NTAG213/215/216 tags (most common)
    // Write to pages 4-x (page 0-3 are header/lock bytes)
    
    uint8_t page = 4; // Start writing at page 4
    uint8_t data_pos = 0;
    
    // Write capability container (CC) at page 3
    uint8_t cc_data[4] = {0xE1, 0x10, 0x12, 0x00}; // CC for NTAG213
    if (!pn532_write_page(3, cc_data)) {
        printf("NFC: Failed to write capability container\n");
        return false;
    }
    
    // Write NDEF message length (TLV format)
    uint8_t tlv_header[4] = {0x03, ndef_len, 0x00, 0x00}; // T=03 (NDEF), L=ndef_len
    if (!pn532_write_page(page++, tlv_header)) {
        printf("NFC: Failed to write NDEF TLV header\n");
        return false;
    }
    
    // Write NDEF data in 4-byte chunks
    data_pos = 2; // Skip the TLV header bytes we already wrote
    
    while (data_pos < ndef_len) {
        uint8_t page_data[4] = {0x00, 0x00, 0x00, 0x00};
        
        // Fill page with NDEF data
        for (int i = 0; i < 4 && data_pos < ndef_len; i++) {
            page_data[i] = ndef_data[data_pos++];
        }
        
        if (!pn532_write_page(page++, page_data)) {
            printf("NFC: Failed to write NDEF data page %d\n", page - 1);
            return false;
        }
    }
    
    // Write terminator TLV
    uint8_t terminator[4] = {0xFE, 0x00, 0x00, 0x00};
    if (!pn532_write_page(page, terminator)) {
        printf("NFC: Failed to write terminator\n");
        return false;
    }
    
    printf("NFC: Successfully wrote NDEF data to tag\n");
    return true;
}

static bool pn532_write_page(uint8_t page, const uint8_t* data)
{
    if (!data) return false;
    
    // NTAG Write command
    uint8_t command[] = {
        PN532_COMMAND_INDATAEXCHANGE,
        0x01,           // Target number
        0xA2,           // NTAG Write command
        page,           // Page number
        data[0], data[1], data[2], data[3]  // 4 bytes of data
    };
    
    if (!nfc_i2c_write_command(command, sizeof(command))) {
        return false;
    }
    
    uint8_t response[16];
    uint8_t response_len;
    if (!nfc_i2c_read_response(response, sizeof(response), &response_len)) {
        return false;
    }
    
    // Check for successful write (response should be 0xD5 0x41 0x00)
    if (response_len >= 3 && response[0] == 0xD5 && response[1] == 0x41 && response[2] == 0x00) {
        return true;
    }
    
    printf("NFC: Write failed with response: ");
    for (int i = 0; i < response_len; i++) {
        printf("%02X ", response[i]);
    }
    printf("\n");
    
    return false;
}

static bool pn532_format_tag(void)
{
    // Format tag by writing empty NDEF message
    uint8_t empty_ndef[] = {0xD0, 0x00, 0x00}; // Empty NDEF record
    return pn532_write_ndef_data(empty_ndef, sizeof(empty_ndef));
}