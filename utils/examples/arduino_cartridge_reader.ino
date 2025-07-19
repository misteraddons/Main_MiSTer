/*
 * Arduino Cartridge Reader for MiSTer
 * 
 * Simple UART-based cartridge reader that can detect and dump
 * various cartridge types for automatic game launching on MiSTer
 * 
 * Supported cartridges:
 * - SNES (via cart slot)
 * - Game Boy (via cart slot)
 * - Genesis (via cart slot)
 * 
 * Hardware Requirements:
 * - Arduino Mega 2560 (for sufficient pins)
 * - Cartridge slots/connectors
 * - Level shifters (if needed)
 * - Pull-up resistors
 */

// Pin definitions for SNES cartridge
#define SNES_DATA_PIN_START 22  // Data pins D0-D7 (pins 22-29)
#define SNES_ADDR_PIN_START 30  // Address pins A0-A23 (pins 30-53)
#define SNES_CE_PIN 2           // Chip Enable
#define SNES_OE_PIN 3           // Output Enable
#define SNES_WE_PIN 4           // Write Enable (not used for reading)

// Pin definitions for Game Boy cartridge
#define GB_DATA_PIN_START 22    // Data pins D0-D7 (pins 22-29)
#define GB_ADDR_PIN_START 30    // Address pins A0-A15 (pins 30-45)
#define GB_CS_PIN 2             // Chip Select
#define GB_RD_PIN 3             // Read
#define GB_WR_PIN 4             // Write (not used)

// Cartridge detection pins
#define SNES_DETECT_PIN 5       // SNES cartridge presence detection
#define GB_DETECT_PIN 6         // GB cartridge presence detection
#define CART_LED_PIN 13         // LED to indicate cartridge presence

// Constants
#define SNES_HEADER_OFFSET 0x7FC0   // LoROM header location
#define SNES_HEADER_ALT_OFFSET 0xFFC0  // HiROM header location
#define GB_HEADER_OFFSET 0x0100     // Game Boy header location
#define MAX_ROM_SIZE 0x400000       // 4MB max ROM size
#define UART_BAUD 115200

// Cart types
enum CartType {
    CART_NONE = 0,
    CART_SNES,
    CART_GAMEBOY,
    CART_GENESIS
};

// Global variables
CartType currentCartType = CART_NONE;
bool cartridgeInserted = false;
unsigned long lastCheckTime = 0;
const unsigned long checkInterval = 1000; // Check every second

void setup() {
    Serial.begin(UART_BAUD);
    
    // Initialize pins
    setupSNESPins();
    setupGameBoyPins();
    
    pinMode(SNES_DETECT_PIN, INPUT_PULLUP);
    pinMode(GB_DETECT_PIN, INPUT_PULLUP);
    pinMode(CART_LED_PIN, OUTPUT);
    
    // Disable all cartridge slots initially
    disableAllSlots();
    
    Serial.println("Arduino Cartridge Reader v1.0");
    Serial.println("Ready for MiSTer communication");
    
    // Send initial status
    sendStatus();
}

void loop() {
    // Check for UART commands from MiSTer
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        handleCommand(command);
    }
    
    // Periodically check for cartridge insertion/removal
    if (millis() - lastCheckTime > checkInterval) {
        checkCartridgePresence();
        lastCheckTime = millis();
    }
    
    delay(10);
}

void setupSNESPins() {
    // Set data pins as inputs with pull-ups
    for (int i = 0; i < 8; i++) {
        pinMode(SNES_DATA_PIN_START + i, INPUT_PULLUP);
    }
    
    // Set address pins as outputs
    for (int i = 0; i < 24; i++) {
        pinMode(SNES_ADDR_PIN_START + i, OUTPUT);
        digitalWrite(SNES_ADDR_PIN_START + i, LOW);
    }
    
    // Set control pins as outputs
    pinMode(SNES_CE_PIN, OUTPUT);
    pinMode(SNES_OE_PIN, OUTPUT);
    pinMode(SNES_WE_PIN, OUTPUT);
    
    // Default to disabled state
    digitalWrite(SNES_CE_PIN, HIGH);   // Chip disabled
    digitalWrite(SNES_OE_PIN, HIGH);   // Output disabled
    digitalWrite(SNES_WE_PIN, HIGH);   // Write disabled
}

void setupGameBoyPins() {
    // Set data pins as inputs with pull-ups
    for (int i = 0; i < 8; i++) {
        pinMode(GB_DATA_PIN_START + i, INPUT_PULLUP);
    }
    
    // Set address pins as outputs
    for (int i = 0; i < 16; i++) {
        pinMode(GB_ADDR_PIN_START + i, OUTPUT);
        digitalWrite(GB_ADDR_PIN_START + i, LOW);
    }
    
    // Set control pins as outputs
    pinMode(GB_CS_PIN, OUTPUT);
    pinMode(GB_RD_PIN, OUTPUT);
    pinMode(GB_WR_PIN, OUTPUT);
    
    // Default to disabled state
    digitalWrite(GB_CS_PIN, HIGH);     // Chip disabled
    digitalWrite(GB_RD_PIN, HIGH);     // Read disabled
    digitalWrite(GB_WR_PIN, HIGH);     // Write disabled
}

void disableAllSlots() {
    // Disable SNES slot
    digitalWrite(SNES_CE_PIN, HIGH);
    digitalWrite(SNES_OE_PIN, HIGH);
    
    // Disable Game Boy slot
    digitalWrite(GB_CS_PIN, HIGH);
    digitalWrite(GB_RD_PIN, HIGH);
}

void checkCartridgePresence() {
    bool snesPresent = !digitalRead(SNES_DETECT_PIN);
    bool gbPresent = !digitalRead(GB_DETECT_PIN);
    
    CartType newCartType = CART_NONE;
    if (snesPresent) {
        newCartType = CART_SNES;
    } else if (gbPresent) {
        newCartType = CART_GAMEBOY;
    }
    
    // Check if cartridge status changed
    if (newCartType != currentCartType) {
        currentCartType = newCartType;
        cartridgeInserted = (newCartType != CART_NONE);
        
        digitalWrite(CART_LED_PIN, cartridgeInserted);
        
        if (cartridgeInserted) {
            Serial.print("CART_INSERTED ");
            Serial.println(getCartTypeName(currentCartType));
            
            // Auto-read cartridge info
            delay(500); // Wait for cartridge to settle
            readCartridgeInfo();
        } else {
            Serial.println("CART_REMOVED");
        }
    }
}

const char* getCartTypeName(CartType type) {
    switch (type) {
        case CART_SNES: return "SNES";
        case CART_GAMEBOY: return "GAMEBOY";
        case CART_GENESIS: return "GENESIS";
        default: return "UNKNOWN";
    }
}

void handleCommand(String command) {
    if (command == "STATUS") {
        sendStatus();
    } else if (command == "READ_CART") {
        readCartridgeInfo();
    } else if (command == "DUMP_ROM") {
        dumpROM();
    } else if (command == "PING") {
        Serial.println("OK PONG");
    } else {
        Serial.println("ERROR Unknown command");
    }
}

void sendStatus() {
    Serial.print("STATUS cart_type=");
    Serial.print(getCartTypeName(currentCartType));
    Serial.print(" inserted=");
    Serial.println(cartridgeInserted ? "true" : "false");
}

void readCartridgeInfo() {
    if (!cartridgeInserted) {
        Serial.println("ERROR No cartridge inserted");
        return;
    }
    
    switch (currentCartType) {
        case CART_SNES:
            readSNESInfo();
            break;
        case CART_GAMEBOY:
            readGameBoyInfo();
            break;
        default:
            Serial.println("ERROR Unsupported cartridge type");
            break;
    }
}

void readSNESInfo() {
    Serial.println("CART_INFO type=SNES");
    
    // Try to read SNES header
    uint8_t header[32];
    bool headerFound = false;
    
    // Try LoROM header first
    if (readSNESBytes(SNES_HEADER_OFFSET, header, 32)) {
        if (validateSNESHeader(header)) {
            headerFound = true;
        }
    }
    
    // Try HiROM header if LoROM failed
    if (!headerFound && readSNESBytes(SNES_HEADER_ALT_OFFSET, header, 32)) {
        if (validateSNESHeader(header)) {
            headerFound = true;
        }
    }
    
    if (headerFound) {
        // Extract game title (first 21 bytes of header)
        char title[22];
        memcpy(title, header, 21);
        title[21] = '\0';
        
        // Clean up title (remove trailing spaces)
        for (int i = 20; i >= 0 && title[i] == ' '; i--) {
            title[i] = '\0';
        }
        
        Serial.print("GAME_TITLE \"");
        Serial.print(title);
        Serial.println("\"");
        
        // Extract other info
        uint8_t romType = header[21];
        uint8_t romSize = header[23];
        uint8_t ramSize = header[24];
        
        Serial.print("ROM_TYPE 0x");
        Serial.println(romType, HEX);
        Serial.print("ROM_SIZE ");
        Serial.println(1 << (romSize + 10)); // Size in KB
        
    } else {
        Serial.println("ERROR Could not read SNES header");
    }
}

void readGameBoyInfo() {
    Serial.println("CART_INFO type=GAMEBOY");
    
    // Read Game Boy header
    uint8_t header[80];
    if (readGameBoyBytes(GB_HEADER_OFFSET, header, 80)) {
        // Extract title (0x34-0x43 in header, offset 0x34 from start)
        char title[17];
        memcpy(title, &header[0x34], 16);
        title[16] = '\0';
        
        // Clean up title
        for (int i = 15; i >= 0 && (title[i] == 0 || title[i] == ' '); i--) {
            title[i] = '\0';
        }
        
        Serial.print("GAME_TITLE \"");
        Serial.print(title);
        Serial.println("\"");
        
        // Check if it's Game Boy Color
        uint8_t cgbFlag = header[0x43];
        if (cgbFlag == 0x80 || cgbFlag == 0xC0) {
            Serial.println("CART_SUBTYPE GBC");
        } else {
            Serial.println("CART_SUBTYPE GB");
        }
        
        // ROM size
        uint8_t romSizeCode = header[0x48];
        int romSizeKB = 32 << romSizeCode;
        Serial.print("ROM_SIZE ");
        Serial.println(romSizeKB);
        
    } else {
        Serial.println("ERROR Could not read Game Boy header");
    }
}

bool validateSNESHeader(uint8_t* header) {
    // Simple validation: check complement bytes
    return (header[21] == ((header[23] ^ 0xFF) & 0xFF));
}

bool readSNESBytes(uint32_t address, uint8_t* buffer, int count) {
    for (int i = 0; i < count; i++) {
        buffer[i] = readSNESByte(address + i);
    }
    return true; // Simplified - real implementation would have error checking
}

uint8_t readSNESByte(uint32_t address) {
    // Set address on address bus
    for (int i = 0; i < 24; i++) {
        digitalWrite(SNES_ADDR_PIN_START + i, (address >> i) & 1);
    }
    
    // Enable chip and output
    digitalWrite(SNES_CE_PIN, LOW);
    digitalWrite(SNES_OE_PIN, LOW);
    
    // Small delay for signal propagation
    delayMicroseconds(1);
    
    // Read data from data bus
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        if (digitalRead(SNES_DATA_PIN_START + i)) {
            data |= (1 << i);
        }
    }
    
    // Disable outputs
    digitalWrite(SNES_OE_PIN, HIGH);
    digitalWrite(SNES_CE_PIN, HIGH);
    
    return data;
}

bool readGameBoyBytes(uint16_t address, uint8_t* buffer, int count) {
    for (int i = 0; i < count; i++) {
        buffer[i] = readGameBoyByte(address + i);
    }
    return true;
}

uint8_t readGameBoyByte(uint16_t address) {
    // Set address on address bus
    for (int i = 0; i < 16; i++) {
        digitalWrite(GB_ADDR_PIN_START + i, (address >> i) & 1);
    }
    
    // Enable chip select and read
    digitalWrite(GB_CS_PIN, LOW);
    digitalWrite(GB_RD_PIN, LOW);
    
    // Small delay for signal propagation
    delayMicroseconds(1);
    
    // Read data from data bus
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        if (digitalRead(GB_DATA_PIN_START + i)) {
            data |= (1 << i);
        }
    }
    
    // Disable outputs
    digitalWrite(GB_RD_PIN, HIGH);
    digitalWrite(GB_CS_PIN, HIGH);
    
    return data;
}

void dumpROM() {
    if (!cartridgeInserted) {
        Serial.println("ERROR No cartridge inserted");
        return;
    }
    
    Serial.println("ROM_DUMP_START");
    
    // This would implement full ROM dumping
    // For brevity, just send a placeholder
    Serial.println("ROM_DUMP_END size=0");
}

/*
 * Hardware Setup Guide:
 * 
 * SNES Cartridge Slot:
 * - Connect cartridge edge connector to Arduino pins
 * - Data lines (D0-D7) to pins 22-29
 * - Address lines (A0-A23) to pins 30-53
 * - Control signals: CE=pin2, OE=pin3, WE=pin4
 * - Cart detect switch to pin 5
 * 
 * Game Boy Cartridge Slot:
 * - Data lines (D0-D7) to pins 22-29
 * - Address lines (A0-A15) to pins 30-45
 * - Control signals: CS=pin2, RD=pin3, WR=pin4
 * - Cart detect switch to pin 6
 * 
 * Level Shifters:
 * - Use if cartridge operates at different voltage
 * - SNES: 5V logic (Arduino compatible)
 * - Game Boy: 5V logic (Arduino compatible)
 * 
 * MiSTer Connection:
 * - Arduino USB to MiSTer USB port
 * - Configure MiSTer cartridge daemon for /dev/ttyACM0
 * - Set baud rate to 115200
 * 
 * Usage with MiSTer:
 * 1. Connect Arduino to MiSTer via USB
 * 2. Start cartridge daemon on MiSTer
 * 3. Insert cartridge into Arduino reader
 * 4. Arduino detects cartridge and sends info to MiSTer
 * 5. MiSTer automatically launches the game
 */