#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir_input="${S3G_RNBO_BUILD_DIR:-${1:-$repo_root/build-clap-release}}"
release_version="${S3G_RELEASE_VERSION:-0.1.0-pre}"
release_date="${S3G_RELEASE_DATE:-$(date +%F)}"
package_name="${2:-s3g-rnbo-clap-macos-clap-$release_version}"
allow_dirty="${S3G_PACKAGE_ALLOW_DIRTY:-0}"
dist_root="$repo_root/dist"
final_staging="$dist_root/$package_name"
installer="$repo_root/scripts/install-clap-bundles.sh"

if [[ ! "$package_name" =~ ^s3g-rnbo-clap-macos-clap-[A-Za-z0-9._-]+$ ]]; then
  echo "Unsafe package name: $package_name" >&2
  exit 2
fi
if [[ ! "$release_version" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "Unsafe S3G_RELEASE_VERSION: $release_version" >&2
  exit 2
fi
if [[ ! "$release_date" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
  echo "S3G_RELEASE_DATE must use YYYY-MM-DD" >&2
  exit 2
fi
if [[ "$allow_dirty" != 0 && "$allow_dirty" != 1 ]]; then
  echo "S3G_PACKAGE_ALLOW_DIRTY must be 0 or 1" >&2
  exit 2
fi
if [[ ! -d "$build_dir_input" || -L "$build_dir_input" ]]; then
  echo "Missing or unsafe Release build directory: $build_dir_input" >&2
  exit 1
fi

build_dir="$(cd "$build_dir_input" && pwd -P)"
cache="$build_dir/CMakeCache.txt"
if [[ ! -f "$cache" ]]; then
  echo "Missing CMake cache: $cache" >&2
  exit 1
fi
cache_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")"
cache_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache")"
cache_configs="$(sed -n 's/^CMAKE_CONFIGURATION_TYPES:STRING=//p' "$cache")"
project_version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$cache")"
if [[ -z "$cache_source" || ! -d "$cache_source" ]]; then
  echo "CMake cache has no valid source directory: $cache" >&2
  exit 1
fi
cache_source="$(cd "$cache_source" && pwd -P)"
if [[ "$cache_source" != "$repo_root" ]]; then
  echo "Release build was configured from another source tree: $cache_source" >&2
  exit 1
fi
if [[ "$cache_type" != "Release" && ";$cache_configs;" != *";Release;"* ]]; then
  echo "Distribution bundles must come from a Release configuration" >&2
  exit 1
fi
if [[ -z "$project_version" || ( "$release_version" != "$project_version" \
    && "$release_version" != "$project_version-"* ) ]]; then
  echo "Package version does not match the CMake project version" >&2
  echo "  package: $release_version" >&2
  echo "  project: ${project_version:-<missing>}" >&2
  exit 1
fi
if [[ $allow_dirty -eq 0 && -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  echo "Refusing to stage a dirty source tree." >&2
  echo "Commit/review it, or set S3G_PACKAGE_ALLOW_DIRTY=1 for a test package." >&2
  exit 1
fi

echo "Building exact Release artifact tree: $build_dir"
cmake --build "$build_dir" --config Release --parallel

validation_destination="$repo_root/.stage-validation/s3g-rnbo-clap"
bash "$installer" --dry-run --build-dir "$build_dir" \
  --destination "$validation_destination"

bundles=()
while IFS= read -r bundle; do
  bundles+=("$bundle")
done < <(find "$build_dir" -maxdepth 1 -type d -name '*.clap' -print | sort)
if [[ ${#bundles[@]} -eq 0 ]]; then
  echo "No Release .clap bundles found in: $build_dir" >&2
  exit 1
fi

for bundle in "${bundles[@]}"; do
  plist="$bundle/Contents/Info.plist"
  executable="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")"
  executable_path="$bundle/Contents/MacOS/$executable"
  architectures="$(/usr/bin/lipo -archs "$executable_path")"
  if [[ "$architectures" != "arm64" ]]; then
    echo "Release bundles must be Apple-silicon-only arm64: $executable_path ($architectures)" >&2
    exit 1
  fi
done

mkdir -p "$dist_root"
package_work_root="$(mktemp -d "$dist_root/.s3g-rnbo-package.XXXXXX")"
staging="$package_work_root/$package_name"
cleanup() {
  if [[ -n "${package_work_root:-}" && -d "$package_work_root" ]]; then
    rm -rf "$package_work_root"
  fi
}
trap cleanup EXIT
mkdir -p "$staging"

host_names=()
for bundle in "${bundles[@]}"; do
  staged_bundle="$staging/$(basename "$bundle")"
  /usr/bin/ditto --noqtn "$bundle" "$staged_bundle"
  codesign --force --deep --sign - "$staged_bundle"
  codesign --verify --deep --strict "$staged_bundle"
  host_names+=("$(/usr/libexec/PlistBuddy -c 'Print :CFBundleName' \
    "$staged_bundle/Contents/Info.plist")")
done

cp "$installer" "$staging/Install s3g-rnbo-clap CLAPs.command"
chmod 755 "$staging/Install s3g-rnbo-clap CLAPs.command"
cp "$repo_root/LICENSE" "$staging/LICENSE.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"

git_revision="$(git -C "$repo_root" rev-parse HEAD)"
git_status="clean"
if [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  git_status="dirty (explicitly allowed for test packaging)"
fi
cat > "$staging/build-provenance.txt" <<EOF
s3g-rnbo-clap package version: $release_version
Source revision: $git_revision
Source status: $git_status
CMake configuration: Release
CMake project version: $project_version
Bundle count: ${#bundles[@]}
EOF

cat > "$staging/README.txt" <<EOF
s3g-rnbo-clap pre-release macOS CLAP build for REAPER testing.

Version: $release_version
Release date: $release_date

Compatibility:

- Apple silicon Macs only (arm64: M1, M2, M3, M4, or newer).
- REAPER with CLAP support.
- Wrapper conventions synchronized with s3g-dsp 0.6.0.

Recommended installation:

1. Quit REAPER.
2. Double-click "Install s3g-rnbo-clap CLAPs.command".
3. If macOS blocks it, approve it in System Settings > Privacy & Security.
4. Restart REAPER and rescan CLAP plugins.

The installer verifies bundle identities, installs this collection in:

~/Library/Audio/Plug-Ins/CLAP/s3g-rnbo-clap/

Verified prior files are backed up under:

~/Library/Application Support/s3g-rnbo-clap/CLAP Backups/

The wrapper code is BSD-3-Clause. RNBO-generated source and RNBO engine support
have separate Cycling '74/RNBO licensing terms; see THIRD_PARTY_NOTICES.md.

Included plugins (${#bundles[@]}):
EOF
for host_name in "${host_names[@]}"; do
  printf -- '- %s\n' "$host_name" >> "$staging/README.txt"
done

bash "$staging/Install s3g-rnbo-clap CLAPs.command" --dry-run \
  --build-dir "$staging" --destination "$validation_destination"

if [[ -e "$final_staging" || -L "$final_staging" ]]; then
  rm -rf "$final_staging"
fi
mv "$staging" "$final_staging"
trap - EXIT
cleanup
echo "$final_staging"
