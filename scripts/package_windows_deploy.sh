#!/usr/bin/env bash
# Package a self-contained Windows flashing bundle (click_deploy.zip):
# firmware images + bundled Windows esptool + flash scripts. The RP2040 needs
# no tool: it is flashed by copying firmware.uf2 onto its RPI-RP2 BOOTSEL drive.
#
# Usage:
#   scripts/package_windows_deploy.sh [--build] [--keep] [-o OUTPUT.zip]
#
#   --build   rebuild firmware first (./dev build + ./dev rp2040 build)
#   --keep    keep populated firmware/tools in click_deploy/ afterwards
#             (default: clean them again so scripts/test_click_deploy_package.py
#             stays green on the checkout)
#   -o        output zip path (default: click_deploy.zip at the repo root)
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
DEPLOY_DIR="$ROOT_DIR/click_deploy"
OUTPUT_ZIP="$ROOT_DIR/click_deploy.zip"
DO_BUILD=0
KEEP_POPULATED=0

# Pinned tool versions (override via environment if needed).
ESPTOOL_VERSION="${ESPTOOL_VERSION:-5.3.1}"

ESPTOOL_ZIP_URL="https://github.com/espressif/esptool/releases/download/v${ESPTOOL_VERSION}/esptool-v${ESPTOOL_VERSION}-windows-amd64.zip"

while [ $# -gt 0 ]; do
    case "$1" in
        --build) DO_BUILD=1 ;;
        --keep) KEEP_POPULATED=1 ;;
        -o)
            shift
            if [ $# -eq 0 ]; then
                echo "Missing argument for -o" >&2
                exit 1
            fi
            OUTPUT_ZIP="$1"
            ;;
        -h|--help)
            sed -n '2,13p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
    shift
done

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

need_cmd curl
need_cmd unzip
need_cmd zip

WORK_DIR=$(mktemp -d)
POPULATED=0

cleanup() {
    rm -rf "$WORK_DIR"
    if [ "$KEEP_POPULATED" -eq 0 ] && [ "$POPULATED" -eq 1 ]; then
        rm -f "$DEPLOY_DIR"/firmware/esp32s3/*.bin \
              "$DEPLOY_DIR"/firmware/esp32s3/flasher_args.json \
              "$DEPLOY_DIR"/firmware/rp2040/*.elf \
              "$DEPLOY_DIR"/firmware/rp2040/*.uf2
        rm -rf "$DEPLOY_DIR/tools/esptool"
        echo "Cleaned populated artifacts from click_deploy/ (use --keep to retain them)."
    fi
}
trap cleanup EXIT

if [ "$DO_BUILD" -eq 1 ]; then
    echo "==> Building firmware"
    "$ROOT_DIR/dev" build
    "$ROOT_DIR/dev" rp2040 build
fi

echo "==> Syncing firmware into click_deploy/"
"$DEPLOY_DIR/sync_from_build.sh"
POPULATED=1

echo "==> Downloading esptool v${ESPTOOL_VERSION} (windows-amd64)"
curl -fL --retry 3 -o "$WORK_DIR/esptool-win.zip" "$ESPTOOL_ZIP_URL"

mkdir -p "$DEPLOY_DIR/tools/esptool/windows-amd64"
unzip -o -j "$WORK_DIR/esptool-win.zip" 'esptool-windows-amd64/esptool.exe' \
    -d "$DEPLOY_DIR/tools/esptool/windows-amd64" >/dev/null
echo "Bundled tools/esptool/windows-amd64/esptool.exe"
echo "(No picotool needed: the RP2040 is flashed by copying firmware.uf2 onto its RPI-RP2 BOOTSEL drive.)"

echo "==> Writing $OUTPUT_ZIP"
mkdir -p "$(dirname -- "$OUTPUT_ZIP")"
rm -f "$OUTPUT_ZIP"
(cd "$ROOT_DIR" && zip -q -r "$OUTPUT_ZIP" click_deploy -x '*/.DS_Store')

echo
echo "Windows flashing bundle ready: $OUTPUT_ZIP"
echo "On Windows: unzip, then run: powershell -ExecutionPolicy Bypass -File .\\windows\\flash_all.ps1"
