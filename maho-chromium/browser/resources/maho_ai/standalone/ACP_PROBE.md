# ACP Probe

A self-contained Node.js probe that exercises the full `opencode acp` JSON-RPC
wire protocol from end to end. It validates the Phase A protocol design before
wiring the equivalent logic into the Maho C++ adapter.

## Purpose

The Chromium `unit_tests` link is blocked by a pre-existing failure in an
unrelated file (`maho_command_action_handler.cc`). Rather than wait on that
or fight the Chromium build, this probe acts as the verification gate for the
Phase A transport/JSON-RPC primitives by running the same call sequence against
the real `opencode` binary via Node's `child_process.spawn`.

If the probe exits `0`, the wire shape is confirmed and the C++ port has high
confidence to proceed to Phase B. If the probe fails, the wire-level log
provides concrete findings to feed back into the C++ implementation.

## How to run

From the **workspace root**:

```bash
node maho-chromium/browser/resources/maho_ai/standalone/acp_probe.mjs
```

`opencode` must be on `PATH` (or override via `OPENCODE_BINARY` — see below).
Provider credentials must be configured with `opencode auth login` before
running; the probe does not configure auth.

## Probe sequence

1. Spawns `opencode acp` with `stdin`/`stdout`/`stderr` pipes.
2. **`initialize`** — sends `protocolVersion: 1` and
   `clientCapabilities: { fs: { readTextFile: true, writeTextFile: true } }`.
   Prints `agentCapabilities` and `authMethods` from the response.
3. **`session/new`** — creates a new session with `cwd: process.cwd()`.
   Prints the returned `sessionId` and any extension fields
   (`models`, `modes`, `configOptions`).
4. **`session/prompt`** — sends the test prompt (default:
   `"Reply with the single word PROBE_OK and nothing else."`).
   All inbound `session/update` notifications are logged with their
   `sessionUpdate` discriminator.
5. **`session/request_permission`** — any inbound permission request is
   auto-rejected with `{ outcome: { outcome: "selected", optionId: "reject" } }`.
   The probe never approves tool calls.
6. On `session/prompt` resolution: prints `stopReason`, `usage`, and the
   full accumulated assistant text.
7. **Pass verdict**: exits `0` if `stopReason === "end_turn"` and the response
   contains `"PROBE_OK"`. Exits non-zero with a `[probe] FAIL <reason>` line
   otherwise.
8. **Graceful shutdown**: closes stdin, waits 1.5 s, sends SIGTERM, then
   SIGKILL after another 1 s. No `opencode` zombie is left behind.

## Environment variable overrides

| Variable | Default | Description |
|---|---|---|
| `OPENCODE_BINARY` | `opencode` | Path to the `opencode` binary |
| `OPENCODE_PROBE_PROMPT` | `"Reply with the single word PROBE_OK and nothing else."` | Override the test prompt |
| `OPENCODE_PROBE_TIMEOUT_MS` | `60000` | Overall wall-clock timeout in milliseconds |

## Output format

- `[probe] ...` — structured protocol and verdict lines on **stdout**
- `[acp stderr] ...` — child process stderr forwarded to **stderr**
- Final line is always `[probe] PASS` or `[probe] FAIL <reason>`
- Full session log is also saved to
  `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-ai-acp-probe.log`

## Expected pass output (abridged)

```
[probe] spawning: opencode acp
[probe] step 1: sending initialize
[probe] initialize ok — protocolVersion: 1
[probe] agentCapabilities: { ... }
[probe] authMethods: [...]
[probe] step 2: sending session/new
[probe] session/new ok — sessionId: <opaque>
[probe] step 3: sending session/prompt (timeout 60000ms)
[probe] session/update agent_message_chunk: "PROBE_OK"
[probe] session/prompt resolved — stopReason: end_turn
[probe] usage: inputTokens=... outputTokens=...
[probe] accumulated assistant text: "PROBE_OK"
[probe] child exited with code 0
[probe] PASS
```

## Known caveats

- **Provider auth must be configured on the host.** The probe inherits the
  current user environment. Run `opencode auth login` before running the probe
  if auth is not already configured.
- **Network latency.** The default 60 s timeout is generous but may need
  increasing on slow connections or with slow models. Use
  `OPENCODE_PROBE_TIMEOUT_MS=120000`.
- **Binary not found.** If `opencode` is not on PATH and `OPENCODE_BINARY` is
  not set, the probe exits immediately with
  `[probe] FAIL binary not found: "opencode"`.
- **Tool calls during the probe.** The probe auto-rejects all permission
  requests. If the model issues a tool call against the simple `PROBE_OK`
  prompt it means the agent sent an unexpected tool — this is surfaced in
  the log and the rejection is recorded. The probe will still pass as long as
  the model ultimately responds with `PROBE_OK` and `stopReason: end_turn`.
- **Unknown `sessionUpdate` discriminators.** Future ACP extensions that add
  new discriminator values will be logged as `unknown` but will not cause the
  probe to fail. This matches the additive-safe stance in the migration plan.

## Relationship to Phase A C++ code

The probe validates the same wire interactions implemented in:

- `maho_acp_transport.{h,cc}` — nd-JSON line reader/writer
- `maho_acp_jsonrpc.{h,cc}` — JSON-RPC 2.0 router
- `maho_opencode_acp_runtime_service.{h,cc}` — child process manager

A probe `PASS` confirms the protocol design is correct and gives high confidence
that the C++ port will behave identically. A probe `FAIL` provides a concrete
wire-level log to feed back into the C++ before Phase B begins.
