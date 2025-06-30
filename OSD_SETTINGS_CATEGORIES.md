# OSD Settings Organization

This document describes how MiSTer.ini settings are organized into categories for the OSD menu.

## Categories

### 1. Video & Display
Video output, scaling, HDMI/VGA settings, and display configuration.

**Settings:**
- VIDEO_MODE, VIDEO_MODE_PAL, VIDEO_MODE_NTSC - Video mode configuration
- YPBPR - Component video output
- COMPOSITE_SYNC - Composite sync enable
- FORCED_SCANDOUBLER - Force 31kHz output
- VGA_SCALER - Use scaler for VGA/DVI
- VGA_SOG - Sync-on-green for VGA
- DIRECT_VIDEO - Bypass scaler
- DVI_MODE - DVI compatibility mode
- HDMI_LIMITED - HDMI color range
- HDMI_GAME_MODE - Low-latency mode
- VIDEO_INFO - Video info display time
- VSYNC_ADJUST - VSync adjustment mode
- VSCALE_MODE - Vertical scaling algorithm
- VSCALE_BORDER - Border size
- REFRESH_MIN/MAX - Refresh rate limits
- VRR_* - Variable refresh rate settings
- VIDEO_OFF - Video timeout
- CUSTOM_ASPECT_RATIO_1/2 - Custom aspect ratios

### 2. Audio
Audio output and processing configuration.

**Settings:**
- HDMI_AUDIO_96K - 96kHz audio enable
- AFILTER_DEFAULT - Default audio filter

### 3. Input & Controllers
Keyboard, mouse, gamepads, and special controllers.

**Settings:**
- RESET_COMBO - Reset key combination
- KEY_MENU_AS_RGUI - Menu key mapping
- KBD_NOMOUSE - Disable keyboard mouse
- MOUSE_THROTTLE - Mouse speed
- CONTROLLER_INFO - Controller info display
- GAMEPAD_DEFAULTS - Default gamepad maps
- SNIPER_MODE - Mouse sniper mode
- RUMBLE - Force feedback enable
- WHEEL_FORCE - Wheel force strength
- WHEEL_RANGE - Wheel rotation range
- DEADZONE - Analog deadzone config
- JAMMA_VID/PID - JAMMA controller IDs
- SPINNER_* - Spinner configuration
- PLAYER_*_CONTROLLER - Player controller mappings

### 4. System & Boot
System startup, core loading, and OSD behavior.

**Settings:**
- BOOTSCREEN - Show boot screen
- BOOTCORE - Auto-load core
- BOOTCORE_TIMEOUT - Boot core delay
- MENU_PAL - Menu PAL mode
- FONT - Custom font file
- LOGO - Show MiSTer logo
- OSD_TIMEOUT - OSD auto-hide
- OSD_ROTATE - OSD rotation
- FB_SIZE - Framebuffer size
- FB_TERMINAL - Linux terminal
- RBF_HIDE_DATECODE - Hide core dates
- RECENTS - Recent files tracking
- BROWSE_EXPAND - Browse dialog expansion

### 5. Network & Storage
Network shares and storage configuration.

**Settings:**
- SHARED_FOLDER - CIFS/SMB share path
- WAITMOUNT - Wait for device mount

### 6. Advanced
Developer options and advanced configuration.

**Settings:**
- KEYRAH_MODE - Keyrah interface mode
- LOG_FILE_ENTRY - File access logging
- BT_AUTO_DISCONNECT - Bluetooth timeout
- BT_RESET_BEFORE_PAIR - BT reset on pair
- VFILTER_* - Video filter defaults
- SHMASK_* - Shadow mask defaults
- PRESET_DEFAULT - Default preset
- NO_MERGE_* - Controller merge exceptions

## Implementation Details

### Setting Types
- **TYPE_BOOL**: On/Off toggle
- **TYPE_INT**: Integer input with min/max
- **TYPE_HEX**: Hexadecimal input
- **TYPE_FLOAT**: Floating point input
- **TYPE_STRING**: Text input
- **TYPE_ENUM**: Dropdown selection from predefined options
- **TYPE_ARRAY**: Array of values
- **TYPE_CUSTOM**: Requires special handling

### Setting Properties
- **requires_reboot**: Changes require system restart
- **unit**: Display unit (Hz, ms, %, etc.)
- **min/max**: Value constraints for numeric types
- **enum_options**: Available choices for TYPE_ENUM

## Usage

The OSD settings system provides:
1. Organized categories for easier navigation
2. Descriptive names and help text
3. Input validation based on type and constraints
4. Visual indication of settings requiring reboot
5. Direct integration with existing cfg structure

## Future Enhancements

1. Search functionality across all settings
2. Reset to defaults (per-category and global)
3. Import/Export settings profiles
4. Preview changes before saving
5. Context-sensitive help system