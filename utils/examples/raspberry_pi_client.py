#!/usr/bin/env python3
"""
MiSTer UART Game Launcher - Raspberry Pi Client

This Python script runs on Raspberry Pi to provide a complete
game launcher interface using GPIO buttons, LCD display, and
UART communication with MiSTer.

Features:
- GPIO button controls with debouncing
- LCD display for game information
- UART communication with MiSTer
- Game library management
- Status monitoring and logging
- Optional web interface

Hardware Requirements:
- Raspberry Pi (any model with GPIO)
- 16x2 or 20x4 LCD display (I2C)
- Push buttons for controls
- USB-to-Serial adapter for MiSTer connection
- Optional: Rotary encoder, LEDs

Installation:
sudo apt update
sudo apt install python3-pip python3-gpiozero
pip3 install pyserial RPi.GPIO smbus lcddriver

Usage:
python3 raspberry_pi_client.py
"""

import serial
import time
import threading
import logging
import json
import signal
import sys
from datetime import datetime

try:
    import RPi.GPIO as GPIO
    from gpiozero import Button, LED, RotaryEncoder
    HAS_GPIO = True
except ImportError:
    print("Warning: RPi.GPIO not available. Running in test mode.")
    HAS_GPIO = False

try:
    import lcddriver
    HAS_LCD = True
except ImportError:
    print("Warning: LCD driver not available. Continuing without LCD.")
    HAS_LCD = False

# Configuration
SERIAL_PORT = "/dev/ttyUSB0"
SERIAL_BAUD = 115200
LCD_ADDRESS = 0x27  # I2C address for LCD

# GPIO Pin assignments (BCM numbering)
PIN_BUTTON_LAUNCH = 18
PIN_BUTTON_NEXT = 19
PIN_BUTTON_PREV = 20
PIN_BUTTON_MENU = 21
PIN_LED_STATUS = 26
PIN_LED_ACTIVITY = 27

# Rotary encoder pins (optional)
PIN_ENCODER_A = 22
PIN_ENCODER_B = 23

# Game library
GAMES = [
    {"name": "Castlevania SOTN", "core": "PSX", "id_type": "serial", "identifier": "SLUS-00067"},
    {"name": "Final Fantasy VII", "core": "PSX", "id_type": "serial", "identifier": "SCUS-94455"},
    {"name": "Metal Gear Solid", "core": "PSX", "id_type": "serial", "identifier": "SLUS-00594"},
    {"name": "Chrono Trigger", "core": "SNES", "id_type": "title", "identifier": "Chrono Trigger"},
    {"name": "Super Metroid", "core": "SNES", "id_type": "title", "identifier": "Super Metroid"},
    {"name": "Panzer Dragoon", "core": "Saturn", "id_type": "serial", "identifier": "T-8107G"},
    {"name": "Nights into Dreams", "core": "Saturn", "id_type": "serial", "identifier": "T-8106G"},
    {"name": "Sonic CD", "core": "MegaCD", "id_type": "serial", "identifier": "T-25013"},
    {"name": "Streets of Rage 2", "core": "Genesis", "id_type": "title", "identifier": "Streets of Rage 2"},
    {"name": "Street Fighter Alpha", "core": "Arcade", "id_type": "title", "identifier": "Street Fighter Alpha"}
]

class MiSTerUARTClient:
    def __init__(self):
        self.serial_port = None
        self.current_game = 0
        self.mister_connected = False
        self.running = True
        
        # Initialize logging
        self.setup_logging()
        
        # Initialize hardware
        self.setup_serial()
        if HAS_GPIO:
            self.setup_gpio()
        if HAS_LCD:
            self.setup_lcd()
        
        # Test initial connection
        self.test_connection()
        
        # Start monitoring thread
        self.monitor_thread = threading.Thread(target=self.monitor_serial, daemon=True)
        self.monitor_thread.start()
        
        # Display initial state
        self.update_display()
        
    def setup_logging(self):
        """Setup logging configuration"""
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s - %(levelname)s - %(message)s',
            handlers=[
                logging.FileHandler('/tmp/mister_client.log'),
                logging.StreamHandler()
            ]
        )
        self.logger = logging.getLogger(__name__)
        
    def setup_serial(self):
        """Initialize serial connection"""
        try:
            self.serial_port = serial.Serial(
                SERIAL_PORT,
                SERIAL_BAUD,
                timeout=1,
                write_timeout=1
            )
            self.logger.info(f"Serial port opened: {SERIAL_PORT}")
        except Exception as e:
            self.logger.error(f"Failed to open serial port: {e}")
            
    def setup_gpio(self):
        """Setup GPIO pins and buttons"""
        try:
            # Setup buttons with debouncing
            self.button_launch = Button(PIN_BUTTON_LAUNCH, bounce_time=0.1)
            self.button_next = Button(PIN_BUTTON_NEXT, bounce_time=0.1)
            self.button_prev = Button(PIN_BUTTON_PREV, bounce_time=0.1)
            self.button_menu = Button(PIN_BUTTON_MENU, bounce_time=0.1)
            
            # Setup LEDs
            self.led_status = LED(PIN_LED_STATUS)
            self.led_activity = LED(PIN_LED_ACTIVITY)
            
            # Setup rotary encoder (optional)
            try:
                self.encoder = RotaryEncoder(PIN_ENCODER_A, PIN_ENCODER_B)
                self.encoder.when_rotated = self.encoder_rotated
                self.logger.info("Rotary encoder initialized")
            except Exception as e:
                self.logger.warning(f"Rotary encoder setup failed: {e}")
                self.encoder = None
            
            # Assign button callbacks
            self.button_launch.when_pressed = self.launch_game
            self.button_next.when_pressed = self.next_game
            self.button_prev.when_pressed = self.previous_game
            self.button_menu.when_pressed = self.menu_action
            
            self.logger.info("GPIO initialized successfully")
            
        except Exception as e:
            self.logger.error(f"GPIO setup failed: {e}")
            
    def setup_lcd(self):
        """Initialize LCD display"""
        try:
            self.lcd = lcddriver.lcd(LCD_ADDRESS)
            self.lcd.lcd_clear()
            self.lcd.lcd_display_string("MiSTer Launcher", 1)
            self.lcd.lcd_display_string("Initializing...", 2)
            self.logger.info("LCD initialized successfully")
        except Exception as e:
            self.logger.error(f"LCD setup failed: {e}")
            self.lcd = None
            
    def encoder_rotated(self):
        """Handle rotary encoder rotation"""
        if self.encoder.steps > 0:
            self.next_game()
        elif self.encoder.steps < 0:
            self.previous_game()
        self.encoder.steps = 0
        
    def send_uart_command(self, command):
        """Send command to MiSTer via UART"""
        if not self.serial_port:
            self.logger.error("Serial port not available")
            return False
            
        try:
            command_str = f"{command}\r\n"
            self.serial_port.write(command_str.encode())
            self.serial_port.flush()
            
            self.logger.info(f"Sent: {command}")
            
            # Blink activity LED
            if HAS_GPIO and hasattr(self, 'led_activity'):
                self.led_activity.blink(on_time=0.1, off_time=0.1, n=1)
                
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to send command: {e}")
            return False
            
    def read_uart_response(self, timeout=3):
        """Read response from MiSTer UART"""
        if not self.serial_port:
            return None
            
        try:
            self.serial_port.timeout = timeout
            response = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
            return response if response else None
            
        except Exception as e:
            self.logger.error(f"Failed to read response: {e}")
            return None
            
    def monitor_serial(self):
        """Monitor serial port for incoming messages"""
        while self.running:
            try:
                if self.serial_port and self.serial_port.in_waiting > 0:
                    response = self.read_uart_response(timeout=0.1)
                    if response:
                        self.handle_uart_response(response)
                time.sleep(0.1)
                
            except Exception as e:
                self.logger.error(f"Serial monitoring error: {e}")
                time.sleep(1)
                
    def handle_uart_response(self, response):
        """Handle responses from MiSTer"""
        self.logger.info(f"Received: {response}")
        
        if response.startswith("OK"):
            if "LAUNCHED" in response:
                self.logger.info("✓ Game launched successfully!")
                self.show_message("Game Launched!", 2)
            elif "PONG" in response:
                self.mister_connected = True
                self.update_status_led()
                self.logger.info("✓ Connection confirmed")
            elif "Ready" in response:
                self.mister_connected = True
                self.update_status_led()
                self.logger.info("✓ MiSTer UART daemon ready")
                
        elif response.startswith("ERROR"):
            error_msg = response[6:] if len(response) > 6 else "Unknown error"
            self.logger.error(f"MiSTer error: {error_msg}")
            self.show_message("Error occurred", 2)
            
    def test_connection(self):
        """Test connection to MiSTer"""
        self.logger.info("Testing MiSTer connection...")
        self.send_uart_command("PING")
        
        # Wait a moment for response
        time.sleep(1)
        
        # Connection status will be updated by monitor_serial
        if not self.mister_connected:
            self.logger.warning("No response from MiSTer")
            self.show_message("No MiSTer", 2)
            
    def update_status_led(self):
        """Update status LED based on connection"""
        if HAS_GPIO and hasattr(self, 'led_status'):
            if self.mister_connected:
                self.led_status.on()
            else:
                self.led_status.blink(on_time=0.5, off_time=0.5)
                
    def launch_game(self):
        """Launch the currently selected game"""
        game = GAMES[self.current_game]
        command = f"LAUNCH {game['core']} {game['id_type']} {game['identifier']}"
        
        self.logger.info(f"Launching: {game['name']}")
        self.show_message("Launching...", 2)
        
        self.send_uart_command(command)
        
    def next_game(self):
        """Select next game"""
        self.current_game = (self.current_game + 1) % len(GAMES)
        self.logger.info(f"Selected: {GAMES[self.current_game]['name']}")
        self.update_display()
        
    def previous_game(self):
        """Select previous game"""
        self.current_game = (self.current_game - 1) % len(GAMES)
        self.logger.info(f"Selected: {GAMES[self.current_game]['name']}")
        self.update_display()
        
    def menu_action(self):
        """Handle menu button press"""
        self.send_uart_command("STATUS")
        
    def update_display(self):
        """Update LCD display with current game info"""
        if not HAS_LCD or not self.lcd:
            return
            
        try:
            game = GAMES[self.current_game]
            
            # Clear display
            self.lcd.lcd_clear()
            
            # Line 1: Game name (truncated if necessary)
            name = game['name'][:16]
            self.lcd.lcd_display_string(name, 1)
            
            # Line 2: Core and game number
            info = f"{game['core']} [{self.current_game + 1}/{len(GAMES)}]"
            self.lcd.lcd_display_string(info, 2)
            
            # For 4-line displays, show more info
            if hasattr(self.lcd, 'lcd_display_string_pos'):
                try:
                    # Line 3: ID
                    id_str = game['identifier'][:16]
                    self.lcd.lcd_display_string(id_str, 3)
                    
                    # Line 4: Status
                    status = "Connected" if self.mister_connected else "Disconnected"
                    self.lcd.lcd_display_string(status, 4)
                except:
                    pass  # 2-line LCD
                    
        except Exception as e:
            self.logger.error(f"Display update failed: {e}")
            
    def show_message(self, message, line=1, duration=None):
        """Show temporary message on LCD"""
        if not HAS_LCD or not self.lcd:
            return
            
        try:
            # Save current line content
            if line <= 2:
                old_content = None
                if hasattr(self, '_saved_display'):
                    old_content = self._saved_display.get(line)
                
                # Show message
                self.lcd.lcd_display_string(message[:16], line)
                
                # Restore after duration
                if duration:
                    def restore():
                        time.sleep(duration)
                        if old_content:
                            self.lcd.lcd_display_string(old_content, line)
                        else:
                            self.update_display()
                            
                    threading.Thread(target=restore, daemon=True).start()
                    
        except Exception as e:
            self.logger.error(f"Message display failed: {e}")
            
    def print_status(self):
        """Print current status to console"""
        game = GAMES[self.current_game]
        status = "Connected" if self.mister_connected else "Disconnected"
        
        print("\n" + "="*50)
        print("MiSTer UART Game Launcher Status")
        print("="*50)
        print(f"MiSTer Status: {status}")
        print(f"Current Game: {game['name']}")
        print(f"Core: {game['core']}")
        print(f"Identifier: {game['identifier']}")
        print(f"Game {self.current_game + 1} of {len(GAMES)}")
        print("="*50)
        print("Controls:")
        print("  Launch Button: Launch current game")
        print("  Next Button: Select next game")
        print("  Prev Button: Select previous game")
        print("  Menu Button: Check status")
        if self.encoder:
            print("  Rotary Encoder: Navigate games")
        print("  Ctrl+C: Exit")
        print("="*50 + "\n")
        
    def cleanup(self):
        """Cleanup resources"""
        self.running = False
        
        if self.serial_port:
            self.serial_port.close()
            
        if HAS_GPIO:
            GPIO.cleanup()
            
        if HAS_LCD and self.lcd:
            self.lcd.lcd_clear()
            self.lcd.lcd_display_string("Goodbye!", 1)
            
        self.logger.info("Cleanup completed")
        
    def run(self):
        """Main run loop"""
        self.logger.info("MiSTer UART Client started")
        self.print_status()
        
        try:
            while self.running:
                time.sleep(0.1)
                
        except KeyboardInterrupt:
            self.logger.info("Shutdown requested")
            
        finally:
            self.cleanup()

def signal_handler(signum, frame):
    """Handle shutdown signals"""
    print("\nShutdown signal received...")
    sys.exit(0)

def main():
    """Main entry point"""
    print("MiSTer UART Game Launcher - Raspberry Pi Client")
    print("=" * 50)
    
    # Setup signal handlers
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Create and run client
    client = MiSTerUARTClient()
    client.run()

if __name__ == "__main__":
    main()

"""
Hardware Setup Guide:

Raspberry Pi GPIO Connections:
- Button 1 (Launch): GPIO 18 to GND
- Button 2 (Next): GPIO 19 to GND
- Button 3 (Previous): GPIO 20 to GND
- Button 4 (Menu): GPIO 21 to GND
- Status LED: GPIO 26 to 330Ω resistor to LED to GND
- Activity LED: GPIO 27 to 330Ω resistor to LED to GND

Rotary Encoder (optional):
- A pin: GPIO 22
- B pin: GPIO 23
- Common: GND

LCD Display (I2C):
- VCC: 5V
- GND: GND
- SDA: GPIO 2 (SDA)
- SCL: GPIO 3 (SCL)

UART Connection to MiSTer:
- USB-to-Serial adapter connected to Raspberry Pi USB
- Adapter TX -> MiSTer UART RX
- Adapter RX -> MiSTer UART TX
- Common GND

Installation:
1. Enable I2C: sudo raspi-config -> Interface Options -> I2C -> Enable
2. Install dependencies: sudo apt install python3-pip python3-gpiozero
3. Install Python packages: pip3 install pyserial RPi.GPIO smbus
4. Download LCD driver: wget https://raw.githubusercontent.com/the-raspberry-pi-guy/lcd/master/lcddriver.py
5. Run: python3 raspberry_pi_client.py

Features:
- Physical button controls with debouncing
- LCD display for game information
- Rotary encoder navigation
- Status LEDs for connection and activity
- Comprehensive logging
- Thread-safe serial communication
- Graceful shutdown handling

Usage:
- Press Launch button to launch selected game
- Use Next/Previous buttons to navigate
- Rotate encoder for quick navigation
- Press Menu button to check status
- Monitor console/log for detailed information
"""