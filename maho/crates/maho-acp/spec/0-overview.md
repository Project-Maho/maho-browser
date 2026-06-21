# ACP Specification — 0. Overview

**Status:** Draft (working towards 1.0-rc).
**Last updated:** 2026-06-17.

## Abstract

The Agent Client Protocol (ACP) defines a JSON-RPC-shaped wire protocol for communication between a browser-side host and a local AI coding agent runtime. The host issues prompts, cancels in-flight turns, and answers permission requests; the agent streams message chunks, agent thoughts, plans, tool calls, command catalogs, configuration options, and usage updates. ACP is intentionally narrow — it is the contract between an agentic browser console and an out-of-process agent runtime, not a generic LLM API.

This document is the entry point for the ACP specification. Subsequent documents define the transport layer, the method and notification catalog, and the shared type system.

## Status Of This Document

This is a working draft. The protocol described here is implemented by the `maho-acp` Rust crate and consumed by Maho's agentic console (Wave B P1-4). The spec is on track for 1.0-rc in Q3 2026, conditional on at least two independent implementations consuming it end-to-end.

Pre-1.0 changes MAY be incompatible. Implementers SHOULD pin to a specific draft revision for production use.

## Conventions

The keywords **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** in this specification are to be interpreted as described in [RFC 2119](https://www.rfc-editor.org/rfc/rfc2119) and [RFC 8174](https://www.rfc-editor.org/rfc/rfc8174) when, and only when, they appear in all capitals.

JSON object members described as "required" MUST be present in conforming payloads. Members described as "optional" MAY be omitted. Receivers MUST tolerate unknown members and unknown enumeration variants without failing the connection; this is the forward-compatibility rule that allows minor versions to add fields safely.

All examples in this specification are normative for the shape they illustrate. Whitespace and member ordering in JSON payloads are not significant.

## Terminology

- **Host** — the browser process that spawns and drives the agent. In Maho's case, the Chromium browser process and its WebUI surfaces.
- **Agent** — the out-of-process runtime that executes tool calls, drives an LLM, and emits session updates. In Maho's case, OpenCode.
- **Session** — a single conversational context. One ACP connection MAY carry multiple sessions in sequence; only one session is active at a time.
- **Turn** — one host prompt plus the agent's full response (message chunks, tool calls, permission requests, and final stop reason). A turn ends when the agent emits a stop reason or the host cancels.
- **Tool call** — a request by the agent to invoke a named tool with structured input. Tool calls MAY require host approval before execution.
- **Permission request** — an inbound request from the agent asking the host to confirm a sensitive tool call before execution.
- **Update** — a server-to-host notification, carried as a `session/update` notification with a discriminated `update.sessionUpdate` field.

## Goals

- **Browser-AI specificity.** ACP MUST express the events a browser-side agentic console needs to render: streaming text, thoughts, plans, tool calls with status transitions, permission gates, and usage telemetry.
- **Implementation independence.** Any agent runtime that implements the methods and types defined here MUST be drop-in compatible with any compliant host.
- **Forward compatibility.** Receivers MUST tolerate unknown methods, unknown discriminators, and unknown fields. This is enforced by the parser shape in `events.rs` (`AcpUpdate::Other` for unknown discriminators) and SHOULD be enforced by all conforming implementations.
- **Minimal surface.** ACP defines only what a browser-side console actually consumes. Auxiliary concerns (model selection, billing, multi-tenant accounting) are out of scope.

## Non-Goals

- **ACP is not a generic LLM API.** Vendor-specific concepts (function-calling JSON shapes, model identifiers, sampling parameters) are explicitly out of scope. Hosts that need those concerns talk to providers directly.
- **ACP is not a remote desktop or screen-sharing protocol.** It carries text, structured tool calls, and metadata only.
- **ACP is not a multi-host fan-out protocol.** One agent serves one host per connection; multiplexing is a host concern.
- **ACP does not standardize the tool catalog.** Tool names and input schemas are agreed between host and agent out of band; ACP only carries the calls.
- **ACP does not standardize authentication.** A host that spawns an agent locally trusts that agent. Cross-machine authentication is deferred.

## Architecture

```
+----------------------------+         length-prefixed JSON          +------------------------------+
|     Host (Browser)         |  <----------------------------------> |  Agent runtime               |
|  (Maho's AI console)       |       framed JSON-RPC over IPC        |  (e.g. OpenCode subprocess)  |
+----------------------------+                                        +------------------------------+
            ^                                                                       ^
            |                                                                       |
            |   maho_ai.mojom (page handler API)                                    |
            v                                                                       |
+----------------------------+                                                      |
|  WebUI (TypeScript)        |                                                      |
|  - Composer                |                                                      |
|  - Timeline                |                                                      |
|  - Inspector               |                                                      |
|  - Approval card           |                                                      |
+----------------------------+                                                      |
                                                                                    |
            +---------------------- maho-acp Rust crate -------------------------+  |
            |                                                                    |  |
            |  AcpClient (transport, framing, session state)                     |--+
            |  └─ parse_session_update -> AcpUpdate                              |
            |  └─ PermissionRequest / PermissionOutcome                          |
            |                                                                    |
            +--------------------------------------------------------------------+
```

The host owns the agent process. Spawning, restart on crash, and supervised shutdown are host responsibilities. The wire protocol begins at the moment the host opens the agent's stdio (or named pipe) handles.

A single connection drives the state machine described in [`maho-chromium/browser/ai/maho_opencode_acp_adapter.h`](../../../../maho-chromium/browser/ai/maho_opencode_acp_adapter.h):

```
kIdle
  -> kStarting
       -> kInitializing
            -> kSessionPending
                 -> kReady
                      -> kSubmitting
                           -> kAwaitingTurn
                                -> kReady (next turn)
                                -> kError (any failure)
```

Conforming implementations MUST honor the linear progression from `kIdle` to `kReady`; once `kReady`, the host MAY enter `kSubmitting` for each user prompt and MUST return to `kReady` before submitting another. `kError` is terminal until the host issues an explicit reset.

## Reference Material

- Maho's agentic console design lives in [`docs/plans/design-docs/ai-console-phase2.md`](../../../../docs/plans/design-docs/ai-console-phase2.md).
- The wire-format decoder used by the Chromium overlay is [`maho-chromium/browser/ai/maho_acp_event_decoder.h`](../../../../maho-chromium/browser/ai/maho_acp_event_decoder.h).
- The Rust event parser is [`maho/crates/maho-acp/src/events.rs`](../src/events.rs).
- Implementers SHOULD read the OpenCode runtime's documentation alongside this spec, since OpenCode is the first conforming agent.
