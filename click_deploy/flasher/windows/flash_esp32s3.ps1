$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "..\.."))

# In a packaged bundle, firmware and tools sit next to the script.
# In the repo scaffold, they live two levels up in click_deploy/.
$BundleFw = Join-Path $ScriptDir "firmware"
$RepoFw = Join-Path $RepoRoot "firmware"
$FwDir = if (Test-Path (Join-Path $BundleFw "esp32s3\indicator_ha.bin")) { $BundleFw } else { $RepoFw }

$BundleTools = Join-Path $ScriptDir "tools"
$RepoTools = Join-Path $RepoRoot "tools"
$ToolsDir = if (Test-Path $BundleTools) { $BundleTools } else { $RepoTools }

$BundledEsptool = Join-Path $ToolsDir "esptool\windows-amd64\esptool.exe"

$Baud = if ($env:ESP_BAUD) { $env:ESP_BAUD } else { "460800" }
$Port = if ($args.Count -gt 0) { $args[0] } elseif ($env:ESPPORT) { $env:ESPPORT } else { "" }

if (Test-Path $BundledEsptool) {
    $Esptool = $BundledEsptool
} else {
    $cmd = Get-Command esptool -ErrorAction SilentlyContinue
    if (!$cmd) {
        $cmd = Get-Command esptool.py -ErrorAction SilentlyContinue
    }
    if (!$cmd) {
        throw "No bundled esptool and no esptool on PATH."
    }
    $Esptool = $cmd.Source
}

foreach ($file in @("bootloader.bin", "partition-table.bin", "indicator_ha.bin")) {
    $path = Join-Path $FwDir "esp32s3\$file"
    if (!(Test-Path $path)) {
        throw "Missing ESP32-S3 firmware file: $path"
    }
}

$PortArgs = @()
if ($Port) {
    $PortArgs = @("--port", $Port)
} else {
    Write-Host "No ESP32-S3 port supplied; esptool will autodetect."
    Write-Host "To pin a port: `$env:ESPPORT='COM7'; .\flash_esp32s3.ps1"
}

& $Esptool --chip esp32s3 @PortArgs --baud $Baud `
    --before default_reset --after hard_reset `
    write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m `
    0x0 (Join-Path $FwDir "esp32s3\bootloader.bin") `
    0x8000 (Join-Path $FwDir "esp32s3\partition-table.bin") `
    0x10000 (Join-Path $FwDir "esp32s3\indicator_ha.bin")

if ($LASTEXITCODE -ne 0) {
    throw "esptool failed with exit code $LASTEXITCODE"
}
