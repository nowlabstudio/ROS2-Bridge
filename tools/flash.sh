#!/bin/bash
# flash.sh — Flash firmware over USB without touching hardware buttons
#
# Usage:
#   tools/flash.sh [serial_port]
#
# If no port is given, auto-detects the first /dev/tty.usbmodem* device.
#
# Steps:
#   1. Sends "bridge bootsel" over serial → Pico enters BOOTSEL mode
#   2. Waits for /Volumes/RPI-RP2 to mount
#   3. Copies zephyr.uf2

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
UF2="$SCRIPT_DIR/../workspace/build/zephyr/zephyr.uf2"
VOLUME="/Volumes/RPI-RP2"

# ── Serial port ──────────────────────────────────────────────────────
if [ -n "$1" ]; then
    PORT="$1"
else
    PORT=$(ls /dev/tty.usbmodem* 2>/dev/null | head -1)
fi

if [ -z "$PORT" ]; then
    echo "ERROR: No serial port found. Pass port as argument: tools/flash.sh /dev/tty.usbmodemXXX"
    exit 1
fi

# ── UF2 file ─────────────────────────────────────────────────────────
if [ ! -f "$UF2" ]; then
    echo "ERROR: $UF2 not found. Run: make build"
    exit 1
fi

echo "Port:     $PORT"
echo "Firmware: $UF2"
echo ""

# ── Trigger BOOTSEL ──────────────────────────────────────────────────
echo "Sending 'bridge bootsel'..."
echo "bridge bootsel" > "$PORT" 2>/dev/null || true

# ── Wait for RPI-RP2 ─────────────────────────────────────────────────
echo "Waiting for $VOLUME..."
for i in $(seq 1 20); do
    if [ -d "$VOLUME" ]; then
        break
    fi
    sleep 0.5
done

if [ ! -d "$VOLUME" ]; then
    echo "ERROR: $VOLUME did not appear. Is the Pico connected via USB?"
    exit 1
fi

# ── Flash ─────────────────────────────────────────────────────────────
echo "Flashing..."
cp "$UF2" "$VOLUME/"
echo "Done. Pico is rebooting."
