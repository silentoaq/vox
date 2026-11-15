param([Parameter(Mandatory)] [string] $Out)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$public = Join-Path $root 'res\update.pub'
foreach ($path in @($Out, $public)) {
  if (Test-Path $path) { throw "$path 已經存在，換金鑰會讓已安裝的 Vox 收不到更新" }
}

$creation = New-Object Security.Cryptography.CngKeyCreationParameters
$creation.ExportPolicy = [Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
$key = [Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256, $null, $creation)
New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
[IO.File]::WriteAllText($Out, [Convert]::ToBase64String($key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)))
[IO.File]::WriteAllText($public, [Convert]::ToBase64String($key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)))
"私鑰：$Out"
"公鑰：$public"
