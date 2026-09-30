# refresh_dist.ps1 - assemble build outputs into a game-ready folder layout.
#
# Layout (matches the in-game layout: only the DLL sits in plugins\, the rest is in a subfolder):
#   dist\WeaponSoundEnhance.dll
#   dist\WeaponSoundEnhance\WeaponSoundEnhance.ini.template   (user ini is generated from this)
#   dist\WeaponSoundEnhance\WeaponSoundEnhanceGUI.exe
#   dist\WeaponSoundEnhance\fsm_db.csv                        (base ID library)
#   dist\WeaponSoundEnhance\shuoming.txt                      (release notes copy)
#   dist\WeaponSoundEnhance\sounds\                           (empty)
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 as ANSI when there is no
# BOM, and UTF-8 Chinese comments can corrupt the following lines.
param([string]$Root)

if (-not $Root) { $Root = $PSScriptRoot }
if (-not $Root) { $Root = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Root) { $Root = (Get-Location).Path }

$dist = Join-Path $Root 'dist'
$data = Join-Path $dist 'WeaponSoundEnhance'

function Copy-IfExists {
    param([string]$Source, [string]$DestDir)
    if (-not (Test-Path -LiteralPath $Source)) { Write-Warning "missing: $Source"; return }
    if (-not (Test-Path -LiteralPath $DestDir)) {
        New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $DestDir -Force
}

if (-not (Test-Path -LiteralPath $dist)) { New-Item -ItemType Directory -Path $dist -Force | Out-Null }
# Start from a clean dist. Without this, files from an older layout linger and
# get shipped (we caught the legacy WeaponSoundEnhance_GUI.ini still riding along,
# and stale web\*.js would silently override the new ones).
if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist -Force | Out-Null
if (-not (Test-Path -LiteralPath $data)) { New-Item -ItemType Directory -Path $data -Force | Out-Null }
if (-not (Test-Path -LiteralPath (Join-Path $data 'sounds'))) {
    New-Item -ItemType Directory -Path (Join-Path $data 'sounds') -Force | Out-Null
}

# 1) DLL goes to dist root (in game: nativePC\plugins\)
Copy-IfExists (Join-Path $Root 'out\x64\Release\WeaponSoundEnhance.dll') $dist

# 2) everything else goes to the data subfolder
# GUI exe: prefer the new WebView2 build; fall back to the legacy ImGui build.
$guiNew = Join-Path $Root 'gui\out-web\x64\Release\WeaponSoundEnhanceGUI.exe'
$guiOld = Join-Path $Root 'gui\out\x64\Release\WeaponSoundEnhanceGUI.exe'
if (Test-Path -LiteralPath $guiNew) {
    Copy-IfExists $guiNew $data
} else {
    Copy-IfExists $guiOld $data
}
Copy-IfExists (Join-Path $Root 'fsm_db.csv') $data
Copy-IfExists (Join-Path $Root 'RELEASE.md') $data
# Sonar logo shown next to the GUI title (optional: GUI falls back to dots if missing)
Copy-IfExists (Join-Path $Root 'gui\sonar_icon.png') $data

# ---------------------------------------------------------------------------
# WebView2 GUI assets.
# The exe loads web\index.html through https://sonar.local/ mapped to the exe
# folder, and the HTML references icons as "../weapons_icons/xxx.png".
# So BOTH folders must be direct children of the exe folder -- putting them
# anywhere else (or forgetting weapons_icons) shows a screen full of broken
# image placeholders. WebView2Loader.dll must also sit next to the exe.
# ---------------------------------------------------------------------------
$webSrc = Join-Path $Root 'gui\web'
if (Test-Path -LiteralPath $webSrc) {
    $webDst = Join-Path $data 'web'
    Get-ChildItem -LiteralPath $webSrc -Recurse -File |
        Where-Object { $_.FullName -notmatch '\\_shots\\' } |   # dev-only screenshots
        ForEach-Object {
            $rel = $_.FullName.Substring($webSrc.Length + 1)
            $target = Join-Path $webDst $rel
            $targetDir = Split-Path -Parent $target
            if (-not (Test-Path -LiteralPath $targetDir)) {
                New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
            }
            Copy-Item -LiteralPath $_.FullName -Destination $target -Force
        }
} else {
    Write-Warning "missing: $webSrc (GUI would show a blank window)"
}

$iconSrc = Join-Path $Root 'weapons_icons'
if (Test-Path -LiteralPath $iconSrc) {
    $iconDst = Join-Path $data 'weapons_icons'
    if (-not (Test-Path -LiteralPath $iconDst)) {
        New-Item -ItemType Directory -Path $iconDst -Force | Out-Null
    }
    # NOTE: -Path (not -LiteralPath) here, we need the * to be expanded
    Copy-Item -Path (Join-Path $iconSrc '*') -Destination $iconDst -Force
} else {
    Write-Warning "missing: $iconSrc (weapon icons would be blank)"
}

Copy-IfExists (Join-Path $Root 'third_party\webview2\x64\WebView2Loader.dll') $data

# config: shipped as *.ini.template so a release never overwrites the user's own ini
Copy-IfExists (Join-Path $Root 'WeaponSoundEnhance.ini') $data
$iniPlain = Join-Path $data 'WeaponSoundEnhance.ini'
$iniTpl = Join-Path $data 'WeaponSoundEnhance.ini.template'
if (Test-Path -LiteralPath $iniPlain) { Move-Item -LiteralPath $iniPlain -Destination $iniTpl -Force }

# release notes copy. Build the Chinese file name from code points so this .ps1 stays ASCII-only.
$relSrc = Join-Path $data 'RELEASE.md'
$relDst = Join-Path $data ([string][char]0x8BF4 + [string][char]0x660E + '.txt')   # "shuoming.txt" in Chinese
if (Test-Path -LiteralPath $relSrc) { Move-Item -LiteralPath $relSrc -Destination $relDst -Force }

Write-Host "dist updated ($dist):" -ForegroundColor Green
Get-ChildItem -LiteralPath $dist -Recurse -File | ForEach-Object {
    $_.FullName.Substring($dist.Length + 1) + "  (" + $_.Length + ")"
}
