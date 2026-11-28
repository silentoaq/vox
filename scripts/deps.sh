#!/bin/sh
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
deps="$root/deps"
mkdir -p "$deps"

fetch() {
  curl -sSfL --retry 3 -o "$2" "$1"
}

nuget() {
  archive="$deps/$1.$2.zip"
  target="$deps/$3"
  fetch "https://api.nuget.org/v3-flatcontainer/$1/$2/$1.$2.nupkg" "$archive"
  rm -rf "$target"
  mkdir -p "$target"
  unzip -q "$archive" -d "$target"
  rm "$archive"
}

source_archive() {
  archive="$deps/$2.tar.gz"
  target="$deps/$2"
  fetch "$1" "$archive"
  rm -rf "$target"
  mkdir -p "$target"
  tar -xzf "$archive" -C "$target" --strip-components=1
  rm "$archive"
}

nuget microsoft.ml.onnxruntime 1.31.1 onnxruntime

mkdir -p "$deps/pocketfft"
fetch https://raw.githubusercontent.com/mreineck/pocketfft/cpp/pocketfft_hdronly.h "$deps/pocketfft/pocketfft_hdronly.h"
fetch https://raw.githubusercontent.com/mreineck/pocketfft/cpp/LICENSE.md "$deps/pocketfft/LICENSE.md"

source_archive https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz opus
source_archive https://downloads.xiph.org/releases/opus/libopusenc-0.2.1.tar.gz libopusenc

model_hash=82ea615e76d59b5610e56643040db66fb033ea70a8c876d57e1899f3c9bbf2e3
mkdir -p "$deps/models"
model="$deps/models/demucs-hdemucs_mmi-10s.onnx"
fetch "https://vox.lichitdev.com/files/$model_hash" "$model"
echo "$model_hash  $model" | shasum -a 256 -c - >/dev/null
