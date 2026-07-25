#!/usr/bin/env bash
#
# check-ffi-pin.sh — Fast pre-build guard for the vendored maho-ffi snapshot.
#
# Verifies that vendor/maho-ffi/include/maho_ffi.h still matches the sha256
# recorded in vendor/maho-ffi/maho_ffi.pin. If it drifted (e.g. a concurrent
# session ran cbindgen and regenerated the header in place), the build STOPS
# here with a clear, actionable message instead of failing later with cryptic
# Swift argument-label errors.
#
# Wire this as an Xcode pre-build Run Script phase (or call it before xcodebuild):
#   "$SRCROOT/scripts/check-ffi-pin.sh"
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IOS_SHELL="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR="$IOS_SHELL/vendor/maho-ffi"
HEADER="$VENDOR/include/maho_ffi.h"
PIN="$VENDOR/maho_ffi.pin"

if [ ! -f "$PIN" ] || [ ! -f "$HEADER" ]; then
  echo "check-ffi-pin: ERROR: FFI not pinned yet (missing pin or header)." >&2
  echo "  Run: ios-shell/scripts/pin-ffi.sh   (from a consistent core state)" >&2
  exit 1
fi

expected="$(grep '^header_sha256=' "$PIN" | cut -d= -f2)"
actual="$(shasum -a 256 "$HEADER" | awk '{print $1}')"

if [ "$expected" != "$actual" ]; then
  echo "check-ffi-pin: ERROR: vendored maho_ffi.h DRIFTED from its pin." >&2
  echo "    expected sha256: $expected" >&2
  echo "    actual   sha256: $actual" >&2
  echo "" >&2
  echo "  The vendored header was regenerated/edited outside of pin-ffi.sh" >&2
  echo "  (likely a concurrent core/FFI migration touched it)." >&2
  echo "" >&2
  echo "  If this change is INTENTIONAL:" >&2
  echo "    1) ensure the core FFI state is consistent + compiling," >&2
  echo "    2) run ios-shell/scripts/pin-ffi.sh," >&2
  echo "    3) update the Swift bridge (MahoBridge+*.swift) to match, commit together." >&2
  echo "  If it is NOT intentional:" >&2
  echo "    git checkout -- ios-shell/vendor/maho-ffi/" >&2
  exit 1
fi

echo "check-ffi-pin: OK (maho_ffi.h matches pin $actual)"
