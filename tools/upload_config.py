#!/usr/bin/env python3
"""
upload_config.py — Bridge config uploader
==========================================
Reads config.json and uploads it to the Pico via serial shell commands,
then saves it to flash.

Usage:
    python3 tools/upload_config.py
    python3 tools/upload_config.py --port /dev/tty.usbmodem231401
    python3 tools/upload_config.py --config app/config.json --port /dev/tty.usbmodem231401
"""

import argparse
import json
import sys
import time
import serial
import serial.tools.list_ports

# ------------------------------------------------------------------ #
#  Arguments                                                           #
# ------------------------------------------------------------------ #

parser = argparse.ArgumentParser(description="Bridge config uploader")
parser.add_argument("--port",   default=None,              help="Serial port (e.g. /dev/tty.usbmodem23301)")
parser.add_argument("--baud",   default=115200, type=int,  help="Baud rate (default: 115200)")
parser.add_argument("--config", default="app/config.json", help="Path to config.json")
args = parser.parse_args()


# ------------------------------------------------------------------ #
#  Auto-detect serial port                                             #
# ------------------------------------------------------------------ #

def find_pico_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if "usbmodem" in p.device or "ACM" in p.device:
            return p.device
    return None


port = args.port
if not port:
    port = find_pico_port()
    if not port:
        print("ERROR: No Pico serial port found.")
        print("Connect the Pico and specify --port.")
        sys.exit(1)
    print(f"Port auto-detected: {port}")


# ------------------------------------------------------------------ #
#  Load JSON config                                                    #
# ------------------------------------------------------------------ #

try:
    with open(args.config, "r") as f:
        cfg = json.load(f)
except FileNotFoundError:
    print(f"ERROR: {args.config} not found")
    sys.exit(1)
except json.JSONDecodeError as e:
    print(f"ERROR: JSON syntax error: {e}")
    sys.exit(1)

# Extract key-value pairs (only keys known to the bridge shell)
dhcp_val = cfg.get("network", {}).get("dhcp")

KNOWN_KEYS = {
    "network.dhcp":        "true" if dhcp_val is True else ("false" if dhcp_val is False else None),
    "network.mac":         cfg.get("network", {}).get("mac", ""),
    "network.ip":          cfg.get("network", {}).get("ip"),
    "network.netmask":     cfg.get("network", {}).get("netmask"),
    "network.gateway":     cfg.get("network", {}).get("gateway"),
    "network.agent_ip":    cfg.get("network", {}).get("agent_ip"),
    "network.agent_port":  str(cfg.get("network", {}).get("agent_port", "")),
    "ros.node_name":       cfg.get("ros", {}).get("node_name"),
    "ros.namespace":       cfg.get("ros", {}).get("namespace"),
}

# Channel entries — supports both simple and extended format
channels = cfg.get("channels", {})
if isinstance(channels, dict):
    for ch_name, val in channels.items():
        if isinstance(val, dict):
            enabled = val.get("enabled", True)
            KNOWN_KEYS[f"channels.{ch_name}"] = "true" if enabled else "false"
            topic = val.get("topic", "")
            if topic:
                KNOWN_KEYS[f"channels.{ch_name}.topic"] = topic
        else:
            KNOWN_KEYS[f"channels.{ch_name}"] = "true" if val else "false"

# RC trim entries: "rc_trim.ch1_min" = "1000" etc.
rc_trim = cfg.get("rc_trim", {})
if isinstance(rc_trim, dict):
    for field, val in rc_trim.items():
        KNOWN_KEYS[f"rc_trim.{field}"] = str(val)

print("\nConfig to upload:")
for k, v in KNOWN_KEYS.items():
    if v:
        print(f"  {k} = {v}")


# ------------------------------------------------------------------ #
#  Serial connection and upload                                        #
# ------------------------------------------------------------------ #

def send_cmd(ser, cmd, wait=0.3):
    """Send a command and return the response."""
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    response = ""
    while ser.in_waiting:
        response += ser.read(ser.in_waiting).decode(errors="replace")
        time.sleep(0.05)
    return response.strip()


print(f"\nConnecting: {port} @ {args.baud}...")
try:
    ser = serial.Serial(port, args.baud, timeout=2)
    ser.dtr = True
    time.sleep(1.0)
except serial.SerialException as e:
    print(f"ERROR: Could not connect: {e}")
    sys.exit(1)

# Flush input buffer
ser.reset_input_buffer()
time.sleep(0.5)

print("Setting config values...")
errors = 0

for key, value in KNOWN_KEYS.items():
    if not value:
        continue
    cmd = f"bridge config set {key} {value}"
    resp = send_cmd(ser, cmd)
    if "OK" in resp:
        print(f"  ✓ {key} = {value}")
    else:
        print(f"  ✗ {key} = {value}  (response: {resp!r})")
        errors += 1

# Save to flash
print("\nSaving to flash...")
resp = send_cmd(ser, "bridge config save", wait=1.0)
if "saved" in resp.lower() or "save" in resp.lower():
    print("  ✓ Saved")
else:
    print(f"  ? Response: {resp!r}")

# Verify
print("\nVerification:")
resp = send_cmd(ser, "bridge config show", wait=0.5)
print(resp)

if errors:
    print(f"\n{errors} error(s) occurred. Check the output above.")
    ser.close()
    sys.exit(1)

# Reboot
print("\nRebooting bridge...")
send_cmd(ser, "bridge reboot", wait=0.5)
ser.close()
print("✓ Config uploaded and bridge rebooted!")
