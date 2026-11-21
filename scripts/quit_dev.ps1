param(
    [Parameter(Mandatory = $true)]
    [string]$Exe
)

# Ask the running instance to quit through its IPC server first.  A force-kill
# is only a last resort: it leaves the named pipe, Frame Server camera and
# AirPlay engine state behind, which contaminates the next test run.
$process = Get-Process -Name uxplay-windows -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $Exe } |
    Select-Object -First 1
if (-not $process) {
    exit 0
}

& $Exe --quit | Out-Null

for ($i = 0; $i -lt 50; $i++) {
    Start-Sleep -Milliseconds 100
    if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        exit 0
    }
}

# The process did not exit cleanly after five seconds.  Keep the fallback
# explicit so a hung process does not block a rebuild forever.
$stillRunning = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
if ($stillRunning) {
    Stop-Process -Id $process.Id -Force
    exit 0
}
