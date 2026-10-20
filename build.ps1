param(
    [switch]$SkipX86,
    [ValidateSet('Release','Development')][string]$Profile = 'Release'
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$zig = Join-Path $PSScriptRoot 'tools/zig-windows-x86_64-0.13.0/zig.exe'
if (!(Test-Path -LiteralPath $zig)) { throw 'Run bootstrap.ps1 first.' }
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot 'build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot 'build/zig-local-cache'
New-Item -ItemType Directory -Force build,dist | Out-Null
$version = (Get-Content -LiteralPath VERSION -Raw).Trim()
if ($version -notmatch '^\d+(\.\d+){0,3}$') { throw 'VERSION must contain one to four numeric components.' }
$versionParts = @($version.Split('.') | ForEach-Object { [int]$_ })
if (@($versionParts | Where-Object { $_ -gt 65535 }).Count) { throw 'Windows version components must be between 0 and 65535.' }
while ($versionParts.Count -lt 4) { $versionParts += 0 }
$fileVersion = $versionParts -join '.'
$versionHeader = @"
#pragma once
#define MWM_VERSION_W L"$version"
#define MWM_FILE_VERSION $($versionParts -join ',')
#define MWM_FILE_VERSION_STRING "$fileVersion\0"
"@
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'build/plugin-version.h'), $versionHeader)
$runtimeConfiguration = '#pragma once' + "`r`n"
if ($Profile -eq 'Development') {
    $runtimePath = (Join-Path $PSScriptRoot 'runtime').Replace('\','\\').Replace('"','\"')
    $runtimeConfiguration += '#define MWM_DEVELOPMENT_RUNTIME_DIRECTORY L"' + $runtimePath + '"' + "`r`n"
}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'build/runtime-path.h'), $runtimeConfiguration)
$architectures = if ($SkipX86) { @('x64') } else { @('x64','x86') }
foreach ($arch in $architectures) {
    $target = if ($arch -eq 'x64') { 'x86_64-windows-gnu' } else { 'x86-windows-gnu' }
    $binary = Join-Path $PSScriptRoot "build/bin/$arch"
    New-Item -ItemType Directory -Force $binary | Out-Null
    [string[]]$releaseFlags = @()
    if ($Profile -eq 'Release') { $releaseFlags = @('-s') }
    & $zig c++ -target $target -std=c++17 -O2 -shared @releaseFlags -Ibuild -Ivendor/npp-sdk -Itools/webview2/build/native/include src/plugin.cpp src/resources.rc src/exports.def -luser32 -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32 -lcomctl32 -lwinhttp -fno-sanitize=undefined -Wno-ignored-attributes -o "$binary/Markdown_with_Mermaid.dll"
    if ($LASTEXITCODE -ne 0) { throw "$arch build failed" }
    [ordered]@{version=$version;fileVersion=$fileVersion;architecture=$arch;profile=$Profile} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $binary 'build-info.json') -Encoding utf8
}
if ($Profile -eq 'Release') {
    & ./package.ps1 -Architecture $architectures
} else {
    Write-Output 'Development DLLs built; public packages are not created in this mode.'
}
