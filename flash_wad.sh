#!/usr/bin/env bash
# Flashes the Doom WAD file directly into the "wad" partition (see partitions.csv).
# This is separate from `idf.py flash` because the WAD is raw data, not part of
# the app image - it only needs to be (re)flashed once, or whenever you swap WADs.
#
# Usage: ./flash_wad.sh [/dev/tty.usbmodemXXXX] [path/to.wad]

set -euo pipefail

PORT="${1:-/dev/tty.usbmodem2101}"
WAD_FILE="${2:-doom1-cut.wad}"
WAD_OFFSET=0x190000   # must match the "wad" partition's offset in partitions.csv

if [ ! -f "$WAD_FILE" ]; then
    echo "WAD file not found: $WAD_FILE"
    exit 1
fi

python -m esptool --chip esp32s3 --port "$PORT" --baud 921600 \
    --before default-reset --after hard-reset \
    write-flash "$WAD_OFFSET" "$WAD_FILE"

echo "Flashed $WAD_FILE to $PORT at offset $WAD_OFFSET"
