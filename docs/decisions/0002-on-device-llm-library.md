# 0002. On-Device LLM — Use OpenCode + Local OpenAI-Compatible Endpoint

Date: 2026-06-17 (revised)
Status: Supersedes the candle-bundling draft

## Context and Problem Statement

The original draft of this ADR proposed bundling the candle Rust inference
library into `maho-core` and exposing on-device models through a new
`LocalProvider` trait. On review that decision was rejected because:

1. Maho's AI panel does **not** call `maho-core`'s `LLMProvider` system. The
   panel uses `MahoOpenCodeAcpAdapter`, which talks to an OpenCode subprocess
   via the ACP protocol. Provider routing and BYOK already live inside
   OpenCode.
2. OpenCode already supports any OpenAI-compatible HTTP endpoint as a provider.
   Mature local model runners (Ollama, LM Studio, llama.cpp's built-in server,
   vLLM) all expose that surface natively. A user can run a local model and
   point Maho's BYOK config at `http://localhost:11434/v1` (Ollama default)
   today, with no Maho code change.
3. Bundling candle would mean: shipping >100 MB of inference code into the
   browser, owning a model download manager, adding mobile GPU/CPU backend
   complexity, owning model licensing curation, and maintaining a parallel
   provider system that the AI panel does not consume. None of that is
   justified when the OpenCode + local-server path is already complete.
4. The on-device LLM positioning (privacy, offline) is satisfied just as well
   by "configure Ollama, point Maho at localhost". The user's data never
   leaves their machine.

## Decision Drivers

- Avoid duplicate provider infrastructure parallel to OpenCode.
- Ship zero new Rust inference code.
- Keep the user's local model choice fully their own (which runner, which
  model, which hardware).
- Preserve BYOK as the single AI integration story (cloud BYOK and local
  BYOK are the same code path).
- Reduce launch blockers and maintenance surface.

## Considered Options

### A. Document Ollama / LM Studio / llama.cpp as the local path (Chosen)

- Ship zero inference code in Maho.
- Provide an onboarding doc: install Ollama → pull a model → set Maho AI
  panel BYOK to `http://localhost:11434/v1` plus the model id.
- Optional: pre-fill BYOK presets for the most common local runners.

### B. Bundle candle into `maho-core` (original draft, Rejected)

- Rejected because the AI panel does not use `maho-core`'s LLM providers.

### C. Bundle llama.cpp via FFI (Rejected)

- Same problem as B, plus C++ build complexity.

### D. Maho-owned Rust process wrapping a local model (Deferred)

- Functionally identical to running Ollama. Not justified vs. pointing at
  the user's existing Ollama install.

## Decision Outcome

Adopt **Option A**. No on-device inference code lives in this repository.
The deliverable is documentation, plus optional small UX affordances in the
AI panel BYOK preset list.

### Consequences

Positive:

- Zero inference code shipped, zero model download manager to maintain, zero
  mobile GPU backend scope.
- Local model use is unblocked today using stable upstream tooling.
- Privacy story stays clean: prompts never leave the device for local users.
- One BYOK code path covers both cloud and local.

Negative:

- The user has to run Ollama / LM Studio / llama.cpp themselves.
- Mobile-side local inference is out of scope: iOS and Android do not have a
  free production-grade local OpenAI-compatible server. Mobile users use
  cloud BYOK or wait for the ecosystem to catch up.

## Pros and Cons of the Options

| Option | Pros | Cons |
|---|---|---|
| A. Doc + Ollama / LM Studio (Chosen) | Zero code; ships now; matches BYOK story | Mobile local is gated on upstream tooling |
| B. Bundle candle (Rejected) | Single binary | Not used by AI panel; large maintenance surface |
| C. Bundle llama.cpp FFI (Rejected) | Mature runtime | Same problem as B + C++ build cost |
| D. Maho-owned local server (Deferred) | One-click parity | Duplicates Ollama; not justified |

## More Information

- Onboarding doc: `docs/onboarding/local-ai-with-opencode.md`.
- The dead Rust scaffolding the original draft motivated
  (`model_manager.rs`, `providers/local.rs`, `providers/openai_realtime.rs`,
  Swift / Kotlin `RealtimeVoiceProvider` files) has been deleted.
- `MahoCore::is_offline_llm_available()` returns `false` and points at the
  doc above.

## Review Triggers

- OpenCode adds an inference-bundling story we should adopt.
- Serious user demand for one-click on-device inference on mobile.
- A specific model with strict licensing or distribution constraints
  requires Maho-side curation we cannot pass through to the user.

Next review by default: 2027-01-15.
