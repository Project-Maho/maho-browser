# ACP Specification — 2. Methods

**Status:** Draft (working towards 1.0-rc).
**Last updated:** 2026-06-17.

## Scope

This document catalogs the JSON-RPC methods (host → agent) and notifications (agent → host, plus one inbound request type) that compose the ACP control plane. Type definitions referenced here live in [`3-types.md`](3-types.md). Wire framing is defined in [`1-transport.md`](1-transport.md).

The notification names listed below are the values of the `update.sessionUpdate` discriminator field carried in the `session/update` envelope. Conforming implementations MUST emit these exact discriminator strings (snake_case), and parsers MUST tolerate unknown discriminators by treating them as opaque pass-through data (see `AcpUpdate::Other` in the reference implementation).

## State Machine Reference

The host adapter runs the state machine defined in [`maho-chromium/browser/ai/maho_opencode_acp_adapter.h`](../../../../maho-chromium/browser/ai/maho_opencode_acp_adapter.h):

```
kIdle  ->  kStarting  ->  kInitializing  ->  kSessionPending  ->  kReady
                                                                    |
                                                                    v
                                              kReady  <--  kAwaitingTurn  <--  kSubmitting
                                                                    |
                                                                    v
                                                                  kError  (terminal until reset)
```

Each method below is annotated with the states in which it is valid.

## Methods (Host → Agent)

### `session/start_session`

Open a fresh session.

- **Valid in states:** `kStarting`, `kInitializing`.
- **Result:** opaque session id assigned by the agent.
- **Errors:** `-32000` if the agent cannot allocate a session.

**Parameters**

```ts
interface StartSessionParams {
  /** Optional client-provided hint for naming/persistence. */
  workspaceHint?: string;
}
```

**Result**

```ts
interface StartSessionResult {
  sessionId: string;
}
```

**Example request**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "session/start_session",
  "params": { "workspaceHint": "maho-spec-extraction" }
}
```

**Example response**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": { "sessionId": "sess_4f2c" }
}
```

---

### `session/resume_session`

Reattach to a previously created session that the host already knows about.

- **Valid in states:** `kStarting`, `kInitializing`.
- **Result:** the same session id, echoed back, plus any cached config snapshot.
- **Errors:** `-32001` if the session id is unknown to the agent.

**Parameters**

```ts
interface ResumeSessionParams {
  sessionId: string;
}
```

**Result**

```ts
interface ResumeSessionResult {
  sessionId: string;
}
```

**Example request**

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "session/resume_session",
  "params": { "sessionId": "sess_4f2c" }
}
```

---

### `session/submit_prompt`

Submit a user prompt and start a turn. The agent emits zero or more `session/update` notifications during the turn and finally sends the response carrying the stop reason and usage.

- **Valid in states:** `kReady`.
- **Side effect:** transitions the host adapter through `kSubmitting` to `kAwaitingTurn` until the response arrives.
- **Errors:** `-32002` if the agent is busy with another turn; `-32603` for internal failures.

**Parameters**

```ts
interface SubmitPromptParams {
  sessionId: string;
  prompt: PromptPart[];
}
```

**Result**

```ts
interface SubmitPromptResult {
  stopReason: "end_turn" | "max_tokens" | "max_turn_requests" | "refusal" | "cancelled";
  usage?: PromptUsage;
}
```

`PromptPart`, `PromptUsage`, and the stop reason set are defined in [`3-types.md`](3-types.md). The reference parser maps the stop reason via `StopReason::parse`; unknown values are preserved verbatim.

**Example request**

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "session/submit_prompt",
  "params": {
    "sessionId": "sess_4f2c",
    "prompt": [
      { "kind": "text", "text": "Explain the ACP transport." }
    ]
  }
}
```

**Example response**

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "stopReason": "end_turn",
    "usage": { "inputTokens": 312, "outputTokens": 980 }
  }
}
```

---

### `session/cancel_turn`

Cancel the in-flight turn. The agent SHOULD stop emitting updates as soon as possible and respond to the corresponding `submit_prompt` with `stopReason: "cancelled"`.

- **Valid in states:** `kSubmitting`, `kAwaitingTurn`.
- **Result:** empty object on acknowledgement.
- **Errors:** `-32003` if no turn is in flight (no-op acknowledgement is also acceptable).

**Parameters**

```ts
interface CancelTurnParams {
  sessionId: string;
}
```

**Result**

```ts
type CancelTurnResult = {};
```

**Example request**

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "session/cancel_turn",
  "params": { "sessionId": "sess_4f2c" }
}
```

---

### `session/respond_to_approval`

Reply to an inbound `session/request_permission`. The host MUST use the `id` from the original permission request as the JSON-RPC `id` of this response (this is the wire-level correlation; see `PermissionRequest::json_rpc_id` in the reference implementation).

- **Valid in states:** `kAwaitingTurn` (during a turn that emitted a permission request).
- **Result:** none — this is a response, not a request.

**Response shape**

```json
{
  "jsonrpc": "2.0",
  "id": 17,
  "result": {
    "outcome": {
      "kind": "selected",
      "optionId": "once"
    }
  }
}
```

If the user cancelled the prompt without selecting an option, the host emits:

```json
{
  "jsonrpc": "2.0",
  "id": 17,
  "result": {
    "outcome": { "kind": "cancelled" }
  }
}
```

The `PermissionOutcome` shape is defined in [`3-types.md`](3-types.md).

## Inbound Requests (Agent → Host)

### `session/request_permission`

The agent asks the host to confirm a tool call before executing it. The host MUST eventually reply (`respond_to_approval`); abandoning a permission request is a protocol violation.

**Parameters**

```ts
interface PermissionRequestParams {
  sessionId: string;
  toolCall: PermissionToolCall;
  options: PermissionOption[];
}
```

**Example request (from agent)**

```json
{
  "jsonrpc": "2.0",
  "id": 17,
  "method": "session/request_permission",
  "params": {
    "sessionId": "sess_4f2c",
    "toolCall": { "id": "tc99", "tool": "fs.write" },
    "options": [
      { "optionId": "once", "kind": "allow_once", "name": "Allow once" },
      { "optionId": "session", "kind": "allow_session", "name": "Allow for this session" },
      { "optionId": "deny", "kind": "deny", "name": "Deny" }
    ]
  }
}
```

## Notifications (Agent → Host)

All session updates ride a single envelope:

```json
{
  "jsonrpc": "2.0",
  "method": "session/update",
  "params": {
    "sessionId": "sess_4f2c",
    "update": {
      "sessionUpdate": "<discriminator>",
      "...": "..."
    }
  }
}
```

The nine discriminators below mirror the variants of `AcpUpdate` in [`maho/crates/maho-acp/src/events.rs`](../src/events.rs). Receivers MUST tolerate unknown discriminators by preserving them as opaque pass-through (the `AcpUpdate::Other` shape).

---

### `agent_message_chunk`

Streaming token output from the agent's reply.

**Update payload**

```ts
interface AgentMessageChunkUpdate {
  sessionUpdate: "agent_message_chunk";
  messageId: string;
  content: ContentValue; // string, object {type,text}, or array of those
}
```

**Example**

```json
{
  "sessionUpdate": "agent_message_chunk",
  "messageId": "msg1",
  "content": { "type": "text", "text": "hello" }
}
```

---

### `agent_thought_chunk`

Streaming agent reasoning, separated from the user-visible reply.

```ts
interface AgentThoughtChunkUpdate {
  sessionUpdate: "agent_thought_chunk";
  messageId: string;
  content: ContentValue;
}
```

Hosts SHOULD render agent thoughts in a distinct surface (Maho's Inspector panel) and SHOULD NOT mix them with `agent_message_chunk` text.

---

### `user_message_chunk`

Echo of the user's prompt as the agent ingests it. Useful for replay and audit.

```ts
interface UserMessageChunkUpdate {
  sessionUpdate: "user_message_chunk";
  messageId: string;
  content: ContentValue;
}
```

---

### `plan`

Append-only list of plan entries the agent intends to execute.

```ts
interface PlanUpdate {
  sessionUpdate: "plan";
  entries: PlanEntry[];
}
```

`PlanEntry` is defined in [`3-types.md`](3-types.md).

---

### `tool_call`

A new tool call has been started.

```ts
interface ToolCallUpdate {
  sessionUpdate: "tool_call";
  toolCallId: string;
  title: string;
  kind: ToolKind;
  status: ToolCallStatus;
  locations: ToolCallLocation[];
  input: unknown;
}
```

---

### `tool_call_update`

A status or content update for an existing tool call. Receivers MUST match on `toolCallId` to associate it with the original `tool_call`.

```ts
interface ToolCallStatusUpdate {
  sessionUpdate: "tool_call_update";
  toolCallId: string;
  status: ToolCallStatus;
  content?: ContentArray;
}
```

The `content` array, when present, MAY be collapsed to plain text by the receiver (the reference parser does this via `collapse_content_to_text`).

---

### `available_commands_update`

The agent is publishing or republishing its slash-command catalog.

```ts
interface AvailableCommandsUpdate {
  sessionUpdate: "available_commands_update";
  commands: AvailableCommand[];
}
```

---

### `config_option_update`

The agent is exposing a new configuration switch (for example, "model: claude-sonnet-4").

```ts
interface ConfigOptionUpdate {
  sessionUpdate: "config_option_update";
  options: unknown; // opaque, agent-defined
}
```

Hosts SHOULD render the options surface verbatim and MUST NOT re-key the options object.

---

### `usage_update`

Token and cost telemetry for the in-flight turn.

```ts
interface UsageUpdate {
  sessionUpdate: "usage_update";
  used?: number;
  size?: number;
  cost?: { amount: number; currency: string };
}
```

The fields `used` and `size` MAY also arrive under the legacy aliases `inputTokens` and `outputTokens`; conforming parsers MUST accept both.

## Implementation-Defined Error Codes

| Code     | Name                  | Meaning |
|----------|-----------------------|---------|
| `-32000` | `session_unavailable` | The agent cannot allocate or open the requested session. |
| `-32001` | `session_unknown`     | The session id passed to `resume_session` is not known. |
| `-32002` | `agent_busy`          | A turn is already in flight on this session. |
| `-32003` | `no_active_turn`      | `cancel_turn` was called when no turn was in flight. |
| `-32010` | `permission_denied`   | A required permission outcome was negative. |
| `-32011` | `tool_failed`         | A tool execution failed irrecoverably. |

Implementations MAY define additional codes in the `-32000` to `-32099` range and MUST document them.

## AcpUpdate Cross-Reference

For convenience, the nine notification discriminators in this document map 1:1 to the corresponding `AcpUpdate` variants in the reference Rust implementation:

| Wire discriminator | Rust variant |
|---|---|
| `agent_message_chunk` | `AcpUpdate::AgentMessageChunk` |
| `agent_thought_chunk` | `AcpUpdate::AgentThoughtChunk` |
| `user_message_chunk` | `AcpUpdate::UserMessageChunk` |
| `plan` | `AcpUpdate::Plan` |
| `tool_call` | `AcpUpdate::ToolCall` |
| `tool_call_update` | `AcpUpdate::ToolCallUpdate` |
| `available_commands_update` | `AcpUpdate::AvailableCommandsUpdate` |
| `config_option_update` | `AcpUpdate::ConfigOptionUpdate` |
| `usage_update` | `AcpUpdate::UsageUpdate` |

The inbound permission request (`session/request_permission`) maps to `PermissionRequest`, and the host's reply maps to `PermissionOutcome`.
