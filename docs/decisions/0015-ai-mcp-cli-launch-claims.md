# ADR 0015: Launch-Claim Policy for AI, Mobile Chat, Tools-Only MCP/CLI GA, and Automation Parity

- **Status:** Accepted (Active)
- **Date:** 2026-07-22
- **Supersedes:** `docs/demos/README.md` lines 79-80 (historical ban on iOS/Android, Routines, MCP, and CLI launch claims for in-scope surfaces only)

## Context

Prior launch policy (`docs/demos/README.md:79-80`) placed a hard ban on claims regarding iOS/Android mobile shells, Routines, MCP host, and CLI. 
With the execution of `.omo/plans/launch-ai-mcp-cli-readiness.md`, Maho productizes and launches specific readiness surfaces across desktop AI, mobile chat, and the developer CLI/MCP. Automation parity is only partially built (see the Implementation Status table below) and is NOT a fully launched surface.

## Decision

The launch-claim policy is updated to permit active claims for the following **in-scope launch surfaces**:

1. **Desktop AI (`chrome://maho-ai`):**
   - Managed AI (Free/Pro/Max/PAYG) with safe free model defaults and metered relay billing proxy, alongside BYOK fallback.
2. **Mobile Chat (`#chat`, `#conversations`):**
   - Mobile chat experience and conversation history on Android and iOS web-ai bundles.
3. **Tools-Only MCP/CLI GA:**
   - Unified developer command surface under a single `maho` CLI binary (with `maho mail` subcommand).
   - Stdio, UDS, and HTTP tools-only MCP server capabilities (`tools: {}` advertised).
4. **Automation Parity & Routines (SCOPED — see status below):**
   - Routine result inbox (durable run-result ledger) is implemented at the
     `maho-core` / `maho-ffi` / `maho-storage` layer and is queryable via FFI.
     UI/CLI/MCP surfacing of run history is deferred (see status).
   - Notification and inbox/mail heartbeat triggers exist as typed, tested
     trigger parsers with deferred runtime emitters (no browser/mail adapter
     emits them yet).
   - Bounded deep research mode exists as a Max/PAYG-gated, budget-capped
     multi-step agent loop in `maho-agent`, reachable from the agent API. (The
     `maho research` BYOK CLI entry point was removed on 2026-08-28 together
     with the external CLI agent path.) It emits citations/evidence and cannot
     run unbounded. It is NOT a frontier managed model and NOT a cloud-executed
     Ultrabrowse (see status).
   - Resumable cloud handoff exists as **durable local/staging job STATE**
     (create/poll/complete/fail with bill-once idempotency) in the relay.
     **Remote Worker EXECUTION of the job body is DEFERRED** (see status).

## Automation Parity — Implementation Status (authoritative)

This section is the source of truth for what may be claimed. It supersedes any
prior evidence asserting that all four parity surfaces are complete.

| Parity surface (plan tasks 15-18) | Status | Claimable at launch? |
|---|---|---|
| 15. Routine result inbox | Partial: durable ledger + core/FFI/storage query implemented and tested; WebUI/CLI/MCP history surfacing deferred (needs C++ bridge + Mojo, not built this pass) | Only "routines persist their results" — NOT "results inbox in every surface" |
| 16. Notification + inbox/mail heartbeat triggers | Partial: typed `OnNotification` / `OnInboxHeartbeat` triggers parse/match/fire and are reachable via `maho_routines_fire_event`; no browser/mail emitter wired | NO — deferred runtime wiring, do not claim as a shipped trigger |
| 17. Bounded deep research mode | Implemented (minimal): bounded loop with hard max-steps/wall-clock/cost caps, Max/PAYG entitlement gate, managed frontier-model gate, citations/evidence output; reachable via agent API (CLI entry removed 2026-08-28); 12 tests | Only "bounded deep research mode (Max/PAYG) with step/cost/time caps and citations" — NOT "frontier managed research" or "cloud Ultrabrowse" |
| 18. Resumable cloud handoff jobs | Partial: durable job STATE (D1 `handoff_jobs` table + service-token create/poll/complete/fail routes) with bill-once idempotency and reconnect/poll resumability; 10 state-machine tests. **Remote Worker EXECUTION is DEFERRED** — no Worker runs the job body; a local/CLI producer drives work and reports completion | Only "resumable managed job STATE with bill-once accounting" — NOT "cloud-executed managed jobs" |

Do NOT claim "Aside automation parity" as a fully completed launch surface.
Deep research is a bounded local/agent-driven mode (not a managed frontier or
cloud-executed service); cloud handoff is durable job STATE only, with remote
execution deferred. Routine inbox and the extra triggers remain partial with
deferred UI/runtime wiring.

### Retention (task 18)

The `handoff_jobs` table stores METADATA ONLY: job id, user id, job type,
status, timestamps, step count, cost/billing markers, an opaque `result_ref`,
and an error string. It stores NO prompt or response text. `result_ref` is an
opaque pointer to a user-visible / client-held (E2EE) artifact, never inline
content. Rows are subject to the relay's standard retention/pruning policy.



## Hard Scope Guardrails (Strict Enforcement)

1. **Tools-Only MCP GA:**
   - Do NOT claim support for MCP `resources` or `prompts`. ADR 0014 remains in full force.
2. **Scoped Mobile Claims:**
   - Claims regarding mobile are strictly limited to **mobile chat and conversation list**. Do NOT claim full mobile browser parity or all-screen mobile AI.
3. **Password Manager Security Gate:**
   - Do NOT claim secure agent password-vault behavior or agent password autofill. `docs/password-manager-security-fix-plan.md` remains the active blocker.
4. **Closed-Source / Proprietary Licensing:**
   - Do NOT use language such as "open source", "auditable source", "view on GitHub for source code", or link to public source repos for Maho.
