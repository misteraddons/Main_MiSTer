/*
 * PN532 Protocol Implementation
 * 
 * Complete protocol support for PN532 NFC module
 * Supports both I2C and UART interfaces
 */

#ifndef PN532_PROTOCOL_H
#define PN532_PROTOCOL_H

#include <cstdint>
#include <cstring>

// PN532 Commands
#define PN532_COMMAND_DIAGNOSE              0x00
#define PN532_COMMAND_GETFIRMWAREVERSION    0x02
#define PN532_COMMAND_GETGENERALSTATUS      0x04
#define PN532_COMMAND_READREGISTER          0x06
#define PN532_COMMAND_WRITEREGISTER         0x08
#define PN532_COMMAND_READGPIO              0x0C
#define PN532_COMMAND_WRITEGPIO             0x0E
#define PN532_COMMAND_SETSERIALBAUDRATE     0x10
#define PN532_COMMAND_SETPARAMETERS         0x12
#define PN532_COMMAND_SAMCONFIGURATION      0x14
#define PN532_COMMAND_POWERDOWN             0x16
#define PN532_COMMAND_RFCONFIGURATION       0x32
#define PN532_COMMAND_RFREGULATIONTEST      0x58
#define PN532_COMMAND_INJUMPFORDEP          0x56
#define PN532_COMMAND_INJUMPFORPSL          0x46
#define PN532_COMMAND_INLISTPASSIVETARGET   0x4A
#define PN532_COMMAND_INATR                 0x50
#define PN532_COMMAND_INPSL                 0x4E
#define PN532_COMMAND_INDATAEXCHANGE        0x40
#define PN532_COMMAND_INCOMMUNICATETHRU     0x42
#define PN532_COMMAND_INDESELECT            0x44
#define PN532_COMMAND_INRELEASE             0x52
#define PN532_COMMAND_INSELECT              0x54
#define PN532_COMMAND_INAUTOPOLL            0x60
#define PN532_COMMAND_TGINITASTARGET        0x8C
#define PN532_COMMAND_TGSETGENERALBYTES     0x92
#define PN532_COMMAND_TGGETDATA             0x86
#define PN532_COMMAND_TGSETDATA             0x8E
#define PN532_COMMAND_TGSETMETADATA         0x94
#define PN532_COMMAND_TGGETINITIATORCOMMAND 0x88
#define PN532_COMMAND_TGRESPONSETOINITIATOR 0x90
#define PN532_COMMAND_TGGETTARGETSTATUS     0x8A

// Frame structure
#define PN532_PREAMBLE   0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE  0x00
#define PN532_HOSTTOPN532 0xD4
#define PN532_PN532TOHOST 0xD5

// ACK/NACK
const uint8_t PN532_ACK[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
const uint8_t PN532_NACK[] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};

// Mifare commands
#define MIFARE_CMD_AUTH_A     0x60
#define MIFARE_CMD_AUTH_B     0x61
#define MIFARE_CMD_READ       0x30
#define MIFARE_CMD_WRITE      0xA0
#define MIFARE_CMD_TRANSFER   0xB0
#define MIFARE_CMD_DECREMENT  0xC0
#define MIFARE_CMD_INCREMENT  0xC1
#define MIFARE_CMD_STORE      0xC2

// NTAG commands
#define NTAG_CMD_GET_VERSION  0x60
#define NTAG_CMD_READ         0x30
#define NTAG_CMD_FAST_READ    0x3A
#define NTAG_CMD_WRITE        0xA2
#define NTAG_CMD_COMP_WRITE   0xA0
#define NTAG_CMD_READ_CNT     0x39
#define NTAG_CMD_PWD_AUTH     0x1B
#define NTAG_CMD_READ_SIG     0x3C

// Tag types
#define TAG_TYPE_MIFARE_CLASSIC 0x00
#define TAG_TYPE_MIFARE_ULTRALIGHT 0x01
#define TAG_TYPE_MIFARE_PLUS 0x02
#define TAG_TYPE_NTAG 0x03
#define TAG_TYPE_UNKNOWN 0xFF

class PN532Protocol {
public:
    PN532Protocol() : debug_enabled(false) {}
    
    // Build command frame
    bool buildCommandFrame(uint8_t* frame, size_t* frame_len, uint8_t command, 
                          const uint8_t* params, size_t params_len) {
        if (!frame || !frame_len) return false;
        
        size_t pos = 0;
        
        // Preamble and start code
        frame[pos++] = PN532_PREAMBLE;
        frame[pos++] = PN532_STARTCODE1;
        frame[pos++] = PN532_STARTCODE2;
        
        // Length (TFI + Command + params)
        uint8_t length = params_len + 2;
        frame[pos++] = length;
        frame[pos++] = ~length + 1;  // Length checksum
        
        // TFI (Frame identifier - host to PN532)
        frame[pos++] = PN532_HOSTTOPN532;
        
        // Command
        frame[pos++] = command;
        
        // Parameters
        if (params && params_len > 0) {
            memcpy(&frame[pos], params, params_len);
            pos += params_len;
        }
        
        // Data checksum
        uint8_t checksum = PN532_HOSTTOPN532 + command;
        for (size_t i = 0; i < params_len; i++) {
            checksum += params[i];
        }
        frame[pos++] = ~checksum + 1;
        
        // Postamble
        frame[pos++] = PN532_POSTAMBLE;
        
        *frame_len = pos;
        return true;
    }
    
    // Parse response frame
    bool parseResponseFrame(const uint8_t* frame, size_t frame_len,
                           uint8_t* command, uint8_t* response, size_t* response_len) {
        if (!frame || frame_len < 9) return false;
        
        // Check preamble and start code
        if (frame[0] != PN532_PREAMBLE || 
            frame[1] != PN532_STARTCODE1 || 
            frame[2] != PN532_STARTCODE2) {
            return false;
        }
        
        // Get length
        uint8_t length = frame[3];
        uint8_t lcs = frame[4];
        
        // Verify length checksum
        if ((length + lcs) != 0x00) {
            return false;
        }
        
        // Check TFI
        if (frame[5] != PN532_PN532TOHOST) {
            return false;
        }
        
        // Extract command
        *command = frame[6];
        
        // Extract response data
        if (response && response_len) {
            *response_len = length - 2;  // Subtract TFI and command
            if (*response_len > 0) {
                memcpy(response, &frame[7], *response_len);
            }
        }
        
        // Verify data checksum
        uint8_t checksum = PN532_PN532TOHOST + *command;
        for (size_t i = 0; i < *response_len; i++) {
            checksum += response[i];
        }
        checksum = ~checksum + 1;
        
        if (checksum != frame[7 + *response_len]) {
            return false;
        }
        
        return true;
    }
    
    // Check if frame is ACK
    bool isAckFrame(const uint8_t* frame, size_t frame_len) {
        if (frame_len != 6) return false;
        return memcmp(frame, PN532_ACK, 6) == 0;
    }
    
    // Check if frame is NACK
    bool isNackFrame(const uint8_t* frame, size_t frame_len) {
        if (frame_len != 6) return false;
        return memcmp(frame, PN532_NACK, 6) == 0;
    }
    
    // Build ACK frame
    void buildAckFrame(uint8_t* frame) {
        memcpy(frame, PN532_ACK, 6);
    }
    
    // Build NACK frame
    void buildNackFrame(uint8_t* frame) {
        memcpy(frame, PN532_NACK, 6);
    }
    
    // Enable/disable debug output
    void setDebug(bool enable) {
        debug_enabled = enable;
    }
    
    // Detect tag type from ATQ
    uint8_t detectTagType(const uint8_t* atq, uint8_t sak) {
        if (!atq) return TAG_TYPE_UNKNOWN;
        
        // Check MIFARE Classic
        if (sak == 0x08 || sak == 0x18 || sak == 0x88) {
            return TAG_TYPE_MIFARE_CLASSIC;
        }
        
        // Check MIFARE Ultralight / NTAG
        if (atq[0] == 0x44 && atq[1] == 0x00) {
            // Further distinction would require GET_VERSION command
            return TAG_TYPE_NTAG;
        }
        
        return TAG_TYPE_UNKNOWN;
    }
    
    // Calculate block number for NTAG memory layout
    uint8_t ntagGetBlockForPage(uint8_t page) {
        return page;  // NTAG uses page addressing
    }
    
    // Calculate block number for Mifare Classic memory layout  
    uint8_t mifareGetBlockForSector(uint8_t sector, uint8_t block) {
        if (sector < 32) {
            return sector * 4 + block;
        } else {
            return 128 + (sector - 32) * 16 + block;
        }
    }

private:
    bool debug_enabled;
};

#endif // PN532_PROTOCOL_H