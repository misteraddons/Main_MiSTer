#!/usr/bin/env python3
"""
ROM Patch Organizer for MiSTer
Converts scraped ROM hack structure to proper MiSTer ROM patches format
"""

import os
import sys
import re
import shutil
import zipfile
import csv
from pathlib import Path
from difflib import SequenceMatcher
import argparse
from datetime import datetime

def similarity(a, b):
    """Calculate similarity between two strings"""
    return SequenceMatcher(None, a.lower(), b.lower()).ratio()

def parse_scraped_info(scraped_info_path):
    """Parse scraped_info.txt to extract ROM name, hack_of, and CRC32"""
    try:
        with open(scraped_info_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        
        rom_name = None
        hack_of = None
        crc32 = None
        
        # Look for ROM info section (which contains the original ROM details)
        rom_info_section = re.search(r'######## rom_info ########(.*?)(?=########|\Z)', content, re.DOTALL)
        
        if rom_info_section:
            rom_info_content = rom_info_section.group(1)
            
            # Extract ROM name (first line with * in rom_info section)
            rom_name_match = re.search(r'^\s*\*\s*(.+)$', rom_info_content, re.MULTILINE)
            rom_name = rom_name_match.group(1).strip() if rom_name_match else None
            
            # Clean up ROM name - remove "Database match:" prefix if present
            if rom_name and rom_name.startswith('Database match:'):
                rom_name = rom_name.replace('Database match:', '').strip()
            
            # Extract CRC32 from rom_info section
            crc32_match = re.search(r'File/ROM CRC32:\s*([0-9a-fA-F]+)', rom_info_content, re.IGNORECASE)
            if not crc32_match:
                crc32_match = re.search(r'CRC32:\s*([0-9a-fA-F]+)', rom_info_content, re.IGNORECASE)
            
            crc32 = crc32_match.group(1).upper() if crc32_match else None
            
            # Check if the ROM name looks like actual ROM info or just instructions/metadata
            if rom_name and (
                len(rom_name) > 100 or  # Too long to be a ROM name
                'you need' in rom_name.lower() or  # Installation instructions
                'bin/iso' in rom_name.lower() or  # Format instructions
                'exactly:' in rom_name.lower() or  # Size instructions
                rom_name.startswith('CRC32:') or  # Just CRC32 info
                rom_name.startswith('File/ROM CRC32:') or  # Just CRC32 info
                rom_name.startswith('SHA-1:') or  # Just hash info
                'bytes' in rom_name.lower() and 'megabytes' in rom_name.lower() or  # Size info
                re.match(r'^[0-9a-fA-F]{8}$', rom_name.strip()) or  # Just a CRC32 hash
                re.match(r'^[0-9a-fA-F]{40}$', rom_name.strip()) or  # Just a SHA-1 hash
                'database match' in rom_name.lower() or  # Database match instructions
                'apply to' in rom_name.lower() or  # Application instructions
                'size:' in rom_name.lower() or  # Size metadata
                'checksum' in rom_name.lower() or  # Checksum metadata
                len(rom_name.split()) < 2 and any(char.isdigit() for char in rom_name) and len(rom_name) > 6  # Short metadata strings
            ):
                    rom_name = None  # Invalidate rom_name
        
        # Always try to get hack_of as backup
        hack_of_match = re.search(r'hack_of:\s*(.+)', content, re.IGNORECASE)
        if hack_of_match:
            hack_of = hack_of_match.group(1).strip()
        
        # If no rom_info section, try fallback patterns for ROM name and CRC32
        if not rom_name and not crc32:
            # Extract ROM name (first line with * - for original ROM dumps)
            rom_name_match = re.search(r'^\s*\*\s*(.+)$', content, re.MULTILINE)
            rom_name = rom_name_match.group(1).strip() if rom_name_match else None
            
            # Extract CRC32 - try multiple patterns
            crc32_match = re.search(r'Checksum \(CRC32\):\s*0x([0-9a-fA-F]+)', content)
            if not crc32_match:
                # Try alternative patterns
                crc32_match = re.search(r'CRC32:\s*0x([0-9a-fA-F]+)', content)
            if not crc32_match:
                crc32_match = re.search(r'CRC32:\s*([0-9a-fA-F]+)', content)
            if not crc32_match:
                crc32_match = re.search(r'0x([0-9a-fA-F]{8})', content)
            
            crc32 = crc32_match.group(1).upper() if crc32_match else None
        
        
        return rom_name, hack_of, crc32
    except Exception as e:
        print(f"Error parsing {scraped_info_path}: {e}")
        return None, None, None

def load_games_csv(csv_path):
    """Load games database from CSV file"""
    games_db = {}
    try:
        with open(csv_path, 'r', encoding='utf-8') as csvfile:
            reader = csv.DictReader(csvfile)
            
            # Get the actual field names from the CSV
            fieldnames = reader.fieldnames
            
            # Map field names (case-insensitive)
            field_map = {}
            for field in fieldnames:
                field_lower = field.lower()
                if 'system' in field_lower or 'core' in field_lower:
                    field_map['core'] = field
                elif 'path' in field_lower:
                    field_map['path'] = field
                elif 'filename' in field_lower or 'file' in field_lower:
                    field_map['filename'] = field
                elif 'crc32' in field_lower or 'crc' in field_lower:
                    field_map['crc32'] = field
            
            # Make sure we found all required fields
            required_fields = ['core', 'path', 'filename', 'crc32']
            for req in required_fields:
                if req not in field_map:
                    print(f"Error: Could not find required field '{req}' in CSV headers: {fieldnames}")
                    return {}
            
            for row in reader:
                core = row[field_map['core']].strip()
                if core not in games_db:
                    games_db[core] = []
                games_db[core].append({
                    'path': row[field_map['path']].strip(),
                    'filename': row[field_map['filename']].strip(),
                    'crc32': row[field_map['crc32']].strip().upper()
                })
        
        print(f"Loaded {sum(len(games) for games in games_db.values())} games from CSV")
        return games_db
    except Exception as e:
        print(f"Error loading games CSV: {e}")
        import traceback
        traceback.print_exc()
        return {}

def find_rom_by_crc32(crc32, games_db, core_name):
    """Find ROM by exact CRC32 match"""
    if not crc32 or core_name not in games_db:
        return None, None
    
    for game in games_db[core_name]:
        if game['crc32'] == crc32:
                return game['path'], game
    
    return None, None

def find_best_rom_match(rom_name, games_db, core_name, crc32=None, min_similarity=0.6):
    """Find best matching ROM file - first by CRC32, then by name fuzzy matching"""
    
    # Get games for this core
    if core_name not in games_db:
        return None, 0, None
    
    core_games = games_db[core_name]
    
    # First try CRC32 match if provided
    if crc32:
        rom_path, game_info = find_rom_by_crc32(crc32, games_db, core_name)
        if rom_path:
            return rom_path, 1.0, game_info  # Perfect match score
    
    # Fall back to name matching
    if not rom_name:
        return None, 0, None
    
    
    # Clean up the ROM name for matching
    clean_rom_name = re.sub(r'[^a-zA-Z0-9\s]', '', rom_name).lower()
    
    # Extract key words from the ROM name (ignore common words)
    rom_words = [word for word in clean_rom_name.split() if len(word) > 2 and word not in ['the', 'and', 'for', 'usa', 'eur', 'jap', 'rom', 'bin', 'iso']]
    
    
    best_match = None
    best_score = 0
    best_game_info = None
    
    for game in core_games:
        filename = game['filename']
        
        # Remove extension and clean filename
        clean_filename = re.sub(r'[^a-zA-Z0-9\s]', '', Path(filename).stem).lower()
        
        # Calculate basic similarity
        basic_score = similarity(clean_rom_name, clean_filename)
        
        # Calculate word-based similarity (for handling clean names vs full names)
        filename_words = clean_filename.split()
        word_matches = sum(1 for word in rom_words if any(word in fw for fw in filename_words))
        word_score = word_matches / len(rom_words) if rom_words else 0
        
        # Combine scores with weight towards word matching for clean names
        if len(rom_words) <= 3:  # Likely a clean name like "Supreme Warrior"
            combined_score = (word_score * 0.7) + (basic_score * 0.3)
        else:  # More specific name, trust basic similarity more
            combined_score = (basic_score * 0.7) + (word_score * 0.3)
        
        if combined_score > best_score and combined_score >= min_similarity:
            best_score = combined_score
            best_match = game['path']
            best_game_info = game
    
    return best_match, best_score, best_game_info

def calculate_file_crc32(file_path):
    """Calculate CRC32 of a file"""
    import zlib
    try:
        with open(file_path, 'rb') as f:
            # Read file in chunks to handle large files
            crc32 = 0
            while True:
                chunk = f.read(8192)
                if not chunk:
                    break
                crc32 = zlib.crc32(chunk, crc32)
        return f"{crc32 & 0xffffffff:08X}"
    except Exception as e:
        print(f"Error calculating CRC32 for {file_path}: {e}")
        return None

def extract_patch_files(archive_path, dest_dir, verbose=False):
    """Extract patch files from zip/7z/rar, keeping only patch files"""
    patch_extensions = {'.bps', '.ips', '.ups', '.xdelta', '.delta'}
    extracted_files = []
    
    archive_ext = Path(archive_path).suffix.lower()
    
    try:
        if archive_ext == '.zip':
            with zipfile.ZipFile(archive_path, 'r') as zip_ref:
                for file_info in zip_ref.filelist:
                    if not file_info.is_dir():
                        file_ext = Path(file_info.filename).suffix.lower()
                        if file_ext in patch_extensions:
                            # Extract to dest_dir but flatten the path
                            filename = os.path.basename(file_info.filename)
                            with zip_ref.open(file_info) as source, open(os.path.join(dest_dir, filename), 'wb') as target:
                                target.write(source.read())
                            extracted_files.append(os.path.join(dest_dir, filename))
        
        elif archive_ext == '.7z':
            # Try to use py7zr library first, fall back to command line
            try:
                import py7zr
                with py7zr.SevenZipFile(archive_path, mode='r') as z:
                    for fname in z.getnames():
                        if Path(fname).suffix.lower() in patch_extensions:
                            # Extract to dest_dir but flatten the path
                            filename = os.path.basename(fname)
                            # Use extractall with specific file and then move to flatten
                            z.extract(targets=[fname], path=dest_dir)
                            # Move the extracted file to the root if it's in a subdirectory
                            extracted_path = os.path.join(dest_dir, fname)
                            final_path = os.path.join(dest_dir, filename)
                            if extracted_path != final_path:
                                os.rename(extracted_path, final_path)
                                # Clean up any empty directories
                                dir_path = os.path.dirname(extracted_path)
                                while dir_path != dest_dir and dir_path:
                                    try:
                                        os.rmdir(dir_path)
                                        dir_path = os.path.dirname(dir_path)
                                    except OSError:
                                        break
                            extracted_files.append(final_path)
            except ImportError:
                # Fall back to command line tool
                import subprocess
                try:
                    # List files in archive
                    result = subprocess.run(['7z', 'l', archive_path], capture_output=True, text=True)
                    if result.returncode == 0:
                        # Extract only patch files
                        for ext in patch_extensions:
                            subprocess.run(['7z', 'e', archive_path, f'*{ext}', '-o' + dest_dir, '-y'], 
                                         capture_output=True)
                        
                        # Check what was extracted
                        for file in os.listdir(dest_dir):
                            if Path(file).suffix.lower() in patch_extensions:
                                extracted_files.append(os.path.join(dest_dir, file))
                except FileNotFoundError:
                    if verbose:
                        print(f"          Warning: 7z not available. Install py7zr (pip install py7zr) or 7z command")
                    raise Exception("7z extraction tool not available")
        
        elif archive_ext == '.rar':
            # Try to use rarfile library first, fall back to command line
            try:
                import rarfile
                with rarfile.RarFile(archive_path) as rf:
                    for fname in rf.namelist():
                        if Path(fname).suffix.lower() in patch_extensions:
                            # Extract to dest_dir but flatten the path
                            filename = os.path.basename(fname)
                            # Extract the file to dest_dir
                            rf.extract(fname, dest_dir)
                            # Move the extracted file to the root if it's in a subdirectory
                            extracted_path = os.path.join(dest_dir, fname)
                            final_path = os.path.join(dest_dir, filename)
                            if extracted_path != final_path:
                                os.rename(extracted_path, final_path)
                                # Clean up any empty directories
                                dir_path = os.path.dirname(extracted_path)
                                while dir_path != dest_dir and dir_path:
                                    try:
                                        os.rmdir(dir_path)
                                        dir_path = os.path.dirname(dir_path)
                                    except OSError:
                                        break
                            extracted_files.append(final_path)
            except ImportError:
                # Fall back to command line tool
                import subprocess
                try:
                    # Extract only patch files using unrar
                    for ext in patch_extensions:
                        subprocess.run(['unrar', 'e', archive_path, f'*{ext}', dest_dir], 
                                     capture_output=True)
                    
                    # Check what was extracted
                    for file in os.listdir(dest_dir):
                        if Path(file).suffix.lower() in patch_extensions:
                            extracted_files.append(os.path.join(dest_dir, file))
                except FileNotFoundError:
                    if verbose:
                        print(f"          Warning: RAR not available. Install rarfile (pip install rarfile) or unrar command")
                    raise Exception("RAR extraction tool not available")
                
    except Exception as e:
        # Re-raise for upper level error handling - let caller decide how to handle
        raise e
    
    return extracted_files

def log_error(error_log, core, rom, patch, error_msg):
    """Add error to log list"""
    error_entry = {
        'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
        'core': core,
        'rom': rom,
        'patch': patch,
        'error': error_msg
    }
    error_log.append(error_entry)
    
def write_error_log(error_log, log_path):
    """Write error log to file"""
    if not error_log:
        return
        
    with open(log_path, 'w', encoding='utf-8') as f:
        f.write("ROM Patch Organization Error Log\n")
        f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("=" * 80 + "\n\n")
        
        for entry in error_log:
            f.write(f"Time: {entry['timestamp']}\n")
            f.write(f"Core: {entry['core']}\n")
            f.write(f"ROM: {entry['rom']}\n")
            f.write(f"Patch: {entry['patch']}\n")
            f.write(f"Error: {entry['error']}\n")
            f.write("-" * 40 + "\n")

def copy_info_files_for_patches(patch_path, output_rom_dir, patch_filenames, verbose=False):
    """Copy info and readme files for each patch file"""
    scraped_info_path = os.path.join(patch_path, 'scraped_info.txt')
    readme_path = os.path.join(patch_path, 'og_readme.txt')
    
    if not os.path.exists(scraped_info_path):
        return
    
    # Read the files once
    scraped_info_content = None
    readme_content = None
    
    try:
        with open(scraped_info_path, 'r', encoding='utf-8', errors='ignore') as f:
            scraped_info_content = f.read()
    except Exception as e:
        if verbose:
            print(f"        Warning: Could not read scraped_info.txt: {e}")
        return
    
    if os.path.exists(readme_path):
        try:
            with open(readme_path, 'r', encoding='utf-8', errors='ignore') as f:
                readme_content = f.read()
        except Exception as e:
            if verbose:
                print(f"        Warning: Could not read og_readme.txt: {e}")
    
    # Create info/readme for each patch
    for patch_name in patch_filenames:
        try:
            # Write info file
            info_dest = os.path.join(output_rom_dir, f"{patch_name}_info.txt")
            with open(info_dest, 'w', encoding='utf-8') as f:
                f.write(scraped_info_content)
            
            # Write readme file if available
            if readme_content:
                readme_dest = os.path.join(output_rom_dir, f"{patch_name}_readme.txt")
                with open(readme_dest, 'w', encoding='utf-8') as f:
                    f.write(readme_content)
                    
        except Exception as e:
            if verbose:
                print(f"        Warning: Error creating info files for {patch_name}: {e}")

def organize_rom_patches(rom_patches_dir, games_csv, output_dir, dry_run=False, verbose=False):
    """Main function to organize ROM patches"""
    
    if not os.path.exists(rom_patches_dir):
        print(f"Error: ROM patches directory not found: {rom_patches_dir}")
        return
    
    if not os.path.exists(games_csv):
        print(f"Error: Games CSV file not found: {games_csv}")
        return
    
    # Load games database
    games_db = load_games_csv(games_csv)
    if not games_db:
        print("Error: Failed to load games database")
        return
    
    if not dry_run:
        os.makedirs(output_dir, exist_ok=True)
    
    # Initialize error log
    error_log_path = os.path.join(output_dir if not dry_run else ".", f"organize_errors_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt")
    error_log = []
    
    processed_count = 0
    skipped_count = 0
    error_count = 0
    total_patches = 0
    
    # Count total patches first for progress indication
    print("Scanning for patches...")
    patch_count = 0
    for core_dir in os.listdir(rom_patches_dir):
        core_path = os.path.join(rom_patches_dir, core_dir)
        if os.path.isdir(core_path):
            for rom_dir in os.listdir(core_path):
                rom_path = os.path.join(core_path, rom_dir)
                if os.path.isdir(rom_path):
                    patch_count += len([d for d in os.listdir(rom_path) if os.path.isdir(os.path.join(rom_path, d))])
    
    print(f"Found {patch_count} patches to process\n")
    current_patch = 0
    
    # Walk through core directories
    for core_dir in os.listdir(rom_patches_dir):
        core_path = os.path.join(rom_patches_dir, core_dir)
        if not os.path.isdir(core_path):
            continue
            
        # Walk through ROM directories
        for rom_dir in os.listdir(core_path):
            rom_path = os.path.join(core_path, rom_dir)
            if not os.path.isdir(rom_path):
                continue
            
            # Walk through patch directories
            for patch_dir in os.listdir(rom_path):
                patch_path = os.path.join(rom_path, patch_dir)
                if not os.path.isdir(patch_path):
                    continue
                
                current_patch += 1
                if not verbose:
                    print(f"\rProcessing: {current_patch}/{patch_count} - {core_dir}/{rom_dir}/{patch_dir[:30]}...", end='', flush=True)
                else:
                    print(f"\n[{current_patch}/{patch_count}] {core_dir} / {rom_dir} / {patch_dir}")
                
                try:
                    # Look for scraped_info.txt
                    scraped_info_path = os.path.join(patch_path, 'scraped_info.txt')
                    if not os.path.exists(scraped_info_path):
                        if verbose:
                            print(f"      Warning: No scraped_info.txt found, skipping")
                        log_error(error_log, core_dir, rom_dir, patch_dir, "No scraped_info.txt found")
                        skipped_count += 1
                        error_count += 1
                        continue
                
                    # Parse scraped info
                    rom_name, hack_of, crc32 = parse_scraped_info(scraped_info_path)
                    
                    # First try CRC32 if available
                    rom_match = None
                    score = 0
                    game_info = None
                    used_search = None
                    
                    if crc32:
                        if verbose:
                            print(f"      Trying CRC32 match: {crc32}")
                        rom_match, score, game_info = find_best_rom_match(None, games_db, core_dir, crc32=crc32)
                        if rom_match:
                            used_search = "CRC32"
                    
                    # If no CRC32 match, try name-based strategies
                    if not rom_match:
                        search_names = []
                        if rom_name:
                            search_names.append(("ROM info", rom_name))
                        if hack_of:
                            search_names.append(("hack_of", hack_of))
                        # Add patch directory name as last resort
                        search_names.append(("patch name", patch_dir))
                        
                        # Try each search strategy until we find a match
                        for search_label, search_name in search_names:
                            if verbose:
                                print(f"      Trying {search_label}: '{search_name}'")
                            rom_match, score, game_info = find_best_rom_match(search_name, games_db, core_dir)
                            if rom_match:
                                used_search = search_label
                                break
                    
                    if not rom_match:
                        if verbose:
                            print(f"      Warning: No matching ROM found")
                        log_error(error_log, core_dir, rom_dir, patch_dir, "No matching ROM found in database")
                        skipped_count += 1
                        error_count += 1
                        continue
                
                    # Use CRC32 from games database if available, otherwise from scraped info
                    if game_info and game_info['crc32']:
                        db_crc32 = game_info['crc32']
                        if crc32 and crc32 != db_crc32 and verbose:
                            print(f"      Warning: CRC32 mismatch - scraped: {crc32}, database: {db_crc32}")
                            print(f"      Using database CRC32: {db_crc32}")
                        crc32 = db_crc32
                    elif not crc32:
                        if verbose:
                            print(f"      Warning: No CRC32 available")
                        log_error(error_log, core_dir, rom_dir, patch_dir, "No CRC32 available")
                        skipped_count += 1
                        error_count += 1
                        continue
                    
                    # Create proper folder name: ROM_FILENAME_WITHOUT_EXTENSION [CRC32]
                    rom_filename_no_ext = Path(game_info['filename']).stem
                    proper_folder_name = f"{rom_filename_no_ext} [{crc32}]"
                    
                    # Sanitize folder name for filesystem compatibility
                    proper_folder_name = re.sub(r'[<>:"/\\|?*]', '_', proper_folder_name)
                    
                    # Create output directory structure
                    output_core_dir = os.path.join(output_dir, core_dir)
                    output_rom_dir = os.path.join(output_core_dir, proper_folder_name)
                
                    if verbose:
                        print(f"      Output: {output_rom_dir}")
                    
                    if not dry_run:
                        try:
                            os.makedirs(output_rom_dir, exist_ok=True)
                        except Exception as e:
                            if verbose:
                                print(f"      Error creating directory: {e}")
                            log_error(error_log, core_dir, rom_dir, patch_dir, f"Directory creation failed: {e}")
                            skipped_count += 1
                            error_count += 1
                            continue
                
                    # Look for zip files and extract patches, also handle direct patch files
                    patch_files_found = False
                    patch_filenames = []  # Track patch filenames for naming info files
                    
                    for item in os.listdir(patch_path):
                        item_path = os.path.join(patch_path, item)
                        
                        # Skip info files
                        if item in ['scraped_info.txt', 'og_readme.txt']:
                            continue
                        
                        if os.path.isfile(item_path):
                            if item.lower().endswith(('.zip', '.7z', '.rar')):
                                if verbose:
                                    print(f"        Extracting: {item}")
                                if not dry_run:
                                    try:
                                        extracted_files = extract_patch_files(item_path, output_rom_dir, verbose)
                                        if extracted_files:
                                            patch_files_found = True
                                            # Get patch filenames from extracted files
                                            for extracted_file in extracted_files:
                                                patch_filenames.append(Path(extracted_file).stem)
                                            total_patches += len(extracted_files)
                                    except Exception as e:
                                        # Log extraction error but continue
                                        if verbose:
                                            print(f"        Failed to extract {item}: {e}")
                                        # Don't treat extraction failure as fatal - continue processing
                            
                            elif item.lower().endswith(('.bps', '.ips', '.ups', '.xdelta', '.delta')):
                                # Direct patch file
                                if not dry_run:
                                    shutil.copy2(item_path, output_rom_dir)
                                    patch_files_found = True
                                patch_filenames.append(Path(item).stem)
                                total_patches += 1
                        
                        elif os.path.isdir(item_path):
                            # Check subdirectories for patch files (in case of nested structure)
                            for subitem in os.listdir(item_path):
                                subitem_path = os.path.join(item_path, subitem)
                                if os.path.isfile(subitem_path) and subitem.lower().endswith(('.bps', '.ips', '.ups', '.xdelta', '.delta')):
                                    if not dry_run:
                                        shutil.copy2(subitem_path, output_rom_dir)
                                        patch_files_found = True
                                    patch_filenames.append(Path(subitem).stem)
                                    total_patches += 1
                
                    # Copy info files for each patch
                    if not dry_run and patch_filenames:
                        copy_info_files_for_patches(patch_path, output_rom_dir, patch_filenames, verbose)
                    
                    if patch_files_found or dry_run:
                        processed_count += 1
                    else:
                        if verbose:
                            print(f"      Warning: No patch files found")
                        log_error(error_log, core_dir, rom_dir, patch_dir, "No patch files found")
                        skipped_count += 1
                        error_count += 1
                        
                except Exception as e:
                    # Catch any unexpected errors
                    if verbose:
                        print(f"      ERROR: {str(e)}")
                    log_error(error_log, core_dir, rom_dir, patch_dir, f"Unexpected error: {str(e)}")
                    skipped_count += 1
                    error_count += 1
    
    # Clear the progress line
    if not verbose:
        print("\r" + " " * 80 + "\r", end='', flush=True)
    
    print(f"\n=== Summary ===")
    print(f"Total patch directories scanned: {current_patch}")
    print(f"Total patch files found: {total_patches}")
    print(f"Successfully processed: {processed_count}")
    print(f"Skipped: {skipped_count}")
    print(f"Errors: {error_count}")
    
    if dry_run:
        print("\nDRY RUN - No files were actually moved/copied")
    
    # Write error log if there were errors
    if error_log and not dry_run:
        write_error_log(error_log, error_log_path)
        print(f"\nError log written to: {error_log_path}")
    elif error_log:
        print(f"\nErrors encountered (would be logged to: {error_log_path})")
        for entry in error_log[:5]:  # Show first 5 errors
            print(f"  - {entry['core']}/{entry['rom']}/{entry['patch']}: {entry['error']}")
        if len(error_log) > 5:
            print(f"  ... and {len(error_log) - 5} more errors")

def main():
    parser = argparse.ArgumentParser(description='Organize ROM patches for MiSTer')
    parser.add_argument('rom_patches_dir', help='Source ROM patches directory')
    parser.add_argument('games_csv', help='Games CSV database file')
    parser.add_argument('output_dir', help='Output directory for organized patches')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be done without actually doing it')
    parser.add_argument('--verbose', '-v', action='store_true', help='Show detailed debug output')
    parser.add_argument('--min-similarity', type=float, default=0.7, help='Minimum similarity for ROM matching (0.0-1.0)')
    
    args = parser.parse_args()
    
    organize_rom_patches(
        args.rom_patches_dir,
        args.games_csv, 
        args.output_dir,
        dry_run=args.dry_run,
        verbose=args.verbose
    )

if __name__ == "__main__":
    main()