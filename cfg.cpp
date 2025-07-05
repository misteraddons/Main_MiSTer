// cfg.c
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include "cfg.h"
#include "debug.h"
#include "file_io.h"
#include "user_io.h"
#include "video.h"
#include "support/arcade/mra_loader.h"

cfg_t cfg;
static FILE *orig_stdout = NULL;
static FILE *dev_null = NULL;

// Types now defined in cfg.h

// Category information
static const osd_category_info_t category_info[CAT_COUNT] = {
	{ "Video & Display", "\x8D", "Video output and display settings" },
	{ "Audio", "\x8D", "Audio output configuration" },
	{ "Input & Controllers", "\x82", "Keyboard, mouse, and controller settings" },
	{ "System & Boot", "\x80", "System startup and core settings" },
	{ "Network & Storage", "\x1C", "Network and storage options" },
	{ "Advanced", "\x81", "Advanced settings and developer options" }
};

const ini_var_t ini_vars[] =
{
	{ "YPBPR", (void*)(&(cfg.vga_mode_int)), INI_UINT8, 0, 1, "YPbPr Output", "Enable component video output (legacy)", CAT_VIDEO_DISPLAY, NULL, true },
	{ "COMPOSITE_SYNC", (void*)(&(cfg.csync)), INI_UINT8, 0, 1, "Composite Sync", "Enable composite sync on HSync or separate sync on Hsync and Vsync. Composite sync is best for most everything except PC CRTs.", CAT_VIDEO_DISPLAY, NULL, true },
	{ "FORCED_SCANDOUBLER", (void*)(&(cfg.forced_scandoubler)), INI_UINT8, 0, 1, "Force Scandoubler", "Scandouble 15kHz cores to 31kHz. Some cores don't have the scandoubler module (PSX, N64, etc.)", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VGA_SCALER", (void*)(&(cfg.vga_scaler)), INI_UINT8, 0, 1, "VGA Scaler", "Use scaler for VGA/DVI output", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VGA_SOG", (void*)(&(cfg.vga_sog)), INI_UINT8, 0, 1, "VGA Sync-on-Green", "Enable sync-on-green for VGA and YPbPr", CAT_VIDEO_DISPLAY, NULL, true },
	{ "KEYRAH_MODE", (void*)(&(cfg.keyrah_mode)), INI_HEX32, 0, 0xFFFFFFFF, "Keyrah Mode", "Keyrah interface mode", CAT_ADVANCED, NULL, true },
	{ "RESET_COMBO", (void*)(&(cfg.reset_combo)), INI_UINT8, 0, 3, "Reset Key Combo", "Keyboard combination for reset", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "KEY_MENU_AS_RGUI", (void*)(&(cfg.key_menu_as_rgui)), INI_UINT8, 0, 1, "Menu Key as Right GUI", "Use Menu key as Right GUI", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "VIDEO_MODE", (void*)(cfg.video_conf), INI_STRING, 0, sizeof(cfg.video_conf) - 1, "Video Mode", "Auto mode uses HDMI EDID to set optimal resolution. All other settings override the EDID value.", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VIDEO_MODE_PAL", (void*)(cfg.video_conf_pal), INI_STRING, 0, sizeof(cfg.video_conf_pal) - 1, "Video Mode (PAL)", "Video mode for PAL cores", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VIDEO_MODE_NTSC", (void*)(cfg.video_conf_ntsc), INI_STRING, 0, sizeof(cfg.video_conf_ntsc) - 1, "Video Mode (NTSC)", "Video mode for NTSC cores", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VIDEO_INFO", (void*)(&(cfg.video_info)), INI_UINT8, 0, 10, "Video Info Display", "Show video information on screen", CAT_VIDEO_DISPLAY, "sec", false },
	{ "VSYNC_ADJUST", (void*)(&(cfg.vsync_adjust)), INI_UINT8, 0, 2, "VSync Adjustment", "Automatic refresh rate adjustment. `3 buffer 60Hz` = robust sync with the most latency. `3 buffer match` = robust sync, matching the core's sync. `1 buffer match` = lowest latency but may not work with all cores on all displays.", CAT_VIDEO_DISPLAY, NULL, false },
	{ "HDMI_AUDIO_96K", (void*)(&(cfg.hdmi_audio_96k)), INI_UINT8, 0, 1, "HDMI 96kHz Audio", "Enable 96kHz audio output. May cause compatibility issues with AV equipment and DACs.", CAT_AUDIO, NULL, true },
	{ "DVI_MODE", (void*)(&(cfg.dvi_mode)), INI_UINT8, 0, 1, "DVI Mode", "Disable HDMI features for DVI displays", CAT_VIDEO_DISPLAY, NULL, true },
	{ "HDMI_LIMITED", (void*)(&(cfg.hdmi_limited)), INI_UINT8, 0, 2, "HDMI Color Range", "HDMI color range. Set full for most devices. Limited (16-235) for older displays. Limited (16-255) for some HDMI DACs.", CAT_VIDEO_DISPLAY, NULL, true },
	{ "KBD_NOMOUSE", (void*)(&(cfg.kbd_nomouse)), INI_UINT8, 0, 1, "Disable Mouse", "Disable mouse emulation via keyboard", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "MOUSE_THROTTLE", (void*)(&(cfg.mouse_throttle)), INI_UINT8, 1, 100, "Mouse Throttle", "Mouse movement speed", CAT_INPUT_CONTROLLERS, "%", false },
	{ "BOOTSCREEN", (void*)(&(cfg.bootscreen)), INI_UINT8, 0, 1, "Boot Screen", "Show boot screen on startup", CAT_SYSTEM_BOOT, NULL, false },
	{ "VSCALE_MODE", (void*)(&(cfg.vscale_mode)), INI_UINT8, 0, 5, "Vertical Scale Mode", "Vertical scaling algorithm", CAT_VIDEO_DISPLAY, NULL, false },
	{ "VSCALE_BORDER", (void*)(&(cfg.vscale_border)), INI_UINT16, 0, 399, "Vertical Scale Border", "Border size for scaled image", CAT_VIDEO_DISPLAY, "px", false },
	{ "RBF_HIDE_DATECODE", (void*)(&(cfg.rbf_hide_datecode)), INI_UINT8, 0, 1, "Hide Core Dates", "Hide date codes in core names", CAT_SYSTEM_BOOT, NULL, false },
	{ "MENU_PAL", (void*)(&(cfg.menu_pal)), INI_UINT8, 0, 1, "Menu PAL Mode", "Use PAL mode for menu core", CAT_SYSTEM_BOOT, NULL, true },
	{ "BOOTCORE", (void*)(&(cfg.bootcore)), INI_STRING, 0, sizeof(cfg.bootcore) - 1, "Boot Core", "Core to load on startup", CAT_SYSTEM_BOOT, NULL, false },
	{ "BOOTCORE_TIMEOUT", (void*)(&(cfg.bootcore_timeout)), INI_INT16, 2, 30, "Boot Core Timeout", "Timeout before loading boot core", CAT_SYSTEM_BOOT, "sec", false },
	{ "FONT", (void*)(&(cfg.font)), INI_STRING, 0, sizeof(cfg.font) - 1, "Custom Font", "Custom font file path", CAT_SYSTEM_BOOT, NULL, true },
	{ "FB_SIZE", (void*)(&(cfg.fb_size)), INI_UINT8, 0, 4, "Framebuffer Size", "Linux framebuffer size", CAT_SYSTEM_BOOT, NULL, true },
	{ "FB_TERMINAL", (void*)(&(cfg.fb_terminal)), INI_UINT8, 0, 1, "Framebuffer Terminal", "Enable Linux terminal on HDMI and scaled analog video.", CAT_SYSTEM_BOOT, NULL, true },
	{ "OSD_TIMEOUT", (void*)(&(cfg.osd_timeout)), INI_INT16, 0, 3600, "OSD Timeout", "Hide OSD after inactivity.", CAT_SYSTEM_BOOT, "sec", false },
	{ "DIRECT_VIDEO", (void*)(&(cfg.direct_video)), INI_UINT8, 0, 1, "Direct Video", "Bypass scaler for compatible displays and HDMI DACs.", CAT_VIDEO_DISPLAY, NULL, true },
	{ "OSD_ROTATE", (void*)(&(cfg.osd_rotate)), INI_UINT8, 0, 2, "OSD Rotation", "Off (Yoko), 1=90° Clockwise (Tate), 2=90° Counter-Clockwise (Tate)", CAT_SYSTEM_BOOT, NULL, false },
	{ "DEADZONE", (void*)(&(cfg.controller_deadzone)), INI_STRINGARR, sizeof(cfg.controller_deadzone) / sizeof(*cfg.controller_deadzone), sizeof(*cfg.controller_deadzone), "Controller Deadzone", "Analog stick deadzone configuration", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "GAMEPAD_DEFAULTS", (void*)(&(cfg.gamepad_defaults)), INI_UINT8, 0, 1, "Gamepad Defaults", "'Name' means Xbox 'A' button is mapped to SNES 'A' button. 'Positional' means Xbox 'A' button is mapped to SNES 'B' button.", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "RECENTS", (void*)(&(cfg.recents)), INI_UINT8, 0, 1, "Recent Files", "Track recently used files", CAT_SYSTEM_BOOT, NULL, false },
	{ "CONTROLLER_INFO", (void*)(&(cfg.controller_info)), INI_UINT8, 0, 10, "Controller Info", "Display controller information when a new core is loaded.", CAT_INPUT_CONTROLLERS, "sec", false },
	{ "REFRESH_MIN", (void*)(&(cfg.refresh_min)), INI_FLOAT, 0, 150, "Minimum Refresh Rate", "Minimum allowed refresh rate", CAT_VIDEO_DISPLAY, "Hz", false },
	{ "REFRESH_MAX", (void*)(&(cfg.refresh_max)), INI_FLOAT, 0, 150, "Maximum Refresh Rate", "Maximum allowed refresh rate", CAT_VIDEO_DISPLAY, "Hz", false },
	{ "JAMMA_VID", (void*)(&(cfg.jamma_vid)), INI_HEX16, 0, 0xFFFF, "JAMMA VID", "JAMMA interface vendor ID", CAT_ADVANCED, NULL, false },
	{ "JAMMA_PID", (void*)(&(cfg.jamma_pid)), INI_HEX16, 0, 0xFFFF, "JAMMA PID", "JAMMA interface product ID", CAT_ADVANCED, NULL, false },
	{ "JAMMA2_VID", (void*)(&(cfg.jamma2_vid)), INI_HEX16, 0, 0xFFFF, "JAMMA2 VID", "Second JAMMA interface vendor ID", CAT_ADVANCED, NULL, false },
	{ "JAMMA2_PID", (void*)(&(cfg.jamma2_pid)), INI_HEX16, 0, 0xFFFF, "JAMMA2 PID", "Second JAMMA interface product ID", CAT_ADVANCED, NULL, false },
	{ "SNIPER_MODE", (void*)(&(cfg.sniper_mode)), INI_UINT8, 0, 1, "Sniper Mode", "Enable precision aiming mode", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "BROWSE_EXPAND", (void*)(&(cfg.browse_expand)), INI_UINT8, 0, 1, "Browse Expand", "Expand file browser by default", CAT_SYSTEM_BOOT, NULL, false },
	{ "LOGO", (void*)(&(cfg.logo)), INI_UINT8, 0, 1, "Show Logo", "Display MiSTer logo on startup", CAT_SYSTEM_BOOT, NULL, false },
	{ "SHARED_FOLDER", (void*)(&(cfg.shared_folder)), INI_STRING, 0, sizeof(cfg.shared_folder) - 1, "Shared Folder", "Network shared folder path", CAT_NETWORK_STORAGE, NULL, false },
	{ "NO_MERGE_VID", (void*)(&(cfg.no_merge_vid)), INI_HEX16, 0, 0xFFFF, "No Merge VID", "USB device vendor ID to prevent merging", CAT_ADVANCED, NULL, false },
	{ "NO_MERGE_PID", (void*)(&(cfg.no_merge_pid)), INI_HEX16, 0, 0xFFFF, "No Merge PID", "USB device product ID to prevent merging", CAT_ADVANCED, NULL, false },
	{ "NO_MERGE_VIDPID", (void*)(cfg.no_merge_vidpid), INI_HEX32ARR, 0, 0xFFFFFFFF, "No Merge VID:PID", "USB VID:PID pairs to prevent merging", CAT_ADVANCED, NULL, false },
	{ "CUSTOM_ASPECT_RATIO_1", (void*)(&(cfg.custom_aspect_ratio[0])), INI_STRING, 0, sizeof(cfg.custom_aspect_ratio[0]) - 1, "Custom Aspect Ratio 1", "First custom aspect ratio", CAT_VIDEO_DISPLAY, NULL, false },
	{ "CUSTOM_ASPECT_RATIO_2", (void*)(&(cfg.custom_aspect_ratio[1])), INI_STRING, 0, sizeof(cfg.custom_aspect_ratio[1]) - 1, "Custom Aspect Ratio 2", "Second custom aspect ratio", CAT_VIDEO_DISPLAY, NULL, false },
	{ "SPINNER_VID", (void*)(&(cfg.spinner_vid)), INI_HEX16, 0, 0xFFFF, "Spinner VID", "Spinner device vendor ID", CAT_ADVANCED, NULL, false },
	{ "SPINNER_PID", (void*)(&(cfg.spinner_pid)), INI_HEX16, 0, 0xFFFF, "Spinner PID", "Spinner device product ID", CAT_ADVANCED, NULL, false },
	{ "SPINNER_AXIS", (void*)(&(cfg.spinner_axis)), INI_UINT8, 0, 2, "Spinner Axis", "Spinner axis configuration", CAT_ADVANCED, NULL, false },
	{ "SPINNER_THROTTLE", (void*)(&(cfg.spinner_throttle)), INI_INT32, -10000, 10000, "Spinner Throttle", "Spinner sensitivity adjustment", CAT_ADVANCED, NULL, false },
	{ "AFILTER_DEFAULT", (void*)(&(cfg.afilter_default)), INI_STRING, 0, sizeof(cfg.afilter_default) - 1, "Default Audio Filter", "Default audio filter file", CAT_AUDIO, NULL, false },
	{ "VFILTER_DEFAULT", (void*)(&(cfg.vfilter_default)), INI_STRING, 0, sizeof(cfg.vfilter_default) - 1, "Default Video Filter", "Default video filter file", CAT_ADVANCED, NULL, false },
	{ "VFILTER_VERTICAL_DEFAULT", (void*)(&(cfg.vfilter_vertical_default)), INI_STRING, 0, sizeof(cfg.vfilter_vertical_default) - 1, "Default Vertical Filter", "Default vertical filter file", CAT_ADVANCED, NULL, false },
	{ "VFILTER_SCANLINES_DEFAULT", (void*)(&(cfg.vfilter_scanlines_default)), INI_STRING, 0, sizeof(cfg.vfilter_scanlines_default) - 1, "Default Scanlines Filter", "Default scanlines filter file", CAT_ADVANCED, NULL, false },
	{ "SHMASK_DEFAULT", (void*)(&(cfg.shmask_default)), INI_STRING, 0, sizeof(cfg.shmask_default) - 1, "Default Shadow Mask", "Default shadow mask file", CAT_ADVANCED, NULL, false },
	{ "SHMASK_MODE_DEFAULT", (void*)(&(cfg.shmask_mode_default)), INI_UINT8, 0, 255, "Default Shadow Mask Mode", "Default shadow mask mode", CAT_ADVANCED, NULL, false },
	{ "PRESET_DEFAULT", (void*)(&(cfg.preset_default)), INI_STRING, 0, sizeof(cfg.preset_default) - 1, "Default Preset", "Default video preset file", CAT_ADVANCED, NULL, false },
	{ "LOG_FILE_ENTRY", (void*)(&(cfg.log_file_entry)), INI_UINT8, 0, 1, "Log File Entry", "Enable file access logging", CAT_ADVANCED, NULL, false },
	{ "BT_AUTO_DISCONNECT", (void*)(&(cfg.bt_auto_disconnect)), INI_UINT32, 0, 180, "BT Auto Disconnect", "Bluetooth auto-disconnect timeout", CAT_ADVANCED, "min", false },
	{ "BT_RESET_BEFORE_PAIR", (void*)(&(cfg.bt_reset_before_pair)), INI_UINT8, 0, 1, "BT Reset Before Pair", "Reset Bluetooth before pairing", CAT_ADVANCED, NULL, false },
	{ "WAITMOUNT", (void*)(&(cfg.waitmount)), INI_STRING, 0, sizeof(cfg.waitmount) - 1, "Wait for Mount", "Devices to wait for before continuing", CAT_NETWORK_STORAGE, NULL, false },
	{ "RUMBLE", (void *)(&(cfg.rumble)), INI_UINT8, 0, 1, "Controller Rumble", "Enable force feedback/rumble", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "WHEEL_FORCE", (void*)(&(cfg.wheel_force)), INI_UINT8, 0, 100, "Wheel Force Feedback", "Force feedback strength", CAT_INPUT_CONTROLLERS, "%", false },
	{ "WHEEL_RANGE", (void*)(&(cfg.wheel_range)), INI_UINT16, 0, 1000, "Wheel Range", "Steering wheel rotation range", CAT_INPUT_CONTROLLERS, "°", false },
	{ "HDMI_GAME_MODE", (void *)(&(cfg.hdmi_game_mode)), INI_UINT8, 0, 1, "HDMI Game Mode", "Enable low-latency game mode", CAT_VIDEO_DISPLAY, NULL, false },
	{ "VRR_MODE", (void *)(&(cfg.vrr_mode)), INI_UINT8, 0, 3, "Variable Refresh Rate", "VRR mode selection", CAT_VIDEO_DISPLAY, NULL, false },
	{ "VRR_MIN_FRAMERATE", (void *)(&(cfg.vrr_min_framerate)), INI_UINT8, 0, 255, "VRR Min Framerate", "Minimum VRR framerate", CAT_VIDEO_DISPLAY, "Hz", false },
	{ "VRR_MAX_FRAMERATE", (void *)(&(cfg.vrr_max_framerate)), INI_UINT8, 0, 255, "VRR Max Framerate", "Maximum VRR framerate", CAT_VIDEO_DISPLAY, "Hz", false },
	{ "VRR_VESA_FRAMERATE", (void *)(&(cfg.vrr_vesa_framerate)), INI_UINT8, 0, 255, "VRR VESA Framerate", "VESA VRR framerate", CAT_VIDEO_DISPLAY, "Hz", false },
	{ "VIDEO_OFF", (void*)(&(cfg.video_off)), INI_INT16, 0, 3600, "Video Off Timeout", "Turn off video after inactivity", CAT_VIDEO_DISPLAY, "sec", false },
	{ "PLAYER_1_CONTROLLER", (void*)(&(cfg.player_controller[0])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 1 Controller", "Controller mapping for player 1", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "PLAYER_2_CONTROLLER", (void*)(&(cfg.player_controller[1])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 2 Controller", "Controller mapping for player 2", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "PLAYER_3_CONTROLLER", (void*)(&(cfg.player_controller[2])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 3 Controller", "Controller mapping for player 3", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "PLAYER_4_CONTROLLER", (void*)(&(cfg.player_controller[3])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 4 Controller", "Controller mapping for player 4", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "PLAYER_5_CONTROLLER", (void*)(&(cfg.player_controller[4])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 5 Controller", "Controller mapping for player 5", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "PLAYER_6_CONTROLLER", (void*)(&(cfg.player_controller[5])), INI_STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]), "Player 6 Controller", "Controller mapping for player 6", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "DISABLE_AUTOFIRE", (void *)(&(cfg.disable_autofire)), INI_UINT8, 0, 1, "Disable Autofire", "Disable autofire functionality", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "VIDEO_BRIGHTNESS", (void *)(&(cfg.video_brightness)), INI_UINT8, 0, 100, "Video Brightness", "Adjust video brightness", CAT_VIDEO_DISPLAY, "%", false },
	{ "VIDEO_CONTRAST", (void *)(&(cfg.video_contrast)), INI_UINT8, 0, 100, "Video Contrast", "Adjust video contrast", CAT_VIDEO_DISPLAY, "%", false },
	{ "VIDEO_SATURATION", (void *)(&(cfg.video_saturation)), INI_UINT8, 0, 100, "Video Saturation", "Adjust video saturation", CAT_VIDEO_DISPLAY, "%", false },
	{ "VIDEO_HUE", (void *)(&(cfg.video_hue)), INI_UINT16, 0, 360, "Video Hue", "Adjust video hue", CAT_VIDEO_DISPLAY, "°", false },
	{ "VIDEO_GAIN_OFFSET", (void *)(&(cfg.video_gain_offset)), INI_STRING, 0, sizeof(cfg.video_gain_offset), "Video Gain/Offset", "RGB gain and offset adjustments", CAT_VIDEO_DISPLAY, NULL, false },
	{ "HDR", (void*)(&cfg.hdr), INI_UINT8, 0, 2, "HDR Mode", "High Dynamic Range mode", CAT_VIDEO_DISPLAY, NULL, false },
	{ "HDR_MAX_NITS", (void*)(&(cfg.hdr_max_nits)), INI_UINT16, 100, 10000, "HDR Max Brightness", "Maximum HDR brightness", CAT_VIDEO_DISPLAY, "nits", false },
	{ "HDR_AVG_NITS", (void*)(&(cfg.hdr_avg_nits)), INI_UINT16, 100, 10000, "HDR Average Brightness", "Average HDR brightness", CAT_VIDEO_DISPLAY, "nits", false },
	{ "VGA_MODE", (void*)(&(cfg.vga_mode)), INI_STRING, 0, sizeof(cfg.vga_mode) - 1, "VGA Mode", "Analog video output mode.", CAT_VIDEO_DISPLAY, NULL, true },
	{ "NTSC_MODE", (void *)(&(cfg.ntsc_mode)), INI_UINT8, 0, 2, "NTSC Mode", "NTSC color encoding mode", CAT_VIDEO_DISPLAY, NULL, false },
	{ "CONTROLLER_UNIQUE_MAPPING", (void *)(cfg.controller_unique_mapping), INI_UINT32ARR, 0, 0xFFFFFFFF, "Unique Controller Mapping", "Controller-specific button mappings", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "OSD_LOCK", (void*)(&(cfg.osd_lock)), INI_STRING, 0, sizeof(cfg.osd_lock) - 1, "OSD Lock", "Lock OSD with password", CAT_SYSTEM_BOOT, NULL, false },
	{ "OSD_LOCK_TIME", (void*)(&(cfg.osd_lock_time)), INI_UINT16, 0, 60, "OSD Lock Time", "Time before OSD locks", CAT_SYSTEM_BOOT, "sec", false },
	{ "DEBUG", (void *)(&(cfg.debug)), INI_UINT8, 0, 1, "Debug Mode", "Enable debug output", CAT_ADVANCED, NULL, false },
	{ "MAIN", (void*)(&(cfg.main)), INI_STRING, 0, sizeof(cfg.main) - 1, "Main Directory", "Main MiSTer directory name", CAT_SYSTEM_BOOT, NULL, false },
	{ "VFILTER_INTERLACE_DEFAULT", (void*)(&(cfg.vfilter_interlace_default)), INI_STRING, 0, sizeof(cfg.vfilter_interlace_default) - 1, "Default Interlace Filter", "Default interlace filter file", CAT_ADVANCED, NULL, false },
};

const int nvars = (int)(sizeof(ini_vars) / sizeof(ini_var_t));

// Helper functions for OSD integration
const osd_category_info_t* cfg_get_category_info(osd_category_t category)
{
	if (category >= 0 && category < CAT_COUNT)
		return &category_info[category];
	return NULL;
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
		}
		if (!strcasecmp(cfg.vga_mode, "ypbpr")) {
			cfg.vga_mode_int = 1;
			printf("DEBUG: Set vga_mode_int=1 (YPbPr)\n");
		}
		if (!strcasecmp(cfg.vga_mode, "svideo")) {
			cfg.vga_mode_int = 2;
			printf("DEBUG: Set vga_mode_int=2 (S-Video)\n");
		}
		if (!strcasecmp(cfg.vga_mode, "cvbs")) {
			cfg.vga_mode_int = 3;
			printf("DEBUG: Set vga_mode_int=3 (CVBS)\n");
		}
	}
	else
	{
		printf("DEBUG: No vga_mode string set, using vga_mode_int=%d\n", cfg.vga_mode_int);
	}
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
	else if (!strcasecmp(var->name, "DVI_MODE")) default_str = "2";
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
			case INI_HEX8:
			case INI_HEX16:
			case INI_HEX32:
			case INI_FLOAT:
				default_str = "0";
				break;
			case INI_STRING:
				default_str = ""; // Empty string
				break;
			default:
				default_str = "";
				break;
		}
	}
	
	return strcmp(current_value, default_str) != 0;
}

// Check if current memory value differs from file value
static int value_needs_update(const ini_var_t *var, const char* file_value)
{
	char current_value[512];
	format_ini_value(current_value, sizeof(current_value), var);
	
	// If we have no file value and current value is empty, no update needed
	if (!file_value && strlen(current_value) == 0)
		return 0;
	
	// If we have no file value but current value exists, update needed
	if (!file_value && strlen(current_value) > 0)
		return 1;
	
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
	for (int i = 0; i < nvars; i++)
	{
		const ini_var_t *var = &ini_vars[i];
		
		// Skip array types for now
		if (var->type == INI_UINT32ARR || var->type == INI_HEX32ARR || var->type == INI_STRINGARR)
			continue;
		
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
		if (!fp) continue;
		
		char line[1024];
		char *file_value = NULL;
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
				if (extracted)
				{
					file_value = extracted;
					break;
				}
			}
		}
		fclose(fp);
		
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
				continue;
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
					"sed -i '/^\\[MiSTer\\]/a\\;%s=1' \"%s\"",
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
					"sed -i.sed '/^\\[MiSTer\\]/a\\%s=%s' \"%s\"",
					lowercase_name, escaped_value, filepath);
			}
		}
		
		
		if (system(cmd) != 0)
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
	
	// Define whitelist of settings that can be saved via core settings menu
	// Only include settings that are actually accessible through the core settings interface
	const char* core_settings_whitelist[] = {
		// Basic Video settings
		"direct_video", "video_conf", "vsync_adjust", "vscale_mode", "hdmi_limited", 
		"vga_mode", "vga_scaler", "forced_scandoubler", "csync", "vga_sog", "ntsc_mode",
		"hdmi_audio_96k",
		// Advanced Video settings  
		"video_brightness", "video_contrast", "video_saturation", "video_hue",
		"video_gamma", "hdr", "dv_mode", "vrr_mode", "vrr_min_framerate", "vrr_max_framerate",
		"vrr_vesa_framerate", "hdmi_game_mode", "custom_aspect_ratio_1", "custom_aspect_ratio_2",
		// Input & Controls settings
		"controller_info", "wheel_force", "wheel_range", "rumble", "gun_mode", "mouse_throttle",
		"key_menu_as_rgui", "reset_combo", "fb_size", "fb_terminal", 
		// System & Storage settings
		"bootscreen", "recents", "osd_timeout", "direct_video", "dvi_mode",
		// Legacy compatibility
		"ypbpr",
		NULL  // End marker
	};
	
	// Process each variable that might need core-specific saving
	for (int i = 0; i < nvars; i++)
	{
		const ini_var_t *var = &ini_vars[i];
		
		// Skip array types for now
		if (var->type == INI_UINT32ARR || var->type == INI_HEX32ARR || var->type == INI_STRINGARR)
			continue;
		
		// Check if this variable is in our whitelist
		int is_whitelisted = 0;
		for (int w = 0; core_settings_whitelist[w] != NULL; w++)
		{
			if (strcasecmp(var->name, core_settings_whitelist[w]) == 0)
			{
				is_whitelisted = 1;
				break;
			}
		}
		
		// Skip variables not in whitelist
		if (!is_whitelisted)
		{
			printf("DEBUG: Skipping non-whitelisted variable: %s\n", var->name);
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
	
	// Clean up non-whitelisted variables from the core section
	printf("DEBUG: Cleaning up non-whitelisted variables from [%s] section\n", section_name);
	for (int i = 0; i < nvars; i++)
	{
		const ini_var_t *var = &ini_vars[i];
		
		// Skip array types
		if (var->type == INI_UINT32ARR || var->type == INI_HEX32ARR || var->type == INI_STRINGARR)
			continue;
		
		// Check if this variable is in our whitelist
		int is_whitelisted = 0;
		for (int w = 0; core_settings_whitelist[w] != NULL; w++)
		{
			if (strcasecmp(var->name, core_settings_whitelist[w]) == 0)
			{
				is_whitelisted = 1;
				break;
			}
		}
		
		// If not whitelisted, remove it from the core section
		if (!is_whitelisted)
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
