#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_root/build-clap}"
package_name="${2:-s3g-rnbo-clap-macos-clap-pre-release}"
dist_root="$repo_root/dist"
staging="$dist_root/$package_name"

bundles=()
while IFS= read -r bundle; do
  bundles+=("$bundle")
done < <(find "$build_dir" -maxdepth 1 -type d -name '*.clap' | sort)

if [[ "${#bundles[@]}" -eq 0 ]]; then
  echo "Missing built .clap bundle in: $build_dir" >&2
  echo "Run cmake with S3G_RNBO_EXPORT_DIR, S3G_RNBO_INPUT_CHANNELS, and S3G_RNBO_OUTPUT_CHANNELS, then build." >&2
  exit 1
fi

rm -rf "$staging"
mkdir -p "$staging"

for src in "${bundles[@]}"; do
  cp -R "$src" "$staging/"
done

cat > "$staging/README.txt" <<'EOF'
s3g-rnbo-clap pre-release macOS CLAP build for REAPER testing.

Included plugin bundles are the .clap directories staged beside this README.

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
