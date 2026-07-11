#!/usr/bin/env bash
# Self-test for the session-independent AI E2E harness (e2e_chromium_ai.js).
#
# Proves the two mechanics the harness relies on:
#   S1  serve+attach   — a --serve instance is drivable via repeated --attach
#   S3  concurrent      — two --serve instances on distinct ports coexist
#   S4  default         — one-shot default mode still runs a scenario
#   S2  churn-survival  — GUARDED (MAHO_E2E_CHURN_TEST=1): a running instance
#                         survives an out/Default dylib inode swap (mmap keeps
#                         the old inode). OFF by default because it mutates the
#                         SHARED out/Default that other dev sessions build into.
#                         Only enable when no concurrent build holds .build.lock.
#
# Usage:
#   maho-chromium/build/scripts/e2e_isolate_selftest.sh
#   MAHO_E2E_CHURN_TEST=1 maho-chromium/build/scripts/e2e_isolate_selftest.sh
#
# Requires a rendered out/Default (or MAHO_APP_PATH) and, for the ai_real_turn
# path, a configured provider — but this self-test uses only ai_panel_renders.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HARNESS="$SCRIPT_DIR/e2e_chromium_ai.js"
REPO="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUT="${MAHO_APP_PATH:-$REPO/chromium/src/out/Default/Maho.app}"
OUT_DIR="$(cd "$(dirname "$OUT")" && pwd)"

PORT_A="${MAHO_E2E_CDP_PORT:-9422}"
PORT_B=$((PORT_A + 1))
PORT_C=$((PORT_A + 2))

PIDS=()
RESTORE_FROM=""
RESTORE_TO=""

log() { printf '%s\n' "$*"; }

cleanup() {
  for pid in "${PIDS[@]:-}"; do
    [ -n "$pid" ] && kill "$pid" 2>/dev/null
  done
  # Restore any dylib moved aside by the churn test, even on failure/interrupt.
  if [ -n "$RESTORE_FROM" ] && [ -f "$RESTORE_FROM" ]; then
    mv -f "$RESTORE_FROM" "$RESTORE_TO" 2>/dev/null
  fi
}
trap cleanup EXIT INT TERM

fail() { log "SELFTEST FAIL: $*"; exit 1; }

wait_for_cdp() {
  local port="$1" deadline=$(( $(date +%s) + 45 ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if curl -sf "http://127.0.0.1:${port}/json/version" >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  return 1
}

serve() {
  local port="$1"
  MAHO_E2E_CDP_PORT="$port" node "$HARNESS" --serve >/tmp/maho-selftest-serve-${port}.log 2>&1 &
  PIDS+=("$!")
}

# --- S1: serve + attach ------------------------------------------------------
log "S1: serve on $PORT_A, then attach ai_panel_renders"
serve "$PORT_A"
wait_for_cdp "$PORT_A" || fail "S1 CDP $PORT_A never came up"
MAHO_E2E_CDP_PORT="$PORT_A" node "$HARNESS" --attach --scenario ai_panel_renders \
  | tee /tmp/maho-selftest-s1.log
grep -q "PASS ai_panel_renders" /tmp/maho-selftest-s1.log || fail "S1 attach render did not PASS"
log "S1 OK"

# --- S2: churn survival (guarded) -------------------------------------------
if [ "${MAHO_E2E_CHURN_TEST:-0}" = "1" ]; then
  log "S2: swap an out/Default dylib inode; served instance must survive"
  # head -1 may pick a dylib the instance never mmap'd, weakening the assertion;
  # the survival guarantee is exact for loaded dylibs, best-effort here.
  DYLIB="$(ls "$OUT_DIR"/*.dylib 2>/dev/null | head -1)"
  [ -n "$DYLIB" ] || fail "S2 no dylib found in $OUT_DIR"
  RESTORE_TO="$DYLIB"
  RESTORE_FROM="${DYLIB}.selftest-bak"
  # New inode replaces the path (what a linker rewrite does); running instance
  # keeps the old inode via mmap. ditto (not cp) preserves the code signature
  # and xattrs so a later fresh launch of the restored dylib still loads.
  mv "$DYLIB" "$RESTORE_FROM" || fail "S2 mv aside failed"
  ditto "$RESTORE_FROM" "$DYLIB" || { mv -f "$RESTORE_FROM" "$DYLIB"; fail "S2 restore-copy failed"; }
  rm -f "$RESTORE_FROM"; RESTORE_FROM=""
  curl -sf "http://127.0.0.1:${PORT_A}/json/version" >/dev/null 2>&1 \
    || fail "S2 served instance stopped serving after inode swap"
  MAHO_E2E_CDP_PORT="$PORT_A" node "$HARNESS" --attach --scenario ai_panel_renders \
    | tee /tmp/maho-selftest-s2.log
  grep -q "PASS ai_panel_renders" /tmp/maho-selftest-s2.log \
    || fail "S2 attach render failed after inode swap"
  log "S2 OK (mmap survival confirmed)"
else
  log "S2 SKIPPED (set MAHO_E2E_CHURN_TEST=1 to run; mutates shared out/Default)"
fi

# --- S3: two concurrent served instances ------------------------------------
log "S3: second serve on $PORT_B; both ports attachable"
serve "$PORT_B"
wait_for_cdp "$PORT_B" || fail "S3 CDP $PORT_B never came up"
MAHO_E2E_CDP_PORT="$PORT_B" node "$HARNESS" --attach --scenario ai_panel_renders \
  | tee /tmp/maho-selftest-s3.log
grep -q "PASS ai_panel_renders" /tmp/maho-selftest-s3.log || fail "S3 port $PORT_B render failed"
curl -sf "http://127.0.0.1:${PORT_A}/json/version" >/dev/null 2>&1 \
  || fail "S3 first instance ($PORT_A) died when second started"
log "S3 OK"

# --- S4: default one-shot mode ----------------------------------------------
# Default mode launches its OWN instance, so it needs a free port distinct from
# the S1/S3 served instances still holding PORT_A and PORT_B.
log "S4: default one-shot mode runs ai_panel_renders on $PORT_C"
MAHO_E2E_CDP_PORT="$PORT_C" node "$HARNESS" --scenario ai_panel_renders \
  | tee /tmp/maho-selftest-s4.log
grep -q "PASS ai_panel_renders" /tmp/maho-selftest-s4.log || fail "S4 default mode render failed"
log "S4 OK"

log "SELFTEST PASS"
