#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_root/build-clap}"
dst_root="${2:-$HOME/Library/Audio/Plug-Ins/CLAP}"
bundle_name="s3g_rnbo_8ch_passthru.clap"
legacy_bundle_name="s3g_rnbo_test.clap"
src="$build_dir/$bundle_name"

if [[ ! -d "$src" ]]; then
  echo "Missing built bundle: $src" >&2
  echo "Run: cmake -S . -B build-clap -DS3G_BUILD_CLAP=ON -DS3G_RNBO_EXPORT_DIR=rnbo_exports/8ch_passthru -DS3G_RNBO_INPUT_CHANNELS=8 -DS3G_RNBO_OUTPUT_CHANNELS=8 && cmake --build build-clap" >&2
  exit 1
fi

mkdir -p "$dst_root"
rm -rf "$dst_root/$legacy_bundle_name"
rm -rf "$dst_root/$bundle_name"
cp -R "$src" "$dst_root/$bundle_name"
echo "$dst_root/$bundle_name"
