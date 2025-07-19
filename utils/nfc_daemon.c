/*
 * NFC Game Launcher Daemon
 * 
 * Reads NFC cards and triggers game loading via Game Launcher Service
 * Supports MIFARE Classic, NTAG, and other NFC card types
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <nfc/nfc.h>
#include <json-c/json.h>

static volatile int keep_running = 1;
static nfc_device* nfc_device = NULL;
static nfc_context* nfc_context = NULL;

// Signal handler
void signal_handler(int sig) {
    keep_running = 0;
}

// Send game launch command
bool send_game_launch_command(const char* system, const char* serial, const char* card_uid) {
    char command[512];
    snprintf(command, sizeof(command), 
             "{"
             "\"command\": \"find_game\", "
             "\"system\": \"%s\", "
             "\"id_type\": \"serial\", "
             "\"identifier\": \"%s\", "
             "\"source\": \"nfc\", "
             "\"auto_launch\": true, "
             "\"source_data\": {\"card_uid\": \"%s\"}"
             "}",
             system, serial, card_uid);
    
    int fd = open("/dev/MiSTer_game_launcher", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    ssize_t written = write(fd, command, strlen(command));
    write(fd, "\n", 1);
    close(fd);
    
    return written > 0;
}

// Load NFC card database
typedef struct {
    char uid[32];
    char title[256];
    char system[32];
    char serial[64];
    char region[32];
} nfc_card_t;

static nfc_card_t* nfc_cards = NULL;
static int nfc_card_count = 0;

bool load_nfc_database(const char* db_path) {
    FILE* fp = fopen(db_path, "r");
    if (!fp) {
        printf("nfc_daemon: NFC database not found: %s\n", db_path);
        return false;
    }
    
    // Read JSON file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* json_data = malloc(file_size + 1);
    fread(json_data, 1, file_size, fp);
    json_data[file_size] = '\0';
    fclose(fp);
    
    // Parse JSON
    json_object* root = json_tokener_parse(json_data);
    free(json_data);
    
    if (!root) {
        printf("nfc_daemon: Failed to parse NFC database\n");
        return false;
    }
    
    json_object* cards_array;
    if (!json_object_object_get_ex(root, "cards", &cards_array)) {
        json_object_put(root);
        return false;
    }
    
    nfc_card_count = json_object_array_length(cards_array);
    nfc_cards = malloc(nfc_card_count * sizeof(nfc_card_t));
    
    for (int i = 0; i < nfc_card_count; i++) {
        json_object* card = json_object_array_get_idx(cards_array, i);
        json_object* uid_obj, *title_obj, *system_obj, *serial_obj, *region_obj;
        
        if (json_object_object_get_ex(card, "uid", &uid_obj) &&
            json_object_object_get_ex(card, "title", &title_obj) &&
            json_object_object_get_ex(card, "system", &system_obj) &&
            json_object_object_get_ex(card, "serial", &serial_obj) &&
            json_object_object_get_ex(card, "region", &region_obj)) {
            
            strncpy(nfc_cards[i].uid, json_object_get_string(uid_obj), sizeof(nfc_cards[i].uid) - 1);
            strncpy(nfc_cards[i].title, json_object_get_string(title_obj), sizeof(nfc_cards[i].title) - 1);
            strncpy(nfc_cards[i].system, json_object_get_string(system_obj), sizeof(nfc_cards[i].system) - 1);
            strncpy(nfc_cards[i].serial, json_object_get_string(serial_obj), sizeof(nfc_cards[i].serial) - 1);
            strncpy(nfc_cards[i].region, json_object_get_string(region_obj), sizeof(nfc_cards[i].region) - 1);
        }
    }
    
    json_object_put(root);
    printf("nfc_daemon: Loaded %d NFC cards from database\n", nfc_card_count);
    return true;
}

// Find card by UID
nfc_card_t* find_card_by_uid(const char* uid) {
    for (int i = 0; i < nfc_card_count; i++) {
        if (strcmp(nfc_cards[i].uid, uid) == 0) {
            return &nfc_cards[i];
        }
    }
    return NULL;
}

// Convert UID to string
void uid_to_string(const uint8_t* uid, size_t uid_len, char* uid_str, size_t uid_str_len) {
    uid_str[0] = '\0';
    for (size_t i = 0; i < uid_len && i < uid_str_len/3; i++) {
        char byte_str[4];
        snprintf(byte_str, sizeof(byte_str), "%02X:", uid[i]);
        strcat(uid_str, byte_str);
    }
    
    // Remove trailing colon
    size_t len = strlen(uid_str);
    if (len > 0) {
        uid_str[len - 1] = '\0';
    }
}

// Main NFC polling loop
void nfc_poll_loop() {
    nfc_target target;
    char uid_str[64];
    char last_uid[64] = "";
    
    while (keep_running) {
        // Poll for NFC target
        if (nfc_initiator_poll_target(nfc_device, NULL, 0, &target) > 0) {
            // Convert UID to string
            uid_to_string(target.nti.nai.abtUid, target.nti.nai.szUidLen, uid_str, sizeof(uid_str));
            
            // Check if this is a new card (avoid duplicate reads)
            if (strcmp(uid_str, last_uid) != 0) {
                printf("nfc_daemon: NFC card detected: %s\n", uid_str);
                strcpy(last_uid, uid_str);
                
                // Look up card in database
                nfc_card_t* card = find_card_by_uid(uid_str);
                if (card) {
                    printf("nfc_daemon: Found card: %s (%s %s)\n", card->title, card->system, card->serial);
                    
                    // Send game launch command
                    if (send_game_launch_command(card->system, card->serial, uid_str)) {
                        printf("nfc_daemon: Sent launch command for %s\n", card->title);
                    } else {
                        printf("nfc_daemon: Failed to send launch command\n");
                    }
                } else {
                    printf("nfc_daemon: Unknown card: %s\n", uid_str);
                }
            }
        } else {
            // No card detected - clear last UID
            last_uid[0] = '\0';
        }
        
        usleep(100000); // Poll every 100ms
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("nfc_daemon: Starting NFC Game Launcher Daemon\n");
    
    // Initialize libnfc
    nfc_init(&nfc_context);
    if (!nfc_context) {
        fprintf(stderr, "nfc_daemon: Failed to initialize libnfc\n");
        return 1;
    }
    
    // Open NFC device
    nfc_device = nfc_open(nfc_context, NULL);
    if (!nfc_device) {
        fprintf(stderr, "nfc_daemon: No NFC device found\n");
        nfc_exit(nfc_context);
        return 1;
    }
    
    printf("nfc_daemon: NFC device opened: %s\n", nfc_device_get_name(nfc_device));
    
    // Initialize NFC device as initiator
    if (nfc_initiator_init(nfc_device) < 0) {
        fprintf(stderr, "nfc_daemon: Failed to initialize NFC device\n");
        nfc_close(nfc_device);
        nfc_exit(nfc_context);
        return 1;
    }
    
    // Load NFC card database
    if (!load_nfc_database("/media/fat/utils/configs/nfc_cards.db")) {
        fprintf(stderr, "nfc_daemon: Failed to load NFC database\n");
        nfc_close(nfc_device);
        nfc_exit(nfc_context);
        return 1;
    }
    
    // Start polling loop
    nfc_poll_loop();
    
    // Cleanup
    printf("nfc_daemon: Shutting down\n");
    if (nfc_cards) free(nfc_cards);
    nfc_close(nfc_device);
    nfc_exit(nfc_context);
    
    return 0;
}

/*
 * Example NFC card database (/media/fat/utils/configs/nfc_cards.db):
 * 
 * {
 *   "cards": [
 *     {
 *       "uid": "04:A3:22:B2:C4:58:80",
 *       "title": "Castlevania: Symphony of the Night",
 *       "system": "PSX",
 *       "serial": "SLUS-00067",
 *       "region": "USA"
 *     },
 *     {
 *       "uid": "04:B1:33:C2:D4:69:91",
 *       "title": "Panzer Dragoon Saga",
 *       "system": "Saturn",
 *       "serial": "T-8109H",
 *       "region": "USA"
 *     }
 *   ]
 * }
 */