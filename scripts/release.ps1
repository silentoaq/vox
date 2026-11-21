param(
  [Parameter(Mandatory)] [string] $Key,
  [string] $UpdateUrl,
  [string] $Bucket
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-release'
$makensis = Join-Path $root 'deps\nsis\makensis.exe'
$utf8 = New-Object Text.UTF8Encoding $false
if ($Bucket -and (git -C $root status --porcelain)) { throw '有還沒提交的修改' }
if (-not (Test-Path $makensis)) { throw "少了 $makensis，先跑 scripts\deps.ps1" }

if (-not $UpdateUrl) {
  if ((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw) -notmatch 'set\(VOX_UPDATE_URL "([^"]+)"') { throw 'CMakeLists.txt 裡找不到 VOX_UPDATE_URL' }
  $UpdateUrl = $Matches[1]
}
if ($UpdateUrl -notmatch '/latest$') { throw "UpdateUrl 要以 /latest 結尾：$UpdateUrl" }
$filesUrl = $UpdateUrl -replace '/latest$', '/files'

$version = (Get-Date).ToString('yyyy.MM.dd', [Globalization.CultureInfo]::InvariantCulture)
$taken = @(git -C $root tag -l "v$version" "v$version.*" | ForEach-Object { if ($_ -match '^v[0-9]+\.[0-9]+\.[0-9]+\.([0-9]+)$') { [int]$Matches[1] } else { 0 } })
if ($taken.Count) { $version = "$version.$(1 + ($taken | Measure-Object -Maximum).Maximum)" }
$parts = @($version.Split('.'))
$versionNumbers = (@($parts) + @('0') * (4 - $parts.Count)) -join '.'

cmake -S $root -B $build -DVOX_DEVTOOLS=OFF "-DVOX_UPDATE_URL=$UpdateUrl" "-DVOX_VERSION=$version"
if ($LASTEXITCODE -ne 0) { throw '設定建置失敗' }
cmake --build $build --config Release --target vox vox-separate
if ($LASTEXITCODE -ne 0) { throw '建置失敗' }

$out = Join-Path $build "release\v$version"
$app = Join-Path $out 'app'
$upload = Join-Path $out 'upload'
$latest = Join-Path $upload 'latest'
$blobs = Join-Path $upload 'files'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $app, $latest, $blobs | Out-Null
$built = Join-Path $build 'Release'
$removals = @('!macro removeAppFiles')
foreach ($name in @('Vox.exe', 'vox-separate.exe', 'onnxruntime.dll', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.txt', 'ui', 'models')) {
  $source = Join-Path $built $name
  if (-not (Test-Path $source)) { throw "建置結果裡少了 $name" }
  Copy-Item -Recurse $source $app
  $removals += if ((Get-Item $source).PSIsContainer) { "  RMDir /r `"`$INSTDIR\$name`"" } else { "  Delete `"`$INSTDIR\$name`"" }
}
$removals += '!macroend'
$filesNsh = Join-Path $out 'files.nsh'
[IO.File]::WriteAllText($filesNsh, ($removals -join "`r`n") + "`r`n", $utf8)

$setup = Join-Path $latest 'VoxSetup.exe'
& $makensis /V2 "/DVERSION=$version" "/DVERSION_NUMBERS=$versionNumbers" "/DSOURCE=$app" "/DOUTPUT=$setup" `
  "/DICON=$(Join-Path $root 'res\vox.ico')" "/DFILES_NSH=$filesNsh" (Join-Path $root 'scripts\installer.nsi')
if ($LASTEXITCODE -ne 0) { throw '安裝檔打包失敗' }

$sha = (Get-FileHash -Algorithm SHA256 $setup).Hash.ToLowerInvariant()
$size = (Get-Item $setup).Length
Copy-Item $setup (Join-Path $blobs $sha)
$manifest = [ordered]@{ version = $version; installer = [ordered]@{ url = "$filesUrl/$sha"; size = $size; sha256 = $sha } } | ConvertTo-Json -Depth 4 -Compress
$manifestBytes = $utf8.GetBytes($manifest)
[IO.File]::WriteAllBytes((Join-Path $latest 'update.json'), $manifestBytes)

$private = [Convert]::FromBase64String((Get-Content $Key -Raw).Trim())
$signer = New-Object Security.Cryptography.ECDsaCng([Security.Cryptography.CngKey]::Import($private, [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob))
$signer.HashAlgorithm = [Security.Cryptography.CngAlgorithm]::Sha256
[IO.File]::WriteAllBytes((Join-Path $latest 'update.sig'), $signer.SignData($manifestBytes))

"Vox $version：安裝檔 $([math]::Round($size / 1MB, 1)) MB"
"上傳資料夾：$upload"

if ($Bucket) {
  npx --yes wrangler r2 object put "$Bucket/files/$sha" --file (Join-Path $blobs $sha) --cache-control 'public, max-age=31536000, immutable' --remote
  if ($LASTEXITCODE -ne 0) { throw '上傳 files 失敗' }
  foreach ($name in @('VoxSetup.exe', 'update.sig', 'update.json')) {
    npx --yes wrangler r2 object put "$Bucket/latest/$name" --file (Join-Path $latest $name) --cache-control 'no-cache' --remote
    if ($LASTEXITCODE -ne 0) { throw "上傳 latest/$name 失敗" }
  }

  git -C $root tag "v$version"
  if ($LASTEXITCODE -ne 0) { throw "建立標籤 v$version 失敗" }
  git -C $root push origin "v$version"
  if ($LASTEXITCODE -ne 0) { throw "推送標籤 v$version 失敗" }
  if ((git -C $root remote get-url origin) -notmatch 'github\.com[:/](.+?)(\.git)?$') { throw 'origin 不是 GitHub 的 repo' }
  $repo = $Matches[1]
  $ask = Join-Path $out 'credential.txt'
  [IO.File]::WriteAllText($ask, "protocol=https`nhost=github.com`n`n", $utf8)
  $token = (cmd /c "git credential fill < `"$ask`"" | Where-Object { $_ -like 'password=*' }) -replace '^password=', ''
  Remove-Item $ask
  if (-not $token) { throw '拿不到 GitHub 的登入憑證' }
  $headers = @{ Authorization = "Bearer $token"; Accept = 'application/vnd.github+json' }
  $release = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$repo/releases" -Headers $headers -ContentType 'application/json' `
    -Body (ConvertTo-Json @{ tag_name = "v$version"; name = "Vox $version"; generate_release_notes = $true })
  $ProgressPreference = 'SilentlyContinue'
  Invoke-RestMethod -Method Post -Uri "https://uploads.github.com/repos/$repo/releases/$($release.id)/assets?name=VoxSetup.exe" -Headers $headers `
    -ContentType 'application/octet-stream' -InFile $setup | Out-Null
  "GitHub Release：$($release.html_url)"
}
