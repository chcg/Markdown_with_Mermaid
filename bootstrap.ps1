$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
New-Item -ItemType Directory -Force tools | Out-Null
function Get-VerifiedArchive([string]$Url, [string]$Path, [string]$Sha256) {
    if (!(Test-Path -LiteralPath $Path)) {
        & curl.exe --fail --location --retry 2 --max-time 300 $Url -o $Path
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
    }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Sha256) { throw "SHA256 mismatch: $Path" }
}
if (!(Test-Path tools/zig-windows-x86_64-0.13.0/zig.exe)) {
    Get-VerifiedArchive 'https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip' 'tools/zig.zip' 'D859994725EF9402381E557C60BB57497215682E355204D754EE3DF75EE3C158'
    Expand-Archive tools/zig.zip tools -Force
}
if (!(Test-Path tools/webview2/build/native/include/WebView2.h)) {
    Get-VerifiedArchive 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.2903.40/microsoft.web.webview2.1.0.2903.40.nupkg' 'tools/webview2.zip' 'EF128016DD1E51C59178C827ED5B8AA3322C57AFA8675D930F8109505542AD74'
    Expand-Archive tools/webview2.zip tools/webview2 -Force
}
if ((Get-FileHash assets/mermaid.min.js -Algorithm SHA256).Hash -ne '18327BEF70D96FB505FE7287D9F6A7362EBF07FF6576DDFAFFB1A06F3E1A2954') { throw 'Unexpected Mermaid bundle' }
Write-Output 'Build tools and Mermaid 11.16.1 are ready.'
