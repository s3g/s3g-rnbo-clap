#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_root/build-clap}"
package_name="${2:-s3g-rnbo-clap-macos-clap-pre-release}"
dist_root="$repo_root/dist"
staging="$dist_root/$package_name"
bundle_name="s3g_rnbo_8ch_passthru.clap"
src="$build_dir/$bundle_name"

if [[ ! -d "$src" ]]; then
  echo "Missing built bundle: $src" >&2
  echo "Run: cmake -S . -B build-clap -DS3G_BUILD_CLAP=ON -DS3G_RNBO_EXPORT_DIR=rnbo_exports/8ch_passthru -DS3G_RNBO_INPUT_CHANNELS=8 -DS3G_RNBO_OUTPUT_CHANNELS=8 && cmake --build build-clap" >&2
  exit 1
fi

rm -rf "$staging"
mkdir -p "$staging"
cp -R "$src" "$staging/"

cat > "$staging/README.txt" <<'EOF'
s3g-rnbo-clap pre-release macOS CLAP build for REAPER testing.

Included plugin:

- s3g RNBO 8ch Passthru

This is an early wrapper test for RNBO C++ exports. Plugin names, parameter
mappings, state compatibility, and included plugins may change before a stable
release.

Install by copying the .clap bundle to:

~/Library/Audio/Plug-Ins/CLAP/

Then rescan CLAP plugins in REAPER.

License notes:

The s3g wrapper code is BSD-3-Clause. RNBO-generated source code and RNBO engine
support code have their own Cycling '74/RNBO licensing terms. See the repository
THIRD_PARTY_NOTICES.md before publishing generated-source releases.
EOF

echo "$staging"
