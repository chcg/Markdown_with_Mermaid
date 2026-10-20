param(
    [ValidateSet('x64','x86')][string[]]$Architecture = @('x64','x86'),
    [string]$ReportPath = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
$parts = @($version.Split('.') | ForEach-Object { [int]$_ })
while ($parts.Count -lt 4) { $parts += 0 }
$expectedVersion = $parts -join '.'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$hashes = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $projectRoot 'dist/SHA256SUMS.txt')) {
    if ($line -notmatch '^(?<hash>[a-f0-9]{64})  (?<file>[^/\\]+)$') { throw 'Invalid SHA256SUMS line.' }
    if ($hashes.ContainsKey($Matches.file)) { throw 'Duplicate checksum entry.' }
    $hashes[$Matches.file] = $Matches.hash
}
if ($hashes.Count -ne $Architecture.Count) { throw 'Checksum file must contain only the requested release artifacts.' }
$results = @()
foreach ($arch in $Architecture) {
    $archiveName = "Markdown_with_Mermaid-$version-$arch.zip"
    $archivePath = Join-Path $projectRoot "dist/$archiveName"
    $binaryPath = Join-Path $projectRoot "build/bin/$arch/Markdown_with_Mermaid.dll"
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($binaryPath)
    $numericVersion = "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart).$($info.FilePrivatePart)"
    if ($numericVersion -ne $expectedVersion) { throw "Wrong numeric DLL version: $arch" }
    if ($info.CompanyName -ne 'Artyom Nikiforov (rhyots)') { throw "Wrong DLL author: $arch" }
    $binaryBytes = [IO.File]::ReadAllBytes($binaryPath)
    $wideText = [Text.Encoding]::Unicode.GetString($binaryBytes)
    $narrowText = [Text.Encoding]::UTF8.GetString($binaryBytes)
    foreach ($privatePath in @($projectRoot,$projectRoot.Replace('\','/'))) {
        if ($wideText.Contains($privatePath) -or $narrowText.Contains($privatePath)) { throw "Compiled workspace path in Release DLL: $arch" }
    }
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if (!$hashes.ContainsKey($archiveName) -or $archiveHash -ne $hashes[$archiveName]) { throw "Wrong ZIP checksum: $arch" }
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if ($name.StartsWith('/') -or $name -match '(^|/)\.\.(/|$)|:' -or $name -match '\.(ini|pdb|obj|lib)$') { throw "Unexpected ZIP entry: $name" }
            if ($name -match '\.(md|txt|json|js|css|html|svg)$') {
                $reader = [IO.StreamReader]::new($entry.Open(), [Text.Encoding]::UTF8, $true)
                try { $entryText = $reader.ReadToEnd() } finally { $reader.Dispose() }
                foreach ($privatePath in @($projectRoot,$projectRoot.Replace('\','/'),$env:USERPROFILE,$env:USERPROFILE.Replace('\','/'))) {
                    if ($privatePath -and $entryText.Contains($privatePath)) { throw "Private path in package text: $name" }
                }
            }
        }
        $loaders = @($zip.Entries | Where-Object { $_.Name -ieq 'WebView2Loader.dll' })
        if ($loaders.Count -ne 1 -or $loaders[0].FullName.Replace('\','/') -ne "$arch/WebView2Loader.dll") { throw 'Exactly one architecture-matched loader is required.' }
        foreach ($required in @('Markdown_with_Mermaid.dll','assets/viewer.html',"$arch/WebView2Loader.dll",'licenses/NOTICE-BundledDependencies.txt','licenses/LICENSE-Scintilla.txt','docs/DATA-PRIVACY.md','LICENSE','README.md')) {
            if (!($zip.Entries | Where-Object { $_.FullName.Replace('\','/') -eq $required })) { throw "Missing ZIP entry: $required" }
        }
        $dll = @($zip.Entries | Where-Object { $_.FullName -eq 'Markdown_with_Mermaid.dll' })
        if ($dll.Count -ne 1) { throw 'DLL must occur exactly once at ZIP root.' }
        $stream = $dll[0].Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $dllHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
        finally { $sha.Dispose(); $stream.Dispose() }
        if ($dllHash -ne (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'Packaged DLL differs from compiled DLL.' }
        $results += [ordered]@{architecture=$arch;version=$numericVersion;archive=$archiveName;sha256=$archiveHash;bytes=(Get-Item -LiteralPath $archivePath).Length;entries=$zip.Entries.Count;rootDll=$true;privateCompiledPath=$false}
    } finally { $zip.Dispose() }
}
$report = [ordered]@{status='PASS';version=$version;architectures=$results}
if ($ReportPath) { $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReportPath -Encoding utf8 }
$report | ConvertTo-Json -Depth 5
