# MiSTer Command Bridge API

The Command Bridge provides a unified interface for processing commands from various sources (UART, Menu, CD-ROM, etc.) and routing them to appropriate handlers or the MiSTer_cmd device.

## Architecture

```
External Sources → CMD Bridge → Command Handlers → Actions
     |                 |              |
   UART              Registry      Built-in
   Menu                |          Custom
   CD-ROM         MiSTer_cmd      Extensions
```

## Integration Guide

### 1. Basic Usage

```cpp
#include "cmd_bridge.h"

// Initialize the bridge (called once)
cmd_bridge_init();

// Process a command
cmd_result_t result = cmd_bridge_process("load_core Genesis");
if (result.success) {
    printf("Success: %s\n", result.message);
} else {
    printf("Failed: %s\n", result.message);
}
```

### 2. For UART Integration (uart_enhance branch)

```cpp
// In UART command processing
void uart_process_command(const char* uart_cmd) {
    // Remove "CMD:" prefix if present
    const char* cmd = uart_cmd;
    if (strncmp(cmd, "CMD:", 4) == 0) {
        cmd += 4;
    }
    
    // Process through bridge
    cmd_result_t result = cmd_bridge_process(cmd);
    
    // Send result back via UART
    uart_send_response(result.success ? "OK" : "ERROR", result.message);
}
```

### 3. For Menu Integration (smart_game_loading branch)

```cpp
// Register custom game loading handler
cmd_result_t handle_smart_load(const char* args) {
    // Parse game name
    // Search for game
    // Load appropriate core and game
    return result;
}

// In initialization
cmd_bridge_register("smart_load", handle_smart_load, "Intelligently load game");
```

### 4. For CD-ROM Integration (cdrom branch)

```cpp
// Register CD-ROM specific commands
cmd_result_t handle_cdrom_load(const char* args) {
    CDRomGameInfo info;
    if (cdrom_identify_game("/dev/sr0", args, &info)) {
        // Create disc image
        // Load appropriate core
        // Mount image
    }
    return result;
}

// In initialization
cmd_bridge_register("cdrom_load", handle_cdrom_load, "Load game from CD-ROM");
```

## Built-in Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `help` | none | List all available commands |
| `load_core` | `<rbf_name>` | Load a core RBF file |
| `load_game` | `<path>` | Load a game/ROM file |
| `mount_image` | `<index> <path>` | Mount disk image to index |
| `reset` | `[cold\|warm]` | Reset the current core |
| `set_option` | `<option> <value>` | Set core configuration |
| `screenshot` | `[filename]` | Take a screenshot |
| `menu` | `<up\|down\|left\|right\|ok\|back>` | Navigate OSD menu |

## Extending with Custom Commands

### Step 1: Define Handler Function

```cpp
cmd_result_t my_custom_handler(const char* args) {
    cmd_result_t result = { false, "", -1 };
    
    // Parse arguments
    if (!args) {
        strcpy(result.message, "Missing required arguments");
        return result;
    }
    
    // Do your custom processing
    if (do_something_cool(args)) {
        result.success = true;
        snprintf(result.message, sizeof(result.message), 
                 "Successfully processed: %s", args);
        result.result_code = 0;
    } else {
        strcpy(result.message, "Processing failed");
    }
    
    return result;
}
```

### Step 2: Register Command

```cpp
// During initialization
cmd_bridge_init();
cmd_bridge_register("my_command", my_custom_handler, 
                   "Does something cool with the arguments");
```

## MiSTer_cmd Passthrough

Any unrecognized commands are automatically forwarded to `/dev/MiSTer_cmd` if available. This ensures compatibility with existing MiSTer commands.

## Command Result Structure

```cpp
typedef struct {
    bool success;        // true if command succeeded
    char message[256];   // Human-readable result message
    int result_code;     // Numeric result code (0 = success)
} cmd_result_t;
```

## Thread Safety

The current implementation is **not thread-safe**. If you need to call from multiple threads, add your own synchronization.

## Example: Complete Integration

```cpp
// main.cpp or uart_handler.cpp
#include "cmd_bridge.h"

void init_my_module() {
    // Initialize bridge
    cmd_bridge_init();
    
    // Register custom commands
    cmd_bridge_register("my_load", handle_my_load, "Custom game loader");
    cmd_bridge_register("my_config", handle_my_config, "Custom configuration");
}

void process_external_command(const char* cmd_string) {
    // Process command
    cmd_result_t result = cmd_bridge_process(cmd_string);
    
    // Handle result
    if (result.success) {
        // Update UI, send ACK, etc.
        show_success_message(result.message);
    } else {
        // Show error, send NACK, etc.
        show_error_message(result.message);
    }
    
    // Log for debugging
    printf("Command '%s' %s: %s (code=%d)\n", 
           cmd_string, 
           result.success ? "succeeded" : "failed",
           result.message,
           result.result_code);
}
```

## Testing

Use the included `test_cmd_bridge` utility:

```bash
# Compile test utility
make test_cmd_bridge

# Test commands
./test_cmd_bridge "help"
./test_cmd_bridge "load_core Genesis"
./test_cmd_bridge "menu up"
```

## Future Enhancements

- Async command execution with callbacks
- Command history and undo
- Command aliases and macros
- Permission/security levels
- Rate limiting for external sources
- Response streaming for long operations