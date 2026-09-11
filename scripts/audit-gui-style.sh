#!/usr/bin/env bash
set -euo pipefail
rnbo_audit_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec cmake "-DS3G_RNBO_ROOT=$rnbo_audit_root" "-DS3G_DSP_DIR=${S3G_DSP_DIR:-$rnbo_audit_root/../s3g-dsp}" -P "$rnbo_audit_root/cmake/AuditGui.cmake"
