#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cmd_bridge.h"

// Mock file_io functions for testing
extern "C" {
    bool FileExists(const char* path) {
        return true; // Mock implementation
    }
}

// Example custom command handler
cmd_result_t test_custom_command(const char* args) {
    cmd_result_t result = { false, "", -1 };
    
    if (!args) {
        strcpy(result.message, "test_custom requires arguments");
        return result;
    }
    
    result.success = true;
    snprintf(result.message, sizeof(result.message), 
             "Custom command executed with args: %s", args);
    result.result_code = 42;
    
    return result;
}

void test_command(const char* cmd) {
    printf("\n=== Testing command: '%s' ===\n", cmd);
    
    cmd_result_t result = cmd_bridge_process(cmd);
    
    printf("Result: %s\n", result.success ? "SUCCESS" : "FAILED");
    printf("Message: %s\n", result.message);
    printf("Code: %d\n", result.result_code);
}

void run_interactive_mode() {
    printf("\n=== Interactive Mode ===\n");
    printf("Enter commands (type 'quit' to exit):\n");
    
    char input[256];
    while (true) {
        printf("cmd> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        // Remove newline
        size_t len = strlen(input);
        if (len > 0 && input[len-1] == '\n') {
            input[len-1] = '\0';
        }
        
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            break;
        }
        
        if (strlen(input) > 0) {
            test_command(input);
        }
    }
}

int main(int argc, char* argv[]) {
    printf("MiSTer Command Bridge Test Utility\n");
    printf("==================================\n");
    
    // Initialize the bridge
    cmd_bridge_init();
    
    // Register a custom test command
    cmd_bridge_register("test_custom", test_custom_command, 
                       "Test custom command handler");
    
    // If command provided as argument, test it
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            test_command(argv[i]);
        }
        return 0;
    }
    
    // Otherwise run built-in tests
    printf("\nRunning built-in tests:\n");
    
    // Test basic commands
    test_command("help");
    test_command("load_core Genesis");
    test_command("load_game /games/sonic.bin");
    test_command("mount_image 0 /games/disk.img");
    test_command("reset cold");
    test_command("set_option video_mode 1");
    test_command("screenshot test.png");
    test_command("menu up");
    test_command("menu down");
    test_command("menu ok");
    
    // Test search commands
    test_command("search_files sonic");
    test_command("search_games mario");
    test_command("search_cores SNES");
    test_command("search_games zelda Nintendo");
    test_command("search_cores");
    
    // Test custom command
    test_command("test_custom hello world");
    
    // Test error cases
    test_command("");  // Empty command
    test_command("   ");  // Whitespace only
    test_command("nonexistent_command");  // Unknown command
    test_command("load_core");  // Missing arguments
    test_command("mount_image abc");  // Invalid arguments
    
    // Test edge cases
    test_command("  load_core   Genesis  ");  // Extra whitespace
    test_command("LOAD_CORE genesis");  // Case insensitive
    
    // Run interactive mode if no arguments provided
    if (argc == 1) {
        run_interactive_mode();
    }
    
    printf("\nTest complete!\n");
    return 0;
}