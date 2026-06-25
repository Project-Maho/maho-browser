# Sidebar Favorites Click — Activate Live Tab, Fallback to New Foreground Tab

**Created:** 2026-05-30
**Target:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`
**Tests:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_navigation_interactive_test.cc`
**Goal:** Clicking a favorite tile activates the existing live tab when one is open, instead of navigating the current tab. When no live tab matches, open the URL in a new foreground tab.

---

## Context / Problem

`MahoSidebarFavoritesGridView::OnTilePressed()` currently treats every favorite click as a URL load:

```cpp
// maho_sidebar_favorites_grid_view.cc:1209-1219
void MahoSidebarFavoritesGridView::OnTilePressed(size_t index) {
  if (!browser_ || index >= tile_urls_.size() || !tile_urls_[index].is_valid()) {
    return;
  }

  browser_->OpenURL(content::OpenURLParams(
                        tile_urls_[index], content::Referrer(),
                        WindowOpenDisposition::CURRENT_TAB,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK, false),
                    base::NullCallback());
}
```

Two problems:

1. **Navigation hijacks the current tab.** Even when the favorite already maps to a live tab in the strip, the click reloads the favorite URL into whatever the user is currently viewing. The user loses their place; the live tab is never activated.
2. **No tab-id resolution.** Favorites already store `tile_tab_ids_[index]` (set in `Update()` from `BuildTileSpecs` at line 627). That identity is ignored on click.

The sidebar tab list view already solved the same problem and exposes the authoritative pattern. `MahoSidebarTabListView::ResolveTabStripIndexForTabId()` (lines 2710-2729 of `maho_sidebar_tab_list_view.cc`) is the single source of truth: it walks the `TabStripModel` and matches by `MahoTabIdHelper::stable_tab_id()`. `ActivateTabById()` (lines 2680-2685) is the thin activate-on-resolve wrapper.

The favorites grid must adopt the same pattern, with a clean fallback when no live tab is found.

---

## Fallback Semantics Decision

When `tile_tab_ids_[index]` does not resolve to a live tab (closed since the favorite was created, never opened in this window, etc.), open in a **new foreground tab** (`WindowOpenDisposition::NEW_FOREGROUND_TAB`), not `CURRENT_TAB`.

Rationale:

- A favorite tile is a persistent shortcut, not an address bar entry. Users do not expect clicking a sidebar shortcut to discard their current tab's state.
- Matches Arc / Zen behavior: pinned-style shortcuts open new tabs when the live tab is gone.
- Symmetric with the "live tab" branch: in both cases the user lands on the favorite content; the only question is whether an existing tab is reused or a new one is created. Neither path mutates the current tab.

---

## Files To Change

| File | Change |
|------|--------|
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc` | Rewrite `OnTilePressed()` to resolve by tab id, then activate or fall back to new foreground tab. Add include for `MahoTabIdHelper`. |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_navigation_interactive_test.cc` | Update `FavoriteClickActivatesTab` and `FavoriteClickAfterDragCancel` to assert tab activation (no navigation of current tab). Add a new test for the fallback path. |

**No `BUILD.gn` changes.** `//maho/browser:maho_tab_id_helper` is already listed in the sidebar target's deps (`maho-chromium/browser/ui/views/sidebar/BUILD.gn:130`), so the new include resolves without touching GN.

---

## Production Change — `maho_sidebar_favorites_grid_view.cc`

### Step 1 — Add include

Add near the existing Maho includes:

```cpp
#include "maho/browser/maho_tab_id_helper.h"
```

(The tab list view includes it via the same path; mirror that.)

### Step 2 — Rewrite `OnTilePressed()`

Replace the current body (lines 1209-1219) with:

```cpp
void MahoSidebarFavoritesGridView::OnTilePressed(size_t index) {
  if (!browser_ || index >= tile_urls_.size()) {
    return;
  }

  // 1. Prefer activating an existing live tab when the favorite has a tab id
  //    that matches a live WebContents. Single authoritative path:
  //    MahoTabIdHelper::stable_tab_id(). Mirrors
  //    MahoSidebarTabListView::ResolveTabStripIndexForTabId().
  const std::string& tab_id = tile_tab_ids_[index];
  if (!tab_id.empty()) {
    TabStripModel* strip = browser_->tab_strip_model();
    if (strip) {
      for (int i = 0; i < strip->count(); ++i) {
        content::WebContents* contents = strip->GetWebContentsAt(i);
        if (!contents) {
          continue;
        }
        auto* helper = MahoTabIdHelper::FromWebContents(contents);
        if (helper && helper->stable_tab_id() == tab_id) {
          strip->ActivateTabAt(
              i, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kOther));
          return;
        }
      }
    }
  }

  // 2. Fallback: no live tab matches. Open the favorite URL in a NEW
  //    foreground tab so the user's current tab is preserved.
  if (!tile_urls_[index].is_valid()) {
    return;
  }
  browser_->OpenURL(content::OpenURLParams(
                        tile_urls_[index], content::Referrer(),
                        WindowOpenDisposition::NEW_FOREGROUND_TAB,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK, false),
                    base::NullCallback());
}
```

Notes:

- The early-return guard now allows an empty/invalid URL when a tab id is present (a live tab can be activated even if `tile_urls_[index]` is stale).
- The URL validity check is preserved on the fallback path so we do not call `OpenURL` with an invalid GURL.
- Use `TabStripModel::ActivateTabAt` directly (matches the reuse pattern already established in this codebase). If verification flags it as inconsistent with `MahoSidebarTabListView::ActivateTab`, switch to the same private helper used there. Do not introduce a new public surface.

### Shared-helper extraction — DEFERRED

Two call sites now contain the same id-to-strip-index walk: `ResolveTabStripIndexForTabId` in the tab list view, and the inline loop in `OnTilePressed`. Extracting a shared helper (e.g. on `MahoTabIdHelper` or a free function in a sidebar utility header) is **intentionally deferred**:

- Touching the tab list view risks regressions in the larger sidebar surface that this fix is not scoped to.
- The duplication is small (~10 lines) and read-only.
- The follow-up should land as its own change once both call sites are stable and tested.

Do not extract a helper as part of this plan.

---

## Test Changes — `maho_sidebar_navigation_interactive_test.cc`

The two existing tests assert the wrong contract — they pass today only because `CURRENT_TAB` happens to navigate to the favorite's URL. After the fix, the active tab's identity (not just its URL) must change.

### Update `FavoriteClickActivatesTab` (lines 316-344)

Replace the URL-substring check with an assertion that the active `WebContents` is the one bound to the favorite's `tab_id`.

```cpp
content::WebContents* active =
    browser()->tab_strip_model()->GetActiveWebContents();
ASSERT_TRUE(active);
auto* helper = MahoTabIdHelper::FromWebContents(active);
ASSERT_TRUE(helper);
EXPECT_EQ(helper->stable_tab_id(), first_fav.tab_id)
    << "Click on a live-mapped favorite must activate that tab, "
       "not navigate the current tab";
```

Add `#include "maho/browser/maho_tab_id_helper.h"` to the test if it is not already present.

### Update `FavoriteClickAfterDragCancel` (lines 346-387)

Same substitution as above for the post-click assertion: clicking after a cancelled drag must still activate the same live tab. Replace the URL-host substring check with the `stable_tab_id()` equality check.

### New test — fallback to new foreground tab

Add after `FavoriteClickAfterDragCancel`:

```cpp
IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       FavoriteClickWithNoLiveTabOpensNewForegroundTab) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());
  const auto& first_fav = state.favorites.items[0];
  ASSERT_FALSE(first_fav.tab_id.empty());

  // Close every live tab whose stable_tab_id matches the favorite.
  // After this, OnTilePressed must NOT find a live mapping and must
  // fall back to NEW_FOREGROUND_TAB.
  TabStripModel* strip = browser()->tab_strip_model();
  for (int i = strip->count() - 1; i >= 0; --i) {
    content::WebContents* wc = strip->GetWebContentsAt(i);
    auto* helper = wc ? MahoTabIdHelper::FromWebContents(wc) : nullptr;
    if (helper && helper->stable_tab_id() == first_fav.tab_id) {
      strip->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
    }
  }
  // Ensure at least one unrelated tab remains so we can detect "new tab".
  ASSERT_GT(strip->count(), 0);
  const int tab_count_before = strip->count();
  content::WebContents* active_before = strip->GetActiveWebContents();
  ASSERT_TRUE(active_before);

  base::RunLoop().RunUntilIdle();
  ResolveViews();

  views::View* tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(tile);
  const gfx::Point center = tile->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(strip->count(), tab_count_before + 1)
      << "Fallback path must open a new tab, not reuse current";
  EXPECT_NE(strip->GetActiveWebContents(), active_before)
      << "New tab must be foreground (active)";
  EXPECT_TRUE(strip->GetActiveWebContents()
                  ->GetVisibleURL()
                  .spec()
                  .find(first_fav.url.host()) != std::string::npos);
}
```

Notes:

- The exact mechanism for forcing "no live tab" depends on what `SeedProfile` produces. If favorites in the seeded profile are not backed by a live tab, the close loop is a no-op and the test still validates the fallback. If they are backed by a live tab, the close loop removes it and forces fallback. Either way the assertions hold.
- If `SeedProfile` always leaves the favorite's tab alive and closing it tears down state the test relies on, the test can be reframed: open the browser with a profile that omits the live tab (e.g. a fresh profile) and assert tab count grows by one. Pick whichever is least invasive given how the test fixture currently seeds favorites — do not add new fixture plumbing if the close-loop variant works.

---

## Non-goals / Scope Guardrails

- **Do not** extract a shared `ResolveTabStripIndexForTabId` helper. Deferred (see above).
- **Do not** modify `MahoSidebarTabListView` or any other sidebar subsystem.
- **Do not** change `BUILD.gn`. The dep is already wired.
- **Do not** change favorite tile rendering, icon, drag, context menu, or animation behavior.
- **Do not** touch `MahoFavoritesContextMenu` or the unfavorite/drag paths in this file.
- **Do not** refactor `OnTilePressed`'s sibling methods even if they share patterns.
- **Do not** change the disposition for the live-tab branch (it activates, no `OpenURL` call).
- **Do not** introduce a new Mojo event or dispatch for the click — favorites click stays a local view-layer action.
- **Do not** add a unit test if an interactive test already covers the path. Keep test additions minimal and in the existing test file.

---

## Verification

Per repo conventions in `AGENTS.md`:

1. `lsp_diagnostics` on `maho_sidebar_favorites_grid_view.cc` and `maho_sidebar_navigation_interactive_test.cc` — no new errors beyond pre-existing upstream noise.
2. `python3 maho-chromium/build/scripts/build_maho.py` — builds clean. Do not edit any `BUILD.gn` if the build fails; stop and report (per the GN guardrail in `AGENTS.md`).
3. Manual smoke (in built Chrome):
   - With a favorite whose tab is open in another sidebar slot: click → that tab becomes active, current tab unchanged.
   - With a favorite whose tab was closed: click → new foreground tab opens at the favorite's URL, prior tab still in strip.
   - Drag a tile, cancel, click: behaves identically to the no-drag click (regression check for `FavoriteClickAfterDragCancel`).
4. Run the updated interactive tests:
   - `FavoriteClickActivatesTab` — passes with the new `stable_tab_id()` assertion.
   - `FavoriteClickAfterDragCancel` — passes with the new assertion.
   - `FavoriteClickWithNoLiveTabOpensNewForegroundTab` — new test passes.

Stop after first successful run of the three tests plus a clean build. Do not re-verify.
