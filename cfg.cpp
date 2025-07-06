// cfg.c
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <strings.h>
#include "cfg.h"
#include "debug.h"
#include "file_io.h"
#include "user_io.h"
#include "video.h"
#include "osd.h"
#include "support/arcade/mra_loader.h"

cfg_t cfg;
static FILE *orig_stdout = NULL;
static FILE *dev_null = NULL;

// File picker support variables
char cfg_file_picker_initial_path[1024];
const ini_var_t* cfg_file_picker_current_setting = NULL;
int cfg_file_picker_return_state = 0;

// Types now defined in cfg.h

// Category information
static const osd_category_info_t category_info[CAT_COUNT] = {
	{ "Digital AV", "HDMI output and display settings" },
	{ "Analog AV", "VGA/YPbPr analog video settings" },
	{ "Controllers", "Gamepad and controller settings" },
	{ "Arcade Input", "Arcade stick and spinner settings" },
	{ "Keyboard & Mouse", "Keyboard and mouse settings" },
	{ "User Interface", "Menu and OSD settings" },
	{ "System", "System boot and core settings" },
	{ "Filters", "Video and audio filter defaults" }
};

const ini_var_t ini_vars[] =
{
	{ "YPBPR", (void*)(&(cfg.vga_mode_int)), INI_UINT8, 0, 1, "YPbPr Output", "Enable component video output (legacy)", CAT_AV_ANALOG, NULL, true, NULL, 0, 0, -1, MENU_BOTH, 0 },
	{ "COMPOSITE_SYNC", (void*)(&(cfg.csync)), INI_UINT8, 0, 1, "Composite Sync", "Enable composite sync on HSync or separate sync on Hsync and Vsync. Composite sync is best for most everything except PC CRTs.", CAT_AV_ANALOG, NULL, true, NULL, 0, 0, -1, MENU_MAIN, 0 },
	{ "FORCED_SCANDOUBLER", (void*)(&(cfg.forced_scandoubler)), INI_UINT8, 0, 1, "Force Scandoubler", "Scandouble 15kHz cores to 31kHz. Some cores don't have the scandoubler module (PSX, N64, etc.)", CAT_AV_ANALOG, NULL, true, NULL, 0, 0, 1, MENU_MAIN, 0 },
	{ "VGA_SCALER", (void*)(&(cfg.vga_scaler)), INI_UINT8, 0, 1, "VGA Scaler", "Use scaler for VGA/DVI output", CAT_AV_ANALOG, NULL, true, NULL, 0, 0, 2, MENU_MAIN, 0 },
	{ "VGA_SOG", (void*)(&(cfg.vga_sog)), INI_UINT8, 0, 1, "VGA Sync-on-Green", "Enable sync-on-green for VGA and YPbPr", CAT_AV_ANALOG, NULL, true, NULL, 0, 0, -1, MENU_MAIN, 0 },
	{ "SYNC_MODE", (void*)(&(cfg.sync_mode)), INI_UINT8, 0, 2, "Sync Mode", "Analog sync mode: Separate=HSync+VSync, Composite=HSync only, Sync-on-Green=embedded in green signal", CAT_AV_ANALOG, NULL, true, NULL, 0, 0, 3, MENU_MAIN, 0 },
	{ "KEYRAH_MODE", (void*)(&(cfg.keyrah_mode)), INI_HEX32, 0, 0xFFFFFFFF, "Keyrah Mode", "Keyrah interface mode", CAT_INPUT_KB_MOUSE, NULL, true, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "RESET_COMBO", (void*)(&(cfg.reset_combo)), INI_UINT8, 0, 3, "Reset Key Combo", "Keyboard combination for reset", CAT_INPUT_KB_MOUSE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "KEY_MENU_AS_RGUI", (void*)(&(cfg.key_menu_as_rgui)), INI_UINT8, 0, 1, "Menu Key as Right GUI", "Use Menu key as Right GUI", CAT_INPUT_KB_MOUSE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "VIDEO_MODE", (void*)(cfg.video_conf), INI_STRING, 0, sizeof(cfg.video_conf) - 1, "Video Mode", "Auto mode uses HDMI EDID to set optimal resolution. All other settings override the EDID value.", CAT_AV_DIGITAL, NULL, true, "DIRECT_VIDEO", 0, 0, 1, MENU_BOTH, 0 },
	{ "VIDEO_MODE_PAL", (void*)(cfg.video_conf_pal), INI_STRING, 0, sizeof(cfg.video_conf_pal) - 1, "Video Mode (PAL)", "Video mode for PAL cores", CAT_AV_DIGITAL, NULL, true, NULL, 0, 0, 25, MENU_BOTH, 0 },
	{ "VIDEO_MODE_NTSC", (void*)(cfg.video_conf_ntsc), INI_STRING, 0, sizeof(cfg.video_conf_ntsc) - 1, "Video Mode (NTSC)", "Video mode for NTSC cores", CAT_AV_DIGITAL, NULL, true, NULL, 0, 0, 26, MENU_BOTH, 0 },
	{ "VIDEO_INFO", (void*)(&(cfg.video_info)), INI_UINT8, 0, 10, "Video Info Display", "Show video information on screen", CAT_UI, "s", false, NULL, 0, 0, 2, MENU_BOTH, 0 },
	{ "VSYNC_ADJUST", (void*)(&(cfg.vsync_adjust)), INI_UINT8, 0, 2, "VSync Adjustment", "Automatic refresh rate adjustment. `3 buffer 60Hz` = robust sync with the most latency. `3 buffer match` = robust sync, matching the core's sync. `1 buffer match` = lowest latency but may not work with all cores on all displays.", CAT_AV_DIGITAL, NULL, false, "DIRECT_VIDEO", 0, 0, 4, MENU_BOTH, 0 },
	{ "HDMI_AUDIO_96K", (void*)(&(cfg.hdmi_audio_96k)), INI_UINT8, 0, 1, "HDMI 96kHz Audio", "Enable 96kHz audio output. May cause compatibility issues with AV equipment and DACs.", CAT_AV_DIGITAL, NULL, true, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "DVI_MODE", (void*)(&(cfg.dvi_mode)), INI_UINT8, 0, 1, "DVI Mode", "Disable HDMI features for DVI displays", CAT_AV_DIGITAL, NULL, true, "DIRECT_VIDEO", 0, 0, 4, MENU_MAIN, 0 },
	{ "HDMI_LIMITED", (void*)(&(cfg.hdmi_limited)), INI_UINT8, 0, 2, "HDMI Color Range", "HDMI color range. Set full for most devices. Limited (16-235) for older displays. Limited (16-255) for some HDMI DACs.", CAT_AV_DIGITAL, NULL, true, NULL, 0, 0, 7, MENU_MAIN, 0 },
	{ "KBD_NOMOUSE", (void*)(&(cfg.kbd_nomouse)), INI_UINT8, 0, 1, "Disable Mouse", "Disable mouse emulation via keyboard", CAT_INPUT_KB_MOUSE, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "MOUSE_THROTTLE", (void*)(&(cfg.mouse_throttle)), INI_UINT8, 1, 100, "Mouse Throttle", "Mouse movement speed", CAT_INPUT_KB_MOUSE, "%", false, NULL, 0, 0, 99, MENU_BOTH, 5 },
	{ "BOOTSCREEN", (void*)(&(cfg.bootscreen)), INI_UINT8, 0, 1, "Boot Screen", "Show boot screen on startup", CAT_UI, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "VSCALE_MODE", (void*)(&(cfg.vscale_mode)), INI_UINT8, 0, 5, "Vertical Scale Mode", "Vertical scaling algorithm", CAT_AV_DIGITAL, NULL, false, "DIRECT_VIDEO", 0, 0, 3, MENU_BOTH, 0 },
	{ "VSCALE_BORDER", (void*)(&(cfg.vscale_border)), INI_UINT16, 0, 399, "Vertical Scale Border", "Border size for scaled image", CAT_AV_DIGITAL, "px", false, "DIRECT_VIDEO", 0, 0, 22, MENU_BOTH, 10 },
	{ "RBF_HIDE_DATECODE", (void*)(&(cfg.rbf_hide_datecode)), INI_UINT8, 0, 1, "Hide Core Dates", "Hide date codes in core names", CAT_UI, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "MENU_PAL", (void*)(&(cfg.menu_pal)), INI_UINT8, 0, 1, "Menu PAL Mode", "Use PAL mode for menu core", CAT_AV_DIGITAL, NULL, true, NULL, 0, 0, 98, MENU_MAIN, 0 },
	{ "BOOTCORE", (void*)(&(cfg.bootcore)), INI_STRING, 0, sizeof(cfg.bootcore) - 1, "Boot Core", "Core to load on startup", CAT_SYSTEM, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "BOOTCORE_TIMEOUT", (void*)(&(cfg.bootcore_timeout)), INI_INT16, 2, 30, "Boot Core Timeout", "Timeout before loading boot core", CAT_SYSTEM, "sec", false, "BOOTCORE", 1, 1, 99, MENU_MAIN, 1 },
	{ "FONT", (void*)(&(cfg.font)), INI_STRING, 0, sizeof(cfg.font) - 1, "Custom Font", "Custom font file path", CAT_UI, NULL, true, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "FB_SIZE", (void*)(&(cfg.fb_size)), INI_UINT8, 0, 4, "Framebuffer Size", "Linux framebuffer size", CAT_UI, NULL, true, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "FB_TERMINAL", (void*)(&(cfg.fb_terminal)), INI_UINT8, 0, 1, "Framebuffer Terminal", "Enable Linux terminal on HDMI and scaled analog video.", CAT_UI, NULL, true, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "OSD_TIMEOUT", (void*)(&(cfg.osd_timeout)), INI_INT16, 0, 3600, "OSD Timeout", "Hide OSD after inactivity.", CAT_UI, "sec", false, NULL, 0, 0, 99, MENU_MAIN, 30 },
	{ "DIRECT_VIDEO", (void*)(&(cfg.direct_video)), INI_UINT8, 0, 1, "Direct Video", "Bypass scaler for compatible displays and HDMI DACs.", CAT_AV_DIGITAL, NULL, true, NULL, 0, 0, 0, MENU_MAIN, 0 },
	{ "OSD_ROTATE", (void*)(&(cfg.osd_rotate)), INI_UINT8, 0, 2, "OSD Rotation", "Off (Yoko), 1=90° Clockwise (Tate), 2=90° Counter-Clockwise (Tate)", CAT_UI, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "DEADZONE", (void*)(&(cfg.controller_deadzone)), INI_STRINGARR, sizeof(cfg.controller_deadzone) / sizeof(*cfg.controller_deadzone), sizeof(*cfg.controller_deadzone), "Controller Deadzone", "Analog stick deadzone configuration", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "GAMEPAD_DEFAULTS", (void*)(&(cfg.gamepad_defaults)), INI_UINT8, 0, 1, "Gamepad Defaults", "'Name' means Xbox 'A' button is mapped to SNES 'A' button. 'Positional' means Xbox 'A' button is mapped to SNES 'B' button.", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "RECENTS", (void*)(&(cfg.recents)), INI_UINT8, 0, 1, "Recent Files", "Track recently used files", CAT_UI, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "CONTROLLER_INFO", (void*)(&(cfg.controller_info)), INI_UINT8, 0, 60, "Controller Info", "Display controller information when a new core is loaded.", CAT_INPUT_CONTROLLER, "s", false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "REFRESH_MIN", (void*)(&(cfg.refresh_min)), INI_FLOAT, 0, 150, "Minimum Refresh Rate", "Minimum allowed refresh rate", CAT_AV_DIGITAL, "Hz", false, NULL, 0, 0, 23, MENU_BOTH, 1 },
	{ "REFRESH_MAX", (void*)(&(cfg.refresh_max)), INI_FLOAT, 0, 150, "Maximum Refresh Rate", "Maximum allowed refresh rate", CAT_AV_DIGITAL, "Hz", false, NULL, 0, 0, 24, MENU_BOTH, 1 },
	{ "JAMMA_VID", (void*)(&(cfg.jamma_vid)), INI_HEX16, 0, 0xFFFF, "JAMMA VID", "JAMMA interface vendor ID", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "JAMMA_PID", (void*)(&(cfg.jamma_pid)), INI_HEX16, 0, 0xFFFF, "JAMMA PID", "JAMMA interface product ID", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "JAMMA2_VID", (void*)(&(cfg.jamma2_vid)), INI_HEX16, 0, 0xFFFF, "JAMMA2 VID", "Second JAMMA interface vendor ID", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "JAMMA2_PID", (void*)(&(cfg.jamma2_pid)), INI_HEX16, 0, 0xFFFF, "JAMMA2 PID", "Second JAMMA interface product ID", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "SNIPER_MODE", (void*)(&(cfg.sniper_mode)), INI_UINT8, 0, 1, "Sniper Mode", "Enable precision aiming mode", CAT_INPUT_KB_MOUSE, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "WHEEL_FORCE", (void*)(&(cfg.wheel_force)), INI_UINT8, 0, 100, "Wheel Force", "Force feedback strength for steering wheels", CAT_INPUT_CONTROLLER, "%", false, NULL, 0, 0, 99, MENU_BOTH, 5 },
	{ "BROWSE_EXPAND", (void*)(&(cfg.browse_expand)), INI_UINT8, 0, 1, "Browse Expand", "Expand file browser by default", CAT_UI, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "LOGO", (void*)(&(cfg.logo)), INI_UINT8, 0, 1, "Show Logo", "Display MiSTer logo on startup", CAT_UI, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "SHARED_FOLDER", (void*)(&(cfg.shared_folder)), INI_STRING, 0, sizeof(cfg.shared_folder) - 1, "Shared Folder", "Network shared folder path", CAT_SYSTEM, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "NO_MERGE_VID", (void*)(&(cfg.no_merge_vid)), INI_HEX16, 0, 0xFFFF, "No Merge VID", "USB device vendor ID to prevent merging", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, -1, MENU_BOTH, 0 },
	{ "NO_MERGE_PID", (void*)(&(cfg.no_merge_pid)), INI_HEX16, 0, 0xFFFF, "No Merge PID", "USB device product ID to prevent merging", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, -1, MENU_BOTH, 0 },
	{ "NO_MERGE_VIDPID", (void*)(cfg.no_merge_vidpid), INI_HEX32ARR, 0, 0xFFFFFFFF, "No Merge VID:PID", "USB VID:PID pairs to prevent merging", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "CUSTOM_ASPECT_RATIO_1", (void*)(&(cfg.custom_aspect_ratio[0])), INI_STRING, 0, sizeof(cfg.custom_aspect_ratio[0]) - 1, "Custom Aspect Ratio 1", "First custom aspect ratio", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 20, MENU_BOTH, 0 },
	{ "CUSTOM_ASPECT_RATIO_2", (void*)(&(cfg.custom_aspect_ratio[1])), INI_STRING, 0, sizeof(cfg.custom_aspect_ratio[1]) - 1, "Custom Aspect Ratio 2", "Second custom aspect ratio", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 21, MENU_BOTH, 0 },
	{ "SPINNER_VID", (void*)(&(cfg.spinner_vid)), INI_HEX16, 0, 0xFFFF, "Spinner VID", "Spinner device vendor ID", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "SPINNER_PID", (void*)(&(cfg.spinner_pid)), INI_HEX16, 0, 0xFFFF, "Spinner PID", "Spinner device product ID", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "SPINNER_AXIS", (void*)(&(cfg.spinner_axis)), INI_UINT8, 0, 2, "Spinner Axis", "Spinner axis configuration", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "SPINNER_THROTTLE", (void*)(&(cfg.spinner_throttle)), INI_INT32, -10000, 10000, "Spinner Throttle", "Spinner sensitivity adjustment", CAT_INPUT_ARCADE, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "AFILTER_DEFAULT", (void*)(&(cfg.afilter_default)), INI_STRING, 0, sizeof(cfg.afilter_default) - 1, "Default Audio Filter", "Default audio filter file", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "VFILTER_DEFAULT", (void*)(&(cfg.vfilter_default)), INI_STRING, 0, sizeof(cfg.vfilter_default) - 1, "Default Video Filter", "Default video filter file", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "VFILTER_VERTICAL_DEFAULT", (void*)(&(cfg.vfilter_vertical_default)), INI_STRING, 0, sizeof(cfg.vfilter_vertical_default) - 1, "Default Vertical Filter", "Default vertical filter file", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "VFILTER_SCANLINES_DEFAULT", (void*)(&(cfg.vfilter_scanlines_default)), INI_STRING, 0, sizeof(cfg.vfilter_scanlines_default) - 1, "Default Scanlines Filter", "Default scanlines filter file", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "SHMASK_DEFAULT", (void*)(&(cfg.shmask_default)), INI_STRING, 0, sizeof(cfg.shmask_default) - 1, "Default Shadow Mask", "Default shadow mask file", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "SHMASK_MODE_DEFAULT", (void*)(&(cfg.shmask_mode_default)), INI_UINT8, 0, 255, "Default Shadow Mask Mode", "Default shadow mask mode", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "PRESET_DEFAULT", (void*)(&(cfg.preset_default)), INI_STRING, 0, sizeof(cfg.preset_default) - 1, "Default Preset", "Default video preset file", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "LOG_FILE_ENTRY", (void*)(&(cfg.log_file_entry)), INI_UINT8, 0, 1, "Log File Entry", "Enable file access logging", CAT_SYSTEM, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "BT_AUTO_DISCONNECT", (void*)(&(cfg.bt_auto_disconnect)), INI_UINT32, 0, 180, "BT Auto Disconnect", "Bluetooth auto-disconnect timeout", CAT_INPUT_CONTROLLER, "min", false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "BT_RESET_BEFORE_PAIR", (void*)(&(cfg.bt_reset_before_pair)), INI_UINT8, 0, 1, "BT Reset Before Pair", "Reset Bluetooth before pairing", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "WAITMOUNT", (void*)(&(cfg.waitmount)), INI_STRING, 0, sizeof(cfg.waitmount) - 1, "Wait for Mount", "Devices to wait for before continuing", CAT_SYSTEM, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "RUMBLE", (void *)(&(cfg.rumble)), INI_UINT8, 0, 1, "Controller Rumble", "Enable force feedback/rumble", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "WHEEL_RANGE", (void*)(&(cfg.wheel_range)), INI_UINT16, 0, 1000, "Wheel Range", "Steering wheel rotation range", CAT_INPUT_CONTROLLER, "°", false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "HDMI_GAME_MODE", (void *)(&(cfg.hdmi_game_mode)), INI_UINT8, 0, 1, "HDMI Game Mode", "Enable low-latency game mode", CAT_AV_DIGITAL, NULL, false, "DIRECT_VIDEO", 0, 0, 5, MENU_BOTH, 0 },
	{ "VRR_MODE", (void *)(&(cfg.vrr_mode)), INI_UINT8, 0, 3, "Variable Refresh Rate", "VRR mode selection", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 15, MENU_BOTH, 0 },
	{ "VRR_MIN_FRAMERATE", (void *)(&(cfg.vrr_min_framerate)), INI_UINT8, 0, 240, "VRR Min Framerate", "Minimum VRR framerate", CAT_AV_DIGITAL, "Hz", false, "VRR_MODE", 1, 3, 17, MENU_BOTH, 0 },
	{ "VRR_MAX_FRAMERATE", (void *)(&(cfg.vrr_max_framerate)), INI_UINT8, 0, 240, "VRR Max Framerate", "Maximum VRR framerate", CAT_AV_DIGITAL, "Hz", false, "VRR_MODE", 1, 3, 16, MENU_BOTH, 0 },
	{ "VRR_VESA_FRAMERATE", (void *)(&(cfg.vrr_vesa_framerate)), INI_UINT8, 0, 240, "VRR VESA Framerate", "VESA VRR base framerate", CAT_AV_DIGITAL, "Hz", false, "VRR_MODE", 1, 3, 19, MENU_BOTH, 0 },
	{ "VIDEO_OFF", (void*)(&(cfg.video_off)), INI_INT16, 0, 3600, "Video Off Timeout", "Turn off video after inactivity", CAT_UI, "sec", false, NULL, 0, 0, 99, MENU_MAIN, 15 },
	{ "PLAYER_1_CONTROLLER", (void*)(&(cfg.player_controller[0])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 1 Controller", "Controller mapping for player 1", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "PLAYER_2_CONTROLLER", (void*)(&(cfg.player_controller[1])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 2 Controller", "Controller mapping for player 2", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "PLAYER_3_CONTROLLER", (void*)(&(cfg.player_controller[2])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 3 Controller", "Controller mapping for player 3", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "PLAYER_4_CONTROLLER", (void*)(&(cfg.player_controller[3])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 4 Controller", "Controller mapping for player 4", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "PLAYER_5_CONTROLLER", (void*)(&(cfg.player_controller[4])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 5 Controller", "Controller mapping for player 5", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "PLAYER_6_CONTROLLER", (void*)(&(cfg.player_controller[5])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 6 Controller", "Controller mapping for player 6", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "DISABLE_AUTOFIRE", (void *)(&(cfg.disable_autofire)), INI_UINT8, 0, 1, "Disable Autofire", "Disable autofire functionality", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "VIDEO_BRIGHTNESS", (void *)(&(cfg.video_brightness)), INI_UINT8, 0, 100, "Video Brightness", "Adjust video brightness", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 8, MENU_BOTH, 5 },
	{ "VIDEO_CONTRAST", (void *)(&(cfg.video_contrast)), INI_UINT8, 0, 100, "Video Contrast", "Adjust video contrast", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 9, MENU_BOTH, 5 },
	{ "VIDEO_SATURATION", (void *)(&(cfg.video_saturation)), INI_UINT8, 0, 100, "Video Saturation", "Adjust video saturation", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 10, MENU_BOTH, 5 },
	{ "VIDEO_HUE", (void *)(&(cfg.video_hue)), INI_UINT16, 0, 360, "Video Hue", "Adjust video hue", CAT_AV_DIGITAL, "°", false, NULL, 0, 0, 10, MENU_BOTH, 0 },
	{ "VIDEO_GAIN_OFFSET", (void *)(&(cfg.video_gain_offset)), INI_STRING, 0, sizeof(cfg.video_gain_offset), "Video Gain/Offset", "RGB gain and offset adjustments", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 12, MENU_BOTH, 0 },
	{ "HDR", (void*)(&cfg.hdr), INI_UINT8, 0, 2, "HDR Mode", "High Dynamic Range mode", CAT_AV_DIGITAL, NULL, false, NULL, 0, 0, 13, MENU_BOTH, 0 },
	{ "HDR_MAX_NITS", (void*)(&(cfg.hdr_max_nits)), INI_UINT16, 100, 10000, "HDR Max Brightness", "Maximum HDR brightness", CAT_AV_DIGITAL, "nits", false, "HDR", 1, 2, 14, MENU_BOTH, 0 },
	{ "HDR_AVG_NITS", (void*)(&(cfg.hdr_avg_nits)), INI_UINT16, 100, 10000, "HDR Average Brightness", "Average HDR brightness", CAT_AV_DIGITAL, "nits", false, "HDR", 1, 2, 15, MENU_BOTH, 0 },
	{ "VGA_MODE", (void*)(&(cfg.vga_mode)), INI_STRING, 0, sizeof(cfg.vga_mode) - 1, "VGA Mode", "Analog video output mode.", CAT_AV_ANALOG, NULL, true, NULL, 0, 0, 0, MENU_MAIN, 0 },
	{ "NTSC_MODE", (void *)(&(cfg.ntsc_mode)), INI_UINT8, 0, 2, "NTSC Mode", "NTSC color encoding mode", CAT_AV_ANALOG, NULL, false, "YPBPR", 2, 3, 99, MENU_MAIN, 0 },
	{ "CONTROLLER_UNIQUE_MAPPING", (void *)(cfg.controller_unique_mapping), INI_UINT32ARR, 0, 0xFFFFFFFF, "Unique Controller Mapping", "Controller-specific button mappings", CAT_INPUT_CONTROLLER, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "OSD_LOCK", (void*)(&(cfg.osd_lock)), INI_STRING, 0, sizeof(cfg.osd_lock) - 1, "OSD Lock", "Lock OSD with password", CAT_UI, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "OSD_LOCK_TIME", (void*)(&(cfg.osd_lock_time)), INI_UINT16, 0, 60, "OSD Lock Time", "Time before OSD locks", CAT_UI, "sec", false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "DEBUG", (void *)(&(cfg.debug)), INI_UINT8, 0, 1, "Debug Mode", "Enable debug output", CAT_SYSTEM, NULL, false, NULL, 0, 0, 99, MENU_BOTH, 0 },
	{ "MAIN", (void*)(&(cfg.main)), INI_STRING, 0, sizeof(cfg.main) - 1, "Main Directory", "Main MiSTer directory name", CAT_SYSTEM, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
	{ "VFILTER_INTERLACE_DEFAULT", (void*)(&(cfg.vfilter_interlace_default)), INI_STRING, 0, sizeof(cfg.vfilter_interlace_default) - 1, "Default Interlace Filter", "Default interlace filter file", CAT_FILTERS, NULL, false, NULL, 0, 0, 99, MENU_MAIN, 0 },
};

const int nvars = (int)(sizeof(ini_vars) / sizeof(ini_var_t));

// Helper functions for OSD integration
const osd_category_info_t* cfg_get_category_info(osd_category_t category)
{
	if (category >= 0 && category < CAT_COUNT)
		return &category_info[category];
	return NULL;
}

// Get all settings for a specific category
int cfg_get_settings_for_category(osd_category_t category, const ini_var_t*** settings, int* count, menu_flags_t menu_type)
{
	static const ini_var_t* category_settings[256]; // Max settings per category
	int setting_count = 0;
	
	// Iterate through all ini_vars and collect ones matching the category and menu type
	for (int i = 0; i < nvars; i++)
	{
		if (ini_vars[i].category == category && 
		    ini_vars[i].menu_position != -1 && // Skip hidden settings
		    (ini_vars[i].menu_flags & menu_type)) // Check if setting appears in this menu type
		{
			category_settings[setting_count++] = &ini_vars[i];
			if (setting_count >= 256) break; // Prevent overflow
		}
	}
	
	// Sort by menu_position (0=first, 99=auto-sort by name, 255=last)
	for (int i = 0; i < setting_count - 1; i++)
	{
		for (int j = i + 1; j < setting_count; j++)
		{
			const ini_var_t* a = category_settings[i];
			const ini_var_t* b = category_settings[j];
			
			// Compare menu_position first
			if (a->menu_position != b->menu_position)
			{
				if (a->menu_position > b->menu_position)
				{
					category_settings[i] = b;
					category_settings[j] = a;
				}
			}
			// If same menu_position (usually 99), sort alphabetically by display name
			else if (strcmp(a->display_name, b->display_name) > 0)
			{
				category_settings[i] = b;
				category_settings[j] = a;
			}
		}
	}
	
	if (settings) *settings = category_settings;
	if (count) *count = setting_count;
	
	return setting_count;
}

// Auto-detect UI type based on variable characteristics
ui_type_t cfg_auto_detect_ui_type(const ini_var_t* var)
{
	if (!var) return UI_HIDDEN;
	
	// Special cases based on name patterns
	if (strstr(var->name, "DEFAULT") || strstr(var->name, "FILTER") || 
	    strstr(var->name, "FONT") || strstr(var->name, "PRESET"))
		return UI_FILE_PICKER;
	
	// Based on type and range
	switch (var->type)
	{
		case INI_UINT8:
		case INI_INT8:
			// Boolean: 0-1 range = checkbox
			if (var->min == 0 && var->max == 1)
				return UI_CHECKBOX;
			// Small enumeration: 0-5 range = dropdown
			else if (var->max - var->min <= 5)
				return UI_DROPDOWN;
			// Larger range = slider
			else
				return UI_SLIDER;
			
		case INI_UINT16:
		case INI_INT16:
		case INI_UINT32:
		case INI_INT32:
		case INI_FLOAT:
			// Numeric ranges = slider
			return UI_SLIDER;
			
		case INI_STRING:
			// Video modes and similar = dropdown (if we had options)
			// For now, default to string input
			return UI_STRING_INPUT;
			
		case INI_HEX8:
		case INI_HEX16:
		case INI_HEX32:
			// Hex values = usually hidden advanced settings
			return UI_HIDDEN;
			
		default:
			return UI_HIDDEN;
	}
}

// Auto-detect which menus this setting should appear in
menu_flags_t cfg_auto_detect_menu_flags(const ini_var_t* var)
{
	if (!var) return MENU_NONE;
	
	// Advanced settings (requires_reboot=true) typically not in core menus
	if (var->requires_reboot)
		return MENU_MAIN;
	
	// Settings that make sense for core overrides
	switch (var->category)
	{
		case CAT_AV_DIGITAL:
		case CAT_AV_ANALOG:
		case CAT_INPUT_CONTROLLER:
		case CAT_INPUT_ARCADE:
		case CAT_INPUT_KB_MOUSE:
		case CAT_UI:
			return MENU_BOTH;  // Both main and core menus
			
		case CAT_SYSTEM:
		case CAT_FILTERS:
		default:
			return MENU_MAIN;  // Main menu only
	}
}

// Get custom step size for specific slider settings  
uint8_t cfg_get_custom_step_size(const char* setting_name)
{
	if (!setting_name) return 0;
	
	// Define custom step sizes for SLIDER settings only
	if (strcmp(setting_name, "MOUSE_THROTTLE") == 0) return 5;
	if (strcmp(setting_name, "CONTROLLER_INFO") == 0) return 1;
	if (strcmp(setting_name, "VIDEO_INFO") == 0) return 1;
	if (strcmp(setting_name, "WHEEL_FORCE") == 0) return 5;
	if (strcmp(setting_name, "VIDEO_BRIGHTNESS") == 0) return 5;
	if (strcmp(setting_name, "VIDEO_CONTRAST") == 0) return 5;
	if (strcmp(setting_name, "VIDEO_SATURATION") == 0) return 5;
	if (strcmp(setting_name, "VIDEO_HUE") == 0) return 10;
	if (strcmp(setting_name, "VRR_MIN_FRAMERATE") == 0) return 10;
	if (strcmp(setting_name, "VRR_MAX_FRAMERATE") == 0) return 10;
	if (strcmp(setting_name, "VRR_VESA_FRAMERATE") == 0) return 10;
	if (strcmp(setting_name, "BOOTCORE_TIMEOUT") == 0) return 1;
	if (strcmp(setting_name, "OSD_TIMEOUT") == 0) return 30;
	if (strcmp(setting_name, "WHEEL_RANGE") == 0) return 10;
	if (strcmp(setting_name, "VSCALE_BORDER") == 0) return 10;
	if (strcmp(setting_name, "SPINNER_THROTTLE") == 0) return 10;
	if (strcmp(setting_name, "BT_AUTO_DISCONNECT") == 0) return 5;
	if (strcmp(setting_name, "HDR_MAX_NITS") == 0) return 100;
	if (strcmp(setting_name, "HDR_AVG_NITS") == 0) return 25;
	
	return 0; // Use auto-detection
}

// Forward declarations for temp value helpers
static bool get_temp_uint8_value(const char* setting_name, uint8_t* value);

// Check if a setting is enabled/available based on dependencies
bool cfg_is_setting_enabled(const char* setting_name)
{
	if (!setting_name) return true;
	
	// Special case: Sync mode filtering based on VGA mode
	if (strcmp(setting_name, "SYNC_MODE") == 0)
	{
		// Get current VGA mode (check temp settings first)
		uint8_t vga_mode_int = cfg.vga_mode_int;
		uint8_t temp_vga_mode;
		if (get_temp_uint8_value("YPBPR", &temp_vga_mode))
		{
			vga_mode_int = temp_vga_mode;
		}
		
		// For S-Video and CVBS (vga_mode >= 2), sync mode is locked to composite
		// But we don't disable it, we just prevent changing it
		return true; // Always show but will be locked in S-Video/CVBS mode
	}
	
	// Find the setting in ini_vars to check for dependencies
	const ini_var_t* var = cfg_get_ini_var(setting_name);
	if (!var || !var->depends_on) 
	{
		return true; // No dependency, always enabled
	}
	
	// Get the dependency setting
	const ini_var_t* dep_var = cfg_get_ini_var(var->depends_on);
	if (!dep_var) 
	{
		return true; // Dependency setting not found, assume enabled
	}
	
	// Get current value of dependency setting (check temp values first)
	int64_t dep_value = 0;
	
	// Check for temp value first
	uint8_t temp_uint8;
	if (dep_var->type == INI_UINT8 && get_temp_uint8_value(dep_var->name, &temp_uint8))
	{
		dep_value = temp_uint8;
	}
	else
	{
		// Fall back to memory value
		switch (dep_var->type)
		{
			case INI_UINT8:
				dep_value = *(uint8_t*)dep_var->var;
				break;
			case INI_INT8:
				dep_value = *(int8_t*)dep_var->var;
				break;
			case INI_UINT16:
				dep_value = *(uint16_t*)dep_var->var;
				break;
			case INI_INT16:
				dep_value = *(int16_t*)dep_var->var;
				break;
			case INI_UINT32:
				dep_value = *(uint32_t*)dep_var->var;
				break;
			case INI_INT32:
				dep_value = *(int32_t*)dep_var->var;
				break;
			case INI_HEX8:
				dep_value = *(uint8_t*)dep_var->var;
				break;
			case INI_HEX16:
				dep_value = *(uint16_t*)dep_var->var;
				break;
			case INI_HEX32:
				dep_value = *(uint32_t*)dep_var->var;
				break;
			case INI_FLOAT:
				dep_value = (int64_t)*(float*)dep_var->var;
				break;
			case INI_STRING:
				// For strings, check if non-empty (1) or empty (0)
				{
					const char* str_val = (const char*)dep_var->var;
					dep_value = (str_val && str_val[0] != '\0') ? 1 : 0;
				}
				break;
			default:
				return true; // Unsupported dependency type, assume enabled
		}
	}
	
	// Check if dependency value is within the required range
	return (dep_value >= var->depends_min && dep_value <= var->depends_max);
}

// Auto-detect appropriate step size for sliders
uint8_t cfg_auto_detect_step_size(const ini_var_t* var)
{
	if (!var) return 1;
	
	// Use step_size from ini_var if specified (non-zero)
	if (var->step_size > 0)
		return var->step_size;
	
	// Check for custom step size first
	uint8_t custom_step = cfg_get_custom_step_size(var->name);
	if (custom_step > 0)
		return custom_step;
	
	int64_t range = var->max - var->min;
	
	// Auto-detect based on range
	if (range > 100)
		return 10;
	else if (range > 20)
		return 5;
	else
		return 1;
}

// Helper functions for debugging and development
const char* cfg_ui_type_to_string(ui_type_t ui_type)
{
	switch (ui_type)
	{
		case UI_CHECKBOX: return "CHECKBOX";
		case UI_SLIDER: return "SLIDER";
		case UI_DROPDOWN: return "DROPDOWN";
		case UI_FILE_PICKER: return "FILE_PICKER";
		case UI_STRING_INPUT: return "STRING_INPUT";
		case UI_HIDDEN: return "HIDDEN";
		default: return "UNKNOWN";
	}
}

const char* cfg_menu_flags_to_string(menu_flags_t flags)
{
	switch (flags)
	{
		case MENU_NONE: return "NONE";
		case MENU_MAIN: return "MAIN";
		case MENU_CORE: return "CORE";
		case MENU_BOTH: return "BOTH";
		default: return "UNKNOWN";
	}
}

// Print analysis of how UI types would be auto-detected for existing settings
void cfg_print_ui_analysis(void)
{
	printf("=== UI Type Auto-Detection Analysis ===\n");
	printf("%-20s %-15s %-10s %-8s %s\n", "Setting", "UI Type", "Menu", "Step", "Description");
	printf("%-20s %-15s %-10s %-8s %s\n", "-------", "-------", "----", "----", "-----------");
	
	for (int i = 0; i < nvars && i < 20; i++) // Show first 20 for demo
	{
		const ini_var_t* var = &ini_vars[i];
		ui_type_t ui_type = cfg_auto_detect_ui_type(var);
		menu_flags_t menu_flags = cfg_auto_detect_menu_flags(var);
		uint8_t step_size = cfg_auto_detect_step_size(var);
		
		printf("%-20s %-15s %-10s %-8d %s\n", 
			var->name,
			cfg_ui_type_to_string(ui_type),
			cfg_menu_flags_to_string(menu_flags),
			step_size,
			var->display_name ? var->display_name : "");
	}
	printf("... (showing first 20 of %d total settings)\n", nvars);
}


// Find ini_var entry by name
const ini_var_t* cfg_get_ini_var(const char* name)
{
	if (!name) return NULL;
	
	for (int i = 0; i < nvars; i++)
	{
		if (!strcmp(ini_vars[i].name, name))
		{
			return &ini_vars[i];
		}
	}
	
	return NULL;
}

// Get help text for a setting by key name using ini_vars
const char* cfg_get_help_text(const char* setting_key)
{
	if (!setting_key) return NULL;
	
	// Try to find in ini_vars (primary source)
	const ini_var_t* var = cfg_get_ini_var(setting_key);
	if (var && var->description)
	{
		return var->description;
	}
	
	// Return default help text if no specific help found
	return "Use left/right arrows to change this setting value";
}

// Override table for settings that need custom display logic
typedef struct {
	const char* setting_name;
	const char* (*render_func)(const char* setting_name, const char* display_name, char* buffer, size_t buffer_size);
} setting_override_t;

// Custom render function for DIRECT_VIDEO (HDMI Mode abstraction)
// Forward declaration for temp value helper
static bool get_temp_string_value(const char* setting_name, char* buffer, size_t buffer_size);

static const char* render_direct_video(const char* setting_name, const char* display_name, char* buffer, size_t buffer_size)
{
	// Get value from temp settings if available, otherwise use memory
	uint8_t direct_video_value = cfg.direct_video; // Default to memory value
	
	// Check for temp value
	uint8_t temp_value;
	if (get_temp_uint8_value("DIRECT_VIDEO", &temp_value))
	{
		direct_video_value = temp_value;
		printf("DEBUG: render_direct_video using temp value %d\n", direct_video_value);
	}
	
	// Show as "HDMI Mode" with HDMI/HDMI DAC abstraction instead of raw Direct Video On/Off
	const char* mode_text = direct_video_value ? "HDMI DAC" : "HDMI";
	snprintf(buffer, buffer_size, " %s: %s", display_name, mode_text);
	return buffer;
}

// Custom render function for VRR_MODE
static const char* render_vrr_mode(const char* setting_name, const char* display_name, char* buffer, size_t buffer_size)
{
	// Get value from temp settings if available, otherwise use memory
	uint8_t vrr_mode_value = cfg.vrr_mode; // Default to memory value
	
	// Check for temp value
	uint8_t temp_value;
	if (get_temp_uint8_value("VRR_MODE", &temp_value))
	{
		vrr_mode_value = temp_value;
	}
	
	const char* vrr_text = "Off";
	switch (vrr_mode_value) {
		case 1: vrr_text = "Freesync"; break;
		case 2: vrr_text = "Freesync+"; break;
		case 3: vrr_text = "HDMI VRR"; break;
	}
	snprintf(buffer, buffer_size, " %s: %s", display_name, vrr_text);
	return buffer;
}

// Custom render function for SNIPER_MODE
static const char* render_sniper_mode(const char* setting_name, const char* display_name, char* buffer, size_t buffer_size)
{
	// Get value from temp settings if available, otherwise use memory
	uint8_t sniper_mode_value = cfg.sniper_mode; // Default to memory value
	
	// Check for temp value
	uint8_t temp_value;
	if (get_temp_uint8_value("SNIPER_MODE", &temp_value))
	{
		sniper_mode_value = temp_value;
	}
	
	const char* mode_text = sniper_mode_value ? "Swap" : "Norm";
	snprintf(buffer, buffer_size, " %s: %s", display_name, mode_text);
	return buffer;
}

// Custom render function for GAMEPAD_DEFAULTS
static const char* render_gamepad_defaults(const char* setting_name, const char* display_name, char* buffer, size_t buffer_size)
{
	// Get value from temp settings if available, otherwise use memory
	uint8_t gamepad_defaults_value = cfg.gamepad_defaults; // Default to memory value
	
	// Check for temp value
	uint8_t temp_value;
	if (get_temp_uint8_value("GAMEPAD_DEFAULTS", &temp_value))
	{
		gamepad_defaults_value = temp_value;
	}
	
	const char* mode_text = gamepad_defaults_value ? "Pos." : "Name";
	snprintf(buffer, buffer_size, " %s: %s", display_name, mode_text);
	return buffer;
}

// Get current VGA mode (checking temp settings first)
static uint8_t get_current_vga_mode(void)
{
	uint8_t vga_mode_int = cfg.vga_mode_int;
	
	// Check for temp VGA_MODE string changes
	char temp_vga_mode[64];
	if (get_temp_string_value("VGA_MODE", temp_vga_mode, sizeof(temp_vga_mode)))
	{
		// Convert string to int
		if (strcasecmp(temp_vga_mode, "rgb") == 0 || strlen(temp_vga_mode) == 0) vga_mode_int = 0;
		else if (strcasecmp(temp_vga_mode, "ypbpr") == 0) vga_mode_int = 1;
		else if (strcasecmp(temp_vga_mode, "svideo") == 0) vga_mode_int = 2;
		else if (strcasecmp(temp_vga_mode, "cvbs") == 0) vga_mode_int = 3;
	}
	
	return vga_mode_int;
}

// Check if sync mode is locked based on VGA mode
static bool is_sync_mode_locked(void)
{
	// Sync mode is locked in S-Video and CVBS modes (vga_mode >= 2)
	return (get_current_vga_mode() >= 2);
}

// Check if a setting should be stippled (disabled but visible) based on dependencies
static bool is_setting_stippled(const char* setting_name)
{
	uint8_t vga_mode = get_current_vga_mode();
	
	// S-Video and CVBS (vga_mode >= 2) can't use scandoubler or scaler
	if (vga_mode >= 2)
	{
		if (strcmp(setting_name, "FORCED_SCANDOUBLER") == 0 ||
		    strcmp(setting_name, "VGA_SCALER") == 0)
		{
			return true;
		}
	}
	
	// NTSC_MODE is only available in S-Video and CVBS modes (vga_mode >= 2)
	if (strcmp(setting_name, "NTSC_MODE") == 0)
	{
		if (vga_mode < 2) // RGB and YPbPr modes
		{
			return true;
		}
	}
	
	// HDMI settings that depend on DIRECT_VIDEO being disabled
	if (strcmp(setting_name, "VIDEO_MODE") == 0 ||
	    strcmp(setting_name, "VSYNC_ADJUST") == 0 ||
	    strcmp(setting_name, "DVI_MODE") == 0 ||
	    strcmp(setting_name, "VSCALE_MODE") == 0 ||
	    strcmp(setting_name, "VSCALE_BORDER") == 0 ||
	    strcmp(setting_name, "HDMI_GAME_MODE") == 0)
	{
		// Check if DIRECT_VIDEO is enabled (temp value or memory value)
		uint8_t direct_video_value = cfg.direct_video; // Default to memory value
		
		// Check for temp value
		uint8_t temp_value;
		if (get_temp_uint8_value("DIRECT_VIDEO", &temp_value))
		{
			direct_video_value = temp_value;
		}
		
		// If DIRECT_VIDEO is enabled (1), these settings should be stippled
		if (direct_video_value == 1)
		{
			return true;
		}
	}
	
	// HDR nits settings that depend on HDR mode being enabled (1 or 2)
	if (strcmp(setting_name, "HDR_MAX_NITS") == 0 ||
	    strcmp(setting_name, "HDR_AVG_NITS") == 0)
	{
		// Check HDR mode (temp value or memory value)
		uint8_t hdr_value = cfg.hdr; // Default to memory value
		
		// Check for temp value
		uint8_t temp_value;
		if (get_temp_uint8_value("HDR", &temp_value))
		{
			hdr_value = temp_value;
		}
		
		// If HDR is disabled (0), these settings should be stippled
		if (hdr_value == 0)
		{
			return true;
		}
	}
	
	// VRR framerate settings that depend on VRR_MODE being enabled (1, 2, or 3)
	if (strcmp(setting_name, "VRR_MIN_FRAMERATE") == 0 ||
	    strcmp(setting_name, "VRR_MAX_FRAMERATE") == 0 ||
	    strcmp(setting_name, "VRR_VESA_FRAMERATE") == 0)
	{
		// Check VRR_MODE (temp value or memory value)
		uint8_t vrr_mode_value = cfg.vrr_mode; // Default to memory value
		
		// Check for temp value
		uint8_t temp_value;
		if (get_temp_uint8_value("VRR_MODE", &temp_value))
		{
			vrr_mode_value = temp_value;
		}
		
		// If VRR_MODE is disabled (0), these settings should be stippled
		if (vrr_mode_value == 0)
		{
			return true;
		}
	}
	
	return false;
}

// Custom render function for SYNC_MODE
static const char* render_sync_mode(const char* setting_name, const char* display_name, char* buffer, size_t buffer_size)
{
	// Get value from temp settings if available, otherwise use memory
	uint8_t sync_mode_value = cfg.sync_mode; // Default to memory value
	
	// Check for temp value
	uint8_t temp_value;
	if (get_temp_uint8_value("SYNC_MODE", &temp_value))
	{
		sync_mode_value = temp_value;
		printf("DEBUG: render_sync_mode using temp value %d\n", sync_mode_value);
	}
	
	uint8_t vga_mode = get_current_vga_mode();
	const char* sync_labels[] = {"Separate", "Composite", "Sync-on-Green"};
	const char* label = (sync_mode_value <= 2) ? sync_labels[sync_mode_value] : "Unknown";
	
	// Check if sync mode is locked (stippled)
	if (is_sync_mode_locked())
	{
		// Show locked version - force to composite in S-Video/CVBS
		snprintf(buffer, buffer_size, " %s: Composite", display_name);
	}
	else if (vga_mode == 1) // YPbPr mode
	{
		// YPbPr can't use separate sync - show available options only
		if (sync_mode_value == 0) // If set to separate, force to composite
		{
			snprintf(buffer, buffer_size, " %s: Composite", display_name);
		}
		else
		{
			snprintf(buffer, buffer_size, " %s: %s", display_name, label);
		}
	}
	else
	{
		snprintf(buffer, buffer_size, " %s: %s", display_name, label);
	}
	return buffer;
}

// Sync functions for SYNC_MODE virtual setting
void cfg_sync_mode_to_individual()
{
	// Convert sync_mode virtual setting to individual csync/vga_sog settings
	switch (cfg.sync_mode)
	{
		case 0: // Separate
			cfg.csync = 0;
			cfg.vga_sog = 0;
			break;
		case 1: // Composite
			cfg.csync = 1;
			cfg.vga_sog = 0;
			break;
		case 2: // Sync-on-Green
			cfg.csync = 0;
			cfg.vga_sog = 1;
			break;
		default:
			// Invalid value, default to separate
			cfg.sync_mode = 0;
			cfg.csync = 1;
			cfg.vga_sog = 0;
			break;
	}
}

void cfg_sync_individual_to_mode()
{
	// Convert individual csync/vga_sog settings to sync_mode virtual setting
	if (cfg.vga_sog)
	{
		cfg.sync_mode = 2; // Sync-on-Green
	}
	else if (cfg.csync)
	{
		cfg.sync_mode = 1; // Composite
	}
	else
	{
		cfg.sync_mode = 0; // Separate
	}
}

void cfg_auto_set_sync_mode_for_video_mode(int video_mode)
{
	// Auto-configure sync mode based on video mode requirements
	switch (video_mode)
	{
		case 0: // RGB
			// RGB: Leave sync_mode as user configured (any mode supported)
			printf("DEBUG: RGB mode - keeping user sync_mode=%d\n", cfg.sync_mode);
			break;
		case 1: // YPbPr
			// YPbPr: Force sync-on-green mode for proper component video
			cfg.sync_mode = 2; // Sync-on-Green
			printf("DEBUG: Auto-set sync_mode=2 (Sync-on-Green) for YPbPr\n");
			break;
		case 2: // S-Video
			// S-Video: Force composite sync for proper chroma/luma separation
			cfg.sync_mode = 1; // Composite
			printf("DEBUG: Auto-set sync_mode=1 (Composite) for S-Video\n");
			// S-Video: Disable VGA scaler (direct analog output)
			if (cfg.vga_scaler) {
				cfg.vga_scaler = 0;
				printf("DEBUG: Auto-disabled vga_scaler for S-Video mode\n");
			}
			break;
		case 3: // CVBS
			// CVBS: Force composite sync for proper composite video
			cfg.sync_mode = 1; // Composite
			printf("DEBUG: Auto-set sync_mode=1 (Composite) for CVBS\n");
			// CVBS: Disable VGA scaler (direct analog output)
			if (cfg.vga_scaler) {
				cfg.vga_scaler = 0;
				printf("DEBUG: Auto-disabled vga_scaler for CVBS mode\n");
			}
			break;
		default:
			printf("DEBUG: Unknown video mode %d - keeping sync_mode=%d\n", video_mode, cfg.sync_mode);
			break;
	}
}

// Auto-configure all VGA-related settings based on VGA mode
void cfg_auto_configure_vga_settings(int vga_mode_int)
{
	printf("DEBUG: Auto-configuring VGA settings for mode %d\n", vga_mode_int);
	
	switch (vga_mode_int)
	{
		case 0: // RGB
			// RGB: forced scandoubler on or off (user choice), vga scaler on or off (user choice), 
			// sync mode separate/composite/sync-on-green (user choice), ntsc mode disabled
			// Keep forced scandoubler, vga scaler, and sync mode as user configured
			cfg.ntsc_mode = 0; // Disable NTSC for RGB
			printf("DEBUG: RGB mode - disabled NTSC, keeping user scandoubler/scaler/sync settings\n");
			break;
			
		case 1: // YPbPr
			// YPbPr: forced scandoubler on or off (user choice), vga scaler on or off (user choice),
			// sync mode composite or sync-on-green (not separate), ntsc mode disabled
			// Keep forced scandoubler and vga scaler as user configured
			// If sync mode is separate (0), change to sync-on-green (2), otherwise keep user choice
			if (cfg.sync_mode == 0) // Separate sync not supported for YPbPr
			{
				cfg.sync_mode = 2; // Change to Sync-on-Green
				cfg_sync_mode_to_individual(); // Apply sync mode to individual settings
				printf("DEBUG: YPbPr mode - changed from separate to sync-on-green\n");
			}
			cfg.ntsc_mode = 0; // Disable NTSC for YPbPr
			printf("DEBUG: YPbPr mode - disabled NTSC, keeping user scandoubler/scaler\n");
			break;
			
		case 2: // S-Video
			// S-Video: forced scandoubler off, vga scaler (user choice), sync mode composite, ntsc mode enabled defaulted to 0
			cfg.forced_scandoubler = 0; // Force scandoubler off
			cfg.sync_mode = 1; // Composite sync (required)
			cfg.ntsc_mode = 0; // Enable NTSC, default to 0
			cfg_sync_mode_to_individual(); // Apply sync mode to individual settings
			printf("DEBUG: S-Video mode - disabled scandoubler, set composite sync, enabled NTSC=0\n");
			break;
			
		case 3: // CVBS (Composite)
			// CVBS: forced scandoubler off, vga scaler (user choice), sync mode composite, ntsc mode enabled defaulted to 0
			cfg.forced_scandoubler = 0; // Force scandoubler off
			cfg.sync_mode = 1; // Composite sync (required)
			cfg.ntsc_mode = 0; // Enable NTSC, default to 0
			cfg_sync_mode_to_individual(); // Apply sync mode to individual settings
			printf("DEBUG: CVBS mode - disabled scandoubler, set composite sync, enabled NTSC=0\n");
			break;
			
		default:
			printf("DEBUG: Unknown VGA mode %d - no auto-configuration\n", vga_mode_int);
			break;
	}
}

// Auto-configure VGA-related settings in temp mode
void cfg_auto_configure_vga_settings_temp(int vga_mode_int)
{
	printf("DEBUG: Auto-configuring VGA temp settings for mode %d\n", vga_mode_int);
	
	// For temp settings, we need to update the temp values, not the memory values
	// This function will set temp values for the related settings
	const ini_var_t* forced_scandoubler_setting = cfg_get_ini_var("FORCED_SCANDOUBLER");
	const ini_var_t* sync_mode_setting = cfg_get_ini_var("SYNC_MODE");
	const ini_var_t* ntsc_mode_setting = cfg_get_ini_var("NTSC_MODE");
	
	switch (vga_mode_int)
	{
		case 0: // RGB
			// RGB: ntsc mode disabled, keep other settings as user configured
			if (ntsc_mode_setting) cfg_temp_settings_change(ntsc_mode_setting, 0 - cfg.ntsc_mode); // Set to 0
			printf("DEBUG: RGB temp mode - disabled NTSC temp, keeping user settings\n");
			break;
			
		case 1: // YPbPr
			// YPbPr: sync mode composite or sync-on-green (not separate), ntsc mode disabled
			// Only change sync mode if it's currently separate (0)
			if (sync_mode_setting && cfg.sync_mode == 0) // Separate sync not supported for YPbPr
			{
				cfg_temp_settings_change(sync_mode_setting, 2 - cfg.sync_mode); // Change to sync-on-green
				printf("DEBUG: YPbPr temp mode - changed from separate to sync-on-green temp\n");
			}
			if (ntsc_mode_setting) cfg_temp_settings_change(ntsc_mode_setting, 0 - cfg.ntsc_mode); // Set to 0
			printf("DEBUG: YPbPr temp mode - disabled NTSC temp\n");
			break;
			
		case 2: // S-Video
		case 3: // CVBS
			// S-Video/CVBS: forced scandoubler off, sync mode composite, ntsc mode enabled=0
			if (forced_scandoubler_setting) cfg_temp_settings_change(forced_scandoubler_setting, 0 - cfg.forced_scandoubler); // Set to 0
			if (sync_mode_setting) cfg_temp_settings_change(sync_mode_setting, 1 - cfg.sync_mode); // Set to 1 (composite)
			if (ntsc_mode_setting) cfg_temp_settings_change(ntsc_mode_setting, 0 - cfg.ntsc_mode); // Set to 0
			printf("DEBUG: %s temp mode - disabled scandoubler temp, set composite sync temp, enabled NTSC=0 temp\n", 
				   (vga_mode_int == 2) ? "S-Video" : "CVBS");
			break;
			
		default:
			printf("DEBUG: Unknown VGA mode %d - no temp auto-configuration\n", vga_mode_int);
			break;
	}
}

// Override table - add entries here for settings that need custom display logic
static const setting_override_t setting_overrides[] = {
	{ "DIRECT_VIDEO", render_direct_video },
	{ "VRR_MODE", render_vrr_mode },
	{ "SNIPER_MODE", render_sniper_mode },
	{ "GAMEPAD_DEFAULTS", render_gamepad_defaults },
	{ "SYNC_MODE", render_sync_mode },
	// { "VIDEO_MODE", render_video_mode }, // TODO: Fix linker issue with get_resolution_display_name
	// Add more overrides here as needed
	{ NULL, NULL } // Terminator
};

// Check if a setting has a custom override
static const char* check_setting_override(const char* setting_name, const char* display_name, char* buffer, size_t buffer_size)
{
	for (int i = 0; setting_overrides[i].setting_name != NULL; i++)
	{
		if (strcmp(setting_overrides[i].setting_name, setting_name) == 0)
		{
			return setting_overrides[i].render_func(setting_name, display_name, buffer, buffer_size);
		}
	}
	return NULL; // No override found
}

// Check if a setting should be displayed as disabled/grayed out
bool cfg_is_setting_disabled(const char* setting_name)
{
	return !cfg_is_setting_enabled(setting_name);
}

// UI rendering helper functions for enhanced menu display
void cfg_render_setting_value(char* buffer, size_t buffer_size, const char* setting_name, const char* display_name)
{
	if (!buffer || !setting_name || !display_name) return;
	
	// First check for custom overrides
	const char* override_result = check_setting_override(setting_name, display_name, buffer, buffer_size);
	if (override_result)
	{
		return; // Override handled the rendering
	}
	
	// Find the setting in ini_vars
	const ini_var_t* var = cfg_get_ini_var(setting_name);
	if (!var) 
	{
		// Fallback to traditional rendering
		snprintf(buffer, buffer_size, " %s: ???", display_name);
		return;
	}
	
	// Check if setting is disabled - if so, render as grayed out
	bool is_disabled = cfg_is_setting_disabled(setting_name);
	
	// Get the UI type for this setting
	ui_type_t ui_type = cfg_auto_detect_ui_type(var);
	
	// Get current value as string (use temp value if available)
	char value_str[64] = "";
	cfg_get_temp_var_value_as_string(var, value_str, sizeof(value_str));
	
	// Render based on UI type
	switch (ui_type)
	{
		case UI_CHECKBOX:
		{
			// For 0/1 values, show checkbox symbols - use temp value from value_str
			int value = atoi(value_str);
			const char* checkbox_symbol = value ? "\x99" : "\x98";
			
			// Stippled settings always show as unchecked
			if (is_setting_stippled(setting_name)) {
				checkbox_symbol = "\x98"; // Force unchecked for stippled settings
			}
			
			snprintf(buffer, buffer_size, " %s: %s", display_name, checkbox_symbol);
			break;
		}
		
		case UI_SLIDER:
		{
			// For numeric ranges, show value with progress bar
			char progress_bar[8] = "";
			float percentage = 0.0f;
			
			if (var->type == INI_FLOAT)
			{
				float value = atof(value_str); // Use temp/memory value from value_str
				if (var->max > var->min)
					percentage = (value - var->min) / (var->max - var->min);
				
				// Create a 5-character progress bar
				int filled = (int)(percentage * 5.0f);
				for (int i = 0; i < 5; i++)
				{
					progress_bar[i] = (i < filled) ? '\x97' : '\x8C'; // 0x97=filled square, 0x8C=empty square
				}
				progress_bar[5] = '\0';
				
				snprintf(buffer, buffer_size, " %s: %.1f%s %s", display_name, value, var->unit ? var->unit : "", progress_bar);
			}
			else
			{
				// Integer types - use temp/memory value from value_str
				int64_t value = atoll(value_str);
				
				if (var->max > var->min)
					percentage = (float)(value - var->min) / (var->max - var->min);
				
				// Create a 5-character progress bar
				int filled = (int)(percentage * 5.0f);
				for (int i = 0; i < 5; i++)
				{
					progress_bar[i] = (i < filled) ? '\x97' : '\x8C'; // 0x97=filled square, 0x8C=empty square
				}
				progress_bar[5] = '\0';
				
				snprintf(buffer, buffer_size, " %s: %02lld%s %s", display_name, (long long)value, var->unit ? var->unit : "", progress_bar);
			}
			break;
		}
		
		case UI_DROPDOWN:
		{
			// For small ranges, show descriptive option names where possible
			const char* option_name = NULL;
			int current_value = 0;
			
			// Get the current value from temp/memory value_str
			current_value = atoi(value_str);
			
			// Provide readable names for specific settings
			if (strcmp(var->name, "HDMI_LIMITED") == 0)
			{
				if (current_value == 0) option_name = "Full (0-255)";
				else if (current_value == 1) option_name = "Lim. (16-235)";
				else if (current_value == 2) option_name = "Lim. (16-255)";
			}
			else if (strcmp(var->name, "VSYNC_ADJUST") == 0)
			{
				if (current_value == 0) option_name = "3 Buffer 60Hz";
				else if (current_value == 1) option_name = "3 Buffer Match";
				else if (current_value == 2) option_name = "1 Buffer Match";
			}
			else if (strcmp(var->name, "RESET_COMBO") == 0)
			{
				if (current_value == 0) option_name = "LCtrl+LShift+RAlt";
				else if (current_value == 1) option_name = "LCtrl+LShift+Del";
				else if (current_value == 2) option_name = "LCtrl+RAlt+Del";
				else if (current_value == 3) option_name = "LCtrl+LShift+F12";
			}
			
			// Use descriptive name if available, otherwise fall back to value
			if (option_name)
				snprintf(buffer, buffer_size, " %s: %s", display_name, option_name);
			else
				snprintf(buffer, buffer_size, " %s: %s", display_name, value_str);
			break;
		}
		
		case UI_STRING_INPUT:
		{
			// For text input strings, show current value with input indicator
			if (var->type == INI_STRING)
			{
				// Use value_str which contains temp value if available, otherwise memory value
				const char* display_value = (value_str && strlen(value_str) > 0) ? value_str : "Default";
				
				// Special handling for specific string settings
				if (strcmp(var->name, "VIDEO_MODE") == 0 || 
				    strcmp(var->name, "VIDEO_MODE_PAL") == 0 || 
				    strcmp(var->name, "VIDEO_MODE_NTSC") == 0)
				{
					// Use temp/memory value from value_str for proper temp display
					if (value_str && strlen(value_str) > 0)
						snprintf(buffer, buffer_size, " %s: %s", display_name, value_str);
					else
						snprintf(buffer, buffer_size, " %s: Auto", display_name);
				}
				else if (strcmp(var->name, "BOOTCORE") == 0)
				{
					// Show core name or "None" using temp value
					if (value_str && strlen(value_str) > 0)
						snprintf(buffer, buffer_size, " %s: %s", display_name, value_str);
					else
						snprintf(buffer, buffer_size, " %s: None", display_name);
				}
				else if (strcmp(var->name, "VGA_MODE") == 0)
				{
					// Use value_str which already contains temp value if available
					// Show VGA mode or "RGB" for empty/default
					if (value_str && strlen(value_str) > 0)
						snprintf(buffer, buffer_size, " %s: %s", display_name, value_str);
					else
						snprintf(buffer, buffer_size, " %s: rgb", display_name);
				}
				else
				{
					// General string display with text input indicator
					snprintf(buffer, buffer_size, " %s: %s \x10", display_name, display_value); // 0x10 = edit icon
				}
			}
			else
			{
				snprintf(buffer, buffer_size, " %s: %s", display_name, value_str);
			}
			break;
		}
		
		case UI_FILE_PICKER:
		{
			// For file paths, show filename with browse indicator
			if (var->type == INI_STRING)
			{
				// Use temp/memory value from value_str
				if (value_str && strlen(value_str) > 0)
				{
					// Extract filename from path for display
					const char* filename = strrchr(value_str, '/');
					filename = filename ? filename + 1 : value_str;
					
					if (strlen(filename) > 0)
						snprintf(buffer, buffer_size, " %s: %s \x10", display_name, filename); // 0x10 = browse arrow
					else
						snprintf(buffer, buffer_size, " %s: Browse... \x10", display_name);
				}
				else
				{
					snprintf(buffer, buffer_size, " %s: Browse... \x10", display_name);
				}
			}
			else
			{
				snprintf(buffer, buffer_size, " %s: %s", display_name, value_str);
			}
			break;
		}
		
		case UI_HIDDEN:
		default:
		{
			// Hidden or unknown types use traditional rendering
			snprintf(buffer, buffer_size, " %s: %s", display_name, value_str);
			break;
		}
	}
}

// Helper to get current value of any variable as string
void cfg_get_var_value_as_string(const ini_var_t* var, char* buffer, size_t buffer_size)
{
	if (!var || !buffer) return;
	
	switch (var->type)
	{
		case INI_UINT8:
			snprintf(buffer, buffer_size, "%u", *(uint8_t*)var->var);
			break;
		case INI_INT8:
			snprintf(buffer, buffer_size, "%d", *(int8_t*)var->var);
			break;
		case INI_UINT16:
			snprintf(buffer, buffer_size, "%u", *(uint16_t*)var->var);
			break;
		case INI_INT16:
			snprintf(buffer, buffer_size, "%d", *(int16_t*)var->var);
			break;
		case INI_UINT32:
			snprintf(buffer, buffer_size, "%u", *(uint32_t*)var->var);
			break;
		case INI_INT32:
			snprintf(buffer, buffer_size, "%d", *(int32_t*)var->var);
			break;
		case INI_HEX8:
			snprintf(buffer, buffer_size, "0x%02X", *(uint8_t*)var->var);
			break;
		case INI_HEX16:
			snprintf(buffer, buffer_size, "0x%04X", *(uint16_t*)var->var);
			break;
		case INI_HEX32:
			snprintf(buffer, buffer_size, "0x%08X", *(uint32_t*)var->var);
			break;
		case INI_FLOAT:
			snprintf(buffer, buffer_size, "%.2f", *(float*)var->var);
			break;
		case INI_STRING:
		{
			char* str_value = (char*)var->var;
			if (str_value && strlen(str_value) > 0)
				snprintf(buffer, buffer_size, "%s", str_value);
			else
				snprintf(buffer, buffer_size, "");
			break;
		}
		default:
			snprintf(buffer, buffer_size, "???");
			break;
	}
}


#define INI_LINE_SIZE           1024

#define INI_SECTION_START       '['
#define INI_SECTION_END         ']'
#define INCL_SECTION            '+'

#define CHAR_IS_NUM(c)          (((c) >= '0') && ((c) <= '9'))
#define CHAR_IS_ALPHA_LOWER(c)  (((c) >= 'a') && ((c) <= 'z'))
#define CHAR_IS_ALPHA_UPPER(c)  (((c) >= 'A') && ((c) <= 'Z'))
#define CHAR_IS_ALPHANUM(c)     (CHAR_IS_ALPHA_LOWER(c) || CHAR_IS_ALPHA_UPPER(c) || CHAR_IS_NUM(c))
#define CHAR_IS_SPECIAL(c)      (((c) == '[') || ((c) == ']') || ((c) == '(') || ((c) == ')') || \
                                 ((c) == '-') || ((c) == '+') || ((c) == '/') || ((c) == '=') || \
                                 ((c) == '#') || ((c) == '$') || ((c) == '@') || ((c) == '_') || \
                                 ((c) == ',') || ((c) == '.') || ((c) == '!') || ((c) == '*') || \
                                 ((c) == ':') || ((c) == '~'))

#define CHAR_IS_VALID(c)        (CHAR_IS_ALPHANUM(c) || CHAR_IS_SPECIAL(c))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))
#define CHAR_IS_LINEEND(c)      (((c) == '\n'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))


fileTYPE ini_file;

static bool has_video_sections = false;
static bool using_video_section = false;

int ini_pt = 0;
static char ini_getch()
{
	static uint8_t buf[512];
	if (!(ini_pt & 0x1ff)) FileReadSec(&ini_file, buf);
	if (ini_pt >= ini_file.size) return 0;
	return buf[(ini_pt++) & 0x1ff];
}

static int ini_getline(char* line)
{
	char c, ignore = 0, skip = 1;
	int i = 0;

	while ((c = ini_getch()))
	{
		if (!CHAR_IS_SPACE(c)) skip = 0;
		if (i >= (INI_LINE_SIZE - 1) || CHAR_IS_COMMENT(c)) ignore = 1;

		if (CHAR_IS_LINEEND(c)) break;
		if ((CHAR_IS_SPACE(c) || CHAR_IS_VALID(c)) && !ignore && !skip) line[i++] = c;
	}
	line[i] = 0;
	while (i > 0 && CHAR_IS_SPACE(line[i - 1])) line[--i] = 0;
	return c == 0;
}

static int ini_get_section(char* buf, const char *vmode)
{
	int i = 0;
	int incl = (buf[0] == INCL_SECTION);

	// get section start marker
	if (buf[0] != INI_SECTION_START && buf[0] != INCL_SECTION)
	{
		return 0;
	}
	else buf++;

	int wc_pos = -1;
	int eq_pos = -1;

	// get section stop marker
	while (buf[i])
	{
		if (buf[i] == INI_SECTION_END)
		{
			buf[i] = 0;
			break;
		}

		if (buf[i] == '*') wc_pos = i;
		if (buf[i] == '=') eq_pos = i;

		i++;
		if (i >= INI_LINE_SIZE) return 0;
	}

	if (!strcasecmp(buf, "MiSTer") ||
		(is_arcade() && !strcasecmp(buf, "arcade")) ||
		(arcade_is_vertical() && !strcasecmp(buf, "arcade_vertical")) ||
		((wc_pos >= 0) ? !strncasecmp(buf, user_io_get_core_name(1), wc_pos) : !strcasecmp(buf, user_io_get_core_name(1))) ||
		((wc_pos >= 0) ? !strncasecmp(buf, user_io_get_core_name(0), wc_pos) : !strcasecmp(buf, user_io_get_core_name(0))))
	{
		if (incl)
		{
			ini_parser_debugf("included '%s'", buf);
		}
		else
		{
			ini_parser_debugf("Got SECTION '%s'", buf);
		}
		return 1;
	}
	else if ((eq_pos >= 0) && !strncasecmp(buf, "video", eq_pos))
	{
		has_video_sections = true;
		if(!strcasecmp(&buf[eq_pos+1], vmode))
		{
			using_video_section = true;
			ini_parser_debugf("Got SECTION '%s'", buf);
			return 1;
		}
		else
		{
			return 0;
		}
	}

	return 0;
}

static void ini_parse_numeric(const ini_var_t *var, const char *text, void *out)
{
	uint32_t u32 = 0;
	int32_t i32 = 0;
	float f32 = 0.0f;
	char *endptr = nullptr;

	bool out_of_range = true;
	bool invalid_format = false;

	switch(var->type)
	{
	case INI_HEX8:
	case INI_HEX16:
	case INI_HEX32:
	case INI_HEX32ARR:
		invalid_format = strncasecmp(text, "0x", 2);
		// fall through

	case INI_UINT8:
	case INI_UINT16:
	case INI_UINT32:
	case INI_UINT32ARR:
		u32 = strtoul(text, &endptr, 0);
		if (u32 < var->min) u32 = var->min;
		else if (u32 > var->max) u32 = var->max;
		else out_of_range = false;
		break;

	case INI_INT8:
	case INI_INT16:
	case INI_INT32:
		i32 = strtol(text, &endptr, 0);
		if (i32 < var->min) i32 = var->min;
		else if (i32 > var->max) i32 = var->max;
		else out_of_range = false;
		break;

	case INI_FLOAT:
		f32 = strtof(text, &endptr);
		if (f32 < var->min) f32 = var->min;
		else if (f32 > var->max) f32 = var->max;
		else out_of_range = false;
		break;

	default:
		out_of_range = false;
		break;
	}

	if (*endptr) cfg_error("%s: \'%s\' not a number", var->name, text);
	else if (out_of_range) cfg_error("%s: \'%s\' out of range", var->name, text);
	else if (invalid_format) cfg_error("%s: \'%s\' invalid format", var->name, text);

	switch (var->type)
	{
	case INI_HEX8:
	case INI_UINT8: *(uint8_t*)out = u32; break;
	case INI_INT8: *(int8_t*)out = i32; break;
	case INI_HEX16:
	case INI_UINT16: *(uint16_t*)out = u32; break;
	case INI_HEX32ARR:
	case INI_UINT32ARR: *(uint32_t*)out = u32; break;
	case INI_INT16: *(int16_t*)out = i32; break;
	case INI_HEX32:
	case INI_UINT32: *(uint32_t*)out = u32; break;
	case INI_INT32: *(int32_t*)out = i32; break;
	case INI_FLOAT: *(float*)out = f32; break;
	default: break;
	}
}

// Used to determine if an array variable should be appended or restarted.
static bool var_array_append[sizeof(ini_vars) / sizeof(ini_var_t)] = {};

static void ini_parse_var(char* buf)
{
	// find var
	int i = 0;
	while (1)
	{
		if (buf[i] == '=' || CHAR_IS_SPACE(buf[i]))
		{
			buf[i] = 0;
			break;
		}
		else if (!buf[i]) return;
		i++;
	}

	// parse var
	int var_id = -1;
	for (int j = 0; j < (int)(sizeof(ini_vars) / sizeof(ini_var_t)); j++)
	{
		if (!strcasecmp(buf, ini_vars[j].name)) var_id = j;
	}

	if (var_id == -1)
	{
		cfg_error("%s: unknown option", buf);
	}
	else // get data
	{
		i++;
		while (buf[i] == '=' || CHAR_IS_SPACE(buf[i])) i++;
		ini_parser_debugf("Got VAR '%s' with VALUE %s", buf, buf+i);

		const ini_var_t *var = &ini_vars[var_id];

		switch (var->type)
		{
		case INI_STRING:
			memset(var->var, 0, var->max);
			snprintf((char*)(var->var), var->max, "%s", buf+i);
			if (!strcasecmp(var->name, "VGA_MODE"))
			{
				printf("DEBUG: Loaded VGA_MODE=\"%s\" from INI file\n", (char*)var->var);
			}
			break;

		case INI_STRINGARR:
			{
				int item_sz = var->max;

				if (!var_array_append[var_id])
				{
					var_array_append[var_id] = true;

					for (int n = 0; n < var->min; n++)
					{
						char *str = ((char*)var->var) + (n * item_sz);
						str[0] = 0;
					}
				}

				for (int n = 0; n < var->min; n++)
				{
					char *str = ((char*)var->var) + (n * item_sz);
					if (!strlen(str))
					{
						snprintf(str, item_sz, "%s", buf + i);
						break;
					}
				}
			}
			break;

		case INI_HEX32ARR:
		case INI_UINT32ARR:
			{
				if (!var_array_append[var_id])
				{
					var_array_append[var_id] = true;

					uint32_t *arr = (uint32_t*)var->var;
					arr[0] = 0;
				}

				uint32_t *arr = (uint32_t*)var->var;
				uint32_t pos = ++arr[0];
				ini_parse_numeric(var, &buf[i], &arr[pos]);
			}
			break;

		default:
			ini_parse_numeric(var, &buf[i], var->var);
			if (!strcasecmp(var->name, "DEBUG"))
			{
				stdout = cfg.debug ? orig_stdout : dev_null;
			}
			if (!strcasecmp(var->name, "YPBPR"))
			{
				printf("DEBUG: Loaded YPBPR=%d from INI file\n", cfg.vga_mode_int);
			}
			break;
		}
	}
}

static void ini_parse(int alt, const char *vmode)
{
	static char line[INI_LINE_SIZE];
	int section = 0;
	int eof;

	if (!orig_stdout) orig_stdout = stdout;
	if (!dev_null)
	{
		dev_null = fopen("/dev/null", "w");
		if (dev_null)
		{
			int null_fd = fileno(dev_null);
			if (null_fd >= 0) fcntl(null_fd, F_SETFD, FD_CLOEXEC);
			stdout = dev_null;
		}
	}

	ini_parser_debugf("Start INI parser for core \"%s\"(%s), video mode \"%s\".", user_io_get_core_name(0), user_io_get_core_name(1), vmode);

	memset(line, 0, sizeof(line));
	memset(&ini_file, 0, sizeof(ini_file));

	const char *name = cfg_get_name(alt);
	if (!FileOpen(&ini_file, name))	return;

	ini_parser_debugf("Opened file %s with size %llu bytes.", name, ini_file.size);

	ini_pt = 0;

	// parse ini
	while (1)
	{
		// get line
		eof = ini_getline(line);
		ini_parser_debugf("line(%d): \"%s\".", section, line);

		if (line[0] == INI_SECTION_START)
		{
			// if first char in line is INI_SECTION_START, get section
			section = ini_get_section(line, vmode);
			if (section)
			{
				memset(var_array_append, 0, sizeof(var_array_append));
			}
		}
		else if (line[0] == INCL_SECTION && !section)
		{
			section = ini_get_section(line, vmode);
			if (section)
			{
				memset(var_array_append, 0, sizeof(var_array_append));
			}
		}
		else if(section)
		{
			// otherwise this is a variable, get it
			ini_parse_var(line);
		}

		// if end of file, stop
		if (eof) break;
	}

	FileClose(&ini_file);
}

static constexpr int CFG_ERRORS_MAX = 4;
static constexpr int CFG_ERRORS_STRLEN = 128;
static char cfg_errors[CFG_ERRORS_MAX][CFG_ERRORS_STRLEN];
static int cfg_error_count = 0;

const char* cfg_get_name(uint8_t alt)
{
	static int done = 0;
	static char names[3][64] = {};
	static char name[64];

	if (!done)
	{
		done = 1;
		DIR *d = opendir(getRootDir());
		if (!d)
		{
			printf("Couldn't open dir: %s\n", getRootDir());
		}
		else
		{
			struct dirent *de;
			int i = 0;
			while ((de = readdir(d)) && i < 3)
			{
				int len = strlen(de->d_name);
				if (!strncasecmp(de->d_name, "MiSTer_", 7) && !strcasecmp(de->d_name + len - 4, ".ini"))
				{
					snprintf(names[i], sizeof(names[0]), "%s", de->d_name);
					i++;
				}
			}
			closedir(d);
		}

		for (int i = 1; i < 3; i++)
		{
			for (int j = 1; j < 3; j++)
			{
				if ((!names[j - 1][0] && names[j][0]) || (names[j - 1][0] && names[j][0] && strcasecmp(names[j - 1], names[j]) > 0))
				{
					strcpy(name, names[j - 1]);
					strcpy(names[j - 1], names[j]);
					strcpy(names[j], name);
				}
			}
		}
	}

	strcpy(name, "MiSTer.ini");
	if (alt && alt < 4) strcpy(name, names[alt-1]);
	return name;
}

const char* cfg_get_label(uint8_t alt)
{
	if (!alt) return "Main";

	const char *name = cfg_get_name(alt);
	if (!name[0]) return " -- ";

	static char label[6];
	snprintf(label, sizeof(label), "%s", name + 7);
	char *p = strrchr(label, '.');
	if (p) *p = 0;
	if (!strcasecmp(label, "alt"))   return "Alt1";
	if (!strcasecmp(label, "alt_1")) return "Alt1";
	if (!strcasecmp(label, "alt_2")) return "Alt2";
	if (!strcasecmp(label, "alt_3")) return "Alt3";

	for (int i = 0; i < 4; i++) if (!label[i]) label[i] = ' ';
	label[4] = 0;
	return label;
}

void cfg_parse()
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.bootscreen = 1;
	cfg.fb_terminal = 1;
	cfg.controller_info = 6;
	cfg.browse_expand = 1;
	cfg.logo = 1;
	cfg.rumble = 1;
	cfg.wheel_force = 50;
	cfg.dvi_mode = 2;
	cfg.hdr = 0;
	cfg.hdr_max_nits = 1000;
	cfg.hdr_avg_nits = 250;
	cfg.video_brightness = 50;
	cfg.video_contrast = 50;
	cfg.video_saturation = 100;
	cfg.video_hue = 0;
	strcpy(cfg.video_gain_offset, "1, 0, 1, 0, 1, 0");
	strcpy(cfg.main, "MiSTer");
	has_video_sections = false;
	using_video_section = false;
	cfg_error_count = 0;
	ini_parse(altcfg(), video_get_core_mode_name(1));
	if (has_video_sections && !using_video_section)
	{
		// second pass to look for section without vrefresh
		ini_parse(altcfg(), video_get_core_mode_name(0));
	}

	// Handle legacy ypbpr=1 setting by converting to new vga_mode
	if (cfg.vga_mode_int == 1 && strlen(cfg.vga_mode) == 0)
	{
		strcpy(cfg.vga_mode, "ypbpr");
		printf("DEBUG: Legacy YPBPR=1 converted to vga_mode=\"ypbpr\"\n");
	}
	
	if (strlen(cfg.vga_mode))
	{
		printf("DEBUG: Processing vga_mode=\"%s\"\n", cfg.vga_mode);
		if (!strcasecmp(cfg.vga_mode, "rgb")) {
			cfg.vga_mode_int = 0;
			printf("DEBUG: Set vga_mode_int=0 (RGB)\n");
			// RGB: Leave sync_mode as user configured (any mode supported)
		}
		if (!strcasecmp(cfg.vga_mode, "ypbpr")) {
			cfg.vga_mode_int = 1;
			printf("DEBUG: Set vga_mode_int=1 (YPbPr)\n");
			// YPbPr: Force sync-on-green mode for proper component video
			cfg.sync_mode = 2; // Sync-on-Green
			printf("DEBUG: Auto-set sync_mode=2 (Sync-on-Green) for YPbPr\n");
		}
		if (!strcasecmp(cfg.vga_mode, "svideo")) {
			cfg.vga_mode_int = 2;
			printf("DEBUG: Set vga_mode_int=2 (S-Video)\n");
			// S-Video: Force composite sync for proper chroma/luma separation
			cfg.sync_mode = 1; // Composite
			printf("DEBUG: Auto-set sync_mode=1 (Composite) for S-Video\n");
			// S-Video: Disable VGA scaler (direct analog output)
			if (cfg.vga_scaler) {
				cfg.vga_scaler = 0;
				printf("DEBUG: Auto-disabled vga_scaler for S-Video mode\n");
			}
		}
		if (!strcasecmp(cfg.vga_mode, "cvbs")) {
			cfg.vga_mode_int = 3;
			printf("DEBUG: Set vga_mode_int=3 (CVBS)\n");
			// CVBS: Force composite sync for proper composite video
			cfg.sync_mode = 1; // Composite
			printf("DEBUG: Auto-set sync_mode=1 (Composite) for CVBS\n");
			// CVBS: Disable VGA scaler (direct analog output)
			if (cfg.vga_scaler) {
				cfg.vga_scaler = 0;
				printf("DEBUG: Auto-disabled vga_scaler for CVBS mode\n");
			}
		}
	}
	else
	{
		printf("DEBUG: No vga_mode string set, using vga_mode_int=%d\n", cfg.vga_mode_int);
		// Auto-set sync mode based on vga_mode_int value
		cfg_auto_set_sync_mode_for_video_mode(cfg.vga_mode_int);
	}
	
	// Final sync: if user has explicitly set csync/vga_sog in INI, that takes precedence over auto-setting
	// Only sync from individual settings if video mode auto-setting didn't override them
	if (cfg.vga_mode_int == 0) // RGB mode - respect user's explicit sync settings
	{
		cfg_sync_individual_to_mode();
		printf("DEBUG: RGB mode - synced from individual settings: sync_mode=%d\n", cfg.sync_mode);
	}
	else
	{
		// For other video modes, auto-setting took precedence, now apply to individual settings
		cfg_sync_mode_to_individual();
		printf("DEBUG: Video mode %d - applied auto sync_mode=%d to individual settings\n", cfg.vga_mode_int, cfg.sync_mode);
	}
	
	// Test the UI auto-detection system
	cfg_print_ui_analysis();
}

bool cfg_has_video_sections()
{
	return has_video_sections;
}


void cfg_error(const char *fmt, ...)
{
	if (cfg_error_count >= CFG_ERRORS_MAX) return;

	va_list args;
	va_start(args, fmt);
	vsnprintf(cfg_errors[cfg_error_count], CFG_ERRORS_STRLEN, fmt, args);
	va_end(args);

	printf("ERROR CFG: %s\n", cfg_errors[cfg_error_count]);

	cfg_error_count += 1;
}

bool cfg_check_errors(char *msg, size_t max_len)
{
	msg[0] = '\0';

	if (cfg_error_count == 0) return false;

	int pos = snprintf(msg, max_len, "%d INI Error%s\n---", cfg_error_count, cfg_error_count > 1 ? "s" : "");

	for (int i = 0; i < cfg_error_count; i++)
	{
		pos += snprintf(msg + pos, max_len - pos, "\n%s\n", cfg_errors[i]);
	}

	return true;
}

void cfg_print()
{
	printf("Loaded config:\n--------------\n");
	for (uint i = 0; i < (sizeof(ini_vars) / sizeof(ini_vars[0])); i++)
	{
		switch (ini_vars[i].type)
		{
		case INI_UINT8:
			printf("  %s=%u\n", ini_vars[i].name, *(uint8_t*)ini_vars[i].var);
			break;

		case INI_UINT16:
			printf("  %s=%u\n", ini_vars[i].name, *(uint16_t*)ini_vars[i].var);
			break;

		case INI_UINT32:
			printf("  %s=%u\n", ini_vars[i].name, *(uint32_t*)ini_vars[i].var);
			break;

		case INI_UINT32ARR:
			if (*(uint32_t*)ini_vars[i].var)
			{
				uint32_t* arr = (uint32_t*)ini_vars[i].var;
				for (uint32_t n = 0; n < arr[0]; n++) printf("  %s=%u\n", ini_vars[i].name, arr[n + 1]);
			}
			break;

		case INI_HEX8:
			printf("  %s=0x%02X\n", ini_vars[i].name, *(uint8_t*)ini_vars[i].var);
			break;

		case INI_HEX16:
			printf("  %s=0x%04X\n", ini_vars[i].name, *(uint16_t*)ini_vars[i].var);
			break;

		case INI_HEX32:
			printf("  %s=0x%08X\n", ini_vars[i].name, *(uint32_t*)ini_vars[i].var);
			break;

		case INI_HEX32ARR:
			if (*(uint32_t*)ini_vars[i].var)
			{
				uint32_t* arr = (uint32_t*)ini_vars[i].var;
				for (uint32_t n = 0; n < arr[0]; n++) printf("  %s=0x%08X\n", ini_vars[i].name, arr[n + 1]);
			}
			break;

		case INI_INT8:
			printf("  %s=%d\n", ini_vars[i].name, *(int8_t*)ini_vars[i].var);
			break;

		case INI_INT16:
			printf("  %s=%d\n", ini_vars[i].name, *(int16_t*)ini_vars[i].var);
			break;

		case INI_INT32:
			printf("  %s=%d\n", ini_vars[i].name, *(int32_t*)ini_vars[i].var);
			break;

		case INI_FLOAT:
			printf("  %s=%f\n", ini_vars[i].name, *(float*)ini_vars[i].var);
			break;

		case INI_STRING:
			if (*(uint32_t*)ini_vars[i].var) printf("  %s=%s\n", ini_vars[i].name, (char*)ini_vars[i].var);
			break;

		case INI_STRINGARR:
			for (int n = 0; n < ini_vars[i].min; n++)
			{
				char *str = ((char*)ini_vars[i].var) + (n*ini_vars[i].max);
				if (!strlen(str)) break;
				printf("  %s=%s\n", ini_vars[i].name, str);
			}
			break;
		}
	}
	printf("--------------\n");
}

static int yc_parse_mode(char* buf, yc_mode *mode)
{
	int i = 0;
	while (1)
	{
		if (buf[i] == '=' || CHAR_IS_LINEEND(buf[i]))
		{
			buf[i] = 0;
			break;
		}
		else if (!buf[i]) return 0;
		i++;
	}

	i++;
	while (buf[i] == '=' || CHAR_IS_SPACE(buf[i])) i++;
	ini_parser_debugf("Got yc_mode '%s' with VALUE %s", buf, buf + i);

	snprintf(mode->key, sizeof(mode->key), "%s", buf);
	mode->phase_inc = strtoull(buf + i, 0, 0);
	if (!mode->phase_inc)
	{
		printf("ERROR: cannot parse YC phase_inc: '%s'\n", buf + i);
		return 0;
	}

	return 1;
}

void yc_parse(yc_mode *yc_table, int max)
{
	memset(yc_table, 0, max * sizeof(yc_mode));

	static char line[INI_LINE_SIZE];
	int eof;

	memset(line, 0, sizeof(line));
	memset(&ini_file, 0, sizeof(ini_file));

	const char *corename = user_io_get_core_name(1);
	int corename_len = strlen(corename);

	const char *name = "yc.txt";
	if (!FileOpen(&ini_file, name))	return;

	ini_parser_debugf("Opened file %s with size %llu bytes.", name, ini_file.size);

	ini_pt = 0;
	int n = 0;

	while (n < max)
	{
		// get line
		eof = ini_getline(line);
		if (!strncasecmp(line, corename, corename_len))
		{
			int res = yc_parse_mode(line, &yc_table[n]);
			if (res) n++;
		}

		// if end of file, stop
		if (eof) break;
	}

	FileClose(&ini_file);
}

// Helper function to format value based on type
static void format_ini_value(char *buffer, size_t buffer_size, const ini_var_t *var)
{
	switch (var->type)
	{
		case INI_UINT8:
		case INI_INT8:
			snprintf(buffer, buffer_size, "%d", *(uint8_t*)var->var);
			break;
		
		case INI_UINT16:
		case INI_INT16:
			snprintf(buffer, buffer_size, "%d", *(uint16_t*)var->var);
			break;
		
		case INI_UINT32:
		case INI_INT32:
			snprintf(buffer, buffer_size, "%u", *(uint32_t*)var->var);
			break;
		
		case INI_HEX8:
			snprintf(buffer, buffer_size, "0x%02X", *(uint8_t*)var->var);
			break;
		
		case INI_HEX16:
			snprintf(buffer, buffer_size, "0x%04X", *(uint16_t*)var->var);
			break;
		
		case INI_HEX32:
			snprintf(buffer, buffer_size, "0x%08X", *(uint32_t*)var->var);
			break;
		
		case INI_FLOAT:
			snprintf(buffer, buffer_size, "%.2f", *(float*)var->var);
			break;
		
		case INI_STRING:
		{
			const char *str = (char*)var->var;
			// Skip empty strings to avoid writing empty values
			if (str && strlen(str) > 0)
			{
				snprintf(buffer, buffer_size, "%s", str);
			}
			else
			{
				buffer[0] = '\0'; // Empty value
			}
			break;
		}
		
		case INI_UINT32ARR:
		case INI_HEX32ARR:
		case INI_STRINGARR:
			// Arrays need special handling - for now just mark as array
			snprintf(buffer, buffer_size, "; Array type not implemented");
			break;
		
		default:
			snprintf(buffer, buffer_size, "; Unknown type");
			break;
	}
}

// Parse a value from an INI file line and return it as a string
// Returns the value if found, or a special marker if commented out
static char* extract_ini_file_value(char* line, const char* key, int* is_commented)
{
	static char value[512];
	value[0] = '\0';
	*is_commented = 0;
	
	char* original_line = line;
	
	// Skip whitespace at start
	while (*line && CHAR_IS_SPACE(*line)) line++;
	
	// Check if line is commented out
	if (*line == ';' || *line == '#')
	{
		*is_commented = 1;
		line++; // Skip comment character
		while (*line && CHAR_IS_SPACE(*line)) line++; // Skip whitespace after comment
	}
	
	// Check if line starts with the key
	int key_len = strlen(key);
	if (strncasecmp(line, key, key_len) != 0)
		return NULL;
	
	// Skip the key and find '='
	line += key_len;
	while (*line && CHAR_IS_SPACE(*line)) line++;
	if (*line != '=') return NULL;
	line++;
	
	// Skip whitespace after '='
	while (*line && CHAR_IS_SPACE(*line)) line++;
	
	// Extract value (up to comment or end of line)
	int i = 0;
	while (*line && *line != ';' && *line != '#' && *line != '\n' && *line != '\r' && i < 511)
	{
		value[i++] = *line++;
	}
	value[i] = '\0';
	
	// Trim trailing whitespace
	while (i > 0 && CHAR_IS_SPACE(value[i-1]))
		value[--i] = '\0';
	
	return value;
}

// Check if current value differs from default value
static int value_differs_from_default(const ini_var_t *var)
{
	char current_value[512];
	format_ini_value(current_value, sizeof(current_value), var);
	
	// Get default value by checking what the variable would be after memset+defaults
	const char* default_str = "";
	
	// Special cases - these have explicit defaults set in cfg_parse()
	if (!strcasecmp(var->name, "BOOTSCREEN")) default_str = "1";
	else if (!strcasecmp(var->name, "FB_TERMINAL")) default_str = "1";
	else if (!strcasecmp(var->name, "CONTROLLER_INFO")) default_str = "6";
	else if (!strcasecmp(var->name, "BROWSE_EXPAND")) default_str = "1";
	else if (!strcasecmp(var->name, "LOGO")) default_str = "1";
	else if (!strcasecmp(var->name, "RUMBLE")) default_str = "1";
	else if (!strcasecmp(var->name, "WHEEL_FORCE")) default_str = "50";
	// DVI_MODE default is 0 (auto-detect) - removed incorrect default_str = "2"
	else if (!strcasecmp(var->name, "HDR")) default_str = "0";
	else if (!strcasecmp(var->name, "HDR_MAX_NITS")) default_str = "1000";
	else if (!strcasecmp(var->name, "HDR_AVG_NITS")) default_str = "250";
	else if (!strcasecmp(var->name, "VIDEO_BRIGHTNESS")) default_str = "50";
	else if (!strcasecmp(var->name, "VIDEO_CONTRAST")) default_str = "50";
	else if (!strcasecmp(var->name, "VIDEO_SATURATION")) default_str = "100";
	else if (!strcasecmp(var->name, "VIDEO_HUE")) default_str = "0";
	else if (!strcasecmp(var->name, "VIDEO_GAIN_OFFSET")) default_str = "1, 0, 1, 0, 1, 0";
	else if (!strcasecmp(var->name, "MAIN")) default_str = "MiSTer";
	else if (!strcasecmp(var->name, "YPBPR")) default_str = "0"; // Special legacy case
	else 
	{
		// For all other variables, default is based on type after memset to 0
		switch (var->type)
		{
			case INI_UINT8:
			case INI_INT8:
			case INI_UINT16:
			case INI_INT16:
			case INI_UINT32:
			case INI_INT32:
				default_str = "0";
				break;
			case INI_HEX8:
				default_str = "0x00";
				break;
			case INI_HEX16:
				default_str = "0x0000";
				break;
			case INI_HEX32:
				default_str = "0x00000000";
				break;
			case INI_FLOAT:
				default_str = "0.00";  // Match the formatting used by format_ini_value (%.2f)
				break;
			case INI_STRING:
				default_str = ""; // Empty string
				break;
			default:
				default_str = "";
				break;
		}
	}
	
	int differs = strcmp(current_value, default_str) != 0;
	
	// Debug a few key settings
	if (!strcasecmp(var->name, "COMPOSITE_SYNC") || !strcasecmp(var->name, "DEBUG") || 
	    !strcasecmp(var->name, "FORCED_SCANDOUBLER") || !strcasecmp(var->name, "HDMI_GAME_MODE"))
	{
		printf("DEBUG: %s - current='%s', default='%s', differs=%d\n", 
		       var->name, current_value, default_str, differs);
	}
	
	return differs;
}

// Check if current memory value differs from file value
static int value_needs_update(const ini_var_t *var, const char* file_value)
{
	char current_value[512];
	format_ini_value(current_value, sizeof(current_value), var);
	
	// If we have no file value and current value is empty, no update needed
	if (!file_value && strlen(current_value) == 0)
		return 0;
	
	// If we have no file value but current value exists and differs from default, update needed
	if (!file_value && strlen(current_value) > 0)
	{
		int differs = value_differs_from_default(var);
		if (!strcasecmp(var->name, "COMPOSITE_SYNC") || !strcasecmp(var->name, "DEBUG") || !strcasecmp(var->name, "HDMI_GAME_MODE"))
		{
			printf("DEBUG: value_needs_update() %s - current='%s', differs_from_default=%d\n", 
			       var->name, current_value, differs);
		}
		return differs;
	}
	
	// If we have file value but current value is empty, update needed (to remove)
	if (file_value && strlen(current_value) == 0)
		return 1;
	
	// Compare the values
	return strcmp(current_value, file_value) != 0;
}

// Save configuration to INI file using sed for selective updates
int cfg_save(uint8_t alt)
{
	const char *ini_filename = cfg_get_name(alt);
	char filepath[1024];
	char backuppath[1024];
	char temppath[1024];
	
	printf("DEBUG: Saving to alternate INI %d: %s\n", alt, ini_filename);
	
	// Create file paths
	snprintf(filepath, sizeof(filepath), "%s/%s", getRootDir(), ini_filename);
	snprintf(backuppath, sizeof(backuppath), "%s.temp", filepath);
	snprintf(temppath, sizeof(temppath), "%s.sed", filepath);
	
	printf("DEBUG: Full filepath: %s\n", filepath);
	
	// Create backup
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" 2>/dev/null", filepath, backuppath);
	system(cmd);
	
	// Check if file exists and has [MiSTer] section
	FILE *check_fp = fopen(filepath, "r");
	int has_mister_section = 0;
	int file_exists = (check_fp != NULL);
	
	if (check_fp)
	{
		char line[1024];
		while (fgets(line, sizeof(line), check_fp))
		{
			// Skip whitespace
			char *trimmed = line;
			while (*trimmed && CHAR_IS_SPACE(*trimmed)) trimmed++;
			
			if (strncasecmp(trimmed, "[MiSTer]", 8) == 0)
			{
				has_mister_section = 1;
				break;
			}
		}
		fclose(check_fp);
	}
	
	// If file doesn't exist or has no [MiSTer] section, create it
	if (!file_exists || !has_mister_section)
	{
		FILE *fp = fopen(filepath, file_exists ? "a" : "w");
		if (!fp)
		{
			printf("Failed to create/modify INI file: %s\n", filepath);
			return 0;
		}
		
		if (file_exists)
			fprintf(fp, "\n");
		fprintf(fp, "[MiSTer]\n");
		fclose(fp);
	}
	
	// Process each variable that might need updating
	printf("DEBUG: sizeof(ini_vars)=%zu, sizeof(ini_var_t)=%zu\n", sizeof(ini_vars), sizeof(ini_var_t));
	printf("DEBUG: Processing %d ini_vars entries\n", nvars);
	int processed = 0, skipped_array = 0, skipped_sync = 0;
	
	for (int i = 0; i < nvars; i++)
	{
		const ini_var_t *var = &ini_vars[i];
		
		// Skip array types for now
		if (var->type == INI_UINT32ARR || var->type == INI_HEX32ARR || var->type == INI_STRINGARR)
			continue;
		
		// Special case for SYNC_MODE: sync to individual settings and skip direct saving
		if (!strcasecmp(var->name, "SYNC_MODE"))
		{
			cfg_sync_mode_to_individual();
			continue; // Don't save SYNC_MODE directly to INI
		}
		
		// Debug: Show memory value for key settings
		if (strstr(var->name, "HDMI_GAME_MODE") || strstr(var->name, "BOOTSCREEN"))
		{
			printf("DEBUG: Processing %s (type=%d)\n", var->name, var->type);
			printf("DEBUG: Memory value: ");
			switch (var->type)
			{
				case INI_UINT8:
					printf("%u", *(uint8_t*)var->var);
					break;
				case INI_UINT16:
					printf("%u", *(uint16_t*)var->var);
					break;
				case INI_INT16:
					printf("%d", *(int16_t*)var->var);
					break;
				default:
					printf("(unknown type)");
			}
			printf("\n");
		}
		
		char current_value[512];
		
		// Special case for YPBPR: always write 0 (legacy compatibility)
		if (!strcasecmp(var->name, "YPBPR"))
		{
			strcpy(current_value, "0");
		}
		// Special case for MOUSE_THROTTLE: if 0, comment out with value 1
		else if (!strcasecmp(var->name, "MOUSE_THROTTLE"))
		{
			format_ini_value(current_value, sizeof(current_value), var);
			if (strcmp(current_value, "0") == 0)
			{
				// Don't write MOUSE_THROTTLE=0, instead comment it out
				// This will be handled specially below
				strcpy(current_value, "0_COMMENT_OUT");
			}
		}
		else
		{
			format_ini_value(current_value, sizeof(current_value), var);
		}
		
		// Only process if we have a value to write
		if (strlen(current_value) == 0)
			continue;
		
		// Convert variable name to lowercase for sed (file uses lowercase)
		char lowercase_name[256];
		for (int i = 0; var->name[i] && i < 255; i++)
		{
			lowercase_name[i] = tolower(var->name[i]);
		}
		lowercase_name[strlen(var->name)] = '\0';
		
		// Check if this value exists in the file and differs
		FILE *fp = fopen(filepath, "r");
		if (!fp) {
			printf("DEBUG: Could not open %s for reading\n", filepath);
			continue;
		}
		
		char line[1024];
		char *file_value = NULL;
		char file_value_buffer[512];  // Buffer to store the extracted value
		int in_mister_section = 0;
		int is_commented = 0;
		
		while (fgets(line, sizeof(line), fp))
		{
			char *trimmed = line;
			while (*trimmed && CHAR_IS_SPACE(*trimmed)) trimmed++;
			
			// Check for section headers
			if (*trimmed == '[')
			{
				in_mister_section = (strncasecmp(trimmed, "[MiSTer]", 8) == 0);
				if (!in_mister_section && strlen(trimmed) > 2)
					break; // We've passed the [MiSTer] section
				continue;
			}
			
			// Only look for variables in [MiSTer] section
			if (in_mister_section)
			{
				char *extracted = extract_ini_file_value(trimmed, lowercase_name, &is_commented);
				if (extracted)
				{
					// Copy the value to our buffer since extract_ini_file_value uses a static buffer
					strncpy(file_value_buffer, extracted, sizeof(file_value_buffer) - 1);
					file_value_buffer[sizeof(file_value_buffer) - 1] = '\0';
					file_value = file_value_buffer;
					break;
				}
			}
		}
		fclose(fp);
		
		// Debug check for specific problematic settings
		if (!strcasecmp(var->name, "HDMI_GAME_MODE") || !strcasecmp(var->name, "BOOTSCREEN") || !strcasecmp(var->name, "DEBUG"))
		{
			printf("DEBUG: %s - file_value='%s', current='%s', is_commented=%d\n", 
				var->name, file_value ? file_value : "NULL", current_value, is_commented);
			printf("DEBUG: %s - value_needs_update=%d\n", 
				var->name, value_needs_update(var, file_value));
		}
		
		// Check if update is needed
		if (!value_needs_update(var, file_value) && !is_commented)
			continue;
		
		if (strcmp(current_value, "0_COMMENT_OUT") == 0)
		{
			printf("DEBUG: %s - file_value='%s', current='0' -> commenting out with value 1%s\n", 
				var->name, file_value ? file_value : "NULL", is_commented ? " (was commented)" : "");
		}
		else
		{
			printf("DEBUG: %s - file_value='%s', current='%s'%s\n", 
				var->name, file_value ? file_value : "NULL", current_value, is_commented ? " (commented)" : "");
		}
		
		processed++;
		
		// Escape special characters in current_value for sed
		char escaped_value[1024];
		int j = 0;
		for (int i = 0; current_value[i] && j < 1023; i++)
		{
			if (current_value[i] == '/' || current_value[i] == '&' || current_value[i] == '\\')
			{
				escaped_value[j++] = '\\';
			}
			escaped_value[j++] = current_value[i];
		}
		escaped_value[j] = '\0';
		
		// For missing values, only add if they differ from defaults
		if (!file_value && !is_commented)
		{
			// Only add missing settings if current value differs from default
			if (!value_differs_from_default(var))
			{
				// Skip settings that match their defaults
				continue;
			}
		}
		
		// Special handling for MOUSE_THROTTLE=0 (comment out instead)
		if (strcmp(escaped_value, "0_COMMENT_OUT") == 0)
		{
			if (file_value || is_commented)
			{
				// Comment out the existing line and set it to 1
				snprintf(cmd, sizeof(cmd),
					"sed -i '/^\\[MiSTer\\]/,/^\\[.*\\]/{/^[[:space:]]*[;#]*[[:space:]]*%s[[:space:]]*=/{s/^[[:space:]]*[;#]*[[:space:]]*%s[[:space:]]*=.*/;%s=1/;}}' \"%s\"",
					lowercase_name, lowercase_name, lowercase_name, filepath);
			}
			else
			{
				// Add commented line
				snprintf(cmd, sizeof(cmd),
					"sed -i '/^\\[MiSTer\\]/a ;%s=1' \"%s\"",
					lowercase_name, filepath);
			}
		}
		else
		{
			// Use sed to update or add the value
			if (file_value || is_commented)
			{
				if (is_commented)
				{
					// Uncomment and update the value - need to escape special regex chars
					snprintf(cmd, sizeof(cmd),
						"sed -i '/^\\[MiSTer\\]/,/^\\[.*\\]/{/^[[:space:]]*[;#][[:space:]]*%s[[:space:]]*=/{s/^[[:space:]]*[;#][[:space:]]*%s[[:space:]]*=.*/%s=%s/;}}' \"%s\"",
						lowercase_name, lowercase_name, lowercase_name, escaped_value, filepath);
				}
				else
				{
					// Update existing uncommented value
					snprintf(cmd, sizeof(cmd),
						"sed -i '/^\\[MiSTer\\]/,/^\\[.*\\]/{/^[[:space:]]*%s[[:space:]]*=/{s/^[[:space:]]*%s[[:space:]]*=.*/%s=%s/;}}' \"%s\"",
						lowercase_name, lowercase_name, lowercase_name, escaped_value, filepath);
				}
			}
			else
			{
				// Add new value after [MiSTer] line (missing setting that differs from default)
				snprintf(cmd, sizeof(cmd),
					"sed -i '/^\\[MiSTer\\]/a %s=%s' \"%s\"",
					lowercase_name, escaped_value, filepath);
				if (!strcasecmp(var->name, "BOOTSCREEN"))
				{
					printf("DEBUG: BOOTSCREEN sed command: %s\n", cmd);
				}
			}
		}
		
		
		int sed_result = system(cmd);
		if (!strcasecmp(var->name, "BOOTSCREEN"))
		{
			printf("DEBUG: BOOTSCREEN sed result: %d\n", sed_result);
		}
		
		if (sed_result != 0)
		{
			printf("Error: sed command failed for %s\n", var->name);
			printf("Restoring backup...\n");
			snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s\"", backuppath, filepath);
			system(cmd);
			return 0;
		}
	}
	
	// Clean up sed backup files
	snprintf(cmd, sizeof(cmd), "rm -f \"%s.sed\"", filepath);
	system(cmd);
	
	printf("DEBUG: Total entries in ini_vars: %d\n", nvars);
	printf("DEBUG: Entries actually processed: %d\n", processed);
	printf("Configuration saved to: %s\n", filepath);
	return 1;
}

// Get global setting value from [MiSTer] section for comparison with core-specific values
static char* get_global_setting_value(const char* filepath, const char* lowercase_name)
{
	static char global_value[512];
	global_value[0] = '\0';
	
	FILE *fp = fopen(filepath, "r");
	if (!fp) return NULL;
	
	char line[1024];
	int in_mister_section = 0;
	int is_commented = 0;
	
	while (fgets(line, sizeof(line), fp))
	{
		char *trimmed = line;
		while (*trimmed && CHAR_IS_SPACE(*trimmed)) trimmed++;
		
		// Check for section headers
		if (*trimmed == '[')
		{
			in_mister_section = (strncasecmp(trimmed, "[MiSTer]", 8) == 0);
			continue;
		}
		
		// Only look for variables in [MiSTer] section
		if (in_mister_section)
		{
			char *extracted = extract_ini_file_value(trimmed, lowercase_name, &is_commented);
			if (extracted && !is_commented) // Only uncommented values count as global
			{
				strncpy(global_value, extracted, sizeof(global_value) - 1);
				global_value[sizeof(global_value) - 1] = '\0';
				fclose(fp);
				return global_value;
			}
		}
	}
	fclose(fp);
	return NULL;
}

// Save core-specific configuration to [CoreName] section
int cfg_save_core_specific(uint8_t alt)
{
	// Get current core name
	const char *core_name = user_io_get_core_name(0);
	if (!core_name || !core_name[0])
	{
		printf("DEBUG: Not saving core-specific settings - no core loaded\n");
		return 0;
	}
	
	// Special case: MENU core maps to [MiSTer] section
	const char *section_name;
	if (!strcasecmp(core_name, "MENU"))
	{
		section_name = "MiSTer";
		printf("DEBUG: MENU core detected - saving to [MiSTer] section\n");
		// For MENU core, just use the regular cfg_save function
		return cfg_save(alt);
	}
	else
	{
		section_name = core_name;
	}
	
	const char *ini_filename = cfg_get_name(alt);
	char filepath[1024];
	char backuppath[1024];
	
	printf("DEBUG: Saving core-specific settings to [%s] section in %s\n", section_name, ini_filename);
	
	// Create file paths
	snprintf(filepath, sizeof(filepath), "%s/%s", getRootDir(), ini_filename);
	snprintf(backuppath, sizeof(backuppath), "%s.temp", filepath);
	
	// Create backup
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" 2>/dev/null", filepath, backuppath);
	system(cmd);
	
	// Check if file exists and has the core section
	FILE *check_fp = fopen(filepath, "r");
	int has_core_section = 0;
	int file_exists = (check_fp != NULL);
	
	if (check_fp)
	{
		char line[1024];
		while (fgets(line, sizeof(line), check_fp))
		{
			char *trimmed = line;
			while (*trimmed && CHAR_IS_SPACE(*trimmed)) trimmed++;
			
			if (*trimmed == '[' && strncasecmp(trimmed + 1, section_name, strlen(section_name)) == 0)
			{
				has_core_section = 1;
				break;
			}
		}
		fclose(check_fp);
	}
	
	// Track whether we need to create the section
	int need_to_create_section = (!file_exists || !has_core_section);
	int any_settings_to_save = 0;
	
	// Determine which settings can be saved to core sections based on categories
	// Include settings from categories that are accessible through the core settings menu
	const osd_category_t allowed_categories[] = {
		CAT_AV_DIGITAL,        // HDMI Video settings
		CAT_AV_ANALOG,      // Analog Video settings
		CAT_INPUT_CONTROLLER,  // Controller settings
		CAT_INPUT_ARCADE,      // Arcade Input settings
		CAT_INPUT_KB_MOUSE,    // Keyboard & Mouse settings
		CAT_UI,                // UI settings
		(osd_category_t)-1     // End marker
	};
	
	// Blacklist specific settings that are in allowed categories but shouldn't be core-specific
	// Keep this minimal - only for settings that should NEVER be core-specific
	const char* blacklisted_settings[] = {
		NULL  // Currently empty - let menu accessibility determine what's saveable
	};
	
	// Process each variable that might need core-specific saving
	for (int i = 0; i < nvars; i++)
	{
		const ini_var_t *var = &ini_vars[i];
		
		// Skip array types for now
		if (var->type == INI_UINT32ARR || var->type == INI_HEX32ARR || var->type == INI_STRINGARR)
			continue;
		
		// Check if this variable's category is allowed for core settings
		int is_allowed = 0;
		for (int c = 0; allowed_categories[c] != (osd_category_t)-1; c++)
		{
			if (var->category == allowed_categories[c])
			{
				is_allowed = 1;
				break;
			}
		}
		
		// Skip variables from non-allowed categories
		if (!is_allowed)
		{
			printf("DEBUG: Skipping variable from non-allowed category: %s (category %d)\n", var->name, var->category);
			continue;
		}
		
		// Special case for SYNC_MODE: sync to individual settings and skip direct saving
		if (!strcasecmp(var->name, "SYNC_MODE"))
		{
			cfg_sync_mode_to_individual();
			continue; // Don't save SYNC_MODE directly to INI
		}
		
		// Check if this variable is blacklisted
		int is_blacklisted = 0;
		for (int b = 0; blacklisted_settings[b] != NULL; b++)
		{
			if (strcasecmp(var->name, blacklisted_settings[b]) == 0)
			{
				is_blacklisted = 1;
				break;
			}
		}
		
		// Skip blacklisted variables
		if (is_blacklisted)
		{
			printf("DEBUG: Skipping blacklisted variable: %s\n", var->name);
			continue;
		}
		
		char current_value[512];
		
		// Special case for YPBPR: always write 0 (legacy compatibility)
		if (!strcasecmp(var->name, "YPBPR"))
		{
			strcpy(current_value, "0");
		}
		// Special case for MOUSE_THROTTLE: if 0, comment out with value 1
		else if (!strcasecmp(var->name, "MOUSE_THROTTLE"))
		{
			format_ini_value(current_value, sizeof(current_value), var);
			if (strcmp(current_value, "0") == 0)
			{
				strcpy(current_value, "0_COMMENT_OUT");
			}
		}
		else
		{
			format_ini_value(current_value, sizeof(current_value), var);
		}
		
		// Only process if we have a value to write
		if (strlen(current_value) == 0)
			continue;
		
		// Convert variable name to lowercase for sed (file uses lowercase)
		char lowercase_name[256];
		for (int i = 0; var->name[i] && i < 255; i++)
		{
			lowercase_name[i] = tolower(var->name[i]);
		}
		lowercase_name[strlen(var->name)] = '\0';
		
		// Get the global [MiSTer] value for comparison
		char *global_value = get_global_setting_value(filepath, lowercase_name);
		
		// Only save core-specific setting if it differs from the global [MiSTer] value
		// If no global value exists, don't save it unless it's been explicitly changed
		int should_save = 0;
		if (global_value)
		{
			// Compare against global [MiSTer] value
			should_save = (strcmp(current_value, global_value) != 0);
			if (should_save)
			{
				printf("DEBUG: %s differs from [MiSTer] value: '%s' != '%s'\n", 
					var->name, current_value, global_value);
			}
		}
		else
		{
			// No global value exists - check if current differs from default
			// Only save if user changed it from the hardcoded default
			should_save = value_differs_from_default(var);
			if (should_save)
			{
				printf("DEBUG: %s differs from default (no [MiSTer] entry): current='%s'\n", 
					var->name, current_value);
			}
		}
		
		if (!should_save)
			continue;
		
		printf("DEBUG: Core setting %s - global='%s', current='%s'\n", 
			var->name, global_value ? global_value : "NULL", current_value);
		
		// Check if this value exists in the core section
		FILE *fp = fopen(filepath, "r");
		if (!fp) continue;
		
		char line[1024];
		char *core_file_value = NULL;
		int in_core_section = 0;
		int is_commented = 0;
		
		while (fgets(line, sizeof(line), fp))
		{
			char *trimmed = line;
			while (*trimmed && CHAR_IS_SPACE(*trimmed)) trimmed++;
			
			// Check for section headers
			if (*trimmed == '[')
			{
				in_core_section = (strncasecmp(trimmed + 1, section_name, strlen(section_name)) == 0);
				continue;
			}
			
			// Only look for variables in core section
			if (in_core_section)
			{
				char *extracted = extract_ini_file_value(trimmed, lowercase_name, &is_commented);
				if (extracted)
				{
					core_file_value = extracted;
					break;
				}
			}
		}
		fclose(fp);
		
		// Check if update is needed
		if (!value_needs_update(var, core_file_value) && !is_commented)
			continue;
		
		// Escape special characters in current_value for sed
		char escaped_value[1024];
		int j = 0;
		for (int i = 0; current_value[i] && j < 1023; i++)
		{
			if (current_value[i] == '/' || current_value[i] == '&' || current_value[i] == '\\')
			{
				escaped_value[j++] = '\\';
			}
			escaped_value[j++] = current_value[i];
		}
		escaped_value[j] = '\0';
		
		// Create section if needed and this is our first setting to save
		if (need_to_create_section && !any_settings_to_save)
		{
			FILE *fp = fopen(filepath, file_exists ? "a" : "w");
			if (!fp)
			{
				printf("Failed to create/modify INI file: %s\n", filepath);
				return 0;
			}
			
			if (file_exists)
				fprintf(fp, "\n");
			fprintf(fp, "[%s]\n", section_name);
			fclose(fp);
			
			// Mark that section now exists
			has_core_section = 1;
		}
		
		// Mark that we have at least one setting to save
		any_settings_to_save = 1;
		
		// Special handling for MOUSE_THROTTLE=0 (comment out instead)
		if (strcmp(escaped_value, "0_COMMENT_OUT") == 0)
		{
			if (core_file_value || is_commented)
			{
				// Comment out the existing line and set it to 1
				snprintf(cmd, sizeof(cmd),
					"sed -i '/^\\[%s\\]/,/^\\[.*\\]/{/^[[:space:]]*[;#]*[[:space:]]*%s[[:space:]]*=/{s/^[[:space:]]*[;#]*[[:space:]]*%s[[:space:]]*=.*/;%s=1/;}}' \"%s\"",
					section_name, lowercase_name, lowercase_name, lowercase_name, filepath);
			}
			else
			{
				// Add commented line to core section
				snprintf(cmd, sizeof(cmd),
					"sed -i '/^\\[%s\\]/a\\;%s=1' \"%s\"",
					section_name, lowercase_name, filepath);
			}
		}
		else
		{
			// Use sed to update or add the value in core section
			if (core_file_value || is_commented)
			{
				if (is_commented)
				{
					// Uncomment and update the value
					snprintf(cmd, sizeof(cmd),
						"sed -i '/^\\[%s\\]/,/^\\[.*\\]/{/^[[:space:]]*[;#][[:space:]]*%s[[:space:]]*=/{s/^[[:space:]]*[;#][[:space:]]*%s[[:space:]]*=.*/%s=%s/;}}' \"%s\"",
						section_name, lowercase_name, lowercase_name, lowercase_name, escaped_value, filepath);
				}
				else
				{
					// Update existing uncommented value
					snprintf(cmd, sizeof(cmd),
						"sed -i '/^\\[%s\\]/,/^\\[.*\\]/{/^[[:space:]]*%s[[:space:]]*=/{s/^[[:space:]]*%s[[:space:]]*=.*/%s=%s/;}}' \"%s\"",
						section_name, lowercase_name, lowercase_name, lowercase_name, escaped_value, filepath);
				}
			}
			else
			{
				// Add new value after [CoreName] line
				snprintf(cmd, sizeof(cmd),
					"sed -i '/^\\[%s\\]/a\\%s=%s' \"%s\"",
					section_name, lowercase_name, escaped_value, filepath);
			}
		}
		
		if (system(cmd) != 0)
		{
			printf("Error: sed command failed for %s in [%s] section\n", var->name, core_name);
			printf("Restoring backup...\n");
			snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s\"", backuppath, filepath);
			system(cmd);
			return 0;
		}
	}
	
	// Clean up variables from non-allowed categories from the core section
	printf("DEBUG: Cleaning up variables from non-allowed categories from [%s] section\n", section_name);
	for (int i = 0; i < nvars; i++)
	{
		const ini_var_t *var = &ini_vars[i];
		
		// Skip array types
		if (var->type == INI_UINT32ARR || var->type == INI_HEX32ARR || var->type == INI_STRINGARR)
			continue;
		
		// Check if this variable's category is allowed
		int is_allowed = 0;
		for (int c = 0; allowed_categories[c] != (osd_category_t)-1; c++)
		{
			if (var->category == allowed_categories[c])
			{
				is_allowed = 1;
				break;
			}
		}
		
		// Check if this variable is blacklisted
		int is_blacklisted = 0;
		if (is_allowed)
		{
			for (int b = 0; blacklisted_settings[b] != NULL; b++)
			{
				if (strcasecmp(var->name, blacklisted_settings[b]) == 0)
				{
					is_blacklisted = 1;
					break;
				}
			}
		}
		
		// If not allowed or blacklisted, remove it from the core section
		if (!is_allowed || is_blacklisted)
		{
			char lowercase_name[256];
			for (int j = 0; var->name[j] && j < 255; j++)
			{
				lowercase_name[j] = tolower(var->name[j]);
			}
			lowercase_name[strlen(var->name)] = '\0';
			
			// Remove the line from core section
			snprintf(cmd, sizeof(cmd),
				"sed -i '/^\\[%s\\]/,/^\\[.*\\]/{/^[[:space:]]*[;#]*[[:space:]]*%s[[:space:]]*=/d}' \"%s\"",
				section_name, lowercase_name, filepath);
			system(cmd);
		}
	}
	
	// Clean up sed backup files
	snprintf(cmd, sizeof(cmd), "rm -f \"%s.sed\"", filepath);
	system(cmd);
	
	// If no settings were saved and section exists, remove the empty section
	if (!any_settings_to_save && has_core_section)
	{
		// Use awk to remove empty sections - more reliable than sed
		snprintf(cmd, sizeof(cmd),
			"awk 'BEGIN{p=1} /^\\[%s\\]/{p=0; hold=$0; next} "
			"/^\\[.*\\]/{if(!p && !content) print hold; p=1; content=0} "
			"p{print} !p && /^[^[]/{content=1; if(hold){print hold; hold=\"\"} print}' "
			"\"%s\" > \"%s.tmp\" && mv \"%s.tmp\" \"%s\"",
			section_name, filepath, filepath, filepath, filepath);
		system(cmd);
		
		printf("No core-specific settings to save - empty [%s] section removed\n", core_name);
	}
	else if (any_settings_to_save)
	{
		printf("Core-specific configuration saved to [%s] section in: %s\n", core_name, filepath);
	}
	else
	{
		printf("No core-specific settings to save - [%s] section not created\n", core_name);
	}
	
	return 1;
}

// Reset all settings to factory defaults
void cfg_reset_all()
{
	printf("Resetting all settings to factory defaults...\n");
	
	// Get the current INI file path
	char filepath[256];
	snprintf(filepath, sizeof(filepath), "%s/%s", getRootDir(), cfg_get_name(altcfg(-1)));
	
	// Set the cfg structure to factory defaults first
	memset(&cfg, 0, sizeof(cfg_t));
	
	// Apply documented MiSTer defaults
	cfg.vga_scaler = 0;               // No VGA scaler
	cfg.forced_scandoubler = 0;       // No forced scandoubler
	cfg.csync = 1;                    // Composite sync enabled (most compatible)
	cfg.vga_sog = 0;                  // No sync-on-green
	cfg.direct_video = 0;             // Scaler enabled
	cfg.vsync_adjust = 0;             // Auto vsync
	cfg.hdmi_audio_96k = 0;           // 48kHz audio
	cfg.dvi_mode = 0;                 // HDMI mode
	cfg.hdmi_limited = 0;             // Full range
	cfg.vscale_mode = 0;              // Interpolation filter
	cfg.vscale_border = 0;            // No border
	cfg.menu_pal = 0;                 // NTSC menu
	cfg.vrr_mode = 0;                 // VRR off
	cfg.vrr_min_framerate = 0;        // Auto
	cfg.vrr_max_framerate = 0;        // Auto
	cfg.vrr_vesa_framerate = 0;       // Auto
	
	// Input settings
	cfg.mouse_throttle = 10;          // 10% mouse speed
	cfg.kbd_nomouse = 0;              // Mouse enabled
	cfg.sniper_mode = 0;              // Sniper mode off
	cfg.browse_expand = 1;            // Expand file browser
	cfg.gamepad_defaults = 1;         // Positional mapping
	cfg.wheel_force = 50;             // 50% force feedback
	cfg.spinner_throttle = 0;         // No spinner throttle
	cfg.spinner_axis = 0;             // Default axis
	
	// UI settings
	cfg.bootscreen = 1;               // Show boot screen
	cfg.osd_timeout = 30;             // 30 seconds OSD timeout
	cfg.osd_rotate = 0;               // No rotation
	cfg.logo = 1;                     // Show logo
	cfg.recents = 1;                  // Track recent files
	cfg.rbf_hide_datecode = 0;        // Show core dates
	cfg.video_info = 0;               // No video info overlay
	cfg.controller_info = 6;          // 6 seconds controller info
	
	// System settings
	cfg.bootcore_timeout = 10;        // 10 seconds boot timeout
	cfg.fb_size = 1;                  // Standard framebuffer
	cfg.fb_terminal = 0;              // No terminal
	
	// Apply default VGA mode auto-configuration
	cfg_auto_configure_vga_settings(0); // RGB mode
	
	// Clear temporary settings if any
	cfg_temp_settings_clear();
	
	// Simple and safe: rewrite the entire INI file with defaults
	// Read the existing file, preserve non-[MiSTer] sections, replace [MiSTer] section
	FILE *original_fp = fopen(filepath, "r");
	if (!original_fp)
	{
		printf("Error: Could not read %s\n", filepath);
		return;
	}
	
	// Create a temporary file
	char temp_filepath[256];
	snprintf(temp_filepath, sizeof(temp_filepath), "%s.tmp", filepath);
	FILE *temp_fp = fopen(temp_filepath, "w");
	if (!temp_fp)
	{
		fclose(original_fp);
		printf("Error: Could not create temporary file\n");
		return;
	}
	
	// Copy everything except [MiSTer] section
	char line[512];
	bool in_mister_section = false;
	bool wrote_mister_section = false;
	
	while (fgets(line, sizeof(line), original_fp))
	{
		// Check for section headers
		if (line[0] == '[')
		{
			if (strncasecmp(line, "[MiSTer]", 8) == 0)
			{
				// Write our clean [MiSTer] section
				fprintf(temp_fp, "[MiSTer]\n");
				in_mister_section = true;
				wrote_mister_section = true;
				continue;
			}
			else
			{
				// Different section - copy it
				in_mister_section = false;
				fprintf(temp_fp, "%s", line);
				continue;
			}
		}
		
		// If we're in [MiSTer] section, skip the line (we already wrote the clean header)
		if (in_mister_section)
			continue;
		
		// Copy other sections' content
		fprintf(temp_fp, "%s", line);
	}
	
	// If there was no [MiSTer] section, add it
	if (!wrote_mister_section)
	{
		fprintf(temp_fp, "\n[MiSTer]\n");
	}
	
	fclose(original_fp);
	fclose(temp_fp);
	
	// Replace the original file with the temporary file
	char backup_filepath[256];
	snprintf(backup_filepath, sizeof(backup_filepath), "%s.bak", filepath);
	rename(filepath, backup_filepath);  // Create backup
	rename(temp_filepath, filepath);    // Replace with new file
	
	printf("All settings have been reset to factory defaults.\n");
}

// Reset core-specific settings to factory defaults
void cfg_reset_core_specific()
{
	const char *core_name = user_io_get_core_name(0);
	if (!core_name || !core_name[0] || !strcasecmp(core_name, "MENU"))
	{
		printf("No core loaded, cannot reset core-specific settings\n");
		return;
	}
	
	printf("Resetting %s settings to factory defaults...\n", core_name);
	
	// For core-specific settings, we actually delete the section from the INI file
	// This will cause the settings to revert to the global defaults
	
	// Load current ini file to determine section name
	char section_name[64];
	// Handle cores with special characters by using encoded names
	snprintf(section_name, sizeof(section_name), "%s", core_name);
	// Replace special characters in section name
	for (int i = 0; section_name[i]; i++)
	{
		if (section_name[i] == ' ' || section_name[i] == '(' || section_name[i] == ')' || 
		    section_name[i] == '[' || section_name[i] == ']' || section_name[i] == '.')
		{
			section_name[i] = '_';
		}
	}
	
	// Get the current INI file path
	char filepath[256];
	snprintf(filepath, sizeof(filepath), "%s/%s", getRootDir(), cfg_get_name(altcfg(-1)));
	
	// Create a temporary file without the core section
	char temp_filepath[256];
	snprintf(temp_filepath, sizeof(temp_filepath), "%s.tmp", filepath);
	
	// Use sed to remove the core section
	char cmd[512];
	snprintf(cmd, sizeof(cmd), 
		"sed -i.bak '/^\\[%s\\]/,/^\\[/{/^\\[%s\\]/d;/^\\[/!d;}' %s",
		section_name, section_name, filepath);
	system(cmd);
	
	// Also clear any core-specific settings from memory
	// This is a simplified approach - in a real implementation you might want to
	// track which settings are core-specific and reset only those
	cfg_temp_settings_clear();
	
	printf("%s settings have been reset to factory defaults.\n", core_name);
}

// Print category organization for debugging
void cfg_print_category_organization(void)
{
	printf("\n=== Settings by Category ===\n");
	
	for (int cat = 0; cat < CAT_COUNT; cat++)
	{
		const osd_category_info_t* cat_info = cfg_get_category_info((osd_category_t)cat);
		const ini_var_t** settings = NULL;
		int count = 0;
		
		cfg_get_settings_for_category((osd_category_t)cat, &settings, &count, MENU_BOTH);
		
		printf("\n%s (%d settings)\n", cat_info->name, count);
		printf("  %s\n", cat_info->description);
		printf("  Settings:\n");
		
		for (int i = 0; i < count; i++)
		{
			const ini_var_t* var = settings[i];
			printf("    - %-25s: %s\n", var->name, var->display_name);
		}
	}
	
	printf("\n");
}

// Generate a dynamic menu for a category
int cfg_generate_category_menu(osd_category_t category, int menu_offset, int* menusub, const char* title, menu_flags_t menu_type, int* first_visible)
{
	const ini_var_t** settings = NULL;
	int count = 0;
	int m = menu_offset;
	char s[256];
	
	// Get sorted settings for this category
	cfg_get_settings_for_category(category, &settings, &count, menu_type);
	
	// Set category title if provided
	if (title) {
		OsdSetTitle(title, 0);
	}
	
	// Add blank line
	OsdWrite(m++);
	
	// Calculate scrolling parameters
	int total_settings = count;
	int visible_lines = OsdGetSize() - 3; // Reserve space for title, blank line
	bool has_accept_button = (category == CAT_AV_DIGITAL || category == CAT_AV_ANALOG);
	// Don't reserve space for Accept button - allow it to use the very bottom row
	
	// Initialize first_visible if needed
	if (!first_visible || *first_visible < 0) {
		static int first_visible_fallback = 0;
		first_visible = &first_visible_fallback;
		*first_visible = 0;
	}
	
	// First, build a list of all displayable items (enabled + stippled)
	struct menu_item {
		const ini_var_t* var;
		bool is_stippled;
		bool is_selectable;
	};
	
	struct menu_item display_items[count];
	int display_count = 0;
	int total_selectable_settings = 0;
	
	for (int i = 0; i < count; i++) {
		const ini_var_t* var = settings[i];
		
		// Skip hidden settings
		if (var->menu_position < 0) continue;
		
		bool is_enabled = cfg_is_setting_enabled(var->name);
		bool is_stippled = is_setting_stippled(var->name);
		
		// Special handling: if a setting should be stippled, always show it regardless of enabled state
		if (is_stippled) {
			is_enabled = true; // Force show stippled settings
		}
		
		// Skip disabled settings that aren't stippled
		if (!is_enabled && !is_stippled) continue;
		
		display_items[display_count].var = var;
		display_items[display_count].is_stippled = is_stippled;
		display_items[display_count].is_selectable = !is_stippled;
		display_count++;
		
		if (!is_stippled) total_selectable_settings++;
	}
	
	// Adjust scroll position based on current selection
	int scroll_margin = 3;
	if (visible_lines < scroll_margin * 2) scroll_margin = 1;
	
	// Convert menusub (selectable index) to display index for scrolling calculations
	int current_display_index = -1;
	if (menusub) {
		int selectable_count = 0;
		for (int i = 0; i < display_count; i++) {
			if (display_items[i].is_selectable) {
				if (selectable_count == *menusub) {
					current_display_index = i;
					break;
				}
				selectable_count++;
			}
		}
	}
	
	// Adjust scroll position based on display index
	if (current_display_index >= 0) {
		if (current_display_index < *first_visible + scroll_margin) {
			*first_visible = current_display_index - scroll_margin;
			if (*first_visible < 0) *first_visible = 0;
		}
		if (current_display_index >= *first_visible + visible_lines - scroll_margin) {
			*first_visible = current_display_index - visible_lines + scroll_margin + 1;
		}
	}
	
	// Ensure scroll bounds
	if (*first_visible < 0) *first_visible = 0;
	if (*first_visible > display_count - visible_lines && display_count > visible_lines) {
		*first_visible = display_count - visible_lines;
	}
	
	// Generate menu items for visible range
	int selectable_index = 0; // Index in selectable items (matches menusub)
	
	for (int i = 0; i < display_count; i++) {
		// Skip items before visible range
		if (i < *first_visible) {
			if (display_items[i].is_selectable) selectable_index++;
			continue;
		}
		
		// Stop if we've filled visible lines
		if (i >= *first_visible + visible_lines) break;
		
		const ini_var_t* var = display_items[i].var;
		bool is_stippled = display_items[i].is_stippled;
		
		// Render the setting value
		cfg_render_setting_value(s, sizeof(s), var->name, var->display_name);
		
		// Write to OSD - only selectable items can be selected
		bool selected = !is_stippled && (menusub && *menusub == selectable_index);
		unsigned char stipple_flag = is_stippled ? 1 : 0;
		OsdWrite(m++, s, selected, stipple_flag);
		
		if (!is_stippled) selectable_index++;
	}
	
	// Add Accept button for Digital and Analog AV categories
	if (category == CAT_AV_DIGITAL || category == CAT_AV_ANALOG)
	{
		// Add blank line
		OsdWrite(m++);
		
		// Add Accept button
		bool accept_selected = (menusub && *menusub == total_selectable_settings);
		OsdWrite(m++, " >>>>>>>>> ACCEPT <<<<<<<<<", accept_selected);
		total_selectable_settings++; // Include Accept button in total count
	}
	
	// Fill remaining lines
	while (m < OsdGetSize()) OsdWrite(m++);
	
	return total_selectable_settings; // Return count of selectable items including Accept button
}

// Handle setting value change (increment/decrement)
void cfg_handle_setting_change(const ini_var_t* setting, int direction)
{
	if (!setting) {
		printf("DEBUG: setting is NULL\n");
		return;
	}
	
	if (!cfg_is_setting_enabled(setting->name)) {
		printf("DEBUG: setting %s is disabled\n", setting->name);
		return; // Don't change disabled or invalid settings
	}
	
	printf("DEBUG: Changing setting %s (type=%d, direction=%d)\n", setting->name, setting->type, direction);
	
	// Get the step size for this setting
	uint8_t step_size = cfg_auto_detect_step_size(setting);
	
	// Handle different setting types
	switch (setting->type)
	{
		case INI_UINT8:
		{
			uint8_t* value = (uint8_t*)setting->var;
			int new_value = *value + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			*value = new_value;
			
			// Handle special cases that need sync
			if (strcmp(setting->name, "COMPOSITE_SYNC") == 0 || strcmp(setting->name, "VGA_SOG") == 0) {
				cfg_sync_individual_to_mode();
			}
			break;
		}
		
		case INI_UINT16:
		{
			uint16_t* value = (uint16_t*)setting->var;
			int new_value = *value + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			*value = new_value;
			break;
		}
		
		case INI_UINT32:
		{
			uint32_t* value = (uint32_t*)setting->var;
			int new_value = *value + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			*value = new_value;
			break;
		}
		
		case INI_INT8:
		{
			int8_t* value = (int8_t*)setting->var;
			int new_value = *value + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			*value = new_value;
			break;
		}
		
		case INI_INT16:
		{
			int16_t* value = (int16_t*)setting->var;
			int new_value = *value + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			*value = new_value;
			break;
		}
		
		case INI_INT32:
		{
			int32_t* value = (int32_t*)setting->var;
			int new_value = *value + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			*value = new_value;
			break;
		}
		
		case INI_FLOAT:
		{
			float* value = (float*)setting->var;
			float new_value = *value + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			*value = new_value;
			break;
		}
		
		// For string types, we'd need file picker or special handling
		case INI_STRING:
		{
			// Special handling for VGA_MODE cycling
			if (strcmp(setting->name, "VGA_MODE") == 0)
			{
				const char* vga_modes[] = {"rgb", "ypbpr", "svideo", "cvbs"};
				int num_modes = sizeof(vga_modes) / sizeof(vga_modes[0]);
				
				char* current_mode = (char*)setting->var;
				int current_index = 0;
				
				// Find current mode index
				for (int i = 0; i < num_modes; i++) {
					if (strcasecmp(current_mode, vga_modes[i]) == 0) {
						current_index = i;
						break;
					}
				}
				
				// Calculate next index with wrapping
				int new_index = current_index + direction;
				if (new_index < 0) new_index = num_modes - 1;
				if (new_index >= num_modes) new_index = 0;
				
				// Set new mode
				strncpy(current_mode, vga_modes[new_index], setting->max - 1);
				current_mode[setting->max - 1] = '\0';
				
				// Update vga_mode_int to match the string
				if (!strcasecmp(vga_modes[new_index], "rgb")) cfg.vga_mode_int = 0;
				else if (!strcasecmp(vga_modes[new_index], "ypbpr")) cfg.vga_mode_int = 1;
				else if (!strcasecmp(vga_modes[new_index], "svideo")) cfg.vga_mode_int = 2;
				else if (!strcasecmp(vga_modes[new_index], "cvbs")) cfg.vga_mode_int = 3;
				
				// Auto-configure related settings based on VGA mode
				cfg_auto_configure_vga_settings(cfg.vga_mode_int);
				
				printf("DEBUG: VGA_MODE changed from '%s' to '%s', vga_mode_int=%d\n", 
					   vga_modes[current_index], vga_modes[new_index], cfg.vga_mode_int);
			}
			// Special handling for VIDEO_MODE cycling
			else if (strcmp(setting->name, "VIDEO_MODE") == 0)
			{
				const char* video_modes[] = {"auto", "1920x1080", "1280x720", "720x480", "720x576"};
				int num_modes = sizeof(video_modes) / sizeof(video_modes[0]);
				
				char* current_mode = (char*)setting->var;
				int current_index = 0;
				
				// Find current mode index (handle empty string as "auto")
				if (strlen(current_mode) == 0) {
					current_index = 0; // auto
				} else {
					for (int i = 0; i < num_modes; i++) {
						if (strcasecmp(current_mode, video_modes[i]) == 0) {
							current_index = i;
							break;
						}
					}
				}
				
				// Calculate next index with wrapping
				int new_index = current_index + direction;
				if (new_index < 0) new_index = num_modes - 1;
				if (new_index >= num_modes) new_index = 0;
				
				// Set new mode (auto is represented as empty string)
				if (new_index == 0) {
					current_mode[0] = '\0'; // auto = empty string
				} else {
					strncpy(current_mode, video_modes[new_index], setting->max - 1);
					current_mode[setting->max - 1] = '\0';
				}
				
				printf("DEBUG: VIDEO_MODE changed from '%s' to '%s'\n", 
					   video_modes[current_index], new_index == 0 ? "auto" : video_modes[new_index]);
			}
			// Special handling for file picker settings - open file browser
			else if (strstr(setting->name, "DEFAULT") || strstr(setting->name, "FILTER") || 
			         strstr(setting->name, "FONT") || strstr(setting->name, "PRESET"))
			{
				// For file picker settings, we need to open a file browser
				printf("DEBUG: File picker requested for %s - implementing file browser\n", setting->name);
				printf("DEBUG: Current file picker state before setting: %p\n", (void*)cfg_file_picker_current_setting);
				
				// Store the current setting for file picker callback
				cfg_set_file_picker_setting(setting);
				printf("DEBUG: File picker state after setting: %p\n", (void*)cfg_file_picker_current_setting);
				
				// Determine directory and file extension based on setting type
				const char* file_ext = "";
				const char* subdir = "";
				
				if (strstr(setting->name, "AFILTER")) {
					subdir = "filters_audio";
					file_ext = "TXT";
				} else if (strstr(setting->name, "VFILTER")) {
					subdir = "filters";
					file_ext = "TXT";
				} else if (strstr(setting->name, "SHMASK")) {
					subdir = "shadow_masks";
					file_ext = "TXT";
				} else if (strstr(setting->name, "FONT")) {
					subdir = "font";
					file_ext = "PF";
				} else if (strstr(setting->name, "PRESET")) {
					subdir = "presets";
					file_ext = "INI";
				}
				
				// Set initial path - use current setting value if it exists, otherwise default directory
				char* current_path = (char*)setting->var;
				if (strlen(current_path) > 0) {
					// If setting has a value, use its directory or the full path
					// Check if current_path already includes the subdirectory
					if (strstr(current_path, subdir) == current_path) {
						// Path already includes subdirectory, use as-is
						snprintf(cfg_file_picker_initial_path, sizeof(cfg_file_picker_initial_path), 
								 "%s", current_path);
					} else {
						// Path doesn't include subdirectory, prepend it
						snprintf(cfg_file_picker_initial_path, sizeof(cfg_file_picker_initial_path), 
								 "%s/%s", subdir, current_path);
					}
				} else {
					// Default to the appropriate subdirectory
					snprintf(cfg_file_picker_initial_path, sizeof(cfg_file_picker_initial_path), 
							 "%s", subdir);
				}
				
				// Open file browser - this will trigger menu transition
				cfg_open_file_picker(cfg_file_picker_initial_path, file_ext);
				
				printf("DEBUG: File picker opened for %s with dir=%s, ext=%s\n", 
					   setting->name, cfg_file_picker_initial_path, file_ext);
			}
			break;
		}
		default:
			// Unhandled string setting type
			printf("DEBUG: Unhandled string setting: %s\n", setting->name);
			break;
	}
	
	// Handle virtual sync_mode setting
	if (strcmp(setting->name, "SYNC_MODE") == 0) {
		cfg_sync_mode_to_individual();
	}
}

// Check if a setting requires confirmation screen (audio/video settings)
bool cfg_requires_confirmation(const ini_var_t* setting)
{
	if (!setting) return false;
	
	// AV categories (CAT_AV_DIGITAL, CAT_AV_ANALOG) use temp settings, not confirmation
	if (setting->category == CAT_AV_DIGITAL || setting->category == CAT_AV_ANALOG) {
		return false; // These use temp settings system instead
	}
	
	// Only critical display settings that could break the display should require confirmation
	if (strcmp(setting->name, "MENU_PAL") == 0) return true;
	
	// The following settings should NOT require confirmation per user request:
	// VIDEO_INFO, FB_TERMINAL, OSD_TIMEOUT - these are UI settings that don't break display
	
	// The following should cycle normally without confirmation:
	// video_mode, vsync_adjust, hdmi_color_range, video_brightness/contrast/saturation/hue,
	// video_gain_offset, hdr settings, vrr settings
	
	return false;
}

// Storage for setting backup during confirmation
static struct {
	char setting_name[64];
	char old_value_str[128];
	char new_value_str[128];
	uint8_t old_uint8;
	uint16_t old_uint16;
	uint32_t old_uint32;
	int8_t old_int8;
	int16_t old_int16; 
	int32_t old_int32;
	float old_float;
	char old_string[256];
	const ini_var_t* setting_ptr;
} setting_backup = {0};

// Temporary settings system for AV menus
#define MAX_TEMP_SETTINGS 64
typedef struct {
	const ini_var_t* setting;
	union {
		uint8_t uint8_val;
		uint16_t uint16_val; 
		uint32_t uint32_val;
		int8_t int8_val;
		int16_t int16_val;
		int32_t int32_val;
		float float_val;
		char string_val[256];
	} value;
} temp_setting_t;

static struct {
	bool active;
	osd_category_t category;
	temp_setting_t initial_values[MAX_TEMP_SETTINGS];
	temp_setting_t changed_values[MAX_TEMP_SETTINGS];
	int initial_count;
	int changed_count;
} temp_settings = {0};

// Helper function to get temp value for custom renderers
static bool get_temp_uint8_value(const char* setting_name, uint8_t* value)
{
	if (!temp_settings.active || !value)
		return false;
		
	for (int i = 0; i < temp_settings.changed_count; i++)
	{
		if (strcmp(temp_settings.changed_values[i].setting->name, setting_name) == 0)
		{
			*value = temp_settings.changed_values[i].value.uint8_val;
			return true;
		}
	}
	return false;
}

// Helper function to get temp string value
static bool get_temp_string_value(const char* setting_name, char* buffer, size_t buffer_size)
{
	if (!temp_settings.active || !buffer)
		return false;
		
	for (int i = 0; i < temp_settings.changed_count; i++)
	{
		if (strcmp(temp_settings.changed_values[i].setting->name, setting_name) == 0)
		{
			strncpy(buffer, temp_settings.changed_values[i].value.string_val, buffer_size - 1);
			buffer[buffer_size - 1] = '\0';
			return true;
		}
	}
	return false;
}

// Helper function to store current value of a setting
static void store_setting_value(temp_setting_t* temp_setting, const ini_var_t* setting)
{
	temp_setting->setting = setting;
	
	switch (setting->type)
	{
		case INI_UINT8:
		case INI_HEX8:
			temp_setting->value.uint8_val = *(uint8_t*)setting->var;
			break;
		case INI_INT8:
			temp_setting->value.int8_val = *(int8_t*)setting->var;
			break;
		case INI_UINT16:
		case INI_HEX16:
			temp_setting->value.uint16_val = *(uint16_t*)setting->var;
			break;
		case INI_INT16:
			temp_setting->value.int16_val = *(int16_t*)setting->var;
			break;
		case INI_UINT32:
		case INI_HEX32:
			temp_setting->value.uint32_val = *(uint32_t*)setting->var;
			break;
		case INI_INT32:
			temp_setting->value.int32_val = *(int32_t*)setting->var;
			break;
		case INI_FLOAT:
			temp_setting->value.float_val = *(float*)setting->var;
			break;
		case INI_STRING:
			strncpy(temp_setting->value.string_val, (char*)setting->var, sizeof(temp_setting->value.string_val) - 1);
			temp_setting->value.string_val[sizeof(temp_setting->value.string_val) - 1] = '\0';
			break;
		default:
			printf("ERROR: Unsupported setting type for temp storage: %d\n", setting->type);
			break;
	}
}

// Helper function to restore value to a setting
static void restore_setting_value(const temp_setting_t* temp_setting)
{
	const ini_var_t* setting = temp_setting->setting;
	
	switch (setting->type)
	{
		case INI_UINT8:
		case INI_HEX8:
			*(uint8_t*)setting->var = temp_setting->value.uint8_val;
			break;
		case INI_INT8:
			*(int8_t*)setting->var = temp_setting->value.int8_val;
			break;
		case INI_UINT16:
		case INI_HEX16:
			*(uint16_t*)setting->var = temp_setting->value.uint16_val;
			break;
		case INI_INT16:
			*(int16_t*)setting->var = temp_setting->value.int16_val;
			break;
		case INI_UINT32:
		case INI_HEX32:
			*(uint32_t*)setting->var = temp_setting->value.uint32_val;
			break;
		case INI_INT32:
			*(int32_t*)setting->var = temp_setting->value.int32_val;
			break;
		case INI_FLOAT:
			*(float*)setting->var = temp_setting->value.float_val;
			break;
		case INI_STRING:
			strncpy((char*)setting->var, temp_setting->value.string_val, setting->max);
			break;
		default:
			printf("ERROR: Unsupported setting type for restore: %d\n", setting->type);
			break;
	}
}

// Initialize temporary settings system for a category
void cfg_temp_settings_start(osd_category_t category)
{
	printf("DEBUG: Starting temp settings for category %d\n", category);
	
	// Clear any existing temp settings
	cfg_temp_settings_clear();
	
	temp_settings.active = true;
	temp_settings.category = category;
	temp_settings.initial_count = 0;
	temp_settings.changed_count = 0;
	
	// Store initial values for all settings in this category
	const ini_var_t** settings;
	int count;
	if (cfg_get_settings_for_category(category, &settings, &count, MENU_BOTH) > 0)
	{
		for (int i = 0; i < count && temp_settings.initial_count < MAX_TEMP_SETTINGS; i++)
		{
			const ini_var_t* setting = settings[i];
			if (setting && setting->type != INI_UINT32ARR && setting->type != INI_HEX32ARR && setting->type != INI_STRINGARR)
			{
				store_setting_value(&temp_settings.initial_values[temp_settings.initial_count], setting);
				temp_settings.initial_count++;
			}
		}
	}
	
	printf("DEBUG: Stored %d initial setting values\n", temp_settings.initial_count);
}

// Helper function to apply direction change to a temp setting value
static void apply_direction_to_temp_value(temp_setting_t* temp_setting, int direction)
{
	const ini_var_t* setting = temp_setting->setting;
	
	// Get the step size for this setting
	uint8_t step_size = cfg_auto_detect_step_size(setting);
	
	switch (setting->type)
	{
		case INI_UINT8:
		case INI_HEX8:
		{
			// Special handling for SYNC_MODE in YPbPr mode
			if (strcmp(setting->name, "SYNC_MODE") == 0)
			{
				uint8_t vga_mode = get_current_vga_mode();
				if (vga_mode == 1) // YPbPr mode - skip separate sync (0)
				{
					uint8_t current_value = temp_setting->value.uint8_val;
					uint8_t new_value;
					
					if (direction > 0) // Moving forward
					{
						if (current_value == 1) new_value = 2; // Composite -> Sync-on-Green
						else new_value = 1; // Sync-on-Green -> Composite
					}
					else // Moving backward
					{
						if (current_value == 2) new_value = 1; // Sync-on-Green -> Composite
						else new_value = 2; // Composite -> Sync-on-Green
					}
					temp_setting->value.uint8_val = new_value;
					printf("DEBUG: YPbPr SYNC_MODE cycling: %d -> %d (skipping separate)\n", current_value, new_value);
				}
				else
				{
					// Normal cycling for RGB mode
					int new_value = temp_setting->value.uint8_val + direction;
					if (new_value < setting->min) new_value = setting->max;
					if (new_value > setting->max) new_value = setting->min;
					temp_setting->value.uint8_val = new_value;
				}
			}
			else
			{
				// Normal uint8 handling for other settings
				int new_value = temp_setting->value.uint8_val + (direction * step_size);
				if (new_value < setting->min) new_value = setting->max;
				if (new_value > setting->max) new_value = setting->min;
				temp_setting->value.uint8_val = new_value;
			}
			break;
		}
		case INI_INT8:
		{
			int new_value = temp_setting->value.int8_val + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			temp_setting->value.int8_val = new_value;
			break;
		}
		case INI_UINT16:
		case INI_HEX16:
		{
			int new_value = temp_setting->value.uint16_val + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			temp_setting->value.uint16_val = new_value;
			break;
		}
		case INI_INT16:
		{
			int new_value = temp_setting->value.int16_val + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			temp_setting->value.int16_val = new_value;
			break;
		}
		case INI_UINT32:
		case INI_HEX32:
		{
			int new_value = temp_setting->value.uint32_val + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			temp_setting->value.uint32_val = new_value;
			break;
		}
		case INI_INT32:
		{
			int new_value = temp_setting->value.int32_val + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			temp_setting->value.int32_val = new_value;
			break;
		}
		case INI_FLOAT:
		{
			float new_value = temp_setting->value.float_val + (direction * step_size);
			if (new_value < setting->min) new_value = setting->max;
			if (new_value > setting->max) new_value = setting->min;
			temp_setting->value.float_val = new_value;
			break;
		}
		case INI_STRING:
		{
			// Special handling for VGA_MODE cycling
			if (strcmp(setting->name, "VGA_MODE") == 0)
			{
				const char* vga_modes[] = {"rgb", "ypbpr", "svideo", "cvbs"};
				int num_modes = sizeof(vga_modes) / sizeof(vga_modes[0]);
				
				char* current_mode = temp_setting->value.string_val;
				int current_index = 0;
				
				// Find current mode index
				for (int i = 0; i < num_modes; i++) {
					if (strcasecmp(current_mode, vga_modes[i]) == 0) {
						current_index = i;
						break;
					}
				}
				
				// Calculate next index with wrapping
				int new_index = current_index + direction;
				if (new_index < 0) new_index = num_modes - 1;
				if (new_index >= num_modes) new_index = 0;
				
				// Set new mode
				strncpy(temp_setting->value.string_val, vga_modes[new_index], sizeof(temp_setting->value.string_val) - 1);
				temp_setting->value.string_val[sizeof(temp_setting->value.string_val) - 1] = '\0';
				
				// Update vga_mode_int to match the string (for dependency checking)
				if (!strcasecmp(vga_modes[new_index], "rgb")) cfg.vga_mode_int = 0;
				else if (!strcasecmp(vga_modes[new_index], "ypbpr")) cfg.vga_mode_int = 1;
				else if (!strcasecmp(vga_modes[new_index], "svideo")) cfg.vga_mode_int = 2;
				else if (!strcasecmp(vga_modes[new_index], "cvbs")) cfg.vga_mode_int = 3;
				
				// Auto-configure related settings in temp mode
				cfg_auto_configure_vga_settings_temp(cfg.vga_mode_int);
				
				printf("DEBUG: VGA_MODE temp changed from '%s' to '%s', vga_mode_int=%d\n", 
					   vga_modes[current_index], vga_modes[new_index], cfg.vga_mode_int);
			}
			// Special handling for VIDEO_MODE cycling
			else if (strcmp(setting->name, "VIDEO_MODE") == 0)
			{
				const char* video_modes[] = {"auto", "1920x1080", "1280x720", "720x480", "720x576"};
				int num_modes = sizeof(video_modes) / sizeof(video_modes[0]);
				
				char* current_mode = temp_setting->value.string_val;
				int current_index = 0;
				
				// Find current mode index (handle empty string as "auto")
				if (strlen(current_mode) == 0) {
					current_index = 0; // auto
				} else {
					for (int i = 0; i < num_modes; i++) {
						if (strcasecmp(current_mode, video_modes[i]) == 0) {
							current_index = i;
							break;
						}
					}
				}
				
				// Calculate next index with wrapping
				int new_index = current_index + direction;
				if (new_index < 0) new_index = num_modes - 1;
				if (new_index >= num_modes) new_index = 0;
				
				// Set new mode (auto is represented as empty string)
				if (new_index == 0) {
					temp_setting->value.string_val[0] = '\0'; // auto = empty string
				} else {
					strncpy(temp_setting->value.string_val, video_modes[new_index], sizeof(temp_setting->value.string_val) - 1);
					temp_setting->value.string_val[sizeof(temp_setting->value.string_val) - 1] = '\0';
				}
				
				printf("DEBUG: VIDEO_MODE temp changed from '%s' to '%s'\n", 
					   video_modes[current_index], new_index == 0 ? "auto" : video_modes[new_index]);
			}
			// Special handling for VIDEO_MODE_PAL and VIDEO_MODE_NTSC cycling
			else if (strcmp(setting->name, "VIDEO_MODE_PAL") == 0 || strcmp(setting->name, "VIDEO_MODE_NTSC") == 0)
			{
				const char* video_modes[] = {"auto", "1920x1080", "1280x720", "720x480", "720x576"};
				int num_modes = sizeof(video_modes) / sizeof(video_modes[0]);
				
				char* current_mode = temp_setting->value.string_val;
				int current_index = 0;
				
				// Find current mode index (handle empty string as "auto")
				if (strlen(current_mode) == 0) {
					current_index = 0; // auto
				} else {
					for (int i = 0; i < num_modes; i++) {
						if (strcasecmp(current_mode, video_modes[i]) == 0) {
							current_index = i;
							break;
						}
					}
				}
				
				// Calculate next index with wrapping
				int new_index = current_index + direction;
				if (new_index < 0) new_index = num_modes - 1;
				if (new_index >= num_modes) new_index = 0;
				
				// Set new mode (auto is represented as empty string)
				if (new_index == 0) {
					temp_setting->value.string_val[0] = '\0'; // auto = empty string
				} else {
					strncpy(temp_setting->value.string_val, video_modes[new_index], sizeof(temp_setting->value.string_val) - 1);
					temp_setting->value.string_val[sizeof(temp_setting->value.string_val) - 1] = '\0';
				}
				
				printf("DEBUG: %s temp changed from '%s' to '%s'\n", 
					   setting->name, video_modes[current_index], new_index == 0 ? "auto" : video_modes[new_index]);
			}
			// Special handling for CUSTOM_ASPECT_RATIO cycling
			else if (strcmp(setting->name, "CUSTOM_ASPECT_RATIO_1") == 0 || strcmp(setting->name, "CUSTOM_ASPECT_RATIO_2") == 0)
			{
				const char* aspect_ratios[] = {"", "4:3", "16:9", "16:10", "21:9", "1:1"};
				int num_ratios = sizeof(aspect_ratios) / sizeof(aspect_ratios[0]);
				
				char* current_ratio = temp_setting->value.string_val;
				int current_index = 0;
				
				// Find current ratio index
				for (int i = 0; i < num_ratios; i++) {
					if (strcasecmp(current_ratio, aspect_ratios[i]) == 0) {
						current_index = i;
						break;
					}
				}
				
				// Calculate next index with wrapping
				int new_index = current_index + direction;
				if (new_index < 0) new_index = num_ratios - 1;
				if (new_index >= num_ratios) new_index = 0;
				
				// Set new ratio
				strncpy(temp_setting->value.string_val, aspect_ratios[new_index], sizeof(temp_setting->value.string_val) - 1);
				temp_setting->value.string_val[sizeof(temp_setting->value.string_val) - 1] = '\0';
				
				printf("DEBUG: %s temp changed from '%s' to '%s'\n", 
					   setting->name, aspect_ratios[current_index], aspect_ratios[new_index]);
			}
			// Special handling for file picker settings - open file browser
			else if (strstr(setting->name, "DEFAULT") || strstr(setting->name, "FILTER") || 
			         strstr(setting->name, "FONT") || strstr(setting->name, "PRESET"))
			{
				// For file picker settings, we need to open a file browser
				// For now, just clear the path (TODO: implement full file browser)
				printf("DEBUG: File picker requested for %s - TODO: implement file browser\n", setting->name);
				
				// Temporary: toggle between empty and a sample path for testing
				if (strlen(temp_setting->value.string_val) == 0) {
					// Convert absolute path to relative path from /media/fat
					const char* sample_path = "filters/example.txt";
					strncpy(temp_setting->value.string_val, sample_path, sizeof(temp_setting->value.string_val) - 1);
					temp_setting->value.string_val[sizeof(temp_setting->value.string_val) - 1] = '\0';
					printf("DEBUG: %s temp set to sample path '%s'\n", setting->name, sample_path);
				} else {
					// Clear path
					temp_setting->value.string_val[0] = '\0';
					printf("DEBUG: %s temp cleared\n", setting->name);
				}
			}
			break;
		}
		default:
			printf("ERROR: Unsupported setting type for temp change: %d\n", setting->type);
			break;
	}
}

// Handle setting change in temp mode
void cfg_temp_settings_change(const ini_var_t* setting, int direction)
{
	printf("DEBUG: cfg_temp_settings_change called for %s, direction=%d, active=%d\n", 
		setting->name, direction, temp_settings.active);
		
	if (!temp_settings.active)
	{
		printf("ERROR: Temp settings not active\n");
		return;
	}
	
	// Check if sync mode is locked and prevent changes
	if (strcmp(setting->name, "SYNC_MODE") == 0 && is_sync_mode_locked())
	{
		printf("DEBUG: SYNC_MODE is locked in S-Video/CVBS mode, ignoring change\n");
		return;
	}
	
	// Check if setting is stippled (disabled) and prevent changes
	if (is_setting_stippled(setting->name))
	{
		printf("DEBUG: %s is stippled in current VGA mode, ignoring change\n", setting->name);
		return;
	}
	
	// Find or add to changed values array
	int found_index = -1;
	for (int i = 0; i < temp_settings.changed_count; i++)
	{
		if (temp_settings.changed_values[i].setting == setting)
		{
			found_index = i;
			break;
		}
	}
	
	if (found_index == -1)
	{
		// Add new changed setting - start with current memory value
		if (temp_settings.changed_count < MAX_TEMP_SETTINGS)
		{
			found_index = temp_settings.changed_count;
			store_setting_value(&temp_settings.changed_values[found_index], setting);
			temp_settings.changed_count++;
		}
		else
		{
			printf("ERROR: Too many temp setting changes\n");
			return;
		}
	}
	
	// Apply the direction change to the temp value (not memory)
	apply_direction_to_temp_value(&temp_settings.changed_values[found_index], direction);
	
	printf("DEBUG: Temp changed %s, total changes: %d\n", setting->name, temp_settings.changed_count);
}

// Apply all temporary changes to memory
bool cfg_temp_settings_apply(void)
{
	if (!temp_settings.active)
		return false;
	
	printf("DEBUG: Applying %d temp setting changes to memory\n", temp_settings.changed_count);
	
	// Apply all changed values to memory
	for (int i = 0; i < temp_settings.changed_count; i++)
	{
		restore_setting_value(&temp_settings.changed_values[i]);
		printf("DEBUG: Applied %s to memory\n", temp_settings.changed_values[i].setting->name);
	}
	
	// Send to hardware
	user_io_send_buttons(1);
	
	// Clear temp settings
	cfg_temp_settings_clear();
	return true;
}

// Apply temp settings but keep temp data for potential revert (used for confirmation screen)
bool cfg_temp_settings_apply_pending(void)
{
	if (!temp_settings.active)
		return false;
	
	printf("DEBUG: Applying %d temp setting changes to memory (pending confirmation)\n", temp_settings.changed_count);
	
	// Apply all changed values to memory
	for (int i = 0; i < temp_settings.changed_count; i++)
	{
		restore_setting_value(&temp_settings.changed_values[i]);
		printf("DEBUG: Applied %s to memory (pending)\n", temp_settings.changed_values[i].setting->name);
	}
	
	// Send to hardware
	user_io_send_buttons(1);
	
	// Don't clear temp settings - keep them for potential revert
	return true;
}

// Revert all changes to initial values
void cfg_temp_settings_revert(void)
{
	if (!temp_settings.active)
		return;
	
	printf("DEBUG: Reverting %d temp setting changes\n", temp_settings.changed_count);
	
	// Restore all initial values
	for (int i = 0; i < temp_settings.initial_count; i++)
	{
		restore_setting_value(&temp_settings.initial_values[i]);
	}
	
	// Send to hardware
	user_io_send_buttons(1);
	
	// Clear temp settings
	cfg_temp_settings_clear();
}

// Reject temporary settings (restore initial values but preserve changed settings for further editing)
void cfg_temp_settings_reject(void)
{
	if (!temp_settings.active)
		return;
	
	printf("DEBUG: Rejecting temp settings, preserving %d changes for further editing\n", temp_settings.changed_count);
	
	// Restore all initial values to memory (revert the preview)
	for (int i = 0; i < temp_settings.initial_count; i++)
	{
		restore_setting_value(&temp_settings.initial_values[i]);
	}
	
	// Send to hardware
	user_io_send_buttons(1);
	
	// Note: We do NOT clear temp_settings here - this preserves the changed values
	// so the user can continue editing from where they left off
}

// Clear temporary settings
void cfg_temp_settings_clear(void)
{
	printf("DEBUG: Clearing temp settings\n");
	memset(&temp_settings, 0, sizeof(temp_settings));
}

// Check if there are pending changes
bool cfg_temp_settings_has_changes(void)
{
	return temp_settings.active && temp_settings.changed_count > 0;
}

// Get temporary value for display (used by UI to show pending changes)
void cfg_get_temp_var_value_as_string(const ini_var_t* var, char* buffer, size_t buffer_size)
{
	if (!temp_settings.active)
	{
		// No temp settings active, use normal function
		cfg_get_var_value_as_string(var, buffer, buffer_size);
		return;
	}
	
	printf("DEBUG: Getting temp value for %s (active=%d, changed_count=%d)\n", 
		var->name, temp_settings.active, temp_settings.changed_count);
	
	// Look for this setting in changed values
	for (int i = 0; i < temp_settings.changed_count; i++)
	{
		printf("DEBUG: Checking temp setting %d: %s vs %s\n", i, 
			temp_settings.changed_values[i].setting->name, var->name);
			
		if (temp_settings.changed_values[i].setting == var)
		{
			printf("DEBUG: Found temp value for %s in slot %d\n", var->name, i);
			// Found a temporary value, format it
			const temp_setting_t* temp_setting = &temp_settings.changed_values[i];
			
			switch (var->type)
			{
				case INI_UINT8:
				case INI_INT8:
					snprintf(buffer, buffer_size, "%d", temp_setting->value.uint8_val);
					printf("DEBUG: Returning temp UINT8 value '%s' for %s\n", buffer, var->name);
					return;
				case INI_UINT16:
				case INI_INT16:
					snprintf(buffer, buffer_size, "%d", temp_setting->value.uint16_val);
					return;
				case INI_UINT32:
				case INI_INT32:
					snprintf(buffer, buffer_size, "%u", temp_setting->value.uint32_val);
					return;
				case INI_HEX8:
					snprintf(buffer, buffer_size, "0x%02X", temp_setting->value.uint8_val);
					return;
				case INI_HEX16:
					snprintf(buffer, buffer_size, "0x%04X", temp_setting->value.uint16_val);
					return;
				case INI_HEX32:
					snprintf(buffer, buffer_size, "0x%08X", temp_setting->value.uint32_val);
					return;
				case INI_FLOAT:
					snprintf(buffer, buffer_size, "%.2f", temp_setting->value.float_val);
					return;
				case INI_STRING:
					snprintf(buffer, buffer_size, "%s", temp_setting->value.string_val);
					printf("DEBUG: Returning temp STRING value '%s' for %s\n", buffer, var->name);
					return;
				default:
					break;
			}
		}
	}
	
	// Setting not found in temp changes, use memory value
	cfg_get_var_value_as_string(var, buffer, buffer_size);
}

// Generic apply function for confirmed setting changes
int generic_setting_apply(void)
{
	// Check if we have temporary settings to apply
	if (cfg_temp_settings_has_changes())
	{
		printf("DEBUG: Applying temp settings changes\n");
		cfg_temp_settings_apply();
		return 1;
	}
	
	// Fallback to old single-setting confirmation
	user_io_send_buttons(1);
	printf("DEBUG: Applied setting %s\n", setting_backup.setting_name);
	return 1;
}

// Generic revert function for setting changes
int generic_setting_revert(void)
{
	// Check if we have temporary settings to revert
	if (temp_settings.active)
	{
		printf("DEBUG: Reverting temp settings changes\n");
		cfg_temp_settings_revert();
		return 1;
	}
	
	// Fallback to old single-setting confirmation
	if (!setting_backup.setting_ptr) {
		printf("DEBUG: No setting to revert\n");
		return 1;
	}
	
	const ini_var_t* setting = setting_backup.setting_ptr;
	printf("DEBUG: Reverting setting %s\n", setting->name);
	
	// Restore the old value based on type
	switch (setting->type)
	{
		case INI_UINT8:
			*((uint8_t*)setting->var) = setting_backup.old_uint8;
			break;
		case INI_UINT16:
			*((uint16_t*)setting->var) = setting_backup.old_uint16;
			break;
		case INI_UINT32:
			*((uint32_t*)setting->var) = setting_backup.old_uint32;
			break;
		case INI_INT8:
			*((int8_t*)setting->var) = setting_backup.old_int8;
			break;
		case INI_INT16:
			*((int16_t*)setting->var) = setting_backup.old_int16;
			break;
		case INI_INT32:
			*((int32_t*)setting->var) = setting_backup.old_int32;
			break;
		case INI_FLOAT:
			*((float*)setting->var) = setting_backup.old_float;
			break;
		case INI_STRING:
			strncpy((char*)setting->var, setting_backup.old_string, setting->max);
			break;
		default:
			printf("DEBUG: Unknown setting type for revert\n");
			break;
	}
	
	// Handle special sync cases
	if (strcmp(setting->name, "SYNC_MODE") == 0) {
		cfg_sync_mode_to_individual();
	} else if (strcmp(setting->name, "COMPOSITE_SYNC") == 0 || strcmp(setting->name, "VGA_SOG") == 0) {
		cfg_sync_individual_to_mode();
	}
	
	user_io_send_buttons(1);
	return 1;
}

// Setup confirmation screen for a setting change
void cfg_setup_setting_confirmation(const ini_var_t* setting, const char* old_value, const char* new_value)
{
	if (!setting) return;
	
	// Store backup information
	strncpy(setting_backup.setting_name, setting->name, sizeof(setting_backup.setting_name) - 1);
	strncpy(setting_backup.old_value_str, old_value, sizeof(setting_backup.old_value_str) - 1);
	strncpy(setting_backup.new_value_str, new_value, sizeof(setting_backup.new_value_str) - 1);
	setting_backup.setting_ptr = setting;
	
	// Store the old value based on type
	switch (setting->type)
	{
		case INI_UINT8:
			setting_backup.old_uint8 = *((uint8_t*)setting->var);
			break;
		case INI_UINT16:
			setting_backup.old_uint16 = *((uint16_t*)setting->var);
			break;
		case INI_UINT32:
			setting_backup.old_uint32 = *((uint32_t*)setting->var);
			break;
		case INI_INT8:
			setting_backup.old_int8 = *((int8_t*)setting->var);
			break;
		case INI_INT16:
			setting_backup.old_int16 = *((int16_t*)setting->var);
			break;
		case INI_INT32:
			setting_backup.old_int32 = *((int32_t*)setting->var);
			break;
		case INI_FLOAT:
			setting_backup.old_float = *((float*)setting->var);
			break;
		case INI_STRING:
			strncpy(setting_backup.old_string, (char*)setting->var, sizeof(setting_backup.old_string) - 1);
			break;
	}
}

// Generate dynamic category selection menu
int cfg_generate_category_selection_menu(int menu_offset, int* menusub, const char* title, menu_flags_t menu_type)
{
	int m = menu_offset;
	
	// Set title
	if (title) {
		OsdSetTitle(title, 0);
	}
	
	OsdWrite(m++, "");
	OsdWrite(m++, " Select Category:");
	OsdWrite(m++, "");
	
	// Create mapping of display index to actual category index
	static int category_mapping[CAT_COUNT];
	int display_categories = 0;
	
	// Generate category menu items, skipping empty categories
	for (int i = 0; i < CAT_COUNT; i++)
	{
		// Count enabled settings in this category
		int enabled_count = cfg_count_enabled_settings_in_category((osd_category_t)i, menu_type);
		
		// Skip empty categories
		if (enabled_count == 0) {
			continue;
		}
		
		// Store the mapping from display index to actual category
		category_mapping[display_categories] = i;
		
		char category_line[32];
		// Format as exactly 28 characters: space + name + padding + \x16
		snprintf(category_line, sizeof(category_line), " %s", category_info[i].name);
		
		// Pad with spaces to position 27, then add \x16 at position 27
		int len = strlen(category_line);
		while (len < 27) category_line[len++] = ' ';
		category_line[27] = '\x16';
		category_line[28] = '\0';
		
		bool selected = (menusub && *menusub == display_categories);
		OsdWrite(m++, category_line, selected);
		display_categories++;
	}
	
	// Update menusub to point to the display index instead of category index
	// This requires the caller to map it back using cfg_get_category_from_display_index
	
	return display_categories; // Return number of non-empty categories
}

// Get the actual category index from display index (accounting for hidden empty categories)
osd_category_t cfg_get_category_from_display_index(int display_index, menu_flags_t menu_type)
{
	int display_categories = 0;
	
	for (int i = 0; i < CAT_COUNT; i++)
	{
		// Count enabled settings in this category
		int enabled_count = cfg_count_enabled_settings_in_category((osd_category_t)i, menu_type);
		
		// Skip empty categories
		if (enabled_count == 0) {
			continue;
		}
		
		if (display_categories == display_index) {
			return (osd_category_t)i;
		}
		display_categories++;
	}
	
	return (osd_category_t)0; // Fallback to first category
}

// Get display index from actual category enum (reverse of cfg_get_category_from_display_index)
int cfg_get_display_index_from_category(osd_category_t category, menu_flags_t menu_type)
{
	int display_index = 0;
	for (int i = 0; i < CAT_COUNT; i++)
	{
		// Count enabled settings in this category
		int enabled_count = cfg_count_enabled_settings_in_category((osd_category_t)i, menu_type);
		
		// Skip empty categories
		if (enabled_count == 0) {
			continue;
		}
		
		if ((osd_category_t)i == category) {
			return display_index;
		}
		display_index++;
	}
	
	return 0; // Fallback to first display index
}

// Get setting at menu position within a category (skipping disabled settings)
const ini_var_t* cfg_get_category_setting_at_index(osd_category_t category, int index, menu_flags_t menu_type)
{
	const ini_var_t** settings = NULL;
	int count = 0;
	
	cfg_get_settings_for_category(category, &settings, &count, menu_type);
	
	int enabled_index = 0;
	for (int i = 0; i < count; i++)
	{
		// Skip hidden settings (negative menu_position)
		if (settings[i]->menu_position < 0) {
			continue;
		}
		
		// Skip stippled settings (disabled but visible)
		if (is_setting_stippled(settings[i]->name)) {
			continue;
		}
		
		if (!cfg_is_setting_enabled(settings[i]->name)) {
			continue; // Skip disabled settings
		}
		
		if (enabled_index == index) {
			return settings[i];
		}
		enabled_index++;
	}
	
	return NULL; // Index out of range
}

// Count enabled settings in a category
int cfg_count_enabled_settings_in_category(osd_category_t category, menu_flags_t menu_type)
{
	const ini_var_t** settings = NULL;
	int count = 0;
	
	cfg_get_settings_for_category(category, &settings, &count, menu_type);
	
	int enabled_count = 0;
	for (int i = 0; i < count; i++)
	{
		// Skip hidden settings (negative menu_position)
		if (settings[i]->menu_position < 0) {
			continue;
		}
		
		// Skip stippled settings (disabled but visible)
		if (is_setting_stippled(settings[i]->name)) {
			continue;
		}
		
		if (cfg_is_setting_enabled(settings[i]->name)) {
			enabled_count++;
		}
	}
	
	return enabled_count;
}

// File picker support functions
void cfg_set_file_picker_setting(const ini_var_t* setting)
{
	cfg_file_picker_current_setting = setting;
}

void cfg_open_file_picker(const char* initial_dir, const char* file_ext)
{
	// This function signals the menu system to open file picker
	// The actual SelectFile call will be made from the menu handler
	printf("DEBUG: cfg_open_file_picker called with dir='%s', ext='%s'\n", initial_dir, file_ext);
	
	// Store the file picker request details
	strncpy(cfg_file_picker_initial_path, initial_dir, sizeof(cfg_file_picker_initial_path) - 1);
	cfg_file_picker_initial_path[sizeof(cfg_file_picker_initial_path) - 1] = '\0';
	
	// Set a flag that the menu system can check to trigger SelectFile
	// This will be handled by checking cfg_file_picker_current_setting != NULL
}

void cfg_file_picker_callback(const char* selected_path)
{
	if (!cfg_file_picker_current_setting || !selected_path) {
		printf("ERROR: Invalid file picker callback parameters\n");
		return;
	}
	
	printf("DEBUG: File picker callback with path: '%s'\n", selected_path);
	
	// Convert absolute path to relative path (remove /media/fat prefix)
	const char* relative_path = selected_path;
	if (strncmp(selected_path, "/media/fat/", 11) == 0) {
		relative_path = selected_path + 11; // Skip "/media/fat/"
	} else if (strncmp(selected_path, "/media/fat", 10) == 0) {
		relative_path = selected_path + 10; // Skip "/media/fat"
		if (*relative_path == '/') relative_path++; // Skip leading slash
	}
	
	// Update the setting value
	char* setting_value = (char*)cfg_file_picker_current_setting->var;
	strncpy(setting_value, relative_path, cfg_file_picker_current_setting->max - 1);
	setting_value[cfg_file_picker_current_setting->max - 1] = '\0';
	
	printf("DEBUG: Updated setting '%s' to '%s'\n", 
		   cfg_file_picker_current_setting->name, relative_path);
	
	// Do NOT save to INI file immediately - only save to memory
	// The user will save when they choose "Save All Settings"
	printf("DEBUG: File saved to memory only, not to INI file\n");
	
	// Clear file picker state
	cfg_file_picker_current_setting = NULL;
}
