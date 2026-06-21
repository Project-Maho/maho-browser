# ACP Specification — 1. Transport

**Status:** Draft (working towards 1.0-rc).
**Last updated:** 2026-06-17.

## Scope

This document defines the wire transport for ACP. It covers framing, encoding, ordering, backpressure, and error frames. The set of methods, notifications, and types carried over this transport is defined in [`2-methods.md`](2-methods.md) and [`3-types.md`](3-types.md).

## Wire Format Overview

ACP is a length-prefixed JSON-RPC protocol over a local byte-stream IPC channel. A conforming connection MUST use either standard input/output of the agent subprocess or a local named pipe (Unix domain socket on POSIX, named pipe on Windows). Network sockets are explicitly out of scope for the 1.0 wire profile.

## Framing

Each message on the wire is framed as:

```
+---------------------------------+
|  Length prefix (4 bytes)        |   uint32, little-endian
+---------------------------------+
|  Payload (Length bytes)         |   UTF-8 JSON
+---------------------------------+
```

Rules:

1. The length prefix is a 4-byte little-endian unsigned integer giving the byte length of the payload that immediately follows.
2. The payload MUST be exactly `Length` bytes. The receiver MUST NOT read past `Length` while parsing one frame.
3. Implementations MUST tolerate a stream of consecutive frames; there are no separators between frames beyond the length prefix.
4. The maximum payload length SHOULD be 16 MiB. Receivers MAY accept larger frames but SHOULD reject frames over 64 MiB to bound memory.
5. A truncated length prefix, a payload shorter than the declared length, or a payload that does not parse as a JSON object MUST be treated as a connection-fatal error.

> **Note on byte order.** The framing uses little-endian to match the reference decoder in [`maho-chromium/browser/ai/maho_acp_event_decoder.h`](../../../../maho-chromium/browser/ai/maho_acp_event_decoder.h). Implementers MUST NOT use big-endian framing; doing so will silently fail against the reference host.

## Encoding

The payload MUST be UTF-8 encoded JSON. Implementations:

- MUST emit valid UTF-8 only.
- MUST NOT use a byte-order mark (BOM).
- SHOULD use compact JSON (no insignificant whitespace) to minimize per-frame overhead, but MAY emit pretty-printed JSON; receivers MUST accept both.
- MUST encode numbers as standard JSON numbers. Integer fields documented as 64-bit MAY exceed the safe integer range of IEEE 754 doubles; receivers SHOULD parse those fields as 64-bit integers directly rather than via floating-point.

## Message Shape

Each payload is a JSON-RPC 2.0 object. The following shapes are valid:

### Request (host → agent or agent → host)

```json
{
  "jsonrpc": "2.0",
  "id": 42,
  "method": "session/submit_prompt",
  "params": { "...": "..." }
}
```

### Response (correlated with a prior request)

```json
{
  "jsonrpc": "2.0",
  "id": 42,
  "result": { "...": "..." }
}
```

or, on failure:

```json
{
  "jsonrpc": "2.0",
  "id": 42,
  "error": {
    "code": -32601,
    "message": "Method not found",
    "data": { "method": "session/unknown" }
  }
}
```

### Notification (one-way)

```json
{
  "jsonrpc": "2.0",
  "method": "session/update",
  "params": { "...": "..." }
}
```

Notifications MUST NOT carry an `id` field; receivers MUST ignore notifications that incorrectly include `id`.

## Direction And Initiation

- The host sends requests for **methods**: `start_session`, `resume_session`, `submit_prompt`, `cancel_turn`, `respond_to_approval`. These have responses.
- The agent sends notifications for **session updates**: streaming chunks, plans, tool calls, tool call updates, available commands, config option updates, usage updates. These have no responses.
- The agent sends requests for **permission requests** (`session/request_permission`). The host MUST eventually reply with a JSON-RPC response carrying a `PermissionOutcome`.
- Either side MAY send notifications. Either side MAY send requests, but in practice the host initiates control-plane methods and the agent initiates permission requests.

## Ordering

Within a single connection:

1. The agent's `session/update` notifications MUST be delivered in the same order they were emitted by the agent. This is a property of the underlying byte stream and is preserved by the framing rules above.
2. A request and its response are correlated only by `id`. Responses MAY arrive out of order with respect to other unrelated requests.
3. After the host sends `submit_prompt`, the agent's session updates for that turn MUST appear before the response to `submit_prompt`. The response is the terminal signal for the turn.

## Backpressure

ACP defines no transport-level backpressure mechanism. The agent SHOULD self-throttle by buffering session updates if the host's read rate is slow; the host SHOULD drain the read side eagerly to avoid filling kernel pipe buffers and stalling the agent.

If the host detects that it cannot keep up (for example, repeated read timeouts on the agent side), it MAY cancel the current turn via `cancel_turn` and return to `kReady`. Hosts MUST NOT rely on the agent to recover from a stalled read.

## Error Frames

Two error categories exist:

### Protocol-level errors (JSON-RPC `error` member)

Returned in response to a specific request. Use the standard JSON-RPC error code ranges:

| Code      | Name              | Use |
|-----------|-------------------|-----|
| `-32700`  | Parse error       | Payload was not valid JSON. The connection SHOULD be closed after sending. |
| `-32600`  | Invalid request   | JSON object did not match the JSON-RPC 2.0 envelope. |
| `-32601`  | Method not found  | The receiver does not implement the named method. |
| `-32602`  | Invalid params    | Method exists but parameters did not validate. |
| `-32603`  | Internal error    | Receiver-side failure not attributable to the request. |
| `-32000` to `-32099` | Implementation-defined | ACP-specific errors; see [`2-methods.md`](2-methods.md) for the full table. |

### Connection-fatal errors

A truncated frame, a non-UTF-8 payload, or a payload that does not parse as a JSON object MUST cause the receiver to close the connection. The host SHOULD log the error and transition the adapter state machine to `kError`. Recovery requires a fresh connection.

## Connection Lifecycle

1. The host spawns the agent process (or opens the pipe).
2. The host sends `start_session`. The agent transitions the host adapter from `kStarting` through `kInitializing` to `kSessionPending` to `kReady`.
3. The host MAY send `submit_prompt` once the adapter is `kReady`.
4. The agent streams `session/update` notifications and MAY send `session/request_permission` during a turn.
5. The agent sends a final `submit_prompt` response when the turn ends.
6. The host MAY send `cancel_turn` at any time during a turn; the agent SHOULD respond with a stop reason of `cancelled`.
7. The host closes the connection by terminating the agent process or closing the pipe. There is no graceful-close handshake in the 1.0 wire profile.
