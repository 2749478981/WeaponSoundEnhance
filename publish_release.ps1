# publish_release.ps1 - create the v2.4 GitHub release and upload the zip asset.
# ASCII-only on purpose (PowerShell 5.1 reads BOM-less .ps1 as ANSI).
param(
    [string]$Tag = 'v2.4',
    [string]$Zip = 'D:\mod3\MHW plugins\dll\WeaponSoundEnhance\WeaponSoundEnhance_v2.4_nosounds.zip',
    [string]$Notes = 'D:\mod3\MHW plugins\dll\WeaponSoundEnhance\RELEASE.md',
    [string]$Token = $env:GH_TOKEN,
    [string]$Proxy = 'http://127.0.0.1:7897'
)

$ErrorActionPreference = 'Stop'
$owner = '2749478981'
$repo = 'WeaponSoundEnhance'
$api = "https://api.github.com/repos/$owner/$repo"
$headers = @{
    Authorization = "Bearer $Token"
    Accept = 'application/vnd.github+json'
    'User-Agent' = 'wse-release-script'
}

$body = [System.IO.File]::ReadAllText($Notes, [System.Text.Encoding]::UTF8)
$payload = @{
    tag_name = $Tag
    name = "WeaponSoundEnhance $Tag"
    body = $body
    draft = $false
    prerelease = $false
} | ConvertTo-Json -Depth 4 -Compress

$jsonBytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
Write-Host "creating release $Tag ($($jsonBytes.Length) bytes of JSON) ..."
$rel = Invoke-RestMethod -Method Post -Uri "$api/releases" -Headers $headers -Body $jsonBytes -ContentType 'application/json; charset=utf-8' -Proxy $Proxy
Write-Host "release id = $($rel.id)  url = $($rel.html_url)"

$name = [System.IO.Path]::GetFileName($Zip)
$uploadUrl = "https://uploads.github.com/repos/$owner/$repo/releases/$($rel.id)/assets?name=$name"
Write-Host "uploading $name ..."
$asset = Invoke-RestMethod -Method Post -Uri $uploadUrl -Headers $headers -InFile $Zip -ContentType 'application/zip' -Proxy $Proxy
Write-Host "asset = $($asset.name)  size = $($asset.size)  state = $($asset.state)"
Write-Host "download = $($asset.browser_download_url)"
