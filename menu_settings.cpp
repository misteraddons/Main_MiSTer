#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "menu.h"
#include "osd.h"
#include "user_io.h"
#include "cfg.h"
#include "input.h"
// #include "osd_settings.h" // Replaced with unified cfg system
#include "hardware.h"
#include "fpga_io.h"
#include "menu_column_positions.h"

// Menu states
typedef enum {
    SETTINGS_STATE_CATEGORIES = 0,
    SETTINGS_STATE_SETTINGS_LIST,
    SETTINGS_STATE_EDIT_SETTING,
    SETTINGS_STATE_CONFIRM_SAVE,
    SETTINGS_STATE_EXIT
} settings_menu_state_t;

// Global state
static settings_menu_state_t current_state = SETTINGS_STATE_CATEGORIES;
static int selected_category = 0;
static int selected_setting = 0;
static int setting_scroll = 0;
static const ini_var_t* current_setting = NULL;
static bool settings_changed = false;
static bool needs_reboot = false;

// Editing values
static int edit_int = 0;
static float edit_float = 0.0f;
static char edit_string[256] = "";
static int edit_enum = 0;

// Forward declarations
static void DrawCategoriesMenu();
static void DrawSettingsList();
static void DrawEditSetting();
static void DrawConfirmSave();
static void HandleCategoriesInput();
static void HandleSettingsListInput();
static void HandleEditSettingInput();
static void HandleConfirmSaveInput();
static void LoadCurrentSettingValue();
static void SaveCurrentSettingValue();
static const char* GetSettingValueString(const ini_var_t* setting);

// Main settings menu entry point
void SettingsMenu()
{
    printf("*** SettingsMenu() called - returning immediately ***\n");
    
    // TEMPORARY: Just return immediately to avoid freeze
    // TODO: Implement proper settings menu without conflicts
    return;
    
    // OLD CODE BELOW - COMMENTED OUT
    /*
    // Initialize state
    current_state = SETTINGS_STATE_CATEGORIES;
    selected_category = 0;
    selected_setting = 0;
    setting_scroll = 0;
    settings_changed = false;
    needs_reboot = false;
    
    printf("*** Starting settings menu loop ***\n");
    
    // Menu loop
    int loop_count = 0;
    while (current_state != SETTINGS_STATE_EXIT && loop_count < 100) // Reduced limit for testing
    {
        // Draw current screen
        switch (current_state)
        {
            case SETTINGS_STATE_CATEGORIES:
                DrawCategoriesMenu();
                break;
            case SETTINGS_STATE_SETTINGS_LIST:
                DrawSettingsList();
                break;
            case SETTINGS_STATE_EDIT_SETTING:
                DrawEditSetting();
                break;
            case SETTINGS_STATE_CONFIRM_SAVE:
                DrawConfirmSave();
                break;
            default:
                current_state = SETTINGS_STATE_EXIT;
                break;
        }
        
        // Small delay for responsiveness
        usleep(50000);
        
        loop_count++;
        if (loop_count % 20 == 0) {
            printf("*** Settings menu loop: %d, state: %d ***\n", loop_count, current_state);
        }
        
        // Handle input based on current state
        switch (current_state)
        {
            case SETTINGS_STATE_CATEGORIES:
                HandleCategoriesInput();
                break;
            case SETTINGS_STATE_SETTINGS_LIST:
                HandleSettingsListInput();
                break;
            case SETTINGS_STATE_EDIT_SETTING:
                HandleEditSettingInput();
                break;
            case SETTINGS_STATE_CONFIRM_SAVE:
                HandleConfirmSaveInput();
                break;
            default:
                break;
        }
    }
    
    if (loop_count >= 100) {
        printf("*** Settings menu loop limit reached - exiting ***\n");
    }
    
    printf("*** SettingsMenu() exiting ***\n");
    */
}

// Draw categories selection menu
static void DrawCategoriesMenu()
{
    char s[64];
    
    // Title with change indicator
    if (settings_changed)
        OsdSetTitle("Settings *", OSD_ARROW_LEFT);
    else
        OsdSetTitle("Settings", OSD_ARROW_LEFT);
    
    int line = 0;
    
    // Header
    OsdWrite(line++, "", 0, 0);
    OsdWrite(line++, "  Select Category:", 0, 0);
    OsdWrite(line++, "", 0, 0);
    
    // Categories
    for (int i = 0; i < CAT_COUNT; i++)
    {
        const osd_category_info_t* cat_info = cfg_get_category_info((osd_category_t)i);
        
        // Format with icon if available
        if (cat_info->icon && strlen(cat_info->icon) > 0)
        {
            snprintf(s, sizeof(s), "  %s %s", cat_info->icon, cat_info->name);
        }
        else
        {
            snprintf(s, sizeof(s), "    %s", cat_info->name);
        }
        
        OsdWrite(line++, s, i == selected_category, 0);
    }
    
    // Fill remaining lines
    for (; line < 15; line++)
    {
        OsdWrite(line, "", 0, 0);
    }
    
    // Help text
    if (settings_changed)
        OsdWrite(15, " \x12\x13:Select \x1B:Enter ESC:Save&Exit", 0, 0);
    else
        OsdWrite(15, " \x12\x13:Select \x1B:Enter ESC:Exit", 0, 0);
}

// Note: Column positions are now pre-computed in menu_column_positions.h

// Draw settings list for selected category
static void DrawSettingsList()
{
    char s[64];
    
    const osd_category_info_t* cat_info = cfg_get_category_info((osd_category_t)selected_category);
    
    // Title with category name
    snprintf(s, sizeof(s), "%.20s", cat_info->name);
    OsdSetTitle(s, OSD_ARROW_LEFT);
    
    // Get settings for this category - we need to manually filter from cfg.cpp
    static const ini_var_t* category_settings[100];
    int count = 0;
    
    // Filter settings by category from the global ini_vars array
    extern const ini_var_t ini_vars[];
    extern const int nvars;
    
    for (int i = 0; i < nvars; i++)
    {
        if (ini_vars[i].category == selected_category)
        {
            category_settings[count++] = &ini_vars[i];
            if (count >= 100) break;
        }
    }
    
    const ini_var_t** settings = (const ini_var_t**)category_settings;
    
    // Use pre-computed value column position for this category
    int value_column_pos = category_value_column_pos[selected_category];
    
    // Debug output to show pre-computed positioning
    printf("DEBUG: Category \"%s\" - Pre-computed value column position: %d (longest name: %d chars)\n", 
           cat_info->name, value_column_pos, category_longest_name[selected_category]);
    
    // Adjust scroll position
    if (selected_setting < setting_scroll)
        setting_scroll = selected_setting;
    if (selected_setting >= setting_scroll + 12)
        setting_scroll = selected_setting - 11;
    
    int line = 0;
    
    // Settings list
    for (int i = setting_scroll; i < count && line < 14; i++)
    {
        const ini_var_t* setting = settings[i];
        const char* value_str = GetSettingValueString(setting);
        
        // Use display_name if available, otherwise use name
        const char* display_name = setting->display_name ? setting->display_name : setting->name;
        
        // Format setting line with dynamic positioning
        int name_len = strlen(display_name);
        int value_len = strlen(value_str);
        
        // Check if name fits in available space
        int max_name_space = value_column_pos - 1; // Leave 1 char for colon
        
        if (name_len > max_name_space)
        {
            // Truncate name if too long
            strncpy(s, display_name, max_name_space - 2);
            s[max_name_space - 2] = '\0';
            strcat(s, "..");
        }
        else
        {
            strcpy(s, display_name);
        }
        
        strcat(s, ":");
        
        // Pad to value column position
        int current_len = strlen(s);
        while (current_len < value_column_pos && current_len < 26)
        {
            strcat(s, " ");
            current_len++;
        }
        
        // Add value, ensuring we don't exceed screen width
        int remaining_space = 28 - current_len; // 28 total chars, leaving 2 for reboot indicator
        if (value_len > remaining_space)
        {
            // Truncate value if necessary
            strncat(s, value_str, remaining_space - 2);
            strcat(s, "..");
        }
        else
        {
            strcat(s, value_str);
        }
        
        // Add reboot indicator
        if (setting->requires_reboot)
            strcat(s, "\x1D");
        
        OsdWrite(line++, s, i == selected_setting, 0);
    }
    
    // Fill remaining lines
    for (; line < 15; line++)
    {
        OsdWrite(line, "", 0, 0);
    }
    
    // Help text
    if (count > 12)
    {
        snprintf(s, sizeof(s), " \x12\x13:Select(%d/%d) \x1B:Edit ESC:Back", 
                selected_setting + 1, count);
    }
    else
    {
        strcpy(s, " \x12\x13:Select \x1B:Edit ESC:Back");
    }
    OsdWrite(15, s, 0, 0);
}

// Draw setting editor
static void DrawEditSetting()
{
    char s[64];
    
    // Title with setting name (truncated if needed)
    strncpy(s, current_setting->display_name, 20);
    s[20] = '\0';
    OsdSetTitle(s, OSD_ARROW_LEFT);
    
    int line = 1;
    
    // Setting description
    if (current_setting->description)
    {
        // Word wrap description if needed
        const char* desc = current_setting->description;
        int desc_len = strlen(desc);
        if (desc_len <= 28)
        {
            OsdWrite(line++, desc, 0, 0);
        }
        else
        {
            // Simple word wrap - break at spaces near 28 chars
            strncpy(s, desc, 28);
            s[28] = '\0';
            
            // Find last space to break cleanly
            int break_pos = 27;
            while (break_pos > 0 && s[break_pos] != ' ')
                break_pos--;
            
            if (break_pos > 0)
            {
                s[break_pos] = '\0';
                OsdWrite(line++, s, 0, 0);
                
                // Second line
                if (desc_len > break_pos + 1)
                {
                    strncpy(s, desc + break_pos + 1, 28);
                    s[28] = '\0';
                    OsdWrite(line++, s, 0, 0);
                }
            }
            else
            {
                OsdWrite(line++, s, 0, 0);
            }
        }
    }
    
    line++; // Empty line
    
    // Current value and editing interface
    switch (current_setting->type)
    {
        case INI_UINT8:
        case INI_INT8:
        case INI_UINT16:
        case INI_INT16:
        case INI_UINT32:
        case INI_INT32:
        {
            snprintf(s, sizeof(s), "Value: %d%s", edit_int, 
                    current_setting->unit ? current_setting->unit : "");
            OsdWrite(line++, s, 1, 0);
            
            snprintf(s, sizeof(s), "Range: %lld - %lld", 
                    current_setting->min, current_setting->max);
            OsdWrite(line++, s, 0, 0);
            break;
        }
        
        case INI_HEX8:
        case INI_HEX16:
        case INI_HEX32:
        {
            snprintf(s, sizeof(s), "Value: 0x%X", edit_int);
            OsdWrite(line++, s, 1, 0);
            
            snprintf(s, sizeof(s), "Range: 0x%llX - 0x%llX", 
                    current_setting->min, current_setting->max);
            OsdWrite(line++, s, 0, 0);
            break;
        }
        
        case INI_FLOAT:
        {
            snprintf(s, sizeof(s), "Value: %.2f%s", edit_float,
                    current_setting->unit ? current_setting->unit : "");
            OsdWrite(line++, s, 1, 0);
            
            snprintf(s, sizeof(s), "Range: %.2f - %.2f", 
                    (float)current_setting->min, (float)current_setting->max);
            OsdWrite(line++, s, 0, 0);
            break;
        }
        
        case INI_STRING:
        {
            snprintf(s, sizeof(s), "Value: %.20s", edit_string);
            OsdWrite(line++, s, 1, 0);
            OsdWrite(line++, "(String editing not implemented)", 0, 0);
            break;
        }
        
        default:
            OsdWrite(line++, "Unsupported setting type", 0, 0);
            break;
    }
    
    // Fill remaining lines
    for (; line < 15; line++)
    {
        OsdWrite(line, "", 0, 0);
    }
    
    // Help text based on setting type
    switch (current_setting->type)
    {
        case INI_UINT8:
        case INI_INT8:
        case INI_UINT16:
        case INI_INT16:
        case INI_UINT32:
        case INI_INT32:
        case INI_FLOAT:
        case INI_HEX8:
        case INI_HEX16:
        case INI_HEX32:
            strcpy(s, " \x12\x13:±1 \x11\x10:±10 \x1B:Save ESC:Cancel");
            break;
        case INI_STRING:
            strcpy(s, " \x1B:Save ESC:Cancel (Edit N/A)");
            break;
        default:
            strcpy(s, " ESC:Cancel");
            break;
    }
    OsdWrite(15, s, 0, 0);
}

// Draw save confirmation
static void DrawConfirmSave()
{
    OsdSetTitle("Confirm Save", 0);
    
    int line = 4;
    
    // Get the current INI filename and display it
    const char *ini_filename = cfg_get_name(altcfg(-1));
    char save_message[64];
    snprintf(save_message, sizeof(save_message), "Save changes to %s?", ini_filename);
    OsdWrite(line++, save_message, 0, 0);
    line++;
    
    if (needs_reboot)
    {
        OsdWrite(line++, "Some changes require reboot", 0, 0);
        line++;
    }
    
    OsdWrite(line++, "  Yes", 1, 0);
    OsdWrite(line++, "  No", 0, 0);
    
    // Fill remaining lines
    for (; line < 15; line++)
    {
        OsdWrite(line, "", 0, 0);
    }
    
    OsdWrite(15, " \x12\x13:Select \x1B:Confirm", 0, 0);
}

// Input handlers
static void HandleCategoriesInput()
{
    static int debug_count = 0;
    debug_count++;
    
    if (debug_count % 50 == 0) {
        printf("*** HandleCategoriesInput: debug_count=%d ***\n", debug_count);
    }
    
    if (user_io_menu_button())
    {
        printf("*** Menu button pressed - exiting ***\n");
        if (settings_changed)
            current_state = SETTINGS_STATE_CONFIRM_SAVE;
        else
            current_state = SETTINGS_STATE_EXIT;
        return;
    }
    
    static uint32_t last_input = 0;
    uint32_t input = user_io_user_button();
    
    if (debug_count % 50 == 0) {
        printf("*** HandleCategoriesInput: input=0x%08X, last_input=0x%08X ***\n", input, last_input);
    }
    
    // Debounce input
    if (input == last_input) return;
    last_input = input;
    
    if (input)
    {
        if (input & JOY_UP)
        {
            if (selected_category > 0)
                selected_category--;
        }
        else if (input & JOY_DOWN)
        {
            if (selected_category < CAT_COUNT - 1)
                selected_category++;
        }
        else if (input & (JOY_BTN1 | JOY_RIGHT))
        {
            current_state = SETTINGS_STATE_SETTINGS_LIST;
            selected_setting = 0;
            setting_scroll = 0;
        }
    }
}

static void HandleSettingsListInput()
{
    if (user_io_menu_button())
    {
        current_state = SETTINGS_STATE_CATEGORIES;
        return;
    }
    
    static uint32_t last_input = 0;
    uint32_t input = user_io_user_button();
    
    if (input == last_input) return;
    last_input = input;
    
    if (input)
    {
        // Get settings for this category from the global ini_vars array
        static const ini_var_t* category_settings[100];
        int count = 0;
        
        extern const ini_var_t ini_vars[];
        extern const int nvars;
        
        for (int i = 0; i < nvars; i++)
        {
            if (ini_vars[i].category == selected_category)
            {
                category_settings[count++] = &ini_vars[i];
                if (count >= 100) break;
            }
        }
        
        if (input & JOY_UP)
        {
            if (selected_setting > 0)
                selected_setting--;
        }
        else if (input & JOY_DOWN)
        {
            if (selected_setting < count - 1)
                selected_setting++;
        }
        else if (input & (JOY_BTN1 | JOY_RIGHT))
        {
            if (count > 0)
            {
                current_setting = category_settings[selected_setting];
                LoadCurrentSettingValue();
                current_state = SETTINGS_STATE_EDIT_SETTING;
            }
        }
        else if (input & JOY_LEFT)
        {
            current_state = SETTINGS_STATE_CATEGORIES;
        }
    }
}

static void HandleEditSettingInput()
{
    if (user_io_menu_button())
    {
        current_state = SETTINGS_STATE_SETTINGS_LIST;
        return;
    }
    
    static uint32_t last_input = 0;
    uint32_t input = user_io_user_button();
    
    if (input == last_input) return;
    last_input = input;
    
    if (input)
    {
        switch (current_setting->type)
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
                if (input & JOY_UP)
                {
                    if (edit_int < current_setting->max)
                    {
                        edit_int++;
                    }
                }
                else if (input & JOY_DOWN)
                {
                    if (edit_int > current_setting->min)
                    {
                        edit_int--;
                    }
                }
                else if (input & JOY_RIGHT)
                {
                    if (edit_int + 10 <= current_setting->max)
                    {
                        edit_int += 10;
                    }
                }
                else if (input & JOY_LEFT)
                {
                    if (edit_int - 10 >= current_setting->min)
                    {
                        edit_int -= 10;
                    }
                }
                break;
                
            case INI_FLOAT:
                if (input & JOY_UP)
                {
                    if (edit_float + 0.1f <= current_setting->max)
                    {
                        edit_float += 0.1f;
                    }
                }
                else if (input & JOY_DOWN)
                {
                    if (edit_float - 0.1f >= current_setting->min)
                    {
                        edit_float -= 0.1f;
                    }
                }
                else if (input & JOY_RIGHT)
                {
                    if (edit_float + 1.0f <= current_setting->max)
                    {
                        edit_float += 1.0f;
                    }
                }
                else if (input & JOY_LEFT)
                {
                    if (edit_float - 1.0f >= current_setting->min)
                    {
                        edit_float -= 1.0f;
                    }
                }
                break;
                
            default:
                break;
        }
        
        // Save on Enter/A button
        if (input & JOY_BTN1)
        {
            SaveCurrentSettingValue();
            settings_changed = true;
            if (current_setting->requires_reboot)
                needs_reboot = true;
            current_state = SETTINGS_STATE_SETTINGS_LIST;
        }
    }
}

static void HandleConfirmSaveInput()
{
    static uint32_t last_input = 0;
    uint32_t input = user_io_user_button();
    
    if (input == last_input) return;
    last_input = input;
    
    if (input & JOY_BTN1)  // Enter/A - Yes, save
    {
        // Save settings to currently active alternate INI
        if (cfg_save(altcfg(-1)))  // Save to current alternate INI
        {
            settings_changed = false;
            current_state = SETTINGS_STATE_EXIT;
        }
        else
        {
            // TODO: Show error message if save fails
            current_state = SETTINGS_STATE_EXIT;
        }
    }
    else if (user_io_menu_button())  // ESC/Menu - No, don't save
    {
        current_state = SETTINGS_STATE_EXIT;
    }
}

// Helper functions
static void LoadCurrentSettingValue()
{
    if (!current_setting) return;
    
    switch (current_setting->type)
    {
        case INI_UINT8:
        case INI_INT8:
            edit_int = *(uint8_t*)current_setting->var;
            edit_enum = edit_int;
            break;
            
        case INI_UINT16:
        case INI_INT16:
            edit_int = *(uint16_t*)current_setting->var;
            break;
            
        case INI_UINT32:
        case INI_INT32:
        case INI_HEX8:
        case INI_HEX16:
        case INI_HEX32:
            edit_int = *(uint32_t*)current_setting->var;
            break;
            
        case INI_FLOAT:
            edit_float = *(float*)current_setting->var;
            break;
            
        case INI_STRING:
            strncpy(edit_string, (char*)current_setting->var, sizeof(edit_string) - 1);
            edit_string[sizeof(edit_string) - 1] = '\0';
            break;
            
        default:
            break;
    }
}

static void SaveCurrentSettingValue()
{
    if (!current_setting) return;
    
    switch (current_setting->type)
    {
        case INI_UINT8:
        case INI_INT8:
            *(uint8_t*)current_setting->var = edit_int;
            break;
            
        case INI_UINT16:
        case INI_INT16:
            *(uint16_t*)current_setting->var = edit_int;
            break;
            
        case INI_UINT32:
        case INI_INT32:
        case INI_HEX8:
        case INI_HEX16:
        case INI_HEX32:
            *(uint32_t*)current_setting->var = edit_int;
            break;
            
        case INI_FLOAT:
            *(float*)current_setting->var = edit_float;
            break;
            
        case INI_STRING:
            strcpy((char*)current_setting->var, edit_string);
            break;
            
        default:
            break;
    }
}

static const char* GetSettingValueString(const ini_var_t* setting)
{
    static char value_str[32];
    
    switch (setting->type)
    {
        case INI_UINT8:
        case INI_INT8:
        {
            int val = *(uint8_t*)setting->var;
            snprintf(value_str, sizeof(value_str), "%d%s", val, 
                    setting->unit ? setting->unit : "");
            return value_str;
        }
        
        case INI_UINT16:
        case INI_INT16:
        {
            int val = *(uint16_t*)setting->var;
            snprintf(value_str, sizeof(value_str), "%d%s", val, 
                    setting->unit ? setting->unit : "");
            return value_str;
        }
        
        case INI_UINT32:
        case INI_INT32:
        {
            int val = *(uint32_t*)setting->var;
            snprintf(value_str, sizeof(value_str), "%d%s", val, 
                    setting->unit ? setting->unit : "");
            return value_str;
        }
        
        case INI_HEX8:
        {
            int val = *(uint8_t*)setting->var;
            snprintf(value_str, sizeof(value_str), "0x%02X", val);
            return value_str;
        }
        
        case INI_HEX16:
        {
            int val = *(uint16_t*)setting->var;
            snprintf(value_str, sizeof(value_str), "0x%04X", val);
            return value_str;
        }
        
        case INI_HEX32:
        {
            int val = *(uint32_t*)setting->var;
            snprintf(value_str, sizeof(value_str), "0x%08X", val);
            return value_str;
        }
        
        case INI_FLOAT:
        {
            float val = *(float*)setting->var;
            snprintf(value_str, sizeof(value_str), "%.2f%s", val, 
                    setting->unit ? setting->unit : "");
            return value_str;
        }
        
        case INI_STRING:
        {
            const char* str = (const char*)setting->var;
            if (strlen(str) > 8)
            {
                strncpy(value_str, str, 5);
                value_str[5] = '\0';
                strcat(value_str, "...");
                return value_str;
            }
            return str;
        }
        
        default:
            return "N/A";
    }
}