#!/bin/bash
# flash.sh — Flash firmware over USB without touching hardware buttons
#
# Usage:
#   tools/flash.sh [serial_port]
#
# If no port is given, auto-detects the first available serial port.
# Supports both Linux (/dev/ttyACM*) and macOS (/dev/tty.usbmodem*).
#
# Steps:
#   1. Sends "bridge bootsel" over serial → Pico enters BOOTSEL mode
#   2. Waits for the RPI-RP2 volume to mount
#   3. Copies zephyr.uf2

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
UF2="$SCRIPT_DIR/../workspace/build/zephyr/zephyr.uf2"

# ── Platform detection ────────────────────────────────────────────
OS="$(uname -s)"
if [ "$OS" = "Darwin" ]; then
    VOLUME="/Volumes/RPI-RP2"
    PORT_GLOB="/dev/tty.usbmodem*"
else
    # Linux: volume mounted by udisks2 under /media/$USER or /run/media/$USER
    VOLUME_BASE="/media/$USER/RPI-RP2"
    VOLUME_ALT="/run/media/$USER/RPI-RP2"
    PORT_GLOB="/dev/ttyACM*"
fi

# ── Serial port ──────────────────────────────────────────────────────
if [ -n "$1" ]; then
    PORT="$1"
else
    PORT=$(ls $PORT_GLOB 2>/dev/null | head -1)
fi

if [ -z "$PORT" ]; then
    if [ "$OS" = "Darwin" ]; then
        echo "ERROR: No serial port found. Pass port as argument: tools/flash.sh /dev/tty.usbmodemXXX"
    else
        echo "ERROR: No serial port found. Pass port as argument: tools/flash.sh /dev/ttyACM0"
    fi
    exit 1
fi

# ── UF2 file ─────────────────────────────────────────────────────────
if [ ! -f "$UF2" ]; then
    echo "ERROR: $UF2 not found. Run: make build"
    exit 1
fi

echo "Platform: $OS"
echo "Port:     $PORT"
echo "Firmware: $UF2"
echo ""

# ── Trigger BOOTSEL ──────────────────────────────────────────────────
echo "Sending 'bridge bootsel'..."
echo "bridge bootsel" > "$PORT" 2>/dev/null || true

# ── Wait for RPI-RP2 ─────────────────────────────────────────────────
if [ "$OS" = "Darwin" ]; then
    VOLUME="$VOLUME_BASE"
else
    # On Linux the mount point may be either path; check both
    VOLUME=""
    for i in $(seq 1 20); do
        if [ -d "$VOLUME_BASE" ]; then
            VOLUME="$VOLUME_BASE"
            break
        elif [ -d "$VOLUME_ALT" ]; then
            VOLUME="$VOLUME_ALT"
            break
        fi
        sleep 0.5
    done
fi

if [ "$OS" = "Darwin" ]; then
    echo "Waiting for $VOLUME..."
    for i in $(seq 1 20); do
        if [ -d "$VOLUME" ]; then
            break
        fi
        sleep 0.5
    done
fi

if [ -z "$VOLUME" ] || [ ! -d "$VOLUME" ]; then
    echo "ERROR: RPI-RP2 volume did not appear. Is the Pico connected via USB?"
    if [ "$OS" != "Darwin" ]; then
        echo "       Expected at: $VOLUME_BASE  or  $VOLUME_ALT"
        echo "       If it mounted elsewhere, pass the path as \$VOLUME env var."
    fi
    exit 1
fi

# ── Flash ─────────────────────────────────────────────────────────────
echo "Flashing to $VOLUME ..."
cp "$UF2" "$VOLUME/"
echo "Done. Pico is rebooting."
