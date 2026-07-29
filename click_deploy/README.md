# SenseCAP Indicator Click Deploy

This folder packages the SenseCAP Indicator firmware (ESP32-S3 + RP2040) into
self-contained, single-target flasher zips.

- `package.sh` — packaging script: builds if stale, pulls firmware straight
  from `../build/` and `../rp2040/.pio/build/...`, and zips a bundle.
- `scripts/` — the flashing scripts shipped inside every bundle.
  - `macos_linux/` — shell scripts (used by `macos-arm64`, `linux-amd64`,
    `linux-aarch64`).
  - `windows/` — PowerShell scripts (used by `windows-amd64`).
- `tools/` — local cache of downloaded `esptool` binaries, one per target.
  Populated on demand and reused across runs; never committed.
- `dist/` — packaging output (unzipped bundle + zip). Fully gitignored,
  disposable, wiped and rebuilt every run.

Firmware images and tool binaries are never committed. `build/` is the single
source of truth; this folder only reads from it at package time.

## Maintainer workflow

```sh
./click_deploy/package.sh <target>
# targets: macos-arm64 | linux-amd64 | linux-aarch64 | windows-amd64
```

Run with no argument for an interactive prompt. The script:

- Checks whether firmware is up to date against the source tree; rebuilds via
  `./dev build` / `./dev rp2040 build` if stale (or if `FORCE_BUILD=1`).
- Downloads the `esptool` binary for the target if not already cached under
  `tools/esptool/<target>/`.
- Assembles `dist/indicator_ha-<target>-<git-hash>/` directly from `build/`
  and `rp2040/.pio/build/...` (no intermediate staging copy).
- Zips it to `dist/indicator_ha-<target>-<git-hash>.zip`.

`<git-hash>` is the short commit hash (`git rev-parse --short HEAD`), with a
`-dirty` suffix if the working tree has uncommitted changes — so bundles from
different builds are easy to tell apart.

Force a rebuild even if firmware looks fresh:

```sh
FORCE_BUILD=1 ./click_deploy/package.sh macos-arm64
```

Use a different `esptool` download source (default tries a domestic GitHub
mirror first, then falls back to the official GitHub URL):

```sh
ESPTOOL_BASE_URL=https://github.com/espressif/esptool/releases/download \
  ./click_deploy/package.sh windows-amd64
```

## Layout of a packaged bundle

```text
indicator_ha-<target>-<git-hash>/
  firmware/
    esp32s3/      bootloader.bin, partition-table.bin, indicator_ha.bin, flasher_args.json
    rp2040/       firmware.elf, firmware.uf2
  tools/
    esptool/      single binary matching <target>
    picotool/     macOS/Linux only, optional (falls back to PATH)
  flash_all.sh / flash_all.ps1       runs rp2040 then esp32s3
  flash_esp32s3.sh / flash_esp32s3.ps1
  flash_rp2040.sh / flash_rp2040.ps1
  install.sh                         (macOS/Linux only) chmod +x helper
```

## End-user flashing

### macOS / Linux

Unzip the bundle and run:

```sh
cd indicator_ha-macos-arm64-abc1234
./install.sh
./flash_all.sh
```

To flash only one chip — useful since the RP2040 flash step doesn't always
succeed on the first try and may need a retry without touching the ESP32-S3:

```sh
./flash_esp32s3.sh
./flash_rp2040.sh
```

To pin serial ports:

```sh
ESPPORT=/dev/cu.usbmodemXXXX ./flash_esp32s3.sh
RP2040_PORT=/dev/cu.usbmodemYYYY ./flash_rp2040.sh
```

### Windows PowerShell

Unzip the bundle and run:

```powershell
cd indicator_ha-windows-amd64-abc1234
.\flash_all.ps1
```

If Windows blocks the scripts with an execution-policy error, run once with:

```powershell
powershell -ExecutionPolicy Bypass -File .\flash_all.ps1
```

To pin serial ports:

```powershell
$env:ESPPORT = "COM7"
$env:RP2040_PORT = "COM8"
.\flash_all.ps1
```

## Tooling notes

- Each bundle ships exactly one `esptool` binary, matching its target — the
  scripts don't need to sniff the host architecture.
- The RP2040 is flashed differently per platform:
  - **macOS / Linux:** `picotool` (bundled if present, otherwise from PATH).
  - **Windows:** copy `firmware.uf2` onto the `RPI-RP2` BOOTSEL USB drive. No
    extra driver or `picotool` is needed.
- `esptool` v5 prints deprecation warnings for the underscore-style arguments
  (`write_flash`, `--flash_mode`, ...) used by the scripts; they still work, and
  the same arguments also run on the older `esptool` v4 shipped with ESP-IDF.
- `picotool` is not auto-downloaded (no reliable prebuilt binaries). Drop one
  manually into `tools/picotool/<target>/picotool` to have `package.sh`
  bundle it; otherwise the flash script falls back to PATH.
