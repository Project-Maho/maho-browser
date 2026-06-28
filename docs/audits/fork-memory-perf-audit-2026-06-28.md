# Maho Fork — Memory Leak & Performance Audit (Consolidated)

**Date:** 2026-06-28
**Scope:** Maho fork code only. Upstream Chromium NOT audited.
**Surfaces:** `maho-chromium/browser/ai/`, `maho-chromium/browser/` (non-AI), `maho-chromium/components/`, `maho-chromium/chromium_src/`, `maho-chromium/browser/ui/views/`, `maho-chromium/browser/resources/`, `maho-chromium/third_party/maho/` (FFI bridge), `maho/crates/maho-ffi/`, `maho/crates/maho-core/`, `maho/crates/maho-storage/`, `maho/crates/maho-engine/`.
**Method:** 5 parallel deep audits, each with structured pattern checklists. Findings cross-referenced against per-surface sub-reports.

**Per-surface sub-reports (read for full detail):**
- `docs/audit-memory-performance-non-ai.md` — C++ overlay (non-AI)
- `docs/audits/rust-core-memory-perf-audit-2026-06-28.md` — Rust maho-core
- `maho-chromium/docs/memory-performance-audit.md` — chromium_src + views + WebUI resources
- (browser/ai and FFI bridge findings only in this consolidated report — see inline)

---

## TL;DR

The fork is structurally sound (RAII, ScopedObservation, WeakPtrFactory used consistently in most C++; FFI bridge has consistent null-checks and panic catches), but has **9 issues that will leak memory or block the UI thread in real user sessions**. Most impactful:

1. **Thread-safety violation across 32 settings handlers** — `maho-core` FFI invoked from `ThreadPool` workers (must be UI-thread per stated invariant). Race / corruption risk.
2. **Stack-allocated `MahoBrowserToolExecutor` destroyed before async callback** — every tool-using AI turn that needs page context leaks the entire conversation round state and hangs the UI.
3. **Sidebar rebuild does 3+ synchronous FFI calls + JSON parse with per-byte favicon arrays** on the UI thread — tab-switch jank.
4. **Per-Browser static map never evicted** in `MahoBoostWindowController` — dangling `Browser*` keys + leaked controllers across long sessions.
5. **`reqwest::Client::new()` rebuilt 6× per request** in Rust LLM providers — TLS handshake + connection pool reconstructed every call.
6. **`unbounded_channel()` for streaming LLM tokens** — slow consumer → unbounded memory growth.
7. **`base::Unretained(helper)` ad-block callback crossing IO→UI thread boundary** — UAF if tab closed mid-request.
8. **Preview-byte free pairs `Box::into_raw(Box<[u8]>)` with `Vec::from_raw_parts`** — relies on undocumented allocator layout, latent heap corruption.
9. **`IsCorePinnedTab` JSON-parses all open tabs on every tab close** — O(tabs) UI-thread work in a hot path.

Estimated quick wins (≤30 min each): items **5, 6, 8**. Items **1, 2** require careful refactor.

---

## Critical findings (cross-surface)

### C1 — Thread-safety violation: 32 settings handlers call maho-core FFI from ThreadPool
**Surface:** C++ non-AI · **File:** `browser/ui/webui/maho_settings/maho_settings_page_handler.cc:276-528`
```cpp
std::vector<...> BuildProfilesOnPool() {
  MahoCore* core = maho::GetCore();
  char* json_str = maho_core_list_profiles(core);  // FFI from thread pool!
```
Affected handlers (every `*OnPool` helper): `BuildProfiles`, `CreateProfile`, `DeleteProfile`, `SwitchProfile`, `BuildATCRules`, `AddATCRule`, `RemoveATCRule`, `ToggleATCRule`, `BuildShortcuts`, `SetShortcut`, `ResetShortcut`, `ResetAllShortcuts`, `ToggleShortcut`, `BuildSyncStatus`, `BuildSyncDevices`, `GenerateSyncKey`, `StartSync`, `JoinSync`, `StopSync`, `ApplyThemeToSpace`, `BuildSpaces`, `BuildAutofillAddresses`, `BuildAutofillPayments`, `BuildFilterLists`, `RebuildContentRules`, `AddFilterList`, `ToggleFilterList`, `RemoveFilterList`, `BuildInstalledExtensions`, `AddAutofillAddress`, `DeleteAutofillAddress`, `AddAutofillPayment`, `DeleteAutofillPayment`.
**Why:** `browser/importer/AGENTS.md` INV-8 states FFI calls must be on UI thread. `MahoCore` is `!Sync` (no internal `Mutex` between managers — composed-by-value). Concurrent FFI calls from worker threads and the UI thread race on shared state in Rust.
**Fix:** Move FFI calls back onto the UI thread (`PostTaskAndReplyWithResult` only for the response transform), or add an `RwLock` around `MahoCore` access in Rust.

### C2 — Stack-local `MahoBrowserToolExecutor` destroyed before async page-context callback fires
**Surface:** browser/ai · **File:** `maho_chat_orchestration_service.cc:608`
```cpp
MahoBrowserToolExecutor executor(browser_, browser_tools_v1_enabled_);
executor.Execute(tool_invocation.name, arguments,
    base::BindOnce(&MahoChatOrchestrationService::OnToolInvocationComplete,
                   weak_factory_.GetWeakPtr(), state, tool_invocation));
// executor goes out of scope here — its async work is still pending
```
The executor’s internal `MahoAiPageContextExtractor` posts an async `ExecuteJavaScriptInIsolatedWorld` bound to its own `WeakPtrFactory`. When `executor` dies at scope exit, that weak ptr invalidates, the JS callback silently no-ops, `OnToolInvocationComplete` never fires, and `std::shared_ptr<ConversationRoundState>` (with user message + up to 50KB page context) is leaked indefinitely. **Every** tool round using page context (4 of 5 registered tools) is affected.
**Fix:** Make the executor a member of `MahoChatOrchestrationService` (or of `ConversationRoundState`) so it outlives the async chain.

### C3 — Sidebar state rebuild: 3+ synchronous FFI calls + per-byte favicon JSON parse on UI thread
**Surface:** views · **File:** `browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` (1050 lines)
`BuildViewStateModel` fans out to `GetCoreFooterSpaceState`, `BuildTabListModel` (separate FFI for sidebar tree), `BuildFavoritesModelForSpaceIdJson` (separate FFI for favorites) — all synchronous, all on UI thread. Favicon bytes arrive as JSON arrays of integers (one `base::Value::Int` per byte), then a recursive `ParseTreeNode` rebuilds the byte vector. For a sidebar with 50 tabs each carrying a 1KB favicon = 50,000 `base::Value::Int` allocations parsed per state rebuild.
**Fix:** Single `maho_core_get_sidebar_state` FFI returning a binary/flatbuffer payload, or at least one JSON call. Encode favicons as base64 strings (or zerocopy shared mem), not integer arrays.

### C4 — Per-Browser static map never evicts entries — `MahoBoostWindowController`
**Surface:** views · **File:** `browser/ui/views/boost/maho_boost_window_controller.cc:35-39`
```cpp
std::map<Browser*, std::unique_ptr<MahoBoostWindowController>>&
GetBrowserControllersMap() {
  static auto* map =
      new std::map<Browser*, std::unique_ptr<MahoBoostWindowController>>();
  return *map;
}
```
Entries inserted in `GetForBrowser()` are never removed on Browser destruction. Long sessions leak controllers and accumulate dangling `Browser*` keys (UAF risk on pointer recycling).
**Fix:** Make it `BrowserUserData<MahoBoostWindowController>` or add `BrowserListObserver::OnBrowserRemoved` cleanup.

### C5 — `MahoUnifiedAgentAdapter::user_data_handle_` leaked per `Reset()` cycle
**Surface:** browser/ai · **File:** `maho_unified_agent_adapter.cc:112, 168`
```cpp
// SubmitMessage:
user_data_handle_ = new std::weak_ptr<FfiUserData>(ffi_user_data_);
// Reset:
user_data_handle_ = nullptr;   // never deleted
```
Comment claims "16 bytes per adapter lifetime", but `Reset()` runs on every "New Chat" click. Bounded but persistent.
**Fix:** Free the handle in `Reset()` before nulling, or replace with an atomic-flag wrapper that doesn't require leaking.

### C6 — Preview-bytes free function relies on undocumented `Box<[u8]>`/`Vec<u8>` allocator compatibility
**Surface:** FFI · **File:** `maho/crates/maho-ffi/src/lib.rs:2357-2381`
```rust
let boxed = bytes.into_boxed_slice();
Box::into_raw(boxed) as *mut u8         // alloc — discards slice length metadata
// ... later:
let _ = Vec::from_raw_parts(data, len, len);  // free — assumes cap == len
```
Works today only because `into_boxed_slice` shrinks capacity; allocator changes (custom allocators, jemalloc swap, Rust toolchain update) can turn this into silent heap corruption.
**Fix:**
```rust
let slice = std::slice::from_raw_parts_mut(data, len);
drop(Box::from_raw(slice));
```

### C7 — Import-module FFI exports unwrap panics across the C ABI
**Surface:** FFI · **File:** `maho/crates/maho-ffi/src/import.rs:27, 128, 262, 278`
Five `pub unsafe extern "C" fn` exports (`maho_import_detect_browsers`, `maho_import_orchestrator_start/cancel/free`, `maho_import_string_free`) are **not** wrapped with `ffi_safe!`. Filesystem I/O + JSON parsing inside them can panic, which unwinds across the C ABI = instant UB.
**Fix:** Wrap every `extern "C" fn` in `import.rs` with `ffi_safe!(..., fallback)`.

---

## High findings

### H1 — `base::Unretained(helper)` crosses IO→UI thread boundary in ad-block path
**Surface:** chromium_src override · **File:** `chromium_src/chrome/browser/chrome_content_browser_client.cc:26-28`
```cpp
on_blocked = base::BindRepeating(
    &MahoAdBlockTabHelper::IncrementBlockedCount, base::Unretained(helper));
```
Throttle lives on IO thread, tab helper is UI-thread `WebContentsUserData`. Request completing after tab close → UAF on helper.
**Fix:** Use `helper->GetWeakPtr()` + `PostTask(UI, ...)` wrapper.

### H2 — `IsCorePinnedTab` JSON-parses all tabs on every tab close
**Surface:** non-AI · **File:** `browser/maho_tab_registry.cc:52-82`
Synchronous FFI returning full tab-model JSON + full parse + linear scan, invoked from `OnTabStripModelChanged` for every removal. Batch-close-other-tabs amplifies this O(N) cost into O(N²).
**Fix:** Add a dedicated `maho_core_is_tab_pinned(core, tab_id)` FFI returning a bool.

### H3 — `BuildTabsJson` serializes all tabs across all windows on every command-palette keystroke
**Surface:** views · **File:** `browser/ui/views/command/maho_command_model.cc:461-498`
Runs on UI thread before the debounced search posts to thread pool. At 100 tabs, 10–50ms of synchronous work per keystroke.
**Fix:** Cache + invalidate via `TabStripModelObserver`; or move the iteration onto the thread pool.

### H4 — `MahoSpaceFFIClient` caches `MahoCore*` at construction
**Surface:** non-AI · **File:** `browser/importer/maho_space_ffi_client.cc:126`
```cpp
MahoSpaceFFIClient::MahoSpaceFFIClient() : core_(maho::GetCore()) {}
```
Singleton constructed lazily; if first access happens before `PostEarlyInitialization`, `core_` is permanently nullptr and every subsequent `CreateSpace`/`CreateTab` silently no-ops (silent data loss for import flows).
**Fix:** Remove the cached `core_` member; call `maho::GetCore()` fresh in each method.

### H5 — `BrowserUserData<MahoNotificationOverlay>` static map can leak entries
**Surface:** non-AI · **File:** `browser/ui/notifications/maho_notification_overlay.{cc,h}`
If `GetOrCreateForBrowser` runs while `BrowserView::GetWidget()` is still null, no widget observer is registered → entry persists forever (dangling `Browser*` key + leaked overlay).
**Fix:** Add `BrowserListObserver::OnBrowserClosing` fallback cleanup.

### H6 — Chat/agent/routines FFI callbacks have no lifetime contract for `user_data`
**Surface:** FFI · **File:** `maho_ffi.h:1588-1594, 1743-1750, 1881-1885`
`maho_core_chat_set_event_sink`, `maho_agent_send_message`, `maho_routines_run` all take a `void* user_data` callback context. Callbacks fire from background threads. Nothing prevents C++ from destroying the owner before the next callback. UAF on the C++ side.
**Fix:** Document that `cancel()` synchronously guarantees no further callbacks; or require ref-counted user_data on C++ side.

### H7 — `MahoFullPageCaptureClient` uses `delete this` with no leak path for unresponsive agent host
**Surface:** views · **File:** `browser/ui/views/maho_dom_screenshot/maho_full_page_capture_client.cc`
Self-deletion in `Finish()` + `AgentHostClosed()`. If the agent host neither sends a response nor closes (frozen renderer), the client leaks forever.
**Fix:** Add a watchdog timer with deadline → `Finish()` after timeout.

### H8 — `unbounded_channel()` used in 4 streaming paths
**Surface:** Rust core · **Files:**
- `stream_utils.rs:38`
- `llm_provider.rs:78`
- `memory_manager.rs:77` (long-lived task result channel)
- `providers/anthropic.rs:29`
Slow consumer → unbounded `Vec` growth in channel buffer. The memory_manager one is the most dangerous (long-lived).
**Fix:** Replace with `mpsc::channel(1024)` and propagate backpressure.

### H9 — `reqwest::Client::new()` constructed per request (no connection pooling)
**Surface:** Rust core · **Files:** `llm_provider.rs`, `providers/anthropic.rs`, `llm_client.rs` (6 occurrences)
Each construction rebuilds the connection pool, DNS resolver, TLS cache. With token-streaming LLM calls this is the single biggest avoidable latency.
**Fix:** Static `Lazy<reqwest::Client>` (or per-provider `Arc<Client>`).

### H10 — `block_on(get_runtime(), ...)` inside async-callable paths
**Surface:** Rust core · **Files:** `llm_provider.rs:31` (`validate_api_key`), `chat_session.rs:161`
If invoked from within a task on the shared runtime, `block_on` deadlocks the runtime (worker count exhausted).
**Fix:** Make the affected functions `async`, or run them on a dedicated blocking thread.

### H11 — `MahoMemoryManager::active_tasks: Vec<JoinHandle>` never aborted on drop
**Surface:** Rust core · **File:** `memory_manager.rs:93/148/161/173`
Only cleaned in `drain_results()`. No `Drop` impl. If `MahoCore` is dropped (test teardown, profile destroy) without `drain_results`, all background tasks keep running detached.
**Fix:** Implement `Drop` that calls `.abort()` on every handle.

### H12 — `sync_manager::entity_versions: HashMap<String, u64>` grows unbounded
**Surface:** Rust core · **File:** `sync_manager.rs`
Every synced entity inserts one entry. Never cleared (no eviction, no per-space scoping). Long-running sync sessions = unbounded `HashMap`.
**Fix:** Bounded LRU keyed by recency; or persist + purge.

### H13 — `accumulated_text_` (chat) and `full_text` (LLM) grow without size cap
**Surface:** browser/ai · **Files:** `maho_unified_agent_adapter.cc:321-323`, `maho_ai_llm_client.cc:200/242`
A misconfigured/malicious endpoint streaming forever or a hung Rust session → unbounded heap.
**Fix:** Add max-response-size sentinel (e.g. 2MB) and abort with error.

### H14 — `LOG(ERROR)` unconditional dump of raw LLM JSON response on every turn
**Surface:** browser/ai · **File:** `maho_chat_orchestration_service.cc:433`
Production `LOG(ERROR)` writing 10–50KB per chat turn. Disk + log-lock contention + log noise.
**Fix:** `DVLOG(1)` or feature-flag.

### H15 — Import orchestrator shares `*mut MahoCore` across threads with `unsafe impl Sync` and no lock
**Surface:** FFI · **File:** `maho/crates/maho-ffi/src/import_destination.rs:33-38`
C++ holds the same `MahoCore*` after starting an import; the import thread mutates it. Any concurrent FFI call from C++ (even read-only `get_tab_view_models`) is a data race.
**Fix:** Either gate FFI calls with an `AtomicBool` "import in progress", or add internal `RwLock` in `MahoCore`.

---

## Medium findings (summary — full detail in sub-reports)

### Memory / leaks
- **M1** `tool_dispatcher.rs::permission_cache` — bounded only by unique tool names; `active_tool_rounds` HashMap leaks entries when a tool round terminates without re-entering the response handler.
- **M2** `llm_manager.rs::preview_cache: HashMap<String,(String,String)>` — no eviction.
- **M3** `sync_manager::outgoing_messages` and `pending_pushes` — append-only if network is down and `flush()` never runs.
- **M4** `notification_manager::notifications` — append-only Vec; only shrinks on explicit dismiss.
- **M5** `MahoSplineLinePathView::OnPaint` and `SearchIconView::OnPaint` — `SkPath` rebuilt per paint (cache + rebuild only on bounds change).
- **M6** `widget_.release()` in `MahoBoostWindowController::OnDelegateWindowClosing` — releases ownership without destruction; relies on `OnWidgetDestroying` firing.
- **M7** `base::Unretained(local_state)` in 60-second delayed task — `maho_update_manager.cc:178`. UAF on abnormal shutdown.
- **M8** `LiveFolderItemCache::items_by_config_` — `NoDestructor` singleton with no eviction; bounded in practice by unique configs, but still grows monotonically.
- **M9** `engine_favicon_cache_` in `maho_command_model.h:183` — bounded by model lifetime (palette open/close), but stash + invalidate would be cheaper.

### Performance / UI-thread blocking
- **M10** `maho::core::CheckRequest` synchronous FFI on UI thread per network request (`maho_ad_block_throttle.cc:63`).
- **M11** `maho::core::GetCosmeticResources` + JSON parse called twice per main-frame navigation (`maho_cosmetic_filters_handler.cc`).
- **M12** `maho_core_add_history_entry` synchronous FFI on UI thread per navigation (`maho_tab_registry.cc:227`).
- **M13** `ReinjectBoostIntoTabsForDomain` — full all-windows × all-tabs string-compare per boost update.
- **M14** Boost picker mode: 100ms polling `ExecuteJavaScriptInIsolatedWorld` instead of push-based messaging.
- **M15** `MahoBoostInjectionHandler` JSON parse + script injection per navigation in `ReadyToCommitNavigation`.
- **M16** O(n²) SSE buffer processing via repeated `erase(0, pos+2)` on growing string (`maho_ai_llm_client.cc:152`) — ~4MB of memmove for a 20KB streaming response.
- **M17** `prompt_messages.Clone()` deep-copies entire conversation history each tool round (`maho_chat_orchestration_service.cc:517`), up to `kMaxToolRounds=4` times per turn.
- **M18** Full 50KB page-text `base::ToLowerASCII` allocation just to do case-insensitive `find()` (`maho_browser_tool_executor.cc:278`).
- **M19** `to_json_cstring` chains 3 allocations per FFI return; `command_bar_search` re-serializes/de-serializes all tabs per keystroke (every 50–100ms).
- **M20** `maho_core_should_block_request` synchronous on network thread with no caching layer.
- **M21** `serde_json` (de)serialization on every FFI return — single biggest avoidable FFI cost (P-1 in sub-report).

### Code-quality / robustness
- **M22** `AssertUnwindSafe` in `ffi_safe!` leaves `MahoCore` in inconsistent state on panic; no "poisoned" flag rejects future calls.
- **M23** Two duplicate string-free entrypoints (`maho_string_free` vs `maho_import_string_free`) — divergence risk.
- **M24** `maho_routines_list(char** out)` uses out-param while every other API returns `char*`.
- **M25** `maho_core_get_zoom` panic fallback is `0.0` (zoom-to-0%) instead of `1.0` (no zoom).
- **M26** `maho_core_save_form_data` uses `unwrap_or("")` on invalid UTF-8 (silent data corruption, inconsistent with every other function in the file).
- **M27** `maho_core_update_tab_preview` accepts `data_len` from C++ unverified — out-of-bounds read if caller lies.

---

## Notes / acceptable patterns

- `MahoAiPopupLifetimeTracker` correctly uses `BrowserCollectionObserver` + bidirectional map cleanup.
- FFI `Box::into_raw`/`Box::from_raw` correctly paired for `MahoCore` and `MahoImportSession`.
- `cbindgen`-generated header reduces signature drift (as long as it's regenerated on Rust changes).
- C++ overlay consistently uses `ScopedObservation` rather than naked `AddObserver`/`RemoveObserver`.
- `MahoTabIdHelper` uses `WebContentsUserData` — auto-cleans on contents destruction.
- WebUI Mojo pipes (in `browser/resources/*/app.ts`) need no explicit `close()` — auto-closed on frame teardown.
- `os_crypt` key derivation correctly dispatched off UI thread.
- `chromium_src/` overrides are all documentation-only reference files; runtime patches inserted by `apply_chromium_src_overrides.py`. No hot-path C++ shipping from `chromium_src/` itself.
- `maho-engine` is a `StubWebViewEngine` only — not on the production path.

---

## Recommended action order

**Tier 1 — Fix immediately (1 day total):**
1. C6 — fix `Box<[u8]>` free pattern (5 min)
2. H9 — static `reqwest::Client` (15 min × 6 files)
3. H8 — replace `unbounded_channel()` with bounded (20 min × 4 files)
4. H14 — `DVLOG(1)` for LLM raw response (1 min)
5. M25 — `maho_core_get_zoom` fallback `1.0` (1 min)
6. M26 — `maho_core_save_form_data` match-error pattern (3 min)
7. C7 — wrap `import.rs` FFI exports in `ffi_safe!` (15 min)

**Tier 2 — Fix within sprint (1 week total):**
8. C2 — promote `MahoBrowserToolExecutor` to member (4 hr)
9. C5 — fix `user_data_handle_` leak in `Reset()` (1 hr)
10. C4 — convert `MahoBoostWindowController` to `BrowserUserData<>` (4 hr)
11. H2 — add `maho_core_is_tab_pinned` FFI (2 hr)
12. H4 — remove `core_` cache from `MahoSpaceFFIClient` (30 min)
13. H5 — `BrowserListObserver` fallback for notification overlay (2 hr)
14. H7 — watchdog timer on `MahoFullPageCaptureClient` (1 hr)
15. H10 — make `validate_api_key` async (2 hr)
16. H11 — `Drop for MahoMemoryManager` (1 hr)
17. H13 — max-response-size sentinel (1 hr)
18. M16 — SSE buffer cursor pattern (1 hr)

**Tier 3 — Architectural / requires design (1 month):**
19. C1 — UI-thread invariant fix for 32 settings handlers (1 day refactor + tests)
20. C3 — sidebar state binary protocol + base64 favicons (2 days)
21. H6 — formalize FFI callback lifetime contract (design doc + ABI change)
22. H15 — `MahoCore` internal `RwLock` or import-progress gating (2 days)
23. H12 — bounded `entity_versions` with persistence (3 days)
24. M21 — shared-memory / flatbuffers for hot FFI returns (1 week)

---

## Out of scope confirmations

- Upstream Chromium not audited (per request).
- Mail / Website / Android-shell / iOS-shell not audited.
- `maho-cli`, `maho-bench`, `maho-agent`, `maho-jni`, `maho-import`, `maho-extension` Rust crates not audited.
- Tests (`*_unittest.cc`, `*_browsertest.cc`, `tests/`) not audited.

For audit re-runs on a specific surface, see the orchestration prompt at the head of each per-surface sub-report in `docs/` and `maho-chromium/docs/`.
