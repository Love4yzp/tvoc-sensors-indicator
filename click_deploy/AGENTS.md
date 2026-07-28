# click_deploy — Agent Guide

This folder is the packaging scaffold for the no-toolchain flashing bundle of
the SenseCAP Indicator firmware (ESP32-S3 screen side + RP2040 sensor
coprocessor). The shipped artifact is `click_deploy.zip` at the repository
root. Read this before changing anything here.

## Golden rules

- **Never commit firmware images or tool binaries** (`.bin`, `.uf2`, `.elf`,
  `.exe`). They are gitignored; the checkout must stay scaffold-only.
  `scripts/test_click_deploy_package.py` enforces this and fails as soon as
  populated artifacts exist in this folder — so either clean after packaging
  (the packaging script's default) or accept the guard failing locally while
  the folder is populated (`--keep`).
- **Do not reintroduce picotool on Windows.** picotool cannot talk to the
  RP2040 BOOTSEL device without a Zadig/WinUSB driver on Windows (confirmed in
  the field: "RP2040 device ... appears to be in BOOTSEL mode, but picotool
  was unable to connect"). The Windows script flashes the RP2040 by copying
  `firmware.uf2` onto the chip's `RPI-RP2` USB mass-storage drive, which uses
  the built-in Windows driver. The macOS/Linux scripts still use `picotool`
  from PATH — that is fine there, do not "unify" them onto the Windows scheme
  without a reason.
- **Do not change ESP32-S3 flash offsets/settings casually.** The PowerShell
  and shell scripts hardcode `0x0 bootloader / 0x8000 partition-table /
  0x10000 app` with `--flash_mode dio --flash_size 8MB --flash_freq 80m`,
  matching `build/flasher_args.json`. If `partitions.csv` or the ESP-IDF
  config ever changes these, update the scripts and this file together.
- MQTT topics, partition layout, and other product behavior are out of scope
  here — this folder only packages and flashes what `build/` produced.

## Packaging workflow (maintainer)

One command from the repository root:

```sh
scripts/package_windows_deploy.sh           # package existing build outputs
scripts/package_windows_deploy.sh --build   # rebuild both firmwares first
scripts/package_windows_deploy.sh --keep    # leave folder populated afterwards
```

The script syncs `build/` + `rp2040/.pio/build/` artifacts via
`sync_from_build.sh`, downloads the pinned Windows `esptool.exe` (version via
`ESPTOOL_VERSION`, currently 5.3.1) into `tools/esptool/windows-amd64/`,
writes `click_deploy.zip`, then cleans the populated artifacts unless `--keep`
is given.

## Layout the scripts expect

```text
click_deploy/
  firmware/esp32s3/   bootloader.bin, partition-table.bin, indicator_ha.bin, flasher_args.json
  firmware/rp2040/    firmware.elf, firmware.uf2
  tools/esptool/windows-amd64/esptool.exe   # only bundled tool; esptool.py on PATH is the fallback
  macos_linux/        flash_all.sh, flash_esp32s3.sh, flash_rp2040.sh (picotool), install.sh
  windows/            flash_all.ps1, flash_esp32s3.ps1, flash_rp2040.ps1 (UF2-drive copy)
```

## How the Windows flow works (and why)

1. `flash_all.ps1` runs `flash_rp2040.ps1` first, then `flash_esp32s3.ps1`.
2. `flash_rp2040.ps1`: find the RP2040 COM port (`VID_2886&PID_0050` Seeed or
   `VID_2E8A&PID_00C0`), open it at 1200 baud to trigger BOOTSEL, wait for a
   volume labeled `RPI-RP2` (up to ~15 s), `Copy-Item firmware.uf2` onto it.
   The drive vanishes as the chip reboots — that is success, not an error.
3. `flash_esp32s3.ps1`: prefers the bundled `tools/esptool/windows-amd64/
   esptool.exe`, falls back to `esptool`/`esptool.py` on PATH, then
   `write_flash` with the offsets above. esptool v5 prints deprecation
   warnings for the underscore-style args; they still work, and the same args
   also run on the esptool v4 shipped with ESP-IDF — do not "modernize" them
   to hyphens unless the fallback story is reconsidered.
4. Port pinning for end users: `$env:ESPPORT="COM7"`, `$env:RP2040_PORT="COM8"`.
   Execution-policy workaround: `powershell -ExecutionPolicy Bypass -File .\windows\flash_all.ps1`.

## Verification after any change here

```sh
python3 scripts/test_click_deploy_package.py   # scaffold guard (needs clean folder)
bash -n scripts/package_windows_deploy.sh      # packaging script syntax
```

PowerShell scripts can be parse-checked on any OS with pwsh:
`[System.Management.Automation.Language.Parser]::ParseFile(...)`.
Real flashing can only be verified on an actual Windows machine — say so in
the commit/PR if it was not done.
