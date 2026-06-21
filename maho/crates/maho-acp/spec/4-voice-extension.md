# ACP Voice Extension (Proposal)

Status: Draft proposal. Not yet accepted upstream.
Owner: Maho team.
Targets: ACP 1.x.

This document proposes adding bidirectional realtime voice frames to the Agent
Client Protocol. The motivation and the placement choice are explained in
[`docs/decisions/0003-realtime-voice-provider.md`](../../../../docs/decisions/0003-realtime-voice-provider.md).

## Motivation

ACP today is text-and-tool-call shaped. Agent runtimes (OpenCode and others)
already integrate cloud LLM providers that support realtime voice (OpenAI
Realtime, Deepgram, ElevenLabs, Azure Speech, etc.). To expose that to ACP
clients (such as Maho) we need a small, well-bounded extension.

We do **not** want each ACP client to ship its own parallel WebRTC stack and
provider routing. That duplicates work, fragments permission flows, and
prevents agents from controlling turn-taking the way they need to.

## Goals

- One protocol shape for streaming user → agent audio and agent → user audio.
- Per-turn lifecycle that matches ACP's existing per-turn lifecycle.
- Clear permission semantics for microphone access.
- Compatible with existing ACP transport (length-prefixed JSON over stdio).
- Optional: out-of-band binary audio frames for clients that want to avoid
  base64 in JSON.

## Non-Goals

- Picking a specific cloud provider. The agent runtime decides.
- Specifying a wire codec for the audio bytes. The agent and client negotiate
  format strings (PCM16, Opus, G.711, etc.) at session start.
- Managing model selection. That stays in the existing ACP provider config.
- Telephony / SIP / PSTN. Out of scope for v1.

## Concepts

- A **voice stream** is opened inside an existing ACP session, scoped to one
  conversational turn.
- Audio flows in both directions during the stream, framed as `audioChunk`
  messages with monotonically increasing `seq` numbers.
- The client signals the end of the user's spoken turn with
  `voiceStreamUserTurnEnd`. The agent decides when to start speaking.
- The agent signals end-of-stream with `voiceStreamEnded` plus a reason code.
- A transcript text channel runs in parallel for both sides as ordinary ACP
  text events; voice does not replace text, it augments it.

## Lifecycle

```
Client                                  Agent
  |                                       |
  | voiceStreamStart {format, lang, ...}  |
  |-------------------------------------->|
  |                                       |
  | voiceStreamStarted {streamId, format} |
  |<--------------------------------------|
  |                                       |
  | userVoiceMessage {seq, audio}    *N   |
  |-------------------------------------->|
  |                                       |
  | voiceStreamUserTurnEnd                |
  |-------------------------------------->|
  |                                       |
  |       assistantVoiceMessage {seq, audio} *M
  |<--------------------------------------|
  |       agentMessageChunk {text}           *M (transcript)
  |<--------------------------------------|
  |                                       |
  |       voiceStreamEnded {reason}       |
  |<--------------------------------------|
```

Either side may send `voiceStreamCancel` at any point to abort.

## Methods (client → agent)

- `voiceStreamStart(sessionId, params)` →
  `{ streamId: string, accepted: { audioFormat: string, sampleRateHz: int } }`
- `voiceStreamUserTurnEnd(sessionId, streamId)` → `{ ok: bool }`
- `voiceStreamCancel(sessionId, streamId)` → `{ ok: bool }`

`params`:

- `audioFormat: "pcm16" | "opus" | "g711_ulaw" | "g711_alaw"`
- `sampleRateHz: 16000 | 24000 | 48000`
- `language?: string` (BCP-47, hint only)
- `instructions?: string` (system instructions for this stream only)

## Notifications (client → agent)

- `userVoiceMessage`
  - `sessionId: string`
  - `streamId: string`
  - `seq: int`            // monotonically increasing per stream
  - `audio: string`       // base64 of one chunk
  - `final?: bool`        // true on the last chunk before user-turn-end
  - `transcript?: string` // optional client-side ASR hint

## Notifications (agent → client)

- `voiceStreamStarted`
  - `sessionId, streamId, audioFormat, sampleRateHz`
- `assistantVoiceMessage`
  - `sessionId, streamId, seq, audio, final?`
- `voiceStreamEnded`
  - `sessionId, streamId, reason: "completed" | "cancelled" | "error" | "timeout"`
  - `errorMessage?: string`

## Permission semantics

`voiceStreamStart` is a permission-bearing call. The agent runtime must
treat it like any other tool that consumes microphone access:

- The first `voiceStreamStart` in a session triggers a `permissionRequest`
  with `kind: "voice"` unless the user has already granted persistent voice
  permission for this session or this agent.
- `permissionRequest.options` follows the existing ACP permission shape so
  the client can reuse its current approval UI.

## Mapping to ACP types (proposal)

The proposed wire-level discriminators map to the following Rust enum
variants once the spec is accepted and the variants are added to
`AcpUpdate` in `maho/crates/maho-acp/src/events.rs`:

| Proposed wire discriminator | Proposed Rust variant |
|---|---|
| `userVoiceMessage` | `AcpUpdate::UserVoiceMessage` |
| `assistantVoiceMessage` | `AcpUpdate::AssistantVoiceMessage` |
| `voiceStreamStarted` | `AcpUpdate::VoiceStreamStarted` |
| `voiceStreamEnded` | `AcpUpdate::VoiceStreamEnded` |

Earlier drafts of `events.rs` carried these variants as placeholders. They
were removed on 2026-06-18 as DEAD code (no agent emitted them) and will be
reintroduced together with the parsing branches when this proposal is
accepted upstream.

We propose adding two new methods (`voiceStreamStart`, `voiceStreamUserTurnEnd`,
`voiceStreamCancel`) and one new notification kind in the permission flow
(`kind: "voice"`).

## Out-of-band binary audio (optional)

Base64 inside JSON is fine for hundreds of milliseconds of PCM16 at 16 kHz,
but it doubles bandwidth. As an optional v1.1 extension, transports may
negotiate a side-channel binary frame format keyed by `streamId`. JSON-only
implementations remain conformant.

## Security considerations

- Microphone is a high-trust capability. Clients MUST surface the mic
  indicator while a stream is open.
- The agent runtime SHOULD NOT persist raw audio after a stream ends unless
  the client passed an explicit consent flag.
- Audio is treated as PII; transports MUST NOT log audio frames at info level
  or above.

## Open questions

- Should we standardize a "barge-in" message (agent stops speaking when the
  user starts) or leave that to the agent's VAD?
- Do we want optional Opus framing in v1, or restrict v1 to PCM16 for
  interoperability and add Opus later?
- Should we include explicit `usageUpdate` cost reporting per voice second,
  or keep usage reporting as agent-internal until the existing
  `usageUpdate` event is generalized?

## Status

This is a public proposal. The Maho team is the editor; any ACP implementer
may comment, fork, or contribute. Maho's `maho-acp` crate does not yet
implement either the client-side variants or the new client → agent methods;
both will be added once the proposal is accepted upstream.
