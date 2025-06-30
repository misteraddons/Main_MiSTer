#include <stddef.h>
#include <string.h>
#include "osd_settings.h"
#include "cfg.h"

// Enum options for various settings
static const char* bool_options[] = { "Off", "On" };
static const char* reset_combo_options[] = { "LCtrl+LAlt+RAlt", "LCtrl+LGM+RGM", "LCtrl+LAlt+Del", "LCtrl+LAlt+RAlt or LCtrl+LAlt+Del" };
static const char* vscale_mode_options[] = { "Integer", "Integer (Use Fw)", "Integer (Use Ar)", "Integer (Use Fw+Ar)", "Scale", "Scale (Use Ar)" };
static const char* hdmi_limited_options[] = { "Off", "16-235", "16-255" };
static const char* vsync_adjust_options[] = { "Off", "Auto", "Low lag" };
static const char* osd_rotate_options[] = { "No", "Yes", "90 degrees" };
static const char* fb_size_options[] = { "Auto", "Full size", "1/2 of resolution", "1/4 of resolution", "Disable" };
static const char* vrr_mode_options[] = { "Off", "Auto", "FreeSync", "VESA" };

// Category information
static const osd_category_info_t category_info[CAT_COUNT] = {
    { "Video & Display", "\x8D", "Video output and display settings" },
    { "Audio", "\x8D", "Audio output configuration" },
    { "Input & Controllers", "\x82", "Keyboard, mouse, and controller settings" },
    { "System & Boot", "\x80", "System startup and core settings" },
    { "Network & Storage", "\x1C", "Network and storage options" },
    { "Advanced", "\x81", "Advanced settings and developer options" }
};

// All settings definitions
static const osd_setting_def_t all_settings[] = {
    // ===== VIDEO & DISPLAY SETTINGS =====
    { "VIDEO_MODE", "Video Mode", "Default video mode", CAT_VIDEO_DISPLAY, TYPE_STRING, 
      &cfg.video_conf, 0, 0, NULL, 0, NULL, true },
    
    { "VIDEO_MODE_PAL", "Video Mode (PAL)", "Video mode for PAL cores", CAT_VIDEO_DISPLAY, TYPE_STRING, 
      &cfg.video_conf_pal, 0, 0, NULL, 0, NULL, true },
    
    { "VIDEO_MODE_NTSC", "Video Mode (NTSC)", "Video mode for NTSC cores", CAT_VIDEO_DISPLAY, TYPE_STRING, 
      &cfg.video_conf_ntsc, 0, 0, NULL, 0, NULL, true },
    
    { "YPBPR", "YPbPr Output", "Enable component video output", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.vga_mode_int, 0, 1, bool_options, 2, NULL, true },
    
    { "COMPOSITE_SYNC", "Composite Sync", "Enable composite sync on HSync", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.csync, 0, 1, bool_options, 2, NULL, true },
    
    { "FORCED_SCANDOUBLER", "Force Scandoubler", "Force scandoubler for 15kHz cores", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.forced_scandoubler, 0, 1, bool_options, 2, NULL, true },
    
    { "VGA_SCALER", "VGA Scaler", "Use scaler for VGA/DVI output", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.vga_scaler, 0, 1, bool_options, 2, NULL, true },
    
    { "VGA_SOG", "VGA Sync-on-Green", "Enable sync-on-green for VGA", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.vga_sog, 0, 1, bool_options, 2, NULL, true },
    
    { "DIRECT_VIDEO", "Direct Video", "Bypass scaler for compatible displays", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.direct_video, 0, 1, bool_options, 2, NULL, true },
    
    { "DVI_MODE", "DVI Mode", "Disable HDMI features for DVI displays", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.dvi_mode, 0, 1, bool_options, 2, NULL, true },
    
    { "HDMI_LIMITED", "HDMI Color Range", "HDMI color range limitation", CAT_VIDEO_DISPLAY, TYPE_ENUM, 
      &cfg.hdmi_limited, 0, 2, hdmi_limited_options, 3, NULL, true },
    
    { "HDMI_GAME_MODE", "HDMI Game Mode", "Enable low-latency game mode", CAT_VIDEO_DISPLAY, TYPE_BOOL, 
      &cfg.hdmi_game_mode, 0, 1, bool_options, 2, NULL, false },
    
    { "VIDEO_INFO", "Video Info Display", "Show video information on screen", CAT_VIDEO_DISPLAY, TYPE_INT, 
      &cfg.video_info, 0, 10, NULL, 0, "sec", false },
    
    { "VSYNC_ADJUST", "VSync Adjustment", "Automatic refresh rate adjustment", CAT_VIDEO_DISPLAY, TYPE_ENUM, 
      &cfg.vsync_adjust, 0, 2, vsync_adjust_options, 3, NULL, false },
    
    { "VSCALE_MODE", "Vertical Scale Mode", "Vertical scaling algorithm", CAT_VIDEO_DISPLAY, TYPE_ENUM, 
      &cfg.vscale_mode, 0, 5, vscale_mode_options, 6, NULL, false },
    
    { "VSCALE_BORDER", "Vertical Scale Border", "Border size for scaled image", CAT_VIDEO_DISPLAY, TYPE_INT, 
      &cfg.vscale_border, 0, 399, NULL, 0, "px", false },
    
    { "REFRESH_MIN", "Minimum Refresh Rate", "Minimum allowed refresh rate", CAT_VIDEO_DISPLAY, TYPE_FLOAT, 
      &cfg.refresh_min, 0, 150, NULL, 0, "Hz", false },
    
    { "REFRESH_MAX", "Maximum Refresh Rate", "Maximum allowed refresh rate", CAT_VIDEO_DISPLAY, TYPE_FLOAT, 
      &cfg.refresh_max, 0, 150, NULL, 0, "Hz", false },
    
    { "VRR_MODE", "Variable Refresh Rate", "VRR mode selection", CAT_VIDEO_DISPLAY, TYPE_ENUM, 
      &cfg.vrr_mode, 0, 3, vrr_mode_options, 4, NULL, false },
    
    { "VRR_MIN_FRAMERATE", "VRR Min Framerate", "Minimum VRR framerate", CAT_VIDEO_DISPLAY, TYPE_INT, 
      &cfg.vrr_min_framerate, 0, 255, NULL, 0, "Hz", false },
    
    { "VRR_MAX_FRAMERATE", "VRR Max Framerate", "Maximum VRR framerate", CAT_VIDEO_DISPLAY, TYPE_INT, 
      &cfg.vrr_max_framerate, 0, 255, NULL, 0, "Hz", false },
    
    { "VRR_VESA_FRAMERATE", "VRR VESA Framerate", "VESA VRR framerate", CAT_VIDEO_DISPLAY, TYPE_INT, 
      &cfg.vrr_vesa_framerate, 0, 255, NULL, 0, "Hz", false },
    
    { "VIDEO_OFF", "Video Off Timeout", "Turn off video after inactivity", CAT_VIDEO_DISPLAY, TYPE_INT, 
      &cfg.video_off, 0, 3600, NULL, 0, "sec", false },
    
    // ===== AUDIO SETTINGS =====
    { "HDMI_AUDIO_96K", "HDMI 96kHz Audio", "Enable 96kHz audio output", CAT_AUDIO, TYPE_BOOL, 
      &cfg.hdmi_audio_96k, 0, 1, bool_options, 2, NULL, true },
    
    { "AFILTER_DEFAULT", "Default Audio Filter", "Default audio filter file", CAT_AUDIO, TYPE_STRING, 
      &cfg.afilter_default, 0, 0, NULL, 0, NULL, false },
    
    // ===== INPUT & CONTROLLER SETTINGS =====
    { "RESET_COMBO", "Reset Key Combo", "Keyboard combination for reset", CAT_INPUT_CONTROLLERS, TYPE_ENUM, 
      &cfg.reset_combo, 0, 3, reset_combo_options, 4, NULL, false },
    
    { "KEY_MENU_AS_RGUI", "Menu Key as Right GUI", "Use Menu key as Right GUI", CAT_INPUT_CONTROLLERS, TYPE_BOOL, 
      &cfg.key_menu_as_rgui, 0, 1, bool_options, 2, NULL, false },
    
    { "KBD_NOMOUSE", "Disable Mouse", "Disable mouse emulation via keyboard", CAT_INPUT_CONTROLLERS, TYPE_BOOL, 
      &cfg.kbd_nomouse, 0, 1, bool_options, 2, NULL, false },
    
    { "MOUSE_THROTTLE", "Mouse Throttle", "Mouse movement speed", CAT_INPUT_CONTROLLERS, TYPE_INT, 
      &cfg.mouse_throttle, 1, 100, NULL, 0, "%", false },
    
    { "CONTROLLER_INFO", "Controller Info", "Display controller information", CAT_INPUT_CONTROLLERS, TYPE_INT, 
      &cfg.controller_info, 0, 10, NULL, 0, "sec", false },
    
    { "GAMEPAD_DEFAULTS", "Gamepad Defaults", "Use default gamepad mappings", CAT_INPUT_CONTROLLERS, TYPE_BOOL, 
      &cfg.gamepad_defaults, 0, 1, bool_options, 2, NULL, false },
    
    { "SNIPER_MODE", "Sniper Mode", "Enable mouse sniper mode", CAT_INPUT_CONTROLLERS, TYPE_BOOL, 
      &cfg.sniper_mode, 0, 1, bool_options, 2, NULL, false },
    
    { "RUMBLE", "Controller Rumble", "Enable force feedback/rumble", CAT_INPUT_CONTROLLERS, TYPE_BOOL, 
      &cfg.rumble, 0, 1, bool_options, 2, NULL, false },
    
    { "WHEEL_FORCE", "Wheel Force Feedback", "Force feedback strength", CAT_INPUT_CONTROLLERS, TYPE_INT, 
      &cfg.wheel_force, 0, 100, NULL, 0, "%", false },
    
    { "WHEEL_RANGE", "Wheel Range", "Steering wheel rotation range", CAT_INPUT_CONTROLLERS, TYPE_INT, 
      &cfg.wheel_range, 0, 1000, NULL, 0, "°", false },
    
    // ===== SYSTEM & BOOT SETTINGS =====
    { "BOOTSCREEN", "Boot Screen", "Show boot screen on startup", CAT_SYSTEM_BOOT, TYPE_BOOL, 
      &cfg.bootscreen, 0, 1, bool_options, 2, NULL, false },
    
    { "BOOTCORE", "Boot Core", "Core to load on startup", CAT_SYSTEM_BOOT, TYPE_STRING, 
      &cfg.bootcore, 0, 0, NULL, 0, NULL, false },
    
    { "BOOTCORE_TIMEOUT", "Boot Core Timeout", "Timeout before loading boot core", CAT_SYSTEM_BOOT, TYPE_INT, 
      &cfg.bootcore_timeout, 2, 30, NULL, 0, "sec", false },
    
    { "MENU_PAL", "Menu PAL Mode", "Use PAL mode for menu core", CAT_SYSTEM_BOOT, TYPE_BOOL, 
      &cfg.menu_pal, 0, 1, bool_options, 2, NULL, true },
    
    { "FONT", "Custom Font", "Custom font file path", CAT_SYSTEM_BOOT, TYPE_STRING, 
      &cfg.font, 0, 0, NULL, 0, NULL, true },
    
    { "LOGO", "Show Logo", "Display MiSTer logo", CAT_SYSTEM_BOOT, TYPE_BOOL, 
      &cfg.logo, 0, 1, bool_options, 2, NULL, false },
    
    { "OSD_TIMEOUT", "OSD Timeout", "Hide OSD after inactivity", CAT_SYSTEM_BOOT, TYPE_INT, 
      &cfg.osd_timeout, 0, 3600, NULL, 0, "sec", false },
    
    { "OSD_ROTATE", "OSD Rotation", "Rotate OSD display", CAT_SYSTEM_BOOT, TYPE_ENUM, 
      &cfg.osd_rotate, 0, 2, osd_rotate_options, 3, NULL, false },
    
    { "FB_SIZE", "Framebuffer Size", "Linux framebuffer size", CAT_SYSTEM_BOOT, TYPE_ENUM, 
      &cfg.fb_size, 0, 4, fb_size_options, 5, NULL, true },
    
    { "FB_TERMINAL", "Framebuffer Terminal", "Enable Linux terminal on HDMI", CAT_SYSTEM_BOOT, TYPE_BOOL, 
      &cfg.fb_terminal, 0, 1, bool_options, 2, NULL, true },
    
    { "RBF_HIDE_DATECODE", "Hide Core Dates", "Hide date codes in core names", CAT_SYSTEM_BOOT, TYPE_BOOL, 
      &cfg.rbf_hide_datecode, 0, 1, bool_options, 2, NULL, false },
    
    { "RECENTS", "Recent Files", "Track recently used files", CAT_SYSTEM_BOOT, TYPE_BOOL, 
      &cfg.recents, 0, 1, bool_options, 2, NULL, false },
    
    { "BROWSE_EXPAND", "Browse Expand", "Expand browse dialog", CAT_SYSTEM_BOOT, TYPE_BOOL, 
      &cfg.browse_expand, 0, 1, bool_options, 2, NULL, false },
    
    // ===== NETWORK & STORAGE SETTINGS =====
    { "SHARED_FOLDER", "Network Share", "CIFS/SMB network share path", CAT_NETWORK_STORAGE, TYPE_STRING, 
      &cfg.shared_folder, 0, 0, NULL, 0, NULL, false },
    
    { "WAITMOUNT", "Wait for Mount", "Devices to wait for before continuing", CAT_NETWORK_STORAGE, TYPE_STRING, 
      &cfg.waitmount, 0, 0, NULL, 0, NULL, false },
    
    // ===== ADVANCED SETTINGS =====
    { "KEYRAH_MODE", "Keyrah Mode", "Keyrah interface mode", CAT_ADVANCED, TYPE_HEX, 
      &cfg.keyrah_mode, 0, 0xFFFFFFFF, NULL, 0, NULL, true },
    
    { "LOG_FILE_ENTRY", "Log File Entry", "Enable file access logging", CAT_ADVANCED, TYPE_BOOL, 
      &cfg.log_file_entry, 0, 1, bool_options, 2, NULL, false },
    
    { "BT_AUTO_DISCONNECT", "BT Auto Disconnect", "Bluetooth auto-disconnect timeout", CAT_ADVANCED, TYPE_INT, 
      &cfg.bt_auto_disconnect, 0, 180, NULL, 0, "min", false },
    
    { "BT_RESET_BEFORE_PAIR", "BT Reset Before Pair", "Reset Bluetooth before pairing", CAT_ADVANCED, TYPE_BOOL, 
      &cfg.bt_reset_before_pair, 0, 1, bool_options, 2, NULL, false },
    
    { "VFILTER_DEFAULT", "Default Video Filter", "Default video filter file", CAT_ADVANCED, TYPE_STRING, 
      &cfg.vfilter_default, 0, 0, NULL, 0, NULL, false },
    
    { "VFILTER_VERTICAL_DEFAULT", "Default Vertical Filter", "Default vertical filter file", CAT_ADVANCED, TYPE_STRING, 
      &cfg.vfilter_vertical_default, 0, 0, NULL, 0, NULL, false },
    
    { "VFILTER_SCANLINES_DEFAULT", "Default Scanlines Filter", "Default scanlines filter file", CAT_ADVANCED, TYPE_STRING, 
      &cfg.vfilter_scanlines_default, 0, 0, NULL, 0, NULL, false },
    
    { "SHMASK_DEFAULT", "Default Shadow Mask", "Default shadow mask file", CAT_ADVANCED, TYPE_STRING, 
      &cfg.shmask_default, 0, 0, NULL, 0, NULL, false },
    
    { "SHMASK_MODE_DEFAULT", "Default Shadow Mask Mode", "Default shadow mask mode", CAT_ADVANCED, TYPE_INT, 
      &cfg.shmask_mode_default, 0, 255, NULL, 0, NULL, false },
    
    { "PRESET_DEFAULT", "Default Preset", "Default video preset file", CAT_ADVANCED, TYPE_STRING, 
      &cfg.preset_default, 0, 0, NULL, 0, NULL, false },
    
    // Special/complex settings that need custom handling
    { "DEADZONE", "Controller Deadzone", "Analog stick deadzone configuration", CAT_INPUT_CONTROLLERS, TYPE_CUSTOM, 
      &cfg.controller_deadzone, 0, 0, NULL, 0, NULL, false },
    
    { "CUSTOM_ASPECT_RATIO_1", "Custom Aspect Ratio 1", "First custom aspect ratio", CAT_VIDEO_DISPLAY, TYPE_STRING, 
      &cfg.custom_aspect_ratio[0], 0, 0, NULL, 0, NULL, false },
    
    { "CUSTOM_ASPECT_RATIO_2", "Custom Aspect Ratio 2", "Second custom aspect ratio", CAT_VIDEO_DISPLAY, TYPE_STRING, 
      &cfg.custom_aspect_ratio[1], 0, 0, NULL, 0, NULL, false },
};

static const int total_settings = sizeof(all_settings) / sizeof(all_settings[0]);

// Get category information
const osd_category_info_t* osd_get_category_info(osd_category_t category) {
    if (category >= CAT_COUNT) return NULL;
    return &category_info[category];
}

// Get all settings for a specific category
const osd_setting_def_t* osd_get_settings_for_category(osd_category_t category, int* count) {
    static osd_setting_def_t category_settings[100];  // Max settings per category
    int n = 0;
    
    for (int i = 0; i < total_settings; i++) {
        if (all_settings[i].category == category) {
            category_settings[n++] = all_settings[i];
            if (n >= 100) break;
        }
    }
    
    if (count) *count = n;
    return category_settings;
}

// Get a specific setting by ini name
const osd_setting_def_t* osd_get_setting_by_name(const char* ini_name) {
    for (int i = 0; i < total_settings; i++) {
        if (strcasecmp(all_settings[i].ini_name, ini_name) == 0) {
            return &all_settings[i];
        }
    }
    return NULL;
}

// Get total number of settings
int osd_get_total_settings(void) {
    return total_settings;
}