# Build the Microsoft Windows-Camera virtual-camera sample with the UxPlay
# frame-pipe overlay. This script is intentionally separate from the normal
# MinGW/Qt build: the Media Foundation media source must be compiled with MSVC.
param(
    [string]$SampleRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Windows-Camera'),
    [switch]$Installer
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$sampleRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SampleRoot)
$sampleDir = Join-Path $sampleRoot 'Samples\VirtualCamera'

function Find-VsDevCmd {
    $roots = @(
        'C:\Program Files\Microsoft Visual Studio\2022',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022'
    )
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $candidate = Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName 'Common7\Tools\VsDevCmd.bat' } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1
        if ($candidate) { return $candidate }
    }
    throw 'Visual Studio 2022 C++ tools were not found.'
}

if (-not (Test-Path (Join-Path $sampleDir 'VirtualCameraMediaSource\VirtualCameraMediaSource.vcxproj'))) {
    New-Item -ItemType Directory -Force -Path $sampleRoot | Out-Null
    git clone --depth 1 https://github.com/microsoft/Windows-Camera.git $sampleRoot
}

$nuget = Join-Path $repoRoot 'build\nuget.exe'
if (-not (Test-Path $nuget)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $nuget) | Out-Null
    Invoke-WebRequest -Uri 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe' -OutFile $nuget
}

$sourceDir = Join-Path $sampleDir 'VirtualCameraMediaSource'
& $nuget restore (Join-Path $sourceDir 'packages.config') `
    -PackagesDirectory (Join-Path $sourceDir 'packages') | Out-Host

# Apply the UxPlay frame-pipe overlay after restore.
Copy-Item (Join-Path $repoRoot 'virtualcam\media-source\*') $sourceDir -Force
Copy-Item (Join-Path $repoRoot 'virtualcam\protocol\SharedFrame.h') $sourceDir -Force

# The CppWinRT NuGet package in this sample expects cppwinrt.exe under
# build\native; newer package layouts keep it under bin.
$cppWinRt = Get-ChildItem (Join-Path $sourceDir 'packages') -Directory -Filter 'Microsoft.Windows.CppWinRT*' |
    Select-Object -First 1
if ($cppWinRt) {
    $bin = Join-Path $cppWinRt.FullName 'bin\cppwinrt.exe'
    if (Test-Path $bin) {
        Copy-Item $bin (Join-Path $cppWinRt.FullName 'build\native\cppwinrt.exe') -Force
    }
}

$vsDevCmd = Find-VsDevCmd
$project = Join-Path $sourceDir 'VirtualCameraMediaSource.vcxproj'
$buildBat = Join-Path (Split-Path $nuget) 'build-vcam.ps1.bat'
@"
@echo off
call "$vsDevCmd" -arch=x64 -host_arch=x64
msbuild "$project" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
"@ | Set-Content -Path $buildBat -Encoding ASCII
& cmd.exe /c $buildBat
if ($LASTEXITCODE -ne 0) { throw "media source build failed ($LASTEXITCODE)" }

if ($Installer) {
    $installerDir = Join-Path $sampleDir 'VirtualCamera_Installer'
    & $nuget restore (Join-Path $installerDir 'packages.config') `
        -PackagesDirectory (Join-Path $installerDir 'packages') | Out-Host
    $installerCppWinRt = Get-ChildItem (Join-Path $installerDir 'packages') -Directory -Filter 'Microsoft.Windows.CppWinRT*' |
        Select-Object -First 1
    if ($installerCppWinRt) {
        $bin = Join-Path $installerCppWinRt.FullName 'bin\cppwinrt.exe'
        if (Test-Path $bin) {
            Copy-Item $bin (Join-Path $installerCppWinRt.FullName 'build\native\cppwinrt.exe') -Force
        }
    }
    $installerProject = Join-Path $installerDir 'VirtualCamera_Installer.vcxproj'
    $projectText = Get-Content $installerProject -Raw
    if ($projectText -notmatch '<LanguageStandard>') {
        $projectText = $projectText -replace '(<AdditionalOptions>%\(AdditionalOptions\) /permissive- /bigobj</AdditionalOptions>)', "`$1`n      <LanguageStandard>stdcpp17</LanguageStandard>"
        Set-Content -Path $installerProject -Value $projectText -Encoding UTF8
    }
    $installerBat = Join-Path (Split-Path $nuget) 'build-vcam-installer.ps1.bat'
    @"
@echo off
call "$vsDevCmd" -arch=x64 -host_arch=x64
msbuild "$installerProject" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
"@ | Set-Content -Path $installerBat -Encoding ASCII
    & cmd.exe /c $installerBat
    if ($LASTEXITCODE -ne 0) { throw "installer build failed ($LASTEXITCODE)" }
}

Write-Host "Virtual camera media source: $(Join-Path $sourceDir 'x64\Release\VirtualCameraMediaSource.dll')"
