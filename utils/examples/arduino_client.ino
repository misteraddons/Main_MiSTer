/*
 * MiSTer UART Game Launcher - Arduino Client Example
 * 
 * This Arduino sketch demonstrates how to control MiSTer game launching
 * via UART/Serial interface. Perfect for arcade buttons, rotary encoders,
 * or custom control panels.
 * 
 * Hardware Requirements:
 * - Arduino Uno, Leonardo, or compatible board
 * - USB connection to MiSTer
 * - Optional: Buttons, switches, LCD display
 * 
 * Features:
 * - Simple game launching via serial commands
 * - Button-based game selection
 * - Serial monitor interface for testing
 * - Status monitoring and error handling
 */

// Game library - add your favorite games here
struct Game {
    const char* name;
    const char* core;
    const char* id_type;
    const char* identifier;
};

Game gameLibrary[] = {
    {"Castlevania SOTN", "PSX", "serial", "SLUS-00067"},
    {"Final Fantasy VII", "PSX", "serial", "SCUS-94455"},
    {"Metal Gear Solid", "PSX", "serial", "SLUS-00594"},
    {"Panzer Dragoon", "Saturn", "serial", "T-8107G"},
    {"Nights into Dreams", "Saturn", "serial", "T-8106G"},
    {"Sonic CD", "MegaCD", "serial", "T-25013"},
    {"Lunar Silver Star", "MegaCD", "serial", "T-25033"},
    {"Street Fighter Alpha", "Arcade", "title", "Street Fighter Alpha"}
};

const int NUM_GAMES = sizeof(gameLibrary) / sizeof(Game);

// Pin definitions (customize for your hardware)
const int BUTTON_1 = 2;   // Launch current game
const int BUTTON_2 = 3;   // Next game
const int BUTTON_3 = 4;   // Previous game
const int BUTTON_4 = 5;   // Status check
const int LED_STATUS = 13; // Status LED

// Variables
int currentGame = 0;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 200;
bool misterConnected = false;

void setup() {
    // Initialize serial communication (must match MiSTer UART daemon config)
    Serial.begin(115200);
    
    // Initialize pins
    pinMode(BUTTON_1, INPUT_PULLUP);
    pinMode(BUTTON_2, INPUT_PULLUP);
    pinMode(BUTTON_3, INPUT_PULLUP);
    pinMode(BUTTON_4, INPUT_PULLUP);
    pinMode(LED_STATUS, OUTPUT);
    
    // Wait for connection
    delay(2000);
    
    Serial.println("Arduino MiSTer Game Launcher v1.0");
    Serial.println("Connecting to MiSTer...");
    
    // Test connection
    testConnection();
    
    // Display current game
    displayCurrentGame();
}

void loop() {
    // Handle button presses
    handleButtons();
    
    // Handle serial input from MiSTer
    handleSerial();
    
    // Update status LED
    digitalWrite(LED_STATUS, misterConnected ? HIGH : LOW);
    
    delay(50); // Small delay to prevent excessive polling
}

void handleButtons() {
    unsigned long currentTime = millis();
    
    // Debounce check
    if (currentTime - lastButtonPress < DEBOUNCE_DELAY) {
        return;
    }
    
    // Button 1: Launch current game
    if (digitalRead(BUTTON_1) == LOW) {
        lastButtonPress = currentTime;
        launchCurrentGame();
    }
    
    // Button 2: Next game
    if (digitalRead(BUTTON_2) == LOW) {
        lastButtonPress = currentTime;
        currentGame = (currentGame + 1) % NUM_GAMES;
        displayCurrentGame();
    }
    
    // Button 3: Previous game
    if (digitalRead(BUTTON_3) == LOW) {
        lastButtonPress = currentTime;
        currentGame = (currentGame - 1 + NUM_GAMES) % NUM_GAMES;
        displayCurrentGame();
    }
    
    // Button 4: Status check
    if (digitalRead(BUTTON_4) == LOW) {
        lastButtonPress = currentTime;
        checkStatus();
    }
}

void handleSerial() {
    if (Serial.available()) {
        String response = Serial.readStringUntil('\n');
        response.trim();
        
        Serial.print("MiSTer: ");
        Serial.println(response);
        
        // Parse response
        if (response.startsWith("OK")) {
            misterConnected = true;
            
            if (response.indexOf("LAUNCHED") != -1) {
                Serial.println("✓ Game launched successfully!");
            } else if (response.indexOf("PONG") != -1) {
                Serial.println("✓ Connection OK");
            } else if (response.indexOf("Ready") != -1) {
                Serial.println("✓ MiSTer UART daemon ready");
            }
        } else if (response.startsWith("ERROR")) {
            Serial.print("✗ Error: ");
            Serial.println(response.substring(6));
        }
    }
}

void launchCurrentGame() {
    Game& game = gameLibrary[currentGame];
    
    Serial.print("Launching: ");
    Serial.println(game.name);
    
    // Send LAUNCH command to MiSTer
    Serial.print("LAUNCH ");
    Serial.print(game.core);
    Serial.print(" ");
    Serial.print(game.id_type);
    Serial.print(" ");
    Serial.println(game.identifier);
}

void displayCurrentGame() {
    Game& game = gameLibrary[currentGame];
    
    Serial.println("=====================================");
    Serial.print("Current Game [");
    Serial.print(currentGame + 1);
    Serial.print("/");
    Serial.print(NUM_GAMES);
    Serial.println("]:");
    Serial.print("  Name: ");
    Serial.println(game.name);
    Serial.print("  Core: ");
    Serial.println(game.core);
    Serial.print("  ID: ");
    Serial.println(game.identifier);
    Serial.println("=====================================");
    Serial.println("Commands:");
    Serial.println("  Button 1 (or 'l'): Launch game");
    Serial.println("  Button 2 (or 'n'): Next game");
    Serial.println("  Button 3 (or 'p'): Previous game");
    Serial.println("  Button 4 (or 's'): Status check");
    Serial.println("  Serial commands: PING, STATUS, VERSION");
}

void testConnection() {
    Serial.println("PING");
    
    // Wait for response
    unsigned long startTime = millis();
    while (millis() - startTime < 3000) { // 3 second timeout
        if (Serial.available()) {
            String response = Serial.readStringUntil('\n');
            response.trim();
            
            if (response.indexOf("PONG") != -1) {
                misterConnected = true;
                Serial.println("✓ Connected to MiSTer!");
                return;
            }
        }
        delay(100);
    }
    
    Serial.println("✗ No response from MiSTer");
    Serial.println("  Check UART daemon is running");
    Serial.println("  Check serial connection");
}

void checkStatus() {
    Serial.println("STATUS");
}

// Handle manual serial commands for testing
void serialEvent() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toLowerCase();
        
        if (command == "l") {
            launchCurrentGame();
        } else if (command == "n") {
            currentGame = (currentGame + 1) % NUM_GAMES;
            displayCurrentGame();
        } else if (command == "p") {
            currentGame = (currentGame - 1 + NUM_GAMES) % NUM_GAMES;
            displayCurrentGame();
        } else if (command == "s") {
            checkStatus();
        } else if (command == "ping") {
            Serial.println("PING");
        } else if (command == "status") {
            Serial.println("STATUS");
        } else if (command == "version") {
            Serial.println("VERSION");
        } else if (command == "help") {
            displayCurrentGame();
        } else if (command.startsWith("launch ")) {
            // Manual launch command: "launch PSX serial SLUS-00067"
            Serial.println(command.substring(7));
        }
    }
}

/*
 * Hardware Setup Guide:
 * 
 * Basic Setup (USB only):
 * 1. Connect Arduino to MiSTer via USB
 * 2. Upload this sketch
 * 3. Open Serial Monitor at 115200 baud
 * 4. Test with 'ping' command
 * 
 * Button Setup:
 * - Button 1 (Launch): Pin 2 to GND
 * - Button 2 (Next): Pin 3 to GND  
 * - Button 3 (Previous): Pin 4 to GND
 * - Button 4 (Status): Pin 5 to GND
 * - Status LED: Pin 13 (built-in LED)
 * 
 * Optional Enhancements:
 * - LCD Display: Show current game info
 * - Rotary Encoder: Navigate game library
 * - Multiple button banks: Different game categories
 * - SD Card: Store larger game libraries
 * - OLED Display: Rich game information
 * 
 * Wiring Example:
 * 
 *     +5V ----[10kΩ]---- Button ---- GND
 *              |
 *           Arduino Pin
 * 
 * The internal pullup resistors are used in this example,
 * so external pullups are optional.
 */