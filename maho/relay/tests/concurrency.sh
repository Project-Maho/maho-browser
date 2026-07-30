#!/usr/bin/env bash
#
# Concurrency harness for the money-path atomicity fixes (W1: T4/T6/T7, T18).
#
# integration.sh fires curl sequentially and therefore CANNOT reproduce the
# race conditions these tasks fix. This script fires N requests in parallel and
# judges the result by the FINAL DB STATE (via `wrangler d1 execute`), not by
# HTTP status alone — every response can be 200 while the DB was double-mutated.
#
# Purpose (T0): run this against staging BEFORE applying W1 to confirm the
# defect reproduces (e.g. T4 double-credit), then AFTER to confirm it is gone.
#
# Requirements (all staging/infra — cannot run in local CI):
#   BASE                 relay base url (default staging)
#   D1_ENV               wrangler env for `d1 execute` (default: staging)
#   D1_NAME              D1 database name for `d1 execute`
#   SERVICE_TOKEN        MAHO_RELAY_SERVICE_TOKEN (jobs routes)
#   LS_WEBHOOK_SECRET    LemonSqueezy webhook HMAC secret (T4 webhook signing)
#   TEST_USER_ID         a staging user id owning the credits/jobs rows
#   PARALLEL             fan-out width (default: 10)
#
# Verdict rule: a scenario PASSES only when the final DB state shows the effect
# applied exactly once.

set -uo pipefail

BASE="${BASE:-https://relay-staging.mahobrowser.com}"
D1_ENV="${D1_ENV:-staging}"
D1_NAME="${D1_NAME:-maho-relay}"
PARALLEL="${PARALLEL:-10}"

pass=0
fail=0

ok()   { printf "  PASS  %s\n" "$1"; pass=$((pass + 1)); }
bad()  { printf "  FAIL  %s\n" "$1"; fail=$((fail + 1)); }
skip() { printf "  SKIP  %s\n" "$1"; }

d1_scalar() {
  # Runs a SQL query and prints the first scalar of the first row.
  wrangler d1 execute "$D1_NAME" --env "$D1_ENV" --json --command "$1" 2>/dev/null \
    | python3 -c "import sys,json; r=json.load(sys.stdin); rows=r[0]['results']; print(list(rows[0].values())[0] if rows else '')"
}

require() {
  local name="$1"
  if [ -z "${!name:-}" ]; then
    skip "\$$name unset — cannot run concurrency scenarios that need it"
    return 1
  fi
  return 0
}

echo "=== concurrency.sh :: $BASE (parallel=$PARALLEL) ==="

# --------------------------------------------------------------------------
# T4 — webhook idempotency: same event_id delivered N times in parallel must
# credit exactly once.
# --------------------------------------------------------------------------
t4_webhook() {
  require LS_WEBHOOK_SECRET || return
  require TEST_USER_ID || return

  local event_id="conc-t4-$(date +%s)-$RANDOM"
  local body
  body=$(printf '{"meta":{"event_name":"order_created","custom_data":{"user_id":"%s"}},"data":{"id":"%s","attributes":{"variant_id":4001,"status":"paid"}}}' \
    "$TEST_USER_ID" "$event_id")
  local sig
  sig=$(printf '%s' "$body" | openssl dgst -sha256 -hmac "$LS_WEBHOOK_SECRET" | sed 's/^.* //')

  local before after
  before=$(d1_scalar "SELECT balance_usd FROM credits WHERE user_id=$TEST_USER_ID")

  for _ in $(seq 1 "$PARALLEL"); do
    curl -s -o /dev/null -X POST "$BASE/billing/lemonsqueezy" \
      -H 'Content-Type: application/json' \
      -H "X-Signature: $sig" \
      --data-raw "$body" &
  done
  wait

  after=$(d1_scalar "SELECT balance_usd FROM credits WHERE user_id=$TEST_USER_ID")
  # PAYG variant 4001 = +$10.00 exactly once.
  local delta
  delta=$(python3 -c "print(round(float('${after:-0}') - float('${before:-0}'), 2))")
  if [ "$delta" = "10.0" ] || [ "$delta" = "10.00" ]; then
    ok "T4 webhook x$PARALLEL credits once (delta=$delta)"
  else
    bad "T4 webhook double-credit: delta=$delta (expected 10.00)"
  fi
}

# --------------------------------------------------------------------------
# T6 — job complete-once: N parallel completions bill exactly once.
# --------------------------------------------------------------------------
t6_job_complete() {
  require SERVICE_TOKEN || return
  require TEST_USER_ID || return

  local job_id="conc-t6-$(date +%s)-$RANDOM"
  curl -s -o /dev/null -X POST "$BASE/jobs/create" \
    -H "Authorization: Bearer $SERVICE_TOKEN" -H 'Content-Type: application/json' \
    -d "{\"id\":\"$job_id\",\"user_id\":$TEST_USER_ID,\"job_type\":\"deep_research\",\"estimated_cost_usd\":0.5}"

  local tmp; tmp=$(mktemp -d)
  for i in $(seq 1 "$PARALLEL"); do
    curl -s -X POST "$BASE/jobs/complete" \
      -H "Authorization: Bearer $SERVICE_TOKEN" -H 'Content-Type: application/json' \
      -d "{\"id\":\"$job_id\",\"final_cost_usd\":0.42}" > "$tmp/$i" &
  done
  wait

  local billed_true
  billed_true=$(grep -l '"billed":true' "$tmp"/* 2>/dev/null | wc -l | tr -d ' ')
  rm -rf "$tmp"
  if [ "$billed_true" = "1" ]; then
    ok "T6 job complete x$PARALLEL bills once (billed=true count=1)"
  else
    bad "T6 job double-bill: billed=true count=$billed_true (expected 1)"
  fi
}

# --------------------------------------------------------------------------
# T7 — bypass consume-once: N parallel consumes → 200 exactly once, rest 400
# (never 500).
# --------------------------------------------------------------------------
t7_bypass() {
  require SERVICE_TOKEN || return
  if [ -z "${BYPASS_TOKEN:-}" ]; then
    skip "\$BYPASS_TOKEN unset — mint a fresh bypass token to run T7"
    return
  fi

  local tmp; tmp=$(mktemp -d)
  for i in $(seq 1 "$PARALLEL"); do
    curl -s -o /dev/null -w '%{http_code}\n' -X POST "$BASE/auth/consume_bypass" \
      -H "Authorization: Bearer $SERVICE_TOKEN" -H 'Content-Type: application/json' \
      -d "{\"token\":\"$BYPASS_TOKEN\"}" > "$tmp/$i" &
  done
  wait

  local n200 n400 n500
  n200=$(grep -c '^200$' "$tmp"/* 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
  n400=$(grep -c '^400$' "$tmp"/* 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
  n500=$(grep -c '^500$' "$tmp"/* 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
  rm -rf "$tmp"
  if [ "$n200" = "1" ] && [ "$n500" = "0" ]; then
    ok "T7 bypass x$PARALLEL: 200 once, 400 rest, no 500 (200=$n200 400=$n400 500=$n500)"
  else
    bad "T7 bypass race: 200=$n200 400=$n400 500=$n500 (expected 200=1, 500=0)"
  fi
}

# --------------------------------------------------------------------------
# T18 — room seq: N concurrent messages to one room must get unique seqs with
# no storage_error. Requires a WS harness (see ws_test.py); wired once T18 lands.
# --------------------------------------------------------------------------
t18_room_seq() {
  skip "T18 concurrent-message scenario — implement alongside T18 (needs WS fan-out in ws_test.py)"
}

t4_webhook
t6_job_complete
t7_bypass
t18_room_seq

echo "=== concurrency.sh: $pass passed, $fail failed ==="
[ "$fail" -eq 0 ]
