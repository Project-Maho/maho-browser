# ACP — Agent Client Protocol

**Status:** Draft, working towards 1.0-rc.
**Last updated:** 2026-06-17.
**Reference implementation:** the `maho-acp` Rust crate in this workspace.

## What ACP Is

ACP (Agent Client Protocol) is a JSON-RPC-shaped protocol that lets a browser-side host application talk to an out-of-process AI coding agent. The host sends prompts, cancels turns, and answers permission requests. The agent streams message chunks, thoughts, plans, tool calls, command catalogs, configuration options, and usage updates back to the host.

ACP is intentionally narrow: it is the contract between Maho's agentic console and an OpenCode-style agent runtime. It is not a generic LLM API and not a remote desktop protocol.

## Why It Matters

Today, every browser that ships an agentic AI surface invents its own wire protocol between the chrome process and the agent runtime. That fragments the agent ecosystem: an agent built for one browser cannot drop into another, and a browser that adopts a new agent runtime has to write a fresh adapter. ACP is the smallest possible protocol that lets a browser host any compliant agent, and lets an agent serve any compliant browser.

Maho ships ACP as the contract behind the AI console (Wave B P1-4). The spec extraction tracked in Wave C P2-1 is what turns that internal contract into a public, citable specification.

## Scope

In scope:

- Wire format and framing for messages exchanged between a browser-side host and a single local agent runtime.
- The set of JSON-RPC methods and notifications required to drive a chat-style turn with tool calls and permission gates.
- Shared type definitions for prompt parts, plans, tool calls, permission requests, and usage data.

Out of scope:

- Network transport. ACP runs over local IPC (stdio or named pipe). Exposing it over a network is a separate, future profile.
- LLM model selection. ACP carries opaque text and tool calls; the agent runtime is responsible for choosing and invoking models.
- Authentication. The host trusts the agent runtime it spawned; cross-machine trust is out of scope for 1.0.

## Versioning Policy

ACP follows SemVer. The current draft is pre-1.0 and MAY change incompatibly between revisions. The 1.0-rc target is **Q3 2026**, conditional on at least two independent implementations consuming the spec end-to-end.

After 1.0:

- **Major** versions are reserved for breaking wire changes. Maho commits to a 12-month deprecation window.
- **Minor** versions add fields or events that older receivers can safely ignore (existing parsers MUST tolerate unknown discriminators).
- **Patch** versions clarify text only.

## How To Participate

The spec lives in this directory while bootstrap is in progress. The public repository at `github.com/maho-browser/acp-spec` is planned but currently blocked on org access; until that opens, contribute via PR to this workspace.

We welcome:

- Implementer feedback on framing, methods, and types.
- Reference payloads from new agent runtimes that exercise edge cases.
- Profile proposals (for example, a remote-IPC profile, or a multi-session profile).

OpenCode is the first external collaborator. Maho's agent integration runs against OpenCode today; spec changes that diverge from OpenCode's runtime are flagged in PR review.

## License

Apache 2.0, including the patent grant. See `LICENSE` at the workspace root once the public repository is opened. Contributions are accepted under the same license via the standard inbound-equals-outbound rule.

## Files In This Directory

- [`0-overview.md`](0-overview.md) — abstract, goals, terminology, architecture.
- [`1-transport.md`](1-transport.md) — wire format and framing.
- [`2-methods.md`](2-methods.md) — JSON-RPC method and notification catalog.
- [`3-types.md`](3-types.md) — shared type definitions.
