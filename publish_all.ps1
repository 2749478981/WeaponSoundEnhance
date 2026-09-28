$token = $env:GH_TOKEN
$owner = '2749478981'; $repo = 'WeaponSoundEnhance'; $proxy = 'http://127.0.0.1:7897'
$headers = @{ Authorization = "Bearer $token"; Accept = 'application/vnd.github+json'; 'User-Agent' = 'wse-release-script' }
$root = 'D:\mod3\MHW plugins\dll\WeaponSoundEnhance'

# 按版本切分 RELEASE.md（每个版本自标题起到下一个版本标题止）
$lines = [System.IO.File]::ReadAllLines("$root\RELEASE.md", [System.Text.Encoding]::UTF8)
$blocks = @{}   # tag -> body（含标题）
$cur = $null
foreach ($l in $lines) {
    if ($l -match '^# WeaponSoundEnhance v(2\.\d+)') {
        $cur = $Matches[1]; $blocks[$cur] = ''
    }
    if ($cur -and ($null -ne $blocks[$cur])) { $blocks[$cur] += $l + "`n" }
}
[System.IO.File]::WriteAllText("$root\_release_bodies.txt", ($blocks.Keys -join " | "), [System.Text.Encoding]::UTF8)

foreach ($tag in @('v2.15','v2.16','v2.17','v2.18','v2.19','v2.20')) {
    $body = $blocks[$tag]
    if (-not $body) { $body = "WeaponSoundEnhance $tag" }
    $payload = @{ tag_name = $tag; name = "WeaponSoundEnhance $tag"; body = $body; draft = $false; prerelease = $false } | ConvertTo-Json -Depth 4 -Compress
    $json = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $rel = $null
    for ($i = 1; $i -le 3; $i++) {
        try { $rel = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$owner/$repo/releases" -Headers $headers -Body $json -ContentType 'application/json; charset=utf-8' -Proxy $proxy; break }
        catch {
            Write-Output ("$tag create attempt $i : " + $_.Exception.Message)
            if ($_.Exception.Message -match '422') { $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases/tags/$tag" -Headers $headers -Proxy $proxy; break }
            Start-Sleep -Seconds 5
        }
    }
    if (-not $rel) { Write-Output "$tag release 创建失败"; continue }
    Write-Output ("$tag release = " + $rel.html_url)
    $zip = "$root\WeaponSoundEnhance_${tag}_nosounds.zip"
    if (Test-Path -LiteralPath $zip) {
        $name = [System.IO.Path]::GetFileName($zip)
        for ($i = 1; $i -le 5; $i++) {
            try { $a = Invoke-RestMethod -Method Post -Uri "https://uploads.github.com/repos/$owner/$repo/releases/$($rel.id)/assets?name=$name" -Headers $headers -InFile $zip -ContentType 'application/zip' -Proxy $proxy; Write-Output ("   asset = " + $a.browser_download_url); break }
            catch { Write-Output ("   upload $i failed: " + $_.Exception.Message); Start-Sleep -Seconds 6 }
        }
    } else {
        Write-Output "   (无独立安装包，用户请用 v2.20)"
    }
}