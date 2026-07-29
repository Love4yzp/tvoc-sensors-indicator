#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

chmod +x "$SCRIPT_DIR"/*.sh

if [ -d "$SCRIPT_DIR/tools" ]; then
  find "$SCRIPT_DIR/tools" -type f \( -name esptool -o -name picotool \) -exec chmod +x {} \; 2>/dev/null || true
fi

echo "Flashing scripts are ready."
echo "Run: ./flash_all.sh"
