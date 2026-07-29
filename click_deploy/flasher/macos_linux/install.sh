#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

chmod +x "$SCRIPT_DIR"/*.sh

# Make any bundled tools executable if present (bundle-local or repo-level).
for tools_dir in "$SCRIPT_DIR/tools" "$SCRIPT_DIR/../../tools"; do
  if [ -d "$tools_dir" ]; then
    find "$tools_dir" -type f \( -name esptool -o -name picotool \) -exec chmod +x {} \; 2>/dev/null || true
  fi
done

echo "Flashing scripts are ready."
echo "Run: ./flash_all.sh"
