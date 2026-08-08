# Aside Benchmark Refresh — Implementation Evidence Ledger

**Date:** 2026-07-21
**Purpose:** Ground the six Aside benchmark areas in current repository state. Evidence only — no marketing copy, no recommendations. Supersedes stale claims in older Aside follow-up docs where noted.
**Scope note:** Maho is proprietary/closed-source. Nothing here is an OSS claim or a public-source reference.

## Status taxonomy

- **Shipped-in-browser** — wired into the Chromium build and exercised via the browser surface.
- **Implemented (crate), not launch-committed** — code exists and compiles/tests in the Rust workspace, but launch docs explicitly exclude it from launch claims.
- **Exists but gated/insecure** — code path exists but a current security/doc gap blocks a "secure/shipped" claim.
- **Planned** — documented intent, not yet implemented.
- **Strategy/docs only** — decision/marketing framing, no shippable surface of its own.

---

## 1. Zero-setup AI onboarding

**Status: Shipped-in-browser.**

The Maho-managed AI path lets a signed-in user chat with no API key. On agent turn setup, when an `auth:access_token` is present, `SwiftideBackend` builds an OpenAI-compatible client against the billing proxy rather than requiring BYOK.

- `maho/crates/maho-agent/src/swiftide_backend.rs`: the managed path selects `crate::proxy_url()` and `crate::DEFAULT_MANAGED_MODEL` (`google/gemini-3-flash-lite:free`) when the profile sets no `preferred_model` (lines 984-986); BYOK is the fallback only when no access token exists.
- `maho/crates/maho-agent/src/lib.rs:30`: `DEFAULT_MANAGED_MODEL = "google/gemini-3-flash-lite:free"`. `proxy_url()` defaults to `https://proxy.maho.co/v1` (override via `MAHO_PROXY_URL`).
- `maho/billing-proxy/src/lib.rs:96`: `DEFAULT_MANAGED_MODEL = "google/gemini-3-flash-lite:free"`, the same slug used when a request omits `model` (line 806). The Cloudflare Worker terminates managed requests — auth via relay introspect, tier allowlist + `max_tokens` cap injection, subscription/PAYG check-or-reserve, OpenRouter upstream call.
- Tests: `maho/crates/maho-agent/tests/swiftide_mahomanaged_test.rs` — `test_mahomanaged_path_sends_bearer_token_and_model` (line 132) asserts the proxy receives `Bearer <access_token>` and model `google/gemini-3-flash-lite:free` (lines 181-182); `test_byok_fallback_when_no_access_token`; `test_mahomanaged_stores_messages`.
- Launch commitment: `docs/demos/README.md` lists demo #01 "Zero-Setup AI" as a launch-committed feature (lines 11, 42-43).

**Evidence caveats.** The managed default model is `google/gemini-3-flash-lite:free` (agent const `DEFAULT_MANAGED_MODEL` and billing-proxy `DEFAULT_MANAGED_MODEL`), and it sits inside the `gemini-3`-family free/managed allowlist the billing-proxy enforces. The earlier `gemini-2.5-flash` vs `gemini-3` slug drift between the agent default and the proxy allowlist is resolved: both now use the `gemini-3-flash-lite:free` slug. Marketing copy should still avoid pinning a specific model name (the allowlist can rotate), but this is no longer a code-level mismatch. When citing the default in launch docs, use `google/gemini-3-flash-lite:free`.

**Governing docs:** `docs/decisions/0009-openrouter-subscription-bm.md` (managed tiers, markup, model-family policy), `docs/decisions/0010-payg-credits-model.md` (PAYG as managed-proxy overflow, not BYOK), `maho/billing-proxy/README.md` (local relay+proxy bring-up).

---

## 2. Tab Tidy

**Status: Shipped-in-browser.**

One-click AI grouping of open tabs, wired through Mojo IDL and a page handler, links in the full `chrome` target.

- `docs/operations/build-verification-2026-06-28.md` PART 1 (lines 40-49): `maho_ai.mojom` `RequestTabTidy(array<TabInfo>) => (array<TidyFolder>)` and `ApplyTabTidyFolders(...)` validated; PART 5 (lines 158-163) confirms `RequestTabTidy`/`ApplyTabTidyFolders` implemented with `override`; Conclusion (lines 199-203): full `chrome` links, `Maho.app` produced.
- `docs/operations/manual-test-runbook.md` B3 (lines 269-276) and B7 (lines 302-306): sidebar `wand-2` / `wand_sparkles` icon opens a folder-suggestion dialog (React + native paths reach the same `llm_manager` backend).
- Launch commitment: `docs/demos/README.md` demo #02 "Tab Tidy" (lines 12, 44), called the most shareable shot in the bundle.

**Evidence caveat.** `docs/operations/build-verification-2026-06-28.md` PART 4 (lines 144-150) records a hand-maintained `maho_ai.mojom-webui.js.d.ts` stub missing `TabInfo`/`TidyFolder`; this affects local IDE typecheck only, not the Chromium build.

**Governing docs:** build-verification (above), manual-test-runbook Section B, `docs/demos/README.md`.

---

## 3. Routines

**Status: Implemented (crate), NOT launch-committed.**

Routines exist as CLI subcommands, an MCP tool pair, and a `chrome://maho-routines` WebUI, gated to Max tier — but launch docs explicitly disclaim them.

- CLI: `maho/crates/maho-cli/src/main.rs` — `Routine` subcommand (lines 129-133), `RoutineCommands::{List,Run}` (lines 289-323), dispatch (lines 810-822). Note the `Run` handler currently ignores `provider`/`model`/`yolo`/`allow_tools`/`workspace` (bound to `_`, lines 814-821).
- MCP: `maho/crates/maho-browser-mcp/src/server.rs` — `browser_routines_list` (lines 1107-1118) and `browser_routines_run` (lines 1120-1131), the latter documented as returning a structured "Max tier required" error for tier-locked routines.
- WebUI: `docs/operations/build-verification-2026-06-28.md` (lines 54-59, 122-128) confirms `maho_routines.mojom` bindings + page handler; `docs/operations/manual-test-runbook.md` B4 (lines 277-286) drives `chrome://maho-routines` with a Max-tier upgrade gate.
- CLI README (`maho/crates/maho-cli/README.md`) states routine execution is "a Max-tier exclusive feature."

**Launch-commitment determination (explicit):** Historical note: `docs/demos/README.md` line 80 previously stated "No claims about Routines, MCP host, or CLI. Not shipping at launch." This was superseded by ADR 0015 for tools-only MCP GA, single `maho` CLI, and routine results/triggers.

**Governing docs:** ADR 0015 (launch claim policy), `docs/demos/README.md` (launch-claim scope), CLI README (Max-tier scoping).

---

## 4. Agent password-manager messaging

**Status: Exists but gated/insecure. A fully-secure / shipped "agent vault" claim is NOT supported by current evidence.**

- Core store is plaintext in memory: `maho/crates/maho-core/src/password_manager.rs` keeps a `Vec<SavedPassword>` and stores the raw secret as `password: Option<String>` with no encryption (lines 3-56; add at 38-56).
- Current security docs describe active plaintext/gated gaps: `docs/password-manager-security-fix-plan.md` — F1 "passwords stored to disk in plaintext, SQLCipher key not wired" (lines 15, 28-34, noting `set_sqlcipher_key` has **zero callers**), F2 residual `*.db.plain_backup` (line 16), F3 no OS/biometric re-auth on reveal (lines 17, 111-116). Core principle, line 22: the self-managed password manager's new-save feature must **not** be exposed to users before Milestone 1 ships.
- No agent path to a password vault: the agent tool set built in `maho/crates/maho-agent/src/swiftide_backend.rs` is fs/shell/browser/mail/MCP/CLI tools (platform tools at lines 1080-1091; browser tool shims at 607-661) — there is **no** password-manager tool wired to the agent.

**Explicit statement:** Because current security docs indicate plaintext-at-rest and missing reveal-time authorization gaps (SQLCipher key unwired, plaintext backups, no OS re-auth), password-manager agent messaging cannot claim fully secure or shipped agent-vault behavior. Any such claim would contradict `docs/password-manager-security-fix-plan.md`.

**Governing docs:** `docs/password-manager-security-fix-plan.md` (M1/M2/M3 remediation; DoD lines 200-206).

---

## 5. MCP / CLI developer surface

**Status: Implemented (crate), NOT launch-committed. MCP capability scope is tools-only by decision.**

- MCP server: `maho/crates/maho-browser-mcp/src/server.rs` exposes a broad tools-only surface (tab, history, bookmark, page-extract, accessibility/actions, network, leases, routines, mail — e.g. tools defined across lines 519-1131). It advertises `{tools:{}}` only.
- Tools-only decision: `docs/decisions/0014-oq2-resources-prompts.md` — **Accepted NO-GO for v1** (line 5); MCP `resources`/`prompts` intentionally deferred on both server and host (Decision, lines 112-124; Maho current surface, lines 38-45). The CLI README carries the required "MCP capability scope … tools-only for v1" note citing ADR 0014.
- CLI: `maho/crates/maho-cli/src/main.rs` — `Mcp` subcommand (lines 123-127, 243-287) and `Run` autonomous agent command (lines 138-162, 830-848); `maho tool list/call` passthrough (lines 164-168, 227-241).

**Launch-commitment determination (explicit):** `docs/demos/README.md` line 80 excludes "MCP host, or CLI" from launch claims ("Not shipping at launch"). So both the MCP developer surface and the CLI are implemented but **not** launch-committed.

**Evidence caveat.** CLI README claims "36 available browser tools"; not independently re-counted against `server.rs` here — treat the specific count as unverified.

**Governing docs:** `docs/decisions/0014-oq2-resources-prompts.md` (tools-only scope), `docs/demos/README.md` line 80 (launch exclusion), CLI README (`maho/crates/maho-cli/README.md`) MCP scope note.

---

## 6. Benchmark / demo framing

**Status: Strategy/docs only.**

- Demo bundle: `docs/demos/README.md` defines three launch demos (Zero-Setup AI, Tab Tidy, Tier Upgrade) with a hard-constraints block (lines 77-83): no iOS/Android claims, no Routines/MCP/CLI claims, no competitor naming in voiceover.
- Benchmark posture: `docs/brand/BRAND.md` §5 (line 69) and §7 (line 87) forbid self-congratulatory or fabricated/unverified benchmarks ("Aside의 반면교사"), requiring fact-based, officially-sourced numbers only.

**Historical framing (resolved in the 2026-07 refresh; do not carry forward the old literals):**
- The `docs/demos/README.md` CTA pricing note was updated in the 2026-07 refresh and now reads "Pricing on every CTA card: Free $0/mo (with a $0.50 included AI ceiling), Pro $5/mo, Max $20/mo." This matches current pricing in `docs/decisions/0009-openrouter-subscription-bm.md` (Free $0 / Pro $5 / Max $20; tier table lines 55-63, revision history lines 9-13) and the variant map in `docs/operations/manual-test-runbook.md` (Pro $5 = variant 1001, Max $20 = variant 2002; lines 106-114). Historical context: an earlier draft priced Free at a half-dollar, Pro at twenty dollars, and Max at two hundred dollars; that superseded layout predates the current tiers and must not be reused. The current `$0.50` figure is the Free per-cycle spend ceiling, not a price.
- `docs/operations/manual-test-runbook.md` is titled "Aside Follow-up BM Redesign" (line 1) and reflects current v2 pricing ($0/$5/$20, credit packs $10/$50/$100), so it is the current runbook despite the legacy title. With the demo README CTA note updated, no doc in the checked set carries the retired price literals.

**Governing docs:** `docs/demos/README.md` (demo scope + constraints), `docs/brand/BRAND.md` §5/§7 (voice/benchmark rules), `docs/decisions/0009-openrouter-subscription-bm.md` (current pricing of record).

---

## Cross-area summary

| Area | Status | Primary evidence |
|------|--------|------------------|
| Zero-setup AI onboarding | Shipped-in-browser | `swiftide_backend.rs:981-996`; `billing-proxy/src/lib.rs`; `swiftide_mahomanaged_test.rs` |
| Tab Tidy | Shipped-in-browser | `build-verification-2026-06-28.md` (T3.1 GREEN); `manual-test-runbook.md` B3/B7 |
| Routines | Implemented (crate), not launch-committed | `maho-cli/src/main.rs:289-323`; `browser-mcp/src/server.rs:1107-1131`; `demos/README.md:80` |
| Agent password-manager messaging | Exists but gated/insecure | `password_manager.rs:38-56`; `password-manager-security-fix-plan.md` F1-F3 |
| MCP / CLI developer surface | Implemented (crate), not launch-committed; tools-only | `browser-mcp/src/server.rs`; `decisions/0014-oq2-resources-prompts.md`; `demos/README.md:80` |
| Benchmark / demo framing | Strategy/docs only | `demos/README.md`; `brand/BRAND.md` §5/§7 |
