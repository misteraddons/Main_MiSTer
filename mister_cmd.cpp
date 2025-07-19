/*
 * MiSTer Command Interface
 * 
 * Minimal command interface for external processes to communicate with MiSTer
 * This allows CD-ROM daemon and other tools to trigger actions without
 * being integrated into the main codebase
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>

#include "mister_cmd.h"
#include "menu.h"
#include "user_io.h"
#include "file_io.h"
#include "menu_refresh.h"
#include "input.h"

#define MISTER_CMD_FIFO "/dev/MiSTer_cmd"
#define CMD_BUFFER_SIZE 256

static pthread_t cmd_thread;
static volatile int cmd_thread_running = 0;
static int cmd_fd = -1;

// Command handlers
static void handle_refresh_menu() {
    // Queue a menu refresh on next UI loop
    menu_request_refresh();
}

static void handle_popup_cdrom_selection() {
    // Show CD-ROM selection popup
    // This will scan for numbered MGL files and show them
    SelectFile("/media/fat/", "MGL", SCANO_DIR | SCANO_CDROM, MENU_CDROM_SELECT, KEY_F12);
}

static void handle_load_mgl(const char* path) {
    if (path && strlen(path) > 0) {
        user_io_file_mount(path, 0);
    }
}

static void handle_osd_message(const char* message) {
    if (message && strlen(message) > 0) {
        // Show OSD info message for 3 seconds
        Info(message, 3000);
    }
}

static void handle_command(const char* cmd) {
    // Remove newline if present
    char clean_cmd[CMD_BUFFER_SIZE];
    strncpy(clean_cmd, cmd, CMD_BUFFER_SIZE - 1);
    clean_cmd[CMD_BUFFER_SIZE - 1] = '\0';
    
    char* newline = strchr(clean_cmd, '\n');
    if (newline) *newline = '\0';
    
    printf("MiSTer_cmd: Received command: '%s'\n", clean_cmd);
    
    // Parse commands
    if (strcmp(clean_cmd, "refresh_menu") == 0) {
        handle_refresh_menu();
    }
    else if (strcmp(clean_cmd, "popup_cdrom_selection") == 0) {
        handle_popup_cdrom_selection();
    }
    else if (strncmp(clean_cmd, "load_mgl ", 9) == 0) {
        handle_load_mgl(clean_cmd + 9);
    }
    else if (strncmp(clean_cmd, "osd_message ", 12) == 0) {
        handle_osd_message(clean_cmd + 12);
    }
    else {
        printf("MiSTer_cmd: Unknown command: %s\n", clean_cmd);
    }
}

// Command thread - reads from FIFO
static void* cmd_thread_func(void* arg) {
    char buffer[CMD_BUFFER_SIZE];
    
    while (cmd_thread_running) {
        // Open FIFO for reading (will block until someone opens for writing)
        cmd_fd = open(MISTER_CMD_FIFO, O_RDONLY);
        if (cmd_fd < 0) {
            if (cmd_thread_running) {
                perror("MiSTer_cmd: Failed to open FIFO");
                sleep(5);
            }
            continue;
        }
        
        // Read commands
        while (cmd_thread_running) {
            ssize_t bytes = read(cmd_fd, buffer, sizeof(buffer) - 1);
            if (bytes <= 0) {
                // Writer closed the FIFO
                break;
            }
            
            buffer[bytes] = '\0';
            
            // Handle each line as a separate command
            char* line = strtok(buffer, "\n");
            while (line) {
                if (strlen(line) > 0) {
                    handle_command(line);
                }
                line = strtok(NULL, "\n");
            }
        }
        
        close(cmd_fd);
        cmd_fd = -1;
    }
    
    return NULL;
}

// Initialize command interface
void mister_cmd_init() {
    // Remove old FIFO if it exists
    unlink(MISTER_CMD_FIFO);
    
    // Create new FIFO
    if (mkfifo(MISTER_CMD_FIFO, 0666) < 0) {
        perror("MiSTer_cmd: Failed to create FIFO");
        return;
    }
    
    // Make it world-writable
    chmod(MISTER_CMD_FIFO, 0666);
    
    // Start command thread
    cmd_thread_running = 1;
    if (pthread_create(&cmd_thread, NULL, cmd_thread_func, NULL) != 0) {
        perror("MiSTer_cmd: Failed to create thread");
        cmd_thread_running = 0;
        unlink(MISTER_CMD_FIFO);
        return;
    }
    
    printf("MiSTer_cmd: Command interface initialized\n");
}

// Cleanup command interface
void mister_cmd_cleanup() {
    cmd_thread_running = 0;
    
    // Close FIFO if open
    if (cmd_fd >= 0) {
        close(cmd_fd);
    }
    
    // Wake up the thread by writing to FIFO
    int fd = open(MISTER_CMD_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        write(fd, "exit\n", 5);
        close(fd);
    }
    
    // Wait for thread to exit
    pthread_join(cmd_thread, NULL);
    
    // Remove FIFO
    unlink(MISTER_CMD_FIFO);
    
    printf("MiSTer_cmd: Command interface cleaned up\n");
}