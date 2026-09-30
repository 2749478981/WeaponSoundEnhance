# package_release.ps1 - build the distributable zip from the dist folder.
#
# Run refresh_dist.ps1 first (or let this script do it) so dist\ holds the
# final layout, then zip its CONTENTS (no wrapper folder) so extracting into
# nativePC\plugins\ lands WeaponSoundEnhance.dll and WeaponSoundEnhance\ in the
# right place -- same shape as every earlier release.
#
# ASCII-only on purpose (PowerShell 5.1 reads BOM-less .ps1 as ANSI).
param(
    [string]$Ver,
    [switch]$SkipDist
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $root) { $root = Split-Path -Parent $MyInvocation.MyCommand.Path }

if (-not $Ver) {
    # read the GUI version straight out of the source, so the zip can never
    # disagree with what the window title shows
    $h = Join-Path $root 'gui\src\app.h'
    $txt = [System.IO.File]::ReadAllText($h, [System.Text.Encoding]::UTF8)
    if ($txt -match 'kWseGuiVersion\s*=\s*"([^"]+)"') { $Ver = $Matches[1] }
    else { throw "cannot read kWseGuiVersion from $h" }
}

$dist = Join-Path $root 'dist'
$zip = Join-Path $root ("WeaponSoundEnhance_v{0}_nosounds.zip" -f $Ver)

if (-not $SkipDist) {
    Write-Host "[package] refreshing dist ..."
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'refresh_dist.ps1') | Out-Null
}
if (-not (Test-Path -LiteralPath (Join-Path $dist 'WeaponSoundEnhance.dll'))) {
    throw "dist is incomplete: $dist\WeaponSoundEnhance.dll missing"
}

if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem

# includeBaseDirectory = $false -> the CONTENTS of dist go to the archive root
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $dist, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

# ---------------------------------------------------------------------------
# .NET's ZipArchive writes non-ASCII entry names as UTF-8 bytes and does set the
# "language encoding flag" (general purpose bit 11), so Windows Explorer decodes
# them correctly. This patch is a safety net for the case where it does not:
# flip bit 11 in both the central directory header (+8) and the local file
# header (+6) for every entry whose name contains non-ASCII bytes.
# ---------------------------------------------------------------------------
function Set-ZipUtf8Flag {
    param([string]$Path)
    $b = [System.IO.File]::ReadAllBytes($Path)

    $eocd = -1
    for ($i = $b.Length - 22; $i -ge 0; $i--) {
        if ($b[$i] -eq 0x50 -and $b[$i+1] -eq 0x4B -and $b[$i+2] -eq 0x05 -and $b[$i+3] -eq 0x06) {
            $eocd = $i; break
        }
    }
    if ($eocd -lt 0) { throw 'zip: end-of-central-directory not found' }

    $count = [BitConverter]::ToUInt16($b, $eocd + 10)
    $p = [int][BitConverter]::ToUInt32($b, $eocd + 16)
    $patched = 0

    for ($n = 0; $n -lt $count; $n++) {
        if (-not ($b[$p] -eq 0x50 -and $b[$p+1] -eq 0x4B -and $b[$p+2] -eq 0x01 -and $b[$p+3] -eq 0x02)) { break }
        $flags    = [BitConverter]::ToUInt16($b, $p + 8)
        $nameLen  = [BitConverter]::ToUInt16($b, $p + 28)
        $extraLen = [BitConverter]::ToUInt16($b, $p + 30)
        $cmtLen   = [BitConverter]::ToUInt16($b, $p + 32)
        $localOff = [int][BitConverter]::ToUInt32($b, $p + 42)
        $name     = [System.Text.Encoding]::UTF8.GetString($b, $p + 46, $nameLen)

        $nonAscii = $false
        foreach ($ch in $name.ToCharArray()) { if ([int]$ch -gt 127) { $nonAscii = $true; break } }

        if ($nonAscii -and (($flags -band 0x800) -eq 0)) {
            $fb = [BitConverter]::GetBytes([uint16]($flags -bor 0x800))
            $b[$p + 8] = $fb[0]; $b[$p + 9] = $fb[1]
            if ($b[$localOff] -eq 0x50 -and $b[$localOff+1] -eq 0x4B -and
                $b[$localOff+2] -eq 0x03 -and $b[$localOff+3] -eq 0x04) {
                $lf = [BitConverter]::ToUInt16($b, $localOff + 6)
                $fb2 = [BitConverter]::GetBytes([uint16]($lf -bor 0x800))
                $b[$localOff + 6] = $fb2[0]; $b[$localOff + 7] = $fb2[1]
            }
            $patched++
        }
        $p = $p + 46 + $nameLen + $extraLen + $cmtLen
    }

    [System.IO.File]::WriteAllBytes($Path, $b)
    return $patched
}

$n = Set-ZipUtf8Flag -Path $zip
Write-Host ("[package] utf-8 name flags patched on {0} entr(ies)" -f $n)

$size = [math]::Round((Get-Item -LiteralPath $zip).Length / 1KB, 1)
Write-Host ("[package] {0}  ({1} KB)" -f (Split-Path $zip -Leaf), $size) -ForegroundColor Green

$z = [System.IO.Compression.ZipFile]::OpenRead($zip)
Write-Host ("[package] {0} entries:" -f $z.Entries.Count)
$z.Entries | Sort-Object FullName | ForEach-Object { Write-Host ("  " + $_.FullName) }
$z.Dispose()
