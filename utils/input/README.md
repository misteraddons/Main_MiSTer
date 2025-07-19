# Input Method Daemons

Various input methods for launching games on MiSTer.

## nfc_daemon
NFC/RFID tag reader support using PN532 module.
- Read game information from NFC tags
- Launch games by tapping tags
- Includes tag writer utility

## uart_daemon
Serial communication daemon for external device control.
- Accepts game launch commands via UART
- Supports ESP32/Arduino integration
- Bidirectional communication with announcements

## gpio_daemon
GPIO button and rotary encoder support.
- Map physical buttons to games
- Rotary encoder for game selection
- Direct GPIO control

## filesystem_daemon
File system trigger monitoring.
- Launch games by dropping files into watched directories
- Automatic game detection from file patterns

## cartridge_daemon
USB/UART cartridge reader support.
- Detect physical game cartridges
- Extract game information and launch
- Support for multiple cartridge formats
