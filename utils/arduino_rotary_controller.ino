/*
 * Arduino I2C Rotary Encoder Controller for MiSTer
 * 
 * Supports multiple rotary encoders with push buttons
 * Communicates via I2C to the MiSTer I2C daemon
 * 
 * Hardware:
 * - Arduino Uno/Nano/Pro Mini
 * - Rotary encoders (KY-040 or similar)
 * - I2C connection to MiSTer
 * 
 * I2C Address: 0x30 (configurable)
 */

#include <Wire.h>

#define I2C_ADDRESS 0x30
#define MAX_ENCODERS 4

// Rotary encoder pins
struct EncoderPin {
  int pin_a;
  int pin_b;
  int pin_button;
  volatile int position;
  volatile bool button_pressed;
  volatile bool button_changed;
  int last_a;
  int last_b;
  unsigned long last_button_change;
};

// Encoder configurations
EncoderPin encoders[MAX_ENCODERS] = {
  {2, 3, 4, 0, false, false, 0, 0, 0},    // Encoder 0
  {5, 6, 7, 0, false, false, 0, 0, 0},    // Encoder 1
  {8, 9, 10, 0, false, false, 0, 0, 0},   // Encoder 2
  {11, 12, 13, 0, false, false, 0, 0, 0}  // Encoder 3
};

int encoder_count = 2; // Actually using 2 encoders
bool led_state = false;

// I2C command structure
enum I2CCommand {
  CMD_INIT = 0x01,
  CMD_GET_ENCODER_COUNT = 0x02,
  CMD_GET_ENCODER_POSITION = 0x10,
  CMD_GET_BUTTON_STATE = 0x20,
  CMD_RESET_ENCODER = 0x30
};

void setup() {
  Serial.begin(9600);
  Serial.println("MiSTer Rotary Encoder Controller");
  
  // Initialize I2C as slave
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
  
  // Initialize encoders
  for (int i = 0; i < encoder_count; i++) {
    pinMode(encoders[i].pin_a, INPUT_PULLUP);
    pinMode(encoders[i].pin_b, INPUT_PULLUP);
    pinMode(encoders[i].pin_button, INPUT_PULLUP);
    
    encoders[i].last_a = digitalRead(encoders[i].pin_a);
    encoders[i].last_b = digitalRead(encoders[i].pin_b);
  }
  
  // Set up interrupts for encoder 0 (main game selector)
  attachInterrupt(digitalPinToInterrupt(encoders[0].pin_a), encoder0_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoders[0].pin_b), encoder0_isr, CHANGE);
  
  // Built-in LED for status
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.println("Ready");
}

void loop() {
  // Update encoder positions (polling method for encoders without interrupts)
  for (int i = 1; i < encoder_count; i++) {
    updateEncoder(i);
  }
  
  // Check button states
  for (int i = 0; i < encoder_count; i++) {
    checkButton(i);
  }
  
  // Blink LED to show activity
  static unsigned long last_blink = 0;
  if (millis() - last_blink > 500) {
    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state);
    last_blink = millis();
  }
  
  delay(1);
}

// Interrupt service routine for encoder 0
void encoder0_isr() {
  updateEncoder(0);
}

// Update encoder position
void updateEncoder(int enc) {
  int a = digitalRead(encoders[enc].pin_a);
  int b = digitalRead(encoders[enc].pin_b);
  
  if (a != encoders[enc].last_a || b != encoders[enc].last_b) {
    // Standard quadrature encoding
    if (encoders[enc].last_a == LOW && a == HIGH) {
      if (b == LOW) {
        encoders[enc].position++;
      } else {
        encoders[enc].position--;
      }
    }
    
    encoders[enc].last_a = a;
    encoders[enc].last_b = b;
    
    // Debug output
    if (enc == 0) {
      Serial.print("Encoder 0 position: ");
      Serial.println(encoders[enc].position);
    }
  }
}

// Check button state with debouncing
void checkButton(int enc) {
  bool current_state = !digitalRead(encoders[enc].pin_button); // Active low
  unsigned long now = millis();
  
  if (current_state != encoders[enc].button_pressed) {
    if (now - encoders[enc].last_button_change > 50) { // 50ms debounce
      encoders[enc].button_pressed = current_state;
      encoders[enc].button_changed = true;
      encoders[enc].last_button_change = now;
      
      if (current_state) {
        Serial.print("Button ");
        Serial.print(enc);
        Serial.println(" pressed");
      }
    }
  }
}

// I2C receive event
void receiveEvent(int bytes) {
  if (bytes > 0) {
    int command = Wire.read();
    
    switch (command) {
      case CMD_INIT:
        Serial.println("I2C: Init command received");
        break;
        
      case CMD_RESET_ENCODER:
        if (bytes > 1) {
          int encoder_id = Wire.read();
          if (encoder_id < encoder_count) {
            encoders[encoder_id].position = 0;
            Serial.print("I2C: Reset encoder ");
            Serial.println(encoder_id);
          }
        }
        break;
        
      default:
        Serial.print("I2C: Unknown command: ");
        Serial.println(command);
        break;
    }
  }
}

// I2C request event
void requestEvent() {
  // This is a simplified version - real implementation would need
  // to remember what was requested in the previous receive event
  
  // For now, just send encoder count
  Wire.write(encoder_count);
}

// Send encoder position (would be called based on I2C register address)
void sendEncoderPosition(int encoder_id) {
  if (encoder_id < encoder_count) {
    int32_t pos = encoders[encoder_id].position;
    Wire.write((uint8_t)(pos & 0xFF));
    Wire.write((uint8_t)((pos >> 8) & 0xFF));
    Wire.write((uint8_t)((pos >> 16) & 0xFF));
    Wire.write((uint8_t)((pos >> 24) & 0xFF));
  }
}

// Send button state
void sendButtonState(int encoder_id) {
  if (encoder_id < encoder_count) {
    uint8_t state = encoders[encoder_id].button_pressed ? 1 : 0;
    Wire.write(state);
    
    // Clear the changed flag
    encoders[encoder_id].button_changed = false;
  }
}

/*
 * Hardware Setup:
 * 
 * Encoder 0 (Main Game Selector):
 * - Pin A: Digital 2 (interrupt)
 * - Pin B: Digital 3 (interrupt)  
 * - Button: Digital 4
 * 
 * Encoder 1 (Secondary/Filter):
 * - Pin A: Digital 5
 * - Pin B: Digital 6
 * - Button: Digital 7
 * 
 * I2C Connection to MiSTer:
 * - SDA: A4 (Arduino) -> SDA (MiSTer)
 * - SCL: A5 (Arduino) -> SCL (MiSTer)
 * - GND: GND -> GND
 * - VCC: 5V -> 5V (or 3.3V)
 * 
 * Usage:
 * - Encoder 0: Browse through game collection
 * - Encoder 0 Button: Launch selected game
 * - Encoder 1: Filter by system/genre
 * - Encoder 1 Button: Toggle filter mode
 */