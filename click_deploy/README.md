# SenseCAP Indicator Click Deploy

This folder is a packaging scaffold for a firmware bundle that can flash both
chips in the SenseCAP Indicator:

- ESP32-S3 screen-side firmware
- RP2040 sensor coprocessor firmware

The repository does not commit generated firmware images or flashing tool
binaries. Build outputs are copied into this folder when preparing a release
package.

## Maintainer workflow

One command from the repository root produces `click_deploy.zip` (firmware +
bundled Windows tools + flash scripts):

```sh
scripts/package_windows_deploy.sh           # package existing build outputs
scripts/package_windows_deploy.sh --build   # rebuild firmware first, then package
```

The script syncs the firmware, downloads the pinned Windows esptool binary,
writes the zip, and then cleans the populated artifacts out of this folder
again (`--keep` skips the cleanup; the package-layout guard
`scripts/test_click_deploy_package.py` expects a scaffold-only checkout).

The equivalent manual steps are:

```sh
./dev build
./dev rp2040 build
./click_deploy/sync_from_build.sh
```

Then zip the `click_deploy/` folder. A complete zip should contain:

```text
click_deploy/
  firmware/
    esp32s3/
      bootloader.bin
      partition-table.bin
      indicator_ha.bin
      flasher_args.json
    rp2040/
      firmware.elf
      firmware.uf2
  tools/
    esptool/
      windows-amd64/esptool.exe
  macos_linux/
  windows/
```

The ESP32-S3 flash scripts prefer the bundled `tools/esptool/` binary and fall
back to `esptool` / `esptool.py` on PATH. The RP2040 needs no flashing tool:
in BOOTSEL mode it appears as a USB drive named `RPI-RP2`, and copying
`firmware.uf2` onto it flashes the chip (the macOS/Linux scripts still use
`picotool` from PATH).

### Bundled Windows tools

For a fully self-contained Windows package, download the official prebuilt
binary and place it here (the path is what the PowerShell script looks for):

```text
tools/esptool/windows-amd64/esptool.exe     # from https://github.com/espressif/esptool/releases
                                            # (esptool-vX.Y.Z-windows-amd64.zip, tested with v5.3.1)
```

picotool is intentionally **not** bundled for Windows: picotool there needs a
Zadig/WinUSB driver to talk to the BOOTSEL device, while the UF2-drive method
works with the built-in Windows mass-storage driver.

Note: esptool v5 prints deprecation warnings for the underscore-style
arguments (`write_flash`, `--flash_mode`, ...) used by the flash scripts; they
still work, and the same arguments also run on the older esptool v4 that ships
with ESP-IDF.

## macOS / Linux

```sh
cd click_deploy
./macos_linux/install.sh
./macos_linux/flash_all.sh
```

To pin serial ports:

```sh
ESPPORT=/dev/cu.usbmodemXXXX ./macos_linux/flash_esp32s3.sh
RP2040_PORT=/dev/cu.usbmodemYYYY ./macos_linux/flash_rp2040.sh
```

## Windows PowerShell

```powershell
cd click_deploy
.\windows\flash_all.ps1
```

If Windows blocks the scripts with an execution-policy error, run them once
with:

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\flash_all.ps1
```

To pin serial ports:

```powershell
$env:ESPPORT = "COM7"
$env:RP2040_PORT = "COM8"
.\windows\flash_all.ps1
```
