# Maho Mobile → Arc Search v2.0 Parity Roadmap

**Created:** 2026-06-15
**Status:** Draft — Pending Momus Review
**Branch:** main (e2e5fbf)
**Scope:** iOS shell + Android shell + Rust core crates + relay agent extension

---

## 1. Executive Summary

Build Maho's mobile browsers (iOS ~15.7K LOC Swift, Android ~16.1K LOC Kotlin) from their current "basic tab browser with sync" state into a competitive AI-native mobile browser on par with a hypothetical "Arc Search v2.0" — defined by 22 features distilled from industry convergence (Perplexity Comet, Chrome auto-browse, Arc Search legacy, Opera Aria, Brave Leo Mobile, Safari Intelligence).

**Key architectural decisions already locked:**
- D1: `chat_session.rs` is the single mobile chat system; `llm_manager.rs` deprecated for mobile
- D4: Mobile agent = cloud-agent on `maho/relay/` (Playwright + headless Chromium + WebSocket task protocol)
- D5: BYOK forever — user supplies LLM API keys, Maho never subsidizes tokens
- D7: Page fetch 3-tier fallback: URLSession+Readability → relay fetcher → hidden WKWebView

**Timeline:** 8 waves, ~16-20 weeks for W0-W4 (core parity), W5-W8 stretch (differentiation + agent).
**Parallelism:** W0 (bug fixes) ∥ W1 (foundation) run concurrently from day 1.

---

## 2. Glossary

| Term | Definition |
|------|-----------|
| ACP | Agent Communication Protocol — `maho-ffi/src/acp.rs`, 1206 LOC. Full lifecycle exists but iOS sandbox blocks `Process::spawn` |
| BYOK | Bring Your Own Key — user provides their own LLM API keys |
| Cloud-agent | Headless browser agent running on relay server, iOS/Android are thin WebSocket clients |
| ChatContent | Proposed enum replacing `content: String` in chat types — gates multimodal (D3) |
| LLMProvider | Proposed trait abstracting LLM backends — first impl `OpenAICompatibleProvider` |
| MCP | Model Context Protocol — `mcp_manager.rs` is NOT real MCP, it's a local tool dispatcher (D6) |
| Relay | `maho/relay/` — currently pure store-and-forward sync; W8 extends with agent capabilities |
| Space | Maho's profile/workspace concept with per-space settings and personality |

---

## 3. Current State Assessment

### 3.1 What Exists (Verified)

| Component | Status | Evidence |
|-----------|--------|----------|
| iOS basic browsing | Working | WKWebView, tabs, bookmarks, history |
| Android basic browsing | Working | WebView, tabs, navigation |
| Sync relay | Working | `maho/relay/` store-and-forward |
| Embedding system | Production-ready | BGE-small-en-v1.5 INT8, fastembed/tract-onnx, 384-dim |
| Memory manager | Partial | Auto-extraction + HNSW + FTS, missing `space_id` FK |
| Chat session (Rust) | Exists, no mobile callers | `chat_session.rs` 354 LOC, streaming SSE + tool calls |
| ACP (Rust) | Exists, blocked on iOS | `acp.rs` 1206 LOC, needs cloud-agent path |
| Skills system | Exists | 6 builtin skills, no `tools` field |
| iOS FFI | 242 extern functions | C headers in bridging header, zero AI call sites |
| Android JNI | 134 functions | Zero AI functions exposed |

### 3.2 P0 Bugs (Wave 0)

| Bug | File | Impact |
|-----|------|--------|
| Android content blocker stub | `MahoBridge.kt` — `nativeCompileContentRules`/`nativeGetContentRules` return `"[]"` | No ad blocking on Android |
| Fake incognito (iOS) | Missing separate `WKWebsiteDataStore` | Privacy violation — shares cookies with normal mode |
| sendTabToDevice empty strings | `SyncSettingsView.swift` | Sync sends blank URL/title |
| Plaintext passwords | `maho-storage/sqlite.rs` | Security vulnerability — passwords stored as TEXT |
| Missing CAMERA permission | `AndroidManifest.xml` | Blocks visual search, QR scanning |

### 3.3 Critical Gaps to v2.0

- **Zero AI on mobile** — no chat, no summary, no agent, no tool calls
- **No LLM abstraction** — `chat_session.rs:111-114` hardcodes `content: String`
- **No JNI AI bridge** — 134 JNI functions, zero AI
- **No iOS AI bridge** — C headers exist but zero Swift call sites
- **No mobile agent path** — ACP blocked by iOS sandbox, relay is sync-only
- **No BYOK settings UI** — no way for users to enter API keys on mobile
- **No streaming chat UI** — neither platform has token-streaming display

---

## 4. Target Feature Set (22 Features)

Derived from industry convergence analysis across Perplexity Comet, Chrome auto-browse, Arc Search, Opera Aria, Brave Leo Mobile, Safari Intelligence.

| # | Feature | Category | Wave |
|---|---------|----------|------|
| F1 | Instant Answer (search → AI summary) | Core AI | W1 |
| F2 | Ask-about-tab (page context Q&A) | Core AI | W1 |
| F3 | Multi-turn chat with memory | Core AI | W2 |
| F4 | Streaming token display | Core AI | W1 |
| F5 | BYOK settings (API key management) | Core AI | W1 |
| F6 | Page summarization (LLM-based) | Content Intelligence | W1 |
| F7 | Smart tab grouping | Content Intelligence | W3 |
| F8 | Semantic history search | Content Intelligence | W1 |
| F9 | Auto-archive stale tabs | Content Intelligence | W3 |
| F10 | Visual search (camera → AI) | Multimodal | W3 |
| F11 | Voice input → AI | Multimodal | W3 |
| F12 | Screenshot → Ask AI | Multimodal | W3 |
| F13 | Per-space AI personality | Personalization | W4 |
| F14 | Browsing pattern learning | Personalization | W4 |
| F15 | Proactive suggestions | Personalization | W5 |
| F16 | Content blocker (real) | Privacy & Security | W0 |
| F17 | Real incognito mode | Privacy & Security | W0 |
| F18 | Encrypted credential storage | Privacy & Security | W0 |
| F19 | Privacy dashboard | Privacy & Security | W5 |
| F20 | Multi-step web agent | Agent | W8 |
| F21 | Form auto-fill via agent | Agent | W8 |
| F22 | Cross-app agent actions | Agent | W8 |

---

## 5. Architecture Spine

### 5.1 LLM Provider Abstraction (W1, gates everything)

```
┌─────────────────────────────────────────┐
│              LLMProvider trait           │
│  fn complete(&self, req) -> Stream       │
│  fn models(&self) -> Vec<ModelInfo>      │
│  fn supports_vision(&self) -> bool       │
│  fn supports_tools(&self) -> bool        │
└──────────┬──────────────────────────┬────┘
           │                          │
   OpenAICompatibleProvider    AnthropicProvider (W2)
   (OpenAI, Groq, Together,    (direct Anthropic API)
    OpenRouter, local Ollama)
```

**File:** `maho/crates/maho-core/src/llm_provider.rs` (new)
**Types:** `maho/crates/maho-types/src/llm.rs` (new)

### 5.2 ChatContent Migration (W1-W2, gates multimodal)

```rust
// maho/crates/maho-types/src/chat.rs — NEW
pub enum ChatContent {
    Text(String),
    Image { mime: String, data: Vec<u8> },      // W3
    Mixed(Vec<ChatContentPart>),                 // W3
}

// Migration: chat_session.rs line 111-114
// FROM: content: String
// TO:   content: ChatContent
```

### 5.3 Mobile Bridge Extension

```
iOS (Swift)                    Rust Core                   Android (Kotlin)
┌──────────┐    FFI (C)     ┌──────────────┐   JNI      ┌──────────────┐
│ ChatView  │◄──────────────│ chat_session  │───────────►│ ChatFragment │
│ BYOKView  │   +18 new     │ llm_provider  │  +22 new   │ BYOKActivity │
│ AgentView │   functions    │ tool_dispatch │  functions  │ AgentView    │
└──────────┘                └──────────────┘             └──────────────┘
```

**iOS FFI additions (W1):** `maho/crates/maho-ffi/src/lib.rs`
- `maho_llm_provider_create`, `maho_llm_provider_destroy`
- `maho_chat_session_create`, `maho_chat_send_message`, `maho_chat_stream_next`
- `maho_byok_set_key`, `maho_byok_get_providers`, `maho_byok_validate_key`
- `maho_summarize_page`, `maho_semantic_search`

**Android JNI additions (W1):** `maho/crates/maho-jni/src/lib.rs`
- Mirror of iOS FFI additions adapted for JNI conventions

### 5.4 Cloud Agent Architecture (W8)

```
┌─────────────┐     WebSocket      ┌──────────────────────────┐
│ iOS/Android  │◄─────────────────►│     maho/relay/           │
│ thin client  │   task protocol    │                          │
│              │                    │  ┌────────────────────┐  │
│ • task UI    │   status/stream    │  │  Agent Executor     │  │
│ • approval   │◄──────────────────│  │  • Playwright CDP   │  │
│ • result     │                    │  │  • headless Chrome  │  │
│   display    │                    │  │  • tool dispatch    │  │
└─────────────┘                    │  └────────────────────┘  │
                                   └──────────────────────────┘
```

**Why cloud-agent (D4):**
- iOS sandbox blocks `Process::spawn` — no local browser automation possible
- Only production mobile agentic browser (Perplexity Comet) uses cloud architecture
- Self-hosted relay option preserves privacy ("your keys, your data, your relay")
- BYOK applies to relay too — relay forwards user's API key to LLM providers

**Relay extensions needed:**
- `maho/relay/src/agent/` — new module
- `maho/relay/src/agent/executor.rs` — Playwright CDP task runner
- `maho/relay/src/agent/protocol.rs` — WebSocket task protocol (submit/status/approve/cancel)
- `maho/relay/src/agent/sandbox.rs` — resource limits, timeout, URL allowlist
- Dependencies: `playwright` (Rust bindings or subprocess), `tokio-tungstenite`

---

## 6. Wave Plan

### Wave 0 — Bug Fixes (2 weeks, parallel with W1)

> **Gate:** All P0 bugs resolved before any public beta.

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W0-1: Fix Android content blocker | `maho-jni/src/lib.rs`, `MahoBridge.kt` — wire real `compile_content_rules` / `get_content_rules` from `content_blocker.rs` | 2d | Load EasyList, verify blocked network requests in WebView |
| W0-2: Real iOS incognito | `ios-shell/` — create separate `WKWebsiteDataStore.nonPersistent()` for incognito tabs, isolate cookies/cache | 2d | Open incognito tab, log in to site, close, reopen — no session persists |
| W0-3: Fix sendTabToDevice | `SyncSettingsView.swift` — pass actual `webView.url` and `webView.title` instead of empty strings | 0.5d | Send tab from iOS, receive on desktop with correct URL/title |
| W0-4: Encrypted password storage | `maho-storage/sqlite.rs` — replace plaintext TEXT with encrypted blob (platform keychain for key, AES-256-GCM for data) | 3d | Verify passwords unreadable in raw SQLite, round-trip encrypt/decrypt test |
| W0-5: Add CAMERA permission (Android) | `AndroidManifest.xml` | 0.1d | Camera intent works |
| W0-6: Privacy toggle UI | Both shells — toggle for: block trackers, block cookies, clear on close | 2d | Toggle states persist, behavior matches |

### Wave 1 — AI Foundation (3 weeks, parallel with W0)

> **Gate:** User can enter API key, send a message, receive streamed response on both platforms.

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W1-1: `LLMProvider` trait + `OpenAICompatibleProvider` | New `maho-core/src/llm_provider.rs`, types in `maho-types/src/llm.rs` | 3d | Unit test: mock HTTP → streamed tokens, model listing, error handling |
| W1-2: BYOK storage (Rust) | `maho-storage/sqlite.rs` — new `api_keys` table with platform keychain encryption | 2d | Round-trip: store key → retrieve → use in provider |
| W1-3: Wire `chat_session.rs` to `LLMProvider` | `chat_session.rs` — replace hardcoded HTTP with provider dispatch | 2d | Integration test: chat session → provider → streamed response |
| W1-4: iOS FFI — chat + BYOK + summarize | `maho-ffi/src/lib.rs` — add ~12 new extern functions | 2d | C header generated, Swift can call each function |
| W1-5: Android JNI — chat + BYOK + summarize | `maho-jni/src/lib.rs` — add ~14 new JNI functions | 2d | Kotlin can call each function |
| W1-6: iOS BYOK settings UI | New `ios-shell/Views/BYOKSettingsView.swift` | 2d | Enter key → stored → appears in provider list |
| W1-7: Android BYOK settings UI | New Android settings screen | 2d | Same as iOS |
| W1-8: iOS streaming chat UI | New `ios-shell/Views/ChatView.swift` — token-by-token display, markdown rendering | 3d | Send message → see tokens appear progressively |
| W1-9: Android streaming chat UI | New Android chat composable | 3d | Same as iOS |
| W1-10: LLM-based page summarizer | `chat_session.rs` + replace `PageSummarizer.swift` extractive logic | 1d | Summarize page → coherent LLM summary vs old extractive output |
| W1-11: Semantic history search | Wire existing `embedding.rs` + `memory_manager.rs` to mobile via FFI/JNI | 2d | Search "that recipe site" → finds matching history entries |
| W1-12: Ask-about-tab (F2) | `chat_session.rs` — inject page content as context, page-fetch 3-tier fallback (D7) | 2d | Open article → "What does this say about X?" → accurate answer |

### Wave 2 — Multi-turn & Memory (2 weeks)

> **Gate:** Conversations persist across app restarts, context window managed properly.

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W2-1: Conversation persistence | `maho-storage/sqlite.rs` — `conversations` table with message history | 2d | Close app → reopen → conversation history intact |
| W2-2: Context window management | `chat_session.rs` — sliding window + summary of older messages | 2d | 50+ message conversation doesn't exceed token limit |
| W2-3: `ChatContent` enum (Phase 1 — text only) | `maho-types/src/chat.rs` — introduce enum, migrate `chat_session.rs` | 2d | All existing text chat works with new type, backward compat |
| W2-4: Anthropic provider | New `maho-core/src/providers/anthropic.rs` | 2d | Claude models work via BYOK |
| W2-5: Conversation UI (both platforms) | Chat history list, conversation picker, delete/rename | 2d | Navigate between conversations, delete old ones |

### Wave 3 — Multimodal & Smart Organization (3 weeks)

> **Gate:** Camera → AI works, tabs auto-organize, voice input functional.

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W3-1: `ChatContent` enum (Phase 2 — image) | `maho-types/src/chat.rs` — add `Image` + `Mixed` variants | 1d | Image content serializes correctly to vision API format |
| W3-2: Visual search — camera capture | iOS: AVCaptureSession, Android: CameraX + CAMERA permission (W0-5) | 3d | Capture photo → image bytes available |
| W3-3: Visual search — image → AI | Wire captured image → `ChatContent::Image` → vision-capable provider | 2d | Take photo of product → AI identifies it |
| W3-4: Screenshot → Ask AI (F12) | Platform screenshot API → same image → AI pipeline | 2d | Screenshot current page → "What's in this chart?" |
| W3-5: Voice input (F11) | iOS: Speech framework, Android: SpeechRecognizer → text → chat | 2d | Speak question → transcribed → AI responds |
| W3-6: Smart tab grouping (F7) | `embedding.rs` — embed tab titles/URLs → HNSW clustering → suggest groups | 3d | 20 open tabs → AI suggests 3-4 meaningful groups |
| W3-7: Auto-archive stale tabs (F9) | Background job: tabs untouched >7d → archive with embedding for retrieval | 2d | Old tabs disappear from tab bar, findable via search |

### Wave 4 — Personalization (2 weeks)

> **Gate:** Per-space AI personality, browsing patterns influence suggestions.

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W4-1: Add `space_id` FK to `memory_blocks` | `maho-storage/sqlite.rs` — migration | 1d | Memory entries scoped to space |
| W4-2: Per-space system prompt | `chat_session.rs` — prepend space-specific personality to system message | 1d | "Work" space uses professional tone, "Personal" is casual |
| W4-3: Browsing pattern extraction | New `pattern_manager.rs` — track frequent sites, time-of-day patterns, topic clusters | 3d | After 1 week of use, can identify "morning news routine" |
| W4-4: Learned preferences in AI context | Inject pattern summary into chat system prompt | 1d | AI knows user prefers Python over JavaScript without being told |
| W4-5: Mobile UI for space personality | Settings per space: name, tone, focus areas | 2d | Configure "Research" space with academic tone |

### Wave 5 — Proactive Intelligence (2 weeks)

> **Gate:** Browser suggests actions before user asks.

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W5-1: Proactive suggestion engine | New `suggestion_manager.rs` — triggers on: new tab, idle, pattern match | 3d | Open browser at 8am → "Read your morning news?" suggestion |
| W5-2: Suggestion UI (both platforms) | Non-intrusive chip/card below address bar | 2d | Suggestions appear, dismissable, tappable |
| W5-3: Privacy dashboard (F19) | UI showing: trackers blocked, data stored locally, AI usage stats | 3d | See "47 trackers blocked today", "0 bytes sent to cloud" |
| W5-4: Tool dispatch rename + enhancement | Rename `mcp_manager.rs` → `tool_dispatcher.rs` (D6), add `tools` field to skills | 2d | Existing 8 tools work, new tools registrable |

### Wave 6 — Polish & Performance (2 weeks)

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W6-1: Offline AI (small model) | Integrate Phi-3-mini or similar via GGUF on-device | 5d | Airplane mode → basic chat still works |
| W6-2: Token usage tracking | Per-provider token counting, cost estimation display | 2d | User sees "$0.03 used today" in settings |
| W6-3: Response caching | Cache identical queries (same page + same question = cached answer) | 2d | Re-ask same question → instant response, no API call |
| W6-4: Battery/memory optimization | Profile AI features on older devices, add quality toggles | 3d | iPhone 12 runs chat without thermal throttle |

### Wave 7 — Ecosystem Integration (2 weeks)

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W7-1: Share sheet AI actions | iOS: Share Extension, Android: Share target → summarize/chat about shared content | 3d | Share article from Twitter → Maho summarizes |
| W7-2: Widget — AI quick actions | iOS: WidgetKit, Android: App Widget → "Ask Maho" + recent suggestions | 3d | Home screen widget → tap → voice/text AI input |
| W7-3: Spotlight/App Search integration | iOS: CSSearchableItem, Android: App Indexing → AI conversations findable | 2d | System search "recipe conversation" → finds Maho chat |
| W7-4: Desktop ↔ Mobile AI sync | Sync conversations + memory blocks via relay | 2d | Start chat on desktop → continue on phone |

### Wave 8 — Mobile Agent (3 weeks)

> **Gate:** User can issue multi-step web task from phone, agent executes on relay, user approves actions.

| Task | File(s) | Effort | Verification |
|------|---------|--------|-------------|
| W8-1: Relay agent executor | `maho/relay/src/agent/executor.rs` — Playwright CDP runner with headless Chromium | 5d | Relay can navigate to URL, extract content, fill form in headless browser |
| W8-2: WebSocket task protocol | `maho/relay/src/agent/protocol.rs` — submit/status/approve/cancel/stream messages | 3d | Client submits task → receives status updates → approves action → gets result |
| W8-3: Agent sandbox & safety | `maho/relay/src/agent/sandbox.rs` — resource limits, 5min timeout, URL allowlist, action approval gates | 3d | Agent cannot: exceed memory limit, run >5min, visit blocked URLs, act without approval |
| W8-4: iOS agent client | New `ios-shell/Views/AgentView.swift` — task submission, status display, approval UI | 3d | Submit "book cheapest flight SFO→NRT next Friday" → see agent browsing → approve booking |
| W8-5: Android agent client | New Android agent composable — mirror iOS | 3d | Same as iOS |
| W8-6: Agent tool integration | Connect agent executor to `tool_dispatcher.rs` tools — web search, page fetch, form fill, click, extract | 2d | Agent uses tools in sequence to complete multi-step task |
| W8-7: Form auto-fill via agent (F21) | Agent detects form fields → proposes fill from user profile/memory → user approves | 2d | Navigate to checkout → agent fills shipping/payment → user confirms |
| W8-8: BYOK for relay agent | Agent uses user's API key forwarded via WebSocket auth — relay never stores keys | 1d | API key flows: mobile → relay WebSocket → LLM provider, relay has zero key persistence |

---

## 7. Decision Log

| ID | Decision | Rationale | Date |
|----|----------|-----------|------|
| D1 | `chat_session.rs` only for mobile; `llm_manager.rs` deprecated | chat_session has streaming SSE + tool calls, llm_manager is broker pattern that requires shell-owned HTTP — wrong for mobile FFI | 2026-06-14 |
| D2 | `LLMProvider` trait, first impl `OpenAICompatibleProvider` | Covers OpenAI, Groq, Together, OpenRouter, local Ollama with single implementation | 2026-06-14 |
| D3 | `ChatContent` enum migration (String → enum) | Hardcoded `content: String` at chat_session.rs:111-114 blocks all multimodal. Two-phase: text-only (W2) then image (W3) | 2026-06-14 |
| D4 | Cloud-agent on relay (path α) | iOS sandbox blocks Process::spawn, only production mobile agent (Perplexity Comet) uses cloud arch, self-hosted preserves privacy | 2026-06-14 |
| D5 | BYOK forever | User confirmed. No token subsidization, no usage-based pricing. API key management is first-class feature | 2026-06-14 |
| D6 | Rename mcp_manager.rs → tool_dispatcher.rs | Current code is NOT MCP (no JSON-RPC transport, no stdio/SSE, 8 hardcoded tools). Name is misleading | 2026-06-14 |
| D7 | Page fetch 3-tier fallback | 1) URLSession+Readability (fast, no JS) → 2) relay fetcher (handles JS-heavy) → 3) hidden WKWebView (last resort, slow) | 2026-06-14 |

---

## 8. Risk Register

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| iOS App Store rejection for "browser automation" language | Medium | High | Frame as "AI assistant" not "automation", agent runs on user's relay not in-app |
| BYOK UX friction (users don't have API keys) | High | Medium | Partner with OpenRouter for one-click signup, show cost estimates, W6 offline model as free fallback |
| Relay headless Chromium resource cost | Medium | Medium | Sandbox limits (5min, 512MB), user-provisioned relay, optional Maho-hosted tier |
| ChatContent migration breaks existing desktop chat | Low | High | Two-phase migration, backward-compat serde, exhaustive tests before merge |
| Vision API costs surprise users | Medium | Low | Show estimated cost before sending image, cache identical image queries |
| Android WebView limitations vs iOS WKWebView | Medium | Medium | Feature-flag per platform, graceful degradation |

---

## 9. Verification Strategy

### Per-Wave Gates

Each wave has a gate condition (stated in wave header). Wave N+1 cannot begin until Wave N's gate passes.
**Exception:** W0 ∥ W1 run in parallel (confirmed by user).

### Automated Tests

| Layer | Tool | Scope |
|-------|------|-------|
| Rust unit tests | `cargo test --workspace` (from `maho/`) | All new managers, providers, types |
| Rust integration tests | `cargo test --workspace` with test fixtures | Chat flow end-to-end, provider mock |
| Relay tests | `cargo test` (from `maho/relay/`) | Agent executor, protocol, sandbox |
| iOS build | Xcode build + SwiftUI previews | Compile check, UI preview |
| Android build | Gradle build | Compile check |
| Appium regression | `maho/appium-tests/` | E2E mobile flows post-wave |

### Manual Verification Checklist (per wave)

- [ ] Feature works on iOS simulator (latest iPhone)
- [ ] Feature works on Android emulator (API 34)
- [ ] BYOK key entry → AI response flows correctly
- [ ] No plaintext secrets in logs or storage
- [ ] Battery impact acceptable (no continuous background)
- [ ] Offline graceful degradation (no crash, helpful message)

---

## 10. File Impact Map

### New Files

| File | Wave | Purpose |
|------|------|---------|
| `maho-core/src/llm_provider.rs` | W1 | LLMProvider trait + OpenAICompatibleProvider |
| `maho-types/src/llm.rs` | W1 | Provider types, model info, streaming types |
| `maho-types/src/chat.rs` | W2 | ChatContent enum, message types |
| `maho-core/src/providers/anthropic.rs` | W2 | Anthropic provider |
| `maho-core/src/pattern_manager.rs` | W4 | Browsing pattern extraction |
| `maho-core/src/suggestion_manager.rs` | W5 | Proactive suggestions |
| `ios-shell/Views/BYOKSettingsView.swift` | W1 | API key management UI |
| `ios-shell/Views/ChatView.swift` | W1 | Streaming chat UI |
| `ios-shell/Views/AgentView.swift` | W8 | Agent task UI |
| `android-shell/.../BYOKSettingsActivity.kt` | W1 | API key management UI |
| `android-shell/.../ChatFragment.kt` | W1 | Streaming chat UI |
| `android-shell/.../AgentView.kt` | W8 | Agent task UI |
| `relay/src/agent/mod.rs` | W8 | Agent module |
| `relay/src/agent/executor.rs` | W8 | Playwright CDP runner |
| `relay/src/agent/protocol.rs` | W8 | WebSocket task protocol |
| `relay/src/agent/sandbox.rs` | W8 | Resource limits, safety |

### Modified Files (Major Changes)

| File | Wave | Changes |
|------|------|---------|
| `maho-core/src/chat_session.rs` | W1-W4 | Provider dispatch, context management, page context injection, space personality |
| `maho-ffi/src/lib.rs` | W1-W5 | +18 new extern functions for AI features |
| `maho-jni/src/lib.rs` | W1-W5 | +22 new JNI functions for AI features |
| `maho-storage/sqlite.rs` | W0-W4 | Encrypted passwords, api_keys table, conversations, space_id migration |
| `maho-core/src/mcp_manager.rs` | W5 | Rename to tool_dispatcher.rs, add tools field |
| `maho-core/src/memory_manager.rs` | W4 | Add space_id scoping |
| `maho-core/src/skills_manager.rs` | W5 | Add tools field to skills |
| `ios-shell/Views/SyncSettingsView.swift` | W0 | Fix sendTabToDevice |
| `android-shell/.../MahoBridge.kt` | W0-W1 | Fix content blocker, add AI JNI calls |
| `AndroidManifest.xml` | W0 | Add CAMERA permission |
| `relay/Cargo.toml` | W8 | Add playwright, tokio-tungstenite deps |

---

## 11. Dependency Chain

```
W0 (bugs) ──────────────────────────────────────────────────────────►
W1 (foundation) ───► W2 (multi-turn) ───► W3 (multimodal) ───► W4 (personalization)
                                                                       │
                     W5 (proactive) ◄──────────────────────────────────┘
                         │
                     W6 (polish) ───► W7 (ecosystem) ───► W8 (agent)
```

**Critical path:** W1 → W2 → W3 → W4 → W5 → W8
**Parallel tracks:** W0 ∥ W1, W6 can overlap with W5, W7 can overlap with W6

---

## 12. Out of Scope (Explicit)

- Path γ (Chromium fork to Android) — rejected, 6-12 months, derails everything
- Maho-hosted LLM inference — violates BYOK principle
- Desktop Chromium AI features — separate roadmap, different codebase
- MCP protocol implementation — premature, current tool system sufficient through W7
- Real-time collaboration — not in Arc Search v2.0 target set
- Maho Mail AI features — separate product, separate roadmap
