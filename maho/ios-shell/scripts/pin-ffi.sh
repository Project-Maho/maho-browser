#!/usr/bin/env bash
#
# pin-ffi.sh — Vendor a CONSISTENT maho-ffi snapshot (C header + static libs) for iOS.
#
# WHY THIS EXISTS
#   Ordinary iOS builds must NOT regenerate the FFI header/lib from the live
#   `maho-ffi` crate source. In this monorepo the same working tree is edited by
#   parallel sessions; a concurrent core/FFI migration leaves the crate source,
#   the cbindgen-generated header, and the Swift bridge temporarily inconsistent,
#   which breaks the iOS build with cryptic Swift argument-label errors.
#
#   Instead, iOS links a PINNED snapshot vendored under `vendor/maho-ffi/`,
#   regenerated ONLY by running this script deliberately, from a consistent
#   (compiling) core state. Live core churn then never breaks a normal build.
#   This mirrors how desktop already consumes a prebuilt static lib under
#   `maho-chromium/third_party/maho/`.
#
# USAGE
#   ios-shell/scripts/pin-ffi.sh                 # refuses if FFI source is git-dirty
#   ios-shell/scripts/pin-ffi.sh --allow-dirty   # pin anyway (you own the risk)
#   ios-shell/scripts/pin-ffi.sh --cbindgen-config path/to/cbindgen.toml
#
# AFTER PINNING
#   1. Point project.yml at the vendored artifacts (see vendor/maho-ffi/README.md).
#   2. Ensure a pre-build phase runs scripts/check-ffi-pin.sh.
#   3. Commit vendor/maho-ffi/ together with the matching Swift bridge changes.
#
set -euo pipefail

# ── Config (mirrors ios-shell/Makefile) ──────────────────────────────────────
RUST_TARGET_IOS="aarch64-apple-ios"
RUST_TARGET_SIM="aarch64-apple-ios-sim"
IOS_DEPLOYMENT_TARGET="17.0"
LIB_NAME="libmaho_ffi.a"
FFI_CRATE="maho-ffi"

ALLOW_DIRTY=0
CBINDGEN_CONFIG=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    --cbindgen-config) CBINDGEN_CONFIG="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
    *) echo "pin-ffi: unknown arg: $1" >&2; exit 2 ;;
  esac
done

# ── Path resolution (relative to this script) ────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IOS_SHELL="$(cd "$SCRIPT_DIR/.." && pwd)"          # maho/ios-shell
WORKSPACE="$(cd "$IOS_SHELL/.." && pwd)"           # maho/ (has Cargo.toml)
TARGET_DIR="$WORKSPACE/target"
VENDOR="$IOS_SHELL/vendor/maho-ffi"
[ -n "$CBINDGEN_CONFIG" ] || CBINDGEN_CONFIG="$IOS_SHELL/cbindgen.toml"

# ── Preconditions ────────────────────────────────────────────────────────────
command -v cbindgen >/dev/null 2>&1 || {
  echo "pin-ffi: ERROR: cbindgen not found on PATH (cargo install cbindgen)." >&2
  exit 1
}
[ -f "$WORKSPACE/Cargo.toml" ] || {
  echo "pin-ffi: ERROR: $WORKSPACE/Cargo.toml not found." >&2
  exit 1
}
[ -f "$CBINDGEN_CONFIG" ] || {
  echo "pin-ffi: ERROR: cbindgen config not found: $CBINDGEN_CONFIG" >&2
  exit 1
}

# ── Consistency guard: refuse to pin from a dirty FFI source ──────────────────
# The whole point is to pin a CONSISTENT snapshot. Pinning while the crate source
# (or the Swift bridge that must match it) is mid-migration would vendor a broken
# artifact. Override only if you know the tree is consistent.
dirty="$(git -C "$WORKSPACE" status --porcelain -- \
           crates/maho-ffi crates/maho-core crates/maho-types \
           ios-shell/Bridge 2>/dev/null || true)"
if [ -n "$dirty" ] && [ "$ALLOW_DIRTY" -ne 1 ]; then
  echo "pin-ffi: REFUSING to pin — FFI source / Swift bridge is git-dirty:" >&2
  echo "$dirty" | sed 's/^/    /' >&2
  echo "" >&2
  echo "  Pin only from a consistent, compiling state. If a core/FFI migration is" >&2
  echo "  in progress, wait for it to land. Re-run with --allow-dirty to override." >&2
  exit 1
fi

echo "pin-ffi: building $FFI_CRATE static libs (ios + sim)…"
IPHONEOS_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
  cargo build --target "$RUST_TARGET_IOS" --release -p "$FFI_CRATE" --manifest-path "$WORKSPACE/Cargo.toml"
IPHONEOS_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
  cargo build --target "$RUST_TARGET_SIM" --release -p "$FFI_CRATE" --manifest-path "$WORKSPACE/Cargo.toml"

IOS_LIB="$TARGET_DIR/$RUST_TARGET_IOS/release/$LIB_NAME"
SIM_LIB="$TARGET_DIR/$RUST_TARGET_SIM/release/$LIB_NAME"
[ -f "$IOS_LIB" ] || { echo "pin-ffi: ERROR: missing $IOS_LIB" >&2; exit 1; }
[ -f "$SIM_LIB" ] || { echo "pin-ffi: ERROR: missing $SIM_LIB" >&2; exit 1; }

# ── Vendor layout ────────────────────────────────────────────────────────────
mkdir -p "$VENDOR/include" "$VENDOR/lib/ios" "$VENDOR/lib/sim"

echo "pin-ffi: generating header via cbindgen…"
cbindgen --config "$CBINDGEN_CONFIG" --crate "$FFI_CRATE" \
  --output "$VENDOR/include/maho_ffi.h" "$WORKSPACE"

cp "$IOS_LIB" "$VENDOR/lib/ios/$LIB_NAME"
cp "$SIM_LIB" "$VENDOR/lib/sim/$LIB_NAME"

# ── Pin metadata ─────────────────────────────────────────────────────────────
HEADER_SHA="$(shasum -a 256 "$VENDOR/include/maho_ffi.h" | awk '{print $1}')"
SRC_SHA="$(git -C "$WORKSPACE" rev-parse HEAD 2>/dev/null || echo unknown)"
NOW="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
cat > "$VENDOR/maho_ffi.pin" <<EOF
# Auto-written by scripts/pin-ffi.sh — do not edit by hand.
source_commit=$SRC_SHA
header_sha256=$HEADER_SHA
cbindgen_config=$(basename "$CBINDGEN_CONFIG")
rust_targets=$RUST_TARGET_IOS,$RUST_TARGET_SIM
ios_deployment_target=$IOS_DEPLOYMENT_TARGET
pinned_at=$NOW
allow_dirty=$ALLOW_DIRTY
EOF

echo ""
echo "pin-ffi: DONE"
echo "  header : $VENDOR/include/maho_ffi.h  (sha256 $HEADER_SHA)"
echo "  ios lib: $VENDOR/lib/ios/$LIB_NAME"
echo "  sim lib: $VENDOR/lib/sim/$LIB_NAME"
echo "  pin    : $VENDOR/maho_ffi.pin  (source_commit $SRC_SHA)"
echo ""
echo "  Next: point project.yml HEADER_SEARCH_PATHS/LIBRARY_SEARCH_PATHS at vendor/"
echo "        (see vendor/maho-ffi/README.md), then commit vendor/ + Swift bridge."
