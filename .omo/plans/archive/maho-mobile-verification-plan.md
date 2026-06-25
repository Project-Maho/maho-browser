# Maho Mobile Verification Plan — W0-W8 Implementation

**Created:** 2026-06-18
**Depends on:** `.omo/plans/maho-mobile-arcsearch-v2.md`, `.omo/plans/maho-mobile-w2-w8-execution.md`
**Status:** Ready for execution
**Scope:** Verify 22 features × 4 layers (Rust/FFI/JNI/UI) implemented in W0-W8

---

## 0. Executive Summary

872 unit tests pass. Zero runtime verification. This plan phases verification from cheapest
(static analysis, integration tests) to most expensive (real API calls, full device E2E),
with clear pass/fail gates at each phase.

**Total estimated effort:** 12-15 engineering days across 5 phases
**Critical path to "demo-ready":** Phases 1-3 (5-7 days)
**Critical path to "beta-ready":** All 5 phases (12-15 days)

---

## 1. PHASE 1 — Static & Unit Hardening (1-2 days)

**Goal:** Confirm all compilation is real, fill critical unit test gaps, catch
serialization mismatches before any runtime attempt.

**Cost:** Cheapest. No device, no API keys, no network.

### 1.1 Tasks

| ID | Task | Location | Gate |
|----|------|----------|------|
| V1-1 | Run `cargo clippy --workspace -- -D warnings` from `maho/` | Rust workspace | Zero warnings |
| V1-2 | Run `cargo clippy -- -D warnings` from `maho/relay/` | Relay workspace | Zero warnings |
| V1-3 | Add serde round-trip tests for ALL new types in `maho-types/src/chat.rs` | `maho-types` | ChatContent::Text/Image/Mixed, ChatMessage, ConversationMeta all round-trip |
| V1-4 | Add serde round-trip tests for `LLMStreamChunk` variants (Token, ToolCallDelta, ImageDelta, Error) | `maho-types/src/llm.rs` | All variants serialize/deserialize correctly |
| V1-5 | Add JSON format compatibility tests: Rust ↔ Swift expected output | `maho-core/tests/json_compat.rs` (new) | Verify snake_case output matches what iOS expects (camelCase via `#[serde(rename_all)]` or not) |
| V1-6 | Add JSON format compatibility tests: Rust ↔ Kotlin expected output | Same file | Verify Android `MahoJson` deserializes Rust output correctly |
| V1-7 | Verify `SpaceAIConfig` serde with `#[serde(default)]` on all fields | `maho-types/src/space.rs` tests | Missing fields don't crash deserialization |
| V1-8 | Add FFI null-safety smoke tests for ALL new `maho_core_chat_*` functions | `maho-ffi/src/lib.rs` or new `maho-ffi/tests/` | Passing NULL ptr doesn't segfault (returns error or no-op) |
| V1-9 | Verify `maho_string_free` is called on every returned `*mut c_char` path | Code audit of ios-shell + maho-chromium | No leak paths identified |
| V1-10 | Run Android lint: `./gradlew app:lintDebug` | android-shell/ | No new errors (warnings acceptable) |
| V1-11 | Run iOS build with `-warnings-as-errors` | ios-shell/ | Identify any Swift warnings in new AI views |
| V1-12 | Verify all new SQLite migrations run in sequence without conflict | `maho-storage` test with fresh DB | `conversation_sessions`, `ai_usage_stats`, `ai_response_cache`, `space_id` columns all created |

### 1.2 Test Artifacts to Add

```
maho/crates/maho-types/src/chat.rs          — add 8 serde tests (V1-3)
maho/crates/maho-types/src/llm.rs           — add 4 serde tests (V1-4)
maho/crates/maho-core/tests/json_compat.rs  — new file, 10+ format assertions (V1-5, V1-6)
maho/crates/maho-ffi/tests/null_safety.rs   — new file, 12+ null-pointer tests (V1-8)
maho/crates/maho-storage/src/sqlite.rs      — add migration sequence test (V1-12)
```

### 1.3 Pass/Fail Gate

```
✓ cargo clippy (both workspaces) = 0 warnings
✓ cargo test --workspace (maho/) = ALL pass (701+ existing + new)
✓ cargo test (relay/) = ALL pass (171+ existing)
✓ ./gradlew app:lintDebug = no new errors
✓ xcodebuild build = no new warnings treated as errors
✓ JSON compat tests prove iOS/Android can decode all new Rust types
```

---

## 2. PHASE 2 — Integration Tests (2-3 days)

**Goal:** Verify data flows through FFI/JNI boundaries with real (in-process)
execution. Catch stub functions, wrong pointer casts, missing JSON fields.

**Cost:** Medium. Requires compiling for target arch but no external dependencies.

### 2.1 Tasks

| ID | Task | Location | Gate |
|----|------|----------|------|
| V2-1 | Create FFI integration test: full chat lifecycle (create session → send message → receive tokens → free) | `maho-ffi/tests/chat_integration.rs` | Tokens received via callback, session freed without leak |
| V2-2 | Create FFI integration test: BYOK key encrypt → store → retrieve → decrypt | `maho-ffi/tests/byok_integration.rs` | Round-trip key recovery with correct provider name |
| V2-3 | Create FFI integration test: conversation CRUD (create → save message → list → delete) | `maho-ffi/tests/conversation_integration.rs` | List returns created conversation, delete removes it |
| V2-4 | Create FFI integration test: tab clustering (embed → cluster → return JSON) | `maho-ffi/tests/clustering_integration.rs` | Returns valid JSON array of TabCluster objects |
| V2-5 | Create FFI integration test: response cache (store → hit → TTL expire → miss) | `maho-ffi/tests/cache_integration.rs` | Cache hit returns instant, expired cache misses |
| V2-6 | Create FFI integration test: suggestion manager trigger → ranked results | `maho-ffi/tests/suggestion_integration.rs` | NewTab trigger with pattern data → non-empty suggestions |
| V2-7 | JNI smoke test via `cargo test -p maho-jni` (add `#[cfg(test)]` module) | `maho-jni/src/lib.rs` | Basic: construct JNI-compatible JSON, verify parsing |
| V2-8 | Relay WebSocket integration: connect → TaskSubmit → TaskAck → StatusUpdate → TaskComplete | `relay/server/tests/agent_ws.rs` | Full lifecycle without real Chrome (mock executor) |
| V2-9 | Relay sandbox enforcement: URL blocklist, timeout, concurrent task limit | `relay/server/tests/agent_sandbox_integration.rs` | Blocked URL → error, timeout → cancelled |
| V2-10 | Conversation sync: serialize → transmit → deserialize round-trip | `maho-core/tests/conversation_sync.rs` | SyncEntityType::Conversation payload round-trips via relay protocol |
| V2-11 | Mock LLM provider: verify OpenAI vision serialization (ChatContent::Image → correct JSON) | `maho-core/tests/vision_serialization.rs` | Image → `{"type":"image_url","image_url":{"url":"data:image/png;base64,..."}}` |
| V2-12 | Mock LLM provider: verify Anthropic vision serialization | Same file | Image → `{"type":"image","source":{"type":"base64",...}}` |
| V2-13 | Context window management: 60 messages → pruning → fits in token budget | `maho-core` inline test extension | History truncated, system prompt preserved |
| V2-14 | Token counter accuracy: compare against known tiktoken outputs for 20 test strings | `maho-core/src/token_counter.rs` test extension | Within ±20% of cl100k_base for English text |

### 2.2 Pass/Fail Gate

```
✓ ALL FFI integration tests pass (V2-1 through V2-6)
✓ JNI module compiles with test module (V2-7)
✓ Relay agent WebSocket lifecycle test passes (V2-8)
✓ Sandbox enforcement tests pass (V2-9)
✓ Vision serialization matches OpenAI/Anthropic API specs (V2-11, V2-12)
✓ Context window respects 128k token limit (V2-13)
✓ Token counter within ±20% accuracy (V2-14)
✓ Total: cargo test --workspace = ALL pass
```

---

## 3. PHASE 3 — Simulator/Emulator Smoke (2-3 days)

**Goal:** App launches, navigates to new AI screens, doesn't crash. Verify FFI/JNI
actually connects at runtime (not just compile-time). This is the minimum gate for
"demo-ready."

**Cost:** Requires device simulator/emulator + built app binaries. No external API calls yet.

### 3.1 Prerequisites

```bash
# iOS: build for simulator
cd maho/ios-shell && xcodebuild build \
  -scheme "Maho" -destination "platform=iOS Simulator,name=iPhone 17,OS=26.5" \
  -configuration Debug

# Android: build debug APK
cd maho/android-shell && ./gradlew app:assembleDebug

# Boot simulators
xcrun simctl boot "iPhone 17"
# Android: start emulator (API 34+)
```

### 3.2 iOS Manual QA Script (15 steps)

| # | Step | Action | Expected | Pass/Fail |
|---|------|--------|----------|-----------|
| 1 | Launch | Tap Maho icon | App opens to home/tab view without crash | |
| 2 | Navigate to Settings | Tap ≡ menu → Settings | Settings screen renders | |
| 3 | Open BYOK Settings | Settings → AI → API Keys | BYOKSettingsView renders with OpenAI + Anthropic fields | |
| 4 | Enter mock API key | Type "sk-test-12345" in OpenAI field → Save | Key appears masked, no crash | |
| 5 | Open Chat | Tap AI/Chat icon (or navigate to ChatView) | ChatView renders with empty state | |
| 6 | Type message (offline) | Type "Hello" → Send | Error message displayed (no API key validation or network error), no crash | |
| 7 | Open Conversation List | Navigate to conversation list | ConversationListView renders (empty or with test conversation) | |
| 8 | Open Privacy Dashboard | Settings → Privacy → AI Privacy | PrivacyDashboardView renders with toggle controls | |
| 9 | Open Space AI Config | Space settings → AI Personality | SpaceAIConfigView renders with tone/focus fields | |
| 10 | Open Offline Model | Settings → AI → Offline Model | OfflineModelView renders showing model info (not downloaded) | |
| 11 | Toggle Incognito | Long-press + New Incognito Tab | Tab opens with `nonPersistent()` data store | |
| 12 | Content Blocker (indirect) | Navigate to ad-heavy page (e.g. cnn.com) | Page loads (verifying no crash from new shouldInterceptRequest path) | |
| 13 | Deep Links | Open `maho://ask` in Safari | App opens to ChatView | |
| 14 | Deep Links | Open `maho://voice` in Safari | App opens with voice capture attempting to start | |
| 15 | Widget render | Add Maho widget to home screen | Widget renders (search + AI quick actions visible) | |

### 3.3 Android Manual QA Script (15 steps)

| # | Step | Action | Expected | Pass/Fail |
|---|------|--------|----------|-----------|
| 1 | Launch | Tap Maho icon | App opens without crash | |
| 2 | Navigate to Settings | Menu → Settings | Settings screen renders | |
| 3 | Open BYOK Settings | Settings → AI → API Keys | BYOKSettingsScreen renders | |
| 4 | Enter mock key | Type "sk-test-12345" → Save | Key stored, no crash | |
| 5 | Open Chat | Navigate to ChatScreen | Compose field visible, empty state | |
| 6 | Type message (offline) | "Hello" → Send | Graceful error, no crash | |
| 7 | Open Conversation List | Conversations menu | ConversationListScreen renders | |
| 8 | Camera Permission | ChatScreen → camera icon | CAMERA permission dialog appears | |
| 9 | Content Blocker | Navigate to ad-heavy site | `shouldInterceptRequest` + `nativeCheckRequest` fires (verify via Logcat) | |
| 10 | Visual Search | Open VisualSearchScreen | Camera preview renders (or permission prompt) | |
| 11 | Space AI Config | Space → AI settings | SpaceAIConfigScreen renders | |
| 12 | Agent Screen | Navigate to AgentScreen | Screen renders with task input field | |
| 13 | Deep Link | `adb shell am start -d "maho://ask"` | App opens to ChatScreen | |
| 14 | Widget | Add Maho widget | Glance widget renders with AI quick actions | |
| 15 | Share Target | Share URL from Chrome → Maho | Share dialog shows Open/Summarize/Ask Maho options | |

### 3.4 Pass/Fail Gate

```
✓ iOS app launches in simulator without crash
✓ All 15 iOS QA steps pass (no crash, screens render)
✓ Android app launches in emulator without crash
✓ All 15 Android QA steps pass (no crash, screens render)
✓ No ANR on any screen transition
✓ No purple Thread Checker warnings in Xcode console
✓ Memory usage < 200MB at idle after navigating all AI screens
```

---

## 4. PHASE 4 — Live Integration (3-4 days)

**Goal:** Real API round-trips work. Streaming displays correctly. End-to-end data
flows verified with actual LLM responses.

**Cost:** Requires BYOK API keys (OpenAI + Anthropic). Costs real tokens (~$1-5 total).

### 4.1 Live Integration Test Matrix

| Feature | Platform | Test | Expected | Blocking? |
|---------|----------|------|----------|-----------|
| F1: Instant Answer | iOS | Type question → send | Streamed tokens appear progressively | YES |
| F1: Instant Answer | Android | Same | Same | YES |
| F2: Ask-about-tab | iOS | Load page → "What is this about?" | Response references page content | YES |
| F2: Ask-about-tab | Android | Same | Same | YES |
| F3: Multi-turn | iOS | Send 3 messages → close → reopen | History persists, context maintained | YES |
| F4: Streaming | iOS | Long response | Tokens appear word-by-word | YES |
| F4: Streaming | Android | Same | Same | YES |
| F5: BYOK validate | iOS | Enter real OpenAI key → validate | Green checkmark, models list populated | YES |
| F5: BYOK validate | Android | Enter real Anthropic key → validate | Green checkmark | YES |
| F6: Summarize | iOS | Load Wikipedia article → Summarize | Coherent 3-5 sentence summary | YES |
| F10: Visual Search | iOS | Take photo → ask "what is this?" | Vision API response describes image | NO |
| F10: Visual Search | Android | Same (need real device for camera) | Same | NO |
| W2: Anthropic | iOS | Switch to Claude Sonnet → ask question | Response from Anthropic | YES |
| W6: Token counter | Both | After chat → check settings | Token count > 0, cost estimate shown | NO |
| W6: Cache | Both | Ask same question twice | Second response instant | NO |
| W7: Sync | Both | Start convo on iOS → check on Android | Conversation appears on other device | NO |
| W8: Agent | Android | Submit "navigate to example.com" | Agent screen shows progress | NO |

### 4.2 Anthropic SSE Verification (Critical)

```
Test: Send "Say hello" to claude-sonnet-4-20250514 with max_tokens=50
Expected SSE events:
  - message_start (id, model, usage)
  - content_block_start (type: text)
  - content_block_delta (text_delta: "Hello"...)
  - content_block_stop
  - message_delta (stop_reason: end_turn)
  - message_stop
Verify: All tokens received, no parse errors, stop event fires on_complete callback
```

### 4.3 OpenAI Vision Verification (Critical)

```
Test: Send ChatContent::Image (PNG screenshot) + "Describe this" to gpt-4o
Expected: Response describes the image content
Verify: Base64 encoding accepted, no 400 errors, response coherent
```

### 4.4 Pass/Fail Gate

```
✓ OpenAI streaming chat works on both platforms
✓ Anthropic streaming chat works on both platforms
✓ Conversation persists across app restart
✓ Page context injection works (ask-about-tab gives page-relevant answer)
✓ BYOK key validation succeeds with real keys on both platforms
✓ No memory leak after 10+ consecutive messages
✓ Event sink callbacks fire correctly (no deadlock in 50ms polling loop)
```

---

## 5. PHASE 5 — Edge Cases & Regression (3-4 days)

**Goal:** Error handling, offline graceful degradation, concurrent operations,
platform-specific edge cases. Required for "beta-ready" claim.

### 5.1 Error Handling Tests

| Scenario | Test | Expected |
|----------|------|----------|
| Invalid API key | Enter "sk-invalid" → send message | Clear error: "Invalid API key" not raw HTTP 401 |
| Network loss mid-stream | Start response → airplane mode | Partial response preserved, error shown, no crash |
| Expired key | Revoke key → send message | Error prompts re-entry, doesn't loop |
| Rate limited (429) | Trigger rate limit (rapid sends) | Retry-after shown or graceful backoff |
| Huge response (>100k tokens) | Ask for very long response | Memory bounded, UI responsive |
| Empty response | Model returns empty | No crash, shows placeholder |
| Malformed SSE | Mock test, inject broken SSE | Parser doesn't panic, shows error |
| Concurrent sessions | Open 2 chat views simultaneously | Both work independently |
| Background/foreground | Send message → background → foreground | Response completed, visible on return |
| App kill during stream | Force-kill during response | Next launch: partial saved or clean state |

### 5.2 Platform-Specific Tests

**iOS-specific:**
| Test | Expected |
|------|----------|
| Memory pressure (simulate) | No crash, graceful resource release |
| Widget after cold boot | Widget data loads from UserDefaults |
| Spotlight query "my conversation" | CSSearchableItem results appear |
| Share extension from Safari | Opens Maho with shared URL as context |

**Android-specific:**
| Test | Expected |
|------|----------|
| Process death (don't keep activities) | State restored from SQLite, no crash |
| Dark mode toggle | All AI screens render correctly |
| Font scale 200% | Chat bubbles don't overlap |
| Low memory device (trim to 2GB) | No OOM |
| ProGuard/R8 doesn't strip JNI | All `native*` methods still resolved |

### 5.3 Security Tests

| Test | Expected |
|------|----------|
| API key not in logs | grep logcat/Console for key → not found |
| API key not in crash reports | Simulate crash → inspect report |
| Encrypted key in SQLite | sqlite3 db "SELECT * FROM api_keys" → encrypted blob |
| Incognito cookies isolated | Log in → incognito, close → regular session unaffected |
| Relay agent doesn't store keys | After task: sqlite3 relay.db → no api_key column/value |

### 5.4 Pass/Fail Gate

```
✓ All error scenarios show user-friendly messages (no raw HTTP, no panics)
✓ Network loss doesn't crash or deadlock
✓ Concurrent chat sessions work independently
✓ App survives background/foreground transitions during streaming
✓ API keys never appear in logs or crash reports
✓ Android process death + restore works
✓ Tab grouping produces sensible results with real content
```

---

## 6. RISK REGISTER

Ranked by Impact × Likelihood (1-5 scale each):

| # | Risk | Impact | Likelihood | Score | Mitigation | Phase |
|---|------|--------|------------|-------|------------|-------|
| 1 | FFI stub functions return garbage/NULL (14+ stubs added during recovery) | 5 | 4 | **20** | V2-1 through V2-6: integration tests with real core execution | Phase 2 |
| 2 | JSON snake_case/camelCase mismatch between Rust and Swift/Kotlin | 4 | 4 | **16** | V1-5, V1-6: explicit format compatibility tests | Phase 1 |
| 3 | JNI 50ms event polling loop deadlocks or leaks on Android | 5 | 3 | **15** | V4 live test on device + Android Profiler memory check | Phase 4 |
| 4 | iOS NavigationLinks to new views broken (xcodeproj re-inclusion) | 3 | 4 | **12** | V3 iOS QA: navigate to every new screen | Phase 3 |
| 5 | Anthropic SSE parser fails on real API (only mock-tested) | 4 | 3 | **12** | V4-3: dedicated real-API SSE verification | Phase 4 |
| 6 | Camera permission → capture → bytes flow broken end-to-end | 3 | 3 | **9** | V3 device test (Android) + V4 visual search test | Phase 3+4 |
| 7 | chromiumoxide can't find/launch Chrome on relay server | 3 | 3 | **9** | V2-8 with mock executor first; V4 with real Chrome | Phase 4 |
| 8 | ChatContent::Image base64 rejected by OpenAI/Anthropic vision API | 4 | 2 | **8** | V2-11/V2-12 format tests + V4-4 real API test | Phase 2+4 |
| 9 | Conversation sync creates duplicates or loses messages | 4 | 2 | **8** | V2-10 round-trip test + V5 multi-device test | Phase 2+5 |
| 10 | Tab clustering threshold (0.75) produces poor groupings on real data | 2 | 3 | **6** | V5 data consistency test with real tab titles | Phase 5 |

---

## 7. FEATURE → VERIFICATION GATE MAP

| Feature | Unit (P1) | Integration (P2) | Simulator (P3) | Live (P4) | Edge (P5) |
|---------|-----------|-------------------|-----------------|-----------|-----------|
| F1: Instant Answer | serde ✓ | FFI chat lifecycle | screen render | real response | network loss |
| F2: Ask-about-tab | — | page context injection | screen render | page-relevant answer | empty page |
| F3: Multi-turn | conversation serde | CRUD integration | list render | persist across restart | 100 msgs |
| F4: Streaming | LLMStreamChunk serde | callback integration | screen render | progressive tokens | huge response |
| F5: BYOK | key serde | encrypt/decrypt cycle | settings render | real key validation | invalid key |
| F6: Summarize | — | — | screen render | coherent summary | empty page |
| F7: Smart Tabs | cluster tests | FFI cluster integration | — | real tab titles | edge sizes |
| F9: Auto-archive | lifecycle tests | stale tab detection | — | — | zero tabs |
| F10: Visual Search | ChatContent::Image serde | vision serialization | camera render | real vision API | no camera |
| F11: Voice Input | — | — | voice button render | transcription flow | denied mic |
| F12: Screenshot→AI | — | — | button render | screenshot + vision | empty page |
| F13: Space Personality | SpaceAIConfig serde | system prompt build | config screen | different tones | empty config |
| F14: Patterns | pattern tests | — | — | real history | no history |
| F15: Suggestions | suggestion tests | trigger integration | chip render | time-aware | all dismissed |
| F16: Content Blocker | — | nativeCheckRequest | page load | ad-heavy site | malformed URL |
| F17: Real Incognito | — | — | incognito tab | cookie isolation | crash mid-session |
| F18: Encrypted Passwords | — | encrypt/decrypt | — | — | key rotation |
| F19: Privacy Dashboard | — | usage tracking | screen render | stats after chat | zero usage |
| F20: Web Agent | protocol tests + sandbox | WS lifecycle | agent screen | relay round-trip | timeout |
| W7 Share/Widget/Spotlight | — | — | render tests | deep link flow | cold boot |
| W7 Conversation Sync | sync serde | round-trip relay | — | cross-device | conflict |

---

## 8. CI INTEGRATION PLAN

### 8.1 Immediate (Phase 1-2 additions)

```yaml
# Add to existing rust-test job:
relay-test:
  name: relay-test
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - uses: dtolnay/rust-toolchain@stable
      with: { toolchain: 1.78.0 }
    - uses: Swatinem/rust-cache@v2
      with: { workspaces: "maho/relay" }
    - name: relay cargo test
      working-directory: maho/relay
      run: cargo test

android-unit:
  name: android-unit-tests
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - uses: actions/setup-java@v4
      with: { distribution: temurin, java-version: 17 }
    - name: Android unit tests
      working-directory: maho/android-shell
      run: ./gradlew app:testDebugUnitTest
```

### 8.2 Near-term (Phase 3 — add iOS CI)

```yaml
ios-build:
  name: ios-build
  runs-on: macos-14
  steps:
    - uses: actions/checkout@v4
    - name: Build for simulator
      working-directory: maho/ios-shell
      run: |
        xcodebuild build \
          -scheme "Maho" \
          -destination "platform=iOS Simulator,name=iPhone 15,OS=latest" \
          -configuration Debug \
          CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO
```

### 8.3 Long-term (Phase 5 — device-dependent)

- Android instrumented tests via `reactivecircus/android-emulator-runner@v2`
- Appium E2E with self-hosted runner
- Live API tests scheduled nightly with secrets
- Visual regression screenshot comparison per PR

---

## 9. CRITICAL PATH — Minimum for "Production-Ready"

```
PHASE 1 (1-2d) ──── Required for ANY deployment
  Must pass: V1-1, V1-2, V1-3, V1-4, V1-5, V1-6, V1-12

PHASE 2 (2-3d) ──── Required for "it actually works" claim
  Must pass: V2-1, V2-2, V2-3, V2-11, V2-12

PHASE 3 (2-3d) ──── Required for "demo-ready"
  Must pass: All iOS + Android smoke QA steps (no crashes)

PHASE 4 (3-4d) ──── Required for "beta-ready"
  Must pass: Streaming works on both platforms with real keys

PHASE 5 (3-4d) ──── Required for "App Store-ready"
  Must pass: All error handling + security tests
```

**Absolute minimum demo-ready: Phases 1-3 = 5-8 days**
**Beta-ready: Phases 1-4 = 8-12 days**
**App Store-ready: All phases = 12-15 days**

---

## 10. ATOMIC COMMIT STRATEGY

### Branch Structure

```
main
  └── verify/phase-1-static
      └── verify/phase-2-integration
          └── verify/phase-3-smoke
              └── verify/phase-4-live
                  └── verify/phase-5-edge
```

### Commit Convention

```
verify/{phase}/{area}: {verb} {subject}

Body: What this test verifies and why it matters.
Gate: {pass/fail criterion}
```

---

## 11. KNOWN DEFERRED (NOT IN THIS PLAN)

| Item | Reason | When |
|------|--------|------|
| W8 LLM planner loop (relay) | Not implemented, only stubs | Future W8 completion |
| W8 iOS agent UI | Not implemented | Future W8 completion |
| W8 form auto-fill | Not implemented | Future W8 completion |
| Real-device camera E2E | Requires physical device | When devices available |
| Performance profiling (battery) | Needs sustained real-device usage | Pre-App Store |
| Accessibility audit | Separate concern | Pre-App Store |
| Localization testing | No i18n in AI features yet | Future |
| Chromiumoxide real Chrome test | Needs headless Chrome on CI | When CI has Chrome |

---

## 12. VERIFICATION DEPENDENCIES

```
Phase 1 ─── no external dependencies (pure code)
Phase 2 ─── no external dependencies (in-process tests)
Phase 3 ─── requires: Xcode 16+, iOS Simulator 26.5, Android SDK 34+, emulator
Phase 4 ─── requires: OpenAI API key, Anthropic API key, network, local relay
Phase 5 ─── requires: all of Phase 4 + Instruments.app, Android Profiler
```

---

## TOP 3 IMMEDIATE ACTIONS

1. **Run `cargo clippy --workspace -- -D warnings`** — likely reveals issues in new code
2. **Write JSON compat tests (Risk #2 high-likelihood)** — catches snake_case/camelCase problem before any device test
3. **Write FFI null-safety tests (Risk #1 highest-impact)** — proves stubs don't crash

## EFFORT SUMMARY

| Phase | Days | Blocking? | Output |
|-------|------|-----------|--------|
| 1: Static/Unit | 1-2 | YES (gates all others) | ~30 new tests, JSON compat verified |
| 2: Integration | 2-3 | YES (gates demo) | ~20 new integration tests, FFI proven |
| 3: Simulator Smoke | 2-3 | YES (gates demo) | App runs, screens render, no crashes |
| 4: Live Integration | 3-4 | YES (gates beta) | Real LLM responses, streaming works |
| 5: Edge Cases | 3-4 | NO (gates App Store) | Error handling, security, perf |

**END OF PLAN**
