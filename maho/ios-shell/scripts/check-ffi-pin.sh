#!/usr/bin/env bash
#
# check-ffi-pin.sh — Fast pre-build guard for the vendored maho-ffi snapshot.
#
# Verifies that vendor/maho-ffi/ still matches the snapshot recorded in
# vendor/maho-ffi/maho_ffi.pin:
#   - the header's sha256 (catches an out-of-band cbindgen run),
#   - both static libs' sha256 (catches a swapped/partial vendor), and
#   - a content digest of crates/{maho-ffi,maho-core,maho-types} (catches the
#     dangerous case: libs pinned BEFORE a core change, which build fine and
#     silently run old behavior because every FFI symbol still exists).
# On any mismatch the build STOPS here with a clear, actionable message instead
# of failing later with cryptic Swift argument-label errors or, worse, shipping
# a build whose new events the linked core does not understand.
#
# Wire this as an Xcode pre-build Run Script phase (or call it before xcodebuild):
#   "$SRCROOT/scripts/check-ffi-pin.sh"
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IOS_SHELL="$(cd "$SCRIPT_DIR/.." && pwd)"          # maho/ios-shell
WORKSPACE="$(cd "$IOS_SHELL/.." && pwd)"           # maho/ (has Cargo.toml)
VENDOR="$IOS_SHELL/vendor/maho-ffi"
HEADER="$VENDOR/include/maho_ffi.h"
LIB_IOS="$VENDOR/lib/ios/libmaho_ffi.a"
LIB_SIM="$VENDOR/lib/sim/libmaho_ffi.a"
PIN="$VENDOR/maho_ffi.pin"

# sha256 of the FFI-relevant Rust sources as they exist on disk right now.
# Content-based on purpose: it must catch a pin taken before a core edit whether
# or not that edit is committed.
core_source_digest() {
  ( cd "$WORKSPACE" && find crates/maho-ffi crates/maho-core crates/maho-types \
      -path '*/target/*' -prune -o \
      \( -name '*.rs' -o -name 'Cargo.toml' \) -type f -print \
    | LC_ALL=C sort \
    | xargs shasum -a 256 \
    | shasum -a 256 | awk '{print $1}' )
}

file_sha() { shasum -a 256 "$1" | awk '{print $1}'; }

pin_value() { { grep "^$1=" "$PIN" 2>/dev/null || true; } | cut -d= -f2; }

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

# ── The static libs must come from the sources currently on disk ─────────────
# The header check alone cannot catch a stale pin: the app links the prebuilt
# libs, and a pin taken before a core change keeps building and running while
# silently ignoring the new events. Compare the recorded libs and source digest.
for lib in "$LIB_IOS" "$LIB_SIM"; do
  if [ ! -f "$lib" ]; then
    echo "check-ffi-pin: ERROR: missing vendored lib: $lib" >&2
    echo "  Run: ios-shell/scripts/pin-ffi.sh   (from a consistent core state)" >&2
    exit 1
  fi
done

exp_lib_ios="$(pin_value lib_sha256_ios)"
exp_lib_sim="$(pin_value lib_sha256_sim)"
exp_src="$(pin_value core_source_sha256)"

if [ -z "$exp_lib_ios" ] || [ -z "$exp_lib_sim" ] || [ -z "$exp_src" ]; then
  echo "check-ffi-pin: ERROR: the pin predates the lib/source integrity fields." >&2
  echo "  The vendored libs cannot be proven to match the current core sources." >&2
  echo "  Run: ios-shell/scripts/pin-ffi.sh   (from a consistent core state)" >&2
  exit 1
fi

for pair in "ios:$LIB_IOS:$exp_lib_ios" "sim:$LIB_SIM:$exp_lib_sim"; do
  name="${pair%%:*}"; rest="${pair#*:}"; path="${rest%%:*}"; want="${rest#*:}"
  got="$(file_sha "$path")"
  if [ "$want" != "$got" ]; then
    echo "check-ffi-pin: ERROR: vendored $name lib does not match its pin." >&2
    echo "    expected sha256: $want" >&2
    echo "    actual   sha256: $got" >&2
    echo "  Run: ios-shell/scripts/pin-ffi.sh   (from a consistent core state)" >&2
    exit 1
  fi
done

want_src="$exp_src"
got_src="$(core_source_digest)"
if [ "$want_src" != "$got_src" ]; then
  echo "check-ffi-pin: ERROR: the vendored libs are STALE - core/FFI sources changed" >&2
  echo "  after this pin was taken, so the app would link old core behavior." >&2
  echo "    pinned source sha256:  $want_src" >&2
  echo "    current source sha256: $got_src" >&2
  echo "" >&2
  echo "  Re-pin from the current (consistent, compiling) core state:" >&2
  echo "    ios-shell/scripts/pin-ffi.sh" >&2
  echo "  Then commit vendor/maho-ffi/ together with the Swift bridge changes." >&2
  exit 1
fi

echo "check-ffi-pin: OK (libs + core source digest match the pin)"
