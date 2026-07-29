#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="${BASH_SOURCE[0]}"
BAUD="${ESP_BAUD:-460800}"
PORT="${1:-${ESPPORT:-}}"

ESPTOOL="$SCRIPT_DIR/tools/esptool/esptool"
if [ ! -x "$ESPTOOL" ]; then
  if command -v esptool >/dev/null 2>&1; then
    ESPTOOL="$(command -v esptool)"
  elif command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL="$(command -v esptool.py)"
  else
    echo "No bundled esptool and no esptool on PATH." >&2
    exit 1
  fi
fi

FW="$SCRIPT_DIR/firmware/esp32s3"
for file in bootloader.bin partition-table.bin indicator_ha.bin; do
  if [ ! -f "$FW/$file" ]; then
    echo "Missing ESP32-S3 firmware file: $FW/$file" >&2
    exit 1
  fi
done

PORT_ARGS=()
if [ -n "$PORT" ]; then
  PORT_ARGS=(--port "$PORT")
else
  echo "No ESP32-S3 port supplied; esptool will autodetect."
  echo "To pin a port, run: ESPPORT=/dev/cu.usbmodemXXXX $SCRIPT_PATH"
fi

"$ESPTOOL" --chip esp32s3 "${PORT_ARGS[@]:-}" --baud "$BAUD" \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 "$FW/bootloader.bin" \
  0x8000 "$FW/partition-table.bin" \
  0x10000 "$FW/indicator_ha.bin"
