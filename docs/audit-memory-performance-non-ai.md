# Memory Leak & Performance Bottleneck Audit — Maho Chromium Overlay (Non-AI)

**Scope:** `maho-chromium/browser/` (excluding `ai/`, `ui/views/`, tests) + `maho-chromium/components/`  
**Date:** 2026-06-28  
**Branch:** main

---

## Critical

### C1. Settings Page Handler Calls maho-core FFI from Thread Pool (Thread-Safety Violation)

**File:** `browser/ui/webui/maho_settings/maho_settings_page_handler.cc:276-528`

Multiple `*OnPool()` helper functions are dispatched to `base::ThreadPool` yet call `maho::GetCore()` + `maho_core_*` FFI functions that are documented as **UI-thread-only** (INV-8: "FFI calls to `maho_core_handle_event` MUST be on UI thread").

```cpp
// Line 278-280
std::vector<...> BuildProfilesOnPool() {
  MahoCore* core = maho::GetCore();   // atomic read OK
  char* json_str = maho_core_list_profiles(core);  // FFI from thread pool!
```

**Affected functions (32 total):** `BuildProfilesOnPool`, `CreateProfileOnPool`, `DeleteProfileOnPool`, `SwitchProfileOnPool`, `BuildATCRulesOnPool`, `AddATCRuleOnPool`, `RemoveATCRuleOnPool`, `ToggleATCRuleOnPool`, `BuildShortcutsOnPool`, `SetShortcutOnPool`, `ResetShortcutOnPool`, `ResetAllShortcutsOnPool`, `ToggleShortcutOnPool`, `BuildSyncStatusOnPool`, `BuildSyncDevicesOnPool`, `GenerateSyncKeyOnPool`, `StartSyncOnPool`, `JoinSyncOnPool`, `StopSyncOnPool`, `ApplyThemeToSpaceOnPool`, `BuildSpacesOnPool`, `BuildAutofillAddressesOnPool`, `BuildAutofillPaymentsOnPool`, `BuildFilterListsOnPool`, `RebuildContentRulesOnPool`, `AddFilterListOnPool`, `ToggleFilterListOnPool`, `RemoveFilterListOnPool`, `BuildInstalledExtensionsOnPool`, `AddAutofillAddressOnPool`, `DeleteAutofillAddressOnPool`, `AddAutofillPaymentOnPool`, `DeleteAutofillPaymentOnPool`.

**Why bad:** If `maho-core` (Rust LMDB-backed library) does not guarantee internal thread safety for concurrent readers/writers, this causes data races, corruption, or undefined behavior. Even if the Rust side uses internal locks, this violates the project's stated invariant.

**Suggested fix:** Move all FFI calls to the UI thread. Use `PostTaskAndReplyWithResult` only for the response marshaling/UI update. Or use a `base::SequencedTaskRunner` bound to the UI thread for the FFI calls, off-loading only the Mojo reply to the original sequence.

---

### C2. `IsCorePinnedTab` — Full Tab Model JSON Deserialize on Every Tab Close

**File:** `browser/maho_tab_registry.cc:52-82`

```cpp
bool IsCorePinnedTab(const std::string& tab_id) {
  MahoCore* core = maho::GetCore();
  char* json_str = maho_core_get_tab_view_models(core);  // Synchronous FFI
  std::string json(json_str);
  maho_string_free(json_str);
  std::optional<base::Value> parsed = base::JSONReader::Read(json, ...);
  for (const auto& item : parsed->GetList()) { ... }
```

Called from `StripObserver::OnTabStripModelChanged` (line 331) on **every** tab removal event. Deserializes the entire tab model JSON blob + O(n) linear scan on the UI thread for each close.

**Why bad:** O(n) JSON parse + O(n) iteration on the UI thread in a hot path. With 100+ tabs, this causes jank on close. Compounded if multiple tabs are batch-closed (e.g., "Close other tabs").

**Suggested fix:** Add a dedicated `maho_core_is_tab_pinned(core, tab_id)` FFI that performs the lookup in Rust without serializing the entire model. Or cache pinned state locally in `dispatch_cache_`.

---

## High

### H1. `MahoSpaceFFIClient` Caches `core_` Pointer at Construction — Stale if Init Order Changes

**File:** `browser/importer/maho_space_ffi_client.cc:126`

```cpp
MahoSpaceFFIClient::MahoSpaceFFIClient() : core_(maho::GetCore()) {}
```

This is a `base::NoDestructor<MahoSpaceFFIClient>` singleton (line 117-118). If any code path calls `GetInstance()` before `PostEarlyInitialization` sets the core pointer, `core_` is permanently `nullptr` and all subsequent `CreateSpace`/`CreateTab`/`PinTab` calls silently no-op.

**Why bad:** Silent data loss — imported bookmarks, tabs, and spaces are never persisted to the Rust model. Difficult to diagnose because there are no DCHECKs or error logs.

**Suggested fix:** Remove the cached `core_` member. Call `maho::GetCore()` fresh inside each method (consistent with `MahoTabRegistry`, `MahoExtensionStateBridge`, and other overlay singletons).

---

### H2. `BrowserUserData<MahoNotificationOverlay>` Static Map — Potential Dangling Entry Leak

**File:** `browser/ui/notifications/maho_notification_overlay.h:56-58`

```cpp
static std::map<Browser*, std::unique_ptr<T>>& GetInstances() {
  static base::NoDestructor<std::map<Browser*, std::unique_ptr<T>>> instances;
  return *instances;
}
```

**File:** `browser/ui/notifications/maho_notification_overlay.cc:16-24`

```cpp
MahoNotificationOverlay::MahoNotificationOverlay(Browser* browser)
    : BrowserUserData<MahoNotificationOverlay>(browser), browser_(browser) {
  if (browser_) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
    if (bv && bv->GetWidget()) {
      parent_widget_observed_ = bv->GetWidget();
      parent_widget_observed_->AddObserver(this);
```

If `GetOrCreateForBrowser` is called when the BrowserView has no widget yet (early in lifecycle), `parent_widget_observed_` remains `nullptr`. Neither cleanup path (`OnWidgetDestroying` for owned widget or parent widget) fires — the entry persists forever with a dangling `Browser*` key.

**Why bad:** Dangling pointer as map key + leaked `MahoNotificationOverlay` instance. Potential UAF if the map is consulted after the Browser is destroyed.

**Suggested fix:** Add a `BrowserListObserver::OnBrowserClosing` fallback that calls `RemoveFromBrowser`. Or defer `GetOrCreateForBrowser` until the BrowserView widget is guaranteed to exist.

---

### H3. `base::Unretained(local_state)` in 60-Second Delayed Task

**File:** `browser/updates/maho_update_manager.cc:176-179`

```cpp
base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
    FROM_HERE,
    base::BindOnce(&updates::CrashLoopSentinel::MarkStable,
                   base::Unretained(local_state), current_version),
    base::Seconds(60));
```

`local_state` is a raw `PrefService*` from `g_browser_process->local_state()`. If the browser shuts down in under 60 seconds, the task may execute against freed memory.

**Why bad:** Potential UAF during rapid shutdown or test harness teardown.

**Suggested fix:** Store the task as a `base::CancelableOnceCallback` member on `MahoUpdateManager` and cancel it in `Shutdown()`. Or wrap in `base::BindOnce([] { if (g_browser_process) ... })` guard.

---

## Medium

### M1. Synchronous `maho::core::CheckRequest` FFI Per Network Request on UI Thread

**File:** `browser/net/maho_ad_block_throttle.cc:47-63`

```cpp
void MahoAdBlockThrottle::WillStartRequest(network::ResourceRequest* request, bool* defer) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  auto result = maho::core::CheckRequest(core, url.c_str(), source.c_str(), type);
```

Called for **every** network request. The Rust ad-block engine is fast, but the FFI boundary + string marshaling compounds on pages with 100+ subresources.

**Why bad:** Blocks UI thread for the duration of the Rust filter match per request. Cumulative latency on heavy pages.

**Suggested fix:** Move to IO thread, batch requests, or use a Mojo-based async filter service.

---

### M2. Synchronous `GetCosmeticResources` + JSON Parse — Called Twice Per Navigation

**File:** `browser/net/maho_cosmetic_filters_handler.cc:51-89`

Called in both `ReadyToCommitNavigation` and `DidFinishNavigation`, meaning it fires **twice** per main-frame navigation on the UI thread.

```cpp
void MahoCosmeticFiltersHandler::InjectCosmeticFilters(...) {
  std::string json = maho::core::GetCosmeticResources(core, url.c_str());
  auto parsed = base::JSONReader::Read(json, ...);
```

**Why bad:** Double invocation; JSON parsing of cosmetic rules on the UI thread per navigation.

**Suggested fix:** Cache the result by URL. Skip `DidFinishNavigation` call if `ReadyToCommitNavigation` already injected for the same URL.

---

### M3. `maho_core_add_history_entry` — Synchronous FFI Per Cross-Document Navigation

**File:** `browser/maho_tab_registry.cc:227`

```cpp
maho_core_add_history_entry(core, url_spec.c_str(), title.c_str());
```

Called from `NavObserver::DidFinishNavigation` and again from `TitleWasSet` for the same navigation.

**Why bad:** Synchronous LMDB write on UI thread per navigation. Potentially blocks on disk I/O.

**Suggested fix:** Debounce or batch history writes on a background sequence.

---

### M4. `LiveFolderItemCache` — Unbounded, Never Evicted

**File:** `browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.cc:26-29`

```cpp
void LiveFolderItemCache::SetItems(const std::string& config_json,
                                   std::vector<LiveFolderCachedItem> items) {
  items_by_config_[config_json] = std::move(items);
}
```

`items_by_config_` grows with each unique folder configuration. Only removed on explicit `Clear(config_json)` call.

**Why bad:** Memory growth proportional to number of unique folder configurations ever created during process lifetime (NoDestructor singleton).

**Suggested fix:** Add LRU eviction or cap at a reasonable maximum (e.g., 50 entries). In practice, likely bounded to ~10 folders.

---

### M5. `ReinjectBoostIntoTabsForDomain` — Full TabStripModel Iteration Per Boost Update

**File:** `browser/net/maho_boost_injection_handler.cc:88-129`

Iterates ALL tabs in ALL browser windows on every boost rule update event. O(windows × tabs) string comparisons on UI thread.

**Why bad:** Jank when boost rules are modified while many tabs are open.

**Suggested fix:** Maintain an index of open URLs → WebContents, or limit to the active window.

---

### M6. Boost Injection Polling Timer — 100ms JS Execution While Picker Active

**File:** `browser/net/maho_boost_injection_handler.cc:36, 327-331`

```cpp
constexpr base::TimeDelta kPollInterval = base::Milliseconds(100);
poll_timer_.Start(FROM_HERE, kPollInterval,
    base::BindRepeating(&MahoBoostInjectionHandler::PollContentScript, ...));
```

10 JS round-trips/second via `ExecuteJavaScriptInIsolatedWorld` while the element picker is active. Each involves IPC to renderer and back.

**Why bad:** Unnecessary UI-thread + renderer load. Should be push-based.

**Suggested fix:** Replace with `chrome.runtime.sendMessage` or dedicated Mojo interface from the content script.

---

## Notes

### N1. `dispatch_cache_` Entries Not Erased on Shutdown

**File:** `browser/maho_tab_registry.cc:306-314`

During `browser_shutdown::IsTryingToQuit()`, kRemoved handler skips `EraseDispatchCache`. Entries remain until process exit. **Impact:** Minimal — process is terminating.

### N2. Observer Patterns — Correct Throughout

All in-scope code uses `base::ScopedObservation` or symmetric `Add/RemoveObserver` pairs. No orphaned observer patterns detected outside of H2.

### N3. `MahoUpdateManager` Singleton — `base::Unretained(this)` in Timer

**File:** `browser/updates/maho_update_manager.cc:411`

Safe because it's a `base::Singleton` (never destroyed). Documented for completeness.

### N4. Mojo Disconnect Handlers Not Set (Acceptable)

WebUI page handlers don't set explicit `set_disconnect_handler` on their `mojo::Receiver`. Acceptable because handler lifetime is tied to `WebUIController` → `WebContents` navigation.

### N5. `os_crypt` Key Derivation — Correctly Off UI Thread

**File:** `browser/os_crypt/maho_file_key_provider_mac.mm`

File I/O and PBKDF2 correctly dispatched to thread pool. No blocking on UI thread.

---

## Summary

| Severity | Count | Key Theme |
|----------|-------|-----------|
| Critical | 2 | Thread-safety (FFI from thread pool), hot-path JSON deserialization |
| High | 3 | Stale pointer cache, static map leak, Unretained in delayed task |
| Medium | 6 | Synchronous FFI on UI thread, unbounded caches, polling |
| Notes | 5 | Acceptable patterns documented for completeness |

**Highest-impact fix:** C1 (32 thread-safety violations in settings handler) — systematic pattern, likely affects all Settings WebUI operations.  
**Easiest win:** H1 (one-line fix: remove cached `core_` member).  
**Best perf improvement:** C2 (add `maho_core_is_tab_pinned` FFI to eliminate O(n) JSON parse per tab close).
