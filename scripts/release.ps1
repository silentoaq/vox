param(
  [Parameter(Mandatory)] [string] $Key,
  [string] $UpdateUrl,
  [string] $Bucket
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-release'
$utf8 = New-Object Text.UTF8Encoding $false
if ($Bucket -and (git -C $root status --porcelain)) { throw '有還沒提交的修改' }

if (-not $UpdateUrl) {
  if ((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw) -notmatch 'set\(VOX_UPDATE_URL "([^"]+)"') { throw 'CMakeLists.txt 裡找不到 VOX_UPDATE_URL' }
  $UpdateUrl = $Matches[1]
}
if ($UpdateUrl -notmatch '/latest$') { throw "UpdateUrl 要以 /latest 結尾：$UpdateUrl" }
$filesUrl = $UpdateUrl -replace '/latest$', '/files'

$version = (Get-Date).ToString('yyyy.MM.dd', [Globalization.CultureInfo]::InvariantCulture)
$published = @{}
try {
  $current = Invoke-WebRequest -UseBasicParsing "$UpdateUrl/manifest.json"
  $online = [Text.Encoding]::UTF8.GetString($current.RawContentStream.ToArray()) | ConvertFrom-Json
  foreach ($file in $online.files) { $published[$file.sha256] = $true }
  if ($online.version -match "^$([regex]::Escape($version))(\.([0-9]+))?$") { $version = "$version.$(1 + [int]$Matches[2])" }
} catch [Net.WebException] {
  if ($_.Exception.Response.StatusCode -ne [Net.HttpStatusCode]::NotFound) { throw }
}

cmake -S $root -B $build -DVOX_DEVTOOLS=OFF "-DVOX_UPDATE_URL=$UpdateUrl" "-DVOX_VERSION=$version"
if ($LASTEXITCODE -ne 0) { throw '設定建置失敗' }
cmake --build $build --config Release --target vox vox-separate vox-launcher
if ($LASTEXITCODE -ne 0) { throw '建置失敗' }

$out = Join-Path $build "release\v$version"
$app = Join-Path $out 'app'
$upload = Join-Path $out 'upload'
$latest = Join-Path $upload 'latest'
$blobs = Join-Path $upload 'files'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $app, $latest, $blobs | Out-Null
$built = Join-Path $build 'Release'
foreach ($name in @('Vox.exe', 'vox-separate.exe', 'onnxruntime.dll', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.txt', 'ui', 'models')) {
  $source = Join-Path $built $name
  if (-not (Test-Path $source)) { throw "建置結果裡少了 $name" }
  Copy-Item -Recurse $source $app
}
$launcherExe = Join-Path $build 'launcher\Release\Vox.exe'
New-Item -ItemType Directory -Force (Join-Path $app 'launcher') | Out-Null
Copy-Item $launcherExe (Join-Path $app 'launcher')

$files = @()
$fresh = 0
foreach ($item in Get-ChildItem -Recurse -File $app) {
  $sha = (Get-FileHash -Algorithm SHA256 $item.FullName).Hash.ToLowerInvariant()
  $blob = Join-Path $blobs $sha
  if (-not $published[$sha] -and -not (Test-Path $blob)) {
    Copy-Item $item.FullName $blob
    $fresh++
  }
  $files += [ordered]@{
    path = $item.FullName.Substring($app.Length + 1).Replace('\', '/')
    size = $item.Length
    sha256 = $sha
    url = "$filesUrl/$sha"
  }
}
$manifest = [ordered]@{ version = $version; files = $files } | ConvertTo-Json -Depth 4 -Compress
$manifestBytes = $utf8.GetBytes($manifest)
[IO.File]::WriteAllBytes((Join-Path $latest 'manifest.json'), $manifestBytes)
[IO.File]::WriteAllBytes((Join-Path $app 'manifest.json'), $manifestBytes)

$private = [Convert]::FromBase64String((Get-Content $Key -Raw).Trim())
$signer = New-Object Security.Cryptography.ECDsaCng([Security.Cryptography.CngKey]::Import($private, [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob))
$signer.HashAlgorithm = [Security.Cryptography.CngAlgorithm]::Sha256
[IO.File]::WriteAllBytes((Join-Path $latest 'manifest.sig'), $signer.SignData($manifestBytes))

$payload = Join-Path $out 'payload.zip'
& "$env:SystemRoot\System32\tar.exe" -a -c -f $payload -C $app .
if ($LASTEXITCODE -ne 0) { throw '安裝檔壓縮失敗' }
$launcher = [IO.File]::ReadAllBytes($launcherExe)
$setup = [IO.File]::Create((Join-Path $latest 'VoxSetup.exe'))
try {
  $setup.Write($launcher, 0, $launcher.Length)
  $zip = [IO.File]::OpenRead($payload)
  try { $zip.CopyTo($setup) } finally { $zip.Dispose() }
  $footer = New-Object IO.BinaryWriter($setup)
  $footer.Write([uint64]$launcher.Length)
  $footer.Write([Text.Encoding]::ASCII.GetBytes('VOXSETUP'))
  $footer.Flush()
} finally { $setup.Dispose() }
Remove-Item $payload

"Vox $version：$($files.Count) 個檔案，其中 $fresh 個要上傳"
"上傳資料夾：$upload"

if ($Bucket) {
  $objects = @(Get-ChildItem -File $blobs | ForEach-Object { [ordered]@{ key = "files/$($_.Name)"; file = $_.FullName } })
  if ($objects.Count) {
    $list = Join-Path $out 'objects.json'
    [IO.File]::WriteAllText($list, (ConvertTo-Json -InputObject $objects -Compress), $utf8)
    npx --yes wrangler r2 bulk put $Bucket --filename $list --cache-control 'public, max-age=31536000, immutable' --remote
    if ($LASTEXITCODE -ne 0) { throw '上傳 files 失敗' }
  }
  foreach ($name in @('VoxSetup.exe', 'manifest.sig', 'manifest.json')) {
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
    -ContentType 'application/octet-stream' -InFile (Join-Path $latest 'VoxSetup.exe') | Out-Null
  "GitHub Release：$($release.html_url)"
}
