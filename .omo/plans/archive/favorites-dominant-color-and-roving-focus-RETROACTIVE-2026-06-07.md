# Favorites Dominant Color and Roving Focus Retroactive Plan — 2026-06-07

**Status:** Implementation landed 2026-06-07; documentation 2026-06-07
**Owner:** Sisyphus

## 1. Goal

Retroactively document the design and implementation of the Favorites Dominant Color and Roving Focus features that landed as untracked sidebar changes. This documentation serves as the source of truth for audits of these changes.

## 2. Files Touched

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h`

## 3. As-Built API Surface

### Dominant Color Extraction & Cache
- File-scope free function:
  ```cpp
  std::optional<SkColor> ExtractFaviconDominantColor(const ui::ImageModel& image_model);
  ```
  Uses `color_utils::CalculateKMeanColorOfBitmap()` to compute a representative dominant color. Returns `nullopt` for empty or non-image models.
- Member field `favicon_dominant_color_cache_` (`base::flat_map<GURL, SkColor>`) in `MahoSidebarFavoritesGridView`, populated in `Update()` and asynchronously in `OnFaviconLoaded()`.
- Public accessor:
  ```cpp
  std::optional<SkColor> GetFaviconDominantColor(size_t index) const;
  ```

### Active-Tile Border Glow
- Border color updated when a tile is active using `FavoriteTileButton::SetActiveBorderColor()`. Falls back to surface variant color when no dominant color is cached.
- Test hook:
  ```cpp
  void SetTileActiveBorderColorForTesting(size_t index, std::optional<SkColor> color);
  ```

### Drag Placeholder Ghost Border
- `ActivateDragPlaceholder()` extracts dominant color from the drag icon, applying a 50% opacity color to the placeholder tile border as a visual preview.

### Roving Keyboard Focus
- Roving focus (LEFT, RIGHT, HOME, END, SPACE, RETURN) manages keyboard navigation inside the essentials grid.
- All tiles set to `FocusBehavior::ACCESSIBLE_ONLY` after `Update()` so individual tiles do not act as separate Tab stops.
- Roving focus state is tracked by `roving_focus_index_` (size_t) and modified via:
  ```cpp
  void PromoteRovingFocus(size_t index);
  void AdvanceRovingFocus(int delta);
  bool HandleGridKeyFromTile(ui::KeyboardCode key);
  size_t GetFirstVisibleTileIndex() const;
  ```
- UP and DOWN keys are explicitly ignored (Zen parity: UP/DOWN not supported since upstream Firefox advances focus linearly rather than navigating grid columns).

## 4. Test Coverage

The following tests cover these features in `maho_sidebar_dnd_dispatch_unittest.cc`:
- `FavoriteActive_BorderDefaultsToSurfaceVariantWithoutColor`
- `FavoriteActive_BorderUsesInjectedDominantColor`
- `FavoriteActive_PlusFocus_FocusBorderWinsOverActiveColor`
- `FavoriteActive_PlusHover_FillBrightensWithoutBorderChange`
- `FavoriteActive_TogglesBackToIdle`
- `RovingTabStop_NoTileIsTabStopAfterUpdate`
- `RovingTabStop_FocusUpdatesBorderAndTracksRovingIndex`
- `RovingTabStop_ArrowKeyAdvancesFocusToNextTile`
- `RovingTabStop_HomeKeyJumpsToFirstTile`
- `RovingTabStop_NonVisibleTileCannotBeTabStop`
- `FavoriteTileButtonDoesNotStealFocusOnPress`

## 5. Known Limitations

- Multi-row UP/DOWN roving keyboard navigation is not implemented (intentionally stripped for Zen parity).

## 6. Reference

- `favorite-active-glow-and-favicon-source-fix-plan-2026-06-03.md` (which excluded dominant color as non-goal #1; this retroactive plan provides the specification for that work).
