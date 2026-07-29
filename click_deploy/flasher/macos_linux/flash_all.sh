#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/flash_rp2040.sh"
"$SCRIPT_DIR/flash_esp32s3.sh"

echo "Both firmware images have been flashed."
