<#
.SYNOPSIS
    建置 App 與 libuxplay 的獨立 Doxygen API 參考。
.DESCRIPTION
    預設先產生 libuxplay 的標籤檔，再產生 App 文件與跨專案連結。
    使用本機 Doxygen；找不到時才使用 Docker。文件與警告不進版控。
.PARAMETER Target
    All 建置兩套；App 亦重建引擎相依文件；Libuxplay 只建置引擎。
.PARAMETER Open
    建置成功後開啟所選文件，All 預設開啟 App。
.PARAMETER ForceDocker
    明確使用 Docker 的 Ubuntu 環境建置。
.EXAMPLE
    ./scripts/build_docs.ps1 -Target All
.EXAMPLE
    ./scripts/build_docs.ps1 -Target Libuxplay -Open
#>
[CmdletBinding()]
param(
    [ValidateSet('All', 'App', 'Libuxplay')]
    [string]$Target = 'All',
    [switch]$Open,
    [switch]$ForceDocker
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$engineRoot = Join-Path $repoRoot 'libuxplay'
$projects = @(@{ Name = 'libuxplay'; Root = $engineRoot })
if ($Target -ne 'Libuxplay') {
    $projects += @{ Name = 'uxplay-windows'; Root = $repoRoot }
}

$candidates = @()
if (-not $ForceDocker) {
    if ($env:DOXYGEN) { $candidates += $env:DOXYGEN }
    $command = Get-Command doxygen -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    if ($env:MSYSTEM_PREFIX) {
        $candidates += Join-Path $env:MSYSTEM_PREFIX 'bin/doxygen.exe'
    }
    if ($env:MSYS2_ROOT) {
        $candidates += Join-Path $env:MSYS2_ROOT 'ucrt64/bin/doxygen.exe'
    }
}
$doxygen = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
$stopwatch = [Diagnostics.Stopwatch]::StartNew()

foreach ($project in $projects) {
    $config = Join-Path $project.Root 'Doxyfile'
    New-Item -ItemType Directory -Force -Path (Join-Path $project.Root 'build/doxygen') | Out-Null
    if (-not (Test-Path -LiteralPath $config)) { throw "Missing Doxyfile: $config" }
}

if ($doxygen) {
    foreach ($project in $projects) {
        Write-Host "==> Doxygen: $($project.Name)" -ForegroundColor Cyan
        Push-Location $project.Root
        try {
            & $doxygen Doxyfile
            if ($LASTEXITCODE -ne 0) { throw "Doxygen failed: $($project.Name)" }
        } finally {
            Pop-Location
        }
    }
} else {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw 'Doxygen and Docker are unavailable. Set DOXYGEN or install Doxygen.'
    }
    $generate = 'cd /src/libuxplay && doxygen Doxyfile'
    if ($Target -ne 'Libuxplay') { $generate += ' && cd /src && doxygen Doxyfile' }
    & docker run --rm -v "${repoRoot}:/src" -w /src ubuntu:24.04 bash -lc `
        "apt-get update -qq >/dev/null && apt-get install -y -qq doxygen >/dev/null && $generate"
    if ($LASTEXITCODE -ne 0) { throw 'Dockerized Doxygen failed.' }
}

foreach ($project in $projects) {
    $folder = if ($project.Name -eq 'libuxplay') { 'libuxplay' } else { 'app' }
    $index = Join-Path $project.Root "build/doxygen/$folder/index.html"
    if (-not (Test-Path -LiteralPath $index)) { throw "Missing generated index: $index" }
    $warnings = Join-Path $project.Root 'doxygen-warnings.log'
    if ((Test-Path -LiteralPath $warnings) -and (Get-Item -LiteralPath $warnings).Length -gt 0) {
        Get-Content -LiteralPath $warnings -Encoding UTF8 | Write-Host
        throw "Doxygen warnings: $warnings"
    }
    Write-Host "API ready: $index" -ForegroundColor Green
}
$tagFile = Join-Path $engineRoot 'build/doxygen/libuxplay/libuxplay.tag'
if (-not (Test-Path -LiteralPath $tagFile)) { throw "Missing engine tag file: $tagFile" }
# Keep app/libuxplay as siblings, matching changelogkit's exported mount names.
# The engine's own output remains available to its independent configuration.
$apiRoot = Join-Path $repoRoot 'build/doxygen'
New-Item -ItemType Directory -Force -Path $apiRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $engineRoot 'build/doxygen/libuxplay') `
    -Destination $apiRoot -Recurse -Force
$stopwatch.Stop()
Write-Host ("Completed in {0:N1}s" -f $stopwatch.Elapsed.TotalSeconds)
if ($Open) {
    $folder = if ($Target -eq 'Libuxplay') { 'libuxplay' } else { 'app' }
    Start-Process (Join-Path $apiRoot "$folder/index.html")
}
