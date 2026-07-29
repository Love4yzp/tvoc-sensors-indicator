$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir "..\.."))

# In a packaged bundle, firmware sits next to the script.
# In the repo scaffold, it lives two levels up in click_deploy/.
$BundleFw = Join-Path $ScriptDir "firmware"
$RepoFw = Join-Path $RepoRoot "firmware"
$FwDir = if (Test-Path (Join-Path $BundleFw "rp2040\firmware.uf2")) { $BundleFw } else { $RepoFw }

$Uf2 = Join-Path $FwDir "rp2040\firmware.uf2"
$Port = if ($args.Count -gt 0) { $args[0] } elseif ($env:RP2040_PORT) { $env:RP2040_PORT } else { "" }

if (!(Test-Path $Uf2)) {
    throw "Missing RP2040 firmware: $Uf2"
}

# In BOOTSEL mode the RP2040 shows up as a USB mass-storage drive named
# RPI-RP2. Copying the .uf2 onto it flashes the chip — no extra driver or
# picotool needed on Windows.
function Get-Rp2Drive {
    $vol = Get-Volume |
        Where-Object { $_.FileSystemLabel -eq "RPI-RP2" -and $_.DriveLetter } |
        Select-Object -First 1
    if ($vol) {
        return "$($vol.DriveLetter):\"
    }
    return ""
}

function Find-Rp2040Port {
    $ports = Get-CimInstance Win32_PnPEntity |
        Where-Object {
            $_.Name -match "\(COM\d+\)" -and
            ($_.PNPDeviceID -match "VID_2886&PID_0050" -or $_.PNPDeviceID -match "VID_2E8A&PID_00C0")
        }
    foreach ($p in $ports) {
        if ($p.Name -match "(COM\d+)") {
            return $Matches[1]
        }
    }
    return ""
}

function Touch-Serial1200 {
    param([string]$PortName)
    try {
        $serial = New-Object System.IO.Ports.SerialPort $PortName, 1200
        $serial.Open()
        Start-Sleep -Milliseconds 100
        $serial.Close()
    } catch {
        Write-Host "Serial reset failed; continuing in case BOOTSEL is already active."
    }
}

$Drive = Get-Rp2Drive

if ($Drive) {
    Write-Host "RP2040 BOOTSEL drive is already present at $Drive"
} else {
    if (!$Port) {
        $Port = Find-Rp2040Port
    }
    if (!$Port) {
        throw "RP2040 serial port was not auto-detected. Set `$env:RP2040_PORT='COMx' and retry."
    }

    Write-Host "Triggering RP2040 BOOTSEL through $Port"
    Touch-Serial1200 $Port

    Write-Host "Waiting for the RPI-RP2 drive to appear..."
    for ($i = 0; $i -lt 50; $i++) {
        Start-Sleep -Milliseconds 300
        $Drive = Get-Rp2Drive
        if ($Drive) {
            break
        }
    }

    if (!$Drive) {
        throw "The RPI-RP2 drive did not appear. Unplug and replug the USB cable, then retry."
    }
}

Write-Host "Flashing RP2040: copying firmware.uf2 to $Drive"
Copy-Item $Uf2 $Drive
Write-Host "RP2040 flashed. The RPI-RP2 drive disappears as the chip reboots."
