#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$repo_root/src/s3g_rnbo_test_clap.cpp"
cmake="$repo_root/CMakeLists.txt"
readme="$repo_root/README.md"
notes="$repo_root/docs/rnbo-clap-wrapper.md"
dsp_root="${S3G_DSP_DIR:-$repo_root/../s3g-dsp}"
dsp_guide="$dsp_root/docs/gui-style-guide.md"

fail=0

warn() {
  printf 'gui-style audit: %s\n' "$*" >&2
  fail=1
}

require_pattern() {
  local pattern="$1"
  local file="$2"
  local label="$3"
  if ! grep -Eq "$pattern" "$file"; then
    warn "missing ${label} in ${file#$repo_root/}"
  fi
}

reject_pattern() {
  local pattern="$1"
  local file="$2"
  local label="$3"
  if grep -Eq "$pattern" "$file"; then
    warn "found ${label} in ${file#$repo_root/}"
  fi
}

require_pattern 'constexpr uint32_t kGuiPanelFill = 0x1d1d1d;' "$src" 's3g-dsp panel fill'
require_pattern 'constexpr uint32_t kGuiBorder = 0x565656;' "$src" 's3g-dsp grid/border color'
require_pattern 'constexpr uint32_t kGuiBorderActive = 0xb8b8b8;' "$src" 's3g-dsp accent color'
require_pattern 'constexpr uint32_t kGuiLabel = 0xa8a8a8;' "$src" 's3g-dsp label color'
require_pattern 'constexpr uint32_t kGuiValue = 0x929292;' "$src" 's3g-dsp value color'
require_pattern 'constexpr uint32_t kGuiTitle = 0xc8c8c8;' "$src" 's3g-dsp title color'
require_pattern 'constexpr CGFloat kGuiContentTop = 42;' "$src" 'shared y 42 content top'
require_pattern 'kGuiPanelTitleX = kGuiPanelX \+ 8' "$src" '8 px panel-title inset'
require_pattern 'kGuiStartX = kGuiPanelX \+ 16' "$src" '16 px ordinary-control inset'
require_pattern 'NSFont\* titleFont = .*size:10\.5' "$src" 'regular Menlo 10.5 title font'
require_pattern 'guiPluginTitle\(\).*drawAtPoint' "$src" 'draw-time GUI title formatter'
require_pattern 'guiPeakDbText\(' "$src" 'dBFS peak formatter'
require_pattern 'stringWithFormat:@"PK %\+4\.1f"' "$src" 'shared signed one-decimal peak format'
require_pattern '\[NSApp isActive\]' "$src" 'host foreground redraw gate'
require_pattern 'isHiddenOrHasHiddenAncestor' "$src" 'hidden-view redraw gate'
require_pattern '\[event clickCount\] >= 2' "$src" 'slider double-click default reset'
require_pattern 'static_cast<uint64_t>\(n\) > size' "$src" 'bounded partial state stream handling'
require_pattern 'uppercaseGuiLabel\(S3G_RNBO_PLUGIN_NAME, true\)' "$src" 'lowercase s3g title preservation'
require_pattern 'guiNSStringUpper\(displayName\)' "$src" 'GUI-only RNBO parameter uppercase'
require_pattern 'set\(S3G_RNBO_DERIVED_PLUGIN_NAME "s3g RNBO \$\{S3G_RNBO_PLUGIN_NAME_SUFFIX\}"\)' "$cmake" 'host-facing family-first plugin name'
require_pattern 'title-cased RNBO export folder name' "$cmake" 'host-name cache description'
require_pattern 'host-facing plugin names' "$readme" 'README host/GUI name split'
require_pattern 'guiPluginTitle\(\)' "$notes" 'notes reference to GUI title source of truth'

reject_pattern '0xf0f0f0|0xe8e8e8|0xd1d1d1' "$src" 'old bright GUI color'
reject_pattern 'Menlo-Bold|NSFontWeightBold' "$src" 'bold UI font'
reject_pattern 'stringWithFormat:@"PK %\.3f"' "$src" 'linear hand-formatted peak readout'
reject_pattern 'set\(S3G_RNBO_DERIVED_PLUGIN_NAME "s3g rnbo ' "$cmake" 'lowercase host family name'
reject_pattern 'uppercased RNBO export folder name' "$cmake" 'all-caps host-name cache description'

if [[ -f "$dsp_guide" ]]; then
  require_pattern 'Begin the first toolbox or primary field/view' "$dsp_guide" 'upstream content-top contract'
  require_pattern 'at y 42 px in every editor' "$dsp_guide" 'upstream y 42 content top'
  require_pattern 'Double-clicking any slider returns that parameter' "$dsp_guide" 'upstream slider reset contract'
  require_pattern 'Peak status uses `peakDbText\(\)`' "$dsp_guide" 'upstream dBFS peak contract'
else
  printf 'gui-style audit: sibling s3g-dsp guide not found; local contract checks only (%s)\n' "$dsp_guide" >&2
fi

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

printf 'gui-style audit passed\n'
