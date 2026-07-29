# click_deploy — Agent Guide

This folder is the packaging scaffold for the no-toolchain flashing bundle of
the SenseCAP Indicator firmware (ESP32-S3 screen side + RP2040 sensor
coprocessor). Shipped artifacts are single-target zips written to
`click_deploy/dist/`. Read this before changing anything here.

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
- **Keep `flash_esp32s3.*` and `flash_rp2040.*` as separate, independently
  runnable scripts.** The RP2040 flash step doesn't always succeed on the
  first try (BOOTSEL detection can flake) and needs to be retried on its own
  without re-flashing the ESP32-S3. `flash_all.*` just runs both in sequence.
- **Do not change ESP32-S3 flash offsets/settings casually.** The PowerShell
  and shell scripts hardcode `0x0 bootloader / 0x8000 partition-table /
  0x10000 app` with `--flash_mode dio --flash_size 8MB --flash_freq 80m`,
  matching `build/flasher_args.json`. If `partitions.csv` or the ESP-IDF
  config ever changes these, update the scripts and this file together.
- MQTT topics, partition layout, and other product behavior are out of scope
  here — this folder only packages and flashes what `build/` produced.
- **Firmware is read directly from `../build/` and `../rp2040/.pio/build/...`
  at package time.** There is no intermediate `click_deploy/firmware/`
  staging copy — don't reintroduce one.
- **Every bundle ships exactly one `esptool` binary**, matching its target.
  Don't bundle multiple architectures into one zip — that's what the 4
  separate targets are for.

## Directory layout

```text
click_deploy/
  package.sh              # packaging entry point, takes a target argument
  scripts/                # flashing scripts shipped inside every bundle
    macos_linux/           # shared by macos-arm64, linux-amd64, linux-aarch64
    windows/                # windows-amd64 only
  tools/                  # local esptool download cache, per target
    esptool/<target>/
  dist/                   # packaging output, fully gitignored, disposable
    indicator_ha-<target>-<git-hash>/
    indicator_ha-<target>-<git-hash>.zip
```

## Packaging workflow (maintainer)

```sh
./click_deploy/package.sh <target>
# targets: macos-arm64 | linux-amd64 | linux-aarch64 | windows-amd64
```

Run with no argument for an interactive prompt. It checks firmware freshness,
rebuilds if necessary (`FORCE_BUILD=1` to force), downloads/caches the
matching `esptool` binary, assembles the bundle straight from `build/` and
`rp2040/.pio/build/...`, and writes:

```text
click_deploy/dist/indicator_ha-<target>-<git-hash>.zip
```

`<git-hash>` = `git rev-parse --short HEAD`, `-dirty` suffix if the working
tree has uncommitted changes.

## How the bundles work

- Each bundle is self-contained: scripts, firmware images, and the matching
  `esptool` binary are assembled fresh into `dist/indicator_ha-<target>-<hash>/`
  every run (that directory is wiped first — no stale leftovers).
- Scripts always find `firmware/` and `tools/` as direct siblings inside the
  bundle — there is no "bundle vs. repo scaffold" dual-path logic since
  `scripts/` is never run in place, only after `package.sh` assembles it into
  `dist/`.
- `flash_all.*` runs `flash_rp2040.*` first, then `flash_esp32s3.*`.
- Flash scripts are non-interactive: they auto-detect ports and execute.
  Environment variables override auto-detection:
  - `ESPPORT` / first positional argument for ESP32-S3.
  - `RP2040_PORT` / first positional argument for RP2040.
  - `ESP_BAUD` for ESP32-S3 baud rate.

## Bundled tools

`package.sh` downloads the pinned `esptool` release (version via
`ESPTOOL_VERSION`, currently 5.3.1) for whichever single target is being
packaged, caching it under `tools/esptool/<target>/`. Default download tries
a domestic GitHub mirror first, falling back to the official GitHub URL.
Override with `ESPTOOL_BASE_URL` to use a single custom source.

`picotool` is intentionally **not** auto-downloaded (no reliable official
prebuilt binaries). If a `picotool` binary is manually placed in
`tools/picotool/<target>/picotool`, `package.sh` includes it in macOS/Linux
bundles; otherwise the flash script falls back to `picotool` on PATH. Not
used on Windows — see the golden rule above.

## Verification after any change here

```sh
python3 scripts/test_click_deploy_package.py   # scaffold guard (needs clean folder)
bash -n click_deploy/package.sh                # packaging script syntax
bash -n click_deploy/scripts/macos_linux/*.sh  # shell flash scripts
```

PowerShell scripts can be parse-checked on any OS with `pwsh`:

```powershell
[System.Management.Automation.Language.Parser]::ParseFile(
    "click_deploy/scripts/windows/flash_esp32s3.ps1", [ref]$null, [ref]$null)
```

Real flashing can only be verified on actual target machines — say so in the
commit/PR if it was not done.
