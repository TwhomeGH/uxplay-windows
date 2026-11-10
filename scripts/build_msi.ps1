param(
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$releaseExe = Join-Path $root "release\uxplay-windows.exe"
if (-not (Test-Path $releaseExe)) {
    throw "release\uxplay-windows.exe not found. Stage release\ first (make_release_CI.sh or make_release.sh)."
}

$env:PATH += ";$env:USERPROFILE\.dotnet\tools"
if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    dotnet tool install --global wix
}

$license = Join-Path $root "LICENSE.rtf"
if (-not (Test-Path $license)) {
    Copy-Item (Join-Path $root "docs\LICENSE.rtf") $license
}

Push-Location $root
try {
    wix extension add WixToolset.UI.wixext -acceptEula wix7
    $out = Join-Path $root "uxplaywindows-installer-$Arch.msi"
    wix build -acceptEula wix7 product.wxs -arch $Arch -out $out -ext WixToolset.UI.wixext
    Write-Host "Built: $out"
} finally {
    Pop-Location
}
