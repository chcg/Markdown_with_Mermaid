param(
  [ValidateSet('x64','x86')][string]$Architecture = 'x64',
  [string]$NotepadDirectory = 'C:\Program Files\Notepad++',
  [string]$DataDirectory = (Join-Path $PSScriptRoot 'runtime')
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $NotepadDirectory 'notepad++.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Notepad++ not found: $exe" }
$running = Get-Process 'notepad++' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
if ($running) { throw 'Close this Notepad++ instance before installation; documents will not be closed automatically.' }
$bytes = [IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
$machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
$wanted = if ($Architecture -eq 'x64') { 0x8664 } else { 0x14c }
if ($machine -ne $wanted) { throw 'Plugin architecture does not match Notepad++.' }
$source = Join-Path $PSScriptRoot "dist/$Architecture/Markdown_with_Mermaid"
if (!(Test-Path "$source/Markdown_with_Mermaid.dll")) { throw 'Build the plugin first.' }
$destination = Join-Path $NotepadDirectory 'plugins/Markdown_with_Mermaid'
New-Item -ItemType Directory -Force $destination,$DataDirectory | Out-Null
Copy-Item "$source/Markdown_with_Mermaid.dll" $destination -Force
Copy-Item "$source/assets" $destination -Recurse -Force
Copy-Item "$source/$Architecture" $destination -Recurse -Force
Copy-Item "$source/licenses" $destination -Recurse -Force
Copy-Item "$source/docs" $destination -Recurse -Force
Copy-Item "$source/README.md","$source/Mermaid-examples.md","$source/LICENSE" $destination -Force
[IO.File]::WriteAllText((Join-Path $destination 'Markdown_with_Mermaid.ini'), "[Paths]`r`nDataDirectory=$DataDirectory`r`n", [Text.Encoding]::Unicode)
Write-Output "Installed: $destination"
