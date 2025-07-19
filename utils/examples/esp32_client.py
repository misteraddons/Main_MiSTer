"""
MiSTer UART Game Launcher - ESP32 MicroPython Client

This MicroPython script runs on ESP32 to provide WiFi-enabled
game launching for MiSTer via UART interface.

Features:
- WiFi web interface for game selection
- Physical button controls
- UART communication with MiSTer
- Status monitoring and logging
- Game library management

Hardware Requirements:
- ESP32 development board
- USB-to-Serial adapter for MiSTer connection
- Optional: Buttons, OLED display, LEDs

Installation:
1. Flash MicroPython to ESP32
2. Upload this script as main.py
3. Configure WiFi credentials
4. Connect UART lines to MiSTer
"""

import machine
import network
import socket
import time
import json
from machine import Pin, UART, Timer

# Configuration
WIFI_SSID = "YourWiFiNetwork"
WIFI_PASSWORD = "YourPassword"
UART_TX_PIN = 17  # ESP32 TX to MiSTer RX
UART_RX_PIN = 16  # ESP32 RX from MiSTer TX
UART_BAUD = 115200
WEB_PORT = 80

# Pin definitions
BUTTON_LAUNCH = Pin(2, Pin.IN, Pin.PULL_UP)
BUTTON_NEXT = Pin(4, Pin.IN, Pin.PULL_UP)
BUTTON_PREV = Pin(5, Pin.IN, Pin.PULL_UP)
LED_STATUS = Pin(18, Pin.OUT)
LED_WIFI = Pin(19, Pin.OUT)

# Game library
GAMES = [
    {"name": "Castlevania SOTN", "core": "PSX", "id_type": "serial", "identifier": "SLUS-00067"},
    {"name": "Final Fantasy VII", "core": "PSX", "id_type": "serial", "identifier": "SCUS-94455"},
    {"name": "Metal Gear Solid", "core": "PSX", "id_type": "serial", "identifier": "SLUS-00594"},
    {"name": "Panzer Dragoon", "core": "Saturn", "id_type": "serial", "identifier": "T-8107G"},
    {"name": "Nights into Dreams", "core": "Saturn", "id_type": "serial", "identifier": "T-8106G"},
    {"name": "Sonic CD", "core": "MegaCD", "id_type": "serial", "identifier": "T-25013"},
    {"name": "Street Fighter Alpha", "core": "Arcade", "id_type": "title", "identifier": "Street Fighter Alpha"}
]

class MiSTerController:
    def __init__(self):
        self.uart = UART(2, baudrate=UART_BAUD, tx=UART_TX_PIN, rx=UART_RX_PIN)
        self.current_game = 0
        self.mister_connected = False
        self.last_button_time = 0
        self.debounce_delay = 200
        
        # Initialize
        self.setup_buttons()
        self.test_connection()
        
    def setup_buttons(self):
        """Setup button interrupt handlers"""
        BUTTON_LAUNCH.irq(trigger=Pin.IRQ_FALLING, handler=self.launch_button_handler)
        BUTTON_NEXT.irq(trigger=Pin.IRQ_FALLING, handler=self.next_button_handler)
        BUTTON_PREV.irq(trigger=Pin.IRQ_FALLING, handler=self.prev_button_handler)
        
    def debounce_check(self):
        """Check button debouncing"""
        current_time = time.ticks_ms()
        if time.ticks_diff(current_time, self.last_button_time) < self.debounce_delay:
            return False
        self.last_button_time = current_time
        return True
        
    def launch_button_handler(self, pin):
        """Handle launch button press"""
        if self.debounce_check():
            self.launch_current_game()
            
    def next_button_handler(self, pin):
        """Handle next button press"""
        if self.debounce_check():
            self.current_game = (self.current_game + 1) % len(GAMES)
            print(f"Selected: {GAMES[self.current_game]['name']}")
            
    def prev_button_handler(self, pin):
        """Handle previous button press"""
        if self.debounce_check():
            self.current_game = (self.current_game - 1) % len(GAMES)
            print(f"Selected: {GAMES[self.current_game]['name']}")
    
    def send_uart_command(self, command):
        """Send command to MiSTer via UART"""
        try:
            self.uart.write(f"{command}\r\n")
            print(f"Sent: {command}")
            return True
        except Exception as e:
            print(f"UART Error: {e}")
            return False
    
    def read_uart_response(self, timeout=3000):
        """Read response from MiSTer UART"""
        start_time = time.ticks_ms()
        response = ""
        
        while time.ticks_diff(time.ticks_ms(), start_time) < timeout:
            if self.uart.any():
                data = self.uart.read()
                if data:
                    response += data.decode('utf-8', errors='ignore')
                    if '\n' in response:
                        return response.strip()
            time.sleep_ms(10)
        
        return None
    
    def test_connection(self):
        """Test connection to MiSTer"""
        print("Testing MiSTer connection...")
        self.send_uart_command("PING")
        
        response = self.read_uart_response()
        if response and "PONG" in response:
            self.mister_connected = True
            LED_STATUS.on()
            print("✓ Connected to MiSTer!")
        else:
            self.mister_connected = False
            LED_STATUS.off()
            print("✗ No response from MiSTer")
    
    def launch_current_game(self):
        """Launch the currently selected game"""
        game = GAMES[self.current_game]
        command = f"LAUNCH {game['core']} {game['id_type']} {game['identifier']}"
        
        print(f"Launching: {game['name']}")
        self.send_uart_command(command)
        
        response = self.read_uart_response()
        if response and "OK LAUNCHED" in response:
            print("✓ Game launched successfully!")
            # Blink status LED
            for _ in range(3):
                LED_STATUS.off()
                time.sleep_ms(100)
                LED_STATUS.on()
                time.sleep_ms(100)
        else:
            print(f"✗ Launch failed: {response}")
    
    def get_status(self):
        """Get MiSTer system status"""
        self.send_uart_command("STATUS")
        return self.read_uart_response()
    
    def get_current_game_info(self):
        """Get current game information"""
        return GAMES[self.current_game]

class WiFiWebServer:
    def __init__(self, controller):
        self.controller = controller
        self.socket = None
        
    def connect_wifi(self):
        """Connect to WiFi network"""
        wlan = network.WLAN(network.STA_IF)
        wlan.active(True)
        
        if not wlan.isconnected():
            print(f"Connecting to {WIFI_SSID}...")
            wlan.connect(WIFI_SSID, WIFI_PASSWORD)
            
            timeout = 0
            while not wlan.isconnected() and timeout < 20:
                time.sleep(1)
                timeout += 1
                print(".", end="")
            
            print()
            
        if wlan.isconnected():
            LED_WIFI.on()
            ip = wlan.ifconfig()[0]
            print(f"✓ WiFi connected! IP: {ip}")
            return ip
        else:
            LED_WIFI.off()
            print("✗ WiFi connection failed")
            return None
    
    def generate_web_page(self):
        """Generate HTML web interface"""
        current_game = self.controller.get_current_game_info()
        status = "Connected" if self.controller.mister_connected else "Disconnected"
        
        html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <title>MiSTer Game Launcher</title>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <style>
                body {{ font-family: Arial; margin: 20px; background: #f0f0f0; }}
                .container {{ max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }}
                .status {{ padding: 10px; margin: 10px 0; border-radius: 5px; }}
                .connected {{ background: #d4edda; border: 1px solid #c3e6cb; }}
                .disconnected {{ background: #f8d7da; border: 1px solid #f5c6cb; }}
                .game {{ padding: 15px; margin: 10px 0; background: #e9ecef; border-radius: 5px; }}
                .current {{ background: #d1ecf1; border: 2px solid #bee5eb; }}
                button {{ padding: 10px 20px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; }}
                .launch {{ background: #28a745; color: white; font-size: 16px; }}
                .nav {{ background: #007bff; color: white; }}
                .status-btn {{ background: #6c757d; color: white; }}
            </style>
        </head>
        <body>
            <div class="container">
                <h1>🎮 MiSTer Game Launcher</h1>
                
                <div class="status {'connected' if self.controller.mister_connected else 'disconnected'}">
                    <strong>Status:</strong> {status}
                </div>
                
                <div class="game current">
                    <h3>Currently Selected:</h3>
                    <strong>{current_game['name']}</strong><br>
                    Core: {current_game['core']}<br>
                    ID: {current_game['identifier']}
                </div>
                
                <div style="text-align: center;">
                    <button class="launch" onclick="launchGame()">🚀 Launch Game</button><br>
                    <button class="nav" onclick="prevGame()">◀ Previous</button>
                    <button class="nav" onclick="nextGame()">Next ▶</button><br>
                    <button class="status-btn" onclick="checkStatus()">📊 Status</button>
                </div>
                
                <h3>Available Games:</h3>
        """
        
        for i, game in enumerate(GAMES):
            current_class = "current" if i == self.controller.current_game else ""
            html += f"""
                <div class="game {current_class}" onclick="selectGame({i})">
                    <strong>{game['name']}</strong><br>
                    {game['core']} - {game['identifier']}
                </div>
            """
        
        html += """
                <div id="response" style="margin-top: 20px; padding: 10px; background: #f8f9fa; border-radius: 5px; display: none;"></div>
            </div>
            
            <script>
                function makeRequest(action, data = {}) {
                    fetch(`/${action}`, {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify(data)
                    })
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('response').style.display = 'block';
                        document.getElementById('response').innerHTML = `<strong>Response:</strong> ${data.message}`;
                        if (data.reload) setTimeout(() => location.reload(), 1000);
                    })
                    .catch(error => {
                        document.getElementById('response').style.display = 'block';
                        document.getElementById('response').innerHTML = `<strong>Error:</strong> ${error}`;
                    });
                }
                
                function launchGame() { makeRequest('launch'); }
                function nextGame() { makeRequest('next', {reload: true}); }
                function prevGame() { makeRequest('prev', {reload: true}); }
                function selectGame(index) { makeRequest('select', {game: index, reload: true}); }
                function checkStatus() { makeRequest('status'); }
            </script>
        </body>
        </html>
        """
        
        return html
    
    def handle_request(self, client_socket, request):
        """Handle HTTP requests"""
        try:
            lines = request.split('\n')
            method_line = lines[0]
            path = method_line.split()[1]
            
            if path == '/':
                # Serve main page
                response = self.generate_web_page()
                client_socket.send("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n")
                client_socket.send(response)
                
            elif path.startswith('/launch'):
                self.controller.launch_current_game()
                response = json.dumps({"message": "Game launch command sent"})
                client_socket.send("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n")
                client_socket.send(response)
                
            elif path.startswith('/next'):
                self.controller.current_game = (self.controller.current_game + 1) % len(GAMES)
                response = json.dumps({"message": "Next game selected", "reload": True})
                client_socket.send("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n")
                client_socket.send(response)
                
            elif path.startswith('/prev'):
                self.controller.current_game = (self.controller.current_game - 1) % len(GAMES)
                response = json.dumps({"message": "Previous game selected", "reload": True})
                client_socket.send("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n")
                client_socket.send(response)
                
            elif path.startswith('/status'):
                status = self.controller.get_status()
                response = json.dumps({"message": f"Status: {status}"})
                client_socket.send("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n")
                client_socket.send(response)
                
            else:
                client_socket.send("HTTP/1.1 404 Not Found\r\n\r\n")
                
        except Exception as e:
            print(f"Request error: {e}")
            client_socket.send("HTTP/1.1 500 Internal Server Error\r\n\r\n")
    
    def start_server(self):
        """Start the web server"""
        ip = self.connect_wifi()
        if not ip:
            return
            
        self.socket = socket.socket()
        self.socket.bind(('', WEB_PORT))
        self.socket.listen(5)
        
        print(f"Web server started at http://{ip}:{WEB_PORT}")
        
        while True:
            try:
                client_socket, addr = self.socket.accept()
                print(f"Connection from {addr}")
                
                request = client_socket.recv(1024).decode('utf-8')
                self.handle_request(client_socket, request)
                
                client_socket.close()
                
            except Exception as e:
                print(f"Server error: {e}")
                time.sleep(1)

def main():
    """Main application entry point"""
    print("ESP32 MiSTer Game Launcher v1.0")
    print("================================")
    
    # Initialize controller
    controller = MiSTerController()
    
    # Initialize web server
    web_server = WiFiWebServer(controller)
    
    # Start web server (this will run indefinitely)
    try:
        web_server.start_server()
    except KeyboardInterrupt:
        print("\nShutting down...")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        LED_STATUS.off()
        LED_WIFI.off()

if __name__ == "__main__":
    main()

"""
Hardware Setup:

ESP32 to MiSTer UART Connection:
- ESP32 GPIO 17 (TX) -> MiSTer UART RX
- ESP32 GPIO 16 (RX) -> MiSTer UART TX  
- ESP32 GND -> MiSTer GND

Button Connections:
- Button 1 (Launch): GPIO 2 to GND
- Button 2 (Next): GPIO 4 to GND
- Button 3 (Previous): GPIO 5 to GND

LED Indicators:
- Status LED: GPIO 18
- WiFi LED: GPIO 19

Configuration:
1. Update WIFI_SSID and WIFI_PASSWORD
2. Adjust GPIO pins if needed
3. Modify GAMES list for your library
4. Upload to ESP32 as main.py

Usage:
1. Power on ESP32
2. Connect to WiFi network
3. Open web browser to ESP32 IP address
4. Use web interface or physical buttons
5. Monitor serial output for debugging

Features:
- Web interface for remote control
- Physical button controls
- Real-time status monitoring
- Game library management
- WiFi connectivity indicators
"""