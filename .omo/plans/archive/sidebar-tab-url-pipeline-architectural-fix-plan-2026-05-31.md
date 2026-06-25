# Sidebar Tab URL Pipeline — Architectural Fix Plan (Option C)

**Created:** 2026-05-31
**Last Revised:** 2026-05-31 (post-Momus, post-Zen/Arc/upstream-Chromium investigation)
**Author:** Sisyphus (architectural exploration via background explore agents)
**Scope:** `maho-chromium/browser/maho_tab_registry.{cc,h}` + `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.{cc,h}` + `maho/crates/maho-core/src/event_dispatcher.rs` + `maho/crates/maho-types/src/events/shell_event.rs` + tests
**Status:** Plan v1.1 / Momus-reviewed [OKAY] / Ready to execute
**Estimated effort:** ~3–5 dev-hours (excluding full Chromium rebuild time)
**Provenance:**
- Deep architectural trace (`bg_f6b339d5`, 4m 8s) confirmed pipeline state.
- User flagged the leaf-level URL filter as anti-pattern; plan formalizes the root-cause fix.
- Momus review (`bg_b9ce212a`) verified all file paths and line numbers; verdict `[OKAY]`.
- Zen/Arc/upstream-Chromium investigation (`bg_c8d655f3`, 4m 17s) revealed:
  - Zen does NOT have a "last dispatched URL/title" dedup cache; uses Firefox's `permanentKey` swap discipline + `tab.id` DOM identity instead.
  - Arc is closed source; its `StorableSidebar.json` `savedURL`/`savedTitle` are persistence metadata, not runtime dedup.
  - **Upstream Chromium** ([`components/tabs/public/tab_interface.h`](https://chromium.googlesource.com/chromium/src/+/refs/tags/141.0.7360.10/components/tabs/public/tab_interface.h)) explicitly recommends `RegisterWillDiscardContents()` callback-based cache migration over per-WebContents caching.
- Decision: caching adopts the upstream Chromium pattern (Option B' — Registry-keyed map with explicit migration in the existing `kReplaced` branch). See §3 Decision 3.

---

## 0 — Executive Summary

The current `IsSidebarIneligibleUrl` filter in `maho_sidebar_state_adapter.cc:37-51` is a **symptom-level patch** for a deeper structural gap:

| Layer | State |
|---|---|
| `ShellEvent::TabUrlUpdated` (Rust enum) | ✅ Defined (`shell_event.rs:427-430`) |
| `event_dispatcher::TabUrlUpdated` (Rust handler) | ✅ Wired (`event_dispatcher.rs:1019-1028`) |
| `TabLifecycleManager::update_tab_url` (Rust mutator) | ✅ Implemented (`tab_lifecycle.rs:1032`) |
| C++ dispatch site for `tab_url_updated` | ❌ **MISSING** |
| Symptom workaround: `OverlayActiveTabFields` | Active tab only — masks the gap for the visible tab |
| Symptom workaround: `IsSidebarIneligibleUrl` | Filters NTP/about/system URLs at leaf — masks the gap for inactive tabs |

The fix is **one C++ dispatch call** in `NavObserver::MaybeDispatch`, plus removal of the workarounds and consolidation of URL classification into `MahoDisplayPolicy`. The Rust side is already complete.

**Cache strategy** (Option B', per upstream Chromium's `TabInterface::RegisterWillDiscardContents` pattern): a `MahoTabRegistry` member map keyed by stable TabId, with explicit migration in the existing `kReplaced` branch and erasure on `kRemoved`. Survives tab discard / prerender swap without redundant dispatch.

**Key insight that shrinks scope:** Rust infrastructure is fully built. This is a wiring fix, not a redesign.

### v1.1 changes (2026-05-31, post-Momus, post-investigation)

| Section | v1.0 | v1.1 |
|---|---|---|
| Cache location (§3 D3, §5) | Per-`NavObserver` (Option A) | Per-`MahoTabRegistry` map keyed by TabId with explicit `kReplaced` migration (Option B', upstream Chromium pattern) |
| §6.3 regression test | Interactive browser test | `ParseTreeNode` unit test |
| §13 | 3 open questions for reviewer | All 3 resolved by user input |
| Risks | R1–R8 | R1–R10 (R9, R10 added for B'-specific concerns: cache cleanup + swap semantics) |
| Phase strategy | Phase-by-phase deployment recommended | Single PR covering all 4 phases (per user direction) |

---

## 1 — Background & Verified Pipeline State

```
Chromium TabStripModel
   │ (every tab, no filter)
   ▼
[C++] MahoTabRegistry::StripObserver::OnTabStripModelChanged
       │ kInserted → DispatchCreateTabForContents (maho_tab_registry.cc:68-98)
       │ kRemoved  → DispatchCloseTabForContents
       │ kReplaced → identity carry-over (discard/prerender swap)
       ▼
[C++] DispatchShellEventLocal("create_tab", {tab_id, space_id, url})
       │  ← URL is current GetVisibleURL() OR GetLastCommittedURL(); NO filtering
       ▼
[C++] NavObserver::DidFinishNavigation (maho_tab_registry.cc:124)
       │  ← currently calls maho_core_add_history_entry ONLY
       │  ❌ does NOT dispatch tab_url_updated
       │  ❌ does NOT dispatch tab_title_updated
       ▼
[FFI] maho_core_handle_event → maho_bridge.cc:326 → event_dispatcher
       ▼
[Rust] event_dispatcher.rs::ShellEvent::CreateTab (line 255-276)
       │  resolved_url = url || "chrome://newtab"
       │  tab_mgr.create_tab_with_id() OR tab_mgr.create_tab()
       │  space_mgr.add_tab_to_space()
       ▼
[Rust] core stores Tab { url: <whatever was at create time> }
       │  ⚠️  URL is NEVER updated for inactive tabs
       │  ⚠️  TabUrlUpdated handler exists but is unreachable
       ▼
[Rust] space_manager::get_sidebar_tree → JSON
       │  no URL filtering
       ▼
[C++] MahoSidebarStateAdapter::ParseTreeNode (state_adapter.cc:301-441)
       │  ⚠️  IsSidebarIneligibleUrl()  ← anti-pattern leaf filter
       │  ⚠️  IsGhostTab() (intentional, per state_models.h:22-25)
       ▼
[C++] OverlayActiveTabFields (state_adapter.cc:502-522)
       │  ← active tab ONLY — overwrites with live WebContents URL
       │  This is the workaround that hides the missing TabUrlUpdated
       ▼
[Render] MahoSidebarTabListView
```

**Inactive tab transition case** (the actual user bug):
1. User opens new tab → `chrome://newtab` registered in core
2. User navigates to `https://example.com` → core's `Tab.url` stays `chrome://newtab`
3. User switches to another tab → original tab is no longer active
4. `OverlayActiveTabFields` no longer applies; `ParseTreeNode` sees stale `chrome://newtab`
5. `IsSidebarIneligibleUrl` returns true → **tab disappears from sidebar entirely**

This is the bug masked by `&& !is_active` in the original filter. The filter exists to compensate for the stale URL, but the right fix is to keep the URL fresh.

---

## 2 — Hard Constraints (must hold throughout)

### C1 — `MahoTabIdHelper` UUID is the source of truth
The "Architectural invariant" comment in `maho_tab_registry.h` mandates that Rust never invents tab UUIDs. C++ must always pass the existing `helper->stable_tab_id()` in any `tab_url_updated` event. NEVER generate a new UUID for URL updates.

### C2 — TabReplaced (discard/prerender swap) preserves UUID
`StripObserver::OnTabStripModelChanged` (line 210-235) handles `kReplaced` by carrying the old UUID over to the new `WebContents`. The new `NavObserver` attached to the swapped contents must continue to dispatch `tab_url_updated` against the SAME UUID. Confirm that `MahoTabIdHelper::FromWebContents(new_contents)->stable_tab_id()` returns the carried-over ID before dispatching. If not, the swap path needs to attach the helper before dispatch.

### C3 — `IsGhostTab` filter remains
`state_models.h:22-25` documents the design intent: ghost/url-less tabs (empty url + empty title) must never enter core state. This is correct and orthogonal to URL classification. Keep `IsGhostTab` as-is.

### C4 — Off-the-record + non-normal browsers stay excluded
`IsRegistryEligibleBrowser` (line 35-46) gates ALL tab registration to normal, non-incognito browsers. URL update dispatch must respect the same gate; do not add URL updates for tabs whose creation was already filtered.

### C5 — No new FFI symbols
`maho_core_handle_event` already accepts the JSON-tagged enum. Reuse it; do not add `maho_core_update_tab_url` or similar. Cost: zero ABI surface.

### C6 — Same-document navigation must NOT dispatch
SPAs that update via `history.pushState` fire `DidFinishNavigation` with `IsSameDocument() == true`. The existing history-entry path skips these (line 130). Mirror that skip for `tab_url_updated` to prevent flooding.

### C7 — Title and URL dispatch are independent
`NavObserver::TitleWasSet` (line 134) and `DidFinishNavigation` (line 124) are two distinct hooks. URL changes only at navigation commit; title can change mid-navigation (SPA tab title updates). Add `tab_title_updated` dispatch to `TitleWasSet` AND to `MaybeDispatch` (for navigations where title is set on commit). De-duplicate via `last_url_`/`last_title_` cache, same as the current history entry path.

### C8 — No regressions to `OverlayActiveTabFields` until URL plumbing lands
Remove `OverlayActiveTabFields`'s URL overwrite ONLY in the cleanup phase, AFTER the new URL plumbing is verified working in production builds. Title/loading/audible overwrite stays in any case (those genuinely need WebContents-side live state).

### C9 — `IsSidebarIneligibleUrl` removal happens in the LAST phase
Removing the leaf filter is the final commit. Until URL plumbing is verified, the filter stays as belt-and-suspenders. Premature removal exposes the symptom to users in any rollback window.

---

## 3 — Decisions (Converged)

1. **Wire `tab_url_updated` dispatch in `NavObserver::DidFinishNavigation`** — single C++ change, reuses existing FFI surface.

2. **Wire `tab_title_updated` dispatch in `NavObserver::TitleWasSet` + `DidFinishNavigation`** — for symmetry; we will need title freshness for the same reason.

3. **Cache `last_dispatched_url` / `last_dispatched_title` in `MahoTabRegistry` keyed by stable TabId, with explicit migration on `kReplaced`** — adopts upstream Chromium's recommended pattern (`TabInterface::RegisterWillDiscardContents`). Maho's `OnTabStripModelChanged` already has a `kReplaced` branch (line 210-235) that carries the UUID over via `MahoTabIdHelper`; we extend that branch to migrate the cache entry. Survives tab discard / prerender swap without redundant dispatch. ~10-15 LOC of additional state. **Rejected**: per-`NavObserver` cache (Option A) — would fire one redundant `tab_url_updated` per swap. Idempotent on the Rust side, but upstream guidance and explicit cleanliness favor B'. **Rejected**: per-NavObserver promotion to a tabid map without explicit swap migration — equivalent to A in practice because the cache entry would be orphaned at swap time and a fresh entry would be created.

4. **Consolidate URL classification into `MahoDisplayPolicy`** — move the contents of `IsSidebarIneligibleUrl` (with the same URL list) into a new `MahoDisplayPolicy::IsSystemSurfaceUrl()` declared in `state_models.h`. Update existing callsites:
   - `state_adapter.cc:323` (`ParseTreeNode`) → REMOVE (Phase 4 cleanup)
   - `state_adapter.cc:815` (`build_tree_from_tab_strip` fallback) → KEEP, call new `MahoDisplayPolicy::IsSystemSurfaceUrl`. The fallback path operates from raw TabStripModel without core, so leaf filtering is appropriate THERE.
   - Free function `maho::IsSidebarIneligibleUrl` → DELETE from `state_adapter.h:79`

5. **`OverlayActiveTabFields` keeps title/loading/audio/muted overwrite, drops URL overwrite** — Phase 4 cleanup. URL becomes authoritative from core.

6. **No core-side filtering of URLs at `CreateTab` ingestion** — defer to a future ADR if needed. Reasons:
   - NTP transition case (`chrome://newtab` → `https://example.com`) requires the tab to be registered FROM the start so we have a stable UUID across the transition
   - Filtering at core means resurrection logic for "this tab navigated from system to user" — strictly more complex than just keeping URLs fresh
   - The fundamental bug is staleness, not registration

7. **Test pyramid (3 layers)**:
   - 1 × C++ unit test on `NavObserver` dispatch (mock `MahoCore`, verify event JSON)
   - 1 × Rust unit test on `update_tab_url` already exists; add one that asserts `Tab.url` is queryable after the event
   - 1 × C++ interactive test in `maho_sidebar_edge_cases_interactive_test.cc` for the regression case: open NTP → navigate to https://example.com → switch tabs → assert original tab still in sidebar with example.com URL

8. **Single PR / single squashed commit** — phases land atomically. Bisect-friendly. Rollback = `git revert`. The leaf filter removal can be a separate follow-up commit if the team wants observability time, but the main pipeline change is one commit.

9. **No feature flag** — this is pure plumbing; no user-facing behavior change. The visible behavior is "stale URLs no longer flicker tabs out of the sidebar", which is strictly an improvement. If a feature flag is desired for safety, add `kMahoTabUrlUpdatePipeline` defaulted to `true`; gate the new dispatch site only.

---

## 4 — Phase 0: Pre-flight verification (no edits)

Before writing any code, confirm three claims that the plan rests on:

### 0.1 — `MahoTabIdHelper` is attached to swapped WebContents on `kReplaced`

```bash
rg "MahoTabIdHelper::CreateForWebContents|FromWebContents" maho-chromium/browser/maho_tab_registry.cc
```

Read lines 210-235 of `maho_tab_registry.cc`. Confirm that after the swap, `MahoTabIdHelper::FromWebContents(new_contents)` returns a non-null helper with the carried-over `stable_tab_id`. If NOT, add helper attachment in the swap path BEFORE Phase 1.

### 0.2 — `TabUrlUpdated` handler does the right thing

```bash
rg "update_tab_url" maho/crates/maho-core/src/tab_lifecycle.rs
```

Read the function body. Confirm it does only:
- Find tab by id
- Set `tab.url = new_url`
- Emit `CoreUpdate::TabUpdated`

Confirm there are no side effects that depend on URL being stable (e.g., URL-keyed cache invalidation that would re-trigger sync). If side effects exist, plan a follow-up.

### 0.3 — Confirm sidebar tree refresh path observes `CoreUpdate::TabUpdated`

```bash
rg "CoreUpdate::TabUpdated" maho-chromium/
```

Confirm at least one C++ observer exists that triggers a sidebar repaint when `TabUpdated` arrives. If not, the sidebar will not refresh after `tab_url_updated` until the next ambient repaint (e.g. tab switch). This is acceptable but should be noted.

---

## 5 — Phase 1: C++ dispatch (the core change)

### 5.0 — Cache lives in `MahoTabRegistry`, NOT in `NavObserver`

**Why**: upstream Chromium's `TabInterface::RegisterWillDiscardContents` design pattern says callers should not cache state per-`WebContents` because swaps invalidate that state. Cache should be keyed by a stable identity and migrated explicitly on swap. We already have stable `MahoTabIdHelper::stable_tab_id()` and an existing `kReplaced` handler — Option B' is the natural fit.

**File:** `maho-chromium/browser/maho_tab_registry.h`

Add a small POD struct and a member map to the registry:

```cpp
namespace maho {

struct LastDispatchedTabState {
  std::string url;
  std::string title;
};

class MahoTabRegistry {
 public:
  // ... existing API ...

  // Returns a mutable reference to the cached last-dispatched state for this
  // tab. Inserts an empty entry if none exists. Used by NavObserver to
  // de-duplicate consecutive identical updates.
  LastDispatchedTabState& GetOrCreateDispatchCache(const std::string& tab_id);

  // Removes the cached entry for this tab. Called on tab close.
  void EraseDispatchCache(const std::string& tab_id);

  // Migrates a cached entry from one TabId to another. Called on
  // TabStripModelChange::kReplaced. In Maho's swap path the new contents
  // gets the OLD UUID carried over, so this is effectively a no-op on the
  // map but exists as a forward-compatibility seam if the swap semantics
  // change to issue a new UUID.
  void MigrateDispatchCache(const std::string& from_tab_id,
                             const std::string& to_tab_id);

 private:
  // ... existing members ...
  std::unordered_map<std::string, LastDispatchedTabState> dispatch_cache_;
};

}  // namespace maho
```

### 5.1 — Add URL/title dispatch to `NavObserver` using the registry cache

**File:** `maho-chromium/browser/maho_tab_registry.cc`

**Changes:**

```cpp
// Add at top of anonymous namespace (around line 100):

void DispatchTabUrlUpdated(content::WebContents* contents,
                           const std::string& url_spec) {
  if (!contents) return;
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) return;
  const std::string& tab_id = helper->stable_tab_id();
  if (tab_id.empty()) return;
  DispatchShellEventLocal("tab_url_updated", {
      {"tab_id", tab_id},
      {"url", url_spec},
  });
}

void DispatchTabTitleUpdated(content::WebContents* contents,
                             const std::string& title) {
  if (!contents) return;
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) return;
  const std::string& tab_id = helper->stable_tab_id();
  if (tab_id.empty()) return;
  DispatchShellEventLocal("tab_title_updated", {
      {"tab_id", tab_id},
      {"title", title},
  });
}
```

**Modify `NavObserver`** (constructor takes registry pointer; cache lives in registry):

```cpp
class MahoTabRegistry::NavObserver : public content::WebContentsObserver {
 public:
  NavObserver(content::WebContents* contents, MahoTabRegistry* registry)
      : content::WebContentsObserver(contents), registry_(registry) {
    DCHECK(registry_);
  }

  ~NavObserver() override = default;

  void DidFinishNavigation(content::NavigationHandle* nav) override {
    if (!nav) return;
    if (!nav->IsInPrimaryMainFrame()) return;
    if (!nav->HasCommitted()) return;
    if (nav->IsErrorPage()) return;
    if (nav->IsSameDocument()) return;       // C6 — skip SPA in-page nav
    MaybeDispatchHistory();                  // existing rename for clarity
    MaybeDispatchTabUrl();                   // new
    MaybeDispatchTabTitle();                 // new
  }

  void TitleWasSet(content::NavigationEntry* /*entry*/) override {
    MaybeDispatchHistory();                  // existing
    MaybeDispatchTabTitle();                 // new
    // URL doesn't change on title-only events; do NOT call MaybeDispatchTabUrl.
  }

 private:
  void MaybeDispatchHistory() {
    // ... existing logic, renamed from MaybeDispatch; unchanged ...
  }

  // Look up tab_id once per call. Cache lives in the registry, keyed by
  // stable tab_id, so it survives WebContents swap (kReplaced) — no
  // redundant dispatch after tab discard recovery / prerender swap-in.
  void MaybeDispatchTabUrl() {
    auto* contents = web_contents();
    if (!contents) return;
    const GURL& url = contents->GetLastCommittedURL();
    if (!url.is_valid()) return;
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper) return;
    const std::string& tab_id = helper->stable_tab_id();
    if (tab_id.empty()) return;
    auto& cache = registry_->GetOrCreateDispatchCache(tab_id);
    const std::string url_spec = url.spec();
    if (url_spec == cache.url) return;       // dedup against persistent cache
    cache.url = url_spec;
    DispatchTabUrlUpdated(contents, url_spec);
  }

  void MaybeDispatchTabTitle() {
    auto* contents = web_contents();
    if (!contents) return;
    auto* helper = MahoTabIdHelper::FromWebContents(contents);
    if (!helper) return;
    const std::string& tab_id = helper->stable_tab_id();
    if (tab_id.empty()) return;
    auto& cache = registry_->GetOrCreateDispatchCache(tab_id);
    const std::string title = base::UTF16ToUTF8(contents->GetTitle());
    if (title == cache.title) return;
    cache.title = title;
    DispatchTabTitleUpdated(contents, title);
  }

  // History entry de-dupe (existing, unrelated semantics — KEEP separate)
  std::string last_url_;
  std::string last_title_;

  raw_ptr<MahoTabRegistry> registry_;
};
```

**Why two separate caches:** the history-entry path (`last_url_`/`last_title_`) filters by `SchemeIsHTTPOrHTTPS()` (line 146) — only HTTP(S) URLs become history entries. The tab-record path (registry cache) tracks ALL committed URLs including `chrome://settings`, `chrome://newtab`, etc., so core stays accurate. Different scopes, different filters; collapsing them risks correctness regressions.

### 5.2 — Wire migration in `kReplaced` and cleanup in `kRemoved`

**File:** `maho_tab_registry.cc` (modifications to `StripObserver::OnTabStripModelChanged`)

In Maho's existing swap flow (line 210-235), the OLD UUID is carried over to the new `WebContents` via `MahoTabIdHelper`. The dispatch cache is keyed by that same UUID, so the entry is automatically reachable from the new observer — but to be explicit (and to guard against future swap-semantic changes), call `MigrateDispatchCache(old_id, new_id)` even when `old_id == new_id` (no-op in current behavior, future-proof if not).

In the `kRemoved` branch (line 198-209), call `EraseDispatchCache(tab_id)` whenever `TabRemoveReasonUtils::WillDeleteTab` is true. This prevents unbounded growth of the map.

```cpp
} else if (change.type() == TabStripModelChange::kRemoved) {
  const auto* remove = change.GetRemove();
  if (!remove) return;
  for (const auto& removed : remove->contents) {
    nav_observers_.erase(removed.contents);
    if (!TabRemoveReasonUtils::WillDeleteTab(removed.remove_reason)) {
      continue;
    }
    // Erase dispatch cache before the tab is gone for good.
    if (auto* helper = MahoTabIdHelper::FromWebContents(removed.contents);
        helper && !helper->stable_tab_id().empty()) {
      registry_->EraseDispatchCache(helper->stable_tab_id());
    }
    DispatchCloseTabForContents(removed.contents);
  }
} else if (change.type() == TabStripModelChange::kReplaced) {
  const auto* replace = change.GetReplace();
  if (!replace) return;
  // ... existing UUID carry-over code ...
  const std::string old_id = /* from old contents helper */;
  const std::string new_id = /* from new contents helper, after carry-over */;
  registry_->MigrateDispatchCache(old_id, new_id);
  // ... existing NavObserver re-attach with new contents ...
}
```

(Exact snippet to be finalized during execution after reading the full `kReplaced` branch in context. The principle: cache lifecycle aligns with tab lifecycle, with explicit migration at the only swap point.)

### 5.3 — `Bootstrap()` initial dispatch

In `StripObserver::Bootstrap()` (line 238-244), after `DispatchCreateTabForContents`, the tab is registered with whatever URL was visible at attach time. The dispatch cache for that tab is empty, so the first user navigation triggers normal dispatch. No bootstrap-time `tab_url_updated` is needed.

---

## 6 — Phase 2: Tests

### 6.1 — Rust unit test (cheap)

**File:** `maho/crates/maho-core/src/tab_lifecycle.rs` (existing test module)

Add test asserting:
1. `create_tab_inner(id, space, Some(url_a), ...)` sets `Tab.url == url_a`
2. `update_tab_url(&id, url_b)` mutates `Tab.url == url_b`
3. The mutation produces a `CoreUpdate::TabUpdated { changes: { url: Some(url_b), ... } }`

Mirror existing test patterns in the file. ~20 LOC.

### 6.2 — C++ unit test for NavObserver dispatch

**File:** New file `maho-chromium/browser/maho_tab_registry_unittest.cc`

Test cases:
- `DidFinishNavigation` with `IsSameDocument=true` does NOT dispatch
- `DidFinishNavigation` with valid URL dispatches `tab_url_updated` with correct fields
- Same URL twice in a row dispatches only once (last_dispatched_url_ cache)
- `TitleWasSet` with new title dispatches `tab_title_updated`
- `TitleWasSet` after URL change with both updated dispatches both events

Use existing test scaffolding patterns from `maho-chromium/browser/ui/views/sidebar/maho_sidebar_*_unittest.cc`. Mock the FFI bridge by intercepting `DispatchShellEventLocal`. Add to existing `BUILD.gn` source_set if there's an existing unittest target; otherwise add a new target.

### 6.3 — Regression test (unit-level, per user decision)

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter_unittest.cc` (or the closest existing unit test target — verify in Phase 0)

Build a `ParseTreeNode` unit test fixture that mocks the JSON tree input. Test cases:

```
TEST: NtpUrlSurvivesAfterCoreUrlIsUpdated
  // Simulate the sequence:
  //  1. core has a tab with url="chrome://newtab"  (tree JSON in)
  //  2. ParseTreeNode is called WITHOUT IsSidebarIneligibleUrl filter (post-Phase 4)
  //  3. Result: node IS in tree (not stripped)
  //  4. core has same tab now with url="https://example.com" (tree JSON in)
  //  5. ParseTreeNode produces node with url="https://example.com"

TEST: GhostTabStillStripped
  // Url="" + Title="" → IsGhostTab strips it (this filter remains)

TEST: SystemSurfaceUrlInFallbackPath
  // build_tree_from_tab_strip with chrome://newtab → filtered if not active
  // build_tree_from_tab_strip with chrome://newtab + active → kept (post-Phase 4
  // restoration of the && !is_active guard in the fallback path only)
```

**Why unit-level (changed from interactive in v1.1)**:
- Faster feedback — no Chromium process startup overhead per run
- Sufficient: the bug is at `ParseTreeNode`, which is unit-testable in isolation
- The pipeline integration is implicitly verified by the existing C++ unit test in §6.2 (which covers the dispatch side) plus the Rust test in §6.1 (which covers the `update_tab_url` mutator). Together they cover the integration.
- Trade-off accepted: a hypothetical regression in TabStripModel↔NavObserver wiring would not be caught by this unit test. Acceptable for v1.1; an interactive smoke test can be added in a follow-up if regressions appear in the wild.

---

## 7 — Phase 3: Consolidate URL classification

**Only after Phase 1 + Phase 2 are green.**

### 7.1 — Move classification to `MahoDisplayPolicy`

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`

Add to `MahoDisplayPolicy` namespace:

```cpp
// Returns true for browser-internal "system surface" URLs that historically
// did not appear in the sidebar tab list (NTP, downloads, history, about:*).
// Used by the tab-strip fallback path; the core-driven path no longer
// filters here because tab URLs are now plumbed through tab_url_updated.
inline bool IsSystemSurfaceUrl(const GURL& url) {
  if (!url.is_valid() || url.is_empty()) return true;
  if (url.SchemeIs("about")) return true;
  if (url.SchemeIs("chrome")) {
    return url.host() == "newtab" ||
           url.host() == "downloads" ||
           url.host() == "history";
  }
  return false;
}
```

### 7.2 — Update fallback callsite

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

Change line 815 (the `build_tree_from_tab_strip` fallback) from `IsSidebarIneligibleUrl(tab_url)` to `MahoDisplayPolicy::IsSystemSurfaceUrl(tab_url)`. Update the include accordingly.

### 7.3 — Migrate existing unit test

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc:905-913`

Rename `IsSidebarIneligibleUrlTest.KnownSpecialPagesAreExcluded` → `IsSystemSurfaceUrlTest.KnownSpecialPagesAreExcluded`. Update calls from `maho::IsSidebarIneligibleUrl(...)` to `MahoDisplayPolicy::IsSystemSurfaceUrl(...)`.

---

## 8 — Phase 4: Cleanup

**Only after Phase 1 + 2 + 3 are green and the regression test from 6.3 has been observed passing in the build for at least one merge window.**

### 8.1 — Delete leaf filter callsite (the user's flagged anti-pattern)

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

Remove lines 322-326:
```cpp
//if (raw_url) {
//  if (IsSidebarIneligibleUrl(GURL(*raw_url))) {
//    return std::nullopt;
//  }
//}
```

The `ParseTreeNode` path now trusts core's tab list. `IsGhostTab` filter at line 332 stays.

### 8.2 — Delete free function

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc:37-51` and `.h:79`

Delete `bool IsSidebarIneligibleUrl(const GURL& url);` and its definition. The function is now dead.

### 8.3 — Drop URL overwrite in `OverlayActiveTabFields`

**File:** `state_adapter.cc:511-513`

Change:
```cpp
if (active_contents) {
  const GURL& live_url = GetVisibleOrCommittedUrl(active_contents);
  node.url = live_url.is_valid() ? live_url.spec() : std::string();
  // ... keep title/loading/audible/muted overwrites
}
```
to:
```cpp
if (active_contents) {
  // URL no longer needs live overwrite — tab_url_updated keeps core fresh.
  // Title/loading/audible/muted still need live state because they aren't
  // dispatched on every micro-change.
  node.is_loading = active_contents->IsLoading();
  node.is_audible = active_contents->IsCurrentlyAudible();
  node.is_muted = active_contents->IsAudioMuted();
  node.title = GetTabTitle(model, active_index);
  node.host = GetTabHost(model, active_index);
  if (auto* helper = MahoTabIdHelper::FromWebContents(active_contents);
      helper && !helper->stable_tab_id().empty()) {
    node.tab_id = helper->stable_tab_id();
  }
}
```

(Keep title overwrite for now — Phase 1 dispatches title on `TitleWasSet` but micro-changes during page load may not all flow through. Conservative: keep title overwrite; revisit in a future plan.)

### 8.4 — Resurrect the `&& !is_active` semantics for the fallback path only

**File:** `state_adapter.cc:815`

The fallback path (`build_tree_from_tab_strip`) is hit when there's no core. In that mode, we want the active tab to remain visible even if it's a system surface (otherwise the user gets a dead sidebar). Restore:
```cpp
if (MahoDisplayPolicy::IsSystemSurfaceUrl(tab_url) && !is_active) {
  continue;
}
```

This matches the original guard's intent and only applies in degraded mode.

---

## 9 — File Inventory

| File | Phase | Change |
|---|---|---|
| `maho-chromium/browser/maho_tab_registry.h` | 1 | Add `LastDispatchedTabState` POD + `dispatch_cache_` map + `GetOrCreateDispatchCache` / `EraseDispatchCache` / `MigrateDispatchCache` API |
| `maho-chromium/browser/maho_tab_registry.cc` | 1 | Add `DispatchTabUrlUpdated`, `DispatchTabTitleUpdated`; expand `NavObserver` with new dispatch hooks reading the registry cache; wire `EraseDispatchCache` in `kRemoved`; wire `MigrateDispatchCache` in `kReplaced`; pass registry pointer to `NavObserver` constructor |
| `maho-chromium/browser/maho_tab_registry_unittest.cc` | 2 | New file — NavObserver dispatch tests + cache migration tests |
| `maho-chromium/browser/BUILD.gn` | 2 | Add unittest target if not present (read existing BUILD.gn first; do NOT modify if a target exists) |
| `maho/crates/maho-core/src/tab_lifecycle.rs` | 2 | Add unit test for `update_tab_url` round-trip |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter_unittest.cc` (or equivalent) | 2 | Add `ParseTreeNode` unit test for NTP→URL→inactive regression case (per user decision: unit-level, not interactive) |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc` | 3 | Rename `IsSidebarIneligibleUrlTest` → `IsSystemSurfaceUrlTest`; update calls |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h` | 3 | Add `MahoDisplayPolicy::IsSystemSurfaceUrl` |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | 3 + 4 | Phase 3: switch fallback callsite to new function. Phase 4: delete leaf filter, delete free function, simplify `OverlayActiveTabFields`, restore `&& !is_active` guard in fallback only |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.h` | 4 | Delete `IsSidebarIneligibleUrl` declaration (line 79) |

**Untouched (intentionally):**
- `maho/crates/maho-types/src/events/shell_event.rs` — `TabUrlUpdated`, `TabTitleUpdated` already defined
- `maho/crates/maho-core/src/event_dispatcher.rs` — handlers already wired
- `maho/crates/maho-core/src/tab_lifecycle.rs` — `update_tab_url` / `update_tab_title` already implemented
- `maho/crates/maho-core/src/space_manager.rs` — `get_sidebar_tree` is correct as-is
- The two working-tree changes (`IsSidebarIneligibleUrl` guard removal and `ScrollView` rewire) — those are separate concerns; this plan supersedes the guard-removal need

---

## 10 — Risks & Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| **R1**: `tab_url_updated` flood from popular sites that update history many times per page load | Medium | Registry-keyed cache de-duplicates by URL. SPAs are skipped via `IsSameDocument()`. If still chatty, add a 200ms debounce in Phase 5 (out of scope). |
| **R2**: `MahoTabIdHelper` not attached at navigation time on swap | Medium | Phase 0.1 verification. If unverified, dispatch will silently no-op, falling back to today's behavior. Not a regression. |
| **R3**: Sidebar doesn't repaint on `CoreUpdate::TabUpdated` | Low | Phase 0.3 verification. If true, document and add follow-up; until then, ambient tab-switch repaint covers most cases. |
| **R4**: New URL spec contains characters that break JSON serialization | Low | `base::JSONWriter::Write` handles UTF-8; same path as today's `create_tab` event. |
| **R5**: Title dispatch causes performance regression during fast page loads | Low | Registry cache de-duplicates. Same path as the existing history dedupe. |
| **R6**: Removing leaf filter (Phase 4) before Phase 1 deploys to user builds | High | Phase 4 explicitly gated on Phase 1+2+3 green AND regression test passing in build for one merge window. Leaf filter is belt-and-suspenders during transition. |
| **R7**: `IsSidebarIneligibleUrl` callers exist outside the two known sites | Medium | Search before Phase 4: `rg 'IsSidebarIneligibleUrl' maho-chromium/`. Phase 0 already caught two callsites; re-verify in Phase 4. |
| **R8**: Plan correctness depends on Phase 0 verification — if any of the three claims fail, the scope expands | Medium | Phase 0 is mandatory and dry-run only. Plan does not commit to Phase 1 until 0.1, 0.2, 0.3 are confirmed green. |
| **R9** *(new, B'-specific)*: Registry cache leaks if `kRemoved` isn't called for some tab termination path (e.g., browser shutdown without TabStripModel teardown) | Low | Map is bounded by tab count; leaks are cleaned up at process exit. Unit test verifies `EraseDispatchCache` is called on `WillDeleteTab`. |
| **R10** *(new, B'-specific)*: Cache-miss after swap if `kReplaced` doesn't carry UUID | Low | Phase 0.1 verification confirms UUID carry-over. If not carried, `MigrateDispatchCache(old_id, new_id)` handles the relabeling. If neither, fresh entry is created — equivalent to Option A behavior, no regression. |

---

## 11 — Rollback Strategy

| Failure mode | Rollback |
|---|---|
| Phase 1 breaks build | `git revert` the Phase 1 commit. Pipeline returns to current (pre-fix) behavior. Leaf filter still present. |
| Phase 1 lands but `tab_url_updated` isn't behaving | `git revert` the dispatch additions in `NavObserver`; leaf filter resumes its workaround role. |
| Phase 4 cleanup causes unexpected tab leakage (system URLs appearing) | Revert Phase 4 only; Phase 1+3 stay (harmless on their own). Plan a follow-up to add `IsSystemSurfaceUrl` filter at WebUI display layer if user-facing surface needs the filter. |
| Regression test from 6.3 fails after merge | Revert the entire merge; the regression case is the canary. |

The plan is **fully reversible** at each phase boundary because no schema migrations, no data formats change, and no FFI ABI changes.

---

## 12 — Out of Scope (explicit)

- Filtering URLs at C++ ingestion (`DispatchCreateTabForContents`) — increases complexity around NTP transitions.
- Filtering URLs at Rust core (`event_dispatcher::CreateTab`) — same reason.
- Removing `OverlayActiveTabFields` entirely — title/loading/audio still need live state.
- Adding `tab_loading_changed` C++ dispatch — separate plan; loading state's leaf-time freshness via `OverlayActiveTabFields` is fine for now.
- Refactoring three URL classification functions (`IsSidebarIneligibleUrl`, `IsNtpOrBlankUrl`, `IsInternalBrowserPage`) into one — they have legitimately different scopes (display vs. command-overlay vs. AI security). Document the distinction; don't merge.
- Migrating sidebar test fixtures to the new event flow — existing tests should still pass; only the new regression test in 6.3 is added.

---

## 13 — Resolved Decisions (from user input)

The three open questions from v1.0 have been resolved:

1. **Q1 — Cache location** → **Registry per-tab persistent map (Option B')** with explicit migration in `kReplaced`.
   **Rationale**: Upstream Chromium's `TabInterface::RegisterWillDiscardContents` recommends this pattern over per-WebContents caching. Maho already has `MahoTabIdHelper` providing stable IDs and a `kReplaced` handler that carries UUIDs over — extending it to migrate the cache entry is ~10-15 LOC. Zen/Arc investigation (`bg_c8d655f3`) showed Zen does NOT have a "last dispatched" dedup cache (uses Firefox's `permanentKey` discipline instead) and Arc is closed source / unverifiable. The plan adopts upstream Chromium's pattern, not Zen's, not Arc's.

2. **Q2 — Title dispatch scope** → **Include `tab_title_updated` in this plan** alongside `tab_url_updated`.
   **Rationale**: Same root cause, same fix shape, ~5 LOC additional. Splitting into two plans doubles review burden for negligible scope reduction.

3. **Q3 — Regression test type** → **Unit test on `ParseTreeNode`** (not interactive).
   **Rationale**: Faster feedback, sufficient coverage given the bug surface is unit-testable. Interactive coverage is implicitly provided by the C++ NavObserver dispatch test (§6.2) and the Rust mutator test (§6.1). Trade-off documented in §6.3.

4. **Phase strategy** → **Single PR covering all 4 phases** (per user direction).
   **Rationale**: User accepted the larger rollback unit in exchange for review-cycle compression. Phase ordering remains sequential (0 → 1 → 2 → 3 → 4) within the single PR for bisect-friendliness if it ever splits.

5. **Track A–E coexistence** → **Working tree stays as-is during option C work** (per user direction).
   **Rationale**: Code areas don't overlap; PR-level separation can be deferred. Working tree's existing dirty state (Track A–E + the IsSidebarIneligibleUrl guard removal + ScrollView changes) is left untouched by this plan.

---

## 14 — Acceptance Criteria

The plan is "done" when ALL of:

- [ ] Phase 0 verifications all green
- [ ] Phase 1 lands; `lsp_diagnostics` clean on `maho_tab_registry.cc` and `maho_tab_registry.h`
- [ ] Rust workspace `cargo check --workspace` clean
- [ ] Phase 2 tests all pass:
  - [ ] Rust unit test added and green
  - [ ] C++ unit test for NavObserver dispatch + cache migration added and green
  - [ ] `ParseTreeNode` unit test for NTP→URL→inactive regression case added and green
- [ ] Phase 3 consolidation lands; `IsSystemSurfaceUrl` is the single source of system-URL classification for the sidebar fallback path
- [ ] Phase 4 cleanup lands AND the regression test still passes
- [ ] No new GN files modified beyond what unittest target requires
- [ ] No `as any`, `@ts-ignore`, `unimplemented!()`, or empty catches introduced
- [ ] Registry dispatch cache is bounded — `EraseDispatchCache` is verified called on `WillDeleteTab`
- [ ] PR description references this plan file (v1.1)
- [ ] After merge, the user-flagged anti-pattern (`IsSidebarIneligibleUrl` leaf filter) no longer exists in the core path; only the fallback retains a system-URL filter at appropriate scope

---

## 15 — Summary

The user correctly identified `IsSidebarIneligibleUrl` as an anti-pattern. The architectural exploration confirms it is a workaround for a missing C++ dispatch hook — `tab_url_updated` is fully built on the Rust side but never sent from C++. The fix is one wiring change (Phase 1), with consolidation (Phase 3) and cleanup (Phase 4) following.

Cache strategy adopts upstream Chromium's `TabInterface::RegisterWillDiscardContents` pattern (Option B' — registry-keyed map with explicit `kReplaced` migration), informed by a comparative investigation of Zen Browser source and Arc Browser's available evidence. Zen does not implement an analogous dedup cache (uses Firefox's `permanentKey` discipline); Arc is closed source.

Total scope is ~150 LOC across 5 files plus tests. Plan ships as a single PR covering all 4 phases per user direction. Bisect-friendly within the PR (sequential phases). Fully reversible at the PR level. Resolves the root cause rather than patching symptoms.
