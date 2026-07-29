# SenseCAP Indicator Click Deploy

This folder packages the SenseCAP Indicator firmware into self-contained,
per-platform flasher bundles:

- `firmware/` — latest built firmware images (single source of truth).
- `tools/` — downloaded flashing tools per platform/architecture.
- `flasher/` — platform-specific bundles for end users.
  - `macos_linux/` — shell scripts for macOS and Linux.
  - `windows/` — PowerShell scripts for Windows.
  - `package.sh` — interactive maintainer script that builds, syncs, and zips.

The repository does **not** commit generated firmware images or flashing tool
binaries. Build outputs are copied into this folder when preparing a release
package.

## Maintainer workflow

Run the interactive packaging script from the repository root:

```sh
./click_deploy/flasher/package.sh
```

It will ask for:

1. Platform: `macos_linux` or `windows`
2. Chips: `esp32s3`, `rp2040`, or `both`

Then it:

- Checks whether the requested firmware is up to date against the source tree.
- Rebuilds via `./dev build` or `./dev rp2040 build` if stale.
- Syncs the artifacts into `click_deploy/firmware/`.
- Downloads the pinned `esptool` release if the tool is missing.
- Assembles a self-contained bundle under `flasher/<platform>/`.
- Writes `click_deploy/flasher/indicator_ha-<platform>-<chips>.zip`.
- Cleans the populated `firmware/` and `tools/` directories out of the bundle
  scaffold afterward.

For CI or non-interactive use, set the two variables before running:

```sh
PLATFORM=windows CHIPS=both ./click_deploy/flasher/package.sh
```

To force a rebuild even when the script thinks the firmware is fresh:

```sh
FORCE_BUILD=1 PLATFORM=macos_linux CHIPS=both ./click_deploy/flasher/package.sh
```

To use a different `esptool` download source (the default tries a domestic
GitHub mirror first, then falls back to the official GitHub URL):

```sh
ESPTOOL_BASE_URL=https://github.com/espressif/esptool/releases/download \
  PLATFORM=windows CHIPS=both ./click_deploy/flasher/package.sh
```

### Manual build + sync (without packaging)

If you only want to update `click_deploy/firmware/` without producing a zip:

```sh
./dev build
./dev rp2040 build
```

Then run the packaging script and stop after the sync step, or copy the files
from `build/` and `rp2040/.pio/build/indicator_rp2040/` manually.

## Layout of a complete package

```text
click_deploy/
  firmware/
    esp32s3/      bootloader.bin, partition-table.bin, indicator_ha.bin, flasher_args.json
    rp2040/       firmware.elf, firmware.uf2
  tools/
    esptool/      per-platform binaries
    picotool/     macOS/Linux only, optional (falls back to PATH)
  flasher/
    package.sh
    macos_linux/
      flash_all.sh
      flash_esp32s3.sh
      flash_rp2040.sh
      install.sh
      firmware/...
      tools/...
    windows/
      flash_all.ps1
      flash_esp32s3.ps1
      flash_rp2040.ps1
      firmware/...
      tools/...
```

## End-user flashing

### macOS / Linux

Unzip the bundle and run:

```sh
cd indicator_ha-macos_linux-both
./macos_linux/install.sh
./macos_linux/flash_all.sh
```

To flash only one chip:

```sh
./macos_linux/flash_esp32s3.sh
./macos_linux/flash_rp2040.sh
```

To pin serial ports:

```sh
ESPPORT=/dev/cu.usbmodemXXXX ./macos_linux/flash_esp32s3.sh
RP2040_PORT=/dev/cu.usbmodemYYYY ./macos_linux/flash_rp2040.sh
```

### Windows PowerShell

Unzip the bundle and run:

```powershell
cd indicator_ha-windows-both
.\windows\flash_all.ps1
```

If Windows blocks the scripts with an execution-policy error, run once with:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\flash_all.ps1
```

To pin serial ports:

```powershell
$env:ESPPORT = "COM7"
$env:RP2040_PORT = "COM8"
.\windows\flash_all.ps1
```

## Tooling notes

- The ESP32-S3 scripts prefer the bundled `esptool` binary for the running
  platform and fall back to `esptool` / `esptool.py` on PATH.
- The RP2040 is flashed differently per platform:
  - **macOS / Linux:** `picotool` (bundled if present, otherwise from PATH).
  - **Windows:** copy `firmware.uf2` onto the `RPI-RP2` BOOTSEL USB drive. No
    extra driver or `picotool` is needed.
- `esptool` v5 prints deprecation warnings for the underscore-style arguments
  (`write_flash`, `--flash_mode`, ...) used by the scripts; they still work, and
  the same arguments also run on the older `esptool` v4 shipped with ESP-IDF.
