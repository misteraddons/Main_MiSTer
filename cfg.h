// cfg.h
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#ifndef __CFG_H__
#define __CFG_H__

#include <inttypes.h>
#include <stddef.h>

//// type definitions ////
typedef struct {
	uint32_t keyrah_mode;
	uint8_t forced_scandoubler;
	uint8_t key_menu_as_rgui;
	uint8_t reset_combo;
	uint8_t csync;
	uint8_t vga_scaler;
	uint8_t vga_sog;
	uint8_t sync_mode;              // Virtual setting: 0=Separate, 1=Composite, 2=Sync-on-Green
	uint8_t hdmi_audio_96k;
	uint8_t dvi_mode;
	uint8_t hdmi_limited;
	uint8_t direct_video;
	uint8_t video_info;
	float refresh_min;
	float refresh_max;
	uint8_t controller_info;
	uint8_t vsync_adjust;
	uint8_t kbd_nomouse;
	uint8_t mouse_throttle;
	uint8_t bootscreen;
	uint8_t vscale_mode;
	uint16_t vscale_border;
	uint8_t rbf_hide_datecode;
	uint8_t menu_pal;
	int16_t bootcore_timeout;
	uint8_t fb_size;
	uint8_t fb_terminal;
	uint8_t osd_rotate;
	uint16_t osd_timeout;
	uint8_t gamepad_defaults;
	uint8_t recents;
	uint16_t jamma_vid;
	uint16_t jamma_pid;
	uint16_t jamma2_vid;
	uint16_t jamma2_pid;
	uint16_t no_merge_vid;
	uint16_t no_merge_pid;
	uint32_t no_merge_vidpid[256];
	uint16_t spinner_vid;
	uint16_t spinner_pid;
	int spinner_throttle;
	uint8_t spinner_axis;
	uint8_t sniper_mode;
	uint8_t browse_expand;
	uint8_t logo;
	uint8_t log_file_entry;
	uint8_t shmask_mode_default;
	int bt_auto_disconnect;
	int bt_reset_before_pair;
	char bootcore[256];
	char video_conf[1024];
	char video_conf_pal[1024];
	char video_conf_ntsc[1024];
	char font[1024];
	char shared_folder[1024];
	char waitmount[1024];
	char custom_aspect_ratio[2][16];
	char afilter_default[1023];
	char vfilter_default[1023];
	char vfilter_vertical_default[1023];
	char vfilter_scanlines_default[1023];
	char shmask_default[1023];
	char preset_default[1023];
	char player_controller[6][8][256];
	char controller_deadzone[32][256];
	uint8_t rumble;
	uint8_t wheel_force;
	uint16_t wheel_range;
	uint8_t hdmi_game_mode;
	uint8_t vrr_mode;
	uint8_t vrr_min_framerate;
	uint8_t vrr_max_framerate;
	uint8_t vrr_vesa_framerate;
	uint16_t video_off;
	uint8_t disable_autofire;
	uint8_t video_brightness;
	uint8_t video_contrast;
	uint8_t video_saturation;
	uint16_t video_hue;
	char video_gain_offset[256];
	uint8_t hdr;
	uint16_t hdr_max_nits;
	uint16_t hdr_avg_nits;
	char vga_mode[16];
	uint8_t vga_mode_int;
	uint8_t ntsc_mode;
	uint32_t controller_unique_mapping[256];
	char osd_lock[25];
	uint16_t osd_lock_time;
	uint8_t debug;
	char main[1024];
	char vfilter_interlace_default[1023];
} cfg_t;

extern cfg_t cfg;

//// functions ////
void cfg_parse();
void cfg_print();
const char* cfg_get_name(uint8_t alt);
const char* cfg_get_label(uint8_t alt);
bool cfg_has_video_sections();
int cfg_save(uint8_t alt);
int cfg_save_core_specific(uint8_t alt);
void cfg_reset_all();
void cfg_reset_core_specific();

void cfg_error(const char *fmt, ...);
bool cfg_check_errors(char *msg, size_t max_len);

// Setting categories for OSD menu organization
typedef enum {
	CAT_AV_DIGITAL = 0,
	CAT_AV_ANALOG,
	CAT_INPUT_CONTROLLER,
	CAT_INPUT_ARCADE,
	CAT_INPUT_KB_MOUSE,
	CAT_UI,
	CAT_SYSTEM,
	CAT_FILTERS,
	CAT_COUNT
} osd_category_t;

// Category information
typedef struct {
	const char* name;
	const char* description;
} osd_category_info_t;

// Variable types for ini_var_t
typedef enum
{
	INI_UINT8 = 0, INI_INT8, INI_UINT16, INI_INT16, INI_UINT32, INI_INT32, INI_HEX8, INI_HEX16, INI_HEX32, INI_FLOAT, INI_STRING, INI_UINT32ARR, INI_HEX32ARR, INI_STRINGARR
} ini_vartypes_t;

// UI control types for auto-generated menus
typedef enum {
	UI_CHECKBOX = 0,    // Boolean on/off toggle (for 0/1 values)
	UI_SLIDER,          // Numeric range with +/- increment
	UI_DROPDOWN,        // Cycle through predefined options  
	UI_FILE_PICKER,     // File browser with filters
	UI_STRING_INPUT,    // Text input field
	UI_HIDDEN           // Don't show in auto-generated menus
} ui_type_t;

// Menu flags for where settings should appear
typedef enum {
	MENU_NONE = 0,
	MENU_MAIN = 1,      // Main settings menu
	MENU_CORE = 2,      // Core override settings menu  
	MENU_BOTH = 3       // Both menus
} menu_flags_t;

// Structure for unified settings definition
typedef struct
{
	const char* name;
	void* var;
	ini_vartypes_t type;
	int64_t min;
	int64_t max;
	// OSD metadata fields
	const char* display_name;
	const char* description;
	osd_category_t category;
	const char* unit;
	bool requires_reboot;
	// Dependency system
	const char* depends_on;         // Setting name this depends on (NULL if no dependency)
	int64_t depends_min;            // Minimum value for dependency to be active
	int64_t depends_max;            // Maximum value for dependency to be active (use same as min for exact match)
	// UI generation fields
	int8_t menu_position;           // Order within category (0=first, 99=auto-sort, 127=last, -1=hidden)
	menu_flags_t menu_flags;        // Which menus this appears in
	uint8_t step_size;              // For sliders: increment amount (0 = auto)
	// TODO: Add remaining UI fields to ini_vars entries as needed
	// ui_type_t ui_type;            // UI control type
	// const char** dropdown_options; // For dropdowns: NULL-terminated array
	// const char* file_filter;      // For file picker: extension or pattern
} ini_var_t;

// Get category and setting information
const osd_category_info_t* cfg_get_category_info(osd_category_t category);
int cfg_get_settings_for_category(osd_category_t category, const ini_var_t*** settings, int* count, menu_flags_t menu_type);
const char* cfg_get_setting_display_name(const char* ini_name);
const char* cfg_get_setting_description(const char* ini_name);

// Auto-generation helper functions
ui_type_t cfg_auto_detect_ui_type(const ini_var_t* var);
menu_flags_t cfg_auto_detect_menu_flags(const ini_var_t* var);
uint8_t cfg_auto_detect_step_size(const ini_var_t* var);

// Setting dependency functions
bool cfg_is_setting_enabled(const char* setting_name);
osd_category_t cfg_get_category_from_display_index(int display_index, menu_flags_t menu_type);
int cfg_get_display_index_from_category(osd_category_t category, menu_flags_t menu_type);

// Dynamic menu generation
int cfg_generate_category_menu(osd_category_t category, int menu_offset, int* menusub, const char* title, menu_flags_t menu_type, int* first_visible);
int cfg_generate_category_selection_menu(int menu_offset, int* menusub, const char* title, menu_flags_t menu_type);
const ini_var_t* cfg_get_category_setting_at_index(osd_category_t category, int index, menu_flags_t menu_type);
int cfg_count_enabled_settings_in_category(osd_category_t category, menu_flags_t menu_type);

// Test/debug functions for UI auto-detection
void cfg_print_ui_analysis(void);
void cfg_print_category_organization(void);
const char* cfg_ui_type_to_string(ui_type_t ui_type);
const char* cfg_menu_flags_to_string(menu_flags_t flags);

// UI rendering helper functions for enhanced menu display
void cfg_render_setting_value(char* buffer, size_t buffer_size, const char* setting_name, const char* display_name);
void cfg_get_var_value_as_string(const ini_var_t* var, char* buffer, size_t buffer_size);

// Help text system for menu settings
const ini_var_t* cfg_get_ini_var(const char* name);
const char* cfg_get_help_text(const char* setting_key);

// Virtual setting sync functions
void cfg_sync_mode_to_individual();
void cfg_sync_individual_to_mode();
void cfg_auto_set_sync_mode_for_video_mode(int video_mode);
void cfg_auto_configure_vga_settings(int vga_mode_int);
void cfg_auto_configure_vga_settings_temp(int vga_mode_int);

// Setting editing functions
void cfg_handle_setting_change(const ini_var_t* setting, int direction);
bool cfg_requires_confirmation(const ini_var_t* setting);
void cfg_setup_setting_confirmation(const ini_var_t* setting, const char* old_value, const char* new_value);
int generic_setting_apply(void);
int generic_setting_revert(void);

// Temporary settings system for AV menus
void cfg_temp_settings_start(osd_category_t category);
void cfg_temp_settings_change(const ini_var_t* setting, int direction);
bool cfg_temp_settings_apply(void);
bool cfg_temp_settings_apply_pending(void);
void cfg_temp_settings_revert(void);
void cfg_temp_settings_reject(void);
void cfg_temp_settings_clear(void);
bool cfg_temp_settings_has_changes(void);
void cfg_get_temp_var_value_as_string(const ini_var_t* var, char* buffer, size_t buffer_size);

// External access to ini_vars array and count
extern const ini_var_t ini_vars[];
extern const int nvars;

struct yc_mode
{
	char key[64];
	int64_t phase_inc;
};

void yc_parse(yc_mode *yc_table, int max);

// File picker support for configuration settings
extern char cfg_file_picker_initial_path[1024];
extern const ini_var_t* cfg_file_picker_current_setting;
extern int cfg_file_picker_return_state;
void cfg_set_file_picker_setting(const ini_var_t* setting);
void cfg_open_file_picker(const char* initial_dir, const char* file_ext);
void cfg_file_picker_callback(const char* selected_path);

#endif // __CFG_H__
