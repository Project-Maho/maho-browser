# OpenCode Phase 1 Transport and Protocol Validation

## Purpose

This document is the concrete Phase 1 validation artifact required by `.sisyphus/plans/opencode-integration-implementation-plan.md`.

Its job is to answer the transport and protocol questions that must be resolved before backend adapter work can be considered real. This is a phase-scoped validation document only. It is **not** evidence that the OpenCode migration is complete.

## Scope

This document validates what can be supported by the currently documented OpenCode server and SDK surfaces for Maho's intended architecture:

- WebUI -> Mojo proxy -> Chromium-side adapter -> OpenCode runtime
- Browser-local privileged tools remain in Maho
- Legacy Rust chat path remains fallback only during migration

## Evidence Sources

### Local source-of-truth documents

1. `.sisyphus/plans/opencode-integration-implementation-plan.md`
2. `docs/frontend-architecture.md`
3. `maho/docs/backend-architecture.md`

### External OpenCode evidence reviewed

1. OpenCode server docs: `https://opencode.ai/docs/server/`
2. OpenCode SDK docs: `https://open-code.ai/en/docs/sdk`
3. Context7 results for `/anomalyco/opencode`
4. Context7 results for `/anomalyco/opencode-sdk-js`

## Required Phase 1 Questions

### 1. Can the chosen OpenCode server mode expose session-oriented interaction?

**Answer: Yes, confirmed.**

Official server docs expose session-oriented endpoints including:

- `POST /session` to create a session
- `GET /session` to list sessions
- `GET /session/:id` to get session details
- `POST /session/:id/abort` to abort a running session
- `POST /session/:id/message` to send a message and wait for response
- `POST /session/:id/prompt_async` to send a message asynchronously

Official SDK docs also expose:

- `createOpencodeClient({ baseUrl })`
- `session.create(...)`
- `session.list()`
- `session.get(...)`
- `session.prompt(...)`

**Implication for Maho:**
Maho can model the browser-side runtime around a stable OpenCode session identity rather than around a single prompt/response exchange.

### 2. Can OpenCode deliver event streams in the shape Maho needs?

**Answer: Partially yes, confirmed at the server event stream level.**

Official evidence confirms:

- `GET /event` exposes a server-sent events stream
- the first event is `server.connected`
- SDK supports `event.subscribe()`
- documented event types include session and message updates, including `message.part.updated`, `session.status`, `session.error`, and permission-related events

This is enough to support:

- runtime connection state
- session status observation
- incremental text/message part updates
- error observation
- permission/approval event observation

**What is not yet fully proven from the docs alone:**

- whether the exact event payloads already match Maho's desired normalized event model without additional translation
- whether all browser-tool round-trip states can be observed solely through the documented event bus

**Implication for Maho:**
The adapter should treat OpenCode's event stream as the raw runtime source and normalize it into Maho's own event schema for Chromium WebUI.

### 3. Is there a confirmed approval or permission response mechanism?

**Answer: Yes, confirmed.**

Official evidence confirms:

- server endpoint: `POST /session/:id/permissions/:permissionID`
- SDK support for replying to permission requests
- event stream documentation includes permission-related events such as permission requests and updates

**Implication for Maho:**
The Chromium-side adapter can treat OpenCode permissions as the primary runtime-level approval primitive, then map them onto Maho approval UI and policy rules.

### 4. Can OpenCode accept tool callback round trips cleanly for Maho browser tools?

**Answer: Not fully proven.**

What is confirmed:

- OpenCode exposes tool-related concepts and experimental tool listing
- OpenCode has permissions and event streams around runtime activity
- OpenCode supports plugins, custom tools, and MCP integrations in its own environment

What is **not** sufficiently proven by the current evidence:

- that OpenCode server mode provides a documented external callback contract for an out-of-process host like Maho to receive an arbitrary tool request, execute a privileged browser action locally, and resume the same runtime turn with a structured result
- that suspension/resumption around those external browser callbacks is guaranteed in the exact way Maho needs

**Implication for Maho:**
Maho must not assume the full external browser-tool callback contract exists just because OpenCode supports tools internally.

### 5. Can OpenCode suspend and resume around local browser tool execution?

**Answer: Unproven.**

The reviewed official docs prove session APIs, SSE events, and permission reply paths. They do **not** yet prove the exact suspend/resume semantics required for browser-local privileged tool execution round-trips.

This is the main unresolved contract risk identified in `maho/docs/backend-architecture.md`.

**Implication for Maho:**
Until stronger proof exists, the adapter must own the local multi-round tool loop for browser-local tools.

### 6. What session identifiers, callback identifiers, and reconnect semantics are actually available?

**Confirmed:**

- OpenCode server sessions have stable session IDs
- session CRUD and listing are documented
- event subscription is long-lived over SSE
- permission responses use permission/request IDs

**Unconfirmed or not yet fully specified for Maho needs:**

- whether reconnect semantics alone are enough to recover all in-flight browser-tool callback state
- whether there is a server-native notion of external callback correlation beyond permissions/events that directly maps to Maho browser tool execution

**Implication for Maho:**
Maho should maintain its own local mapping layer between Chromium-side conversation/session state and OpenCode session IDs, plus its own correlation IDs for browser-local tool work if it owns the tool loop.

## Confirmed Capabilities

The following are strong enough to treat as confirmed for planning and implementation:

1. OpenCode server mode can run as an external HTTP service.
2. Session creation and lookup are available.
3. Prompt/message submission is available.
4. SSE event subscription is available.
5. Permission/approval response endpoints are available.
6. A JS/TS SDK exists for server integration.

## Unconfirmed or Still Assumed

The following must still be treated as unproven for Maho's browser-tool architecture:

1. A documented external browser-tool callback contract that cleanly suspends and resumes the same OpenCode run.
2. Guaranteed server-side semantics for feeding privileged browser tool results back into the same runtime loop.
3. Exact event coverage needed for every browser-local tool lifecycle edge case.
4. Whether OpenCode alone can own the full multi-step browser tool loop without Maho retaining loop control.

## Transport Decision

### Chosen transport for the next phases

**Decision:** Target OpenCode **server mode** as an external runtime using the documented HTTP API plus SSE event stream, preferably via the official JS/TS SDK where practical.

That means:

- session management through OpenCode server sessions
- prompt submission through OpenCode session/message APIs
- live runtime observation through the SSE event stream
- permission responses through the documented permission endpoint

## Adapter Contract Decision for Phase 2/3

### Decision

**Maho should target OpenCode server mode directly for sessions, prompts, events, and permission replies, but the adapter must own a local multi-round browser-tool loop until richer callback guarantees are proven.**

### Why this is the correct Phase 1 decision

1. Real session APIs are documented.
2. Real event streaming is documented.
3. Real permission reply handling is documented.
4. External browser-tool callback suspension/resumption is still not strong enough to assume.

### Consequences for implementation

Phase 2 and Phase 3 should be shaped like this:

1. The Chromium-side adapter creates/reuses real OpenCode sessions.
2. The adapter submits prompts to the OpenCode server.
3. The adapter subscribes to the OpenCode event stream and normalizes events for Maho WebUI.
4. If the runtime needs permission, Maho maps that to approval UI and replies through the OpenCode permission API.
5. Browser-local privileged tools remain in `MahoBrowserToolExecutor`, `MahoBrowserToolRegistry`, and `MahoAiPageContextExtractor`.
6. Until stronger callback evidence exists, Maho owns the multi-round browser-tool orchestration loop rather than assuming OpenCode fully manages it externally.

## What Phase 1 PASS Does Not Mean

Phase 1 PASS means only that the transport evidence is strong enough to choose the adapter shape.

It does **not** mean:

1. the repo already has real OpenCode runtime integration
2. the stub `MahoOpenCodeAdapter` is acceptable as final behavior
3. the migration is complete
4. the OpenCode path is already the default runtime path

## Implementation Guidance for the Next Phase

The next implementation phase must reflect this document exactly:

1. Replace the stub OpenCode adapter with real session and event transport.
2. Use OpenCode session IDs as the runtime identity.
3. Normalize OpenCode SSE events into the Maho runtime event contract.
4. Keep browser-local privileged tool execution in Maho.
5. Do not assume a server-native external tool callback contract that has not been proven.
6. If browser tool work must remain locally orchestrated, make that loop explicit in the adapter.

## Oracle Review Input

### Exact review question

"Is the OpenCode transport and callback evidence strong enough to choose the adapter shape, and does it prove that any pass here is only phase scoped?"

### Oracle PASS conditions for Phase 1

1. The server mode behavior has been tested or otherwise proven against the required transport questions.
2. The decision on suspend/resume versus local multi-round loop is explicit.
3. The evidence records what OpenCode can do, what it cannot do, and what must remain local in Maho.

### Oracle FAIL conditions for Phase 1

1. The document is still relying on assumptions about undocumented OpenCode internals.
2. The transport decision is still reversible without a concrete reason.
3. The result is being treated as full migration completion instead of phase-scoped validation.

## Current Repo Status Against This Phase

Current repo state does **not** satisfy the later runtime phases yet:

- `maho-chromium/browser/ai/maho_opencode_adapter.cc` still returns unavailable
- real OpenCode session creation is not yet wired in product code
- real OpenCode event streaming is not yet wired into the runtime adapter path

That is acceptable for Phase 1 because this phase is a transport/protocol validation and decision gate, not runtime completion.
