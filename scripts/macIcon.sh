#!/bin/sh
set -e
source="$1"
out="$2"
set_dir="$(mktemp -d)/Vox.iconset"
mkdir -p "$set_dir"
for size in 16 32 128 256 512; do
  sips -z $size $size "$source" --out "$set_dir/icon_${size}x${size}.png" >/dev/null
  double=$((size * 2))
  sips -z $double $double "$source" --out "$set_dir/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$set_dir" -o "$out"
rm -rf "$(dirname "$set_dir")"
