param(
    [string]$Version = "1.3.0",
    [string]$BuildDirectory = "switch",
    [string]$Flavor = ""
)

$ErrorActionPreference = "Stop"

if ($Version -eq "1.3.0") {
    & (Join-Path $PSScriptRoot "package-stable-release.ps1") -BuildDirectory $BuildDirectory
    return
}


$repo = Split-Path -Parent $PSScriptRoot
$nro = Join-Path $repo "build\$BuildDirectory\SwitchNOW.nro"
if (-not (Test-Path -LiteralPath $nro)) {
    throw "Missing build artifact: $nro. Run .\build-switch.ps1 first."
}

$dist = Join-Path $repo "dist"
$flavorSuffix = if ([string]::IsNullOrWhiteSpace($Flavor)) { "" } else { "-$Flavor" }
$packageName = "SwitchNOW-$Version$flavorSuffix"
$packageRoot = Join-Path $dist $packageName
$switchDir = Join-Path $packageRoot "switch\SwitchNOW"
$versionedNro = Join-Path $dist "$packageName.nro"
$zipPath = Join-Path $dist "$packageName.zip"

$repoResolved = (Resolve-Path -LiteralPath $repo).Path
if (Test-Path -LiteralPath $packageRoot) {
    $packageResolved = (Resolve-Path -LiteralPath $packageRoot).Path
    if (-not $packageResolved.StartsWith($repoResolved, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean package path outside repo: $packageResolved"
    }
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

if (Test-Path -LiteralPath $versionedNro) {
    Remove-Item -LiteralPath $versionedNro -Force
}

New-Item -ItemType Directory -Force -Path $switchDir | Out-Null

Copy-Item -LiteralPath $nro -Destination (Join-Path $switchDir "SwitchNOW.nro") -Force
Copy-Item -LiteralPath $nro -Destination $versionedNro -Force
$nroHash = (Get-FileHash -LiteralPath $versionedNro -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $dist "SHA256SUMS.txt") `
    -Value "$nroHash  $([System.IO.Path]::GetFileName($versionedNro))" `
    -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $repo "resources\icon\icon.jpg") -Destination (Join-Path $switchDir "icon.jpg") -Force
Copy-Item -LiteralPath (Join-Path $repo "resources\icon\icon.png") -Destination (Join-Path $switchDir "icon.png") -Force
Copy-Item -LiteralPath (Join-Path $repo "README.md") -Destination (Join-Path $packageRoot "README.md") -Force
Copy-Item -LiteralPath (Join-Path $repo "THIRD_PARTY_NOTICES.md") `
    -Destination (Join-Path $packageRoot "THIRD_PARTY_NOTICES.md") -Force
Copy-Item -LiteralPath (Join-Path $repo "docs\PATCH_NOTES_1.x_RU.md") `
    -Destination (Join-Path $packageRoot "CHANGELOG_RU.md") -Force
$releaseNotesVersion = $Version -replace '\s+', '-'
$releaseNotes = Join-Path $repo ("docs\RELEASE_NOTES_{0}_RU.md" -f $releaseNotesVersion)
if (Test-Path -LiteralPath $releaseNotes) {
    Copy-Item -LiteralPath $releaseNotes `
        -Destination (Join-Path $packageRoot "RELEASE_NOTES_RU.md") -Force
}
if ($Version -match "^1\.2\.1-(R31|R32|R33|R34)-test$") {
    $diagnosticsRevision = $Matches[1]
    Copy-Item -LiteralPath (Join-Path $repo "docs\PERFORMANCE_DIAGNOSTICS_${diagnosticsRevision}_RU.md") `
        -Destination (Join-Path $packageRoot "PERFORMANCE_DIAGNOSTICS_RU.md") -Force
}
if ($Version -eq "1.2.1-R33-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R32_REPORT_1792781614159_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R34-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R33_THREE_REPORTS_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R35-debug") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R35_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R36-test" -or $Version -eq "1.2.1-R37-test" -or $Version -eq "1.2.1-R38-test" -or $Version -eq "1.2.1-R39-test" -or $Version -eq "1.2.1-R40-test" -or $Version -eq "1.2.1-R41-test" -or $Version -eq "1.2.1-R42-test" -or $Version -eq "1.2.1-R43-test" -or $Version -eq "1.2.1-R44-test" -or $Version -eq "1.2.1-R45-test" -or $Version -eq "1.2.1-R46-test" -or $Version -eq "1.2.1-R47-test" -or $Version -eq "1.2.1-R48-test" -or $Version -eq "1.2.1-R49-test" -or $Version -eq "1.2.1-R50-test" -or $Version -eq "1.2.1-R51-test") {
    $networkRevision = if ($Version -eq "1.2.1-R51-test") { "R51" } elseif ($Version -eq "1.2.1-R50-test") { "R50" } elseif ($Version -eq "1.2.1-R49-test") { "R49" } elseif ($Version -eq "1.2.1-R48-test") { "R48" } elseif ($Version -eq "1.2.1-R47-test") { "R47" } elseif ($Version -eq "1.2.1-R46-test") { "R46" } elseif ($Version -eq "1.2.1-R45-test") { "R45" } elseif ($Version -eq "1.2.1-R44-test") { "R44" } elseif ($Version -eq "1.2.1-R43-test") { "R43" } elseif ($Version -eq "1.2.1-R42-test") { "R42" } elseif ($Version -eq "1.2.1-R41-test") { "R41" } elseif ($Version -eq "1.2.1-R40-test") { "R40" } elseif ($Version -eq "1.2.1-R39-test") { "R39" } elseif ($Version -eq "1.2.1-R38-test") { "R38" } elseif ($Version -eq "1.2.1-R37-test") { "R37" } else { "R36" }
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_${networkRevision}_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R38-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R37_INPUT_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R39-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R38_RENDER_STREAM_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R41-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R40_REPORT_1792875148051_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R42-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R41_TWO_REPORTS_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R43-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R42_REPORT_1793017615121_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R44-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R43_REPORT_1793028556312_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R45-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R44_TWO_STARTUP_REPORTS_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R46-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R45_REPORT_1793090421822_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R47-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R46_REPORT_1793094120427_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R59-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R59_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R60-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R60_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R61-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R61_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R62-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R62_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R63-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R63_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R64-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R64_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R65-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R65_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R66-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R66_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -in @("1.2.1-R67-test", "1.2.1-R68-test", "1.2.1-R69-test", "1.2.1-R70-test", "1.2.1-R71-test", "1.2.1-R72-test", "1.2.1-R73-test")) {
    $autoLoginRevision = if ($Version -in @("1.2.1-R70-test", "1.2.1-R71-test", "1.2.1-R72-test", "1.2.1-R73-test")) { "R70" } else { "R67" }
    Copy-Item -LiteralPath (Join-Path $repo "docs\AUTOLOGIN_${autoLoginRevision}_RU.md") `
        -Destination (Join-Path $packageRoot "AUTOLOGIN_RU.md") -Force
    foreach ($gameFolder in @("Genshin", "Honkai", "wuwa", "zzz")) {
        $gameDirectory = Join-Path $switchDir $gameFolder
        New-Item -ItemType Directory -Force -Path $gameDirectory | Out-Null
        Copy-Item -LiteralPath (Join-Path $repo "docs\autologin\$gameFolder.txt") `
            -Destination (Join-Path $gameDirectory "README.txt") -Force
    }
}
if ($Version -in @("1.2.1-R68-test", "1.2.1-R69-test", "1.2.1-R70-test", "1.2.1-R71-test", "1.2.1-R72-test", "1.2.1-R73-test")) {
    $keyboardRevision = if ($Version -eq "1.2.1-R68-test") { "R68" } elseif ($Version -in @("1.2.1-R70-test", "1.2.1-R71-test", "1.2.1-R72-test", "1.2.1-R73-test")) { "R70" } else { "R69" }
    Copy-Item -LiteralPath (Join-Path $repo "docs\KEYBOARD_SHORTCUTS_${keyboardRevision}_RU.md") `
        -Destination (Join-Path $packageRoot "KEYBOARD_SHORTCUTS_RU.md") -Force
}
if ($Version -in @("1.2.1-R70-test", "1.2.1-R71-test", "1.2.1-R72-test", "1.2.1-R73-test")) {
    $epicRevision = if ($Version -in @("1.2.1-R71-test", "1.2.1-R72-test", "1.2.1-R73-test")) { "R71" } else { "R70" }
    Copy-Item -LiteralPath (Join-Path $repo "docs\EPIC_AUTOLOGIN_${epicRevision}_RU.md") `
        -Destination (Join-Path $packageRoot "EPIC_AUTOLOGIN_RU.md") -Force
    $epicDirectory = Join-Path $switchDir "Epic"
    New-Item -ItemType Directory -Force -Path $epicDirectory | Out-Null
    Copy-Item -LiteralPath (Join-Path $repo "docs\autologin\Epic.txt") `
        -Destination (Join-Path $epicDirectory "README.txt") -Force
}
if ($Version -eq "1.2.1-R58-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R58_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R57-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R57_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R56-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R56_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R55-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R55_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R54-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R54_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
}
if ($Version -eq "1.2.1-R52-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\NETWORK_DEBUG_R52_RU.md") `
        -Destination (Join-Path $packageRoot "NETWORK_DEBUG_RU.md") -Force
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R51_REPORT_1793142072033_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R51-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R50_AUTO_REPORT_1793136319447_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R50-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R49_REPORT_1793118415385_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R49-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R49_LIVE_CONNECTION_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
if ($Version -eq "1.2.1-R48-test") {
    Copy-Item -LiteralPath (Join-Path $repo "docs\ANALYSIS_R48_WEBSOCKET_TESTS_RU.md") `
        -Destination (Join-Path $packageRoot "REPORT_ANALYSIS_RU.md") -Force
}
$versionChanges = Join-Path $repo "docs\CHANGES_1.1.1_TO_1.1.5_RU.md"
if (Test-Path -LiteralPath $versionChanges) {
    Copy-Item -LiteralPath $versionChanges `
        -Destination (Join-Path $packageRoot "CHANGES_1.1.1_TO_1.1.5_RU.md") -Force
}

$manifest = @"
SwitchNOW $Version

Install:
1. Close SwitchNOW. For an existing installation, replace the NRO at its current
   path, preserving its filename (for example SD:/switch/SwitchNOW.nro).
2. For a new installation, copy the switch folder to the root of the SD card.
3. Launch Homebrew Menu with title override/application mode and start SwitchNOW.
4. Keep account/settings data. Start comparison testing with the Balanced preset.

Included:
- switch/SwitchNOW/SwitchNOW.nro
- switch/SwitchNOW/icon.jpg
- switch/SwitchNOW/icon.png
- README.md
- THIRD_PARTY_NOTICES.md
- CHANGELOG_RU.md
- RELEASE_NOTES_RU.md
- CHANGES_1.1.1_TO_1.1.5_RU.md
"@

Set-Content -LiteralPath (Join-Path $packageRoot "INSTALL.txt") -Value $manifest -Encoding ASCII

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $zipPath)

Write-Host "Packaged: $zipPath"
Write-Host "Versioned NRO: $versionedNro"
