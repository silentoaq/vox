$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $root 'deps'
New-Item -ItemType Directory -Force $deps | Out-Null

function Get-NuGet($id, $version, $folder) {
  $archive = Join-Path $deps "$id.$version.zip"
  $target = Join-Path $deps $folder
  Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg" -OutFile $archive
  if (Test-Path $target) { Remove-Item -Recurse -Force $target }
  Expand-Archive $archive -DestinationPath $target
  Remove-Item $archive
}

Get-NuGet 'microsoft.web.webview2' '1.0.4258.31' 'webview2'
Get-NuGet 'microsoft.ml.onnxruntime' '1.31.1' 'onnxruntime'

$pocketfft = Join-Path $deps 'pocketfft'
New-Item -ItemType Directory -Force $pocketfft | Out-Null
Invoke-WebRequest 'https://raw.githubusercontent.com/mreineck/pocketfft/cpp/pocketfft_hdronly.h' -OutFile (Join-Path $pocketfft 'pocketfft_hdronly.h')
Invoke-WebRequest 'https://raw.githubusercontent.com/mreineck/pocketfft/cpp/LICENSE.md' -OutFile (Join-Path $pocketfft 'LICENSE.md')

function Get-Source($url, $folder) {
  $archive = Join-Path $deps "$folder.tar.gz"
  $target = Join-Path $deps $folder
  Invoke-WebRequest $url -OutFile $archive
  if (Test-Path $target) { Remove-Item -Recurse -Force $target }
  New-Item -ItemType Directory -Force $target | Out-Null
  tar -xzf $archive -C $target --strip-components=1
  Remove-Item $archive
}

Get-Source 'https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz' 'opus'
Get-Source 'https://downloads.xiph.org/releases/opus/libopusenc-0.2.1.tar.gz' 'libopusenc'

$modelHash = '82ea615e76d59b5610e56643040db66fb033ea70a8c876d57e1899f3c9bbf2e3'
$models = Join-Path $deps 'models'
$model = Join-Path $models 'demucs-hdemucs_mmi-10s.onnx'
New-Item -ItemType Directory -Force $models | Out-Null
Invoke-WebRequest "https://vox.lichitdev.com/files/$modelHash" -OutFile $model
if ((Get-FileHash -Algorithm SHA256 $model).Hash -ne $modelHash) { throw "$model 的內容不對" }
