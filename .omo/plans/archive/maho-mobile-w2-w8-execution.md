
**Created:** 2026-06-16
**Depends on:** `.omo/plans/maho-mobile-arcsearch-v2.md` (parent roadmap)
**Prerequisite:** W0 + W1 COMPLETE
**Branch strategy:** `mobile/w{N}-{feature-slug}` per task, squash-merge to `mobile/main`

---

## 0. Commit Strategy & TDD Protocol

### Atomic Commit Convention
```
{wave}/{task-id}: {verb} {subject}

Body: 1-2 sentence description of what changed and why.
Test: {test file or command that proves it works}
```

Examples:
```
w2/W2-1a: add ChatContent enum to maho-types/src/chat.rs
w2/W2-2b: implement AnthropicProvider with streaming SSE
w8/W8-1c: add agent executor module to relay server
```

### TDD Protocol (Every Task)
1. **RED**: Write failing test first (unit test for Rust, compile test for FFI/JNI)
2. **GREEN**: Minimal implementation to pass
3. **REFACTOR**: Clean up, ensure `cargo clippy -- -D warnings` passes
4. **VERIFY**: Run full wave verification command

### Branch Rules
- One branch per task group (parallel group = one branch)
- Squash-merge to `mobile/main` when group is verified
- Tag `w{N}-gate` after wave gate passes

---

## 1. WAVE 2 — Multi-turn & Memory (2 weeks)

### Gate Condition
> Conversations persist across app restarts. Context window is managed.
> Anthropic models work via BYOK. Chat history navigable on both platforms.

### Task Graph

#### Parallel Group A (Independent foundations — run simultaneously)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W2-1a | Define `ChatContent` enum (text-only phase) | `maho-types/src/chat.rs` | Rust types | `quick` | W1 complete | `cargo test -p maho-types` — serde round-trip test |
| W2-1b | Define `ChatMessage` typed struct | `maho-types/src/chat.rs` | Rust types | `quick` | W2-1a | `cargo test -p maho-types` — message serialization |
| W2-1c | Add `ConversationMeta` type | `maho-types/src/chat.rs` | Rust types | `quick` | None | Unit test for struct creation |

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W2-2a | Implement `AnthropicProvider` struct | `maho-core/src/providers/anthropic.rs` (new) | Rust core | `deep` | W1-1 (`LLMProvider` trait) | Mock HTTP test: Anthropic SSE format → `LLMStreamChunk` |
| W2-2b | Anthropic streaming SSE parser | `maho-core/src/providers/anthropic.rs` | Rust core | `deep` | W2-2a | Test: multi-chunk SSE → tokens + tool_calls |
| W2-2c | Wire Anthropic into provider dispatch | `maho-core/src/chat_session.rs` L211-219 | Rust core | `small-impl` | W2-2a, W2-2b | Integration test: ChatConfig with anthropic endpoint → correct provider |
| W2-2d | Fix `llm_client.rs` Anthropic auth | `maho-core/src/llm_client.rs` L41-67 | Rust core | `quick` | None | Test: Anthropic memory calls use `x-api-key` header |

#### Parallel Group B (Storage — can start with Group A)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W2-3a | Add `conversation_sessions` table (parent grouping) | `maho-storage/src/sqlite.rs` (schema) | Rust storage | `small-impl` | None | Migration test: table created with correct columns |
| W2-3b | Add `list_conversations()` method | `maho-storage/src/sqlite.rs` | Rust storage | `small-impl` | W2-3a | Test: insert 3 conversations → list returns all, ordered by last_message_at |
| W2-3c | Add `get_conversation_messages(session_id)` | `maho-storage/src/sqlite.rs` | Rust storage | `small-impl` | W2-3a | Test: retrieve messages for specific session |
| W2-3d | Add `save_message(session_id, role, content)` | `maho-storage/src/sqlite.rs` | Rust storage | `quick` | W2-3a | Test: round-trip save + retrieve |
| W2-3e | Add `delete_conversation(session_id)` | `maho-storage/src/sqlite.rs` | Rust storage | `quick` | W2-3a | Test: delete cascade removes messages |
| W2-3f | Add `rename_conversation(session_id, title)` | `maho-storage/src/sqlite.rs` | Rust storage | `quick` | W2-3a | Test: rename persists |

#### Parallel Group C (After A + B foundations)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W2-4a | Migrate `ChatSession.history` to use `ChatContent` | `maho-core/src/chat_session.rs` L70, L110-114 | Rust core | `small-impl` | W2-1a, W2-1b | Existing test still passes + new typed test |
| W2-4b | Persist messages to SQLite on each turn | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | W2-3d, W2-4a | Test: send_user_turn → row in conversations table |
| W2-4c | Load history from SQLite on session resume | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | W2-3c, W2-4b | Test: new ChatSession(existing_id) → pre-populated history |

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W2-5a | Context window: token counting heuristic | `maho-core/src/chat_session.rs` (new fn) | Rust core | `quick` | None | Test: `estimate_tokens("hello world") → ~2-3` |
| W2-5b | Context window: sliding window + summarization | `maho-core/src/chat_session.rs` | Rust core | `deep` | W2-5a, W2-4a | Test: 60-msg history → truncated to fit 128k window |

#### Parallel Group D (Mobile UI — after backend groups)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W2-6a | iOS FFI: conversation CRUD functions | `maho-ffi/src/lib.rs` | FFI bridge | `small-impl` | W2-3a–f | C header generated, compiles |
| W2-6b | Android JNI: conversation CRUD functions | `maho-jni/src/lib.rs` | JNI bridge | `small-impl` | W2-3a–f | JNI compiles, Kotlin callable |
| W2-6c | iOS conversation list UI | `ios-shell/Views/ConversationListView.swift` (new) | iOS UI | `visual-engineering` | W2-6a | SwiftUI preview renders list |
| W2-6d | Android conversation list UI | `android-shell/.../ui/chat/ConversationListScreen.kt` (new) | Android UI | `visual-engineering` | W2-6b | Compose preview renders |
| W2-6e | iOS conversation picker + rename/delete | `ios-shell/Views/ConversationListView.swift` | iOS UI | `visual-engineering` | W2-6c | Swipe-to-delete works |
| W2-6f | Android conversation picker + rename/delete | `android-shell/.../ui/chat/ConversationListScreen.kt` | Android UI | `visual-engineering` | W2-6d | Long-press menu works |

### W2 Parallelism Diagram
```
Start ─┬── Group A (types + anthropic) ──────────────────────┐
       │   W2-1a → W2-1b → W2-1c                            │
       │   W2-2a → W2-2b → W2-2c                            ├── Group C ──┬── Group D
       │   W2-2d (independent)                               │   W2-4a–c   │   W2-6a–f
       │                                                     │   W2-5a–b   │
       └── Group B (storage) ────────────────────────────────┘             │
           W2-3a → W2-3b, W2-3c, W2-3d, W2-3e, W2-3f (parallel)          │
                                                                           └── Gate ✓
```

### Cross-Wave Outputs
- `ChatContent` enum → gates W3-1 (image variant)
- `AnthropicProvider` → unblocks Claude users for all subsequent waves
- `conversation_sessions` table → gates W7-4 (sync conversations)
- Persistence layer → required for W4 (personalization learns from history)

---

## 2. WAVE 3 — Multimodal & Smart Organization (3 weeks)

### Gate Condition
> Camera → AI works on both platforms. Voice input flows to chat.
> Tabs auto-group by semantic similarity. Stale tabs auto-archive.

### Task Graph

#### Parallel Group A (ChatContent Phase 2 + Core multimodal)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W3-1a | Add `Image` + `Mixed` variants to `ChatContent` | `maho-types/src/chat.rs` | Rust types | `quick` | W2-1a (text variant exists) | Serde test: Image variant round-trips |
| W3-1b | Add `ImageDelta` variant to `LLMStreamChunk` | `maho-types/src/llm.rs` | Rust types | `quick` | None | Unit test |
| W3-1c | Serialize `ChatContent::Image` to OpenAI vision format | `maho-core/src/llm_provider.rs` | Rust core | `small-impl` | W3-1a | Test: Image message → correct `image_url` JSON |
| W3-1d | Serialize `ChatContent::Image` to Anthropic vision format | `maho-core/src/providers/anthropic.rs` | Rust core | `small-impl` | W3-1a, W2-2a | Test: Image → `type: "image", source: {type: "base64", ...}` |

#### Parallel Group B (Camera infrastructure — fully independent)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W3-2a | iOS CameraManager: AVCaptureSession setup | `ios-shell/Support/CameraManager.swift` (new) | iOS UI | `deep` | None | Photo captured → Data returned |
| W3-2b | iOS CameraManager: PHPicker fallback (gallery) | `ios-shell/Support/CameraManager.swift` | iOS UI | `small-impl` | W3-2a | Pick photo from library → Data |
| W3-2c | Android CameraManager: CameraX capture | `android-shell/.../support/CameraManager.kt` (new) | Android UI | `deep` | None | Photo captured → ByteArray |
| W3-2d | Android CameraManager: gallery picker fallback | `android-shell/.../support/CameraManager.kt` | Android UI | `small-impl` | W3-2c | Pick photo → ByteArray |

#### Parallel Group C (Voice → Chat — uses existing infra)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W3-3a | iOS: Wire VoiceSearchManager output → ChatSession.send_user_turn | `ios-shell/Views/ChatView.swift` | iOS UI | `small-impl` | W1-8 (chat UI exists) | Speak → transcribed text appears as user message |
| W3-3b | Android: Wire VoiceSearchManager output → ChatSession | `android-shell/.../ui/chat/ChatScreen.kt` | Android UI | `small-impl` | W1-9 (chat UI exists) | Same as iOS |

#### Parallel Group D (Screenshot → AI)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W3-4a | iOS screenshot capture handler | `ios-shell/Bridge/MahoBridge+Screenshots.swift` (new) | iOS UI | `small-impl` | None | Capture current WKWebView → PNG Data |
| W3-4b | Android screenshot capture handler | `android-shell/.../bridge/BridgeScreenshots.kt` (new) | Android UI | `small-impl` | None | Capture current WebView → ByteArray |
| W3-4c | iOS: screenshot → ChatContent::Image → send | `ios-shell/Views/ChatView.swift` | iOS UI | `small-impl` | W3-4a, W3-1a | Screenshot button → image in chat |
| W3-4d | Android: screenshot → ChatContent::Image → send | `android-shell/.../ui/chat/ChatScreen.kt` | Android UI | `small-impl` | W3-4b, W3-1a | Same |

#### Parallel Group E (Visual search end-to-end — after B + A)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W3-5a | iOS FFI: image message function | `maho-ffi/src/lib.rs` | FFI bridge | `quick` | W3-1a | `maho_chat_send_image(session, bytes, len, mime)` compiles |
| W3-5b | Android JNI: image message function | `maho-jni/src/lib.rs` | JNI bridge | `quick` | W3-1a | `nativeChatSendImage(sessionPtr, bytes, mime)` callable |
| W3-5c | iOS: camera → capture → ChatContent::Image → vision provider | `ios-shell/Views/VisualSearchView.swift` (new) | iOS UI | `visual-engineering` | W3-2a, W3-5a, W3-1c | Take photo of object → AI describes it |
| W3-5d | Android: camera → ChatContent::Image → vision provider | `android-shell/.../ui/chat/VisualSearchScreen.kt` (new) | Android UI | `visual-engineering` | W3-2c, W3-5b, W3-1c | Same |

#### Parallel Group F (Smart tabs — independent of multimodal)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W3-6a | Tab embedding: embed title+URL via existing `embedding.rs` | `maho-core/src/embedding.rs` (extend) | Rust core | `small-impl` | None | Test: embed 20 tab titles → 20 384-dim vectors |
| W3-6b | HNSW clustering: group similar tabs | `maho-core/src/tab_grouping.rs` (new) | Rust core | `deep` | W3-6a | Test: 20 tabs → 3-5 clusters with sensible names |
| W3-6c | Auto-archive: background job for stale tabs | `maho-core/src/tab_lifecycle.rs` (extend) | Rust core | `small-impl` | None | Test: tab untouched 7d → marked for archive |
| W3-6d | iOS FFI: tab grouping suggestions | `maho-ffi/src/lib.rs` | FFI bridge | `quick` | W3-6b | Function compiles |
| W3-6e | Android JNI: tab grouping suggestions | `maho-jni/src/lib.rs` | JNI bridge | `quick` | W3-6b | Function callable |
| W3-6f | iOS UI: tab group suggestion card | `ios-shell/Views/TabGroupSuggestionView.swift` (new) | iOS UI | `visual-engineering` | W3-6d | Card renders with group names |
| W3-6g | Android UI: tab group suggestion card | `android-shell/.../ui/tabs/TabGroupSuggestion.kt` (new) | Android UI | `visual-engineering` | W3-6e | Compose preview renders |

### W3 Parallelism Diagram
```
Start ─┬── Group A (ChatContent Image) ──────────┬── Group E (visual search E2E)
       │   W3-1a → W3-1c, W3-1d                  │   W3-5a–d
       │   W3-1b                                  │
       ├── Group B (camera infra) ────────────────┘
       │   W3-2a → W3-2b (iOS)
       │   W3-2c → W3-2d (Android)
       ├── Group C (voice → chat) ──── independent
       │   W3-3a (iOS), W3-3b (Android)
       ├── Group D (screenshot → AI) ─── after A
       │   W3-4a, W3-4b → W3-4c, W3-4d
       └── Group F (smart tabs) ──────── fully independent
           W3-6a → W3-6b → W3-6d,e → W3-6f,g
           W3-6c (independent)
```

### Cross-Wave Outputs
- `ChatContent::Image` → foundation for all future multimodal features
- Camera infra → reusable for QR scanning, document scanning
- Tab grouping engine → input for W4 pattern extraction
- Auto-archive → feeds W5 proactive suggestions ("archived tabs you might want")

---

## 3. WAVE 4 — Personalization (2 weeks)

### Gate Condition
> Per-space AI personality works. Browsing patterns extracted and visible.
> AI responses differ by space context without manual configuration.

### Task Graph

#### Parallel Group A (Schema migrations)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W4-1a | Add `space_id` column to `memory_blocks` table | `maho-storage/src/sqlite.rs` (migration) | Rust storage | `quick` | None | Migration runs; existing data gets NULL space_id |
| W4-1b | Add `space_id` column to `memories` table | `maho-storage/src/sqlite.rs` (migration) | Rust storage | `quick` | None | Same |
| W4-1c | Add `space_id` column to `browse_sessions` table | `maho-storage/src/sqlite.rs` (migration) | Rust storage | `quick` | None | Same |
| W4-1d | Add `ai_config` field to `Space` struct | `maho-types/src/space.rs` L310 | Rust types | `quick` | None | Serde test: Space with/without ai_config |
| W4-1e | Define `SpaceAIConfig` struct | `maho-types/src/space.rs` | Rust types | `quick` | None | Fields: `system_prompt`, `tone`, `focus_areas`, `preferred_model` |

#### Parallel Group B (Space-scoped memory operations)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W4-2a | Update `upsert_memory_block` to accept `space_id` | `maho-storage/src/sqlite.rs` | Rust storage | `small-impl` | W4-1a | Test: memory block with space_id persists |
| W4-2b | Update `list_memory_blocks_by_tier` to filter by space | `maho-storage/src/sqlite.rs` | Rust storage | `small-impl` | W4-1a | Test: returns only matching space's blocks |
| W4-2c | Update `build_personalization_system_prompt` | `maho-core/src/maho_core.rs` L6088+ | Rust core | `small-impl` | W4-2b | Test: prompt includes space-specific memories only |

#### Parallel Group C (Per-space AI personality)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W4-3a | Inject `SpaceAIConfig.system_prompt` into ChatSession | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | W4-1d, W4-1e | Test: "Work" space → professional system prompt prepended |
| W4-3b | Space context in chat: auto-include space name + focus | `maho-core/src/chat_session.rs` | Rust core | `quick` | W4-3a | Test: chat in "Research" space → system prompt mentions research focus |

#### Parallel Group D (Browsing pattern extraction)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W4-4a | `PatternManager` struct + trait | `maho-core/src/pattern_manager.rs` (new) | Rust core | `deep` | None | Struct compiles with pattern tracking fields |
| W4-4b | Frequent site tracking | `maho-core/src/pattern_manager.rs` | Rust core | `small-impl` | W4-4a | Test: 10 visits to github.com → pattern detected |
| W4-4c | Time-of-day pattern detection | `maho-core/src/pattern_manager.rs` | Rust core | `small-impl` | W4-4a | Test: news sites at 8am daily → "morning news" pattern |
| W4-4d | Topic clustering from browsing history | `maho-core/src/pattern_manager.rs` | Rust core | `deep` | W4-4a, W3-6a (embeddings) | Test: 50 tech articles → "technology" cluster |
| W4-4e | Pattern → memory block persistence | `maho-core/src/pattern_manager.rs` | Rust core | `small-impl` | W4-4b, W4-2a | Test: detected pattern → stored in memory_blocks with space_id |

#### Parallel Group E (Learned preferences in AI context)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W4-5a | Pattern summary injection into system prompt | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | W4-4e, W4-3a | Test: pattern "prefers Python" → AI mentions Python first |
| W4-5b | Usage frequency → AI context | `maho-core/src/command_bar.rs` L133 + `chat_session.rs` | Rust core | `quick` | W4-5a | Test: top 5 actions included in context |

#### Parallel Group F (Mobile UI)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W4-6a | iOS FFI: space AI config functions | `maho-ffi/src/lib.rs` | FFI bridge | `quick` | W4-1d | Compiles |
| W4-6b | Android JNI: space AI config functions | `maho-jni/src/lib.rs` | JNI bridge | `quick` | W4-1d | Compiles |
| W4-6c | iOS: space personality settings UI | `ios-shell/Views/SpaceAIConfigView.swift` (new) | iOS UI | `visual-engineering` | W4-6a | Configure tone, focus areas per space |
| W4-6d | Android: space personality settings UI | `android-shell/.../ui/spaces/SpaceAIConfigScreen.kt` (new) | Android UI | `visual-engineering` | W4-6b | Same |

### W4 Parallelism
```
Start ─┬── Group A (schema) ─────────── Group B (space-scoped ops) ── Group C (personality)
       │   W4-1a–e (all parallel)       W4-2a–c                       W4-3a–b
       │                                                                   │
       ├── Group D (patterns) ─── independent ─────────────── Group E (AI context)
       │   W4-4a → W4-4b,c,d → W4-4e                         W4-5a → W4-5b
       │                                                           │
       └── Group F (mobile UI) ──── after schema ─────────────────└── Gate ✓
           W4-6a–d
```

---

## 4. WAVE 5 — Proactive Intelligence (2 weeks)

### Gate Condition
> Browser proactively suggests actions. Privacy dashboard shows AI usage.
> Tool system renamed and extensible.

### Task Graph

#### Parallel Group A (Suggestion engine — core)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W5-1a | `SuggestionManager` struct + trigger system | `maho-core/src/suggestion_manager.rs` (new) | Rust core | `deep` | W4-4a (PatternManager) | Struct compiles, trigger enum defined |
| W5-1b | Trigger: new tab → suggest based on time-of-day pattern | `maho-core/src/suggestion_manager.rs` | Rust core | `small-impl` | W5-1a, W4-4c | Test: 8am new tab → "morning news" suggestion |
| W5-1c | Trigger: idle → suggest based on browsing context | `maho-core/src/suggestion_manager.rs` | Rust core | `small-impl` | W5-1a, W4-4d | Test: on tech site idle → suggest related resource |
| W5-1d | Trigger: pattern match → preemptive action | `maho-core/src/suggestion_manager.rs` | Rust core | `small-impl` | W5-1a, W4-4b | Test: daily github check → "open GitHub?" at usual time |
| W5-1e | Suggestion ranking + deduplication | `maho-core/src/suggestion_manager.rs` | Rust core | `small-impl` | W5-1b–d | Test: 3 triggers → ranked list, no duplicates |

#### Parallel Group B (Tool dispatch refactor — D6)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W5-2a | Rename `mcp_manager.rs` → `tool_dispatcher.rs` | `maho-core/src/tool_dispatcher.rs` (rename) | Rust core | `quick` | None | `cargo check` passes, all imports updated |
| W5-2b | Add public `register_tool()` method | `maho-core/src/tool_dispatcher.rs` | Rust core | `small-impl` | W5-2a | Test: register custom tool → appears in schema |
| W5-2c | Add `tools: Vec<ToolDescriptor>` field to `Skill` struct | `maho-core/src/skills_manager.rs` L6 | Rust core | `quick` | W5-2a | Test: skill with tools → tools registered on activation |
| W5-2d | Wire skills → tool_dispatcher registration | `maho-core/src/skills_manager.rs` | Rust core | `small-impl` | W5-2b, W5-2c | Test: activate skill → its tools available |

#### Parallel Group C (Privacy dashboard)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W5-3a | Add AI-specific fields to `PrivacySettings` | `maho-types/src/settings.rs` L169 | Rust types | `quick` | None | Fields: `ai_data_sharing`, `local_processing_only`, `ai_history_enabled` |
| W5-3b | AI usage stats tracking (tokens sent, API calls) | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | None | Test: after chat → stats incremented |
| W5-3c | Privacy stats aggregation | `maho-storage/src/sqlite.rs` (new table `ai_usage_stats`) | Rust storage | `small-impl` | W5-3b | Test: query daily/weekly/monthly token usage |
| W5-3d | iOS FFI: privacy + suggestion functions | `maho-ffi/src/lib.rs` | FFI bridge | `quick` | W5-3a, W5-1a | Compiles |
| W5-3e | Android JNI: privacy + suggestion functions | `maho-jni/src/lib.rs` | JNI bridge | `quick` | W5-3a, W5-1a | Compiles |

#### Parallel Group D (Mobile UI)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W5-4a | iOS: suggestion chip UI (below address bar) | `ios-shell/Views/SuggestionChipView.swift` (new) | iOS UI | `visual-engineering` | W5-3d | Chip renders, dismissable, tappable |
| W5-4b | Android: suggestion chip UI | `android-shell/.../ui/suggestions/SuggestionChip.kt` (new) | Android UI | `visual-engineering` | W5-3e | Compose preview renders |
| W5-4c | iOS: privacy dashboard screen | `ios-shell/Views/PrivacyDashboardView.swift` (new) | iOS UI | `visual-engineering` | W5-3d | Shows trackers blocked, AI usage, data location |
| W5-4d | Android: privacy dashboard screen | `android-shell/.../ui/privacy/PrivacyDashboard.kt` (new) | Android UI | `visual-engineering` | W5-3e | Same |

### W5 Parallelism
```
Start ─┬── Group A (suggestions) ────────── Group D (UI)
       │   W5-1a → W5-1b,c,d → W5-1e       W5-4a,b,c,d
       ├── Group B (tool dispatch) ──────── independent
       │   W5-2a → W5-2b,c → W5-2d
       └── Group C (privacy) ────────────── feeds Group D
           W5-3a → W5-3b → W5-3c → W5-3d,e
```

---

## 5. WAVE 6 — Polish & Performance (2 weeks)

### Gate Condition
> Offline AI works (basic chat without internet). Token usage visible.
> Response caching reduces API calls. Battery impact acceptable on iPhone 12.

### Task Graph

#### Parallel Group A (Offline AI — critical differentiator)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W6-1a | Add `llama-cpp-rs` dependency to maho-core | `maho/Cargo.toml`, `maho-core/Cargo.toml` | Rust core | `quick` | None | Compiles with feature flag `offline-llm` |
| W6-1b | `LocalLLMProvider` implementing `LLMProvider` trait | `maho-core/src/providers/local.rs` (new) | Rust core | `deep` | W6-1a | Test: load Gemma-3-1B-Q4_K_M → generate tokens |
| W6-1c | Model download manager (background fetch + progress) | `maho-core/src/model_download.rs` (new) | Rust core | `deep` | W6-1a | Test: download 0.8GB model → stored at correct path |
| W6-1d | Auto-fallback: no API key / no network → local model | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | W6-1b | Test: airplane mode → local model used |
| W6-1e | iOS FFI: model download + local inference | `maho-ffi/src/lib.rs` | FFI bridge | `small-impl` | W6-1b, W6-1c | Functions compile |
| W6-1f | Android JNI: model download + local inference | `maho-jni/src/lib.rs` | JNI bridge | `small-impl` | W6-1b, W6-1c | Functions callable |
| W6-1g | iOS: model download UI + offline indicator | `ios-shell/Views/OfflineModelView.swift` (new) | iOS UI | `visual-engineering` | W6-1e | Download progress bar, "Offline AI ready" badge |
| W6-1h | Android: model download UI + offline indicator | `android-shell/.../ui/settings/OfflineModelScreen.kt` (new) | Android UI | `visual-engineering` | W6-1f | Same |

#### Parallel Group B (Token tracking + cost estimation)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W6-2a | Proper token counting (tiktoken-rs or cl100k heuristic) | `maho-core/src/token_counter.rs` (new) | Rust core | `small-impl` | None | Test: "Hello world" → 2 tokens (GPT-4 tokenizer) |
| W6-2b | Per-provider cost table | `maho-core/src/token_counter.rs` | Rust core | `quick` | W6-2a | Test: 1000 tokens GPT-4o → $0.005 input |
| W6-2c | Accumulate usage per session + per day | `maho-storage/src/sqlite.rs` | Rust storage | `small-impl` | W6-2a | Test: 3 messages → total tokens tracked |
| W6-2d | iOS: usage display in settings | `ios-shell/Views/BYOKSettingsView.swift` (modify) | iOS UI | `quick` | W6-2c | "$0.03 used today" visible |
| W6-2e | Android: usage display in settings | `android-shell/.../ui/settings/` | Android UI | `quick` | W6-2c | Same |

#### Parallel Group C (Response caching)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W6-3a | Cache key computation (page URL + query hash) | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | None | Test: same page + same question → same cache key |
| W6-3b | Cache storage (SQLite table `ai_response_cache`) | `maho-storage/src/sqlite.rs` | Rust storage | `small-impl` | W6-3a | Test: store + retrieve cached response |
| W6-3c | Cache hit path (skip API call) | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | W6-3a, W6-3b | Test: identical query → instant response, no HTTP |
| W6-3d | Cache TTL + invalidation | `maho-core/src/chat_session.rs` | Rust core | `quick` | W6-3c | Test: cached response expires after 24h |

#### Parallel Group D (Battery optimization)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W6-4a | Quality toggles (low/medium/high AI quality) | `maho-types/src/settings.rs` | Rust types | `quick` | None | Settings field added |
| W6-4b | Low-quality mode: smaller model, fewer tokens | `maho-core/src/chat_session.rs` | Rust core | `small-impl` | W6-4a | Test: low quality → max_tokens=256, model=gpt-3.5 |
| W6-4c | Background AI throttling | `maho-core/src/suggestion_manager.rs` | Rust core | `quick` | W5-1a | Test: low battery → suggestions paused |
| W6-4d | iOS: quality toggle in settings | `ios-shell/Views/` | iOS UI | `quick` | W6-4a | Toggle persists |
| W6-4e | Android: quality toggle in settings | `android-shell/.../` | Android UI | `quick` | W6-4a | Toggle persists |

### W6 Parallelism
```
Start ─┬── Group A (offline AI) ──── sequential within, parallel with B/C/D
       │   W6-1a → W6-1b,c → W6-1d → W6-1e,f → W6-1g,h
       ├── Group B (token tracking) ── parallel with A/C/D
       │   W6-2a → W6-2b,c → W6-2d,e
       ├── Group C (caching) ── parallel with A/B/D
       │   W6-3a → W6-3b → W6-3c → W6-3d
       └── Group D (battery) ── parallel with all
           W6-4a → W6-4b,c → W6-4d,e
```

### Cross-Wave Outputs
- Offline LLM → enables W8 agent fallback (simple tasks without cloud)
- Token counter → used by W8 BYOK relay cost tracking
- Quality toggles → respected by all subsequent waves

---

## 6. WAVE 7 — Ecosystem Integration (2 weeks)

### Gate Condition
> Share sheet AI actions work. Widget launches AI. Spotlight finds conversations.
> Conversations sync across devices.

### Task Graph

#### Parallel Group A (Share sheet AI)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W7-1a | iOS: Unify two ShareExtension implementations | `ios-shell/ShareExtension/ShareViewController.swift` | iOS UI | `small-impl` | None | One entry point, handles URL + text + image |
| W7-1b | iOS: Share → summarize action | `ios-shell/ShareExtension/ShareViewController.swift` | iOS UI | `small-impl` | W7-1a | Share article → AI summary in extension UI |
| W7-1c | iOS: Share → "Ask Maho about this" action | `ios-shell/ShareExtension/ShareViewController.swift` | iOS UI | `small-impl` | W7-1a | Share → opens chat with content as context |
| W7-1d | Android: Share target → AI actions | `android-shell/.../integration/ShareTargetHandler.kt` | Android UI | `small-impl` | None | ACTION_SEND → choice: summarize or chat |
| W7-1e | Android: Share → summarize action | `android-shell/.../integration/ShareTargetHandler.kt` | Android UI | `small-impl` | W7-1d | Share article → summary notification |

#### Parallel Group B (Widget AI actions)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W7-2a | iOS: Upgrade widget to include AI quick actions | `ios-shell/MahoWidget/MahoWidget.swift` | iOS UI | `visual-engineering` | None | Widget shows: Search, Ask AI, Voice |
| W7-2b | iOS: Widget intent → deep link to AI chat | `ios-shell/Support/AppIntents.swift` | iOS UI | `small-impl` | W7-2a | Tap "Ask AI" → opens chat view |
| W7-2c | Android: Upgrade Glance widget | `android-shell/.../widget/SearchWidget.kt` | Android UI | `visual-engineering` | None | Widget shows: Search, Ask AI, Voice |
| W7-2d | Android: Widget click → AI chat deep link | `android-shell/.../widget/SearchWidget.kt` | Android UI | `small-impl` | W7-2c | Tap → opens chat activity |

#### Parallel Group C (Spotlight / App Search)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W7-3a | iOS: CoreSpotlight index donation for conversations | `ios-shell/Support/SpotlightManager.swift` (new) | iOS UI | `deep` | W2-3a (conversations table) | `CSSearchableItem` indexed for each conversation |
| W7-3b | iOS: Spotlight search → open conversation | `ios-shell/Support/SpotlightManager.swift` | iOS UI | `small-impl` | W7-3a | Search "recipe conversation" → opens in Maho |
| W7-3c | iOS: Index bookmarks + history to Spotlight | `ios-shell/Support/SpotlightManager.swift` | iOS UI | `small-impl` | W7-3a | Search visited site → Maho result |
| W7-3d | Android: App Indexing for conversations | `android-shell/.../integration/AppIndexManager.kt` (new) | Android UI | `small-impl` | W2-3a | Firebase App Indexing or custom deep links |

#### Parallel Group D (Conversation sync)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W7-4a | Add `Conversation` variant to `SyncEntityType` | `maho-core/src/sync_models.rs` L97 | Rust core | `quick` | None | Enum variant compiles, serde works |
| W7-4b | Serialize conversation sessions for sync | `maho-core/src/sync_manager.rs` | Rust core | `small-impl` | W7-4a, W2-3a | Test: conversation → SyncEntity payload |
| W7-4c | Handle incoming conversation sync entities | `maho-core/src/sync_manager.rs` | Rust core | `small-impl` | W7-4b | Test: receive sync entity → conversations table updated |
| W7-4d | iOS FFI: sync trigger for conversations | `maho-ffi/src/lib.rs` | FFI bridge | `quick` | W7-4b | Function compiles |
| W7-4e | Android JNI: sync trigger for conversations | `maho-jni/src/lib.rs` | JNI bridge | `quick` | W7-4b | Function callable |

### W7 Parallelism
```
Start ─┬── Group A (share) ───── all parallel ─────────────────────── Gate ✓
       ├── Group B (widget) ──── all parallel
       ├── Group C (spotlight) ── sequential within
       └── Group D (sync) ────── W7-4a → W7-4b → W7-4c → W7-4d,e
```

---

## 7. WAVE 8 — Mobile Agent (3 weeks)

### Gate Condition
> User submits multi-step web task from phone. Agent executes on relay.
> User sees agent progress and approves actions. Form auto-fill works.

### Task Graph

#### Parallel Group A (Relay agent infrastructure)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W8-1a | Add `chromiumoxide` + `tokio-tungstenite` to relay deps | `maho/relay/server/Cargo.toml` | Relay | `quick` | None | `cargo check` in relay/ |
| W8-1b | Create `agent/` module structure | `relay/server/src/agent/mod.rs` (new) | Relay | `quick` | None | Module compiles |
| W8-1c | Agent executor: connect to headless Chrome via CDP | `relay/server/src/agent/executor.rs` (new) | Relay | `deep` | W8-1a, W8-1b | Test: connect to Chrome, navigate to URL, get title |
| W8-1d | Agent executor: page interaction (click, type, extract) | `relay/server/src/agent/executor.rs` | Relay | `deep` | W8-1c | Test: navigate → fill form → submit → extract result |
| W8-1e | Agent executor: screenshot capture | `relay/server/src/agent/executor.rs` | Relay | `small-impl` | W8-1c | Test: capture viewport → PNG bytes |
| W8-1f | Agent executor: LLM planning loop | `relay/server/src/agent/planner.rs` (new) | Relay | `ultrabrain` | W8-1c, W8-1d | Test: "find cheapest flight" → multi-step plan generated |

#### Parallel Group B (WebSocket task protocol)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W8-2a | Define task protocol message types | `relay/server/src/agent/protocol.rs` (new) | Relay | `small-impl` | None | Types: Submit, Status, Approve, Cancel, Stream, Complete |
| W8-2b | WebSocket upgrade for agent endpoint (`/ws/agent`) | `relay/server/src/server.rs` (extend router) | Relay | `small-impl` | W8-2a | New route added, upgrade works |
| W8-2c | Task lifecycle: submit → queued → running → awaiting_approval → complete | `relay/server/src/agent/protocol.rs` | Relay | `small-impl` | W8-2a | Test: state machine transitions correctly |
| W8-2d | Status streaming: agent → client real-time updates | `relay/server/src/agent/protocol.rs` | Relay | `small-impl` | W8-2c | Test: agent acts → client receives status messages |
| W8-2e | BYOK key forwarding (client → relay → LLM, zero persistence) | `relay/server/src/agent/protocol.rs` | Relay | `small-impl` | W8-2a | Test: key in auth message → used for LLM call → not stored |

#### Parallel Group C (Sandbox & safety)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W8-3a | Resource limits: memory (512MB), CPU time | `relay/server/src/agent/sandbox.rs` (new) | Relay | `small-impl` | W8-1c | Test: exceeding memory → task killed |
| W8-3b | Timeout: 5-minute hard limit per task | `relay/server/src/agent/sandbox.rs` | Relay | `quick` | W8-3a | Test: task running >5min → cancelled |
| W8-3c | URL allowlist/blocklist | `relay/server/src/agent/sandbox.rs` | Relay | `small-impl` | W8-1c | Test: navigate to blocked URL → rejected |
| W8-3d | Action approval gates: sensitive actions require user OK | `relay/server/src/agent/sandbox.rs` | Relay | `small-impl` | W8-2c | Test: form submit → pauses until client approves |
| W8-3e | Rate limiting: max concurrent tasks per user | `relay/server/src/agent/sandbox.rs` | Relay | `quick` | W8-2b | Test: 4th concurrent task → rejected |

#### Parallel Group D (Mobile agent clients — after B)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W8-4a | iOS: WebSocket client for agent protocol | `ios-shell/Support/AgentClient.swift` (new) | iOS UI | `small-impl` | W8-2a | Connect → submit task → receive updates |
| W8-4b | iOS: Agent task submission UI | `ios-shell/Views/AgentView.swift` (new) | iOS UI | `visual-engineering` | W8-4a | Text input → "find cheapest flight" → submit |
| W8-4c | iOS: Agent progress display (steps, screenshots) | `ios-shell/Views/AgentView.swift` | iOS UI | `visual-engineering` | W8-4b | See agent navigating, filling forms |
| W8-4d | iOS: Action approval UI (confirm/deny) | `ios-shell/Views/AgentView.swift` | iOS UI | `visual-engineering` | W8-4c | "Agent wants to submit form — Allow?" modal |
| W8-4e | Android: WebSocket client for agent protocol | `android-shell/.../support/AgentClient.kt` (new) | Android UI | `small-impl` | W8-2a | Same as iOS |
| W8-4f | Android: Agent task submission UI | `android-shell/.../ui/agent/AgentScreen.kt` (new) | Android UI | `visual-engineering` | W8-4e | Same |
| W8-4g | Android: Agent progress display | `android-shell/.../ui/agent/AgentScreen.kt` | Android UI | `visual-engineering` | W8-4f | Same |
| W8-4h | Android: Action approval UI | `android-shell/.../ui/agent/AgentScreen.kt` | Android UI | `visual-engineering` | W8-4g | Same |

#### Parallel Group E (Agent tool integration + form fill)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W8-5a | Wire agent executor to tool_dispatcher tools | `relay/server/src/agent/executor.rs` | Relay | `small-impl` | W8-1d, W5-2b | Test: agent calls `search_bookmarks` tool |
| W8-5b | Agent tools: web_search, page_fetch, extract | `relay/server/src/agent/tools.rs` (new) | Relay | `small-impl` | W8-5a | Test: web_search → results returned to planner |
| W8-5c | Form detection: identify form fields on page | `relay/server/src/agent/form_fill.rs` (new) | Relay | `deep` | W8-1d | Test: detect checkout form → field list |
| W8-5d | Form fill from user profile/memory | `relay/server/src/agent/form_fill.rs` | Relay | `small-impl` | W8-5c | Test: fill shipping address from stored autofill data |
| W8-5e | Form fill approval gate | `relay/server/src/agent/form_fill.rs` | Relay | `small-impl` | W8-5d, W8-3d | Test: proposed fill → client preview → approve → submit |

#### Parallel Group F (Relay migration)

| ID | Task | File(s) | Layer | Category | Dependencies | Verification |
|----|------|---------|-------|----------|--------------|--------------|
| W8-6a | Migration: `agent_tasks` table | `relay/migration/src/m20260700_000003_create_agent_tables.rs` (new) | Relay | `small-impl` | None | `cargo test` in relay/ |
| W8-6b | Migration: `agent_actions` table (audit log) | Same file | Relay | `small-impl` | W8-6a | Migration runs |

### W8 Parallelism
```
Start ─┬── Group A (executor) ───────────────────────────────┐
       │   W8-1a → W8-1b → W8-1c → W8-1d,e → W8-1f         │
       ├── Group B (protocol) ───────────────────────────┐    ├── Group E (tools + form)
       │   W8-2a → W8-2b,c → W8-2d,e                    │    │   W8-5a–e
       ├── Group C (sandbox) ─── after A starts ─────────┤    │
       │   W8-3a → W8-3b,c,d,e                          │    │
       ├── Group D (mobile clients) ── after B ──────────┘    │
       │   W8-4a → W8-4b → W8-4c → W8-4d (iOS)              │
       │   W8-4e → W8-4f → W8-4g → W8-4h (Android)          │
       └── Group F (migration) ── independent ────────────────└── Gate ✓
           W8-6a → W8-6b
```

### Cross-Wave Outputs
- Agent protocol → foundation for future cross-app agent (F22)
- Form fill → reusable for non-agent autofill improvements
- Sandbox patterns → applicable to any future server-side execution

---

## 8. CRITICAL PATH ANALYSIS

### Longest Sequential Chain (determines timeline)

```
W2-1a (ChatContent text) ─── 0.5d
  → W2-4a (migrate ChatSession) ─── 1d
    → W3-1a (ChatContent Image) ─── 0.5d
      → W3-1c (OpenAI vision format) ─── 1d
        → W3-5c (iOS visual search E2E) ─── 2d
          → W4-3a (per-space system prompt) ─── 1d
            → W5-1a (suggestion engine) ─── 2d
              → W8-1f (agent planning loop) ─── 3d

Total critical path: ~12 working days (across waves)
```

### Parallel path (relay — can start from W6):

```
W8-1a (deps) → W8-1c (CDP) → W8-1d (interact) → W8-1f (planner) → W8-5a (tools)
Total: ~10 working days

Start relay work during W6 to overlap.
```

### Wave Duration Estimates

| Wave | Calendar Days | Bottleneck |
|------|--------------|------------|
| W2 | 10 days | Anthropic SSE parser (W2-2b) + context window (W2-5b) |
| W3 | 15 days | Camera infra from scratch (W3-2a, W3-2c) + tab clustering (W3-6b) |
| W4 | 10 days | Pattern extraction (W4-4d — topic clustering) |
| W5 | 10 days | Suggestion engine (W5-1a–e) |
| W6 | 10 days | Offline LLM integration (W6-1b — llama-cpp-rs) |
| W7 | 10 days | CoreSpotlight (W7-3a) + sync protocol (W7-4b) |
| W8 | 15 days | Agent executor (W8-1c–f) + planner (W8-1f) |

**Total: ~80 working days = 16 weeks with parallelism**
**With wave overlap (W6 ∥ W5, W7 ∥ W6, relay prep during W6): ~12-13 weeks**

---

## 9. CROSS-WAVE DEPENDENCY MAP

```
W2-1a (ChatContent)  ──────────────────► W3-1a (Image variant)
W2-2a (AnthropicProvider) ─────────────► All subsequent Claude usage
W2-3a (conversation_sessions table) ──► W7-4a (sync conversations)
W2-4b (persist messages) ──────────────► W4 (patterns learn from history)
W3-6a (tab embedding) ────────────────► W4-4d (topic clustering)
W4-4a (PatternManager) ───────────────► W5-1a (SuggestionManager)
W5-2b (register_tool) ────────────────► W8-5a (agent tool integration)
W5-2a (tool_dispatcher rename) ────────► W8 (all tool references updated)
W6-1b (LocalLLMProvider) ─────────────► W8 (offline fallback for simple agent tasks)
W6-2a (token counting) ───────────────► W8-2e (BYOK cost tracking on relay)
```

---

## 10. ULTRAWORK EXECUTION STRATEGY

### Wave Kickoff Checklist (run before each wave)
1. `bun run typecheck` — zero errors
2. `bun run test` — all pass
3. Previous wave gate verified
4. Branch created: `mobile/w{N}-main`

### Parallel Execution Rules
- **Max 4 parallel `deep` tasks** (model context budget)
- **iOS + Android UI tasks always paired** (same wave, parallel)
- **FFI + JNI always paired** (same API surface)
- **Relay work isolated** — separate terminal, `cd maho/relay`

### Verification Commands Per Layer
```bash
# Rust core (after any maho-core/maho-types/maho-storage change)
cd maho && bun run typecheck && bun run test

# Relay (after any relay/ change)
cd maho/relay && cargo test && cargo clippy -- -D warnings

# iOS build check (after any ios-shell/ change)
cd maho/ios-shell && make test

# Android build check (after any android-shell/ change)
cd maho/android-shell && ./gradlew app:testDebugUnitTest
```

### Risk-Ordered Execution
Start each wave with its highest-risk task:
- W2: Start with W2-2a (Anthropic SSE — complex protocol parsing)
- W3: Start with W3-2a (iOS camera — unknown AVCaptureSession complexity)
- W4: Start with W4-4d (topic clustering — ML/algorithmic challenge)
- W5: Start with W5-1a (suggestion engine — architecture decision)
- W6: Start with W6-1b (llama-cpp-rs — FFI + memory management)
- W7: Start with W7-3a (CoreSpotlight — Apple API learning curve)
- W8: Start with W8-1c (chromiumoxide CDP — relay architecture)

---

## 11. FILE CREATION SUMMARY

### New Files (37 total)

| Wave | File | Layer |
|------|------|-------|
| W2 | `maho-core/src/providers/anthropic.rs` | Rust core |
| W2 | `ios-shell/Views/ConversationListView.swift` | iOS UI |
| W2 | `android-shell/.../ui/chat/ConversationListScreen.kt` | Android UI |
| W3 | `ios-shell/Support/CameraManager.swift` | iOS UI |
| W3 | `android-shell/.../support/CameraManager.kt` | Android UI |
| W3 | `ios-shell/Bridge/MahoBridge+Screenshots.swift` | iOS bridge |
| W3 | `android-shell/.../bridge/BridgeScreenshots.kt` | Android bridge |
| W3 | `ios-shell/Views/VisualSearchView.swift` | iOS UI |
| W3 | `android-shell/.../ui/chat/VisualSearchScreen.kt` | Android UI |
| W3 | `maho-core/src/tab_grouping.rs` | Rust core |
| W3 | `ios-shell/Views/TabGroupSuggestionView.swift` | iOS UI |
| W3 | `android-shell/.../ui/tabs/TabGroupSuggestion.kt` | Android UI |
| W4 | `maho-core/src/pattern_manager.rs` | Rust core |
| W4 | `ios-shell/Views/SpaceAIConfigView.swift` | iOS UI |
| W4 | `android-shell/.../ui/spaces/SpaceAIConfigScreen.kt` | Android UI |
| W5 | `maho-core/src/suggestion_manager.rs` | Rust core |
| W5 | `ios-shell/Views/SuggestionChipView.swift` | iOS UI |
| W5 | `android-shell/.../ui/suggestions/SuggestionChip.kt` | Android UI |
| W5 | `ios-shell/Views/PrivacyDashboardView.swift` | iOS UI |
| W5 | `android-shell/.../ui/privacy/PrivacyDashboard.kt` | Android UI |
| W6 | `maho-core/src/providers/local.rs` | Rust core |
| W6 | `maho-core/src/model_download.rs` | Rust core |
| W6 | `maho-core/src/token_counter.rs` | Rust core |
| W6 | `ios-shell/Views/OfflineModelView.swift` | iOS UI |
| W6 | `android-shell/.../ui/settings/OfflineModelScreen.kt` | Android UI |
| W7 | `ios-shell/Support/SpotlightManager.swift` | iOS UI |
| W7 | `android-shell/.../integration/AppIndexManager.kt` | Android UI |
| W8 | `relay/server/src/agent/mod.rs` | Relay |
| W8 | `relay/server/src/agent/executor.rs` | Relay |
| W8 | `relay/server/src/agent/protocol.rs` | Relay |
| W8 | `relay/server/src/agent/sandbox.rs` | Relay |
| W8 | `relay/server/src/agent/planner.rs` | Relay |
| W8 | `relay/server/src/agent/tools.rs` | Relay |
| W8 | `relay/server/src/agent/form_fill.rs` | Relay |
| W8 | `relay/migration/src/m20260700_000003_create_agent_tables.rs` | Relay |
| W8 | `ios-shell/Support/AgentClient.swift` | iOS UI |
| W8 | `ios-shell/Views/AgentView.swift` | iOS UI |
| W8 | `android-shell/.../support/AgentClient.kt` | Android UI |
| W8 | `android-shell/.../ui/agent/AgentScreen.kt` | Android UI |

### Modified Files (15 high-impact)

| File | Waves | Nature of Changes |
|------|-------|-------------------|
| `maho-types/src/chat.rs` | W2, W3 | +ChatContent enum, +ChatMessage struct |
| `maho-types/src/llm.rs` | W3 | +ImageDelta variant |
| `maho-types/src/space.rs` | W4 | +SpaceAIConfig struct, +ai_config field |
| `maho-types/src/settings.rs` | W5, W6 | +AI privacy fields, +quality toggle |
| `maho-core/src/chat_session.rs` | W2–W6 | Persistence, context window, provider dispatch, caching |
| `maho-core/src/llm_provider.rs` | W3 | Vision message formatting |
| `maho-core/src/llm_client.rs` | W2 | Fix Anthropic auth header |
| `maho-core/src/skills_manager.rs` | W5 | +tools field |
| `maho-core/src/mcp_manager.rs` → `tool_dispatcher.rs` | W5 | Rename + register_tool() |
| `maho-core/src/sync_models.rs` | W7 | +Conversation variant |
| `maho-storage/src/sqlite.rs` | W2–W6 | Schema migrations, new tables, queries |
| `maho-ffi/src/lib.rs` | W2–W7 | +30 new FFI functions |
| `maho-jni/src/lib.rs` | W2–W7 | +35 new JNI functions |
| `relay/server/Cargo.toml` | W8 | +chromiumoxide, +tokio-tungstenite |
| `relay/server/src/server.rs` | W8 | +agent route |

---

## 12. TASK COUNT SUMMARY

| Wave | Tasks | Deep | Small-impl | Quick | Visual-eng | Ultrabrain |
|------|-------|------|------------|-------|------------|------------|
| W2 | 21 | 2 | 10 | 5 | 4 | 0 |
| W3 | 23 | 4 | 11 | 4 | 4 | 0 |
| W4 | 17 | 2 | 8 | 5 | 2 | 0 |
| W5 | 17 | 1 | 8 | 5 | 3 | 0 |
| W6 | 18 | 2 | 8 | 5 | 3 | 0 |
| W7 | 18 | 1 | 10 | 4 | 3 | 0 |
| W8 | 27 | 4 | 14 | 4 | 4 | 1 |
| **Total** | **141** | **16** | **69** | **32** | **23** | **1** |
