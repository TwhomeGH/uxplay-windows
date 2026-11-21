<#
.SYNOPSIS
    增量產生兩套 API，或快速檢查參考文件是否過期。
.DESCRIPTION
    共用 Python 協調器與 CI 相同的差異、建置及驗證流程。
    來源使用內容雜湊；失敗候選與前一版產物保留在 build/doxygen/runs。
.PARAMETER Target
    All／App 檢查引擎相依再處理 App；Libuxplay 只處理引擎。
.PARAMETER Check
    唯讀檢查。最新回傳 0，過期回傳 1，工具或建置錯誤回傳 2。
.PARAMETER Force
    忽略成功基線並強制重建。
.PARAMETER Open
    成功後開啟選定 API 入口。
.PARAMETER ForceDocker
    使用 Docker；因套件版本需進容器才能確認，不跳過建置。
.EXAMPLE
    ./scripts/build_docs.ps1 -Check
.EXAMPLE
    ./scripts/build_docs.ps1 -Force -Open
#>
[CmdletBinding()]
param(
    [ValidateSet('All', 'App', 'Libuxplay')]
    [string]$Target = 'All',
    [switch]$Check,
    [switch]$Force,
    [switch]$Open,
    [switch]$ForceDocker
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($Check -and ($Force -or $Open)) {
    [Console]::Error.WriteLine('-Check cannot be combined with -Force or -Open.')
    exit 2
}
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $python) {
    [Console]::Error.WriteLine('Python 3.10+ is required for API documentation checks.')
    exit 2
}

$candidates = @($env:DOXYGEN)
$command = Get-Command doxygen -ErrorAction SilentlyContinue
if ($command) { $candidates += $command.Source }
if ($env:MSYSTEM_PREFIX) { $candidates += Join-Path $env:MSYSTEM_PREFIX 'bin/doxygen.exe' }
if ($env:MSYS2_ROOT) { $candidates += Join-Path $env:MSYS2_ROOT 'ucrt64/bin/doxygen.exe' }
$doxygen = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
$arguments = @((Join-Path $PSScriptRoot 'build_api_docs.py'), '--target', $Target)
if ($Check) { $arguments += '--check' }
if ($Force) { $arguments += '--force' }
if ($ForceDocker) { $arguments += '--docker' }
elseif ($doxygen) { $arguments += @('--doxygen', $doxygen) }
& $python.Source @arguments
$result = $LASTEXITCODE
if ($result -eq 0 -and $Open) {
    $folder = if ($Target -eq 'Libuxplay') { 'libuxplay' } else { 'app' }
    Start-Process (Join-Path $repoRoot "build/doxygen/$folder/index.html")
}
exit $result
