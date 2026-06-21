# ACP Specification — 3. Types

**Status:** Draft (working towards 1.0-rc).
**Last updated:** 2026-06-17.

## Scope

This document catalogs the shared type definitions used by ACP methods (see [`2-methods.md`](2-methods.md)) and notifications. Each type is given a TypeScript-style declaration plus a JSON example. Types are stable across minor versions; new fields MAY be added but receivers MUST tolerate unknown fields.

The type names below correspond to symbols exported from [`maho/crates/maho-acp/src/events.rs`](../src/events.rs); the wire shape is what counts here, and Rust-specific concerns (`#[serde(flatten)]`, `Value` extras) are noted only when they affect the JSON.

## ContentValue

Free-form payload accepted in chunk-style updates. Receivers MUST accept all three variants. The reference implementation collapses them to plain text via `extract_text_from_content`.

```ts
type ContentValue =
  | string
  | { type: "text"; text: string }
  | Array<string | { type: "text"; text: string } | { type: "content"; content: { type: "text"; text: string } }>;
```

**Examples**

```json
"hello"
```

```json
{ "type": "text", "text": "hello" }
```

```json
[
  { "type": "text", "text": "line one\n" },
  { "type": "text", "text": "line two" }
]
```

## ContentArray

Used by `tool_call_update.content`. Each entry is one fragment of tool output.

```ts
type ContentArray = Array<{
  type: "text";
  text: string;
} | {
  type: "content";
  content: { type: "text"; text: string };
}>;
```

**Example**

```json
[
  { "type": "text", "text": "line1" },
  { "type": "text", "text": "line2" }
]
```

## PromptPart

A single piece of a user prompt sent via `submit_prompt`.

```ts
type PromptPart =
  | { kind: "text"; text: string }
  | { kind: "image"; mediaType: string; data: string /* base64 */ };
```

**Example — text part**

```json
{ "kind": "text", "text": "Summarize the page I'm on." }
```

**Example — image part**

```json
{
  "kind": "image",
  "mediaType": "image/png",
  "data": "iVBORw0KGgoAAAANSUhEUgAA..."
}
```

The 1.0 draft defines only `text` and `image`. Conforming senders MAY include additional `kind` values; receivers MUST ignore unknown parts rather than fail the prompt.

## PlanEntry

One row in a plan update (`session/update` with `sessionUpdate: "plan"`).

```ts
interface PlanEntry {
  title: string;
  status: string; // "pending" | "in_progress" | "completed" | "cancelled" | implementation-defined
  // Implementations MAY include extra fields; receivers MUST preserve them as opaque.
}
```

**Example**

```json
{
  "title": "Read the spec README.",
  "status": "completed"
}
```

The reference parser uses `#[serde(flatten)] extra: Value` to retain unknown fields verbatim. Conforming hosts SHOULD render `title` and `status` and SHOULD pass through unknown extras to inspectors that may understand them.

## ToolKind

Discriminator describing the broad category of a tool call.

```ts
type ToolKind =
  | "tool"
  | "bash"
  | "read"
  | "write"
  | "edit"
  | "glob"
  | "grep"
  | "fetch"
  | "unknown";
```

The `unknown` variant exists for forward compatibility; receivers MUST map any unrecognized string to `unknown` rather than failing the parse.

## ToolCallStatus

Lifecycle status of a tool call.

```ts
type ToolCallStatus =
  | "pending"
  | "in_progress"
  | "completed"
  | "failed"
  | "cancelled"
  | "unknown";
```

Same forward-compatibility rule as `ToolKind`: unknown strings collapse to `unknown`.

## ToolCallLocation

Optional source-location hint attached to a tool call.

```ts
interface ToolCallLocation {
  file?: string;
  line?: number;
  // Implementations MAY include additional fields.
}
```

**Example**

```json
{ "file": "maho/crates/maho-acp/src/events.rs", "line": 252 }
```

## AvailableCommand

One entry in an `available_commands_update`. The agent publishes its slash-command catalog with these.

```ts
interface AvailableCommand {
  id: string;
  name: string;
  // Implementations MAY include additional fields (description, args, etc.).
}
```

**Example**

```json
{ "id": "init", "name": "/init" }
```

## UsageCost

Currency-tagged numeric cost for a turn.

```ts
interface UsageCost {
  amount: number;
  currency: string; // ISO 4217 code recommended ("USD", "EUR")
}
```

**Example**

```json
{ "amount": 0.005, "currency": "USD" }
```

## PromptUsage

Token accounting returned with a `submit_prompt` response.

```ts
interface PromptUsage {
  inputTokens?: number;
  outputTokens?: number;
  // Implementations MAY include additional fields.
}
```

**Example**

```json
{ "inputTokens": 312, "outputTokens": 980 }
```

## StopReason

Terminal signal carried in the `submit_prompt` response.

```ts
type StopReason =
  | "end_turn"
  | "max_tokens"
  | "max_turn_requests"
  | "refusal"
  | "cancelled"
  | string; // forward-compat: unknown values pass through verbatim
```

The reference implementation uses `StopReason::Unknown(String)` for unrecognized values. Hosts SHOULD surface unknown stop reasons in the timeline rather than silently treating them as `end_turn`.

## PermissionToolCall

Identifies the tool call that a permission request is gating.

```ts
interface PermissionToolCall {
  id: string;
  tool: string;
  // Implementations MAY include additional fields (input preview, etc.).
}
```

**Example**

```json
{ "id": "tc99", "tool": "fs.write" }
```

## PermissionOption

One choice presented to the user in a permission prompt.

```ts
interface PermissionOption {
  optionId: string;
  kind: string; // e.g. "allow_once" | "allow_session" | "deny"
  name: string; // human-readable label
}
```

**Example**

```json
{ "optionId": "once", "kind": "allow_once", "name": "Allow once" }
```

## PermissionRequest

The full inbound shape carried in `session/request_permission`. Note that `jsonRpcId` is the wire-level JSON-RPC `id` of the request itself; the host MUST echo this exact value when sending the corresponding response (this is the `json_rpc_id` field in the reference Rust struct).

```ts
interface PermissionRequest {
  jsonRpcId: number;
  sessionId: string;
  toolCall: PermissionToolCall;
  options: PermissionOption[];
}
```

**Example**

```json
{
  "jsonRpcId": 17,
  "sessionId": "sess_4f2c",
  "toolCall": { "id": "tc99", "tool": "fs.write" },
  "options": [
    { "optionId": "once", "kind": "allow_once", "name": "Allow once" },
    { "optionId": "deny", "kind": "deny", "name": "Deny" }
  ]
}
```

## PermissionOutcome

The host's reply to a `PermissionRequest`. Carried inside the `result` member of the JSON-RPC response.

```ts
type PermissionOutcome =
  | { kind: "selected"; optionId: string }
  | { kind: "cancelled" };
```

**Example — selected**

```json
{ "kind": "selected", "optionId": "once" }
```

**Example — cancelled**

```json
{ "kind": "cancelled" }
```

## ToolCall (Notification Payload)

Composite type carried in the `tool_call` notification. Defined here for completeness; see [`2-methods.md`](2-methods.md) for the wire envelope.

```ts
interface ToolCall {
  toolCallId: string;
  title: string;
  kind: ToolKind;
  status: ToolCallStatus;
  locations: ToolCallLocation[];
  input: unknown;
}
```

**Example**

```json
{
  "toolCallId": "tc99",
  "title": "Read file",
  "kind": "read",
  "status": "in_progress",
  "locations": [{ "file": "foo.rs", "line": 10 }],
  "input": { "path": "/tmp/foo" }
}
```

## Forward Compatibility Rules (Recap)

1. Receivers MUST accept unknown JSON object members and preserve them when forwarding.
2. Receivers MUST map unknown enum values to a designated `unknown` variant where one exists (`ToolKind`, `ToolCallStatus`) or pass through verbatim where one does not (`StopReason`).
3. Receivers MUST tolerate unknown `sessionUpdate` discriminator strings by treating the update as opaque (the reference implementation's `AcpUpdate::Other` shape).
4. Senders MAY add new fields and MAY emit new enum variants in minor versions. Senders MUST NOT remove or rename existing fields without bumping the major version.
