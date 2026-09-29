$token = $env:GH_TOKEN
$owner = '2749478981'; $repo = 'sonar'; $proxy = 'http://127.0.0.1:7897'
$headers = @{ Authorization = "Bearer $token"; Accept = 'application/vnd.github+json'; 'User-Agent' = 'wse-release-script' }
$root = 'D:\mod3\MHW plugins\dll\WeaponSoundEnhance'
$tag = $env:REL_TAG

# 从 RELEASE.md 取对应版本的段落作为 release 正文
# 标题形如 "# Sonar v2.33" 或历史遗留的 "# WeaponSoundEnhance v2.32"
$lines = [System.IO.File]::ReadAllLines("$root\RELEASE.md", [System.Text.Encoding]::UTF8)
$body = ''
$cur = $null
foreach ($l in $lines) {
    if ($l -match '^# (?:Sonar|WeaponSoundEnhance) v(2\.\d+)') { $cur = $Matches[1] }
    if ($cur -eq $env:REL_VER) { $body += $l + "`n" }
}
if (-not $body) { $body = 'Sonar ' + $env:REL_TAG }

# 关键：手工拼 JSON 并用 UTF-8 字节发送。
# ConvertTo-Json 会把中文转义成 \uXXXX，某些路径下会变成 '?'（仓库描述就被写坏过一次）。
$payload = '{"tag_name":' + (ConvertTo-Json $tag -Compress) +
           ',"name":' + (ConvertTo-Json ("Sonar " + $tag) -Compress) +
           ',"body":' + (ConvertTo-Json $body -Compress) +
           ',"draft":false,"prerelease":false}'
$json = [System.Text.Encoding]::UTF8.GetBytes($payload)
$rel = $null
for ($i = 1; $i -le 4; $i++) {
    try {
        $rel = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$owner/$repo/releases" -Headers $headers -Body $json -ContentType 'application/json; charset=utf-8' -Proxy $proxy
        break
    } catch {
        Write-Output ("create attempt $i : " + $_.Exception.Message)
        if ($_.Exception.Message -match '422') {
            $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases/tags/$tag" -Headers $headers -Proxy $proxy
            break
        }
        Start-Sleep -Seconds 5
    }
}
if (-not $rel) { Write-Output 'release 创建失败'; exit 1 }
Write-Output ("release = " + $rel.html_url)

$zip = "$root\WeaponSoundEnhance_$($env:REL_TAG)_nosounds.zip"
$name = [System.IO.Path]::GetFileName($zip)
for ($i = 1; $i -le 6; $i++) {
    try {
        $a = Invoke-RestMethod -Method Post -Uri "https://uploads.github.com/repos/$owner/$repo/releases/$($rel.id)/assets?name=$name" -Headers $headers -InFile $zip -ContentType 'application/zip' -Proxy $proxy
        Write-Output ("asset = " + $a.browser_download_url)
        break
    } catch {
        Write-Output ("upload $i failed: " + $_.Exception.Message)
        Start-Sleep -Seconds 6
    }
}