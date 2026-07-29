#!/usr/bin/env bash
# Interactive maintainer script for packaging the SenseCAP Indicator click-deploy
# flasher bundles. Replaces the old click_deploy/sync_from_build.sh workflow.
#
# Usage:
#   ./click_deploy/flasher/package.sh
#
# The script interactively asks for:
#   1. platform: macos_linux | windows
#   2. chips:    esp32s3 | rp2040 | both
#
# It then checks whether the requested firmware artifacts are up to date,
# rebuilds them if necessary, syncs them into click_deploy/firmware/,
# downloads the matching esptool binaries if missing, assembles a self-contained
# platform bundle, and writes:
#   click_deploy/flasher/indicator_ha-<platform>-<chips>.zip
#
# Non-interactive override (useful for CI/testing):
#   PLATFORM=windows CHIPS=both ./click_deploy/flasher/package.sh
#
# By default esptool is downloaded through a domestic GitHub mirror, falling
# back to the official GitHub release URL if the mirror fails. Override with:
#   ESPTOOL_BASE_URL=https://github.com/espressif/esptool/releases/download \
#       ./click_deploy/flasher/package.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DEPLOY_DIR="$REPO_ROOT/click_deploy"
FLASHER_DIR="$DEPLOY_DIR/flasher"

ESPTOOL_VERSION="${ESPTOOL_VERSION:-5.3.1}"
# Default download sources: domestic GitHub mirror first, then the official URL.
# Set ESPTOOL_BASE_URL to use a single custom source.
ESPTOOL_MIRROR_URL="https://ghproxy.com/https://github.com/espressif/esptool/releases/download"
ESPTOOL_OFFICIAL_URL="https://github.com/espressif/esptool/releases/download"

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

copy_file() {
    local src="$1" dst="$2" label="$3"
    mkdir -p "$(dirname "$dst")"
    cp "$src" "$dst"
    echo "  $label -> $dst"
}

# Return 0 if $target exists and is newer than every file in the given trees.
is_fresh() {
    local target="$1"
    shift
    if [ ! -f "$target" ]; then
        return 1
    fi
    local newer
    newer=$(find "$@" -type f -newer "$target" 2>/dev/null | head -n 1)
    [ -z "$newer" ]
}

interactive_select() {
    if [ -n "${PLATFORM:-}" ] && [ -n "${CHIPS:-}" ]; then
        echo "Using PLATFORM=$PLATFORM CHIPS=$CHIPS (non-interactive override)"
        return 0
    fi

    echo "Select platform:"
    local platforms=("macos_linux" "windows")
    select PLATFORM in "${platforms[@]}"; do
        if [ -n "$PLATFORM" ]; then break; fi
    done

    echo "Select chips to include:"
    local chip_choices=("esp32s3" "rp2040" "both")
    select CHIPS in "${chip_choices[@]}"; do
        if [ -n "$CHIPS" ]; then break; fi
    done
}

check_prereqs() {
    need_cmd curl
    need_cmd unzip
    need_cmd zip
}

ensure_esp32s3_firmware() {
    local app_bin="$REPO_ROOT/build/indicator_ha.bin"
    local srcs=(
        "$REPO_ROOT/main"
        "$REPO_ROOT/components"
        "$REPO_ROOT/CMakeLists.txt"
        "$REPO_ROOT/sdkconfig.defaults"
        "$REPO_ROOT/partitions.csv"
    )
    if [ -f "$REPO_ROOT/sdkconfig" ]; then
        srcs+=("$REPO_ROOT/sdkconfig")
    fi

    if [ -n "${FORCE_BUILD:-}" ] || ! is_fresh "$app_bin" "${srcs[@]}"; then
        echo "==> Building ESP32-S3 firmware"
        "$REPO_ROOT/dev" build
    else
        echo "ESP32-S3 firmware is up to date."
    fi

    for file in \
        "$REPO_ROOT/build/bootloader/bootloader.bin" \
        "$REPO_ROOT/build/partition_table/partition-table.bin" \
        "$REPO_ROOT/build/indicator_ha.bin" \
        "$REPO_ROOT/build/flasher_args.json"
    do
        if [ ! -f "$file" ]; then
            echo "Missing ESP32-S3 build artifact: $file" >&2
            exit 1
        fi
    done
}

ensure_rp2040_firmware() {
    local uf2="$REPO_ROOT/rp2040/.pio/build/indicator_rp2040/firmware.uf2"
    local srcs=(
        "$REPO_ROOT/rp2040/src"
        "$REPO_ROOT/rp2040/include"
        "$REPO_ROOT/rp2040/platformio.ini"
    )

    if [ -n "${FORCE_BUILD:-}" ] || ! is_fresh "$uf2" "${srcs[@]}"; then
        echo "==> Building RP2040 firmware"
        "$REPO_ROOT/dev" rp2040 build
    else
        echo "RP2040 firmware is up to date."
    fi

    for file in \
        "$REPO_ROOT/rp2040/.pio/build/indicator_rp2040/firmware.uf2" \
        "$REPO_ROOT/rp2040/.pio/build/indicator_rp2040/firmware.elf"
    do
        if [ ! -f "$file" ]; then
            echo "Missing RP2040 build artifact: $file" >&2
            exit 1
        fi
    done
}

sync_firmware() {
    echo "==> Syncing firmware into $DEPLOY_DIR/firmware/"
    if [ "$CHIPS" = "esp32s3" ] || [ "$CHIPS" = "both" ]; then
        copy_file "$REPO_ROOT/build/bootloader/bootloader.bin" \
            "$DEPLOY_DIR/firmware/esp32s3/bootloader.bin" "ESP32-S3 bootloader"
        copy_file "$REPO_ROOT/build/partition_table/partition-table.bin" \
            "$DEPLOY_DIR/firmware/esp32s3/partition-table.bin" "ESP32-S3 partition table"
        copy_file "$REPO_ROOT/build/indicator_ha.bin" \
            "$DEPLOY_DIR/firmware/esp32s3/indicator_ha.bin" "ESP32-S3 application"
        copy_file "$REPO_ROOT/build/flasher_args.json" \
            "$DEPLOY_DIR/firmware/esp32s3/flasher_args.json" "ESP32-S3 flasher args"
    fi
    if [ "$CHIPS" = "rp2040" ] || [ "$CHIPS" = "both" ]; then
        copy_file "$REPO_ROOT/rp2040/.pio/build/indicator_rp2040/firmware.uf2" \
            "$DEPLOY_DIR/firmware/rp2040/firmware.uf2" "RP2040 UF2 firmware"
        copy_file "$REPO_ROOT/rp2040/.pio/build/indicator_rp2040/firmware.elf" \
            "$DEPLOY_DIR/firmware/rp2040/firmware.elf" "RP2040 ELF firmware"
    fi
}

download_esptool() {
    local platform="$1"
    local dest_dir="$DEPLOY_DIR/tools/esptool/$platform"
    local bin_name="esptool"
    if [[ "$platform" == windows-* ]]; then
        bin_name="esptool.exe"
    fi
    if [ -f "$dest_dir/$bin_name" ]; then
        echo "  esptool for $platform already present"
        return 0
    fi

    local urls=("$ESPTOOL_MIRROR_URL" "$ESPTOOL_OFFICIAL_URL")
    if [ -n "${ESPTOOL_BASE_URL:-}" ]; then
        urls=("$ESPTOOL_BASE_URL")
    fi

    local last_err=""
    for base in "${urls[@]}"; do
        local archive
        local archive_member
        if [[ "$platform" == windows-* ]]; then
            archive="esptool-v${ESPTOOL_VERSION}-${platform}.zip"
            archive_member="esptool-${platform}/esptool*"
        else
            archive="esptool-v${ESPTOOL_VERSION}-${platform}.tar.gz"
            archive_member="esptool-${platform}/esptool"
        fi
        local tool_url="${base}/v${ESPTOOL_VERSION}/${archive}"
        echo "  trying $tool_url"
        local work_dir
        work_dir=$(mktemp -d)
        if curl -fL --retry 2 --max-time 180 -o "$work_dir/$archive" "$tool_url" 2>/dev/null; then
            mkdir -p "$dest_dir"
            if [[ "$platform" == windows-* ]]; then
                unzip -j -o "$work_dir/$archive" "$archive_member" -d "$dest_dir" >/dev/null
            else
                tar -xzf "$work_dir/$archive" -C "$work_dir" "$archive_member"
                cp "$work_dir/$archive_member" "$dest_dir/"
            fi
            chmod +x "$dest_dir"/esptool* 2>/dev/null || true
            rm -rf "$work_dir"
            echo "  downloaded esptool for $platform"
            return 0
        else
            last_err="curl failed for $tool_url"
            rm -rf "$work_dir"
        fi
    done

    echo "  failed to download esptool for $platform: $last_err" >&2
    return 1
}

ensure_tools() {
    echo "==> Ensuring bundled tools are present"
    if [ "$PLATFORM" = "windows" ]; then
        download_esptool "windows-amd64"
    else
        for arch in macos-amd64 macos-arm64 linux-amd64 linux-aarch64; do
            download_esptool "$arch"
        done
    fi
}

assemble_bundle() {
    local platform_dir="$FLASHER_DIR/$PLATFORM"
    echo "==> Assembling self-contained bundle in $platform_dir"

    # Start from a clean scaffold for this platform.
    rm -rf "$platform_dir/firmware" "$platform_dir/tools"

    if [ "$CHIPS" = "esp32s3" ] || [ "$CHIPS" = "both" ]; then
        mkdir -p "$platform_dir/firmware/esp32s3"
        cp "$DEPLOY_DIR/firmware/esp32s3/bootloader.bin" \
           "$DEPLOY_DIR/firmware/esp32s3/partition-table.bin" \
           "$DEPLOY_DIR/firmware/esp32s3/indicator_ha.bin" \
           "$DEPLOY_DIR/firmware/esp32s3/flasher_args.json" \
           "$platform_dir/firmware/esp32s3/"
    fi
    if [ "$CHIPS" = "rp2040" ] || [ "$CHIPS" = "both" ]; then
        mkdir -p "$platform_dir/firmware/rp2040"
        cp "$DEPLOY_DIR/firmware/rp2040/firmware.elf" \
           "$DEPLOY_DIR/firmware/rp2040/firmware.uf2" \
           "$platform_dir/firmware/rp2040/"
    fi

    if [ "$PLATFORM" = "windows" ]; then
        mkdir -p "$platform_dir/tools/esptool/windows-amd64"
        cp "$DEPLOY_DIR/tools/esptool/windows-amd64/esptool.exe" "$platform_dir/tools/esptool/windows-amd64/"
    else
        mkdir -p "$platform_dir/tools/esptool"
        cp -R "$DEPLOY_DIR/tools/esptool/"* "$platform_dir/tools/esptool/"
        if [ -d "$DEPLOY_DIR/tools/picotool" ]; then
            mkdir -p "$platform_dir/tools/picotool"
            cp -R "$DEPLOY_DIR/tools/picotool/"* "$platform_dir/tools/picotool/"
        fi
    fi
}

zip_bundle() {
    local output="$FLASHER_DIR/indicator_ha-${PLATFORM}-${CHIPS}.zip"
    echo "==> Writing $output"
    rm -f "$output"
    (cd "$FLASHER_DIR" && zip -q -r "$output" "$PLATFORM" -x '*/.DS_Store')
    echo "Bundle ready: $output"
}

cleanup_bundle() {
    local platform_dir="$FLASHER_DIR/$PLATFORM"
    rm -rf "$platform_dir/firmware" "$platform_dir/tools"
    echo "Cleaned populated artifacts from $platform_dir"
}

main() {
    cd "$REPO_ROOT"
    interactive_select
    check_prereqs

    if [ "$CHIPS" = "esp32s3" ] || [ "$CHIPS" = "both" ]; then
        ensure_esp32s3_firmware
    fi
    if [ "$CHIPS" = "rp2040" ] || [ "$CHIPS" = "both" ]; then
        ensure_rp2040_firmware
    fi

    sync_firmware
    ensure_tools
    assemble_bundle
    zip_bundle
    cleanup_bundle
}

main "$@"
