#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if find "$script_dir" -maxdepth 1 -type d -name '*.clap' -print -quit | grep -q .; then
  repo_root="$script_dir"
else
  repo_root="$(cd "$script_dir/.." && pwd)"
fi
default_clap_root="$HOME/Library/Audio/Plug-Ins/CLAP"
default_destination="$default_clap_root/s3g-rnbo-clap"
if find "$repo_root" -maxdepth 1 -type d -name '*.clap' -print -quit | grep -q .; then
  build_dir="$repo_root"
else
  build_dir="$repo_root/build-clap"
fi
destination="$default_destination"
backup_parent="${S3G_RNBO_BACKUP_ROOT:-$HOME/Library/Application Support/s3g-rnbo-clap/CLAP Backups}"
dry_run=0
positional=0

usage() {
  cat <<'EOF'
Usage: install-clap-bundles.sh [options] [build-dir] [collection-dir]

Options:
  --build-dir PATH     Build directory containing top-level .clap bundles.
  --destination PATH   Dedicated s3g-rnbo-clap collection directory.
  --dry-run            Validate and show the planned replacement only.
  --help               Show this help.

The default collection directory is:
  ~/Library/Audio/Plug-Ins/CLAP/s3g-rnbo-clap/

A verified previous collection is moved to:
  ~/Library/Application Support/s3g-rnbo-clap/CLAP Backups/

Set S3G_RNBO_BACKUP_ROOT to override the backup directory for automation.

The two positional arguments remain compatible with the old build-dir and
destination argument order, but destination now means a dedicated collection
directory, not the shared CLAP root.
EOF
}

normalize_path() {
  local value="$1"
  while [[ "$value" != "/" && ( "$value" == */ || "$value" == */. ) ]]; do
    value="${value%/}"
    value="${value%/.}"
  done
  printf '%s\n' "$value"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      [[ $# -ge 2 ]] || { echo "--build-dir requires a path" >&2; exit 2; }
      build_dir="$2"
      shift 2
      ;;
    --destination)
      [[ $# -ge 2 ]] || { echo "--destination requires a path" >&2; exit 2; }
      destination="$2"
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ $positional -eq 0 ]]; then
        build_dir="$1"
      elif [[ $positional -eq 1 ]]; then
        destination="$1"
      else
        echo "Unexpected argument: $1" >&2
        exit 2
      fi
      positional=$((positional + 1))
      shift
      ;;
  esac
done

[[ $# -eq 0 ]] || { echo "Unexpected argument: $1" >&2; exit 2; }
build_dir="$(normalize_path "$build_dir")"
destination="$(normalize_path "$destination")"

if [[ "$destination" == "/" || "$destination" == "$default_clap_root" ]]; then
  echo "Refusing to replace a broad CLAP destination: $destination" >&2
  echo "Choose a dedicated collection directory such as: $default_destination" >&2
  exit 1
fi
if [[ -L "$destination" ]]; then
  echo "Refusing to use a symlink as the collection destination: $destination" >&2
  exit 1
fi
if [[ -e "$destination" && ! -d "$destination" ]]; then
  echo "Collection destination exists but is not a directory: $destination" >&2
  exit 1
fi
if [[ ! -d "$build_dir" || -L "$build_dir" ]]; then
  echo "Missing or unsafe build directory: $build_dir" >&2
  exit 1
fi

plist_value() {
  local bundle="$1"
  local key="$2"
  local plist="$bundle/Contents/Info.plist"
  [[ -f "$plist" && ! -L "$plist" ]] || return 1
  /usr/libexec/PlistBuddy -c "Print :$key" "$plist" 2>/dev/null
}

validate_bundle() {
  local bundle="$1"
  local expected_id="${2:-}"
  local name id host_name executable executable_path
  name="$(basename "$bundle")"
  if [[ ! "$name" =~ ^[A-Za-z0-9._-]+\.clap$ || ! -d "$bundle" || -L "$bundle" ]]; then
    echo "Unsafe CLAP bundle: $bundle" >&2
    return 1
  fi
  if ! id="$(plist_value "$bundle" CFBundleIdentifier)"; then
    echo "Cannot read CFBundleIdentifier: $bundle" >&2
    return 1
  fi
  if [[ "$id" != org.s3g.s3g-rnbo-clap.* ]]; then
    echo "Unexpected RNBO CLAP identity: $bundle ($id)" >&2
    return 1
  fi
  if [[ -n "$expected_id" && "$id" != "$expected_id" ]]; then
    echo "Bundle identity mismatch: $bundle" >&2
    echo "  expected: $expected_id" >&2
    echo "  found:    $id" >&2
    return 1
  fi
  if ! host_name="$(plist_value "$bundle" CFBundleName)" || [[ "$host_name" != "s3g "* ]]; then
    echo "Unexpected or unreadable s3g host name: $bundle" >&2
    return 1
  fi
  if ! executable="$(plist_value "$bundle" CFBundleExecutable)"; then
    echo "Cannot read CFBundleExecutable: $bundle" >&2
    return 1
  fi
  executable_path="$bundle/Contents/MacOS/$executable"
  if [[ -z "$executable" || "$executable" == */* || ! -f "$executable_path" \
      || -L "$executable_path" || ! -x "$executable_path" ]]; then
    echo "Missing or unsafe declared executable: $executable_path" >&2
    return 1
  fi
  printf '%s\n' "$id"
}

bundles=()
bundle_ids=()
while IFS= read -r bundle; do
  bundles+=("$bundle")
done < <(find "$build_dir" -maxdepth 1 -type d -name '*.clap' -print | sort)

if [[ ${#bundles[@]} -eq 0 ]]; then
  echo "Missing built .clap bundle in: $build_dir" >&2
  echo "Configure and build an RNBO export first." >&2
  exit 1
fi

for bundle in "${bundles[@]}"; do
  bundle_ids+=("$(validate_bundle "$bundle")")
done

if [[ -d "$destination" ]]; then
  while IFS= read -r installed; do
    name="$(basename "$installed")"
    if [[ "$name" == ".DS_Store" && -f "$installed" ]]; then
      continue
    fi
    if [[ ! -d "$installed" || "$name" != *.clap ]]; then
      echo "Refusing to replace a collection containing unrelated material: $installed" >&2
      exit 1
    fi
    validate_bundle "$installed" >/dev/null
  done < <(find "$destination" -mindepth 1 -maxdepth 1 -print | sort)
fi

legacy_paths=()
if [[ "$destination" == "$default_destination" ]]; then
  for ((i=0; i<${#bundles[@]}; i++)); do
    legacy="$default_clap_root/$(basename "${bundles[$i]}")"
    if [[ -e "$legacy" || -L "$legacy" ]]; then
      validate_bundle "$legacy" "${bundle_ids[$i]}" >/dev/null
      legacy_paths+=("$legacy")
    fi
  done
fi

echo "Validated ${#bundles[@]} RNBO CLAP bundle(s) from: $build_dir"
echo "Collection destination: $destination"
if [[ -d "$destination" ]]; then
  echo "Existing verified collection will be backed up before replacement."
fi
if [[ ${#legacy_paths[@]} -gt 0 ]]; then
  echo "Verified legacy top-level bundle(s) will be backed up:"
  printf '  %s\n' "${legacy_paths[@]}"
fi
if [[ $dry_run -eq 1 ]]; then
  echo "Dry run complete; no files changed."
  exit 0
fi

destination_parent="$(dirname "$destination")"
mkdir -p "$destination_parent"
work_root="$(mktemp -d "$destination_parent/.s3g-rnbo-install.XXXXXX")"
candidate="$work_root/collection"
cleanup() {
  if [[ -n "${work_root:-}" && -d "$work_root" ]]; then
    rm -rf "$work_root"
  fi
}
trap cleanup EXIT
mkdir -p "$candidate"

for ((i=0; i<${#bundles[@]}; i++)); do
  copied="$candidate/$(basename "${bundles[$i]}")"
  /usr/bin/ditto --noqtn "${bundles[$i]}" "$copied"
  validate_bundle "$copied" "${bundle_ids[$i]}" >/dev/null
  if /usr/bin/xattr -pr com.apple.quarantine "$copied" >/dev/null 2>&1; then
    /usr/bin/xattr -drs com.apple.quarantine "$copied"
  fi
done

backup_run=""
if [[ -d "$destination" || ${#legacy_paths[@]} -gt 0 ]]; then
  timestamp="$(date +%Y%m%d-%H%M%S)"
  backup_run="$backup_parent/$timestamp-$$"
  mkdir -p "$backup_run"
fi

previous_collection=""
if [[ -d "$destination" ]]; then
  previous_collection="$backup_run/s3g-rnbo-clap"
  mv "$destination" "$previous_collection"
fi

if ! mv "$candidate" "$destination"; then
  if [[ -n "$previous_collection" && -d "$previous_collection" && ! -e "$destination" ]]; then
    mv "$previous_collection" "$destination"
  fi
  echo "Install failed; the previous collection was restored." >&2
  exit 1
fi

if [[ ${#legacy_paths[@]} -gt 0 ]]; then
  mkdir -p "$backup_run/Previous CLAP Root"
  for legacy in "${legacy_paths[@]}"; do
    mv "$legacy" "$backup_run/Previous CLAP Root/$(basename "$legacy")"
  done
fi

trap - EXIT
cleanup
echo "Installed ${#bundles[@]} bundle(s) in: $destination"
if [[ -n "$backup_run" ]]; then
  echo "Previous verified files backed up in: $backup_run"
fi
