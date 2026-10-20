param([ValidateSet('x64','x86')][string]$Architecture = 'x64', [string]$PdfRegressionMarkdown = '', [string]$DocxImageMarkdown = '', [ValidateSet('Integration','Lifecycle')][string]$Suite = 'Integration')
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot 'build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot 'build/zig-local-cache'
$out = Join-Path $PSScriptRoot "build/hosts/$Architecture"
New-Item -ItemType Directory -Force $out,"$out/$Architecture","$out/assets" | Out-Null
Copy-Item assets/* "$out/assets" -Recurse -Force
Copy-Item "tools/webview2/build/native/$Architecture/WebView2Loader.dll" "$out/$Architecture" -Force
$target = if ($Architecture -eq 'x64') { 'x86_64-windows-gnu' } else { 'x86-windows-gnu' }
$testSource = if ($Suite -eq 'Lifecycle') { 'tests/lifecycle.cpp' } else { 'tests/host.cpp' }
$testExecutable = if ($Suite -eq 'Lifecycle') { "$out/lifecycle-host.exe" } else { "$out/test-host.exe" }
& tools/zig-windows-x86_64-0.13.0/zig.exe c++ -target $target -std=c++17 -O2 -Ibuild -Ivendor/npp-sdk -Itools/webview2/build/native/include $testSource src/resources.rc -luser32 -lgdi32 -lole32 -lshell32 -lshlwapi -luuid -lcomdlg32 -lcomctl32 -lwinhttp -municode -fno-sanitize=undefined -Wno-ignored-attributes -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw 'Test host compilation failed' }
New-Item -ItemType Directory -Force build/previews | Out-Null
$extraArguments = @()
if ($PdfRegressionMarkdown -or $DocxImageMarkdown) { $extraArguments = @($(if ($PdfRegressionMarkdown) { (Resolve-Path -LiteralPath $PdfRegressionMarkdown).Path } else { '' })) }
if ($DocxImageMarkdown) { $extraArguments += (Resolve-Path -LiteralPath $DocxImageMarkdown).Path }
$testOutput = if ($Suite -eq 'Lifecycle') { Join-Path $PSScriptRoot "build/lifecycle-$Architecture" } else { Join-Path $PSScriptRoot "build/previews/$Architecture" }
$testLog = if ($Suite -eq 'Lifecycle') { "build/lifecycle-$Architecture.log" } else { "build/test-$Architecture.log" }
if ($Suite -eq 'Lifecycle') {
    New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
    if (!(Test-Path -LiteralPath "$testOutput/junction-fixture")) { New-Item -ItemType Junction -Path "$testOutput/junction-fixture" -Target (Join-Path $PSScriptRoot 'tests/fixtures') | Out-Null }
}
& $testExecutable (Join-Path $PSScriptRoot 'tests') $testOutput @extraArguments | Tee-Object $testLog
if ($LASTEXITCODE -ne 0) { throw 'Integration tests failed' }
