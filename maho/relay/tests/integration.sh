#!/usr/bin/env bash
set -euo pipefail

BASE="${BASE:-https://relay-staging.mahobrowser.com}"

pass=0
fail=0

check() {
  local expected="$1"
  local actual="$2"
  local name="$3"
  if [ "$expected" = "$actual" ]; then
    printf "  PASS  %s\n" "$name"
    pass=$((pass + 1))
  else
    printf "  FAIL  %s (expected %q, got %q)\n" "$name" "$expected" "$actual"
    fail=$((fail + 1))
  fi
}

json_field() {
  python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('$1', ''))"
}

TS=$(date +%s)

echo "=== integration.sh :: $BASE ==="

RES=$(curl -sw '\n%{http_code}' "$BASE/health")
CODE=$(echo "$RES" | tail -1)
BODY=$(echo "$RES" | head -1)
check "200" "$CODE" "GET /health returns 200"
check "ok" "$BODY" "GET /health body == ok"

RES=$(curl -sX POST "$BASE/waitlist" \
  -H 'Content-Type: application/json' \
  -d "{\"email\":\"itest-waitlist-$TS@example.com\",\"source\":\"general\"}")
OK=$(echo "$RES" | json_field ok)
check "True" "$OK" "POST /waitlist first-time returns ok=true"

RES=$(curl -sX POST "$BASE/waitlist" \
  -H 'Content-Type: application/json' \
  -d "{\"email\":\"itest-waitlist-$TS@example.com\",\"source\":\"general\"}")
DEDUPE=$(echo "$RES" | json_field already_signed_up)
check "True" "$DEDUPE" "POST /waitlist duplicate returns already_signed_up=true"

if echo "$BASE" | grep -q staging; then
  SIGNUP_BODY="{\"email\":\"itest-signup-$TS@example.com\",\"password\":\"correct horse battery staple\",\"display_name\":\"IT\",\"device_name\":\"CI\",\"device_type\":\"desktop\"}"
  RES=$(curl -sX POST "$BASE/auth/signup" -H 'Content-Type: application/json' -d "$SIGNUP_BODY")
  TOKEN=$(echo "$RES" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('access_token', ''))")
  if [ -n "$TOKEN" ]; then
    printf "  PASS  POST /auth/signup returns access_token\n"
    pass=$((pass + 1))
  else
    printf "  FAIL  POST /auth/signup (no access_token in response: %s)\n" "$RES"
    fail=$((fail + 1))
  fi

  RES=$(curl -sw '\n%{http_code}' -H "Authorization: Bearer $TOKEN" "$BASE/auth/me")
  CODE=$(echo "$RES" | tail -1)
  check "200" "$CODE" "GET /auth/me with valid token returns 200"

  RES=$(curl -sw '\n%{http_code}' -H "Authorization: Bearer invalid" "$BASE/auth/me")
  CODE=$(echo "$RES" | tail -1)
  check "401" "$CODE" "GET /auth/me with invalid token returns 401"
else
  echo "  SKIP  auth signup tests on production (OPEN_REGISTRATION=false)"
fi

echo "----"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
