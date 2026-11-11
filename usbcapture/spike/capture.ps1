param(
    [Parameter(Mandatory = $true)][string]$Udid
)

$tool = Join-Path $PSScriptRoot 'tools\quicktime_video_hack\quicktime_video_hack.exe'
if (-not (Test-Path $tool)) {
    throw "quicktime_video_hack not built: $tool (see README.md)"
}

$out = Join-Path $PSScriptRoot ('out\capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.h264')
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null

& $tool -udid $Udid -o $out
"output=$out"
& ffprobe -hide_banner $out
