#!/usr/bin/env python3
"""
MiSTer Network Game Launcher - Python Client Example

This example demonstrates how to integrate MiSTer game launching
into your own Python applications.
"""

import requests
import json
import argparse
import sys
from typing import Dict, Any, Optional

class MiSTerClient:
    """Client for communicating with MiSTer Network Daemon"""
    
    def __init__(self, host: str = "192.168.1.100", port: int = 8080, api_key: Optional[str] = None):
        self.base_url = f"http://{host}:{port}"
        self.api_key = api_key
        self.session = requests.Session()
        
        # Set default headers
        self.session.headers.update({
            'Content-Type': 'application/json',
            'User-Agent': 'MiSTer-Python-Client/1.0'
        })
        
        if api_key:
            self.session.headers.update({
                'Authorization': api_key
            })
    
    def get_status(self) -> Dict[str, Any]:
        """Get system status"""
        try:
            response = self.session.get(f"{self.base_url}/status")
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            raise ConnectionError(f"Failed to get status: {e}")
    
    def launch_game(self, core: str, id_type: str, identifier: str) -> Dict[str, Any]:
        """Launch a game"""
        payload = {
            "core": core,
            "id_type": id_type,
            "identifier": identifier
        }
        
        try:
            response = self.session.post(f"{self.base_url}/launch", json=payload)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            if hasattr(e, 'response') and e.response:
                try:
                    error_data = e.response.json()
                    raise ValueError(f"Launch failed: {error_data.get('error', 'Unknown error')}")
                except json.JSONDecodeError:
                    raise ValueError(f"Launch failed: HTTP {e.response.status_code}")
            else:
                raise ConnectionError(f"Failed to launch game: {e}")
    
    def get_api_info(self) -> Dict[str, Any]:
        """Get API information"""
        try:
            response = self.session.get(f"{self.base_url}/api")
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            raise ConnectionError(f"Failed to get API info: {e}")
    
    def test_connection(self) -> bool:
        """Test if MiSTer is reachable"""
        try:
            status = self.get_status()
            return status.get('status') == 'running'
        except:
            return False

def main():
    parser = argparse.ArgumentParser(description='MiSTer Network Game Launcher Client')
    parser.add_argument('--host', default='192.168.1.100', help='MiSTer IP address')
    parser.add_argument('--port', type=int, default=8080, help='MiSTer port')
    parser.add_argument('--api-key', help='API key for authentication')
    
    subparsers = parser.add_subparsers(dest='command', help='Available commands')
    
    # Status command
    subparsers.add_parser('status', help='Get system status')
    
    # Launch command
    launch_parser = subparsers.add_parser('launch', help='Launch a game')
    launch_parser.add_argument('--core', required=True, help='Core name (PSX, Saturn, etc.)')
    launch_parser.add_argument('--id-type', required=True, choices=['serial', 'title'], 
                              help='Identifier type')
    launch_parser.add_argument('--identifier', required=True, help='Game identifier')
    
    # Info command
    subparsers.add_parser('info', help='Get API information')
    
    # Test command
    subparsers.add_parser('test', help='Test connection')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return 1
    
    # Create client
    client = MiSTerClient(args.host, args.port, args.api_key)
    
    try:
        if args.command == 'status':
            status = client.get_status()
            print("MiSTer Status:")
            print(f"  Status: {status.get('status')}")
            print(f"  Game Launcher Available: {status.get('game_launcher_available')}")
            print(f"  Port: {status.get('port')}")
            print(f"  CORS Enabled: {status.get('cors_enabled')}")
            print(f"  Auth Required: {status.get('auth_required')}")
            
        elif args.command == 'launch':
            print(f"Launching {args.core} game: {args.identifier}")
            result = client.launch_game(args.core, args.id_type, args.identifier)
            print(f"Success: {result.get('message')}")
            
        elif args.command == 'info':
            info = client.get_api_info()
            print("API Information:")
            print(f"  Name: {info.get('name')}")
            print(f"  Version: {info.get('version')}")
            print("  Endpoints:")
            for endpoint, description in info.get('endpoints', {}).items():
                print(f"    {endpoint}: {description}")
            
        elif args.command == 'test':
            if client.test_connection():
                print("✓ Connection successful!")
                status = client.get_status()
                print(f"  MiSTer is running on port {status.get('port')}")
            else:
                print("✗ Connection failed!")
                print(f"  Could not reach MiSTer at {args.host}:{args.port}")
                return 1
                
    except (ConnectionError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    
    return 0

# Example usage as a library
def example_usage():
    """Example of how to use this as a library"""
    
    # Create client
    client = MiSTerClient("192.168.1.100")
    
    # Test connection
    if not client.test_connection():
        print("MiSTer not reachable!")
        return
    
    # Get status
    status = client.get_status()
    print(f"MiSTer status: {status['status']}")
    
    # Launch some games
    games = [
        ("PSX", "serial", "SLUS-00067"),  # Castlevania SOTN
        ("PSX", "serial", "SCUS-94455"),  # Final Fantasy VII
        ("Saturn", "serial", "T-8107G"),  # Panzer Dragoon
        ("PSX", "title", "Metal Gear Solid"),  # By title
    ]
    
    for core, id_type, identifier in games:
        try:
            result = client.launch_game(core, id_type, identifier)
            print(f"✓ Launched {identifier}: {result['message']}")
        except ValueError as e:
            print(f"✗ Failed to launch {identifier}: {e}")

if __name__ == '__main__':
    sys.exit(main())