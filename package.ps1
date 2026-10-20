param([ValidateSet('x64','x86')][string[]]$Architecture = @('x64','x86'))
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$version = (Get-Content -LiteralPath VERSION -Raw).Trim()
$distRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'dist')) + [IO.Path]::DirectorySeparatorChar
$archives = @()
foreach ($arch in $Architecture) {
    $binary = Join-Path $PSScriptRoot "build/bin/$arch/Markdown_with_Mermaid.dll"
    if (!(Test-Path -LiteralPath $binary)) { throw "Build $arch first." }
    $buildInfoPath = Join-Path (Split-Path -Parent $binary) 'build-info.json'
    if (!(Test-Path -LiteralPath $buildInfoPath)) { throw "Rebuild $arch with the current build.ps1 first." }
    $buildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
    if ($buildInfo.profile -ne 'Release' -or $buildInfo.version -ne $version -or $buildInfo.architecture -ne $arch) { throw "Expected current Release build for $arch." }
    $binaryVersion = (Get-Item -LiteralPath $binary).VersionInfo
    $actualVersion = "$($binaryVersion.FileMajorPart).$($binaryVersion.FileMinorPart).$($binaryVersion.FileBuildPart).$($binaryVersion.FilePrivatePart)"
    if ($actualVersion -ne $buildInfo.fileVersion) { throw "DLL VERSIONINFO mismatch for $arch." }
    $out = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "dist/$arch/Markdown_with_Mermaid"))
    if (!$out.StartsWith($distRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Package path escapes dist.' }
    if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
    New-Item -ItemType Directory -Force $out,"$out/assets","$out/$arch","$out/licenses" | Out-Null
    Copy-Item -LiteralPath $binary -Destination $out
    Copy-Item assets/* "$out/assets" -Recurse
    Copy-Item "tools/webview2/build/native/$arch/WebView2Loader.dll" "$out/$arch"
    Copy-Item vendor/LICENSE*,vendor/NOTICE*,vendor/bundled-dependencies.json "$out/licenses"
    $provenance = Get-Content -LiteralPath vendor/provenance.json -Raw | ConvertFrom-Json
    $provenance.files = @($provenance.files | Where-Object { $_.path -match '^(assets/|vendor/npp-sdk/)' })
    $provenance | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$out/licenses/provenance.json" -Encoding utf8
    Copy-Item README.md,LICENSE $out
    foreach ($optionalFile in @('README.ru.md','CHANGELOG.md')) {
        if (Test-Path -LiteralPath $optionalFile) { Copy-Item -LiteralPath $optionalFile -Destination $out }
    }
    New-Item -ItemType Directory -Force "$out/docs" | Out-Null
    Copy-Item docs/PDF-EXPORT.md,docs/DOCX-EXPORT.md,docs/DATA-PRIVACY.md,docs/LICENSING.md "$out/docs"
    if (Test-Path -LiteralPath docs/screenshots) { Copy-Item -LiteralPath docs/screenshots -Destination "$out/docs/screenshots" -Recurse }
    foreach ($optionalDoc in @('BUILDING.md','TESTING.md','BUILD-WORKFLOW.yml')) {
        $documentationPath = Join-Path $PSScriptRoot "docs/$optionalDoc"
        if (Test-Path -LiteralPath $documentationPath) { Copy-Item -LiteralPath $documentationPath -Destination "$out/docs" }
    }
    Copy-Item docs/Mermaid-examples.md "$out/Mermaid-examples.md"
    $archive = Join-Path $PSScriptRoot "dist/Markdown_with_Mermaid-$version-$arch.zip"
    $archiveInputs = @(Get-ChildItem -LiteralPath $out -Force | ForEach-Object { $_.FullName })
    Compress-Archive -LiteralPath $archiveInputs -DestinationPath $archive -Force
    $archives += $archive
}
$archives | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_) } | Set-Content dist/SHA256SUMS.txt -Encoding ascii
