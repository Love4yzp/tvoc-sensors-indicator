# click_deploy — Agent Guide

This folder is the packaging scaffold for the no-toolchain flashing bundle of
the SenseCAP Indicator firmware (ESP32-S3 screen side + RP2040 sensor
coprocessor). The shipped artifacts are per-platform zips under
`click_deploy/flasher/`. Read this before changing anything here.

## Golden rules

- **Never commit firmware images or tool binaries** (`.bin`, `.uf2`, `.elf`,
  `.exe`). They are gitignored. `scripts/test_click_deploy_package.py` checks
  `git ls-files` to ensure no binaries are committed; it is fine to leave the
  local `tools/` cache populated between packaging runs.
- **Do not reintroduce picotool on Windows.** picotool cannot talk to the
  RP2040 BOOTSEL device without a Zadig/WinUSB driver on Windows (confirmed in
  the field: "RP2040 device ... appears to be in BOOTSEL mode, but picotool
  was unable to connect"). The Windows script flashes the RP2040 by copying
  `firmware.uf2` onto the chip's `RPI-RP2` USB mass-storage drive, which uses
  the built-in Windows driver. The macOS/Linux scripts still use `picotool`
  from PATH (or a bundled binary if present) — that is fine there, do not
  "unify" them onto the Windows scheme without a reason.
- **Do not change ESP32-S3 flash offsets/settings casually.** The PowerShell
  and shell scripts hardcode `0x0 bootloader / 0x8000 partition-table /
  0x10000 app` with `--flash_mode dio --flash_size 8MB --flash_freq 80m`,
  matching `build/flasher_args.json`. If `partitions.csv` or the ESP-IDF
  config ever changes these, update the scripts and this file together.
- MQTT topics, partition layout, and other product behavior are out of scope
  here — this folder only packages and flashes what `build/` produced.

## Directory layout

```text
click_deploy/
  firmware/              # latest built firmware (single source of truth)
    esp32s3/             # bootloader.bin, partition-table.bin, indicator_ha.bin, flasher_args.json
    rp2040/              # firmware.elf, firmware.uf2
  tools/                 # downloaded flashing tools
    esptool/             # per-platform binaries
    picotool/            # macOS/Linux only, optional
  flasher/               # platform-specific end-user bundles
    package.sh           # interactive maintainer packaging entry point
    macos_linux/         # shell scripts + firmware + tools (when populated)
    windows/             # PowerShell scripts + firmware + tools (when populated)
```

## Packaging workflow (maintainer)

One interactive command from the repository root:

```sh
./click_deploy/flasher/package.sh
```

It prompts for platform and chip combination, checks firmware freshness,
rebuilds if necessary, syncs into `click_deploy/firmware/`, downloads/arranges
tools, assembles the bundle, and writes:

```text
click_deploy/flasher/indicator_ha-<platform>-<chips>.zip
```

Non-interactive override for CI:

```sh
PLATFORM=windows CHIPS=both ./click_deploy/flasher/package.sh
```

Force a rebuild:

```sh
FORCE_BUILD=1 PLATFORM=macos_linux CHIPS=esp32s3 ./click_deploy/flasher/package.sh
```

The packaging script cleans populated `firmware/` and `tools/` directories out
of `flasher/<platform>/` after zipping so the scaffold stays clean.

## How the bundles work

- Each bundle is self-contained: scripts, firmware images, and required tools
  are copied in at package time. The flash scripts look for `firmware/` and
  `tools/` next to themselves first, and fall back two levels up to the
  `click_deploy/` scaffold when running from the repo.
- `flash_all.*` runs `flash_rp2040.*` first, then `flash_esp32s3.*`.
- Flash scripts are non-interactive: they auto-detect ports and execute.
  Environment variables override auto-detection:
  - `ESPPORT` / first positional argument for ESP32-S3.
  - `RP2040_PORT` / first positional argument for RP2040.
  - `ESP_BAUD` for ESP32-S3 baud rate.

## Bundled Windows tools

For a fully self-contained Windows package, `package.sh` downloads the pinned
`esptool` release (version via `ESPTOOL_VERSION`, currently 5.3.1) into
`tools/esptool/windows-amd64/esptool.exe`.

By default the download tries a domestic GitHub mirror first and falls back to
the official GitHub URL if the mirror fails. Override with `ESPTOOL_BASE_URL`
to use a single custom source, for example:

```sh
ESPTOOL_BASE_URL=https://github.com/espressif/esptool/releases/download \
  ./click_deploy/flasher/package.sh
```

picotool is intentionally **not** bundled for Windows; the UF2-drive method
works with the built-in Windows mass-storage driver.

## Bundled macOS/Linux tools

`package.sh` downloads the four common `esptool` binaries
(`macos-amd64`, `macos-arm64`, `linux-amd64`, `linux-aarch64`) so the single
`macos_linux` bundle works on both operating systems and both architectures.
The same `ESPTOOL_BASE_URL` override applies.

`picotool` is **not** auto-downloaded because reliable official prebuilt
binaries are not available. If a `picotool` binary is manually placed in
`tools/picotool/<platform-dir>/`, `package.sh` will include it; otherwise the
flash script falls back to `picotool` on PATH.

## Verification after any change here

```sh
python3 scripts/test_click_deploy_package.py   # scaffold guard (needs clean folder)
bash -n click_deploy/flasher/package.sh        # packaging script syntax
bash -n click_deploy/flasher/macos_linux/*.sh  # shell flash scripts
```

PowerShell scripts can be parse-checked on any OS with `pwsh`:

```powershell
[System.Management.Automation.Language.Parser]::ParseFile(
    "click_deploy/flasher/windows/flash_esp32s3.ps1", [ref]$null, [ref]$null)
```

Real flashing can only be verified on actual target machines — say so in the
commit/PR if it was not done.
