# Revenue E2E Staging Procedure

Manual staging verification for billing-proxy quota/ceiling enforcement paths that require a running Cloudflare Worker environment.

## Prerequisites

- `wrangler` CLI installed and authenticated
- OpenRouter test API key with billing enabled
- Staging relay server running (or local with `RELAY_URL=http://localhost:8080`)

## Steps

1. **Start billing-proxy locally:**
   ```bash
   cd maho/billing-proxy && wrangler dev
   ```
   Worker starts on `http://localhost:8787`.

2. **Set environment:** Configure `RELAY_URL` to point at the staging relay, `SERVICE_TOKEN` matching relay's `MAHO_RELAY_SERVICE_TOKEN`, and a valid `OPENROUTER_API_KEY`.

3. **Create a test user** on the relay, upgrade to Pro via the `/billing/lemonsqueezy` webhook endpoint (use the same payload shape as `revenue_e2e.rs`).

4. **Send a chat completion** through the proxy (`POST /v1/chat/completions` with Bearer token). Verify the response returns model output and the DO state shows incremented `cost_usd_accumulated`.

5. **Inspect Durable Object state** via Wrangler DO inspector:
   ```bash
   wrangler d1 execute --command "SELECT * FROM __DO_UserUsage"
   ```
   Confirm `base_ceiling_usd=20.0` for Pro.

6. **Exhaust ceiling:** Repeatedly send completions (or manually call `/reserve` via service token) until `cost_usd_accumulated >= base_ceiling_usd + topup_ceiling_usd`.

7. **Verify 429:** Next completion request must return HTTP 429 "Billing quota exceeded for the current period".

## Notes

- Top-up can be simulated by calling `POST /billing/topup` with service token auth.
- Period reset can be tested by advancing DO alarm or setting `next_reset_at` in the past.

## Relay Degradation Modes

When the relay is unreachable from the billing-proxy, the proxy responds with one of three distinct modes instead of the former indiscriminate 401:

### (a) `deny-paid` — `503 Service Unavailable` + `Retry-After: 30`

Triggered when: relay fetch fails AND there is no bypass token AND no free-tier cache entry for the user.

**To test:** Stop the relay, send a request with a valid user token that has no KV cache entry (or wait for TTL to expire). Expect:
- HTTP 503
- `Retry-After: 30` response header
- Body: `{ "error": "Relay temporarily unreachable. Try again in 30s.", "mode": "deny-paid" }`

### (b) `free-only` — 200 with degraded tier + `X-Maho-Degradation-Mode: free-only`

Triggered when: relay fetch fails AND KV cache contains a valid entry for the user with `tier="free"`.

**To test:** Stop the relay, send a request where the user was previously active and their KV cache entry (60s TTL) is still valid with `tier="free"`. Expect:
- HTTP 200 (request proceeds as free tier)
- `X-Maho-Degradation-Mode: free-only` response header
- Request treated as free tier regardless of any paid status

### (c) `bypass-override` — 200 with forced free baseline + `X-Maho-Degradation-Mode: bypass-override`

Triggered when: relay fetch fails BUT a valid `X-Maho-Verify-Bypass` header token was successfully consumed from the relay (or the bypass was validated before relay failure).

**To test:** Obtain a valid bypass token (from `/auth/verify` response), stop the relay after the consume_bypass call succeeds. Expect:
- HTTP 200 (request proceeds with free-tier minimum)
- `X-Maho-Degradation-Mode: bypass-override` response header

**Note:** Full runtime behavior for all three modes requires a running Cloudflare Worker environment. Unit validation of the mode string constants happens via `cargo test` in `billing-proxy/src/lib.rs::tests::test_degradation_mode_strings`. End-to-end coverage of the client↔relay contract (in-process mock relay + mock proxy, no real Cloudflare Worker) is in `maho/crates/maho-cli/tests/revenue_e2e.rs`.

**Note:** The automated test at `maho/crates/maho-cli/tests/revenue_e2e.rs` SUPPLEMENTS this manual staging procedure; it does NOT replace it. The manual procedure remains the only path to exercise the real wasm32 `maho-billing-proxy` Durable-Object ceiling/reserve-commit logic.
