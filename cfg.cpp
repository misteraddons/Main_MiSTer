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
	{ "COMPOSITE_SYNC", (void*)(&(cfg.csync)), INI_UINT8, 0, 1, "Composite Sync", "Enable composite sync on HSync", CAT_VIDEO_DISPLAY, NULL, true },
	{ "FORCED_SCANDOUBLER", (void*)(&(cfg.forced_scandoubler)), INI_UINT8, 0, 1, "Force Scandoubler", "Force scandoubler for 15kHz cores", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VGA_SCALER", (void*)(&(cfg.vga_scaler)), INI_UINT8, 0, 1, "VGA Scaler", "Use scaler for VGA/DVI output", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VGA_SOG", (void*)(&(cfg.vga_sog)), INI_UINT8, 0, 1, "VGA Sync-on-Green", "Enable sync-on-green for VGA", CAT_VIDEO_DISPLAY, NULL, true },
	{ "KEYRAH_MODE", (void*)(&(cfg.keyrah_mode)), INI_HEX32, 0, 0xFFFFFFFF, "Keyrah Mode", "Keyrah interface mode", CAT_ADVANCED, NULL, true },
	{ "RESET_COMBO", (void*)(&(cfg.reset_combo)), INI_UINT8, 0, 3, "Reset Key Combo", "Keyboard combination for reset", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "KEY_MENU_AS_RGUI", (void*)(&(cfg.key_menu_as_rgui)), INI_UINT8, 0, 1, "Menu Key as Right GUI", "Use Menu key as Right GUI", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "VIDEO_MODE", (void*)(cfg.video_conf), INI_STRING, 0, sizeof(cfg.video_conf) - 1, "Video Mode", "Default video mode", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VIDEO_MODE_PAL", (void*)(cfg.video_conf_pal), INI_STRING, 0, sizeof(cfg.video_conf_pal) - 1, "Video Mode (PAL)", "Video mode for PAL cores", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VIDEO_MODE_NTSC", (void*)(cfg.video_conf_ntsc), INI_STRING, 0, sizeof(cfg.video_conf_ntsc) - 1, "Video Mode (NTSC)", "Video mode for NTSC cores", CAT_VIDEO_DISPLAY, NULL, true },
	{ "VIDEO_INFO", (void*)(&(cfg.video_info)), INI_UINT8, 0, 10, "Video Info Display", "Show video information on screen", CAT_VIDEO_DISPLAY, "sec", false },
	{ "VSYNC_ADJUST", (void*)(&(cfg.vsync_adjust)), INI_UINT8, 0, 2, "VSync Adjustment", "Automatic refresh rate adjustment", CAT_VIDEO_DISPLAY, NULL, false },
	{ "HDMI_AUDIO_96K", (void*)(&(cfg.hdmi_audio_96k)), INI_UINT8, 0, 1, "HDMI 96kHz Audio", "Enable 96kHz audio output", CAT_AUDIO, NULL, true },
	{ "DVI_MODE", (void*)(&(cfg.dvi_mode)), INI_UINT8, 0, 1, "DVI Mode", "Disable HDMI features for DVI displays", CAT_VIDEO_DISPLAY, NULL, true },
	{ "HDMI_LIMITED", (void*)(&(cfg.hdmi_limited)), INI_UINT8, 0, 2, "HDMI Color Range", "HDMI color range limitation", CAT_VIDEO_DISPLAY, NULL, true },
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
	{ "FB_TERMINAL", (void*)(&(cfg.fb_terminal)), INI_UINT8, 0, 1, "Framebuffer Terminal", "Enable Linux terminal on HDMI", CAT_SYSTEM_BOOT, NULL, true },
	{ "OSD_TIMEOUT", (void*)(&(cfg.osd_timeout)), INI_INT16, 0, 3600, "OSD Timeout", "Hide OSD after inactivity", CAT_SYSTEM_BOOT, "sec", false },
	{ "DIRECT_VIDEO", (void*)(&(cfg.direct_video)), INI_UINT8, 0, 1, "Direct Video", "Bypass scaler for compatible displays", CAT_VIDEO_DISPLAY, NULL, true },
	{ "OSD_ROTATE", (void*)(&(cfg.osd_rotate)), INI_UINT8, 0, 2, "OSD Rotation", "Rotate OSD display", CAT_SYSTEM_BOOT, NULL, false },
	{ "DEADZONE", (void*)(&(cfg.controller_deadzone)), INI_STRINGARR, sizeof(cfg.controller_deadzone) / sizeof(*cfg.controller_deadzone), sizeof(*cfg.controller_deadzone), "Controller Deadzone", "Analog stick deadzone configuration", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "GAMEPAD_DEFAULTS", (void*)(&(cfg.gamepad_defaults)), INI_UINT8, 0, 1, "Gamepad Defaults", "Use default gamepad mappings", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "RECENTS", (void*)(&(cfg.recents)), INI_UINT8, 0, 1, "Recent Files", "Track recently used files", CAT_SYSTEM_BOOT, NULL, false },
	{ "CONTROLLER_INFO", (void*)(&(cfg.controller_info)), INI_UINT8, 0, 10, "Controller Info", "Display controller information", CAT_INPUT_CONTROLLERS, "sec", false },
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
	{ "VGA_MODE", (void*)(&(cfg.vga_mode)), INI_STRING, 0, sizeof(cfg.vga_mode) - 1, "VGA Mode", "Analog video output mode", CAT_VIDEO_DISPLAY, NULL, true },
	{ "NTSC_MODE", (void *)(&(cfg.ntsc_mode)), INI_UINT8, 0, 2, "NTSC Mode", "NTSC color encoding mode", CAT_VIDEO_DISPLAY, NULL, false },
	{ "CONTROLLER_UNIQUE_MAPPING", (void *)(cfg.controller_unique_mapping), INI_UINT32ARR, 0, 0xFFFFFFFF, "Unique Controller Mapping", "Controller-specific button mappings", CAT_INPUT_CONTROLLERS, NULL, false },
	{ "OSD_LOCK", (void*)(&(cfg.osd_lock)), INI_STRING, 0, sizeof(cfg.osd_lock) - 1, "OSD Lock", "Lock OSD with password", CAT_SYSTEM_BOOT, NULL, false },
	{ "OSD_LOCK_TIME", (void*)(&(cfg.osd_lock_time)), INI_UINT16, 0, 60, "OSD Lock Time", "Time before OSD locks", CAT_SYSTEM_BOOT, "sec", false },
	{ "DEBUG", (void *)(&(cfg.debug)), INI_UINT8, 0, 1, "Debug Mode", "Enable debug output", CAT_ADVANCED, NULL, false },
	{ "MAIN", (void*)(&(cfg.main)), INI_STRING, 0, sizeof(cfg.main) - 1, "Main Directory", "Main MiSTer directory name", CAT_SYSTEM_BOOT, NULL, false },
	{"VFILTER_INTERLACE_DEFAULT", (void*)(&(cfg.vfilter_interlace_default)), INI_STRING, 0, sizeof(cfg.vfilter_interlace_default) - 1, "Default Interlace Filter", "Default interlace filter file", CAT_ADVANCED, NULL, false },
};

const int nvars = (int)(sizeof(ini_vars) / sizeof(ini_var_t));

// Helper functions for OSD integration
const osd_category_info_t* cfg_get_category_info(osd_category_t category)
{
	if (category >= 0 && category < CAT_COUNT)
		return &category_info[category];
	return NULL;
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

// Save configuration to INI file
int cfg_save(uint8_t alt)
{
	const char *ini_filename = cfg_get_name(alt);
	char filepath[1024];
	char temppath[1024];
	
	// Create file paths
	snprintf(filepath, sizeof(filepath), "%s/%s", getRootDir(), ini_filename);
	snprintf(temppath, sizeof(temppath), "%s.tmp", filepath);
	
	FILE *fp = fopen(temppath, "w");
	if (!fp)
	{
		printf("Failed to create temp INI file: %s\n", temppath);
		return 0;
	}
	
	// Write header
	fprintf(fp, "[MiSTer]\n");
	fprintf(fp, "; Configuration generated by OSD Settings Menu\n");
	fprintf(fp, "\n");
	
	// Write all settings
	char value_buffer[512];
	for (int i = 0; i < nvars; i++)
	{
		const ini_var_t *var = &ini_vars[i];
		
		// Skip array types for now (they need special handling)
		if (var->type == INI_UINT32ARR || var->type == INI_HEX32ARR || var->type == INI_STRINGARR)
			continue;
		
		format_ini_value(value_buffer, sizeof(value_buffer), var);
		
		// Only write non-empty values
		if (strlen(value_buffer) > 0)
		{
			fprintf(fp, "%s=%s\n", var->name, value_buffer);
		}
	}
	
	// Close and move temp file to final location
	fclose(fp);
	
	// Atomic replace: move temp file to final location
	if (rename(temppath, filepath) != 0)
	{
		printf("Failed to replace INI file: %s\n", filepath);
		remove(temppath);
		return 0;
	}
	
	printf("Configuration saved to: %s\n", filepath);
	return 1;
}
