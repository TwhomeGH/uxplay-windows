# Checks the QuickTime USB capture spike prerequisites.

$ErrorActionPreference = 'Continue'
$prefix = 'H:\FIX_VIAP\msys64\ucrt64'
$fail = @()

$service = Get-Service 'Apple Mobile Device Service' -ErrorAction SilentlyContinue
if (-not $service -or $service.Status -ne 'Running') {
    $fail += 'Apple Mobile Device Service is not running'
}

$idevice = Join-Path $prefix 'bin\idevice_id.exe'
if (-not (Test-Path $idevice)) {
    $fail += "idevice_id not found: $idevice"
} else {
    $devices = & $idevice -l 2>&1
    if (-not $devices) {
        $fail += 'No device through usbmuxd: unlock it and tap Trust This Computer'
    } else {
        "devices: $devices"
    }
}

foreach ($tool in @('go', 'ffprobe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        $fail += "$tool not found"
    }
}

if ($fail.Count -gt 0) {
    $fail | ForEach-Object { "FAIL: $_" }
    exit 1
}

'OK'
