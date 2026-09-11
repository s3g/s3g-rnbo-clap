#!/usr/bin/env bash
set -euo pipefail
# Build the SAME RNBO export/metadata in independent native and cross trees.
# No installation, source export, or generated-code relicensing is performed.
rnbo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rnbo_export=""
rnbo_inputs=8
rnbo_outputs=8
rnbo_kind=auto
rnbo_slug=""
rnbo_jobs="${S3G_BUILD_JOBS:-4}"
rnbo_build_root="${S3G_RNBO_DUAL_BUILD_ROOT:-$rnbo_root/build-dual}"
rnbo_dist_root="${S3G_RNBO_DUAL_DIST_ROOT:-$rnbo_root/dist}"
rnbo_dsp_root="${S3G_DSP_DIR:-$rnbo_root/../s3g-dsp}"
usage() {
  printf '%s\n' 'Usage: build-dual.sh [--export DIR] [--inputs N] [--outputs N]' \
    '                     [--kind auto|effect|instrument] [--slug NAME]' \
    'No --export builds the fallback test. Requires macOS, CMake, Xcode CLI tools,' \
    'MinGW-w64 and a current sibling s3g-dsp checkout. Does not install anything.'
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --export|--inputs|--outputs|--kind|--slug)
      [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      case "$1" in
        --export) rnbo_export="$2" ;;
        --inputs) rnbo_inputs="$2" ;;
        --outputs) rnbo_outputs="$2" ;;
        --kind) rnbo_kind="$2" ;;
        --slug) rnbo_slug="$2" ;;
      esac
      shift 2 ;;
    *) usage >&2; exit 2 ;;
  esac
done
[[ "$(uname -s)" == Darwin ]] || { printf 'Run build-dual.sh on macOS. Native Windows builds use CMake directly.\n' >&2; exit 2; }
for rnbo_tool in cmake x86_64-w64-mingw32-g++ x86_64-w64-mingw32-windres; do
  command -v "$rnbo_tool" >/dev/null || { printf 'Missing tool: %s\n' "$rnbo_tool" >&2; exit 2; }
done
[[ "$rnbo_inputs" =~ ^[0-9]+$ && "$rnbo_outputs" =~ ^[0-9]+$ && "$rnbo_jobs" =~ ^[1-9][0-9]*$ ]] || { usage >&2; exit 2; }
[[ "$rnbo_kind" == auto || "$rnbo_kind" == effect || "$rnbo_kind" == instrument ]] || { usage >&2; exit 2; }
if [[ -n "$rnbo_export" ]]; then
  [[ -d "$rnbo_export" ]] || { printf 'Missing export: %s\n' "$rnbo_export" >&2; exit 2; }
  rnbo_export="$(cd "$rnbo_export" && pwd -P)"
fi
if [[ -z "$rnbo_slug" ]]; then
  rnbo_slug="${rnbo_export##*/}"
  rnbo_slug="${rnbo_slug:-fallback}"
fi
[[ "$rnbo_slug" =~ ^[A-Za-z0-9_][A-Za-z0-9_-]*$ ]] || { printf 'Unsafe slug\n' >&2; exit 2; }
rnbo_dsp_root="$(cd "$rnbo_dsp_root" && pwd -P)"
rnbo_mac="$rnbo_build_root/$rnbo_slug/macos"
rnbo_win="$rnbo_build_root/$rnbo_slug/windows"
mkdir -p "$rnbo_mac" "$rnbo_win"
rnbo_common=(-DCMAKE_BUILD_TYPE=Release -DS3G_BUILD_CLAP=ON -DBUILD_TESTING=ON
  -DS3G_ENABLE_PORTABLE_CLAP_GUI=ON "-DS3G_DSP_DIR=$rnbo_dsp_root"
  "-DS3G_RNBO_EXPORT_DIR=$rnbo_export" "-DS3G_RNBO_INPUT_CHANNELS=$rnbo_inputs"
  "-DS3G_RNBO_OUTPUT_CHANNELS=$rnbo_outputs" "-DS3G_RNBO_PLUGIN_KIND=$rnbo_kind"
  "-DS3G_RNBO_PLUGIN_SLUG=$rnbo_slug")
# Optional source-cache overrides support fully offline repeat builds.
[[ -z "${S3G_CLAP_INCLUDE_DIR:-}" ]] || rnbo_common+=("-DS3G_CLAP_INCLUDE_DIR=$S3G_CLAP_INCLUDE_DIR")
[[ -z "${S3G_VSTGUI_SOURCE_DIR:-}" ]] || rnbo_common+=("-DFETCHCONTENT_SOURCE_DIR_VSTGUI=$S3G_VSTGUI_SOURCE_DIR")
[[ -z "${S3G_DR_LIBS_SOURCE_DIR:-}" ]] || rnbo_common+=("-DFETCHCONTENT_SOURCE_DIR_DR_LIBS=$S3G_DR_LIBS_SOURCE_DIR")
build_mac() {
  cmake -S "$rnbo_root" -B "$rnbo_mac" "${rnbo_common[@]}"
  cmake --build "$rnbo_mac" --config Release --target s3g_rnbo_test_clap --parallel "$rnbo_jobs"
}
build_windows() {
  cmake -S "$rnbo_root" -B "$rnbo_win" "${rnbo_common[@]}" \
    "-DCMAKE_TOOLCHAIN_FILE=$rnbo_root/cmake/toolchains/mingw-w64-x86_64.cmake"
  cmake --build "$rnbo_win" --config Release --target s3g_rnbo_test_clap --parallel "$rnbo_jobs"
}
build_mac >"$rnbo_mac/build.log" 2>&1 & rnbo_mac_pid=$!
build_windows >"$rnbo_win/build.log" 2>&1 & rnbo_win_pid=$!
printf 'Building macOS and Windows concurrently. Logs:\n%s\n%s\n' "$rnbo_mac/build.log" "$rnbo_win/build.log"
rnbo_failed=0
wait "$rnbo_mac_pid" || rnbo_failed=1
wait "$rnbo_win_pid" || rnbo_failed=1
[[ $rnbo_failed -eq 0 ]] || { printf 'Build failed; no distribution produced. See logs above.\n' >&2; exit 1; }
cmake "-DS3G_RNBO_MAC_BUILD=$rnbo_mac" "-DS3G_RNBO_WINDOWS_BUILD=$rnbo_win" \
  "-DS3G_RNBO_PACKAGE_ROOT=$rnbo_dist_root" "-DS3G_RNBO_PACKAGE_SLUG=$rnbo_slug" \
  "-DS3G_RNBO_SOURCE_ROOT=$rnbo_root" -P "$rnbo_root/cmake/PackageDual.cmake"
