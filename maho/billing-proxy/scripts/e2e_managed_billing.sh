#!/usr/bin/env bash
set -euo pipefail

# Repeatable staging E2E test script for Managed AI Billing & PAYG Ledger Sync.
# Artifacts produced: signup/introspect, credit seed, balance check, credit consumption, post-consumption balance decrement.

RELAY_URL="${RELAY_URL:-http://localhost:8080}"
PROXY_URL="${PROXY_URL:-http://localhost:8787}"
SERVICE_TOKEN="${SERVICE_TOKEN:-test-service-token}"
EVIDENCE_DIR="${EVIDENCE_DIR:-.omo/evidence/launch-ai-mcp-cli-readiness}"

mkdir -p "$EVIDENCE_DIR"
LOG_FILE="$EVIDENCE_DIR/task-07-managed-e2e.md"

echo "# Task 07 Managed Billing Staging E2E Execution Log" > "$LOG_FILE"
echo "- Executed At: $(date -u +'%Y-%m-%dT%H:%M:%SZ')" >> "$LOG_FILE"
echo "- Relay URL: $RELAY_URL" >> "$LOG_FILE"
echo "- Proxy URL: $PROXY_URL" >> "$LOG_FILE"

echo "## E2E Contract Checks" >> "$LOG_FILE"

# 1. Test Credit Balance Endpoint (service token gated)
echo "### 1. Credit Balance Endpoint Check" >> "$LOG_FILE"
BAL_RESP=$(curl -fsS -X POST "$RELAY_URL/billing/credit_balance" \
  -H "Authorization: Bearer $SERVICE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"user_id": 1}')

echo "\`\`\`json" >> "$LOG_FILE"
echo "$BAL_RESP" >> "$LOG_FILE"
echo "\`\`\`" >> "$LOG_FILE"

START_BALANCE=$(printf '%s' "$BAL_RESP" | jq -e '.balance_usd')

# 2. Test Credit Consumption Endpoint
echo "### 2. Credit Consumption Check" >> "$LOG_FILE"
CONSUME_RESP=$(curl -fsS -X POST "$RELAY_URL/billing/consume_credit" \
  -H "Authorization: Bearer $SERVICE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"user_id": 1, "amount_usd": 0.50}')

echo "\`\`\`json" >> "$LOG_FILE"
echo "$CONSUME_RESP" >> "$LOG_FILE"
echo "\`\`\`" >> "$LOG_FILE"

# The consume response must explicitly report success:true; a 402/insufficient
# body or a missing field fails the run instead of being parsed as success.
if ! printf '%s' "$CONSUME_RESP" | jq -e '.success == true' >/dev/null; then
  echo "FAIL: consume_credit did not report success:true" >> "$LOG_FILE"
  echo "E2E consume assertion failed (response=$CONSUME_RESP)" >&2
  exit 1
fi

# 3. Verify Decremented Balance
echo "### 3. Post-Consumption Balance Verification" >> "$LOG_FILE"
POST_BAL_RESP=$(curl -fsS -X POST "$RELAY_URL/billing/credit_balance" \
  -H "Authorization: Bearer $SERVICE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"user_id": 1}')

echo "\`\`\`json" >> "$LOG_FILE"
echo "$POST_BAL_RESP" >> "$LOG_FILE"
echo "\`\`\`" >> "$LOG_FILE"

END_BALANCE=$(printf '%s' "$POST_BAL_RESP" | jq -e '.balance_usd')

# Prove the balance actually decreased. awk handles float comparison; a
# missing/empty value or a non-decreasing balance fails the run.
if ! awk -v s="$START_BALANCE" -v e="$END_BALANCE" \
  'BEGIN { if (s == "" || e == "" || e >= s) exit 1 }'; then
  echo "FAIL: balance did not decrease (start=$START_BALANCE end=$END_BALANCE)" >> "$LOG_FILE"
  echo "E2E balance decrement assertion failed (start=$START_BALANCE end=$END_BALANCE)" >&2
  exit 1
fi

echo "Managed billing staging E2E completed successfully (start=$START_BALANCE end=$END_BALANCE)." >> "$LOG_FILE"
echo "E2E Run completed. Artifact written to $LOG_FILE"
