#ifndef NFC_READER_H
#define NFC_READER_H

#include <stdint.h>
#include <stdbool.h>

// NFC module types
typedef enum {
    NFC_MODULE_NONE = 0,
    NFC_MODULE_PN532,
    NFC_MODULE_RC522,
    NFC_MODULE_PN7150
} nfc_module_type_t;

// NFC tag data structure
typedef struct {
    uint8_t uid[16];        // Unique ID of the tag
    uint8_t uid_length;     // Length of UID (4, 7, or 10 bytes)
    uint8_t data[1024];     // Tag data payload
    uint16_t data_length;   // Length of data
    uint32_t tag_type;      // Type of NFC tag (NTAG213, MIFARE, etc.)
    char text_payload[512]; // Decoded text if NDEF format
} nfc_tag_data_t;

// NFC configuration
typedef struct {
    nfc_module_type_t module_type;
    uint8_t i2c_address;    // I2C address (usually 0x24 for PN532)
    uint8_t irq_pin;        // Optional interrupt pin
    uint8_t reset_pin;      // Optional reset pin
    bool enable_polling;    // Enable continuous polling
    uint16_t poll_interval_ms; // Polling interval in milliseconds
} nfc_config_t;

// NFC reader functions
bool nfc_init(const nfc_config_t* config);
void nfc_deinit(void);
bool nfc_is_available(void);
bool nfc_poll_for_tag(nfc_tag_data_t* tag_data);
bool nfc_read_tag(nfc_tag_data_t* tag_data);
void nfc_process_tag(const nfc_tag_data_t* tag_data);

// Background polling functions
void nfc_start_background_polling(void);
void nfc_stop_background_polling(void);
void nfc_poll_worker(void); // Called from main loop or thread

// Tag processing callbacks
typedef void (*nfc_tag_callback_t)(const nfc_tag_data_t* tag_data);
void nfc_register_tag_callback(nfc_tag_callback_t callback);

// Utility functions
bool nfc_parse_ndef_text(const uint8_t* data, uint16_t length, char* output, uint16_t max_output);
bool nfc_uid_matches(const nfc_tag_data_t* tag1, const nfc_tag_data_t* tag2);
void nfc_format_uid_string(const nfc_tag_data_t* tag, char* output, uint16_t max_length);

#endif // NFC_READER_H