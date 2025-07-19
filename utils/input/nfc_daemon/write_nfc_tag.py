#!/usr/bin/env python3
"""
NFC Tag Writer for MiSTer NFC Daemon
Supports multiple tag formats
"""

import nfc
import sys

def write_nfc1_format(tag, core, game_id):
    """Write NFC1 format: Magic(4) + Core(8) + Game_ID(16) + Type(1) + Reserved(3)"""
    data = bytearray(32)
    
    # Magic word
    data[0:4] = b'NFC1'
    
    # Core name (8 bytes, null-padded)
    core_bytes = core.encode('ascii')[:8]
    data[4:4+len(core_bytes)] = core_bytes
    
    # Game ID (16 bytes, null-padded)
    game_id_bytes = game_id.encode('ascii')[:16]
    data[12:12+len(game_id_bytes)] = game_id_bytes
    
    # Tag type (0 = SINGLE_GAME)
    data[28] = 0
    
    # Reserved bytes already zero
    
    return data

def write_rom_path(tag, rom_path):
    """Write ROM path format"""
    return rom_path.encode('utf-8')

def write_text(tag, text):
    """Write simple text format"""
    return text.encode('utf-8')

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python3 write_nfc_tag.py nfc1 <core> <game_id>")
        print("  python3 write_nfc_tag.py path <rom_path>")
        print("  python3 write_nfc_tag.py text <text>")
        print("")
        print("Examples:")
        print("  python3 write_nfc_tag.py nfc1 PSX SLUS-00067")
        print("  python3 write_nfc_tag.py path '/games/PSX/Final Fantasy VII.bin'")
        print("  python3 write_nfc_tag.py text 'Final Fantasy VII'")
        return
    
    format_type = sys.argv[1]
    
    with nfc.ContactlessFrontend('usb') as clf:
        print("Place NFC tag on reader...")
        tag = clf.connect(rdwr={'on-connect': lambda tag: False})
        
        if tag is None:
            print("No tag detected!")
            return
        
        print(f"Found tag: {tag}")
        
        if format_type == 'nfc1':
            if len(sys.argv) != 4:
                print("Usage: python3 write_nfc_tag.py nfc1 <core> <game_id>")
                return
            core = sys.argv[2]
            game_id = sys.argv[3]
            data = write_nfc1_format(tag, core, game_id)
            print(f"Writing NFC1 format: Core='{core}', Game='{game_id}'")
            
        elif format_type == 'path':
            if len(sys.argv) != 3:
                print("Usage: python3 write_nfc_tag.py path <rom_path>")
                return
            rom_path = sys.argv[2]
            data = write_rom_path(tag, rom_path)
            print(f"Writing ROM path: '{rom_path}'")
            
        elif format_type == 'text':
            if len(sys.argv) != 3:
                print("Usage: python3 write_nfc_tag.py text <text>")
                return
            text = sys.argv[2]
            data = write_text(tag, text)
            print(f"Writing text: '{text}'")
            
        else:
            print(f"Unknown format: {format_type}")
            return
        
        # Write NDEF record
        from ndef import TextRecord
        record = TextRecord(data.decode('utf-8', errors='ignore'))
        tag.ndef.records = [record]
        
        print("Tag written successfully!")
        print(f"Data: {data.hex()}")

if __name__ == '__main__':
    main()