#!/usr/bin/env python3
"""
Morning Board provisioning tool.

Reads WiFi credentials and feed URL from a local .env file and sends
them to the Morning Board over USB serial.

Usage:
    python3 provision.py [--port /dev/ttyACM0] [--env provision.env]

The .env file should contain:
    WIFI_SSID=YourWiFiName
    WIFI_PASS=YourWiFiPassword
    FEED_URL=https://example.com/feed.json
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Install with: pip3 install pyserial")
    sys.exit(1)


def load_env_file(path):
    """Load key=value pairs from a .env file."""
    config = {}
    try:
        with open(path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' in line:
                    key, value = line.split('=', 1)
                    config[key.strip()] = value.strip()
    except FileNotFoundError:
        print(f"Error: File not found: {path}")
        sys.exit(1)
    return config


def mask_command(cmd):
    """Never echo the WiFi password: mask anything after SET WIFI_PASS=."""
    if cmd.startswith("SET WIFI_PASS="):
        return "SET WIFI_PASS=******"
    return cmd


def send_command(ser, cmd, secret=None):
    """Send a command and wait for response. Output never contains `secret`."""
    print(f"Sending: {mask_command(cmd)}")
    ser.write((cmd + '\n').encode())
    time.sleep(0.1)
    
    # Read response
    response = []
    timeout = time.time() + 1.0
    while time.time() < timeout:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                response.append(line)
        time.sleep(0.05)
    
    for line in response:
        if secret:
            line = line.replace(secret, "******")
        print(f"  <- {line}")
    return response


def provision(port, env_path):
    """Provision the Morning Board."""
    config = load_env_file(env_path)
    
    required = ['WIFI_SSID', 'WIFI_PASS', 'FEED_URL']
    for key in required:
        if key not in config:
            print(f"Error: {key} not found in {env_path}")
            sys.exit(1)
    
    try:
        print(f"Connecting to {port}...")
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)  # Wait for board to settle
        
        # Clear any buffered data
        ser.reset_input_buffer()
        
        print("\nProvisioning Morning Board...")
        
        # Send configuration
        pw = config['WIFI_PASS']
        send_command(ser, f"SET WIFI_SSID={config['WIFI_SSID']}", pw)
        send_command(ser, f"SET WIFI_PASS={pw}", pw)
        send_command(ser, f"SET FEED_URL={config['FEED_URL']}", pw)
        
        print("\nVerifying configuration...")
        send_command(ser, "SHOW", pw)
        
        print("\nProvisioning complete!")
        print("To reboot the board, send: REBOOT")
        print("Or press the reset button on the device.")
        
        # Ask if user wants to reboot
        response = input("\nReboot now? [y/N]: ")
        if response.lower() == 'y':
            send_command(ser, "REBOOT", pw)
            print("Board rebooting...")
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"Error: Could not open serial port: {e}")
        print("\nCommon ports:")
        print("  macOS: /dev/cu.usbmodem* or /dev/tty.usbmodem*")
        print("  Linux: /dev/ttyACM0 or /dev/ttyUSB0")
        print("  Windows: COM3, COM4, etc.")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description='Provision Morning Board over USB serial',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Example .env file:
    WIFI_SSID=MyHomeNetwork
    WIFI_PASS=MySecretPassword
    FEED_URL=https://gist.githubusercontent.com/.../feed.json
'''
    )
    parser.add_argument(
        '--port',
        default='/dev/ttyACM0',
        help='Serial port (default: /dev/ttyACM0; macOS: /dev/cu.usbmodem101)'
    )
    parser.add_argument(
        '--env',
        default='provision.env',
        help='Path to .env file (default: provision.env)'
    )
    
    args = parser.parse_args()
    provision(args.port, args.env)


if __name__ == '__main__':
    main()
