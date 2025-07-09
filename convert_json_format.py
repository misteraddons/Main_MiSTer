#!/usr/bin/env python3
"""
Convert single-line JSON database files to properly formatted multi-line JSON
"""

import json
import sys
import os

def convert_json_format(input_file, output_file=None):
    """Convert single-line JSON to properly formatted multi-line JSON"""
    
    if output_file is None:
        # Create output filename
        base, ext = os.path.splitext(input_file)
        output_file = f"{base}_formatted{ext}"
    
    print(f"Converting {input_file} to properly formatted JSON...")
    
    try:
        # Read the JSON file
        with open(input_file, 'r', encoding='utf-8') as f:
            data = json.load(f)
        
        # Write formatted JSON
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False, sort_keys=True)
        
        print(f"Successfully converted to: {output_file}")
        
        # Show file size comparison
        original_size = os.path.getsize(input_file)
        new_size = os.path.getsize(output_file)
        print(f"Original size: {original_size:,} bytes")
        print(f"New size: {new_size:,} bytes")
        print(f"Size increase: {new_size - original_size:,} bytes ({(new_size/original_size - 1)*100:.1f}%)")
        
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in {input_file}: {e}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False
    
    return True

def main():
    if len(sys.argv) < 2:
        print("Convert JSON database files to properly formatted multi-line JSON")
        print()
        print("Usage:")
        print("  python convert_json_format.py <input.json> [output.json]")
        print()
        print("Examples:")
        print("  python convert_json_format.py PSX.data.json")
        print("  python convert_json_format.py PCECD.data.json PCECD_formatted.json")
        print()
        print("If no output file is specified, creates <input>_formatted.json")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    if not os.path.exists(input_file):
        print(f"Error: Input file not found: {input_file}")
        sys.exit(1)
    
    if convert_json_format(input_file, output_file):
        print("\nConversion complete!")
        print("\nThe formatted JSON file can now be used with the updated cdrom.cpp parser.")
    else:
        sys.exit(1)

if __name__ == '__main__':
    main()