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
if (-not (Test-Path -LiteralPath $data)) { New-Item -ItemType Directory -Path $data -Force | Out-Null }
if (-not (Test-Path -LiteralPath (Join-Path $data 'sounds'))) {
    New-Item -ItemType Directory -Path (Join-Path $data 'sounds') -Force | Out-Null
}

# 1) DLL goes to dist root (in game: nativePC\plugins\)
Copy-IfExists (Join-Path $Root 'out\x64\Release\WeaponSoundEnhance.dll') $dist

# 2) everything else goes to the data subfolder
Copy-IfExists (Join-Path $Root 'gui\out\x64\Release\WeaponSoundEnhanceGUI.exe') $data
Copy-IfExists (Join-Path $Root 'fsm_db.csv') $data
Copy-IfExists (Join-Path $Root 'RELEASE.md') $data

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
