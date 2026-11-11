param(
    [Parameter(Mandatory = $true)][string]$Udid,
    [int]$Seconds = 15
)

$prefix = 'H:\FIX_VIAP\msys64\ucrt64'
$env:PATH = (Join-Path $prefix 'bin') + ';' + $env:PATH

$tool = Join-Path $PSScriptRoot 'tools\quicktime_video_hack\quicktime_video_hack.exe'
if (-not (Test-Path $tool)) {
    throw "quicktime_video_hack not built: $tool (see README.md)"
}

$out = Join-Path $PSScriptRoot ('out\capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.h264')
$wav = [System.IO.Path]::ChangeExtension($out, '.wav')
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
$p = Start-Process -FilePath $tool `
    -ArgumentList @('record', $out, $wav, '--udid', $Udid) `
    -PassThru -NoNewWindow
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
"output=$out"
& ffprobe -hide_banner $out
