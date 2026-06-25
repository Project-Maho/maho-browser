# Sidebar Search Pill Compact Variant Removal Retroactive Plan — 2026-06-07

**Status:** Implementation landed 2026-06-07; documentation 2026-06-07
**Owner:** Sisyphus

## 1. Goal

Retroactively document the removal of the compact variant of the sidebar search pill, bringing search pill geometry into exact Zen parity.

## 2. Files Touched

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc`

## 3. As-Built API Surface

### Removed Tokens
- Removed `kSearchPillCompactHeightDp` (35dp) and `kSearchPillCompactCornerRadiusDp` (18dp) from `maho_sidebar_layout_tokens.h`.
- Removed `kSearchPillTrailingInsetDp` (15dp) and `kSearchPillLeadingInsetDp` (13dp) from `maho_sidebar_search_view.cc`.

### Geometry Unification
- Unified `MahoSidebarSearchView::UpdateSearchChrome` to use `kSearchPillCornerRadiusDp` (16dp) and `kSearchPillHeightDp` (38dp) for both placeholder and active modes.
- Insets are unified to `kSearchCardLeadingInsetDp` / `kSearchCardTrailingInsetDp`.
- Visual distinction between modes is color-only.

### Context-Menu callback change
- `ShowContextMenuForViewImpl` binds `FocusCommandSurface` instead of `BeginEditing` to the address bar context menu callback.
- Right-click → "Search" now opens the command palette overlay (intentional UX choice to enhance discoverability).
- Left-click on the idle button still triggers `BeginEditing` inline.

## 4. Test Coverage

- Geometry verification test is added (checks that height and corner radius are identical in placeholder and active modes).
- Context menu is verified manually.

## 5. References

- Zen parity design specs (§4C).
