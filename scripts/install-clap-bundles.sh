#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_root/build-clap}"
dst_root="${2:-$HOME/Library/Audio/Plug-Ins/CLAP}"

bundles=()
while IFS= read -r bundle; do
  bundles+=("$bundle")
done < <(find "$build_dir" -maxdepth 1 -type d -name '*.clap' | sort)

if [[ "${#bundles[@]}" -eq 0 ]]; then
  echo "Missing built .clap bundle in: $build_dir" >&2
  echo "Run cmake with S3G_RNBO_EXPORT_DIR, S3G_RNBO_INPUT_CHANNELS, and S3G_RNBO_OUTPUT_CHANNELS, then build." >&2
  exit 1
fi

mkdir -p "$dst_root"

for src in "${bundles[@]}"; do
  bundle_name="$(basename "$src")"
  rm -rf "$dst_root/$bundle_name"
  cp -R "$src" "$dst_root/$bundle_name"
  echo "$dst_root/$bundle_name"
done
