# NFC Reader Integration Guide

## Hardware Setup

### Recommended NFC Module: PN532
- **I2C Address**: 0x24 (default)
- **Connections**:
  - VCC → 3.3V
  - GND → Ground  
  - SDA → I2C SDA (GPIO 2)
  - SCL → I2C SCL (GPIO 3)
  - Optional: IRQ → GPIO pin for interrupt-driven polling

### Alternative: RC522 Module
- Cheaper but limited to RFID/Mifare
- I2C address: 0x28

## Software Integration

### 1. Main Loop Integration

Add NFC polling to the main game loop in `main.cpp`:

```cpp
// In main.cpp - add to main loop
#include "nfc_reader.h"

int main() {
    // ... existing initialization ...
    
    // Initialize NFC if configured
    if (cfg.nfc_enable) {
        nfc_config_t nfc_config = {
            .module_type = NFC_MODULE_PN532,
            .i2c_address = 0x24,
            .enable_polling = true,
            .poll_interval_ms = 500
        };
        
        if (nfc_init(&nfc_config)) {
            nfc_start_background_polling();
            printf("NFC: Background polling enabled\n");
        }
    }
    
    while (1) {
        // ... existing main loop ...
        
        // Add NFC polling to main loop
        if (cfg.nfc_enable) {
            nfc_poll_worker();  // Non-blocking polling
        }
        
        // ... rest of main loop ...
    }
}
```

### 2. Configuration Integration

Add to `cfg.h`:
```cpp
struct {
    // ... existing config ...
    
    // NFC Configuration
    bool nfc_enable;
    int nfc_module_type;     // 0=none, 1=PN532, 2=RC522
    int nfc_i2c_address;     // I2C address (0x24 for PN532)
    int nfc_poll_interval;   // Polling interval in ms
    bool nfc_auto_load;      // Auto-load games from tags
} cfg;
```

Add to `cfg.cpp`:
```cpp
// In INI parsing section
if (!strcmp(ini_section, "nfc")) {
    if (!strcmp(option, "enable")) {
        cfg.nfc_enable = ini_getbool(value, 0);
    } else if (!strcmp(option, "module")) {
        if (!strcmp(value, "pn532")) cfg.nfc_module_type = 1;
        else if (!strcmp(value, "rc522")) cfg.nfc_module_type = 2;
        else cfg.nfc_module_type = 0;
    } else if (!strcmp(option, "address")) {
        cfg.nfc_i2c_address = strtol(value, NULL, 16);
    } else if (!strcmp(option, "poll_interval")) {
        cfg.nfc_poll_interval = strtol(value, NULL, 10);
    } else if (!strcmp(option, "auto_load")) {
        cfg.nfc_auto_load = ini_getbool(value, 1);
    }
}
```

### 3. Polling Strategies

#### Strategy 1: Main Loop Integration (Recommended)
```cpp
// Non-blocking polling in main loop
void nfc_poll_worker(void) {
    static clock_t last_poll = 0;
    
    clock_t now = clock();
    if ((now - last_poll) < POLL_INTERVAL) return;
    
    last_poll = now;
    
    // Quick, non-blocking check
    if (nfc_has_tag_present()) {
        nfc_tag_data_t tag;
        if (nfc_read_tag_quick(&tag)) {
            if (!nfc_is_same_as_last(&tag)) {
                nfc_process_tag(&tag);
            }
        }
    }
}
```

#### Strategy 2: Interrupt-Driven (Advanced)
```cpp
// IRQ-based polling using GPIO interrupt
static void nfc_irq_handler(void) {
    // Set flag for main loop to process
    nfc_tag_ready = true;
}

// In main loop
if (nfc_tag_ready) {
    nfc_tag_ready = false;
    nfc_process_pending_tag();
}
```

#### Strategy 3: Timer-Based (Alternative)
```cpp
// Use scheduler system for periodic polling
static void nfc_timer_callback(void) {
    if (!in_game_mode() || osd_shown()) {
        return; // Skip polling during menu
    }
    
    nfc_poll_worker();
}

// Register timer
scheduler_add_timer(500, nfc_timer_callback, true); // Every 500ms
```

## Tag Data Formats

### Format 1: NDEF Text Records
```
"GAME:Sonic The Hedgehog:Genesis"
"CORE:Genesis_MiSTer"
"LOAD:/media/fat/games/sonic.bin"
"CMD:load_core Genesis"
```

### Format 2: JSON Data
```json
{
    "type": "game",
    "name": "Sonic The Hedgehog",
    "core": "Genesis",
    "path": "/media/fat/games/sonic.bin",
    "favorite": true
}
```

### Format 3: Binary UID Mapping
```cpp
// Maintain database of UID -> Game mappings
struct nfc_game_mapping {
    uint8_t uid[16];
    uint8_t uid_len;
    char game_path[512];
    char core_name[64];
};
```

## Usage Examples

### Configuration (MiSTer.ini):
```ini
[nfc]
enable=1
module=pn532
address=0x24
poll_interval=500
auto_load=1
```

### Command Bridge Usage:
```bash
# Setup NFC reader
nfc_setup pn532 0x24 500

# Manual poll for tags  
nfc_poll

# Use with other commands
popup_browse /media/fat/games
```

### Tag Programming Examples:
```bash
# Program an NFC tag with game data
echo "GAME:Sonic:Genesis" > /tmp/nfc_data
# Use NFC tools to write to tag

# Program for direct file loading
echo "LOAD:/media/fat/games/sonic.bin" > /tmp/nfc_data

# Program for core switching
echo "CORE:Genesis_MiSTer" > /tmp/nfc_data
```

## Performance Considerations

### Polling Frequency
- **500ms**: Good balance between responsiveness and performance
- **250ms**: More responsive but higher CPU usage
- **1000ms**: Lower CPU usage but less responsive

### CPU Impact
- PN532 I2C communication: ~1-2ms per poll
- Tag processing: ~0.5ms
- Total overhead: <1% CPU at 500ms intervals

### Memory Usage
- NFC driver: ~8KB code + 2KB RAM
- Tag data cache: ~2KB per cached tag
- Total: <16KB memory footprint

## Error Handling

```cpp
// Robust error handling
if (!nfc_poll_for_tag(&tag)) {
    // Handle communication errors
    if (nfc_error_count++ > 10) {
        printf("NFC: Too many errors, reinitializing\n");
        nfc_deinit();
        usleep(1000000); // 1 second delay
        nfc_init(&config);
        nfc_error_count = 0;
    }
    return;
}
```

## Security Considerations

### Tag Validation
```cpp
bool nfc_validate_tag(const nfc_tag_data_t* tag) {
    // Validate UID format
    if (tag->uid_length < 4 || tag->uid_length > 10) return false;
    
    // Validate data content
    if (tag->data_length > MAX_TAG_DATA) return false;
    
    // Check for malicious content
    if (strstr(tag->text_payload, "../") || 
        strstr(tag->text_payload, "..\\")) return false;
    
    return true;
}
```

### Safe Command Execution
```cpp
void nfc_process_tag_safe(const nfc_tag_data_t* tag) {
    if (!nfc_validate_tag(tag)) {
        printf("NFC: Invalid tag data, ignoring\n");
        return;
    }
    
    // Only allow whitelisted commands
    if (strncmp(tag->text_payload, "GAME:", 5) == 0 ||
        strncmp(tag->text_payload, "CORE:", 5) == 0 ||
        strncmp(tag->text_payload, "LOAD:", 5) == 0) {
        nfc_process_tag(tag);
    } else {
        printf("NFC: Unknown command format, ignoring\n");
    }
}
```

## Implementation Steps

1. **Add NFC module hardware** to MiSTer
2. **Enable I2C** in Linux kernel config  
3. **Add NFC files** to MiSTer codebase
4. **Update Makefile** to include NFC compilation
5. **Add configuration options** to MiSTer.ini
6. **Integrate polling** into main loop
7. **Test with NFC tags** programmed with game data
8. **Create tag programming tools** for users

This integration provides a seamless NFC experience while maintaining MiSTer's performance and stability.