#!/usr/bin/env bash
# Maintainer script for packaging the SenseCAP Indicator click-deploy
# flasher bundles.
#
# Usage:
#   ./click_deploy/package.sh <target>
#   targets: macos-arm64 | linux-amd64 | linux-aarch64 | windows-amd64
#
# Builds firmware if stale, downloads the matching esptool binary if missing,
# assembles a self-contained bundle straight from build/ and rp2040/.pio/build/
# (no intermediate staging copy), and writes:
#   click_deploy/dist/indicator_ha-<target>-<git-hash>.zip
#
# Force a rebuild even if firmware looks fresh:
#   FORCE_BUILD=1 ./click_deploy/package.sh macos-arm64
#
# By default esptool is downloaded through a domestic GitHub mirror, falling
# back to the official GitHub release URL if the mirror fails. Override with:
#   ESPTOOL_BASE_URL=https://github.com/espressif/esptool/releases/download \
#       ./click_deploy/package.sh windows-amd64
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPLOY_DIR="$SCRIPT_DIR"
DIST_DIR="$DEPLOY_DIR/dist"

ESPTOOL_VERSION="${ESPTOOL_VERSION:-5.3.1}"
ESPTOOL_MIRROR_URL="https://ghproxy.com/https://github.com/espressif/esptool/releases/download"
ESPTOOL_OFFICIAL_URL="https://github.com/espressif/esptool/releases/download"

TARGETS=(macos-arm64 linux-amd64 linux-aarch64 windows-amd64)

usage() {
    echo "Usage: $0 <target>" >&2
    echo "  targets: ${TARGETS[*]}" >&2
    exit 1
}

select_target() {
    if [ -n "${1:-}" ]; then
        TARGET="$1"
    else
        echo "Select target:"
        select TARGET in "${TARGETS[@]}"; do
            [ -n "$TARGET" ] && break
        done
    fi
    case " ${TARGETS[*]} " in
        *" $TARGET "*) ;;
        *) usage ;;
    esac
}

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
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

download_esptool() {
    local target="$1"
    local dest_dir="$DEPLOY_DIR/tools/esptool/$target"
    local bin_name="esptool"
    if [[ "$target" == windows-* ]]; then
        bin_name="esptool.exe"
    fi
    if [ -f "$dest_dir/$bin_name" ]; then
        echo "esptool for $target already cached"
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
        if [[ "$target" == windows-* ]]; then
            archive="esptool-v${ESPTOOL_VERSION}-${target}.zip"
            archive_member="esptool-${target}/esptool*"
        else
            archive="esptool-v${ESPTOOL_VERSION}-${target}.tar.gz"
            archive_member="esptool-${target}/esptool"
        fi
        local tool_url="${base}/v${ESPTOOL_VERSION}/${archive}"
        echo "  trying $tool_url"
        local work_dir
        work_dir=$(mktemp -d)
        if curl -fL --retry 2 --max-time 180 -o "$work_dir/$archive" "$tool_url" 2>/dev/null; then
            mkdir -p "$dest_dir"
            if [[ "$target" == windows-* ]]; then
                unzip -j -o "$work_dir/$archive" "$archive_member" -d "$dest_dir" >/dev/null
            else
                tar -xzf "$work_dir/$archive" -C "$work_dir" "$archive_member"
                cp "$work_dir/$archive_member" "$dest_dir/"
            fi
            chmod +x "$dest_dir"/esptool* 2>/dev/null || true
            rm -rf "$work_dir"
            echo "  downloaded esptool for $target"
            return 0
        else
            last_err="curl failed for $tool_url"
            rm -rf "$work_dir"
        fi
    done

    echo "  failed to download esptool for $target: $last_err" >&2
    return 1
}

git_ref() {
    local hash
    hash=$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo "nogit")
    if [ -n "$(git -C "$REPO_ROOT" status --porcelain 2>/dev/null)" ]; then
        hash="${hash}-dirty"
    fi
    echo "$hash"
}

assemble_bundle() {
    local name="$1"
    local out_dir="$DIST_DIR/$name"
    echo "==> Assembling $out_dir"
    rm -rf "$out_dir"
    mkdir -p "$out_dir/firmware/esp32s3" "$out_dir/firmware/rp2040"

    cp "$REPO_ROOT/build/bootloader/bootloader.bin" \
       "$REPO_ROOT/build/partition_table/partition-table.bin" \
       "$REPO_ROOT/build/indicator_ha.bin" \
       "$REPO_ROOT/build/flasher_args.json" \
       "$out_dir/firmware/esp32s3/"
    cp "$REPO_ROOT/rp2040/.pio/build/indicator_rp2040/firmware.uf2" \
       "$REPO_ROOT/rp2040/.pio/build/indicator_rp2040/firmware.elf" \
       "$out_dir/firmware/rp2040/"

    if [ "$TARGET" = "windows-amd64" ]; then
        cp "$DEPLOY_DIR/scripts/windows/"*.ps1 "$out_dir/"
        mkdir -p "$out_dir/tools/esptool"
        cp "$DEPLOY_DIR/tools/esptool/windows-amd64/esptool.exe" "$out_dir/tools/esptool/"
    else
        cp "$DEPLOY_DIR/scripts/macos_linux/"*.sh "$out_dir/"
        chmod +x "$out_dir"/*.sh
        mkdir -p "$out_dir/tools/esptool"
        cp "$DEPLOY_DIR/tools/esptool/$TARGET/esptool" "$out_dir/tools/esptool/"
        chmod +x "$out_dir/tools/esptool/esptool"
        if [ -f "$DEPLOY_DIR/tools/picotool/$TARGET/picotool" ]; then
            mkdir -p "$out_dir/tools/picotool"
            cp "$DEPLOY_DIR/tools/picotool/$TARGET/picotool" "$out_dir/tools/picotool/"
            chmod +x "$out_dir/tools/picotool/picotool"
        fi
    fi
}

zip_bundle() {
    local name="$1"
    local zip_path="$DIST_DIR/$name.zip"
    echo "==> Writing $zip_path"
    rm -f "$zip_path"
    (cd "$DIST_DIR" && zip -q -r "$zip_path" "$name" -x '*/.DS_Store')
    echo "Bundle ready: $zip_path"
}

main() {
    select_target "${1:-}"

    need_cmd curl
    need_cmd unzip
    need_cmd zip
    need_cmd git

    ensure_esp32s3_firmware
    ensure_rp2040_firmware
    download_esptool "$TARGET"

    mkdir -p "$DIST_DIR"
    local name="indicator_ha-${TARGET}-$(git_ref)"
    assemble_bundle "$name"
    zip_bundle "$name"
}

main "$@"
