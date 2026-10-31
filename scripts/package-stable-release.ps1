param([string]$BuildDirectory = 'switch')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$nro = Join-Path $repo "build\$BuildDirectory\SwitchNOW.nro"
if (-not (Test-Path -LiteralPath $nro)) { throw 'Build SwitchNOW.nro before packaging.' }
$dist = Join-Path $repo 'dist'
$root = Join-Path $dist 'SwitchNOW-1.3.0'
$zip = Join-Path $dist 'SwitchNOW-1.3.0.zip'
if (Test-Path -LiteralPath $root) {
    $resolved = (Resolve-Path -LiteralPath $root).Path
    $expected = [IO.Path]::GetFullPath((Join-Path $repo 'dist\SwitchNOW-1.3.0'))
    if ($resolved -ne $expected) { throw 'Unexpected output path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'switch\SwitchNOW') | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $root 'switch\SwitchNOW\SwitchNOW.nro')
Copy-Item -LiteralPath $nro -Destination (Join-Path $dist 'SwitchNOW-1.3.0.nro') -Force
foreach ($ext in @('jpg','png')) {
    Copy-Item -LiteralPath (Join-Path $repo "resources\icon\icon.$ext") -Destination (Join-Path $root "switch\SwitchNOW\icon.$ext")
}
foreach ($name in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $root $name)
}
New-Item -ItemType Directory -Path (Join-Path $root 'docs') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'docs\licenses') -Destination (Join-Path $root 'docs\licenses') -Recurse
New-Item -ItemType Directory -Path (Join-Path $root 'docs\branding') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'docs\branding\switchnow-hero-2048.png') -Destination (Join-Path $root 'docs\branding\switchnow-hero-2048.png')
foreach ($name in @('RELEASE_NOTES_1.3.0_RU.md','RELEASE_NOTES_1.3.0_EN.md','BUILD.md','PUBLISHING.md','SECURITY.md','AUTOLOGIN_R70_RU.md','KEYBOARD_SHORTCUTS_R70_RU.md','EPIC_AUTOLOGIN_R71_RU.md')) {
    Copy-Item -LiteralPath (Join-Path $repo "docs\$name") -Destination (Join-Path $root "docs\$name")
}
foreach ($name in @('Genshin','Honkai','wuwa','zzz','Epic')) {
    $target = Join-Path $root "switch\SwitchNOW\$name"
    New-Item -ItemType Directory -Path $target | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo "docs\autologin\$name.txt") -Destination (Join-Path $target 'README.txt')
}
@'
SwitchNOW 1.3.0 — Streaming & Experience Update
Author: Blade-Punisher

Existing installation: close SwitchNOW and replace its NRO at its current path,
preserving the filename. This includes SD:/switch/SwitchNOW.nro.
New installation: copy the switch folder to the root of the SD card.
Use Homebrew Menu in application/title-override mode.
Keep your account/settings files. Do not copy credentials from another device.
Start with Balanced; use the in-app diagnostic report for reproducible problems.
English and Russian release notes and auto-login guides are in docs/.
No account data, console key dumps, logs or diagnostic reports are included.
'@ | Set-Content -LiteralPath (Join-Path $root 'INSTALL.txt') -Encoding utf8
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
[IO.Compression.ZipFile]::CreateFromDirectory($root,$zip)
Write-Host "Packaged: $zip"
