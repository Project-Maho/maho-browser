# ADR 0016: Agent Browser Action Safety Contract

- **Status:** Accepted, 2026-07-25
- **Supersedes:** none

## Context

Maho's browser MCP path already implements navigation and input tools with lease
checks, allowed-domain checks, and secret-redacting handlers. The in-browser
agent is catching up to that surface, but action tools need one shared safety
contract before schemas or dispatch are exposed.

## Decision

1. **Tab leases:** the in-browser agent acquires an active-tab lease like any
   other client. It must never use `force_steal`; an external holder produces a
   clear user-visible conflict error. `browser_hover` intentionally mirrors MCP:
   it is sensitive and approval-required, but it is not lease-required.
2. **Allowed domains:** the agent's effective allowed set is the active tab's
   origin, resolved per turn. An empty allowed-domain set fails closed; this is
   the current MCP behavior and is not a divergence.
3. **Navigation injection boundary:** `browser_navigate` may stay on the active
   origin. Leaving that origin requires a fresh approval naming the destination
   origin. Page-derived text remains untrusted prompt input and must be labelled
   separately from user instructions before it can justify an action.
4. **Canonical metadata:**
   `maho-chromium/browser/ai/maho_browser_action_contract.def` is the checked-in
   source of truth for browser action/read tool metadata. C++ consumes it via
   `maho_browser_action_contract.h`; Rust consumes the same file with
   `include_str!` and fails closed on unsafe or unknown policy tokens.
5. **Kill switch:** `maho::features::kMahoAgentBrowserActions` gates in-browser
   action schema advertisement and action dispatch. It is disabled by default.

## Superseded/Amended (2026-08-16)

Tab-lease acquisition now force-steals at both the CLI/MCP socket and the
in-browser executor call sites. The CLI and agent are user-driven sessions, so
a contending lease must not block the requested browser action.

Scope decision (2026-08-16, explicit owner call): `HandleAcquireLease` applies
force-steal unconditionally to EVERY MCP session, including untrusted
third-party MCP clients — not only the user-driven CLI. The acquisition is not
gated on controller classification. This was chosen deliberately over gating
force-steal to trusted/CLI controllers.

Accepted consequences:

- Automation can act on a tab while a human is entering input; the registry has
  no human-activity signal.
- CLI/MCP and in-browser sessions can evict one another and ping-pong on the
  same tab.
- Any connected MCP client (including third-party) can evict the current lease
  holder of a tab.
- There is no fencing token, so an evicted holder's in-flight action can still
  complete.

## Canonical contract fields

| Field | Meaning |
| --- | --- |
| Tool name | MCP-compatible browser tool name consumed by the action surface. |
| Kind | `Read` or `Action`. |
| Sensitivity | `Low` for supporting reads, `Sensitive` for input/navigation actions. |
| Approval | Whether a per-action user approval is required before dispatch. |
| Lease | Whether an active-tab lease is required before dispatch. |
| Domain policy | Active-tab-origin scoping, with destination-origin approval for navigation. |
| Empty allowlist | Always `FailClosed`; empty never means all sites. |

## Consequences

- Later C++ registry/executor and Rust shim work must bind to the same `.def`
  table instead of hand-maintaining duplicate lists.
- `browser_click`, `browser_type`, `browser_select`, `browser_scroll`,
  `browser_key_press`, and `browser_navigate` are sensitive, approval-required,
  and lease-required.
- `browser_hover` is sensitive and approval-required but not lease-required by
  design, matching MCP's current lease boundary.
- No `vault_*`, third-party/domain API, or `evaluate_js` tool is authorized by
  this ADR.
