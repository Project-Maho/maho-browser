# Maho Tab ID Drift Fix — Plan v5

**Date:** 2026-06-11
**Status:** DRAFT — needs user/momus review
**Bug:** Favorite click opens new tab instead of activating existing tab
**Root Cause:** Rust core LMDB `Tab.id` ↔ Chromium `MahoTabIdHelper` UUID drift
**Fix Scope:** A (Lenient URL Match) + B (Discard Recovery) + C (Suspended Favorite WakeUp)

---

## 1. Goal

Eliminate the "favorite click → new tab" bug by closing all three gaps that cause the `tile_tab_ids_[index]` stored in the favorites grid to diverge from the live `MahoTabIdHelper::stable_tab_id()` in the tab strip:

- **A.** Lenient URL match self-healing (host+scheme+port) so redirect/www variations resolve to the existing live tab.
- **B.** WebContents discard recovery hardening so the already-existing `kReplaced` handler is bulletproof against edge cases.
- **C.** Suspended favorite WakeUp on click, so clicking a favorite whose `Tab.state == Suspended` in Rust core properly wakes the ghost via the existing `WakeSuspendedTab` pattern rather than falling through to "open new tab."

---

## 2. Architecture Background

### 2.1 Rust Core Tab Entity (Single-entity Design)

```
maho/crates/maho-types/src/tab.rs:28-54
```

`Tab` is a single struct with `is_favorite: bool`, `favorite_order: Option<u32>`, and `state: TabLifecycleState`. Favorites, pinned, and normal tabs are **not** separate entities — intentional.

**`TabLifecycleState` enum** (`tab.rs:8-13`):
```rust
enum TabLifecycleState {
    Active,
    Frozen,
    Suspended { snapshot: TabSnapshot },
    Archived { metadata_only: bool },
}
```

### 2.2 Chromium MahoTabIdHelper

```
maho-chromium/browser/maho_tab_id_helper.cc:22-31
```

Constructor logic:
```cpp
MahoTabIdHelper::MahoTabIdHelper(content::WebContents* web_contents)
    : WebContentsUserData<MahoTabIdHelper>(*web_contents) {
  std::string& pending = GetPendingRestoredTabId();
  if (!pending.empty()) {
    stable_tab_id_ = pending;  // pre-set by session restore or WakeSuspendedTab
    pending.clear();
  } else {
    stable_tab_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();  // DRIFT SOURCE
  }
}
```

### 2.3 Drift Origin

| Scenario | Trigger | Why drift happens |
|----------|---------|-------------------|
| Session restore race | Browser restart, multiple windows | `SetPendingRestoredTabId` is static global; concurrent restore overwrites |
| Tab discard (memory pressure) | OS memory warning → background tab discarded | If WC is fully destroyed (not just swapped), no `kReplaced` fires |
| Navigation-triggered WC swap | Cross-origin navigation | Some paths create new WC without `kReplaced` |
| Suspended ghost re-navigation | User closes favorite (suspends), then clicks tile | New WC gets random UUID; favorite's persisted tab_id points at old orphan UUID |

### 2.4 Current Click Handler Fallback Chain

```
maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc:1554-1631
```

| Step | Logic | Line |
|------|-------|------|
| 1 | `tab_id` → scan tab strip for matching `stable_tab_id()` → `ActivateTabAt` | 1565-1578 |
| 1b | URL **strict equality** (`==`) → activate + rebind tab_id | 1587-1611 |
| 2 | Fallback: open NEW foreground tab | 1614-1631 |

**Missing:** No lifecycle-state awareness, strict URL comparison.

### 2.5 Existing WakeSuspendedTab Pattern

```
maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:3354-3375
```

Already implemented for **tab list** clicks:
1. `QueryTabFromCore(tab_id)` — fetches persisted URL/title/isPinned from Rust core via FFI JSON.
2. `MahoTabIdHelper::SetPendingRestoredTabId(tab_id)` — pre-seeds identity.
3. `Navigate(&params)` with `NEW_FOREGROUND_TAB` — new WC inherits the tab_id.
4. Re-pins if needed.

This pattern is **not** used in the favorites grid click handler.

### 2.6 Existing Discard Handler

```
maho-chromium/browser/maho_tab_registry.cc:382-418
```

Handles `TabStripModelChange::kReplaced` by copying old UUID to new WebContents. Covers standard discard-then-reload flow.

### 2.7 Rust Core Event Dispatcher — Re-Announce

```
maho/crates/maho-core/src/event_dispatcher.rs:279-286
```

When a `CreateTab` shell event arrives with an explicit `tab_id` matching an existing tab, the dispatcher resets its lifecycle to `Active`:
```rust
if !matches!(existing.state, TabLifecycleState::Active) {
    tab_mgr.activate_tab(&explicit_id);
}
```

No additional Rust-side changes needed for the wake-up path.

---

## 3. Gap Analysis

| Gap | Description | Reproduction | Impact |
|-----|-------------|--------------|--------|
| **G1** | URL strict-match fails on `www.` prefix, trailing `/`, or scheme redirects | Favorite `https://example.com`, live tab `https://www.example.com/` | Falls to path 2 → new tab |
| **G2** | No suspended-favorite awareness in click handler | Favorite `Suspended` in Rust core (no live WC), user clicks tile | Both scans fail → new tab |
| **G3** | Discard edge: fully destroyed WC (no `kReplaced`) | OS discards, user clicks favorite directly without switching | tab_id scan fails, URL scan fails → new tab |

---

## 4. Architecture Change

### 4.A Lenient URL Match (Self-Healing)

**Location:** `maho_sidebar_favorites_grid_view.cc` path 1b (line 1596)

**Change:** Replace strict `==` with lenient host+scheme+port matcher:

```cpp
namespace {
bool UrlMatchesLeniently(const GURL& a, const GURL& b) {
  if (!a.is_valid() || !b.is_valid()) return false;
  if (a == b) return true;  // fast path
  if (a.scheme() != b.scheme()) return false;
  if (a.EffectiveIntPort() != b.EffectiveIntPort()) return false;

  std::string_view host_a = a.host_piece();
  std::string_view host_b = b.host_piece();
  if (host_a.starts_with("www.")) host_a.remove_prefix(4);
  if (host_b.starts_with("www.")) host_b.remove_prefix(4);

  return host_a == host_b;
}
}
```

**Integration:** Replace line 1596 `live_url == tile_urls_[index]` → `UrlMatchesLeniently(live_url, tile_urls_[index])`

**Cross-platform:** Pure C++ — macOS/Windows/Linux identical.

### 4.B WebContents Discard Recovery

Subsumed by Fix C. The existing `kReplaced` handler covers standard discard. When a WC is fully destroyed (favorites are exempt from `close_tab` dispatch per `maho_tab_registry.cc:370-376`), the tab still exists in Rust core → Fix C's wake-up path handles it.

**Minor hardening:** Add `DLOG(INFO)` trace in `kReplaced` handler for diagnostic observability.

### 4.C Suspended Favorite WakeUp on Click

**Location:** `maho_sidebar_favorites_grid_view.cc` — insert path 1c between lines 1611 and 1614.

```cpp
if (!tab_id.empty()) {
  auto persisted = QueryTabFromCore(tab_id);
  if (persisted) {
    MahoTabIdHelper::SetPendingRestoredTabId(tab_id);
    NavigateParams params(browser_, GURL(persisted->url),
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    if (persisted->is_pinned && params.navigated_or_inserted_contents) {
      TabStripModel* model = browser_->tab_strip_model();
      int new_idx = model->GetIndexOfWebContents(
          params.navigated_or_inserted_contents);
      if (new_idx != TabStripModel::kNoTab && !model->IsTabPinned(new_idx)) {
        model->SetTabPinned(new_idx, true);
      }
    }
    return;
  }
}
```

**`QueryTabFromCore` reuse:** Extract from `maho_sidebar_tab_list_view.cc:3295-3340` to shared `maho_sidebar_tab_core_query.{h,cc}`.

**Rust side:** No changes needed — `event_dispatcher.rs:284-286` already activates suspended tabs on re-announce.

---

## 5. File Diff Plan

| File | Change | Lines Affected |
|------|--------|----------------|
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc` | Add `UrlMatchesLeniently()`, replace strict `==`, insert path 1c wake-up | ~1587-1631 |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h` | (potentially) add includes | minor |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_core_query.h` | **NEW** — `PersistedTabInfo` struct + declaration | ~25 lines |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_core_query.cc` | **NEW** — implementation extracted from tab_list_view | ~50 lines |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | Replace inline definition with `#include` | ~3295-3340 |
| `maho-chromium/browser/ui/views/sidebar/BUILD.gn` | Add new source files | 2 lines |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_url_match_unittest.cc` | **NEW** — unit tests | ~180 lines |
| `maho-chromium/browser/maho_tab_registry.cc` | Add DLOG trace to kReplaced handler | ~393 |

---

## 6. Phasing (TDD-Ordered Atomic Commits)

### Phase 1: Extract `QueryTabFromCore` to Shared Utility
**Commit:** `refactor(sidebar): extract QueryTabFromCore to maho_sidebar_tab_core_query`

### Phase 2: Test-First — Lenient URL Matching (RED)
**Commit:** `test(favorites): add unit tests for UrlMatchesLeniently`

### Phase 3: Implement Lenient URL Matching (GREEN)
**Commit:** `fix(favorites): lenient URL match (scheme+host+port) for tile activation`

### Phase 4: Test-First — Suspended Wake-Up (RED)
**Commit:** `test(favorites): add tests for suspended favorite wake-up path`

### Phase 5: Implement Suspended Favorite WakeUp (GREEN)
**Commit:** `fix(favorites): wake suspended favorite on click instead of opening new tab`

### Phase 6: Discard Hardening
**Commit:** `fix(tab-registry): add DLOG trace to kReplaced discard handler`

---

## 7. Tests

### Unit Tests

| Test | Assertion |
|------|-----------|
| `UrlMatchesLeniently_ExactMatch` | `(a, a)` → true |
| `UrlMatchesLeniently_WwwPrefix` | `(www.x.com, x.com)` → true |
| `UrlMatchesLeniently_SchemeMismatch` | `(http://x.com, https://x.com)` → false |
| `UrlMatchesLeniently_PortMismatch` | `(:8080, :443)` → false |
| `UrlMatchesLeniently_DifferentHost` | `(a.com, b.com)` → false |
| `UrlMatchesLeniently_InvalidUrl` | `("", valid)` → false |
| `UrlMatchesLeniently_TrailingSlash` | same host, `/` vs empty → true |
| `UrlMatchesLeniently_PathDifference` | same host, different paths → true |
| `WakeUp_TabInCore` | tab_id in core → wake triggers |
| `WakeUp_TabNotInCore` | unknown tab_id → falls through |
| `WakeUp_EmptyTabId` | empty → skipped |

### Integration Scenarios

| Scenario | Steps | Expected |
|----------|-------|----------|
| Redirect drift | Fav `http://x.com`, live redirected to `https://www.x.com/` | Activates live tab |
| Memory discard | Fav a tab, open 50+ tabs, click fav | Wakes (no dup) |
| Cmd+W on fav | Fav a tab, Cmd+W, click fav | Wakes suspended |
| Cold start | Quit, relaunch, click fav before full restore | Activates or wakes |

---

## 8. Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Lenient false positive (two tabs same host) | Medium | Only triggers after tab_id match fails. First match wins. Strictly better than "new tab." |
| `QueryTabFromCore` JSON parse on each click | Low | Already used in tab list. Max 12 favorites. |
| `SetPendingRestoredTabId` race (static global) | Low | UI-thread only. Same pattern used in tab list. |
| `UrlMatchesLeniently` with `chrome://` URLs | Low | `chrome://` URLs have unique hosts. Guard: invalid/empty → false. |

---

## 9. Acceptance Criteria

- [ ] Clicking a favorite whose live tab was redirected (URL differs by www/scheme/port) activates the live tab.
- [ ] Clicking a favorite whose WC was discarded creates a WC with correct tab_id.
- [ ] Clicking a favorite whose Rust-core state is `Suspended` wakes it with original tab_id.
- [ ] All new unit tests pass.
- [ ] Full build (`autoninja -C out/Default chrome`) clean.
- [ ] No regression in existing `maho_sidebar_*_test.cc`.

---

## 10. Open Questions

1. **Path-aware matching?** Current design: host-only. Start conservative, refine if false positives emerge.
2. **Targeted FFI (`get_tab_by_id`)?** Defer — current JSON-parse approach is acceptable for N≤12.
3. **`lifecycle_state` in `FavoriteTileSpec`?** Defer — click-time query is simple and proven.
4. **Static `pending_restored_tab_id` race?** Pre-existing limitation. Future: per-WC injection.

---

## 11. Reproduction Scenarios

### Scenario A: Redirect Drift
1. User navigates to `http://github.com` (301 → `https://github.com`).
2. User favorites the tab. Core stores `url: "https://github.com"`, `tab_id: "abc-123"`.
3. Live tab navigates to `https://www.github.com/settings`.
4. If WC was replaced (cross-origin nav): path 1 fails (new UUID), path 1b fails (`github.com` ≠ `www.github.com/settings`) → **new tab**.
5. **Fix A:** `UrlMatchesLeniently` matches on host → activates.

### Scenario B: Memory Pressure Discard
1. 60 tabs open, favorite at index 45.
2. OS memory pressure → Chromium discards background tabs.
3. If WC destroyed entirely (favorites exempt from `close_tab` per registry:370-376): no WC in strip.
4. Path 1 fails, path 1b fails → **new tab**.
5. **Fix C:** `QueryTabFromCore(tab_id)` finds tab → wake-up.

### Scenario C: Cmd+W on Favorite
1. User favorites tab (`tab_id: "abc-123"`, `state: Active`).
2. Cmd+W → tab removed from strip, Rust core transitions to `Suspended`.
3. Favorites grid still shows tile (`get_favorite_tabs` returns non-Archived).
4. Click tile → path 1 fails, path 1b fails → **new tab**.
5. **Fix C:** `QueryTabFromCore("abc-123")` returns persisted info → `SetPendingRestoredTabId` → Navigate → identity preserved.

---

## Wave Execution Plan

```
Wave 1 (parallel):
├── Phase 1: Extract QueryTabFromCore (no deps)
├── Phase 2: Write lenient URL match tests (no deps)
└── Phase 6: Discard hardening DLOG (no deps)

Wave 2 (after Wave 1):
├── Phase 3: Implement lenient URL match (depends: Phase 2)
└── Phase 4: Write wake-up tests (depends: Phase 1)

Wave 3 (after Wave 2):
└── Phase 5: Implement suspended wake-up (depends: Phase 1, Phase 4)
```
