# Sidebar Active Favorite Highlight — Minimal Plumbing Plan

**Created:** 2026-06-01
**Target:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.{cc,h}`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h`
**Goal:** When a sidebar favorite tile points to the currently active tab, render that tile in the active visual state, matching the existing active treatment used by normal and pinned tab rows.

---

## Goal

Favorites that map to the active tab should look active in the sidebar. Today they never do, even when the user is sitting on the exact tab the favorite points to. This is a missing-state bug, not a redesign.

The fix is a thin data flow: enrich the favorite item model with an `is_active` bit on the UI thread, plumb it into the `FavoriteTileButton`, and let `UpdateAppearance()` paint the active treatment alongside its existing focus/hover/pressed handling.

This is intentionally a minimal plumbing plan. It is not a refactor that merges favorites into the tab tree. Favorites remain a separate surface owned by `MahoSidebarFavoritesGridView`; the tab tree remains owned by `MahoSidebarTabListView`. Only the missing `is_active` signal is added.

---

## Current findings

1. Normal and pinned tab rows already carry an `is_active` bit on `SidebarTreeNode`. `SidebarTabRowView::UpdateAppearance()` consumes it and renders the active background, foreground, and accent treatment that ships today.
2. Favorites are rendered by `MahoSidebarFavoritesGridView` and its tile children `FavoriteTileButton`. They are a separate surface from the tab tree, with their own item model `MahoSidebarFavoriteItemModel`.
3. `MahoSidebarFavoriteItemModel` carries `title`, `subtitle`, `url`, `tab_id`, and `is_favorite`. There is no `is_active` field. The grid never receives an active signal, so it cannot render one.
4. Favorites are populated by two builder paths:
   - `BuildFavoritesModelForSpaceIdJson()` runs on a background thread. It must not touch `Browser*` or `TabStripModel*`. It cannot resolve the active tab.
   - `BuildFavoritesModel(Browser*)` runs on the UI thread. It already has the `Browser*` and is the only place where the active tab id is reachable safely. This is the correct enrichment site.
5. `FavoriteTileButton::UpdateAppearance()` currently only handles focus, hover, and pressed states. It does not branch on any "active" notion because the data does not exist yet.
6. Each favorite already stores `tab_id`. Tab id, not URL, is the authoritative identity used elsewhere in the sidebar (e.g. `MahoSidebarTabListView::ResolveTabStripIndexForTabId` matches via `MahoTabIdHelper::stable_tab_id()`). Matching favorites by tab id is the same rule the tab list view uses.

The missing data path, in one sentence: there is no signal on the favorite item model that says "this favorite's tab id equals the active tab's stable id," and there is no consumer in `FavoriteTileButton::UpdateAppearance()` for such a signal.

---

## Target contract

After this change, the following must hold:

1. `MahoSidebarFavoriteItemModel` exposes a new boolean field `is_active`, defaulting to `false`.
2. `BuildFavoritesModel(Browser*)`, on the UI thread, sets `is_active = true` on the favorite whose `tab_id` equals the active `WebContents`'s `MahoTabIdHelper::stable_tab_id()`, and `false` on all others. At most one favorite is active at a time.
3. `BuildFavoritesModelForSpaceIdJson()` does not set `is_active`. It leaves the field at its `false` default. This builder must remain free of `Browser*` / `TabStripModel*` access.
4. `MahoSidebarFavoritesGridView::Update()` (or its existing per-tile dispatch) propagates `is_active` from the model into each `FavoriteTileButton`.
5. `FavoriteTileButton` exposes a `SetActive(bool)` setter (or equivalent) and stores the bit. `UpdateAppearance()` branches on `is_active_` with priority semantics consistent with the existing focus/hover/pressed logic, and renders the active treatment used elsewhere in the sidebar.
6. When the active tab changes (user activates a different tab, closes the active tab, switches windows or spaces), the favorites grid eventually reflects the new active state through the existing `Update()` path. No new observer plumbing is added in this plan; we rely on whatever already triggers `BuildFavoritesModel(Browser*)` + `Update()` cycles today.

Helper signal of the new contract: `FavoriteTileButton::UpdateAppearance()` no longer assumes "favorite tile is never active." The default-false path keeps the existing visual exactly.

---

## Explicit non-goals

1. No merging of favorites into the tab tree. Favorites remain a separate surface.
2. No URL-based matching. `tab_id` is the only matching rule. Favorites without a `tab_id`, or with a `tab_id` that does not resolve to a live tab, are not active.
3. No active-state plumbing on the background thread. `BuildFavoritesModelForSpaceIdJson()` is not modified beyond what is needed to keep `is_active` defaulted to `false`.
4. No new tab-strip observer, no new active-tab notification path, no new mojo surface, no new IPC. We piggyback on the existing `BuildFavoritesModel` + `Update` cycle.
5. No change to favorites click handling, drag, drop, context menu, animation, or icon rendering.
6. No change to `SidebarTabRowView`, `MahoSidebarTabListView`, the tab tree, or any sibling sidebar subsystem.
7. No change to `BUILD.gn` unless a header file is added that requires it. If the bit is added to an existing header, no GN edit is needed. Per the repo guardrail, do not edit `BUILD.gn` to "fix" build errors; stop and report.
8. No changes to `MahoSidebarFavoriteItemModel`'s wire format / Mojo struct unless the model is the same struct. If a Mojo IDL file is involved, treat that as out-of-scope and instead store `is_active` as a view-layer-only field that lives on the C++ model after deserialization. (See risks below.)
9. No new tests for the background builder. The active bit is a UI-thread-only concern.

---

## Files to change

| File | Change |
|------|--------|
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.h` | Add `bool is_active = false;` to the favorite item struct. |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` | In `BuildFavoritesModel(Browser*)`, after favorites are populated, resolve the active `WebContents`'s `stable_tab_id()` and set `is_active` on the matching favorite. Leave `BuildFavoritesModelForSpaceIdJson()` untouched beyond default-false initialization. |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h` | Declare `FavoriteTileButton::SetActive(bool)` (or equivalent) and the storage `bool is_active_ = false;`. |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc` | Plumb `is_active` from the item model into each tile during `Update()`. Extend `FavoriteTileButton::UpdateAppearance()` to render the active treatment when `is_active_` is set. |

Confirm exact paths during implementation. The four-file shape above is the target; if `MahoSidebarFavoriteItemModel` lives in a different header, edit that header instead.

---

## File-by-file plan

### 1. Item model — add `is_active`

In the header that declares `MahoSidebarFavoriteItemModel`, add:

```cpp
bool is_active = false;
```

Place it immediately after `is_favorite` to keep related boolean flags grouped. Default-construct to `false` so existing call sites and the background builder are unaffected.

### 2. UI-thread enrichment — `BuildFavoritesModel(Browser*)`

In the body of `BuildFavoritesModel(Browser*)`, after the favorites vector has been populated and before it is returned:

1. Look up the active `WebContents` via `browser->tab_strip_model()->GetActiveWebContents()`.
2. If it is non-null, resolve `MahoTabIdHelper::FromWebContents(active)->stable_tab_id()`. Hold it in a local `std::string`.
3. Iterate the favorites; if `item.tab_id == active_id` (and `active_id` is non-empty), set `item.is_active = true`. Stop after the first match (at most one active favorite).

Pseudo-code shape:

```cpp
content::WebContents* active = browser->tab_strip_model()->GetActiveWebContents();
std::string active_id;
if (active) {
  if (auto* helper = MahoTabIdHelper::FromWebContents(active)) {
    active_id = helper->stable_tab_id();
  }
}
if (!active_id.empty()) {
  for (auto& item : model.favorites.items) {
    if (!item.tab_id.empty() && item.tab_id == active_id) {
      item.is_active = true;
      break;
    }
  }
}
```

Notes:

- This is a UI-thread function; calling into `Browser*` and `TabStripModel*` is safe here.
- Add an include for `MahoTabIdHelper` if not already pulled in. Mirror the include style used elsewhere in this file (`maho/browser/maho_tab_id_helper.h`).
- Do not introduce a helper function for this loop. It is a four-line addition; extract later if a third caller appears.

### 3. Background builder — leave unchanged

`BuildFavoritesModelForSpaceIdJson()` must continue to operate without `Browser*` / `TabStripModel*`. The new `is_active` field defaults to `false`, which is exactly the right value for this code path. No edits beyond verifying the default is preserved through any (de)serialization the JSON path performs.

If the JSON path round-trips `MahoSidebarFavoriteItemModel` through a serializer, confirm `is_active` is either omitted or written as `false`. Do not add `is_active` to the JSON schema. The JSON pipeline should not be the source of truth for active state.

### 4. Tile button — accept and render the bit

In `FavoriteTileButton`:

1. Add `bool is_active_ = false;` storage.
2. Add a setter (e.g. `void SetActive(bool active)`) that updates `is_active_` and calls `UpdateAppearance()` if the value changed. Mirror the existing setter style on this class.
3. Extend `UpdateAppearance()` to branch on `is_active_` and render the active background, foreground, and any accent ring used by `SidebarTabRowView::UpdateAppearance()`. Reuse the same color tokens / theme keys as the tab row to keep favorites visually consistent with active tab rows.

Visual-priority rule for `UpdateAppearance()`:

- Pressed > hover > active > focus default. Pressed and hover should still win when the user is interacting with the tile, so the active background only shows in the resting state. This matches how `SidebarTabRowView` behaves: hover/pressed are short-lived; active is the resting indicator.
- Adjust the order if the existing tab row uses a different precedence; the rule is "match the tab row precedence exactly," not "introduce a new ordering."

### 5. Grid view — propagate the bit per tile

In `MahoSidebarFavoritesGridView::Update()` (or wherever the grid currently maps each `MahoSidebarFavoriteItemModel` onto a `FavoriteTileButton`), call `tile->SetActive(item.is_active)` alongside the existing per-tile setters (title, subtitle, url, tab_id, favicon, etc.).

If `Update()` rebuilds tiles wholesale rather than diffing, set `is_active_` directly on construction. Either path is acceptable; pick whichever matches the existing pattern in this file.

---

## Verification

Per repo conventions in `AGENTS.md`:

1. `lsp_diagnostics` on every changed file. No new errors beyond pre-existing upstream noise.
2. `python3 maho-chromium/build/scripts/build_maho.py` from the repo root. Builds clean. Do not edit any `BUILD.gn` if the build fails; stop and report (per the GN guardrail in `AGENTS.md`).
3. Manual smoke (in built Chrome):
   - Open a tab whose URL is also a favorite. Confirm the favorite tile renders the active treatment.
   - Switch to a different tab (not in favorites). Confirm no favorite tile renders active.
   - Switch back to the favorited tab. Confirm the same favorite tile renders active again.
   - Hover and press the active favorite tile. Confirm hover/press visuals win over the active resting visual, and that the active visual returns when the cursor leaves and the button is released.
   - Close the tab that the active favorite points to. Confirm the favorite no longer renders as active after the next sidebar refresh.
   - With a favorite whose `tab_id` does not match any live tab: confirm the tile is never active.
4. Optional regression check: existing favorites tests still pass (`maho_sidebar_navigation_interactive_test.cc` and any contract tests under `maho-chromium/browser/ui/views/sidebar/`). Do not add new tests in this plan unless a regression is observed; the active bit is small enough that manual verification is the cheaper path. If a contract test asserts the exact shape of `MahoSidebarFavoriteItemModel`, update its expected fixture to include `is_active = false` for the default case and `is_active = true` for the matching-favorite case.

Stop after the first successful build plus manual smoke pass. Do not re-verify.

---

## Risks

1. **Mojo / serialization coupling.** If `MahoSidebarFavoriteItemModel` is also a Mojo struct or is serialized to JSON for the WebUI surface, adding `is_active` may require touching the IDL or schema. Mitigation: keep `is_active` as a C++-only field if the model is shared with WebUI; let only the Views path observe it. The WebUI favorites surface (if any) does not need active-state plumbing in this plan.
2. **Update cadence.** The favorites grid only re-renders when something triggers a `BuildFavoritesModel(Browser*)` + `Update()` cycle. If active-tab changes do not currently trigger that cycle in some edge case (e.g. background space, archived space), the active state can lag. Mitigation: out of scope here. If lag is observed, add a follow-up plan to subscribe to `TabStripModelObserver::OnActiveTabChanged` from whatever owns the favorites builder. Do not add that subscription as part of this plan.
3. **Multiple windows.** Each browser window has its own active tab. The favorites grid is owned by a specific browser window, so `BuildFavoritesModel(browser_)` already scopes correctly. Confirm by inspection during implementation that the `Browser*` passed in is the window whose sidebar is being rendered, not a stale or global one.
4. **Stable id mismatch.** If a favorite's stored `tab_id` is from a different `MahoTabIdHelper` generation (e.g. profile reset, migration), the equality check will silently fail and the tile will never highlight. Acceptable for v1; the user-visible symptom is the same as today (no highlight) for those stale rows. Out of scope to repair stale tab ids.
5. **Single-active assumption.** The plan stops at the first match. If two favorites somehow share the same `tab_id`, only the first is marked active. This is the intended behavior; duplicate-`tab_id` favorites are a separate data-model bug, not a rendering bug.

---

## Why minimal plumbing, not a refactor

A "favorites is just another tab tree node" refactor would solve active state for free, because `SidebarTreeNode::is_active` already exists. It would also:

- Move favorites under `MahoSidebarTabListView`'s rendering path.
- Change drop targets, drag handling, and context menus.
- Break the favorites grid's bespoke layout (tile grid vs. row list).
- Touch every test that distinguishes "favorite" from "tab row."

That refactor is not justified by a missing highlight. The minimum cost fix is one boolean field, one UI-thread enrichment block, one setter, one branch in `UpdateAppearance()`. That is the entire surface of this plan.
