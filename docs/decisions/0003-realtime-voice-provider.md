# 0003. Realtime Voice — Defer to OpenCode / ACP Voice Extension

Date: 2026-06-17 (revised)
Status: Supersedes the OpenAI Realtime + RealtimeProvider draft

## Context and Problem Statement

The original draft of this ADR adopted OpenAI Realtime as the realtime voice
provider and proposed a new `RealtimeProvider` trait inside `maho-core`. On
review that decision was rejected because:

1. Maho's AI panel runs through `MahoOpenCodeAcpAdapter` → OpenCode
   subprocess. Adding a parallel realtime stack inside `maho-core` would
   duplicate OpenCode without any consumer wiring it up.
2. Voice realtime is fundamentally an agent-runtime concern. The right place
   for it is the agent runtime itself (OpenCode) or the protocol that
   connects Maho to it (ACP). Putting it in `maho-core` skips both layers.
3. ACP today has no voice messages. Adding voice is a protocol-level
   evolution, not a private Maho feature. P2-1 already commits Maho to
   leading the ACP spec; voice is exactly the kind of extension that
   should land there.

## Decision Drivers

- Keep AI integration single-path: OpenCode + ACP.
- Avoid building `maho-core` infrastructure that the AI panel does not call.
- Use the realtime voice work as P2-1 ecosystem leverage instead of as a
  private feature.

## Considered Options

### A. Author an ACP voice extension proposal and contribute it upstream (Chosen)

- Write an RFC-style spec extension under `maho/crates/maho-acp/spec/` adding
  voice frames + permission semantics + session lifecycle.
- Coordinate with OpenCode (and any other ACP implementer) to land the
  extension.
- Once accepted, OpenCode handles transport (OpenAI Realtime, Deepgram,
  ElevenLabs, etc.) the same way it handles cloud LLM provider choice today.
- Maho's `maho-acp` crate already carries placeholder voice events
  (`UserVoiceMessage`, `AssistantVoiceMessage`, `VoiceStreamStarted`,
  `VoiceStreamEnded`) so we can begin consuming voice frames as soon as the
  upstream extension lands.

### B. Implement RealtimeProvider in `maho-core` (original draft, Rejected)

- Rejected because nothing in the AI panel consumes `maho-core` providers.

### C. Maho-side WebRTC overlay parallel to OpenCode (Rejected)

- Two different agents driving the same UI; complex permission and turn-
  taking conflicts; against the single-AI-path principle.

## Decision Outcome

Adopt **Option A**. The realtime voice deliverable is a published ACP voice
extension proposal, the placeholder ACP event variants the crate already
carries, and a TODO doc marking the variants as "pending upstream extension".

### Consequences

Positive:

- One AI integration path stays one path.
- Voice work doubles as ACP ecosystem investment (P2-1).
- Maho is not on the hook for transport, billing, or model choice for voice;
  OpenCode + the user's BYOK config handle it once the extension is accepted.

Negative:

- Voice ships only after the ACP extension is accepted upstream and OpenCode
  implements it. We accept the gating as the cost of doing the work right.
- Until then, Maho's voice surface is documentation only.

## Pros and Cons of the Options

| Option | Pros | Cons |
|---|---|---|
| A. ACP voice extension (Chosen) | Single AI path; ecosystem leverage | Gated on upstream acceptance |
| B. RealtimeProvider in maho-core (Rejected) | "Looks like progress" | Dead code; AI panel does not call it |
| C. Maho-side WebRTC overlay (Rejected) | Maho ships voice unilaterally | Two agents; permission conflicts |

## More Information

- Spec extension proposal: `maho/crates/maho-acp/spec/4-voice-extension.md`.
- Placeholder voice event variants and `VoiceStreamEndReason` enum that
  earlier sat in `maho/crates/maho-acp/src/events.rs` were **removed**
  on 2026-06-18 as DEAD code (no agent ever emitted them). They will be
  reintroduced if and when the ACP voice extension is accepted upstream.
- `OpenAIRealtimeProvider`, `RealtimeProvider`, and `VoiceSession` traits
  proposed in the original draft were deleted as dead code.
- Native shell protocol stubs (`RealtimeVoiceProvider.swift` and `.kt`) were
  also deleted for the same reason.
- The single-shot `VoiceSearchManager` paths on iOS and Android remain
  untouched; they are independent of realtime voice.

## Review Triggers

- ACP voice extension is accepted upstream; we move from "deferred" to
  "ship".
- OpenCode signals it will not add voice; we revisit Option C with eyes
  open.
- A specific user contract requires a private realtime path that cannot
  wait for the spec to clear committee.

Next review by default: 2027-01-15.
