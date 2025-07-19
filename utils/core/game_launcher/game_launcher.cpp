/*
 * MiSTer Game Launcher Service
 * 
 * Centralized GameID lookup and MGL creation service
 * Extracted from cdrom_daemon to support multiple input sources
 * 
 * Supports:
 * - GameID lookup by serial number, title, or hash
 * - Fuzzy search with weighted scoring
 * - MGL file creation (single or multiple selection)
 * - OSD notifications
 * - Multiple game file formats (CHD, CUE, ISO)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#define GAME_LAUNCHER_FIFO "/dev/MiSTer_game_launcher"
#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define CONFIG_FILE "/media/fat/utils/game_launcher.conf"
#define PID_FILE "/tmp/game_launcher.pid"
#define FAVORITES_FILE "/media/fat/utils/favorites.txt"
#define HISTORY_FILE "/media/fat/utils/game_history.txt"
#define STATS_FILE "/media/fat/utils/game_stats.txt"
#define RATINGS_FILE "/media/fat/utils/game_ratings.txt"
#define COMPLETION_FILE "/media/fat/utils/game_completion.txt"
#define PLAYTIME_FILE "/media/fat/utils/game_playtime.txt"
#define COLLECTIONS_FILE "/media/fat/utils/game_collections.txt"
#define MAX_SEARCH_RESULTS 50
#define MAX_FAVORITES 100
#define MAX_COLLECTIONS 20
#define MAX_HISTORY 1000

// Configuration structure
typedef struct {
    char games_dir[256];
    char gameid_dir[256];
    char temp_dir[256];
    int fuzzy_threshold;
    int osd_timeout;
    bool show_notifications;
    int max_results;
    char region_priority[128];
    bool enable_favorites;
    bool enable_history;
    bool enable_stats;
    bool enable_ratings;
    bool enable_completion;
    bool enable_playtime;
    bool enable_collections;
    int max_history_entries;
} launcher_config_t;

// Favorite game entry
typedef struct {
    char core[16];
    char id_type[16];
    char identifier[128];
    char title[128];
    time_t added_time;
    int play_count;
} favorite_game_t;

// Game history entry
typedef struct {
    char core[16];
    char identifier[128];
    char title[128];
    time_t play_time;
    char source[32];
} history_entry_t;

// Game statistics
typedef struct {
    char identifier[128];
    int total_plays;
    time_t first_played;
    time_t last_played;
    int favorite_rank;
} game_stats_t;

// Game rating entry
typedef struct {
    char core[16];
    char identifier[128];
    char title[128];
    int rating; // 1-5 stars
    char review[256];
    time_t rated_time;
} game_rating_t;

// Game collections management structure
typedef struct {
    char name[64];
    char description[256];
    favorite_game_t games[MAX_FAVORITES]; // Reuse favorite structure
    int game_count;
} game_collection_t;

// Game completion entry
typedef struct {
    char core[16];
    char identifier[128];
    char title[128];
    int completion_percentage; // 0-100
    bool completed;
    time_t completion_time;
    char notes[256];
} game_completion_t;

// Game playtime entry
typedef struct {
    char core[16];
    char identifier[128];
    char title[128];
    int total_minutes;
    int session_count;
    time_t last_played;
    time_t session_start;
} game_playtime_t;


// Smart recommendations structure
typedef struct {
    char core[32];
    char identifier[128];
    char title[128];
    float recommendation_score;
} game_recommendation_t;

// Console generation definitions
typedef enum {
    GEN_UNKNOWN = 0,
    GEN_1ST,    // Magnavox Odyssey, Pong
    GEN_2ND,    // Atari 2600, Intellivision
    GEN_3RD,    // NES, Master System
    GEN_4TH,    // SNES, Genesis, TG16
    GEN_5TH,    // PSX, Saturn, N64
    GEN_6TH,    // PS2, Xbox, GameCube, Dreamcast
    GEN_7TH,    // PS3, Xbox 360, Wii
    GEN_8TH     // PS4, Xbox One, Wii U, Switch
} console_generation_t;

// GameID data structures
typedef struct {
    char manufacturer_id[64];
    char id[64]; 
    char version[64];
    char device_info[64];
    char internal_title[128];
    char release_date[32];
    char device_support[64];
    char target_area[64];
    char title[256];
    char language[64];
    char redump_name[256];
    char region[64];
    char system[32];
    char publisher[128];
    char year[16];
    char product_code[32];
    bool valid;
} game_info_t;

// Search result structure
typedef struct {
    char path[512];
    char title[256];
    char region[64];
    int fuzzy_score;
    int region_score;
    int total_score;
} search_result_t;

// Global variables
static launcher_config_t config;
static volatile int keep_running = 1;
static search_result_t search_results[MAX_SEARCH_RESULTS];
static int search_results_count = 0;
static favorite_game_t favorites[MAX_FAVORITES];
static int favorites_count = 0;
static history_entry_t history[MAX_HISTORY];
static int history_count = 0;
static game_rating_t* ratings = NULL;
static int ratings_count = 0;
static game_completion_t* completions = NULL;
static int completions_count = 0;
static game_playtime_t* playtimes = NULL;
static int playtimes_count = 0;
static int collections_count = 0;

// Playtime tracking session variables
static char current_playing_core[32] = "";
static char current_playing_identifier[128] = "";
static time_t current_session_start = 0;

// Forward declarations
bool process_game_request(const char* system, const char* id_type, const char* identifier, const char* source);
void init_playtime_tracking();
void start_game_session(const char* core, const char* identifier);
void stop_game_session();
void update_playtime_tracking();
void stop_playtime_tracking();
int get_game_playtime(const char* core, const char* identifier);
void load_collections();
bool create_collection(const char* name, const char* description);
bool add_game_to_collection(const char* collection_name, const char* core, const char* id_type, 
                           const char* identifier, const char* title);
bool get_random_from_collection(const char* collection_name, char* core, char* id_type, char* identifier, char* title);
int get_game_recommendations(game_recommendation_t* recommendations, int max_recommendations);
bool get_random_recommendation(char* core, char* id_type, char* identifier, char* title);

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    strcpy(config.games_dir, "/media/fat/games");
    strcpy(config.gameid_dir, "/media/fat/utils/gameDB");
    strcpy(config.temp_dir, "/tmp");
    config.fuzzy_threshold = 30;
    config.osd_timeout = 3000;
    config.show_notifications = true;
    config.max_results = 10;
    strcpy(config.region_priority, "USA,Europe,Japan,World");
    config.enable_favorites = true;
    config.enable_history = true;
    config.enable_stats = true;
    config.enable_ratings = true;
    config.enable_completion = true;
    config.enable_playtime = true;
    config.enable_collections = true;
    config.max_history_entries = 100;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("game_launcher: Using default configuration\n");
        return;
    }
    
    char line[512];
    char section[64] = "";
    
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(section, line + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
            }
            continue;
        }
        
        char* equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char* key = line;
        char* value = equals + 1;
        
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        
        if (strcmp(section, "Paths") == 0) {
            if (strcmp(key, "games_dir") == 0) {
                strncpy(config.games_dir, value, sizeof(config.games_dir) - 1);
            } else if (strcmp(key, "gameid_dir") == 0) {
                strncpy(config.gameid_dir, value, sizeof(config.gameid_dir) - 1);
            } else if (strcmp(key, "temp_dir") == 0) {
                strncpy(config.temp_dir, value, sizeof(config.temp_dir) - 1);
            }
        } else if (strcmp(section, "Search") == 0) {
            if (strcmp(key, "fuzzy_threshold") == 0) {
                config.fuzzy_threshold = atoi(value);
            } else if (strcmp(key, "max_results") == 0) {
                config.max_results = atoi(value);
            } else if (strcmp(key, "region_priority") == 0) {
                strncpy(config.region_priority, value, sizeof(config.region_priority) - 1);
            }
        } else if (strcmp(section, "OSD") == 0) {
            if (strcmp(key, "show_notifications") == 0) {
                config.show_notifications = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "osd_timeout") == 0) {
                config.osd_timeout = atoi(value);
            }
        } else if (strcmp(section, "Features") == 0) {
            if (strcmp(key, "enable_favorites") == 0) {
                config.enable_favorites = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "enable_history") == 0) {
                config.enable_history = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "enable_stats") == 0) {
                config.enable_stats = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "max_history_entries") == 0) {
                config.max_history_entries = atoi(value);
            }
        }
    }
    
    fclose(fp);
    printf("game_launcher: Configuration loaded\n");
}

// Send command to MiSTer
bool send_mister_command(const char* cmd) {
    int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    ssize_t written = write(fd, cmd, strlen(cmd));
    close(fd);
    
    return written > 0;
}

// Send OSD message
void send_osd_message(const char* message) {
    if (!config.show_notifications) return;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "osd_message %s", message);
    send_mister_command(cmd);
}

// Levenshtein distance for fuzzy matching
int levenshtein_distance(const char* s1, const char* s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    int matrix[len1 + 1][len2 + 1];
    
    for (int i = 0; i <= len1; i++) {
        matrix[i][0] = i;
    }
    for (int j = 0; j <= len2; j++) {
        matrix[0][j] = j;
    }
    
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            if (tolower(s1[i-1]) == tolower(s2[j-1])) {
                matrix[i][j] = matrix[i-1][j-1];
            } else {
                matrix[i][j] = 1 + (matrix[i-1][j] < matrix[i][j-1] ? 
                    (matrix[i-1][j] < matrix[i-1][j-1] ? matrix[i-1][j] : matrix[i-1][j-1]) :
                    (matrix[i][j-1] < matrix[i-1][j-1] ? matrix[i][j-1] : matrix[i-1][j-1]));
            }
        }
    }
    
    return matrix[len1][len2];
}

// Calculate fuzzy match score
int calculate_fuzzy_score(const char* title, const char* search_term) {
    int distance = levenshtein_distance(title, search_term);
    int max_len = strlen(title) > strlen(search_term) ? strlen(title) : strlen(search_term);
    
    if (max_len == 0) return 0;
    
    int score = 100 - (distance * 100 / max_len);
    return score < 0 ? 0 : score;
}

// Calculate region score
int calculate_region_score(const char* region) {
    if (strstr(region, "USA") || strstr(region, "US") || strstr(region, "NTSC-U")) return 90;
    if (strstr(region, "Europe") || strstr(region, "EUR") || strstr(region, "PAL")) return 80;
    if (strstr(region, "Japan") || strstr(region, "JPN") || strstr(region, "JP") || strstr(region, "NTSC-J")) return 70;
    if (strstr(region, "World")) return 60;
    if (strstr(region, "Asia")) return 50;
    return 10;
}

// Add search result
bool add_search_result(const char* path, const char* title, const char* region, const char* search_term) {
    if (search_results_count >= MAX_SEARCH_RESULTS) return false;
    
    search_result_t* result = &search_results[search_results_count];
    
    strncpy(result->path, path, sizeof(result->path) - 1);
    strncpy(result->title, title, sizeof(result->title) - 1);
    strncpy(result->region, region, sizeof(result->region) - 1);
    
    // Check for exact redump_name match (remove file extension for comparison)
    char title_no_ext[256];
    strncpy(title_no_ext, title, sizeof(title_no_ext) - 1);
    title_no_ext[sizeof(title_no_ext) - 1] = '\0';
    
    // Remove file extension
    char* ext_pos = strrchr(title_no_ext, '.');
    if (ext_pos) *ext_pos = '\0';
    
    // Check if this is an exact redump_name match
    if (strcmp(title_no_ext, search_term) == 0) {
        // Perfect match for redump_name
        result->fuzzy_score = 100;
        printf("game_launcher: EXACT REDUMP MATCH: '%s' == '%s' -> fuzzy_score=100\n", title_no_ext, search_term);
        printf("game_launcher: 100%% match found - stopping search and auto-booting\n");
        result->region_score = calculate_region_score(region);
        result->total_score = result->fuzzy_score;
        search_results_count++;
        return true; // Exit search immediately
    } else {
        result->fuzzy_score = calculate_fuzzy_score(title_no_ext, search_term);
    }
    
    result->region_score = calculate_region_score(region);
    result->total_score = result->fuzzy_score;
    
    printf("game_launcher: Added result: '%s' vs '%s' -> fuzzy_score=%d, region_score=%d, total_score=%d\n", 
           title, search_term, result->fuzzy_score, result->region_score, result->total_score);
    
    search_results_count++;
    return false;
}

// Sort search results by score
void sort_search_results() {
    for (int i = 0; i < search_results_count - 1; i++) {
        for (int j = i + 1; j < search_results_count; j++) {
            if (search_results[i].total_score < search_results[j].total_score) {
                search_result_t temp = search_results[i];
                search_results[i] = search_results[j];
                search_results[j] = temp;
            }
        }
    }
}

// Search GameID for game by serial with fuzzy fallback
bool search_gameid_by_serial(const char* system, const char* serial, game_info_t* game_info) {
    printf("game_launcher: DEBUG - search_gameid_by_serial called with system='%s', serial='%s'\n", system, serial);
    const char* db_system = (strcmp(system, "MegaCD") == 0) ? "SegaCD" : system;
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/%s.data.json", config.gameid_dir, db_system);
    
    FILE* fp = fopen(db_path, "r");
    if (!fp) {
        printf("game_launcher: GameID file not found: %s\n", db_path);
        return false;
    }
    
    // Read file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* json_data = (char*)malloc(file_size + 1);
    fread(json_data, 1, file_size, fp);
    json_data[file_size] = '\0';
    fclose(fp);
    
    // First try exact serial lookup - try multiple patterns
    printf("game_launcher: Searching for serial: %s\n", serial);
    printf("game_launcher: JSON file size: %ld bytes\n", file_size);
    
    // Try different serial patterns with variations for spaces, underscores, hyphens
    char* pos = NULL;
    char search_patterns[12][128];
    
    // Create variations of the serial by replacing spaces, underscores, hyphens
    char serial_variations[3][128];
    strcpy(serial_variations[0], serial); // Original: "GM T-125045-00"
    
    // Replace spaces with underscores
    strcpy(serial_variations[1], serial);
    for (int i = 0; serial_variations[1][i]; i++) {
        if (serial_variations[1][i] == ' ') serial_variations[1][i] = '_';
    }
    
    // Replace spaces with hyphens  
    strcpy(serial_variations[2], serial);
    for (int i = 0; serial_variations[2][i]; i++) {
        if (serial_variations[2][i] == ' ') serial_variations[2][i] = '-';
    }
    
    // Generate search patterns for each variation
    int pattern_count = 0;
    for (int var = 0; var < 3; var++) {
        snprintf(search_patterns[pattern_count++], sizeof(search_patterns[0]), "\"%s\":", serial_variations[var]);
        snprintf(search_patterns[pattern_count++], sizeof(search_patterns[0]), "\"%s\"", serial_variations[var]);
        snprintf(search_patterns[pattern_count++], sizeof(search_patterns[0]), ": \"%s\"", serial_variations[var]);
        snprintf(search_patterns[pattern_count++], sizeof(search_patterns[0]), "\"%s\",", serial_variations[var]);
    }
    
    for (int i = 0; i < pattern_count && !pos; i++) {
        printf("game_launcher: Trying pattern %d: %s\n", i+1, search_patterns[i]);
        pos = strstr(json_data, search_patterns[i]);
        if (pos) {
            printf("game_launcher: Found exact serial match with pattern %d!\n", i+1);
            break;
        }
    }
    
    if (pos) {
        // Find the start of this game object - look for the serial key start
        char* obj_start = pos;
        while (obj_start > json_data && *obj_start != '"') {
            obj_start--;
        }
        // obj_start now points to opening quote of serial key
        
        // Find the end of this game's value object
        char* obj_end = strchr(pos, ':');
        int brace_count = 0;
        if (obj_end) {
            obj_end++; // Skip ':'
            while (*obj_end == ' ' || *obj_end == '\t') obj_end++; // Skip whitespace
            if (*obj_end == '{') {
                obj_end++; // Skip opening brace
                brace_count = 1;
                while (obj_end < json_data + file_size && brace_count > 0) {
                    if (*obj_end == '{') brace_count++;
                    if (*obj_end == '}') brace_count--;
                    if (brace_count == 0) break;
                    obj_end++;
                }
            }
        }
        
        if (brace_count == 0 && obj_end) {
            // Extract fields from JSON object
            char* title_pos = strstr(obj_start, "\"title\":");
            if (title_pos && title_pos < obj_end) {
                title_pos = strchr(title_pos + 8, '"');
                if (title_pos) {
                    title_pos++;
                    char* title_end = strchr(title_pos, '"');
                    if (title_end) {
                        int title_len = title_end - title_pos;
                        if (title_len < sizeof(game_info->title)) {
                            strncpy(game_info->title, title_pos, title_len);
                            game_info->title[title_len] = '\0';
                        }
                    }
                }
            }
            
            // Extract redump_name if available
            char* redump_pos = strstr(obj_start, "\"redump_name\":");
            if (redump_pos && redump_pos < obj_end) {
                redump_pos = strchr(redump_pos + 14, '"');
                if (redump_pos) {
                    redump_pos++;
                    char* redump_end = strchr(redump_pos, '"');
                    if (redump_end) {
                        int redump_len = redump_end - redump_pos;
                        if (redump_len < sizeof(game_info->redump_name)) {
                            strncpy(game_info->redump_name, redump_pos, redump_len);
                            game_info->redump_name[redump_len] = '\0';
                        }
                    }
                }
            }
            
            char* region_pos = strstr(obj_start, "\"region\":");
            if (region_pos && region_pos < obj_end) {
                region_pos = strchr(region_pos + 9, '"');
                if (region_pos) {
                    region_pos++;
                    char* region_end = strchr(region_pos, '"');
                    if (region_end) {
                        int region_len = region_end - region_pos;
                        if (region_len < sizeof(game_info->region)) {
                            strncpy(game_info->region, region_pos, region_len);
                            game_info->region[region_len] = '\0';
                        }
                    }
                }
            }
            
            strncpy(game_info->id, serial, sizeof(game_info->id) - 1);
            strncpy(game_info->system, system, sizeof(game_info->system) - 1);
            game_info->valid = true;
            
            free(json_data);
            return true;
        }
    }
    
    // If exact match failed, try fuzzy search on serial numbers
    printf("game_launcher: Exact serial match failed, trying fuzzy search for: %s\n", serial);
    printf("game_launcher: Parsing JSON for all games to find fuzzy matches\n");
    
    // Reset search results
    search_results_count = 0;
    
    // Parse JSON and extract all games with their serials for fuzzy matching
    int games_found = 0;
    char* current_pos = json_data;
    char search_pattern[128]; // Buffer for extracted serials
    while ((current_pos = strstr(current_pos, "\"serial\":")) != NULL) {
        games_found++;
        current_pos += 9; // Skip "serial":
        
        // Find opening quote
        char* serial_start = strchr(current_pos, '"');
        if (!serial_start) {
            printf("game_launcher: Warning - malformed serial field at game %d\n", games_found);
            continue;
        }
        serial_start++;
        
        // Find closing quote
        char* serial_end = strchr(serial_start, '"');
        if (!serial_end) {
            printf("game_launcher: Warning - unterminated serial string at game %d\n", games_found);
            continue;
        }
        
        // Extract serial
        int serial_len = serial_end - serial_start;
        if (serial_len >= sizeof(search_pattern)) {
            printf("game_launcher: Warning - serial too long (%d chars) at game %d\n", serial_len, games_found);
            continue;
        }
        
        strncpy(search_pattern, serial_start, serial_len);
        search_pattern[serial_len] = '\0';
        
        printf("game_launcher: Processing game %d with serial: '%s'\n", games_found, search_pattern);
        
        // Find the game object containing this serial
        char* obj_start = serial_start;
        while (obj_start > json_data && *obj_start != '{') {
            obj_start--;
        }
        
        // Find the end of this game object
        char* obj_end = serial_end;
        int brace_count = 1;
        while (obj_end < json_data + file_size && brace_count > 0) {
            obj_end++;
            if (*obj_end == '{') brace_count++;
            if (*obj_end == '}') brace_count--;
        }
        
        if (brace_count == 0) {
            // Extract title and region for this game
            char title[256] = "";
            char region[64] = "";
            
            char* title_pos = strstr(obj_start, "\"title\":");
            if (title_pos && title_pos < obj_end) {
                title_pos = strchr(title_pos + 8, '"');
                if (title_pos) {
                    title_pos++;
                    char* title_end = strchr(title_pos, '"');
                    if (title_end) {
                        int title_len = title_end - title_pos;
                        if (title_len < sizeof(title)) {
                            strncpy(title, title_pos, title_len);
                            title[title_len] = '\0';
                        }
                    }
                }
            }
            
            char* region_pos = strstr(obj_start, "\"region\":");
            if (region_pos && region_pos < obj_end) {
                region_pos = strchr(region_pos + 9, '"');
                if (region_pos) {
                    region_pos++;
                    char* region_end = strchr(region_pos, '"');
                    if (region_end) {
                        int region_len = region_end - region_pos;
                        if (region_len < sizeof(region)) {
                            strncpy(region, region_pos, region_len);
                            region[region_len] = '\0';
                        }
                    }
                }
            }
            
            // Add to search results for fuzzy matching - compare extracted serial against target serial
            printf("game_launcher: Found game: title='%s', region='%s', serial='%s'\n", title, region, search_pattern);
            
            // Create a custom result for serial fuzzy matching
            if (search_results_count < MAX_SEARCH_RESULTS) {
                search_result_t* result = &search_results[search_results_count];
                
                strncpy(result->path, search_pattern, sizeof(result->path) - 1);
                strncpy(result->title, title, sizeof(result->title) - 1);
                strncpy(result->region, region, sizeof(result->region) - 1);
                
                // Compare extracted serial with target serial
                result->fuzzy_score = calculate_fuzzy_score(search_pattern, serial);
                result->region_score = calculate_region_score(region);
                result->total_score = result->fuzzy_score;
                
                printf("game_launcher: Serial fuzzy match: '%s' vs '%s' -> score=%d\n", 
                       search_pattern, serial, result->fuzzy_score);
                
                search_results_count++;
            }
        }
        
        current_pos = serial_end + 1;
    }
    
    // Sort and get best match
    printf("game_launcher: Found %d total games in JSON, %d search results generated\n", games_found, search_results_count);
    sort_search_results();
    
    if (search_results_count > 0) {
        printf("game_launcher: Top 3 fuzzy matches:\n");
        for (int i = 0; i < search_results_count && i < 3; i++) {
            printf("game_launcher:   %d. '%s' (score: %d)\n", i+1, search_results[i].title, search_results[i].fuzzy_score);
        }
        
        // Use the best match if it's above threshold (85% match)
        if (search_results[0].fuzzy_score >= 85) {
            strncpy(game_info->title, search_results[0].title, sizeof(game_info->title) - 1);
            strncpy(game_info->id, search_results[0].path, sizeof(game_info->id) - 1);
            strncpy(game_info->system, system, sizeof(game_info->system) - 1);
            strncpy(game_info->region, search_results[0].region, sizeof(game_info->region) - 1);
            game_info->valid = true;
            
            printf("game_launcher: Fuzzy match found: %s (score: %d)\n", search_results[0].title, search_results[0].fuzzy_score);
            free(json_data);
            return true;
        }
    }
    
    free(json_data);
    return false;
}

// MGL parameter structure
struct MGLParams {
    const char* rbf;
    int delay;
    int index;
    const char* type;
};

// Get MGL parameters for system (based on https://github.com/wizzomafizzo/mrext/blob/main/docs/systems.md)
MGLParams get_mgl_params(const char* system) {
    // CD-based systems (type "s")
    if (strcmp(system, "PSX") == 0) return {"_Console/PSX", 1, 1, "s"};
    if (strcmp(system, "Saturn") == 0) return {"_Console/Saturn", 1, 1, "s"};
    if (strcmp(system, "MegaCD") == 0) return {"_Console/MegaCD", 1, 0, "s"};
    if (strcmp(system, "SegaCD") == 0) return {"_Console/MegaCD", 1, 0, "s"};
    if (strcmp(system, "PCECD") == 0) return {"_Console/TurboGrafx16", 1, 0, "s"};
    if (strcmp(system, "TurboGrafx16CD") == 0) return {"_Console/TurboGrafx16", 1, 0, "s"};
    if (strcmp(system, "NeoGeoCD") == 0) return {"_Console/NeoGeo", 1, 1, "s"};
    if (strcmp(system, "AmigaCD32") == 0) return {"_Computer/Minimig", 1, 1, "s"};
    if (strcmp(system, "Atari5200") == 0) return {"_Console/Atari5200", 1, 1, "s"};
    if (strcmp(system, "Amstrad") == 0) return {"_Computer/Amstrad", 1, 0, "s"};
    if (strcmp(system, "CPC") == 0) return {"_Computer/Amstrad", 1, 0, "s"};
    if (strcmp(system, "Atari800") == 0) return {"_Computer/Atari800", 1, 0, "s"};
    if (strcmp(system, "C64") == 0) return {"_Computer/C64", 1, 0, "s"};
    
    // Cartridge/ROM systems (type "f")
    if (strcmp(system, "Genesis") == 0) return {"_Console/Genesis", 1, 1, "f"};
    if (strcmp(system, "MegaDrive") == 0) return {"_Console/Genesis", 1, 1, "f"};
    if (strcmp(system, "SNES") == 0) return {"_Console/SNES", 2, 0, "f"};
    if (strcmp(system, "NES") == 0) return {"_Console/NES", 2, 1, "f"};
    if (strcmp(system, "Famicom") == 0) return {"_Console/NES", 2, 1, "f"};
    if (strcmp(system, "FamicomDiskSystem") == 0) return {"_Console/NES", 2, 1, "f"};
    if (strcmp(system, "SMS") == 0) return {"_Console/SMS", 1, 1, "f"};
    if (strcmp(system, "MasterSystem") == 0) return {"_Console/SMS", 1, 1, "f"};
    if (strcmp(system, "GG") == 0) return {"_Console/SMS", 1, 2, "f"};
    if (strcmp(system, "GameGear") == 0) return {"_Console/SMS", 1, 2, "f"};
    if (strcmp(system, "PCE") == 0) return {"_Console/TurboGrafx16", 1, 0, "f"};
    if (strcmp(system, "TG16") == 0) return {"_Console/TurboGrafx16", 1, 0, "f"};
    if (strcmp(system, "TurboGrafx16") == 0) return {"_Console/TurboGrafx16", 1, 0, "f"};
    if (strcmp(system, "Gameboy") == 0) return {"_Console/Gameboy", 2, 1, "f"};
    if (strcmp(system, "GameBoy") == 0) return {"_Console/Gameboy", 2, 1, "f"};
    if (strcmp(system, "GameBoyColor") == 0) return {"_Console/Gameboy", 2, 1, "f"};
    if (strcmp(system, "GBA") == 0) return {"_Console/GBA", 2, 1, "f"};
    if (strcmp(system, "GameBoyAdvance") == 0) return {"_Console/GBA", 2, 1, "f"};
    if (strcmp(system, "Atari2600") == 0) return {"_Console/Atari7800", 1, 1, "f"};
    if (strcmp(system, "Atari7800") == 0) return {"_Console/Atari7800", 1, 1, "f"};
    if (strcmp(system, "AtariLynx") == 0) return {"_Console/AtariLynx", 1, 1, "f"};
    if (strcmp(system, "Lynx") == 0) return {"_Console/AtariLynx", 1, 1, "f"};
    if (strcmp(system, "NeoGeo") == 0) return {"_Console/NeoGeo", 1, 1, "f"};
    if (strcmp(system, "S32X") == 0) return {"_Console/S32X", 1, 1, "f"};
    if (strcmp(system, "Sega32X") == 0) return {"_Console/S32X", 1, 1, "f"};
    if (strcmp(system, "Amiga") == 0) return {"_Computer/Minimig", 1, 0, "f"};
    if (strcmp(system, "AdventureVision") == 0) return {"_Console/AdventureVision", 1, 1, "f"};
    if (strcmp(system, "Arcade") == 0) return {"_Arcade", 1, 1, "f"};
    
    // Default for unknown systems
    return {"_Console/Unknown", 1, 1, "f"};
}

// Core-specific file extensions
const char* get_core_extensions(const char* system) {
    if (strcmp(system, "PSX") == 0) return ".cue,.chd,.iso";
    if (strcmp(system, "Saturn") == 0) return ".cue,.chd,.iso";
    if (strcmp(system, "MegaCD") == 0) return ".cue,.chd,.iso";
    if (strcmp(system, "PCECD") == 0) return ".cue,.chd,.iso";
    if (strcmp(system, "NeoGeoCD") == 0) return ".cue,.chd,.iso";
    if (strcmp(system, "Genesis") == 0) return ".md,.gen,.smd,.bin";
    if (strcmp(system, "MegaDrive") == 0) return ".md,.gen,.smd,.bin";
    if (strcmp(system, "SNES") == 0) return ".sfc,.smc";
    if (strcmp(system, "NES") == 0) return ".nes";
    if (strcmp(system, "SMS") == 0) return ".sms";
    if (strcmp(system, "GG") == 0) return ".gg";
    if (strcmp(system, "PCE") == 0) return ".pce,.sgx";
    if (strcmp(system, "TG16") == 0) return ".pce,.sgx";
    if (strcmp(system, "Gameboy") == 0) return ".gb,.gbc";
    if (strcmp(system, "GBA") == 0) return ".gba";
    if (strcmp(system, "Atari2600") == 0) return ".a26,.bin";
    if (strcmp(system, "Atari7800") == 0) return ".a78";
    if (strcmp(system, "C64") == 0) return ".prg,.d64,.t64";
    if (strcmp(system, "Amiga") == 0) return ".adf,.hdf,.hdz";
    if (strcmp(system, "Arcade") == 0) return ".mra";
    // Default for unknown systems - CD-like extensions
    return ".chd,.cue,.iso";
}

// Check if file extension is valid for core
bool is_valid_extension(const char* filename, const char* system) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return false;
    
    const char* valid_exts = get_core_extensions(system);
    char ext_lower[8];
    strncpy(ext_lower, ext, sizeof(ext_lower) - 1);
    ext_lower[sizeof(ext_lower) - 1] = '\0';
    
    // Convert to lowercase
    for (int i = 0; ext_lower[i]; i++) {
        ext_lower[i] = tolower(ext_lower[i]);
    }
    
    return strstr(valid_exts, ext_lower) != NULL;
}

// Load favorites from file
void load_favorites() {
    FILE* fp = fopen(FAVORITES_FILE, "r");
    if (!fp) return;
    
    char line[512];
    favorites_count = 0;
    
    while (fgets(line, sizeof(line), fp) && favorites_count < MAX_FAVORITES) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Format: core,id_type,identifier,title,play_count
        char* token = strtok(line, ",");
        if (token) {
            strncpy(favorites[favorites_count].core, token, 15);
            
            token = strtok(NULL, ",");
            if (token) strncpy(favorites[favorites_count].id_type, token, 15);
            
            token = strtok(NULL, ",");
            if (token) strncpy(favorites[favorites_count].identifier, token, 127);
            
            token = strtok(NULL, ",");
            if (token) strncpy(favorites[favorites_count].title, token, 127);
            
            token = strtok(NULL, "\n");
            if (token) favorites[favorites_count].play_count = atoi(token);
            
            favorites[favorites_count].added_time = time(NULL);
            favorites_count++;
        }
    }
    
    fclose(fp);
    printf("game_launcher: Loaded %d favorites\n", favorites_count);
}

// Save favorites to file
void save_favorites() {
    if (!config.enable_favorites) return;
    
    FILE* fp = fopen(FAVORITES_FILE, "w");
    if (!fp) return;
    
    fprintf(fp, "# MiSTer Game Launcher Favorites\n");
    fprintf(fp, "# Format: core,id_type,identifier,title,play_count\n\n");
    
    for (int i = 0; i < favorites_count; i++) {
        fprintf(fp, "%s,%s,%s,%s,%d\n",
                favorites[i].core,
                favorites[i].id_type,
                favorites[i].identifier,
                favorites[i].title,
                favorites[i].play_count);
    }
    
    fclose(fp);
}

// Add game to favorites
bool add_favorite(const char* core, const char* id_type, const char* identifier, const char* title) {
    if (!config.enable_favorites || favorites_count >= MAX_FAVORITES) return false;
    
    // Check if already in favorites
    for (int i = 0; i < favorites_count; i++) {
        if (strcmp(favorites[i].core, core) == 0 &&
            strcmp(favorites[i].identifier, identifier) == 0) {
            return false; // Already exists
        }
    }
    
    // Add new favorite
    strncpy(favorites[favorites_count].core, core, 15);
    strncpy(favorites[favorites_count].id_type, id_type, 15);
    strncpy(favorites[favorites_count].identifier, identifier, 127);
    strncpy(favorites[favorites_count].title, title, 127);
    favorites[favorites_count].added_time = time(NULL);
    favorites[favorites_count].play_count = 0;
    favorites_count++;
    
    save_favorites();
    return true;
}

// Remove game from favorites
bool remove_favorite(const char* core, const char* identifier) {
    if (!config.enable_favorites) return false;
    
    for (int i = 0; i < favorites_count; i++) {
        if (strcmp(favorites[i].core, core) == 0 &&
            strcmp(favorites[i].identifier, identifier) == 0) {
            // Shift remaining entries
            for (int j = i; j < favorites_count - 1; j++) {
                favorites[j] = favorites[j + 1];
            }
            favorites_count--;
            save_favorites();
            return true;
        }
    }
    return false;
}

// Add to game history
void add_to_history(const char* core, const char* identifier, const char* title, const char* source) {
    if (!config.enable_history) return;
    
    // Shift history if at capacity
    if (history_count >= config.max_history_entries) {
        for (int i = 0; i < config.max_history_entries - 1; i++) {
            history[i] = history[i + 1];
        }
        history_count = config.max_history_entries - 1;
    }
    
    // Add new entry
    strncpy(history[history_count].core, core, 15);
    strncpy(history[history_count].identifier, identifier, 127);
    strncpy(history[history_count].title, title, 127);
    strncpy(history[history_count].source, source, 31);
    history[history_count].play_time = time(NULL);
    history_count++;
    
    // Update play count in favorites
    for (int i = 0; i < favorites_count; i++) {
        if (strcmp(favorites[i].core, core) == 0 &&
            strcmp(favorites[i].identifier, identifier) == 0) {
            favorites[i].play_count++;
            save_favorites();
            break;
        }
    }
}

// Get random game from favorites
bool get_random_favorite(char* core, char* id_type, char* identifier) {
    if (favorites_count == 0) return false;
    
    srand(time(NULL));
    int index = rand() % favorites_count;
    
    strcpy(core, favorites[index].core);
    strcpy(id_type, favorites[index].id_type);
    strcpy(identifier, favorites[index].identifier);
    
    return true;
}

// Get random game from GameID database
bool get_random_game_from_core(const char* core, char* id_type, char* identifier, char* title) {
    char db_path[512];
    const char* db_system = (strcmp(core, "MegaCD") == 0) ? "SegaCD" : core;
    snprintf(db_path, sizeof(db_path), "%s/%s.data.json", config.gameid_dir, db_system);
    
    FILE* fp = fopen(db_path, "r");
    if (!fp) return false;
    
    // Quick and dirty: count entries first
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);
    
    char* json_data = (char*)malloc(file_size + 1);
    fread(json_data, 1, file_size, fp);
    json_data[file_size] = '\0';
    fclose(fp);
    
    // Count game entries
    int game_count = 0;
    char* pos = json_data;
    while ((pos = strstr(pos, "\"id\":")) != NULL) {
        game_count++;
        pos += 5;
    }
    
    if (game_count == 0) {
        free(json_data);
        return false;
    }
    
    // Pick random game
    srand(time(NULL));
    int target_index = rand() % game_count;
    int current_index = 0;
    
    pos = json_data;
    while ((pos = strstr(pos, "\"id\":")) != NULL) {
        if (current_index == target_index) {
            // Extract this game's info
            char* id_start = strchr(pos + 5, '"');
            if (id_start) {
                id_start++;
                char* id_end = strchr(id_start, '"');
                if (id_end) {
                    int id_len = id_end - id_start;
                    if (id_len < 128) {
                        strncpy(identifier, id_start, id_len);
                        identifier[id_len] = '\0';
                        strcpy(id_type, "serial");
                        
                        // Try to get title too
                        char* title_pos = strstr(pos, "\"title\":");
                        if (title_pos && title_pos < pos + 500) {
                            char* title_start = strchr(title_pos + 8, '"');
                            if (title_start) {
                                title_start++;
                                char* title_end = strchr(title_start, '"');
                                if (title_end) {
                                    int title_len = title_end - title_start;
                                    if (title_len < 128) {
                                        strncpy(title, title_start, title_len);
                                        title[title_len] = '\0';
                                    }
                                }
                            }
                        }
                        
                        free(json_data);
                        return true;
                    }
                }
            }
        }
        current_index++;
        pos += 5;
    }
    
    free(json_data);
    return false;
}

// Get console generation from core name
console_generation_t get_console_generation(const char* core) {
    if (strcmp(core, "Atari2600") == 0 || strcmp(core, "Atari7800") == 0) return GEN_2ND;
    if (strcmp(core, "NES") == 0 || strcmp(core, "SMS") == 0) return GEN_3RD;
    if (strcmp(core, "SNES") == 0 || strcmp(core, "Genesis") == 0 || strcmp(core, "MegaDrive") == 0 || 
        strcmp(core, "PCE") == 0 || strcmp(core, "TG16") == 0) return GEN_4TH;
    if (strcmp(core, "PSX") == 0 || strcmp(core, "Saturn") == 0 || strcmp(core, "N64") == 0) return GEN_5TH;
    if (strcmp(core, "Dreamcast") == 0) return GEN_6TH;
    return GEN_UNKNOWN;
}

// Rate a game (1-5 stars)
bool rate_game(const char* core, const char* identifier, const char* title, int rating, const char* review) {
    if (!config.enable_ratings || rating < 1 || rating > 5) return false;
    
    // Find existing rating or create new one
    game_rating_t* target_rating = NULL;
    for (int i = 0; i < ratings_count; i++) {
        if (strcmp(ratings[i].core, core) == 0 && strcmp(ratings[i].identifier, identifier) == 0) {
            target_rating = &ratings[i];
            break;
        }
    }
    
    if (!target_rating) {
        // Allocate more space if needed
        ratings = (game_rating_t*)realloc(ratings, (ratings_count + 1) * sizeof(game_rating_t));
        if (!ratings) return false;
        target_rating = &ratings[ratings_count];
        ratings_count++;
        
        strcpy(target_rating->core, core);
        strcpy(target_rating->identifier, identifier);
        strcpy(target_rating->title, title);
    }
    
    target_rating->rating = rating;
    if (review) strncpy(target_rating->review, review, sizeof(target_rating->review) - 1);
    target_rating->rated_time = time(NULL);
    
    // Save to file
    FILE* fp = fopen(RATINGS_FILE, "w");
    if (fp) {
        fprintf(fp, "# Game Ratings\\n# Format: core,identifier,title,rating,review,timestamp\\n\\n");
        for (int i = 0; i < ratings_count; i++) {
            fprintf(fp, "%s,%s,%s,%d,\"%s\",%ld\\n",
                    ratings[i].core, ratings[i].identifier, ratings[i].title,
                    ratings[i].rating, ratings[i].review, ratings[i].rated_time);
        }
        fclose(fp);
    }
    
    return true;
}

// Get random game by generation
bool get_random_game_by_generation(console_generation_t generation, char* core, char* id_type, char* identifier, char* title) {
    // Collect all cores from this generation
    const char* gen_cores[20];
    int gen_core_count = 0;
    
    const char* all_cores[] = {"PSX", "Saturn", "N64", "SNES", "Genesis", "NES", "SMS", "PCE", "TG16", 
                               "Atari2600", "Atari7800", "Dreamcast", "Gameboy", "GBA", NULL};
    
    for (int i = 0; all_cores[i] && gen_core_count < 20; i++) {
        if (get_console_generation(all_cores[i]) == generation) {
            gen_cores[gen_core_count++] = all_cores[i];
        }
    }
    
    if (gen_core_count == 0) return false;
    
    // Select random core from generation
    srand(time(NULL));
    const char* selected_core = gen_cores[rand() % gen_core_count];
    
    return get_random_game_from_core(selected_core, id_type, identifier, title);
}

// Get games by rating
int get_games_by_rating(int min_rating, game_rating_t* result_ratings, int max_results) {
    int count = 0;
    for (int i = 0; i < ratings_count && count < max_results; i++) {
        if (ratings[i].rating >= min_rating) {
            result_ratings[count] = ratings[i];
            count++;
        }
    }
    return count;
}

// Recursive directory search
void search_directory_recursive(const char* dir_path, const char* system, const char* title) {
    DIR* dir = opendir(dir_path);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && search_results_count < MAX_SEARCH_RESULTS) {
        if (entry->d_name[0] == '.') continue; // Skip hidden files
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        if (entry->d_type == DT_DIR) {
            // Recursively search subdirectories
            search_directory_recursive(full_path, system, title);
        } else if (entry->d_type == DT_REG) {
            // Check if file has valid extension for this core
            if (!is_valid_extension(entry->d_name, system)) {
                continue;
            }
            
            // Check if filename matches title
            char filename_lower[256];
            strncpy(filename_lower, entry->d_name, sizeof(filename_lower) - 1);
            filename_lower[sizeof(filename_lower) - 1] = '\0';
            for (int i = 0; filename_lower[i]; i++) {
                filename_lower[i] = tolower(filename_lower[i]);
            }
            
            char title_lower[256];
            strncpy(title_lower, title, sizeof(title_lower) - 1);
            title_lower[sizeof(title_lower) - 1] = '\0';
            for (int i = 0; title_lower[i]; i++) {
                title_lower[i] = tolower(title_lower[i]);
            }
            
            // Try exact substring match first
            if (strstr(filename_lower, title_lower) != NULL) {
                if (add_search_result(full_path, entry->d_name, "Unknown", title)) {
                    closedir(dir);
                    return; // 100% match found, exit immediately
                }
            } else {
                // Try partial matching by removing common suffixes
                char title_clean[256];
                strcpy(title_clean, title_lower);
                
                // Remove common endings that might differ between JSON and filename
                char* endings[] = {" (re)", " (usa)", " (europe)", " (japan)", " - special edition", NULL};
                for (int i = 0; endings[i]; i++) {
                    char* pos = strstr(title_clean, endings[i]);
                    if (pos) *pos = '\0';
                }
                
                // Try matching with cleaned title
                if (strlen(title_clean) > 5 && strstr(filename_lower, title_clean) != NULL) {
                    if (add_search_result(full_path, entry->d_name, "Unknown", title)) {
                        closedir(dir);
                        return; // 100% match found, exit immediately
                    }
                }
            }
        }
    }
    
    closedir(dir);
}

// Search for game files
bool search_game_files(const char* system, const char* title) {
    search_results_count = 0;
    
    // Map database system names to MiSTer core directory names
    const char* core_system = system;
    if (strcmp(system, "SegaCD") == 0) {
        core_system = "MegaCD";
    }
    
    char search_dir[512];
    snprintf(search_dir, sizeof(search_dir), "%s/%s", config.games_dir, core_system);
    
    printf("game_launcher: Searching for '%s' in %s (extensions: %s)\n", 
           title, search_dir, get_core_extensions(system));
    
    // Check if directory exists
    if (access(search_dir, F_OK) != 0) {
        printf("game_launcher: Directory not found: %s\n", search_dir);
        return false;
    }
    
    // Search recursively with extension filtering
    search_directory_recursive(search_dir, system, title);
    
    sort_search_results();
    
    printf("game_launcher: Found %d matches\n", search_results_count);
    return search_results_count > 0;
}

// Get relative path for MGL
const char* get_relative_path_for_mgl(const char* full_path, const char* system) {
    static char relative_path[512];
    
    if (strcmp(system, "MegaCD") == 0) {
        char games_prefix[512];
        snprintf(games_prefix, sizeof(games_prefix), "%s/MegaCD/", config.games_dir);
        if (strncmp(full_path, games_prefix, strlen(games_prefix)) == 0) {
            strncpy(relative_path, full_path + strlen(games_prefix), sizeof(relative_path) - 1);
            relative_path[sizeof(relative_path) - 1] = '\0';
            return relative_path;
        }
    }
    
    return full_path;
}

// Get filename without extension
const char* get_filename_without_ext(const char* full_path) {
    static char filename[256];
    
    const char* last_slash = strrchr(full_path, '/');
    const char* basename = last_slash ? last_slash + 1 : full_path;
    
    strncpy(filename, basename, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    
    char* last_dot = strrchr(filename, '.');
    if (last_dot) {
        *last_dot = '\0';
    }
    
    return filename;
}

// Create single MGL file
bool create_game_mgl(const char* system, const char* title) {
    if (search_results_count == 0) {
        printf("game_launcher: No search results for %s\n", title);
        return false;
    }
    
    search_result_t* best_match = &search_results[0];
    const char* filename = get_filename_without_ext(best_match->path);
    const char* mgl_path_for_file = get_relative_path_for_mgl(best_match->path, system);
    
    char mgl_path[512];
    snprintf(mgl_path, sizeof(mgl_path), "/media/fat/%s.mgl", filename);
    
    FILE* fp = fopen(mgl_path, "w");
    if (!fp) {
        printf("game_launcher: Failed to create MGL: %s\n", mgl_path);
        return false;
    }
    
    MGLParams params = get_mgl_params(system);
    fprintf(fp, "<mistergamedescription>\n");
    fprintf(fp, "  <rbf>%s</rbf>\n", params.rbf);
    fprintf(fp, "  <file delay=\"%d\" type=\"%s\" index=\"%d\" path=\"%s\"/>\n", 
            params.delay, params.type, params.index, mgl_path_for_file);
    fprintf(fp, "</mistergamedescription>\n");
    
    fclose(fp);
    
    printf("game_launcher: Created MGL: %s -> %s (score: %d)\n", 
           mgl_path, mgl_path_for_file, best_match->total_score);
    return true;
}

// Create selection MGLs
void create_selection_mgls(const char* system, const char* title) {
    // Check for 100% match - auto-boot immediately
    if (search_results_count > 0 && search_results[0].fuzzy_score == 100) {
        printf("game_launcher: 100%% match found - auto-booting %s\n", search_results[0].title);
        create_game_mgl(system, search_results[0].title);
        send_osd_message("Auto-loading exact match!");
        send_mister_command("load_core /media/fat/game.mgl");
        return;
    }
    
    printf("game_launcher: Creating selection MGLs for %s\n", title);
    
    for (int i = 0; i < search_results_count && i < 9; i++) {
        const char* filename = get_filename_without_ext(search_results[i].path);
        const char* mgl_path_for_file = get_relative_path_for_mgl(search_results[i].path, system);
        
        char mgl_path[512];
        snprintf(mgl_path, sizeof(mgl_path), "/media/fat/%d-%s.mgl", i + 1, filename);
        
        FILE* fp = fopen(mgl_path, "w");
        if (fp) {
            MGLParams params = get_mgl_params(system);
            fprintf(fp, "<mistergamedescription>\n");
            fprintf(fp, "  <rbf>%s</rbf>\n", params.rbf);
            fprintf(fp, "  <file delay=\"%d\" type=\"%s\" index=\"%d\" path=\"%s\"/>\n", 
                    params.delay, params.type, params.index, mgl_path_for_file);
            fprintf(fp, "</mistergamedescription>\n");
            fclose(fp);
            
            printf("game_launcher: Created selection MGL: %s -> %s (score: %d)\n", 
                   mgl_path, mgl_path_for_file, search_results[i].total_score);
        }
    }
}

// Clean up MGL files
void cleanup_mgls() {
    system("rm -f /media/fat/[0-9]-*.mgl 2>/dev/null");
    system("find /media/fat -maxdepth 1 -name '*.mgl' ! -name '*_*.mgl' -delete 2>/dev/null");
}

// Handle special commands
void handle_special_command(const char* command, const char* param, const char* source) {
    char result_core[16] = {0};
    char result_id_type[16] = {0};
    char result_identifier[128] = {0};
    char result_title[128] = {0};
    
    if (strcmp(command, "cleanup_mgls") == 0) {
        printf("game_launcher: Cleaning up MGL files\n");
        fflush(stdout);
        system("rm -f /media/fat/[0-9]-*.mgl 2>/dev/null");
        system("find /media/fat -maxdepth 1 -name '*.mgl' ! -name '*_*.mgl' -delete 2>/dev/null");
        send_osd_message("MGL files cleaned up");
        // Refresh OSD by toggling it off and on quickly
        send_mister_command("key F12");
        usleep(50000);  // Wait 50ms
        send_mister_command("key F12");
    } else if (strcmp(command, "random_favorite") == 0) {
        if (get_random_favorite(result_core, result_id_type, result_identifier)) {
            send_osd_message("Launching random favorite...");
            process_game_request(result_core, result_id_type, result_identifier, "random_favorite");
        } else {
            send_osd_message("No favorites found");
        }
    }
    else if (strcmp(command, "random_game") == 0) {
        // param should be the core name
        if (strlen(param) > 0) {
            if (get_random_game_from_core(param, result_id_type, result_identifier, result_title)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Random %s: %s", param, result_title);
                send_osd_message(msg);
                process_game_request(param, result_id_type, result_identifier, "random_game");
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "No %s games found", param);
                send_osd_message(msg);
            }
        }
    }
    else if (strcmp(command, "add_favorite") == 0) {
        // param format: core,id_type,identifier,title
        char* core = strtok((char*)param, ",");
        char* id_type = strtok(NULL, ",");
        char* identifier = strtok(NULL, ",");
        char* title = strtok(NULL, ",");
        
        if (core && id_type && identifier && title) {
            if (add_favorite(core, id_type, identifier, title)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Added to favorites: %s", title);
                send_osd_message(msg);
            } else {
                send_osd_message("Already in favorites or list full");
            }
        }
    }
    else if (strcmp(command, "remove_favorite") == 0) {
        // param format: core,identifier
        char* core = strtok((char*)param, ",");
        char* identifier = strtok(NULL, ",");
        
        if (core && identifier) {
            if (remove_favorite(core, identifier)) {
                send_osd_message("Removed from favorites");
            } else {
                send_osd_message("Not found in favorites");
            }
        }
    }
    else if (strcmp(command, "last_played") == 0) {
        if (history_count > 0) {
            history_entry_t* last = &history[history_count - 1];
            char msg[256];
            snprintf(msg, sizeof(msg), "Last played: %s", last->title);
            send_osd_message(msg);
            process_game_request(last->core, "title", last->identifier, "last_played");
        } else {
            send_osd_message("No game history found");
        }
    }
    else if (strcmp(command, "list_favorites") == 0) {
        if (favorites_count > 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Favorites: %d games", favorites_count);
            send_osd_message(msg);
            
            // Could implement showing favorites list in OSD here
        } else {
            send_osd_message("No favorites found");
        }
    }
    else if (strcmp(command, "rate_game") == 0) {
        // param format: core,identifier,title,rating,review
        char* core = strtok((char*)param, ",");
        char* identifier = strtok(NULL, ",");
        char* title = strtok(NULL, ",");
        char* rating_str = strtok(NULL, ",");
        char* review = strtok(NULL, ",");
        
        if (core && identifier && title && rating_str) {
            int rating = atoi(rating_str);
            if (rate_game(core, identifier, title, rating, review ? review : "")) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Rated %s: %d stars", title, rating);
                send_osd_message(msg);
            } else {
                send_osd_message("Failed to save rating");
            }
        }
    }
    else if (strcmp(command, "random_generation") == 0) {
        // param should be generation number (3, 4, 5, etc.)
        if (strlen(param) > 0) {
            console_generation_t generation = (console_generation_t)atoi(param);
            if (get_random_game_by_generation(generation, result_core, result_id_type, result_identifier, result_title)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Random Gen %d: %s", (int)generation, result_title);
                send_osd_message(msg);
                process_game_request(result_core, result_id_type, result_identifier, "random_generation");
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "No Gen %d games found", (int)generation);
                send_osd_message(msg);
            }
        }
    }
    else if (strcmp(command, "random_rated") == 0) {
        // param should be minimum rating (4, 5)
        if (strlen(param) > 0) {
            int min_rating = atoi(param);
            game_rating_t top_games[50];
            int count = get_games_by_rating(min_rating, top_games, 50);
            
            if (count > 0) {
                srand(time(NULL));
                game_rating_t* selected = &top_games[rand() % count];
                char msg[256];
                snprintf(msg, sizeof(msg), "Top Rated: %s (%d stars)", selected->title, selected->rating);
                send_osd_message(msg);
                process_game_request(selected->core, "serial", selected->identifier, "random_rated");
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "No %d+ star games found", min_rating);
                send_osd_message(msg);
            }
        }
    }
    else if (strcmp(command, "playtime") == 0) {
        // param format: core,identifier or empty for current session
        if (strlen(param) > 0) {
            char* core = strtok((char*)param, ",");
            char* identifier = strtok(NULL, ",");
            
            if (core && identifier) {
                int minutes = get_game_playtime(core, identifier);
                char msg[256];
                if (minutes > 0) {
                    int hours = minutes / 60;
                    int mins = minutes % 60;
                    snprintf(msg, sizeof(msg), "Playtime: %dh %dm", hours, mins);
                } else {
                    snprintf(msg, sizeof(msg), "No playtime recorded");
                }
                send_osd_message(msg);
            }
        } else if (current_session_start > 0) {
            time_t now = time(NULL);
            int session_minutes = (now - current_session_start) / 60;
            char msg[128];
            snprintf(msg, sizeof(msg), "Current session: %d minutes", session_minutes);
            send_osd_message(msg);
        } else {
            send_osd_message("No active session");
        }
    }
    else if (strcmp(command, "create_collection") == 0) {
        // param format: collection_name,description
        char* name = strtok((char*)param, ",");
        char* description = strtok(NULL, ",");
        
        if (name && description) {
            if (create_collection(name, description)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Created collection: %s", name);
                send_osd_message(msg);
            } else {
                send_osd_message("Failed to create collection");
            }
        }
    }
    else if (strcmp(command, "add_to_collection") == 0) {
        // param format: collection_name,core,id_type,identifier,title
        char* collection_name = strtok((char*)param, ",");
        char* core = strtok(NULL, ",");
        char* id_type = strtok(NULL, ",");
        char* identifier = strtok(NULL, ",");
        char* title = strtok(NULL, ",");
        
        if (collection_name && core && id_type && identifier && title) {
            if (add_game_to_collection(collection_name, core, id_type, identifier, title)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Added to %s: %s", collection_name, title);
                send_osd_message(msg);
            } else {
                send_osd_message("Failed to add to collection");
            }
        }
    }
    else if (strcmp(command, "random_collection") == 0) {
        // param should be collection name
        if (strlen(param) > 0) {
            char result_core[32], result_id_type[16], result_identifier[128], result_title[128];
            
            if (get_random_from_collection(param, result_core, result_id_type, result_identifier, result_title)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Random from %s: %s", param, result_title);
                send_osd_message(msg);
                process_game_request(result_core, result_id_type, result_identifier, "random_collection");
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "Collection '%s' not found or empty", param);
                send_osd_message(msg);
            }
        }
    }
    else if (strcmp(command, "list_collections") == 0) {
        if (collections_count > 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Collections: %d available", collections_count);
            send_osd_message(msg);
            
            // Could implement showing collections list in OSD here
        } else {
            send_osd_message("No collections found");
        }
    }
    else if (strcmp(command, "recommend_game") == 0) {
        char result_core[32], result_id_type[16], result_identifier[128], result_title[128];
        
        if (get_random_recommendation(result_core, result_id_type, result_identifier, result_title)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Recommended: %s", result_title);
            send_osd_message(msg);
            process_game_request(result_core, result_id_type, result_identifier, "recommendation");
        } else {
            send_osd_message("No recommendations available");
        }
    }
    else if (strcmp(command, "show_recommendations") == 0) {
        game_recommendation_t recommendations[10];
        int count = get_game_recommendations(recommendations, 10);
        
        if (count > 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Top recommendations: %d games", count);
            send_osd_message(msg);
            
            // Could display top 3 recommendations in OSD
            for (int i = 0; i < 3 && i < count; i++) {
                char rec_msg[256];
                snprintf(rec_msg, sizeof(rec_msg), "%d. %s (%.1f)", 
                        i + 1, recommendations[i].title, recommendations[i].recommendation_score);
                send_osd_message(rec_msg);
                sleep(2); // Brief delay between messages
            }
        } else {
            send_osd_message("No recommendations available");
        }
    }
    else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Unknown command: %s", command);
        send_osd_message(msg);
    }
}

// Process game launch request
bool process_game_request(const char* system, const char* id_type, const char* identifier, const char* source) {
    printf("game_launcher: Processing request - System: %s, ID Type: %s, Identifier: %s, Source: %s\n",
           system, id_type, identifier, source);
    
    // Stop any existing session before starting new one
    stop_game_session();
    
    game_info_t game_info = {0};
    
    // Look up game in GameID
    if (strcmp(id_type, "serial") == 0) {
        if (!search_gameid_by_serial(system, identifier, &game_info)) {
            send_osd_message("Game not found in GameID");
            return false;
        }
    } else if (strcmp(id_type, "title") == 0) {
        strncpy(game_info.title, identifier, sizeof(game_info.title) - 1);
        strncpy(game_info.system, system, sizeof(game_info.system) - 1);
        game_info.valid = true;
    } else {
        send_osd_message("Unsupported ID type");
        return false;
    }
    
    if (!game_info.valid) {
        send_osd_message("Invalid game information");
        return false;
    }
    
    char found_msg[256];
    snprintf(found_msg, sizeof(found_msg), "Found: %s", game_info.title);
    send_osd_message(found_msg);
    
    // Search for game files - use redump_name for more accurate matching if available
    const char* search_name = (game_info.redump_name[0] != '\0') ? game_info.redump_name : game_info.title;
    if (search_game_files(system, search_name)) {
        cleanup_mgls();
        
        if (search_results_count == 1 || search_results[0].total_score > 95) {
            // Single match or high confidence - auto-load
            create_game_mgl(system, game_info.title);
            send_osd_message("Game loaded - Ready to play!");
            // Refresh OSD by toggling it off and on quickly
            send_mister_command("key F12");
            usleep(50000);  // Wait 50ms
            send_mister_command("key F12");
        } else {
            // Multiple matches - show selection
            create_selection_mgls(system, game_info.title);
            send_osd_message("Multiple matches found - Select game");
            // Refresh OSD by toggling it off and on quickly
            send_mister_command("key F12");
            usleep(50000);  // Wait 50ms
            send_mister_command("key F12");
        }
        
        // Add to history
        add_to_history(system, identifier, game_info.title, source);
        
        // Start playtime tracking session
        start_game_session(system, identifier);
        
        return true;
    } else {
        send_osd_message("Game not found in library");
        return false;
    }
}

// Command processing thread
// Playtime tracking implementation

void init_playtime_tracking() {
    current_playing_core[0] = '\0';
    current_playing_identifier[0] = '\0';
    current_session_start = 0;
}

void start_game_session(const char* core, const char* identifier) {
    if (strlen(core) > 0 && strlen(identifier) > 0) {
        strncpy(current_playing_core, core, sizeof(current_playing_core) - 1);
        strncpy(current_playing_identifier, identifier, sizeof(current_playing_identifier) - 1);
        current_session_start = time(NULL);
        
        printf("game_launcher: Started session - %s:%s\n", core, identifier);
    }
}

void stop_game_session() {
    if (current_session_start > 0 && strlen(current_playing_core) > 0) {
        time_t session_end = time(NULL);
        int session_minutes = (session_end - current_session_start) / 60;
        
        if (session_minutes > 0) {
            // Load existing playtime
            char playtime_file[512];
            snprintf(playtime_file, sizeof(playtime_file), 
                    "/media/fat/utils/playtime/%s_%s.txt", 
                    current_playing_core, current_playing_identifier);
            
            int total_minutes = session_minutes;
            FILE* fp = fopen(playtime_file, "r");
            if (fp) {
                fscanf(fp, "%d", &total_minutes);
                fclose(fp);
                total_minutes += session_minutes;
            }
            
            // Create directory if needed
            system("mkdir -p /media/fat/utils/playtime");
            
            // Save updated playtime
            fp = fopen(playtime_file, "w");
            if (fp) {
                fprintf(fp, "%d\n", total_minutes);
                fclose(fp);
                
                printf("game_launcher: Session ended - %s:%s (%d min, total: %d min)\n", 
                       current_playing_core, current_playing_identifier, session_minutes, total_minutes);
            }
        }
        
        current_playing_core[0] = '\0';
        current_playing_identifier[0] = '\0';
        current_session_start = 0;
    }
}

void update_playtime_tracking() {
    // Check if current game is still running by monitoring MGL files
    static time_t last_mgl_check = 0;
    time_t now = time(NULL);
    
    if (now - last_mgl_check >= 10) { // Check every 10 seconds
        last_mgl_check = now;
        
        if (current_session_start > 0) {
            // Check if any MGL files exist (simple check for active game)
            DIR* dir = opendir("/media/fat");
            if (dir) {
                struct dirent* entry;
                bool mgl_found = false;
                
                while ((entry = readdir(dir)) != NULL) {
                    if (strstr(entry->d_name, ".mgl")) {
                        mgl_found = true;
                        break;
                    }
                }
                closedir(dir);
                
                // If no MGL files, assume game stopped
                if (!mgl_found && (now - current_session_start) > 30) {
                    stop_game_session();
                }
            }
        }
    }
}

void stop_playtime_tracking() {
    stop_game_session();
}

int get_game_playtime(const char* core, const char* identifier) {
    char playtime_file[512];
    snprintf(playtime_file, sizeof(playtime_file), 
            "/media/fat/utils/playtime/%s_%s.txt", core, identifier);
    
    FILE* fp = fopen(playtime_file, "r");
    if (fp) {
        int minutes = 0;
        fscanf(fp, "%d", &minutes);
        fclose(fp);
        return minutes;
    }
    
    return 0;
}

// Game collections management
static game_collection_t collections[MAX_COLLECTIONS];

void load_collections() {
    FILE* fp = fopen("/media/fat/utils/collections.txt", "r");
    if (!fp) return;
    
    collections_count = 0;
    char line[512];
    
    while (fgets(line, sizeof(line), fp) && collections_count < MAX_COLLECTIONS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // Format: collection_name|description|core:id_type:identifier:title|core:id_type:identifier:title...
        char* pipe1 = strchr(line, '|');
        if (!pipe1) continue;
        *pipe1 = '\0';
        
        char* pipe2 = strchr(pipe1 + 1, '|');
        if (!pipe2) continue;
        *pipe2 = '\0';
        
        // Remove newline
        char* newline = strchr(pipe2 + 1, '\n');
        if (newline) *newline = '\0';
        
        game_collection_t* collection = &collections[collections_count];
        strncpy(collection->name, line, sizeof(collection->name) - 1);
        strncpy(collection->description, pipe1 + 1, sizeof(collection->description) - 1);
        collection->game_count = 0;
        
        // Parse games
        char* games_str = pipe2 + 1;
        char* game_token = strtok(games_str, "|");
        
        while (game_token && collection->game_count < MAX_FAVORITES) {
            char* colon1 = strchr(game_token, ':');
            if (colon1) {
                *colon1 = '\0';
                char* colon2 = strchr(colon1 + 1, ':');
                if (colon2) {
                    *colon2 = '\0';
                    char* colon3 = strchr(colon2 + 1, ':');
                    if (colon3) {
                        *colon3 = '\0';
                        
                        favorite_game_t* game = &collection->games[collection->game_count];
                        strncpy(game->core, game_token, sizeof(game->core) - 1);
                        strncpy(game->id_type, colon1 + 1, sizeof(game->id_type) - 1);
                        strncpy(game->identifier, colon2 + 1, sizeof(game->identifier) - 1);
                        strncpy(game->title, colon3 + 1, sizeof(game->title) - 1);
                        collection->game_count++;
                    }
                }
            }
            game_token = strtok(NULL, "|");
        }
        
        collections_count++;
    }
    
    fclose(fp);
    printf("game_launcher: Loaded %d collections\n", collections_count);
}

void save_collections() {
    system("mkdir -p /media/fat/utils");
    
    FILE* fp = fopen("/media/fat/utils/collections.txt", "w");
    if (!fp) return;
    
    fprintf(fp, "# Game Collections\n");
    fprintf(fp, "# Format: collection_name|description|core:id_type:identifier:title|...\n");
    
    for (int i = 0; i < collections_count; i++) {
        game_collection_t* collection = &collections[i];
        fprintf(fp, "%s|%s", collection->name, collection->description);
        
        for (int j = 0; j < collection->game_count; j++) {
            favorite_game_t* game = &collection->games[j];
            fprintf(fp, "|%s:%s:%s:%s", game->core, game->id_type, game->identifier, game->title);
        }
        fprintf(fp, "\n");
    }
    
    fclose(fp);
}

bool create_collection(const char* name, const char* description) {
    if (collections_count >= MAX_COLLECTIONS) return false;
    
    // Check if collection already exists
    for (int i = 0; i < collections_count; i++) {
        if (strcmp(collections[i].name, name) == 0) {
            return false; // Already exists
        }
    }
    
    game_collection_t* collection = &collections[collections_count];
    strncpy(collection->name, name, sizeof(collection->name) - 1);
    strncpy(collection->description, description, sizeof(collection->description) - 1);
    collection->game_count = 0;
    collections_count++;
    
    save_collections();
    return true;
}

bool add_game_to_collection(const char* collection_name, const char* core, const char* id_type, 
                           const char* identifier, const char* title) {
    // Find collection
    game_collection_t* collection = NULL;
    for (int i = 0; i < collections_count; i++) {
        if (strcmp(collections[i].name, collection_name) == 0) {
            collection = &collections[i];
            break;
        }
    }
    
    if (!collection || collection->game_count >= MAX_FAVORITES) return false;
    
    // Check if game already in collection
    for (int i = 0; i < collection->game_count; i++) {
        if (strcmp(collection->games[i].core, core) == 0 &&
            strcmp(collection->games[i].identifier, identifier) == 0) {
            return false; // Already exists
        }
    }
    
    // Add game
    favorite_game_t* game = &collection->games[collection->game_count];
    strncpy(game->core, core, sizeof(game->core) - 1);
    strncpy(game->id_type, id_type, sizeof(game->id_type) - 1);
    strncpy(game->identifier, identifier, sizeof(game->identifier) - 1);
    strncpy(game->title, title, sizeof(game->title) - 1);
    collection->game_count++;
    
    save_collections();
    return true;
}

bool get_random_from_collection(const char* collection_name, char* core, char* id_type, char* identifier, char* title) {
    // Find collection
    for (int i = 0; i < collections_count; i++) {
        if (strcmp(collections[i].name, collection_name) == 0) {
            game_collection_t* collection = &collections[i];
            
            if (collection->game_count == 0) return false;
            
            srand(time(NULL));
            int index = rand() % collection->game_count;
            favorite_game_t* game = &collection->games[index];
            
            strcpy(core, game->core);
            strcpy(id_type, game->id_type);
            strcpy(identifier, game->identifier);
            strcpy(title, game->title);
            
            return true;
        }
    }
    
    return false;
}

// Smart recommendations engine
float calculate_recommendation_score(const char* core, const char* identifier, const char* title) {
    float score = 0.0f;
    
    // Base score for all games
    score += 1.0f;
    
    // Boost for highly rated games
    for (int i = 0; i < ratings_count; i++) {
        if (strcmp(ratings[i].core, core) == 0 && 
            strcmp(ratings[i].identifier, identifier) == 0) {
            score += (float)ratings[i].rating * 0.5f; // 0.5-2.5 points for 1-5 star ratings
            break;
        }
    }
    
    // Boost for favorited games
    for (int i = 0; i < favorites_count; i++) {
        if (strcmp(favorites[i].core, core) == 0 && 
            strcmp(favorites[i].identifier, identifier) == 0) {
            score += 2.0f; // +2 points for favorites
            break;
        }
    }
    
    // Moderate boost for completed games (they were good enough to finish)
    char completion_file[512];
    snprintf(completion_file, sizeof(completion_file), 
            "/media/fat/utils/completion/%s_%s.txt", core, identifier);
    
    FILE* fp = fopen(completion_file, "r");
    if (fp) {
        int percentage = 0;
        fscanf(fp, "%d", &percentage);
        fclose(fp);
        
        if (percentage >= 100) {
            score += 1.5f; // +1.5 for completed games
        } else if (percentage >= 50) {
            score += 0.5f; // +0.5 for partially completed games
        }
    }
    
    // Consider playtime (moderate playtime indicates good games)
    int playtime_minutes = get_game_playtime(core, identifier);
    if (playtime_minutes > 0) {
        // Sweet spot is 2-8 hours of playtime
        int hours = playtime_minutes / 60;
        if (hours >= 2 && hours <= 8) {
            score += 1.0f; // +1 for games with moderate playtime
        } else if (hours > 8) {
            score += 0.5f; // +0.5 for games with lots of playtime
        }
    }
    
    // Check how recently played (recently played games get slight penalty to encourage variety)
    for (int i = history_count - 1; i >= 0 && i >= history_count - 10; i--) {
        if (strcmp(history[i].core, core) == 0 && 
            strcmp(history[i].identifier, identifier) == 0) {
            int recency = history_count - i;
            score -= (float)recency * 0.1f; // -0.1 to -1.0 penalty for recently played
            break;
        }
    }
    
    // Console generation preference (slight boost for retro systems)
    console_generation_t gen = get_console_generation(core);
    if (gen >= GEN_3RD && gen <= GEN_5TH) {
        score += 0.3f; // Slight boost for classic console generations
    }
    
    return score > 0 ? score : 0.1f; // Minimum score
}

int get_game_recommendations(game_recommendation_t* recommendations, int max_recommendations) {
    int rec_count = 0;
    
    // Go through all known games (from favorites, history, and rated games)
    for (int i = 0; i < favorites_count && rec_count < max_recommendations; i++) {
        game_recommendation_t* rec = &recommendations[rec_count];
        strcpy(rec->core, favorites[i].core);
        strcpy(rec->identifier, favorites[i].identifier);
        strcpy(rec->title, favorites[i].title);
        rec->recommendation_score = calculate_recommendation_score(rec->core, rec->identifier, rec->title);
        rec_count++;
    }
    
    // Add rated games that aren't already in the list
    for (int i = 0; i < ratings_count && rec_count < max_recommendations; i++) {
        bool already_added = false;
        
        for (int j = 0; j < rec_count; j++) {
            if (strcmp(recommendations[j].core, ratings[i].core) == 0 &&
                strcmp(recommendations[j].identifier, ratings[i].identifier) == 0) {
                already_added = true;
                break;
            }
        }
        
        if (!already_added) {
            game_recommendation_t* rec = &recommendations[rec_count];
            strcpy(rec->core, ratings[i].core);
            strcpy(rec->identifier, ratings[i].identifier);
            strcpy(rec->title, ratings[i].title);
            rec->recommendation_score = calculate_recommendation_score(rec->core, rec->identifier, rec->title);
            rec_count++;
        }
    }
    
    // Add games from history that aren't already in the list
    for (int i = 0; i < history_count && rec_count < max_recommendations; i++) {
        bool already_added = false;
        
        for (int j = 0; j < rec_count; j++) {
            if (strcmp(recommendations[j].core, history[i].core) == 0 &&
                strcmp(recommendations[j].identifier, history[i].identifier) == 0) {
                already_added = true;
                break;
            }
        }
        
        if (!already_added) {
            game_recommendation_t* rec = &recommendations[rec_count];
            strcpy(rec->core, history[i].core);
            strcpy(rec->identifier, history[i].identifier);
            strcpy(rec->title, history[i].title);
            rec->recommendation_score = calculate_recommendation_score(rec->core, rec->identifier, rec->title);
            rec_count++;
        }
    }
    
    // Sort recommendations by score (bubble sort for simplicity)
    for (int i = 0; i < rec_count - 1; i++) {
        for (int j = 0; j < rec_count - i - 1; j++) {
            if (recommendations[j].recommendation_score < recommendations[j + 1].recommendation_score) {
                game_recommendation_t temp = recommendations[j];
                recommendations[j] = recommendations[j + 1];
                recommendations[j + 1] = temp;
            }
        }
    }
    
    return rec_count;
}

bool get_random_recommendation(char* core, char* id_type, char* identifier, char* title) {
    game_recommendation_t recommendations[50];
    int count = get_game_recommendations(recommendations, 50);
    
    if (count == 0) return false;
    
    // Weight random selection toward higher-scored recommendations
    // Top 10 recommendations get higher probability
    int selection_pool = count > 10 ? 10 : count;
    srand(time(NULL));
    int index = rand() % selection_pool;
    
    game_recommendation_t* selected = &recommendations[index];
    strcpy(core, selected->core);
    strcpy(id_type, "serial"); // Most recommendations use serial lookup
    strcpy(identifier, selected->identifier);
    strcpy(title, selected->title);
    
    return true;
}

void* command_thread(void* arg) {
    char buffer[4096];
    
    while (keep_running) {
        int fd = open(GAME_LAUNCHER_FIFO, O_RDONLY);
        if (fd < 0) {
            if (keep_running) {
                sleep(1);
            }
            continue;
        }
        
        while (keep_running) {
            ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) {
                break;
            }
            
            buffer[bytes] = '\0';
            
            printf("game_launcher: Received command: %s\n", buffer);
            
            // Simple command parsing (system:id_type:identifier:source)
            char* system = strtok(buffer, ":");
            char* id_type = strtok(NULL, ":");
            char* identifier = strtok(NULL, ":");
            char* source = strtok(NULL, ":\n");
            
            printf("game_launcher: Parsed - System: %s, ID Type: %s, Identifier: %s, Source: %s\n",
                   system ? system : "NULL", id_type ? id_type : "NULL", 
                   identifier ? identifier : "NULL", source ? source : "NULL");
            
            if (system && id_type && identifier) {
                printf("game_launcher: About to process request\n");
                fflush(stdout);
                
                // Handle special commands
                if (strcmp(system, "COMMAND") == 0) {
                    handle_special_command(id_type, identifier, source ? source : "unknown");
                } else {
                    process_game_request(system, id_type, identifier, source ? source : "unknown");
                }
            } else {
                printf("game_launcher: Failed to parse command - missing components\n");
                fflush(stdout);
            }
        }
        
        close(fd);
    }
    
    return NULL;
}

// Write PID file
void write_pid_file() {
    FILE* fp = fopen(PID_FILE, "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
}

// Main function
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("game_launcher: Starting Game Launcher Service\n");
    
    // Load configuration
    load_config();
    
    // Load favorites and history
    if (config.enable_favorites) {
        load_favorites();
    }
    
    // Load collections
    load_collections();
    
    // Create FIFO
    unlink(GAME_LAUNCHER_FIFO);
    if (mkfifo(GAME_LAUNCHER_FIFO, 0666) < 0) {
        perror("game_launcher: Failed to create FIFO");
        return 1;
    }
    chmod(GAME_LAUNCHER_FIFO, 0666);
    
    // Write PID file
    write_pid_file();
    
    // Initialize playtime tracking
    init_playtime_tracking();
    
    // Start command thread
    pthread_t cmd_thread;
    if (pthread_create(&cmd_thread, NULL, command_thread, NULL) != 0) {
        perror("game_launcher: Failed to create command thread");
        return 1;
    }
    
    printf("game_launcher: Service ready\n");
    
    // Main loop
    while (keep_running) {
        update_playtime_tracking();
        sleep(1);
    }
    
    // Cleanup
    printf("game_launcher: Shutting down\n");
    stop_playtime_tracking();
    pthread_join(cmd_thread, NULL);
    unlink(GAME_LAUNCHER_FIFO);
    unlink(PID_FILE);
    
    return 0;
}