# Cross-Space Tab Identity Fix — Full Package Plan

**Created:** 2026-06-02
**Target:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
- `maho-chromium/browser/maho_space_profile_bridge.cc`
- `maho-chromium/browser/maho_tab_registry.cc`
- `maho/crates/maho-types/src/events/shell_event.rs`
- `maho/crates/maho-core/src/event_dispatcher.rs`
- `maho/crates/maho-core/src/maho_core.rs`
- `maho-chromium/browser/maho_tab_id_helper.{h,cc}` (read-only audit; possibly add a non-mutating accessor)

**Goal:** Eliminate the user-visible bug where one apparent tab is shared across multiple spaces in the same Chromium profile, and where closing it in any one space removes it from all of them. Kill four browser↔core reconciliation seams that allow identity bleed between spaces. After this plan, a tab created in space A is visible only in space A, and closing it leaves other spaces' tab lists untouched.

---

## Reported symptom

> 스페이스1에 favorite 탭이 1, 2가 있으면 똑같은 탭이 스페이스2, 3에 일반 탭으로 존재하고, 어느 스페이스 중 하나에서 탭을 닫으면 나머지에서도 사라진다.

This is **not** the previously fixed cross-space `tab_order` leakage in `add_tab_to_space`. The Rust core invariant is now clean. The remaining bug lives at the Chromium ↔ maho-core reconciliation layer, where the assumption "one Browser = one space" silently fails because the architecture intentionally keeps "all spaces of the same Chromium profile share one TabStripModel" (see `.sisyphus/plans/space-profile-gap-fill.md:508`).

---

## Current findings

Verified by direct read of each cited source line:

1. **`ParseTreeNode` mutates live `MahoTabIdHelper` UUIDs at render time.**
   `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc:349-353`
   ```cpp
   if (auto* helper = MahoTabIdHelper::FromWebContents(contents);
       helper && helper->stable_tab_id() != node.tab_id) {
     helper->SetRestoredTabId(node.tab_id);
   }
   ```
   When a core sidebar tree for space N references `node.tab_id`, the rendering code transplants that ID onto whatever live `WebContents` happens to be at the matched strip index. The next strip insertion / Bootstrap then re-announces that helper's now-foreign UUID into the current active space, materializing the same tab into multiple spaces. This is the primary "shared identity" creator.

2. **`SwitchToSpace(Browser*, space_id)` always taints the process-global active.**
   `maho-chromium/browser/maho_space_profile_bridge.cc:99-105`
   ```cpp
   if (browser) {
     browser_active_space_[browser] = space_id;
   }
   active_space_id_ = space_id;
   ```
   The browser-scoped overload still writes the global. Any other browser whose `browser_active_space_` is unseeded then resolves `GetActiveSpaceId()` via the global fallback — i.e. picks up "whatever space some other browser was last switched to." Bootstrap on that other browser stamps every tab with the wrong space.

3. **`DispatchCreateTabForContents` stamps `space_id` from the active bridge state at insert/Bootstrap time.**
   `maho-chromium/browser/maho_tab_registry.cc:74-104`
   ```cpp
   const std::string space_id =
       bridge ? bridge->GetActiveSpaceId(browser) : std::string();
   ...
   DispatchShellEventLocal("create_tab", {{"tab_id", tab_id}, {"space_id", space_id}, ...});
   ```
   At Bootstrap time, `GetActiveSpaceId(browser)` may resolve to the globally-tainted value (#2) and re-emit `create_tab` for tabs already known to core. The Rust handler treats a re-announce with a different space as a `move_tab`, silently relocating tabs the user never moved.

4. **`close_tab` carries no space context.**
   `maho-chromium/browser/maho_tab_registry.cc:106-119`
   ```cpp
   DispatchShellEventLocal("close_tab", {{"tab_id", tab_id}});
   ```
   ```rust
   ShellEvent::CloseTab { tab_id } => { ... }
   ```
   `tab_id` is global. If stale entries for the same `tab_id` exist in another space's `tab_order` (because of #1/#2/#3), one close emits a single `CoreUpdate::TabClosed` that propagates to every sidebar listener and removes the row from all spaces simultaneously.

These four seams together produce the exact symptom: shared appearance + symmetric disappearance.

---

## Target contract

After this plan lands, all of the following must hold:

1. **No render path may mutate `MahoTabIdHelper::stable_tab_id_`.** ID flow is one-directional: helper → core. Sidebar render is read-only with respect to live tab identity. The previously legitimate "session restore" / "tab discard kReplaced" mutation paths remain untouched (those are not on the render path).

2. **`SwitchToSpace(Browser*, space_id)` does not touch `active_space_id_`.** The global is updated only by the `Browser*`-less overload, which is the documented "no-browser-context fallback seed." Browser-scoped switches stay browser-scoped.

3. **`DispatchCreateTabForContents` is a no-op when the tab is already known to core.** Bootstrap and re-emission cannot relocate a known tab. New-tab creation is unchanged. This makes the Rust `CreateTab` handler's "treat re-announce as move" branch unreachable from the registry; that branch can stay as a defensive guard but must not be triggered by Bootstrap.

4. **`ShellEvent::CloseTab` carries an `expected_space_id: Option<SpaceId>`.** When present, the Rust handler asserts (debug) and refuses (release: log + ignore) the close if the tab's actual `tab.space_id` does not match. The C++ dispatcher always populates it for user-initiated closes from a known-space context. Strip-driven closes that originate from `kRemoved` keep using `None`, since by then `tab.space_id` is the only ground truth — `tab_id`-only close stays the closing path of last resort.

5. **All Rust workspace tests still pass (590+).**

6. **Chromium full build (`build_maho.py`) still succeeds.**

7. **No new `// nogncheck` or type-suppression comments are introduced.**

---

## Patch breakdown

The plan ships four patches, sequenced for incremental verifiability. Each one is independently buildable and revertable.

### P1 — Remove `SetRestoredTabId` mutation from `ParseTreeNode`

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`, lines 349–353.

**Action:** Delete the four-line `if`-block. Render path no longer rebrands live helpers.

**Why first:** Smallest diff, zero schema change, eliminates the most active identity-bleed source. Build and runtime risk are minimal because the deleted block exists only to "force" core's view of the tree into the live helper — but that direction is exactly the bug. Live helpers are the source of truth; if there is a mismatch, that's a signal that the strip-index match is wrong and we should not paper over it by overwriting the helper.

**Risk:** A node whose `node.tab_id` doesn't match the helper at the matched `tab_strip_index` will retain the helper's UUID. The downstream `is_active`/`title`/`host` overlay still works because it reads from `model->GetWebContentsAt(node.tab_strip_index)` directly, not via `node.tab_id`. The only loss is a "self-healing" effect that masked an underlying bug — losing it surfaces real bugs instead of silently corrupting identity.

**Verification:** Existing sidebar interactive tests must still pass. Specifically:
- `maho_sidebar_contract_test.cc` — sidebar build still produces correct active state.
- `maho_sidebar_dnd_*` interactive — drag/drop still resolves correctly.

---

### P2 — Browser-scoped `SwitchToSpace` does not poison the global

**File:** `maho-chromium/browser/maho_space_profile_bridge.cc`, lines 99–105.

**Before:**
```cpp
if (browser) {
  browser_active_space_[browser] = space_id;
}
active_space_id_ = space_id;
```

**After:**
```cpp
if (browser) {
  browser_active_space_[browser] = space_id;
} else {
  active_space_id_ = space_id;
}
```

**Why:** The header at `maho_space_profile_bridge.h:60-63` already documents the global as a "no-browser-context fallback." The current code violates that contract by writing it on every browser-scoped call. The fix re-aligns implementation with documented intent.

**Risk:** Surfaces that previously relied on `active_space_id_` being kept in sync after browser-scoped switches must continue to work. Audit all readers:
- `OnBrowserCreated` seeds `browser_active_space_[browser]` from `active_space_id_` if the new browser's profile matches. Under multi-window, this is the only path that can reach a stale global. It still works because the sequence is: (a) user switches space on Browser-1 (P2 keeps global untouched), (b) user opens Browser-2, (c) `OnBrowserCreated` reads global. The global retains its previous, valid value (last `Browser*`-less seed or last process startup). Browser-2 inherits a sane space, not Browser-1's switch.
- All other readers either (i) take a `Browser*` and use the per-browser map, or (ii) explicitly want the global fallback — the second category is intentional and correct.

**Verification:** Multi-browser-window manual test scenario:
1. Open Browser-1 in space A.
2. Open Browser-2 in space A. Verify `browser_active_space_[Browser-2] = A`.
3. Switch Browser-1 to space B.
4. Open Browser-3. Verify `browser_active_space_[Browser-3] = A` (the previous valid global), not B.
5. New tabs in Browser-3 must register in space A, not B.

---

### P3 — `DispatchCreateTabForContents` is a no-op for known tabs

**Files:**
- `maho-chromium/browser/maho_tab_registry.cc`, `DispatchCreateTabForContents()` lines 74–104.
- `maho-chromium/browser/maho_tab_id_helper.h` — add a const accessor `bool has_been_announced() const` and a setter `void mark_announced()` (boolean state on the helper).
- `maho-chromium/browser/maho_tab_id_helper.cc` — implement.

**Action:** Track per-helper "already announced to core" state on the C++ side. The first `DispatchCreateTabForContents` for a given `WebContents` flips the bit and dispatches; subsequent calls return early. The bit also flips during `kReplaced` migration so the new `WebContents` carries the same announce-state.

**Pseudocode:**
```cpp
void DispatchCreateTabForContents(BrowserWindowInterface* browser,
                                  content::WebContents* contents) {
  ...
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  if (!helper) return;
  if (helper->has_been_announced()) return;   // P3 guard
  ...
  DispatchShellEventLocal("create_tab", {...});
  helper->mark_announced();
}
```

**Why:** Bootstrap re-emits `create_tab` for every existing strip tab. Without this guard, every browser-creation event re-announces tabs to core under the current active space, defeating P2's protection at the next layer. With this guard, Bootstrap dispatches `create_tab` only for tabs the C++ side has not yet announced — i.e. genuinely new tabs.

**Edge cases:**
- **Session restore:** `chromium_src/.../browser_tabrestore.cc` patches `CreateRestoredTab` to call `SetRestoredTabId` on the helper. Restored tabs land with their persisted UUID and `has_been_announced = false`. The first Bootstrap announces them; subsequent Bootstraps no-op. This is correct.
- **`kReplaced` (tab discard):** The existing migration path at `maho_tab_registry.cc:281-316` carries the old UUID onto the new `WebContents`. We add: also carry `has_been_announced` from old to new helper. The discarded tab was already announced; the replacement inherits that state.
- **Tab moved across browsers (drag-out):** `kRemoved` with `WillDeleteTab=false` in the source browser means the WebContents survives. `kInserted` in the destination browser reads the existing helper, sees `has_been_announced=true`, and does not re-emit. A `move_tab` event must already be flowing through a different code path; if it is not, that is a separate bug outside this plan's scope and should be filed.

**Risk:** If a path exists where a `WebContents` ends up on a strip without ever having gone through `DispatchCreateTabForContents` (and the announce flag is `false`), Bootstrap will correctly announce it for the first time, which is fine. The risk is the reverse: a path that re-creates the helper from scratch (e.g. some prerender or speculative path that bypasses the `kReplaced` migration). Audit:
- `MahoTabIdHelper::CreateForWebContents` is called from `tab_helpers.cc` patch (`apply_chromium_src_overrides.py:127`). Once per WebContents construction. No bypass.
- `SetRestoredTabId` does not reset `has_been_announced`. Restoration intentionally preserves announce state because restored tabs were once announced before the previous shutdown — but the new core process has no record of them, so we want to re-announce. **Therefore: `SetRestoredTabId` MUST reset `has_been_announced = false`.** Add this to the setter.

**Verification:** Cargo tests still pass. Manual scenario:
1. Open browser, open three tabs in space A.
2. Switch to space B. Existing tabs must remain in space A (verified by reading `maho_core_get_sidebar_tree(A)` returns three tabs, `(B)` returns zero).
3. Open a fourth tab while in space B. It must register only in space B.
4. Restart the browser. Session restore reopens four tabs. The tab list per-space must be unchanged.

---

### P4 — `CloseTab` carries optional `expected_space_id`

**Files:**
- `maho/crates/maho-types/src/events/shell_event.rs` — extend `ShellEvent::CloseTab` with `expected_space_id: Option<SpaceId>`.
- `maho/crates/maho-core/src/event_dispatcher.rs` — `CloseTab` handler validates against `tab.space_id` when `expected_space_id` is `Some`. Mismatch logs and returns without closing.
- `maho-chromium/browser/maho_tab_registry.cc` — `DispatchCloseTabForContents` populates `expected_space_id` from `bridge->GetActiveSpaceId(browser)` when called from a strip-removal path that has the browser context (it does — the observer holds `browser_`). Add a `Browser*` parameter, plumb from the observer.
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:2950` — sidebar's direct `close_tab` dispatch must include the active space.
- `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc:186` — context menu close must include the active space.
- All Rust callers of `ShellEvent::CloseTab` (tests + maho_core internals at `maho_core.rs:2682`) — pass `None` to preserve existing behavior unless the call site has a meaningful expected space.

**Why:** Defense in depth. After P1+P2+P3 the data integrity is correct, but a shell could still emit a close for a `tab_id` that lives in a different space than the user currently sees. `expected_space_id` is the contract that says "I want to close the tab as it appears in my current space; if the data model disagrees, do not silently take the action."

**Schema migration:** `ShellEvent` is JSON-serialized. Adding an optional field is backward-compatible:
- Old clients that send `{"close_tab": {"tab_id": "..."}}` without the new field — `serde(default)` makes `expected_space_id = None`, behavior identical to today.
- New clients send `{"close_tab": {"tab_id": "...", "expected_space_id": "..."}}` — handler validates.
- Persisted events: none. `ShellEvent` is fire-and-forget.

**Risk:**
- **`maho_tab_registry.cc:264-279` `kRemoved` path:** The strip removal already happened in Chromium. By the time we dispatch `close_tab`, the tab is gone from the strip. We can still read `bridge->GetActiveSpaceId(browser_)` — the `StripObserver` holds the browser. **However:** at strip removal time, the active space may differ from the tab's home space (because, post-fix, P1+P2+P3 keep tabs in their original space across switches). If we pass the active space and the tab actually lives in a different space, we'd refuse legitimate closes. **Resolution:** the `kRemoved` path passes `expected_space_id = None`. The strip is the actual source of truth for "this tab is gone"; we just propagate the close. Only UI-surface closes (sidebar row close button, context menu) pass the active space.
- **Race between active space switch and close:** User clicks close on a sidebar row, then switches space mid-event. By the time the dispatch reaches Rust, active space changed. With P4, we'd refuse the close. **Resolution:** snapshot `expected_space_id` at the click handler, not at dispatch time. The sidebar row already knows which space it was rendered for; pass that. (Concretely: `MahoSidebarTabListView` already has `current_space_id_`; reuse it.)

**Verification:**
- Add a Rust unit test: dispatch `CloseTab { tab_id, expected_space_id: Some(other_space) }` against a tab in `home_space`. Assert no removal, error logged.
- Add a Rust unit test: same with `expected_space_id: None`. Assert removal happens (parity with today).
- Sidebar interactive: close a tab from a non-current space view (somehow, e.g., via test harness). Assert no removal.

---

## Sequencing & verification gates

The four patches land in this order. Each gate must pass before the next patch is applied.

| # | Patch | Build | Tests | Manual |
|---|-------|-------|-------|--------|
| 1 | P1 (drop ParseTreeNode mutation) | `build_maho.py` exit 0 | `bun run test` 590/590 | Sidebar still renders active tab correctly |
| 2 | P2 (SwitchToSpace global guard) | exit 0 | 590/590 | Multi-window scenario from §P2 verification |
| 3 | P3 (announce-once helper bit) | exit 0 | 590/590 | Multi-space scenario from §P3 verification |
| 4 | P4 (CloseTab schema + validate) | exit 0 | 590/590 + new unit tests pass | Race-test scenario from §P4 verification |

If any gate fails, halt and revert the failing patch. Do not stack failures.

---

## Out of scope

This plan does **not**:

- Change favorites' profile-wide aggregation. `MahoCore::get_favorite_tabs` continues to return all favorites for the profile, regardless of active space. That is an explicit product decision documented in `docs/arc-bookmark-analysis.md:48` ("cross-space pins"). The `FavoritesIsolatedToActiveSpaceAfterSpaceSwitch` test that asserts isolation is wrong against the implemented design; updating it is a separate bookkeeping task and not gated by this plan.
- Change the "all spaces of one profile share one TabStripModel" architecture. That is the documented model (`.sisyphus/plans/space-profile-gap-fill.md:508`). This plan keeps the architecture and fixes the bridge so it correctly projects per-space ownership onto a shared strip.
- Touch the `SetLastActiveTab` / `GetLastActiveTab` API that exists on `MahoSpaceProfileBridge` but has zero production callers. Wiring it up is a separate UX improvement (active-tab restoration on space switch).
- Modify `ParseTreeNode` ordering of overlay vs read steps beyond removing the `SetRestoredTabId` mutation. The remainder of `ParseTreeNode` (active-state overlay, title overlay) is read-only and stays.
- Change favorites grid rendering. The grid keeps its profile-wide source.

---

## Rollback

Each patch is small enough to revert as a single commit:

- **P1:** restore the four-line `if`-block in `ParseTreeNode`.
- **P2:** restore the unconditional `active_space_id_ = space_id` write.
- **P3:** revert the helper additions and remove the early-return guard.
- **P4:** revert the schema field, the handler check, and the dispatch site changes; the field is `Option<SpaceId>` so old clients are unaffected.

If a regression is reported after landing, the order of bisection is reverse: P4 → P3 → P2 → P1. P1 is the lowest-risk change and unlikely to be the culprit.

---

## Open questions (must resolve during implementation)

1. **`MahoTabIdHelper::has_been_announced` initial value on session-restore-created helper.** Restored tabs go through `SetRestoredTabId`. Per §P3, that setter must reset `has_been_announced = false`. Verify by reading `chromium_src/chrome/browser/ui/browser_tabrestore.cc` patch sequence.
2. **Where is `current_space_id_` stored on `MahoSidebarTabListView`?** P4 requires snapshotting at click time. If the field doesn't exist explicitly (only on the parent view), thread it down or read from `MahoSidebarStateAdapter::active_space_id`.
3. **iOS / Android / macOS shell parity.** The Rust schema change in P4 is shared. Each shell that dispatches `close_tab` must be updated to either pass `expected_space_id: None` (preserving today's behavior) or populate it. Audit:
   - `maho/macos-shell/Sources/.../close_tab` dispatch — pass `None`.
   - `maho/ios-shell/.../close_tab` — pass `None`.
   - `maho/crates/maho-jni/.../close_tab` — pass `None`.
   These are Tier-2 surfaces; only the Chromium overlay needs to populate the field for correctness.

---

## Definition of done

- All four patches landed.
- All gates passed at each step.
- No new test was deleted, ignored, or marked `#[ignore]`.
- The user-reported scenario manually verified:
  - Open space A, create three tabs.
  - Switch to space B. Sidebar tab list shows zero tabs from A.
  - Open three tabs in space B. Sidebar shows three.
  - Switch to space A. Shows the original three, none of B's.
  - Close one tab in space A. Switch to B. B's three tabs untouched.
  - Repeat with favorites: favorite a tab in A. Switch to B. Favorite grid still shows it (intentional, profile-wide). B's regular tab list does not.

When all of the above hold, the bug is closed.
