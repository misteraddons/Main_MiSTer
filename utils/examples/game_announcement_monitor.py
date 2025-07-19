#!/usr/bin/env python3
"""
MiSTer Game Announcement Monitor

This example demonstrates how to receive real-time game announcements
from MiSTer via UART or network interfaces.

Features:
- Monitors UART for game announcements
- Parses game change notifications
- Displays current game information
- Can be extended for custom notifications (LCD, LEDs, webhooks, etc.)
"""

import serial
import time
import threading
import argparse
import sys
import re
from datetime import datetime

class GameAnnouncementMonitor:
    def __init__(self, serial_port=None, baud_rate=115200):
        self.serial_port = serial_port
        self.baud_rate = baud_rate
        self.ser = None
        self.running = False
        self.current_game = None
        
    def connect_serial(self):
        """Connect to UART interface"""
        try:
            self.ser = serial.Serial(self.serial_port, self.baud_rate, timeout=1)
            print(f"✓ Connected to {self.serial_port} at {self.baud_rate} baud")
            time.sleep(2)  # Wait for connection to stabilize
            return True
        except Exception as e:
            print(f"✗ Failed to connect to {self.serial_port}: {e}")
            return False
    
    def parse_game_announcement(self, message):
        """Parse game announcement message"""
        game_info = {}
        
        if message.startswith("GAME_CHANGED"):
            # Format: GAME_CHANGED <core> "<game_name>" "<file_path>"
            match = re.match(r'GAME_CHANGED\s+(\S+)\s+"([^"]+)"\s+"([^"]+)"', message)
            if match:
                game_info = {
                    'event': 'game_changed',
                    'core': match.group(1),
                    'name': match.group(2),
                    'path': match.group(3),
                    'timestamp': datetime.now()
                }
                
        elif message.startswith("GAME_STOPPED"):
            game_info = {
                'event': 'game_stopped',
                'timestamp': datetime.now()
            }
            
        elif message.startswith("GAME_DETAILS"):
            # Format: GAME_DETAILS core="PSX" name="Game Name" path="..." serial="..." timestamp=...
            game_info = {'event': 'game_details'}
            
            # Parse key=value pairs
            matches = re.findall(r'(\w+)="([^"]*)"', message)
            for key, value in matches:
                game_info[key] = value
                
            # Parse timestamp
            timestamp_match = re.search(r'timestamp=(\d+)', message)
            if timestamp_match:
                game_info['timestamp'] = datetime.fromtimestamp(int(timestamp_match.group(1)))
        
        return game_info if game_info else None
    
    def handle_game_announcement(self, game_info):
        """Handle parsed game announcement"""
        if not game_info:
            return
            
        timestamp = game_info.get('timestamp', datetime.now()).strftime('%H:%M:%S')
        
        if game_info['event'] == 'game_changed':
            self.current_game = game_info
            print(f"\n🎮 [{timestamp}] Game Changed!")
            print(f"   Core: {game_info['core']}")
            print(f"   Game: {game_info['name']}")
            print(f"   Path: {game_info['path']}")
            
            # Custom notification examples
            self.on_game_changed(game_info)
            
        elif game_info['event'] == 'game_stopped':
            print(f"\n⏹️  [{timestamp}] Game Stopped")
            self.current_game = None
            self.on_game_stopped()
            
        elif game_info['event'] == 'game_details':
            print(f"\n📋 [{timestamp}] Game Details:")
            for key, value in game_info.items():
                if key not in ['event', 'timestamp']:
                    print(f"   {key.title()}: {value}")
    
    def on_game_changed(self, game_info):
        """Custom handler for game changes - override in subclass"""
        # Example: Send webhook notification
        # requests.post('http://your-server/webhook', json=game_info)
        
        # Example: Update LCD display
        # lcd.display(f"Now Playing:\n{game_info['name']}")
        
        # Example: Light up LEDs
        # led_controller.set_color_for_core(game_info['core'])
        
        pass
    
    def on_game_stopped(self):
        """Custom handler for game stop - override in subclass"""
        # Example: Clear display
        # lcd.clear()
        
        # Example: Turn off LEDs
        # led_controller.turn_off()
        
        pass
    
    def monitor_serial(self):
        """Monitor serial port for announcements"""
        print(f"📡 Monitoring {self.serial_port} for game announcements...")
        print("Press Ctrl+C to stop\n")
        
        while self.running:
            try:
                if self.ser and self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    if line.startswith("GAME_"):
                        game_info = self.parse_game_announcement(line)
                        self.handle_game_announcement(game_info)
                    elif line and not line.startswith("OK"):
                        # Show other messages for debugging
                        timestamp = datetime.now().strftime('%H:%M:%S')
                        print(f"[{timestamp}] {line}")
                        
            except Exception as e:
                print(f"Error reading serial: {e}")
                time.sleep(1)
            
            time.sleep(0.1)
    
    def send_test_command(self, command):
        """Send a test command to MiSTer"""
        if not self.ser:
            print("Not connected to serial port")
            return
            
        try:
            self.ser.write(f"{command}\r\n".encode())
            time.sleep(0.5)
            
            # Read response
            if self.ser.in_waiting > 0:
                response = self.ser.readline().decode('utf-8', errors='ignore').strip()
                print(f"Response: {response}")
                
        except Exception as e:
            print(f"Error sending command: {e}")
    
    def start(self):
        """Start monitoring"""
        if self.serial_port:
            if not self.connect_serial():
                return False
                
            # Test connection
            print("Testing connection...")
            self.send_test_command("PING")
            
        self.running = True
        
        try:
            if self.ser:
                self.monitor_serial()
        except KeyboardInterrupt:
            print("\n\nShutting down...")
        finally:
            self.stop()
            
        return True
    
    def stop(self):
        """Stop monitoring"""
        self.running = False
        if self.ser:
            self.ser.close()

def main():
    parser = argparse.ArgumentParser(description='MiSTer Game Announcement Monitor')
    parser.add_argument('--port', '-p', default='/dev/ttyUSB0', 
                       help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('--baud', '-b', type=int, default=115200,
                       help='Baud rate (default: 115200)')
    parser.add_argument('--test', '-t', action='store_true',
                       help='Send test commands')
    
    args = parser.parse_args()
    
    print("MiSTer Game Announcement Monitor")
    print("=" * 40)
    
    monitor = GameAnnouncementMonitor(args.port, args.baud)
    
    if args.test:
        # Test mode - send some commands
        if monitor.connect_serial():
            print("Test mode - sending commands...")
            monitor.send_test_command("PING")
            monitor.send_test_command("STATUS")
            time.sleep(1)
            monitor.stop()
        return
    
    # Normal monitoring mode
    if not monitor.start():
        sys.exit(1)

class CustomGameMonitor(GameAnnouncementMonitor):
    """Example of custom game monitor with additional features"""
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.game_history = []
        self.play_count = {}
    
    def on_game_changed(self, game_info):
        """Custom game change handler"""
        # Track game history
        self.game_history.append(game_info.copy())
        
        # Count plays
        game_name = game_info['name']
        self.play_count[game_name] = self.play_count.get(game_name, 0) + 1
        
        # Show play count
        print(f"   Play count: {self.play_count[game_name]}")
        
        # Example: Log to file
        with open('/tmp/mister_game_log.txt', 'a') as f:
            timestamp = game_info['timestamp'].strftime('%Y-%m-%d %H:%M:%S')
            f.write(f"{timestamp} - {game_info['core']} - {game_info['name']}\n")
        
        # Example: Send notification to home automation
        # if game_info['core'] == 'PSX':
        #     requests.post('http://homeassistant.local/webhook/psx_game_started')

if __name__ == "__main__":
    main()

"""
Usage Examples:

# Basic monitoring
python3 game_announcement_monitor.py

# Custom serial port
python3 game_announcement_monitor.py --port /dev/ttyACM0

# Test connection
python3 game_announcement_monitor.py --test

# Use custom monitor class
monitor = CustomGameMonitor('/dev/ttyUSB0')
monitor.start()

Integration Ideas:
- Home automation (Home Assistant, OpenHAB)
- Discord/Slack notifications
- LCD/OLED display updates
- LED strip control
- Streaming overlay updates
- Game time tracking
- Achievement systems
- Social media posting
"""