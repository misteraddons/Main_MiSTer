/*
 * MiSTer Core Update Monitor Daemon
 * 
 * Monitors for core updates and notifies user when new versions are available
 * Checks GitHub releases, update_all script status, and core timestamps
 * 
 * Features:
 * - GitHub API integration for release monitoring
 * - Core file timestamp checking
 * - OSD notifications for available updates
 * - Configurable check intervals
 * - Update history tracking
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <dirent.h>

#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define CONFIG_FILE "/media/fat/utils/update_monitor.conf"
#define PID_FILE "/tmp/update_monitor.pid"
#define UPDATE_CACHE_FILE "/tmp/mister_update_cache.json"
#define CORES_DIR "/media/fat/_Computer"
#define CONSOLE_CORES_DIR "/media/fat/_Console"
#define ARCADE_CORES_DIR "/media/fat/_Arcade"

// Core information
typedef struct {
    char name[64];
    char path[256];
    time_t last_modified;
    char version[32];
    char github_repo[128];
    bool update_available;
    time_t last_checked;
} core_info_t;

// HTTP response structure for curl
typedef struct {
    char* data;
    size_t size;
} http_response_t;

// Configuration
typedef struct {
    int check_interval_hours;
    bool enable_notifications;
    bool check_github_releases;
    bool check_update_all;
    bool auto_check_on_startup;
    char github_token[128];
    char notification_filter[256];
    int max_cores_to_track;
} update_config_t;

// Global variables
static volatile int keep_running = 1;
static update_config_t config;
static core_info_t* cores = NULL;
static int cores_count = 0;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Initialize default configuration
void init_config_defaults() {
    config.check_interval_hours = 24;
    config.enable_notifications = true;
    config.check_github_releases = true;
    config.check_update_all = false;
    config.auto_check_on_startup = true;
    strcpy(config.github_token, "");
    strcpy(config.notification_filter, "");
    config.max_cores_to_track = 100;
}

// Load configuration
void load_config() {
    init_config_defaults();
    
    FILE* fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        printf("update_monitor: Using default configuration\n");
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char* equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char* key = line;
        char* value = equals + 1;
        
        // Trim whitespace
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        char* newline = strchr(value, '\n');
        if (newline) *newline = '\0';
        
        if (strcmp(key, "check_interval_hours") == 0) {
            config.check_interval_hours = atoi(value);
        } else if (strcmp(key, "enable_notifications") == 0) {
            config.enable_notifications = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "check_github_releases") == 0) {
            config.check_github_releases = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "check_update_all") == 0) {
            config.check_update_all = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "auto_check_on_startup") == 0) {
            config.auto_check_on_startup = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "github_token") == 0) {
            strncpy(config.github_token, value, sizeof(config.github_token) - 1);
        } else if (strcmp(key, "notification_filter") == 0) {
            strncpy(config.notification_filter, value, sizeof(config.notification_filter) - 1);
        } else if (strcmp(key, "max_cores_to_track") == 0) {
            config.max_cores_to_track = atoi(value);
        }
    }
    
    fclose(fp);
    printf("update_monitor: Configuration loaded\n");
}

// Send OSD message
void send_osd_message(const char* message) {
    if (!config.enable_notifications) return;
    
    int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "osd_message %s", message);
        write(fd, cmd, strlen(cmd));
        close(fd);
    }
}

// HTTP response callback for curl
static size_t http_response_callback(void* contents, size_t size, size_t nmemb, http_response_t* response) {
    size_t total_size = size * nmemb;
    
    char* new_data = realloc(response->data, response->size + total_size + 1);
    if (!new_data) return 0;
    
    response->data = new_data;
    memcpy(&response->data[response->size], contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';
    
    return total_size;
}

// Make HTTP request
char* http_get(const char* url) {
    CURL* curl;
    CURLcode res;
    http_response_t response = {0};
    
    curl = curl_easy_init();
    if (!curl) return NULL;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MiSTer-Update-Monitor/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // Add GitHub token if available
    struct curl_slist* headers = NULL;
    if (strlen(config.github_token) > 0) {
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", config.github_token);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    res = curl_easy_perform(curl);
    
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        if (response.data) free(response.data);
        return NULL;
    }
    
    return response.data;
}

// Check GitHub releases for updates
bool check_github_updates(core_info_t* core) {
    if (!config.check_github_releases || strlen(core->github_repo) == 0) {
        return false;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", core->github_repo);
    
    char* response = http_get(url);
    if (!response) return false;
    
    json_object* root = json_tokener_parse(response);
    free(response);
    
    if (!root) return false;
    
    json_object* tag_name_obj;
    if (json_object_object_get_ex(root, "tag_name", &tag_name_obj)) {
        const char* latest_version = json_object_get_string(tag_name_obj);
        
        // Compare versions (simple string comparison for now)
        if (strcmp(latest_version, core->version) != 0) {
            strncpy(core->version, latest_version, sizeof(core->version) - 1);
            core->update_available = true;
            
            printf("update_monitor: Update available for %s: %s\n", core->name, latest_version);
            
            char message[256];
            snprintf(message, sizeof(message), "Update available: %s %s", core->name, latest_version);
            send_osd_message(message);
        }
    }
    
    json_object_put(root);
    core->last_checked = time(NULL);
    
    return core->update_available;
}

// Scan for core files
void scan_cores() {
    const char* core_dirs[] = {CORES_DIR, CONSOLE_CORES_DIR, ARCADE_CORES_DIR, NULL};
    
    cores_count = 0;
    if (cores) {
        free(cores);
        cores = NULL;
    }
    
    cores = malloc(config.max_cores_to_track * sizeof(core_info_t));
    if (!cores) return;
    
    for (int dir_idx = 0; core_dirs[dir_idx] != NULL; dir_idx++) {
        DIR* dir = opendir(core_dirs[dir_idx]);
        if (!dir) continue;
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL && cores_count < config.max_cores_to_track) {
            if (entry->d_type != DT_REG) continue;
            
            // Check if it's a core file (.rbf extension)
            char* ext = strrchr(entry->d_name, '.');
            if (!ext || strcmp(ext, ".rbf") != 0) continue;
            
            // Get full path
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", core_dirs[dir_idx], entry->d_name);
            
            struct stat file_stat;
            if (stat(full_path, &file_stat) != 0) continue;
            
            // Initialize core info
            core_info_t* core = &cores[cores_count];\n            memset(core, 0, sizeof(core_info_t));\n            \n            // Extract core name (filename without .rbf)\n            strncpy(core->name, entry->d_name, sizeof(core->name) - 1);\n            char* rbf_ext = strstr(core->name, \".rbf\");\n            if (rbf_ext) *rbf_ext = '\\0';\n            \n            strncpy(core->path, full_path, sizeof(core->path) - 1);\n            core->last_modified = file_stat.st_mtime;\n            core->update_available = false;\n            core->last_checked = 0;\n            \n            // Try to determine GitHub repo (basic mapping)\n            if (strstr(core->name, \"Amiga\") || strstr(core->name, \"Minimig\")) {\n                strcpy(core->github_repo, \"MiSTer-devel/Minimig-AGA_MiSTer\");\n            } else if (strstr(core->name, \"ao486\")) {\n                strcpy(core->github_repo, \"MiSTer-devel/ao486_MiSTer\");\n            } else if (strstr(core->name, \"SNES\")) {\n                strcpy(core->github_repo, \"MiSTer-devel/SNES_MiSTer\");\n            } else if (strstr(core->name, \"Genesis\")) {\n                strcpy(core->github_repo, \"MiSTer-devel/Genesis_MiSTer\");\n            } else if (strstr(core->name, \"NES\")) {\n                strcpy(core->github_repo, \"MiSTer-devel/NES_MiSTer\");\n            } else if (strstr(core->name, \"GBA\")) {\n                strcpy(core->github_repo, \"MiSTer-devel/GBA_MiSTer\");\n            }\n            // Add more core mappings as needed\n            \n            cores_count++;\n        }\n        \n        closedir(dir);\n    }\n    \n    printf(\"update_monitor: Found %d cores to monitor\\n\", cores_count);\n}\n\n// Check for updates on all cores\nvoid check_all_updates() {\n    printf(\"update_monitor: Checking for core updates...\\n\");\n    \n    int updates_found = 0;\n    \n    for (int i = 0; i < cores_count; i++) {\n        core_info_t* core = &cores[i];\n        \n        // Skip if filter is set and core doesn't match\n        if (strlen(config.notification_filter) > 0 && \n            !strstr(core->name, config.notification_filter)) {\n            continue;\n        }\n        \n        // Check if enough time has passed since last check\n        time_t now = time(NULL);\n        if (core->last_checked > 0 && \n            (now - core->last_checked) < (config.check_interval_hours * 3600)) {\n            continue;\n        }\n        \n        if (check_github_updates(core)) {\n            updates_found++;\n        }\n        \n        // Add small delay to avoid rate limiting\n        usleep(500000); // 500ms\n    }\n    \n    if (updates_found > 0) {\n        char message[128];\n        snprintf(message, sizeof(message), \"%d core updates available\", updates_found);\n        send_osd_message(message);\n    } else if (config.enable_notifications) {\n        send_osd_message(\"All cores are up to date\");\n    }\n    \n    printf(\"update_monitor: Found %d core updates\\n\", updates_found);\n}\n\n// Save update cache\nvoid save_update_cache() {\n    json_object* root = json_object_new_object();\n    json_object* cores_array = json_object_new_array();\n    \n    for (int i = 0; i < cores_count; i++) {\n        json_object* core_obj = json_object_new_object();\n        \n        json_object_object_add(core_obj, \"name\", json_object_new_string(cores[i].name));\n        json_object_object_add(core_obj, \"version\", json_object_new_string(cores[i].version));\n        json_object_object_add(core_obj, \"last_checked\", json_object_new_int64(cores[i].last_checked));\n        json_object_object_add(core_obj, \"update_available\", json_object_new_boolean(cores[i].update_available));\n        \n        json_array_add(cores_array, core_obj);\n    }\n    \n    json_object_object_add(root, \"cores\", cores_array);\n    json_object_object_add(root, \"last_scan\", json_object_new_int64(time(NULL)));\n    \n    const char* json_string = json_object_to_json_string(root);\n    \n    FILE* fp = fopen(UPDATE_CACHE_FILE, \"w\");\n    if (fp) {\n        fputs(json_string, fp);\n        fclose(fp);\n    }\n    \n    json_object_put(root);\n}\n\n// Load update cache\nvoid load_update_cache() {\n    FILE* fp = fopen(UPDATE_CACHE_FILE, \"r\");\n    if (!fp) return;\n    \n    fseek(fp, 0, SEEK_END);\n    long file_size = ftell(fp);\n    rewind(fp);\n    \n    char* json_data = malloc(file_size + 1);\n    fread(json_data, 1, file_size, fp);\n    json_data[file_size] = '\\0';\n    fclose(fp);\n    \n    json_object* root = json_tokener_parse(json_data);\n    free(json_data);\n    \n    if (!root) return;\n    \n    json_object* cores_array;\n    if (json_object_object_get_ex(root, \"cores\", &cores_array)) {\n        int array_len = json_object_array_length(cores_array);\n        \n        for (int i = 0; i < array_len && i < cores_count; i++) {\n            json_object* core_obj = json_object_array_get_idx(cores_array, i);\n            \n            json_object* name_obj, *version_obj, *last_checked_obj, *update_obj;\n            \n            if (json_object_object_get_ex(core_obj, \"name\", &name_obj)) {\n                const char* name = json_object_get_string(name_obj);\n                \n                // Find matching core\n                for (int j = 0; j < cores_count; j++) {\n                    if (strcmp(cores[j].name, name) == 0) {\n                        if (json_object_object_get_ex(core_obj, \"version\", &version_obj)) {\n                            strncpy(cores[j].version, json_object_get_string(version_obj), \n                                   sizeof(cores[j].version) - 1);\n                        }\n                        if (json_object_object_get_ex(core_obj, \"last_checked\", &last_checked_obj)) {\n                            cores[j].last_checked = json_object_get_int64(last_checked_obj);\n                        }\n                        if (json_object_object_get_ex(core_obj, \"update_available\", &update_obj)) {\n                            cores[j].update_available = json_object_get_boolean(update_obj);\n                        }\n                        break;\n                    }\n                }\n            }\n        }\n    }\n    \n    json_object_put(root);\n}\n\n// Write PID file\nvoid write_pid_file() {\n    FILE* fp = fopen(PID_FILE, \"w\");\n    if (fp) {\n        fprintf(fp, \"%d\\n\", getpid());\n        fclose(fp);\n    }\n}\n\nint main(int argc, char* argv[]) {\n    signal(SIGINT, signal_handler);\n    signal(SIGTERM, signal_handler);\n    signal(SIGPIPE, SIG_IGN);\n    \n    printf(\"update_monitor: Starting MiSTer Core Update Monitor\\n\");\n    \n    // Load configuration\n    load_config();\n    \n    // Initialize curl\n    curl_global_init(CURL_GLOBAL_DEFAULT);\n    \n    bool foreground = (argc > 1 && strcmp(argv[1], \"-f\") == 0);\n    bool check_now = (argc > 1 && strcmp(argv[1], \"--check\") == 0);\n    \n    if (!foreground && !check_now) {\n        pid_t pid = fork();\n        if (pid < 0) {\n            perror(\"fork\");\n            exit(1);\n        }\n        if (pid > 0) {\n            exit(0);\n        }\n        \n        setsid();\n        close(STDIN_FILENO);\n        close(STDOUT_FILENO);\n        close(STDERR_FILENO);\n    }\n    \n    // Write PID file\n    write_pid_file();\n    \n    // Scan for cores\n    scan_cores();\n    load_update_cache();\n    \n    if (check_now) {\n        // One-time check and exit\n        check_all_updates();\n        save_update_cache();\n        exit(0);\n    }\n    \n    // Initial check on startup\n    if (config.auto_check_on_startup) {\n        check_all_updates();\n        save_update_cache();\n    }\n    \n    printf(\"update_monitor: Core update monitoring active\\n\");\n    \n    // Main monitoring loop\n    time_t last_check = time(NULL);\n    while (keep_running) {\n        time_t now = time(NULL);\n        \n        // Check if it's time for periodic update check\n        if ((now - last_check) >= (config.check_interval_hours * 3600)) {\n            check_all_updates();\n            save_update_cache();\n            last_check = now;\n        }\n        \n        sleep(60); // Check every minute\n    }\n    \n    // Cleanup\n    curl_global_cleanup();\n    if (cores) free(cores);\n    \n    printf(\"update_monitor: Shutting down\\n\");\n    unlink(PID_FILE);\n    \n    return 0;\n}"}