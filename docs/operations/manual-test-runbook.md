# Manual Test Runbook - Aside Follow-up BM Redesign

**Audience**: Engineers verifying the launch-critical BM features before/after deploy.
**Last updated**: 2026-06-28
**Scope**: All Wave 0-4 deliverables from `docs/plans/aside-followup-executable-plan-2026-06-27.md`.

## Pre-flight

```bash
# Required env vars (relay side)
export LEMONSQUEEZY_WEBHOOK_SECRET="$(openssl rand -hex 32)"
export MAHO_RELAY_SERVICE_TOKEN="$(openssl rand -hex 32)"
export MAHO_PROXY_URL="http://localhost:8787"        # local proxy for testing
export POSTMARK_API_TOKEN="<token>"                  # or RESEND_API_KEY

# Required env vars (billing-proxy side, via wrangler.toml or wrangler dev --var)
# RELAY_URL=http://localhost:8080
# SERVICE_TOKEN=<same as MAHO_RELAY_SERVICE_TOKEN above>
# OPENROUTER_API_KEY=<master key>
# LEMONSQUEEZY_API_KEY=<api key>
```

> **Critical**: If `MAHO_RELAY_SERVICE_TOKEN` is empty/missing, relay refuses to start. This is by design (C8 fix).

---

## Section A - Backend (relay + billing-proxy)

### A1. Relay starts cleanly
```bash
cd maho/relay
cargo run --bin maho-relay -- --port 8080 --db-path /tmp/maho-relay-test.db --open-registration
```
- ✓ Expect: log `starting maho-relay port=8080 ...`
- ✗ If `FATAL: MAHO_RELAY_SERVICE_TOKEN env var must be set...` → set the env var as above

### A2. Billing-proxy starts cleanly (wrangler dev)
```bash
cd maho/billing-proxy
wrangler dev --port 8787 --var RELAY_URL:http://localhost:8080 --var SERVICE_TOKEN:$MAHO_RELAY_SERVICE_TOKEN --var OPENROUTER_API_KEY:$OPENROUTER_API_KEY
```
- ✓ Expect: `Ready on http://localhost:8787`
- ✗ If wasm32 target missing: `rustup target add wasm32-unknown-unknown`

### A3. Health check
```bash
curl http://localhost:8787/health      # → ok
curl http://localhost:8080/health      # → ok
```

### A4. Signup + introspect (free tier)
```bash
TOKEN=$(curl -s -X POST http://localhost:8080/auth/signup \
  -H "Content-Type: application/json" \
  -d '{"email":"alice@test.com","password":"correct horse battery staple","display_name":"Alice"}' \
  | jq -r .access_token)

curl -s -X POST http://localhost:8080/auth/introspect \
  -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" \
  -d "{\"token\":\"$TOKEN\"}" | jq .
```
- ✓ Expect: `{ user_id, tier: "free", subscription_status: "none", email_verified: false, period_start_at: null }`

### A5. Email verification + bypass token
```bash
curl -s -X POST http://localhost:8080/auth/request_verify \
  -H "Authorization: Bearer $TOKEN" | jq .
# Dev mode: dev_token is in the response (only in cfg!(debug_assertions))
# Production: dev_token is None; rely on email delivery
```
Then verify:
```bash
curl -s -X POST http://localhost:8080/auth/verify \
  -H "Content-Type: application/json" \
  -d "{\"token\":\"<verification-token>\"}" | jq .
# → { status: "success", verify_bypass_token: "<base64>" }
```

### A6. Bypass token single-use enforcement (H1)
```bash
# First use succeeds
curl -s -X POST http://localhost:8080/auth/consume_bypass \
  -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" \
  -d "{\"token\":\"<bypass-token>\"}" -i | head -1
# → HTTP/1.1 200 OK

# Second use → conflict
curl -s -X POST http://localhost:8080/auth/consume_bypass \
  -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" \
  -d "{\"token\":\"<same-bypass-token>\"}" -i | head -1
# → HTTP/1.1 409 CONFLICT
```

### A7. Admin force email verify (R-8)
```bash
curl -s -X POST http://localhost:8080/auth/admin/force_verify_email \
  -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" \
  -d "{\"user_id\":1,\"reason\":\"support ticket #1234, postmark bounced\"}" \
  -i | head -1
# → HTTP/1.1 200 OK
# Relay logs `WARN: ADMIN: force-verified email for user user_id=1 reason="..."`
```

### A8. LemonSqueezy webhook → tier set + downgrade on cancel (C-H5)

**Variant ID map (post pricing v2):**

| Variant | Product | Price | Type |
|---------|---------|-------|------|
| 1001 | Pro subscription | $5/mo | subscription_* |
| 2002 | Max subscription | $20/mo | subscription_* |
| 4001 | PAYG credit pack | $10 one-time | order_created |
| 4002 | PAYG credit pack | $50 one-time | order_created |
| 4003 | PAYG credit pack | $100 one-time | order_created |

Simulate Pro subscription (variant 1001):
```bash
SECRET="$LEMONSQUEEZY_WEBHOOK_SECRET"
PAYLOAD='{"meta":{"event_name":"subscription_created","custom_data":{"user_id":"1"}},
  "data":{"id":"evt-1","type":"subscriptions","attributes":{"variant_id":1001,"status":"active","created_at":"2026-06-28T12:00:00Z","renews_at":"2026-07-28T12:00:00Z"}}}'
SIG=$(echo -n "$PAYLOAD" | openssl dgst -sha256 -hmac "$SECRET" | awk '{print $2}')

curl -s -X POST http://localhost:8080/billing/lemonsqueezy \
  -H "X-Signature: $SIG" -H "Content-Type: application/json" \
  -d "$PAYLOAD" -i | head -1
# → HTTP/1.1 200 OK

# Re-introspect → tier=pro
curl -s -X POST http://localhost:8080/auth/introspect -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" -d "{\"token\":\"$TOKEN\"}" | jq .tier
# → "pro"

# Now simulate cancellation
# (rebuild PAYLOAD with event_name=subscription_cancelled, recompute SIG, POST again)
# → user should downgrade to "free"
```

Simulate `subscription_payment_recovered` (user paid after failed renewal, tier restored):
```bash
PAYLOAD='{"meta":{"event_name":"subscription_payment_recovered","custom_data":{"user_id":"1"}},
  "data":{"id":"evt-rec-1","type":"subscriptions","attributes":{"variant_id":1001,"status":"active","created_at":"2026-06-28T12:30:00Z","renews_at":"2026-07-28T12:00:00Z"}}}'
SIG=$(echo -n "$PAYLOAD" | openssl dgst -sha256 -hmac "$SECRET" | awk '{print $2}')

curl -s -X POST http://localhost:8080/billing/lemonsqueezy \
  -H "X-Signature: $SIG" -H "Content-Type: application/json" \
  -d "$PAYLOAD" -i | head -1
# → HTTP/1.1 200 OK
# → tier restored to "pro", period_start_at reset
```

Simulate PAYG credit pack purchase (`order_created` with variant 4002, $50 pack):
```bash
PAYLOAD='{"meta":{"event_name":"order_created","custom_data":{"user_id":"1"}},
  "data":{"id":"evt-order-50","type":"orders","attributes":{"first_order_item":{"variant_id":4002},"total":5000,"created_at":"2026-06-28T13:00:00Z"}}}'
SIG=$(echo -n "$PAYLOAD" | openssl dgst -sha256 -hmac "$SECRET" | awk '{print $2}')

curl -s -X POST http://localhost:8080/billing/lemonsqueezy \
  -H "X-Signature: $SIG" -H "Content-Type: application/json" \
  -d "$PAYLOAD" -i | head -1
# → HTTP/1.1 200 OK
# → credit_balance_usd += 50.00 in user_credits row
```

Repeat for each of the 8 events:
- `order_created` (PAYG credit pack: variants 4001/4002/4003)
- `subscription_created` / `subscription_updated` (1001 or 2002)
- `subscription_cancelled` / `subscription_expired` / `subscription_payment_failed` → free
- `subscription_resumed` / `subscription_payment_recovered` → restore tier

### A9. Degradation modes (R-10)
Stop the relay. From billing-proxy:
```bash
# Without bypass + paid tier user → 503
curl -s -X POST http://localhost:8787/v1/chat/completions \
  -H "Authorization: Bearer <pro-token>" \
  -d '{"model":"openai/gpt-4o","messages":[{"role":"user","content":"hi"}]}' \
  -i | head -3
# → HTTP/1.1 503 Service Unavailable
# → Retry-After: 30

# With bypass token → free-tier minimum, X-Maho-Degradation-Mode: bypass-override

# With free-tier cached entry → X-Maho-Degradation-Mode: free-only
```

### A10. Quota ceiling enforcement (Q-E 3-layer)
- Send requests until cost_usd_accumulated >= base_ceiling
- ✓ Expect: 429 on next request with body `{ "error": "Quota exceeded" }`
- Send a `subscription_payment_recovered` event for the user, OR purchase a PAYG credit pack (variant 4001/4002/4003) → next request succeeds; PAYG path returns `X-Maho-Source: payg`

### A11. Webhook replay rejection (R-3)
Send `subscription_updated` with a `created_at` older than the user's current `period_start_at`:
- ✓ Expect: relay logs `Rejecting out-of-order webhook update for user N`
- User record `period_start_at` unchanged.

### A12. PAYG credits flow (ADR 0010)

Tests the pay-as-you-go credit pack lifecycle: purchase, balance introspect, consume after subscription exhausted, admin override.

**A12a. Buy a $50 credit pack via webhook:**
```bash
SECRET="$LEMONSQUEEZY_WEBHOOK_SECRET"
PAYLOAD='{"meta":{"event_name":"order_created","custom_data":{"user_id":"1"}},
  "data":{"id":"evt-order-payg-50","type":"orders","attributes":{"first_order_item":{"variant_id":4002},"total":5000,"created_at":"2026-06-28T14:00:00Z"}}}'
SIG=$(echo -n "$PAYLOAD" | openssl dgst -sha256 -hmac "$SECRET" | awk '{print $2}')

curl -s -X POST http://localhost:8080/billing/lemonsqueezy \
  -H "X-Signature: $SIG" -H "Content-Type: application/json" \
  -d "$PAYLOAD" -i | head -1
# → HTTP/1.1 200 OK
# Relay creates or updates user_credits row: balance_usd += 50.00
```

**A12b. Introspect credit balance:**
```bash
curl -s -X GET http://localhost:8080/billing/credit_balance \
  -H "Authorization: Bearer $TOKEN" | jq .
# → {"balance_usd": 50.0, "last_updated_at": "2026-06-28T14:00:01Z"}
```

**A12c. Consume PAYG when subscription is exhausted:**
```bash
# Setup: free or pro user whose monthly ceiling is already drained.
# Send a chat request through billing-proxy:
curl -s -X POST http://localhost:8787/v1/chat/completions \
  -H "Authorization: Bearer $TOKEN" \
  -d '{"model":"openai/gpt-4o-mini","messages":[{"role":"user","content":"hi"}]}' \
  -i | head -10

# ✓ Expect: HTTP/1.1 200 OK
# ✓ Header: X-Maho-Source: payg
# ✓ Header X-Maho-Degradation-Mode is ABSENT (this is a normal path, not a degradation)
# ✓ user_credits.balance_usd decreases by the request cost (markup applied)
```

If `X-Maho-Source` header is not yet wired in billing-proxy, document the discrepancy in the launch checklist and treat as a P0 blocker before D-day.

**A12d. Admin override credit balance:**
```bash
curl -s -X POST http://localhost:8080/auth/admin/set_credit_balance \
  -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"user_id":1,"balance_usd":25.00,"reason":"support refund #4567"}' \
  -i | head -1
# → HTTP/1.1 200 OK
# Relay logs `WARN: ADMIN: set credit_balance user_id=1 from=50.00 to=25.00 reason="..."`

# Re-introspect to confirm:
curl -s -X GET http://localhost:8080/billing/credit_balance -H "Authorization: Bearer $TOKEN" | jq .
# → {"balance_usd": 25.0, ...}
```

---

## Section B - Browser (Chromium)

### B1. Build + launch
```bash
python3 maho-chromium/build/scripts/build_maho.py
open chromium/src/out/Default/Maho.app
```

### B2. Onboarding + sign-up
- ✓ Onboard flow lands on welcome screen
- Open browser settings → "Sign in / Create account"
- Sign up with `bob@test.com`
- ✓ Verification email sent (or dev_token in console log)
- Click verify link → email_verified=true

### B3. Tab Tidy (T3.1)
- Open 10+ tabs of mixed content (github, news, docs, social)
- Click the **wand-2** icon in the sidebar topbar (Lucide wand-sparkles native; wand-2 React)
- ✓ Modal opens with 2-4 folder suggestions (each with checkboxes, all checked by default)
- Click **Apply**
- ✓ Tabs grouped into folders matching the suggestions
- ✓ No A/B prompt, no settings toggle anywhere

### B4. Routines (T3.2)
- Visit `chrome://maho-routines`
- ✓ If tier=Free or Pro: see "Available on Max tier" upgrade card
- Upgrade user to Max tier via webhook (variant 2002)
- Reload `chrome://maho-routines`
- ✓ See 4 recipe cards: Morning Briefing, Close Old Tabs, Summarize Reading List, Weekly Tab Tidy
- Click **Run Now** on any recipe
- ✓ Inline result shows latest run output
- Wait for next cron tick (e.g. set system clock close to the schedule, or use the test "tick now" affordance if added)
- ✓ Scheduled run completes automatically

### B5. AI panel via Maho-managed
- Open AI panel from sidebar
- Send "Hello, summarize this page"
- ✓ Streaming response appears (no API-key dialog)
- ✓ For Free tier: quota counter visible somewhere; after ~$0.50 of usage, get inline upgrade prompt
- ✓ For Pro/Max: response uses appropriate model (e.g. claude-3-haiku, gpt-4o)
- ✓ PAYG mode lights up when the subscription monthly budget hits zero: the inline prompt shifts to "Buy credits" and the modal offers the $10/$50/$100 credit packs. If the user already holds PAYG credit balance, requests continue silently with `X-Maho-Source: payg` and no UI interruption.

### B6. BYOK fallback
- Settings → AI → set custom API key (OpenAI, Anthropic, etc.)
- ✓ Chat goes direct-to-provider (verify by checking billing-proxy logs - no request)
- Remove API key
- ✓ Chat reverts to Maho-managed proxy

### B7. Sidebar Tab Tidy native dialog
- Sidebar top-bar wand icon (Lucide `wand_sparkles` native) → click
- ✓ Native dialog opens (not React modal) with same folder suggestions
- Both native + React paths reach the same llm_manager backend

---

## Section C - CLI (maho-cli)

### C1. Install + login
```bash
cd maho
cargo install --path crates/maho-cli
maho --relay-url http://localhost:8080 --proxy-url http://localhost:8787/v1 \
  signup --email charlie@test.com --password "correct horse battery staple"
# OR: maho login --signup --email ... --password ...
# OR interactive: maho login

maho whoami
# → Email: charlie@test.com
# → Tier: free
```

### C2. Chat
```bash
maho chat "Hello, what is 2+2?"
# → streaming response via billing-proxy
```

### C3. BYOK shortcut
```bash
maho chat --byok openai sk-... "test"
# → direct OpenAI call, NOT through proxy
```

### C4. Logout
```bash
maho logout
maho whoami
# → "Not logged in" error
```

### C5. Config
```bash
maho config show
# → email, tier, redacted token
```

### C6. Credit balance
```bash
maho balance
# → Subscription: Pro ($5/mo), period budget remaining: $3.20
# → PAYG credits: $50.00
# → Total available: $53.20
```

### C7. Buy credits
```bash
maho buy-credits --amount 10
# → Opens LemonSqueezy checkout in default browser for variant 4001 ($10 pack)
# → After payment, run `maho balance` again to confirm new balance
```

---

## Section D - Marketing pages

### D1. Pricing page
- Visit `https://maho.app/pricing` (or local `bun run dev` on port 5555)
- ✓ 3 tiers: Free $0 / Pro $5/mo / Max $20/mo
- ✓ Credit pack section (formerly "Top-ups") shows $10, $50, $100
- ✓ BYOK section explains "free + use your own key"
- ✓ Frontier-model exclusion language is present (no Opus / GPT-4 / o1-style flagships on subscription tiers; available via BYOK or PAYG with appropriate caps)
- ✓ Click "Subscribe Now" → LemonSqueezy checkout opens

### D2. Privacy + terms
- `https://maho.app/privacy` - mentions zero-retention, OpenRouter sub-processor, GDPR erasure
- `https://maho.app/terms` - mentions credit pack purchase terms (non-refundable past 14-day window, doesn't roll over)

### D3. Comparison page
- `https://maho.app/compare/aside`
- ✓ No iOS/Android/cross-platform parity claims
- ✓ "Last updated: 2026-06-28" header
- ✓ BYOK row: Aside = "Available as alternative to bundled plan", Maho = "✓ Free, unlimited"
- ✓ Routines row: Maho = "Not at launch" (HONEST)

---

## Section E - End-to-end revenue path

### E1. Free → Pro → cancel → free
1. Sign up new user (tier=free)
2. Use AI panel ~$0.40 worth (works fine)
3. Send 5 more requests of $0.05 each → 6th request returns 429 with upgrade prompt
4. User clicks "Upgrade to Pro" → LemonSqueezy checkout (test mode, variant 1001 / $5)
5. Complete payment → webhook `subscription_created` → tier=pro
6. Reload: quota reset, ceiling raised to the Pro monthly budget
7. Use a few dollars worth: requests succeed
8. Cancel subscription in LemonSqueezy dashboard → webhook `subscription_cancelled` → tier=free
9. Reload: back to $0.50 ceiling, paid models gated

### E2. Pro user buys a credit pack
1. Pro user near the end of monthly budget
2. Purchase $50 credit pack (variant 4002) via LemonSqueezy
3. Webhook `order_created` → relay `user_credits.balance_usd += 50.00`
4. ✓ Continue using; once Pro budget is drained, requests fall back to PAYG and decrement `balance_usd`. Header `X-Maho-Source: payg` set.
5. Roll into next billing period: Pro budget resets, PAYG `balance_usd` does NOT reset (credit packs are persistent until consumed).

### E3. Email delivery failure recovery (R-8)
1. New signup with email that bounces (Postmark/Resend dashboard shows bounce)
2. User cannot verify → cannot upgrade flow blocks
3. Support admin: `curl /auth/admin/force_verify_email -d '{"user_id":N,"reason":"bounce"}'`
4. ✓ User can now use AI panel

### E4. PAYG-only user (never subscribes)

End-to-end check that a user can live entirely on PAYG credits without ever buying a subscription.

```bash
# 1. Sign up
TOKEN=$(curl -s -X POST http://localhost:8080/auth/signup \
  -H "Content-Type: application/json" \
  -d '{"email":"payg@test.com","password":"correct horse battery staple","display_name":"PaygOnly"}' \
  | jq -r .access_token)

# 2. Verify tier is free, no subscription
curl -s -X POST http://localhost:8080/auth/introspect \
  -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" \
  -d "{\"token\":\"$TOKEN\"}" | jq '{tier, subscription_status}'
# → {"tier": "free", "subscription_status": "none"}

# 3. Drain the free $0.50 ceiling, observe 429
for i in $(seq 1 12); do
  curl -s -X POST http://localhost:8787/v1/chat/completions \
    -H "Authorization: Bearer $TOKEN" \
    -d '{"model":"openai/gpt-4o-mini","messages":[{"role":"user","content":"hi"}]}' \
    -o /dev/null -w "%{http_code}\n"
done | tail -3
# → expect 200 ... 200 ... 429

# 4. Buy a $10 credit pack (variant 4001)
SECRET="$LEMONSQUEEZY_WEBHOOK_SECRET"
USER_ID=$(curl -s -X POST http://localhost:8080/auth/introspect \
  -H "Authorization: Bearer $MAHO_RELAY_SERVICE_TOKEN" \
  -d "{\"token\":\"$TOKEN\"}" | jq -r .user_id)
PAYLOAD="{\"meta\":{\"event_name\":\"order_created\",\"custom_data\":{\"user_id\":\"$USER_ID\"}},\"data\":{\"id\":\"evt-payg-e4\",\"type\":\"orders\",\"attributes\":{\"first_order_item\":{\"variant_id\":4001},\"total\":1000,\"created_at\":\"2026-06-28T15:00:00Z\"}}}"
SIG=$(echo -n "$PAYLOAD" | openssl dgst -sha256 -hmac "$SECRET" | awk '{print $2}')
curl -s -X POST http://localhost:8080/billing/lemonsqueezy \
  -H "X-Signature: $SIG" -H "Content-Type: application/json" \
  -d "$PAYLOAD" -i | head -1
# → HTTP/1.1 200 OK

# 5. Confirm balance
curl -s -X GET http://localhost:8080/billing/credit_balance -H "Authorization: Bearer $TOKEN" | jq .
# → {"balance_usd": 10.0, ...}

# 6. Continue chatting; requests should succeed with X-Maho-Source: payg
curl -s -X POST http://localhost:8787/v1/chat/completions \
  -H "Authorization: Bearer $TOKEN" \
  -d '{"model":"openai/gpt-4o-mini","messages":[{"role":"user","content":"hello again"}]}' \
  -i | grep -i 'X-Maho-Source\|HTTP/'
# → HTTP/1.1 200 OK
# → X-Maho-Source: payg

# 7. Wait one billing period (or manually advance period_start_at); confirm credit balance is unchanged
# Credit packs do NOT expire and do NOT reset on period rollover (ADR 0010).
```

Pass criteria:
- ✓ User never enters paid subscription state (`subscription_status` stays `none`, `tier` stays `free`).
- ✓ After buying the $10 pack, chat resumes immediately with `X-Maho-Source: payg`.
- ✓ Credit balance persists across at least one period rollover.

---

## Section F - Observability + telemetry

### F1. Logs to check
```bash
# Relay
grep -i 'admin\|fatal\|warn\|reject' /var/log/maho-relay.log | tail -50

# Billing-proxy (wrangler tail)
wrangler tail --format pretty --status error

# OpenRouter usage
# Visit OpenRouter dashboard, confirm usage growing per active subs
```

### F2. Manual monthly health (Plan §6)
- Pro active subscriptions count
- Monthly OpenRouter spend vs MRR
- Churn rate
- Free → Pro conversion rate

> Plan §6 Q-5 = NO auto kill-switch. Monthly review is manual.

---

## Section G - Pass criteria (Launch Gate)

All 12 below must PASS:

1. ☐ `cd maho && bun run typecheck` (cargo check --workspace)
2. ☐ `cd maho && bun run test` (cargo test --workspace)
3. ☐ `cd maho/relay && cargo test --workspace -- --test-threads=1 && cargo clippy --workspace -- -D warnings`
4. ☐ `cd maho/billing-proxy && cargo check && cargo test`
5. ☐ `cd website && bun run build`
6. ☐ `python3 maho-chromium/build/scripts/build_maho.py` (full link, no `--skip-rust`)
7. ☐ `cargo test --workspace --test revenue_e2e` passes (sub + PAYG + proxy fallback)
8. ☐ `cd maho && rg "maho-acp|tab_grouping|dev_token: Some|maho-service-token-secret-local-development" --type rust --type md` returns 0 hits in our code. Separately, `rg -e 'fifteen-per-month|ninety-nine-per-month|two-hundred-per-month' -F docs/` (placeholder strings; replace with the v1 literal prices when running the gate locally) must return 0 hits in scope files. Intentional references inside ADR 0001a and the locked-decisions plan are acceptable.
9. ☐ ADR 0009 v2 line 5 reads "Status: Accepted" (subscription tier prices + markup)
10. ☐ ADR 0010 line 5 reads "Status: Accepted" (PAYG credits)
11. ☐ Visit `https://maho.app/pricing`: Pro $5/mo, Max $20/mo, credit packs $10/$50/$100 all present; frontier-exclusion language present
12. ☐ Section A12 (PAYG flow) and Section E4 (PAYG-only user) both pass end-to-end against the staging relay+proxy

---

## Appendix - Quick smoke test (5 minutes)

For day-to-day "is launch still green?" check:

```bash
# 1. Backend tests
cd maho && cargo test --workspace 2>&1 | tail -5
cd maho/relay && cargo test --workspace -- --test-threads=1 2>&1 | tail -5
cd maho/billing-proxy && cargo test 2>&1 | tail -5

# 2. Website
cd website && bun run build 2>&1 | tail -3

# 3. Lock re-check
cd /Users/indo/code/project/maho-workspace
grep -rn "partially superseded" docs/decisions/ && echo "VIOLATION" || echo "OK"
grep -rn "maho-service-token-secret-local-development" maho/relay/server/src/ maho/billing-proxy/src/ && echo "VIOLATION" || echo "OK"
grep -c "subscription_payment_recovered" maho/relay/server/src/billing.rs
# Should print >= 1
```

If all green → launch gate intact.
