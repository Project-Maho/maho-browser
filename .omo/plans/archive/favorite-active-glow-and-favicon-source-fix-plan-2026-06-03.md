# Sidebar Favorite Active Glow + Favicon Source Fix Plan

**Created:** 2026-06-03
**Status:** PLAN-ONLY. The dominant-color path explicitly excluded as non-goal #1
was implemented in a separate scope; see
favorites-dominant-color-and-roving-focus-RETROACTIVE-2026-06-07.md.
**Target:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.{cc,h}`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`
**Goal:** Fix two confirmed bugs in the sidebar favorite active visual stack: (1) newly-added favorites never load real favicons, and (2) the active favicon glow renders as a blurry, distorted halo because of low-quality bilinear upscaling instead of a real Gaussian blur.

---

## Goal

Two bugs, one plan, because they share the same surface and the same active-tile visual.

**Bug 1, the favicon source bug.** A favorite that is created for a tab whose favicon has not yet finished loading currently displays the default globe forever, even after the tab's real favicon arrives. The favorites grid resolves a per-favorite favicon by first asking `TabUIHelper::GetFavicon()` for the live tab. That helper never returns an empty image, so the grid's "fall through to `FaviconService` async lookup" branch (gated on `fav.IsEmpty()`) is dead for these tabs. The default-globe placeholder gets locked in.

**Bug 2, the glow rendering bug.** The active-favorite tile draws a soft halo behind the crisp 15dp favicon. The current implementation is a `views::ImageView` that takes the same favicon and scales it from 16x16 to 47x47 with `kLow` filter quality, then composites it at 0.20 opacity. This is bilinear upscaling, not Gaussian blur. It reads as a stretched, distorted favicon, not a soft glow. Zen achieves this with a CSS `filter: blur(20px)` on a layer; we will mirror that with a real GPU-accelerated blur filter.

**The contract.** A freshly-added favorite must eventually display the real site favicon (not the default globe), and an active favorite tile must render a Zen-style soft favicon halo using a real Gaussian blur applied at the layer level, not low-quality upscaling.

This plan is implementation-ready but plan-only. Implementation is deferred to a separate session.

---

## Findings (with file:line citations)

### Bug 1 — favicon source

1. `FaviconStatus`'s default constructor pre-fills `image` with `IDR_DEFAULT_FAVICON`. See `content/public/browser/favicon_status.cc:13-15`. This means a brand-new tab whose favicon has not been resolved still has a non-empty `image` field on its `NavigationEntry` favicon status.
2. `TabUIHelper::GetFavicon()` at `chrome/browser/ui/tab_ui_helper.cc:185-215` returns that default favicon when no real favicon has been resolved yet. It never returns an empty `gfx::Image`. Callers cannot use `IsEmpty()` to distinguish "real favicon" from "default placeholder."
3. `favicon::FaviconUtils` and the related helpers in `chrome/browser/favicon/favicon_utils.cc:105-132` show that the upstream-stable signal for "the favicon on this WebContents is the real site favicon" is `ContentFaviconDriver::FaviconIsValid()`, not `image.IsEmpty()`. This is what Chromium itself uses to decide whether the favicon has been resolved.
4. `MahoSidebarFavoritesGridView::Update()` at `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc:720-755` populates a `live_tab_favicons` map from each live tab's `TabUIHelper::GetFavicon()`. Because that helper always returns a non-empty image, every entry passes the existing `!fav.IsEmpty()` check, and the favorites grid's fallback to `FaviconService::GetFaviconImageForPageURL()` (gated on `fav.IsEmpty()`) is never entered for these tabs. The default globe is therefore latched as the favorite's favicon for the lifetime of the tab.
5. The `resolved_favicon_cache_` map elsewhere in this file persists real favicons across `Update()` cycles. It must continue to do so. Default placeholders are never written into it today; that behavior must be preserved.

### Bug 2 — glow rendering

6. The favorite tile glow is a `views::ImageView` child of `FavoriteTileButton`. Its image is the resolved favicon. `SetImageSize(kGlowImageSize, kGlowImageSize)` is called with `kGlowImageSize = 47`, while the source favicon is 16x16. `views::ImageView::OnPaintImage` (see `ui/views/controls/image_view.cc`) uses `cc::PaintFlags::FilterQuality::kLow` (bilinear) by default when the image is scaled. This produces a stretched, slightly smoothed but recognizably distorted version of the icon, not a Gaussian blur.
7. A 0.20 layer opacity is applied to the glow, which softens the artifact but does not hide it. Real Zen-style halos (`filter: blur(20px)` in the upstream Zen CSS) cannot be approximated by bilinear upscaling regardless of opacity.
8. The recently-landed layer ordering fix (`SetPaintToLayer` on `glyph_container` and `tile_label_`, plus `SetFillsBoundsOpaquely(false)`) is correct and orthogonal to the blur defect. `views::View::ReorderChildLayers` stacks layered children in reverse view-child-order, so the glow ends up at the bottom and the glyph + label composite above it. The current ordering must be preserved.
9. `cc::FilterOperation::CreateBlurFilter(SkScalar amount)` returns a `cc::FilterOperation` of type `FILTER_TYPE_BLUR`. Wrapped in a `cc::FilterOperations` and passed to `ui::Layer::SetFilters(...)`, this produces a real GPU-accelerated Gaussian blur on the layer. This is the API Chromium itself uses for layer blurs, and it is the closest semantic match for Zen's `filter: blur(20px)`.

---

## Target contract

After this plan is implemented, all of the following must hold:

1. The `live_tab_favicons` map in `MahoSidebarFavoritesGridView::Update()` only contains entries for tabs whose `favicon::ContentFaviconDriver::FromWebContents(contents)->FaviconIsValid()` returns `true`. Tabs whose favicons have not yet resolved are skipped at this stage.
2. For any favorite whose tab is not present in `live_tab_favicons` (favicon still loading, no driver, no live tab at all), `Update()` falls through to the existing `FaviconService::GetFaviconImageForPageURL()` async path. The default globe placeholder is never the terminal value for a favorite whose URL has a real favicon.
3. `resolved_favicon_cache_` continues to persist real favicons across `Update()` cycles. Default-placeholder globes are never written into it. This is existing behavior and must be preserved.
4. The active-favorite glow uses `cc::FilterOperation::CreateBlurFilter(kFavoriteGlowBlurRadiusDp)` applied to the glow `ImageView`'s layer via `ui::Layer::SetFilters`. The filter type is `FILTER_TYPE_BLUR`, the radius is non-zero, and the value is tuned to match Zen's visual feel (start at `16.f`, adjust up to ~`24.f` after manual visual QA).
5. Glow visibility is gated on `is_active_`. This is existing behavior and must be preserved.
6. The glow's underlying image is the real resolved favicon, after the bug 1 fix produces real favicons reliably for newly-added favorites. The glow image and the crisp `tile_icons_` image come from the same source.
7. `tile_icons_` continues to display the crisp 15dp favicon on top of the glow. No change to that layer.
8. `glyph_container` and `tile_label_` keep `SetPaintToLayer()` + `SetFillsBoundsOpaquely(false)` so they composite above the glow's blurred layer. This is the recently-landed layer ordering fix and must be preserved.
9. The accent-tinted fill (`kMahoColorFavoriteActiveFill`, `kMahoColorFavoriteActiveHovered`) renders unchanged.

---

## Architectural choices, with rationale

**Bug 1 fix surface.** The single `live_tab_favicons` builder loop in `Update()` is the right surface. A per-tab `FaviconIsValid()` guard at the point of `favicon_pairs.emplace_back(...)` means the rest of the pipeline (`!fav.IsEmpty()` short-circuits, async fallback, `resolved_favicon_cache_` writes) keeps its existing shape. The alternative, branching per favorite tile inside the per-tile dispatch, would scatter the fix and force `if/else` logic into a code path that already has its own complexity. One guard, one site.

**Bug 2 fix approach.** A layer-level GPU blur via `cc::FilterOperation::CreateBlurFilter` is correct because:

- It is what Chromium itself uses for layer blurs. It composites cheaply on the GPU; one extra filter per active tile is bounded and acceptable.
- It is theme-stable. The blur is purely a layer-level transformation; theme-driven accent fills are unaffected.
- It is semantically the closest equivalent to Zen's `filter: blur(20px)`.

The two alternatives are weaker:

- **Custom Skia paint** (drawing the favicon with a `SkBlurMaskFilter` in an overridden `OnPaint`) would require off-thread Skia ops, a custom `views::View` subclass, and an extra image cache to avoid re-blurring every paint. More code, more risk, no win.
- **Pre-blurred favicon images** (generate a blurred bitmap once and cache it) would mean managing two image caches (`resolved_favicon_cache_` plus a sibling blurred-favicon cache), invalidating both in lockstep, and re-blurring on theme changes. More state, no win.

**Glow image sizing.** Drop `SetImageSize(kGlowImageSize, kGlowImageSize)` on the glow's `ImageView`. Let the favicon render at its natural size (16x16). The blur filter on the layer expands the visible halo without bilinear upscaling. The glow `View`'s bounds (currently centered and slightly larger than the favicon, set in `Layout(PassKey)`) must give the blur breathing room; verify they remain larger than the natural favicon size + the blur radius footprint. If needed, increase the glow `View`'s bounds modestly to avoid clipping the blur edge.

**Out of scope from the start.** Accent-tinted fill (`kMahoColorFavoriteActiveFill` / `Hovered`) is untouched. We are fixing the halo, not redesigning the active state.
- Dominant color extraction was also excluded as Non-Goal #1 in early drafts; its separate spec and implementation are documented in [favorites-dominant-color-and-roving-focus-RETROACTIVE-2026-06-07.md](file:///Users/indo/code/project/maho-workspace/.sisyphus/plans/favorites-dominant-color-and-roving-focus-RETROACTIVE-2026-06-07.md).

---

## File-by-file plan

### 1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`

**i. Includes.** Add:

- `#include "components/favicon/content/content_favicon_driver.h"` for `FaviconIsValid()`.
- `#include "cc/paint/filter_operation.h"` for `cc::FilterOperation::CreateBlurFilter`.
- `#include "cc/paint/filter_operations.h"` for `cc::FilterOperations`.

Insert each into its alphabetically appropriate position within the existing include block.

**ii. `Update()` — gate `live_tab_favicons` on `FaviconIsValid()`.** Inside the per-live-tab loop that builds `favicon_pairs` (and feeds `live_tab_favicons`), at the top of the loop body, before `TabUIHelper::FromWebContents(contents)->GetFavicon()` is consulted, add:

```cpp
auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents);
if (!driver || !driver->FaviconIsValid()) {
  continue;
}
```

That single guard is the entire bug 1 fix at this layer. Tabs whose favicon is still resolving fall through to the existing `FaviconService::GetFaviconImageForPageURL()` async path, which already handles delivering the real favicon when it arrives via `OnFaviconLoaded()`.

**iii. `FavoriteTileButton` glow construction — apply the blur filter.** In the `FavoriteTileButton` ctor (or wherever `glow_view_` is initialized inside `MahoSidebarFavoritesGridView`'s tile setup), after the existing `SetPaintToLayer()` + `SetFillsBoundsOpaquely(false)` + `SetOpacity(0.20f)` block, add:

```cpp
glow_view_->layer()->SetFilters(
    {cc::FilterOperation::CreateBlurFilter(kFavoriteGlowBlurRadiusDp)});
```

Use a brace-initialized `cc::FilterOperations` (single-element list). Place this immediately after the existing layer setup so the blur is applied as soon as the layer exists, before any `Update()` call sets the image.

**iv. Drop `SetImageSize` on the glow.** Remove the `glow_view_->SetImageSize({kGlowImageSize, kGlowImageSize})` call in both `Update()` and `OnFaviconLoaded()` (wherever it currently fires). Let the favicon render at its natural size on the glow `ImageView`. The blur filter on the layer spreads the image visually; bilinear upscaling is no longer doing the work.

**v. Verify glow `View` bounds.** Inspect `Layout(PassKey)` (or whichever method positions the glow `View` inside the tile). The bounds must:

- Stay centered behind the favicon.
- Be at least `natural_favicon_size + 2 * kFavoriteGlowBlurRadiusDp` on each axis to give the blur room without clipping.

If the current bounds are sized to the old `kGlowImageSize` (47), they probably already satisfy this. If they are sized exactly to the favicon, expand them.

**vi. Define `kFavoriteGlowBlurRadiusDp`.** Add a file-scope `constexpr float kFavoriteGlowBlurRadiusDp = 16.f;` in the anonymous namespace at the top of this `.cc` file (or in `maho_sidebar_layout_tokens.h` if that header already collects such tokens; prefer the tokens header if it exists). Document with a comment that the value is tuned for Zen-parity feel and should be adjusted up to ~`24.f` if visual QA shows it too tight.

### 2. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h`

**i. Token.** If `kFavoriteGlowBlurRadiusDp` is declared in this header (e.g. alongside other tile constants like `kGlowImageSize`), add it here as `static constexpr float`. If `maho_sidebar_layout_tokens.h` is the canonical home for shared tokens, declare it there instead. Pick whichever location matches existing convention; do not introduce a new header.

**ii. No new public surface.** No new accessors. The existing `GetTileGlowForTesting()` accessor stays as the test seam.

### 3. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`

**i. Update `FavoriteActive_GlowMatchesTileFavicon`.** Today this test asserts the glow's image is non-empty when a favorite tile is active. Keep that assertion. Strengthen it: assert the glow's image equals (or has the same backing as) `tile_icons_`'s image, since both pull from the same resolved favicon. Drop any size assertions tied to the old `kGlowImageSize`; the glow image now renders at its natural size.

**ii. Add `FavoriteActive_GlowAppliesBlurFilter`.** When `is_active` is true on a tile, the test must verify the `glow_view_->layer()` has at least one `cc::FilterOperation` whose type is `FILTER_TYPE_BLUR` and whose blur radius is non-zero. Use `layer()->filters()` to read back the applied filter list, then iterate and assert.

**iii. Optional — `FavoriteActive_LiveTabDefaultFaviconDoesNotPoisonCache`.** Useful regression test:

- Set up a live tab whose `ContentFaviconDriver::FaviconIsValid()` returns `false` (e.g. a fresh `WebContents` with the default favicon placeholder).
- Trigger a favorites `Update()`.
- Assert: `resolved_favicon_cache_` does not contain a default-globe entry for that favorite's URL.
- Then mark `FaviconIsValid()` as `true` and trigger `OnFaviconLoaded()` with the real favicon.
- Assert: `resolved_favicon_cache_` now contains the real favicon, and the tile's `tile_icons_` reflects it.

If this test requires a non-trivial `WebContents` mock, defer it with a `// TODO(maho-favorites-favicon-tests):` placeholder citing this plan filename; do not let test-infra setup block the bug fix from landing.

---

## Test plan beyond unit tests

1. Build via `python3 maho-chromium/build/scripts/build_maho.py` from the repo root. This is the canonical lock-backed entrypoint per `AGENTS.md`. Do not skip the lock by running `autoninja` directly.
2. Manual QA matrix:
   - **Cold profile, brand-new favorite.** Open a brand-new site whose tab's favicon has not yet loaded. Immediately favorite it. Expected: tile shows the default globe briefly, then the real favicon resolves and is cached in `resolved_favicon_cache_`. Subsequent `Update()` cycles render the real favicon.
   - **Tab navigation while loading.** Start a navigation, and while the tab is still loading (favicon not yet resolved), favorite it. Expected: globe → real favicon when the load completes, with no manual refresh required.
   - **Active tab match.** Switch to the favorited tab. Expected: the favorite tile renders accent fill + a soft Gaussian-blurred favicon halo behind the crisp 15dp favicon. The halo reads as a soft glow, not a stretched icon.
   - **Theme switch.** Toggle light / dark theme while the favorite is active. Expected: glow blur and accent both update without flicker. The blur filter is layer-level and theme-independent; the accent fill follows the theme.
   - **Close source tab.** Close the tab the favorite points to. Expected: the favorite keeps showing the real favicon (cached in `resolved_favicon_cache_`), and the tile no longer renders the active state.
3. Manual visual sanity: capture before/after screenshots of an active favorite tile to confirm the halo no longer reads as distorted, and that the radius is in the Zen-parity range. If the chosen `kFavoriteGlowBlurRadiusDp` value (start `16.f`) reads too tight, raise to `20.f` or `24.f` and re-screenshot.

---

## Risks

1. **GPU cost of layer blurs on low-end machines.** Each active tile costs one blurred-layer composite. There are at most ~6 favorite tiles per sidebar at typical density, and only the active one is visible at a time. Bounded and acceptable. If profiling on a low-end device shows a hot spot, the blur radius is tunable; no other path change is needed.
2. **Blur radius is a visual decision.** Document the chosen value next to its declaration (`kFavoriteGlowBlurRadiusDp`) and note the Zen reference (`filter: blur(20px)`). The implementer should land an initial value (start `16.f`), capture a screenshot during manual QA, and adjust before merging.
3. **`cc::FilterOperation` reachability from this build target.** The cc paint headers (`cc/paint/filter_operation.h`, `cc/paint/filter_operations.h`) are widely included throughout `chrome/browser/ui/views`. Verify the existing `BUILD.gn` for `maho_sidebar_core` (the GN target that compiles `maho_sidebar_favorites_grid_view.cc`) already pulls `//cc/paint` or `//cc:cc` transitively before assuming includes are free. **If a new GN dep is required, STOP and report.** Per `AGENTS.md`, do not edit any `BUILD.gn` to "fix" a build failure; that guardrail applies to this plan as well.
4. **`ContentFaviconDriver` reachability.** `components/favicon/content/content_favicon_driver.h` is in `//components/favicon/content`. Verify the existing `BUILD.gn` for the maho sidebar target already depends on it (via existing favicon code paths in this file). **If a new GN dep is required, STOP and report.**
5. **`Update()` cadence.** The `live_tab_favicons` map is rebuilt every `Update()`. The new `FaviconIsValid()` guard is O(N) over live tabs and cheap. No new observers are added. Tabs whose favicons resolve mid-session will be picked up on the next `Update()` cycle, plus the existing `OnFaviconLoaded()` async path handles delivery for tabs not in the live map.
6. **Glow `View` bounds clipping.** If the glow `View`'s bounds were sized exactly to the old 47x47 image and now must accommodate a blur footprint, the blur edge can clip. The implementer must verify `Layout(PassKey)` gives the glow `View` enough room.

---

## Explicit non-goals

1. No per-favicon dominant color extraction. The glow uses the favicon image directly; we do not sample its dominant color or recolor anything. Accent color continues to come from the theme.
2. No per-space accent glow color tinting in this round. Glow color = favicon color, blurred. No accent overlay on the glow.
3. No removal of the glow infrastructure. The glow `ImageView`, the `is_active_` gate, and the layer ordering fix all stay. We are changing the filter, not the architecture.
4. No changes to focus border, accent fill colors, or roving focus model.
5. No changes to the favorites model layer. `MahoSidebarFavoriteItemModel` keeps its existing fields including `is_active`. The recent active-favorite plumbing plan is already landed and is not touched here.
6. No changes to drag, drop, click handling, context menu, or animation on favorites.
7. No `BUILD.gn` edits in this plan. If implementation discovers a new dep is required, the implementer must stop and ask, per the repo guardrail in `AGENTS.md`.

---

## Verification handoff

This document is plan-only. Implementation will follow in a separate session.

The implementer should:

1. Read this plan top to bottom before starting.
2. Apply the four edits in `maho_sidebar_favorites_grid_view.cc` (includes, `Update()` guard, glow blur filter, drop `SetImageSize`) and the matching `.h` token.
3. Update the existing unit test and add the new blur-filter assertion test.
4. Build via `python3 maho-chromium/build/scripts/build_maho.py`.
5. Walk the manual QA matrix above.
6. Stop after the first successful build + manual QA pass. Do not re-verify.

If any GN edit appears necessary at any point, stop and ask before touching `BUILD.gn`.
