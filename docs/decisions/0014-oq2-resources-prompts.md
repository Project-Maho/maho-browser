# 14. OQ2 — MCP `resources`/`prompts` Support: Go/No-Go

## Status

**Accepted (NO-GO for v1)**, 2026-07-13.

---

## Context

The MCP+CLI parity implementation dropped MCP `resources` and `prompts`
support without recording a decision. Open Question OQ2 was explicitly raised
by the user as a **gate**: before deciding whether Maho should implement
`resources`/`prompts`, we must first *verify what the Aside browser actually
exposes over MCP*. That verification was skipped and the capability was
silently dropped. This ADR closes the gate by recording the verification and
an explicit go/no-go.

### The MCP capability model (baseline)

The Model Context Protocol defines exactly **three** server-side capability
categories, each advertised in the `capabilities` object of the `initialize`
response and controlled independently
([modelcontextprotocol.io/specification](https://modelcontextprotocol.io/specification/2025-06-18/architecture),
[/docs/learn/server-concepts](https://modelcontextprotocol.io/docs/learn/server-concepts)):

| Capability  | What it is                                                        | Discovery / access             | Controller            |
|-------------|-------------------------------------------------------------------|--------------------------------|-----------------------|
| `tools`     | Executable functions an LLM invokes to perform actions            | `tools/list` → `tools/call`    | Model-controlled      |
| `resources` | Passive, URI-addressed data sources (files, DB rows, API output)  | `resources/list` → `resources/read` | Application-controlled |
| `prompts`   | Reusable interaction templates / pre-built instruction sequences  | `prompts/list` → `prompts/get` | User-controlled        |

A server that omits `resources`/`prompts` from its advertised `capabilities`
is **tools-only**, and a well-behaved client must not call `resources/list` or
`prompts/list` against it.

### Maho's current MCP surface

- **Server side** — `maho/crates/maho-browser-mcp/src/server.rs` advertises
  `"capabilities": {"tools": {}}` at three handshake sites
  (server.rs:964, 1042, 1111). Maho's MCP **server** is tools-only today.
- **Client/host side** — the FIX-6-impl candidate change (gated on this ADR)
  would add `list_resources`/`list_prompts` once at the `mcp_client.rs` layer,
  i.e. teach Maho's MCP **host** to consume `resources`/`prompts` from external
  servers it connects to.

The parity question for OQ2 therefore has two facets — does Aside *expose*
`resources`/`prompts` (server side), and does Aside's host *consume* anything
beyond tools (host side)? We verified both.

---

## Verification Finding (Aside)

**Finding: Aside is tools-only across every publicly documented MCP surface.
No evidence — repo or web — that Aside advertises or consumes `resources` or
`prompts`.**

### Repo evidence

- `docs/competitive-analysis/aside-vs-maho.md` §3.5 "외부 인터페이스"
  (external interfaces, line 182) is the only enumerated MCP entry:
  > "MCP 호스트 | Aside가 MCP host로 동작 — 외부 MCP 도구가 브라우저를 driver | PiunikaWeb / 창업자 X 포스트"
  ("Aside operates as an MCP **host** — external MCP **tools** drive the
  browser", sourced from PiunikaWeb / founder X posts).
- Same doc §8.4 (lines 498–502): "외부 MCP 도구가 브라우저를 driver … Aside는
  1.0에서 출하 … Maho는 미진입." The framing is uniformly **tools** ("MCP
  도구"). Neither `resources` nor `prompts` appears anywhere in the competitive
  analysis (grep of the file for `resources`/`prompts` returns zero hits in an
  Aside context).

### Web evidence

- **Official docs — `aside mcp`** ([docs.aside.com/help/developers](https://docs.aside.com/help/developers)):
  > "Use `aside mcp` to connect Aside to another agent or coding tool as a
  > Model Context Protocol server."

  The documented `mcp.json` is a standard stdio server registration
  (`"command": "aside", "args": ["mcp"]`). The section describes only
  **browser automation tools**; there is zero mention of `resources` or
  `prompts`.
- **Developer blog** ([aside.com/blog/developers](https://aside.com/blog/developers), 2026-06-22):
  frames the surface as coding agents invoking **tools** to obtain "browser
  evidence" (CI logs, dashboards, screenshots). No `resources/read` or
  `prompts/get` semantics.
- **Changelog** ([docs.aside.com/changelog/components](https://docs.aside.com/changelog/components)):
  the sole MCP entry concerns keeping "the MCP server alive after idle
  periods" — no resources/prompts addition or change.
- **MCP spec confirmation** ([modelcontextprotocol.io](https://modelcontextprotocol.io/docs/learn/server-concepts)):
  the three-capability model above is confirmed; a server omitting a category
  is definitionally without it.

### Confidence and caveats

- Confidence: **strong inference, not wire-level proof.** No party has
  published the raw `initialize` JSON from `aside mcp`, so the tools-only
  conclusion rests on the uniform absence of resources/prompts across all
  official docs, the changelog, and third-party analysis — a consistent
  signal, but not a captured handshake.
- Aside markets itself as both an MCP **host** (its internal agent can consume
  external servers) and an MCP **server** (via `aside mcp`). Both facets, as
  documented, are described purely in terms of **tools**. If Aside's host
  happens to consume a third-party server that offers resources/prompts, that
  is a property of *that* server, not a Maho-relevant Aside capability.
- Aside launched June 2026 (YC F25); a future release could add
  resources/prompts without prior announcement. This decision should be
  revisited if Aside publishes such support or if an independent Maho use case
  emerges.

---

## Decision

**NO-GO for v1.** Do not implement `list_resources`/`list_prompts` at the
`mcp_client.rs` layer for the current release. Instead:

1. **Drop resources/prompts from v1 scope.** Maho remains tools-only on both
   its MCP server (`server.rs` `{tools:{}}`) and its MCP host (`mcp_client.rs`)
   surfaces for this release.
2. **Require a status note.** Add a short "MCP capability scope" note to the
   MCP/CLI README (the FIX-6 T8 README target) stating that Maho's MCP
   integration is **tools-only** for v1 and that `resources`/`prompts` are
   intentionally deferred pending demonstrated demand or Aside parity pressure.
   The note must not use OSS-claim language.

### Rationale

- **Parity-driven, and parity does not require it.** OQ2's premise is: implement
  only if Aside actually has it, or if there is independent user value. The
  verification shows Aside is tools-only on every documented MCP surface, so
  the parity justification for `resources`/`prompts` **fails**. Shipping them
  would be building past the competitor, not matching it.
- **No demonstrated independent user value for v1.** No Maho requirement,
  design doc, or user request cites a `resources`/`prompts` need. `resources`
  (application-controlled data attachment) and `prompts` (user-controlled
  templates) are additive host features with real handshake, listing, and
  approval-surface cost — cost we should not pay speculatively.
- **Matches existing scope with zero regression risk.** Maho already advertises
  `{tools:{}}` (server.rs) and the host consumes tools only. NO-GO preserves a
  coherent, already-tested tools-only contract and keeps the FIX-2 UDS
  handshake surface minimal.
- **Cheap to reverse.** The FIX-6-impl design deliberately places any future
  `list_resources`/`list_prompts` at a single `mcp_client` layer, so a later
  GO (triggered by Aside adding support or a concrete Maho use case) is a
  localized, one-place change. Deferring now forecloses nothing.

---

## Consequences

- **FIX-6-impl routes to the no-go branch:** no `resources_prompts_listed`
  test; instead a "resources/prompts status" note is added to the MCP/CLI
  README and its presence is grep-verified. No `mcp_client.rs` resources/prompts
  code lands in v1.
- **Maho stays tools-only end-to-end**, consistent with Aside and with the
  current `server.rs` handshake — no capability drift between server and host.
- **Documented, revisitable gate:** the deferral is explicit (README + this
  ADR) rather than silent. Re-open on (a) Aside publishing resources/prompts
  support, or (b) a concrete Maho use case for URI-addressed resources or
  reusable prompt templates.
- **Risk — future asymmetry:** if a partner MCP server exposes rich
  resources/prompts that users want surfaced in Maho, the host will ignore them
  until GO. Mitigated by the low, single-layer cost of reversing this decision.
