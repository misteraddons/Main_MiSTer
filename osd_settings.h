#ifndef OSD_SETTINGS_H
#define OSD_SETTINGS_H

#include <stdint.h>

// Setting categories for OSD menu organization
typedef enum {
    CAT_VIDEO_DISPLAY = 0,
    CAT_AUDIO,
    CAT_INPUT_CONTROLLERS,
    CAT_SYSTEM_BOOT,
    CAT_NETWORK_STORAGE,
    CAT_ADVANCED,
    CAT_COUNT
} osd_category_t;

// Setting types for different UI controls
typedef enum {
    TYPE_BOOL = 0,      // Toggle (On/Off)
    TYPE_INT,           // Numeric input
    TYPE_HEX,           // Hexadecimal input
    TYPE_FLOAT,         // Float input
    TYPE_STRING,        // Text input
    TYPE_ENUM,          // Dropdown selection
    TYPE_ARRAY,         // Array of values
    TYPE_CUSTOM         // Custom handler needed
} osd_setting_type_t;

// Setting definition structure
typedef struct {
    const char* ini_name;           // MiSTer.ini key name
    const char* display_name;       // Display name in OSD
    const char* description;        // Help text/tooltip
    osd_category_t category;        // Category for organization
    osd_setting_type_t type;        // Data type
    const void* var_ptr;           // Pointer to cfg variable
    int64_t min;                   // Min value (for numeric types)
    int64_t max;                   // Max value (for numeric types)
    const char** enum_options;      // Options for TYPE_ENUM
    int enum_count;                // Number of enum options
    const char* unit;              // Unit suffix (e.g., "Hz", "ms")
    bool requires_reboot;          // Does changing require reboot?
} osd_setting_def_t;

// Category information
typedef struct {
    const char* name;
    const char* icon;           // Optional icon character
    const char* description;
} osd_category_info_t;

// Function declarations
const osd_category_info_t* osd_get_category_info(osd_category_t category);
const osd_setting_def_t* osd_get_settings_for_category(osd_category_t category, int* count);
const osd_setting_def_t* osd_get_setting_by_name(const char* ini_name);
int osd_get_total_settings(void);

#endif // OSD_SETTINGS_H