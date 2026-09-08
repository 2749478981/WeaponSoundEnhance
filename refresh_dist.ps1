# refresh_dist.ps1 —— 把最新构建产物集中复制到 dist\
# 约定：所有对外交付物（WeaponSoundEnhance.dll / WeaponSoundEnhanceGUI.exe /
#       WeaponSoundEnhance.ini）统一放在插件目录的 dist\ 下。每次构建后运行本脚本即可。
# 用法： powershell -ExecutionPolicy Bypass -File refresh_dist.ps1
#        （也可带 -Root <插件目录>，便于从其它位置调用）
param([string]$Root)

if (-not $Root) { $Root = $PSScriptRoot }
if (-not $Root) { $Root = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Root) { $Root = (Get-Location).Path }

$dist = Join-Path $Root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null

$src = @(
    (Join-Path $Root 'out\x64\Release\WeaponSoundEnhance.dll'),
    (Join-Path $Root 'gui\out\x64\Release\WeaponSoundEnhanceGUI.exe'),
    (Join-Path $Root 'WeaponSoundEnhance.ini')
)
foreach ($f in $src) {
    if (Test-Path $f) { Copy-Item $f $dist -Force }
    else { Write-Warning "missing: $f" }
}
Write-Host "dist updated ($dist):" -ForegroundColor Green
Get-ChildItem $dist | Where-Object { -not $_.PSIsContainer } |
    Select-Object Name, Length, LastWriteTime
