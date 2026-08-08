# Command Palette & Address Bar UX Comparison

**Swift Maho → Chromium Maho → current verified state**

## Executive summary

The Chromium command palette is now materially aligned with the Swift flow in the verified path:

- Current-tab prefill is verified, including long-URL visibility in the top field.
- The shell is 720dp wide, 220dp tall on open, centered, and clamped within the work area.
- Open, close, and resize motion all exist in the current implementation.
- Cmd+L, Cmd+T, the persistent Search / Ask Maho selector, site search, and recent-search fallback are distinct implemented modes/behaviors.
- Remaining differences are polish-level: exact spacing, motion feel, and some visual tuning.

This doc intentionally avoids pixel-perfect claims. The verified current-tab flow has no material blocker.

## Current-state comparison

| Area | Swift Maho | Chromium Maho |
|---|---|---|
| Shell width | 720pt | 720dp |
| Shell initial height | 224pt | 220dp |
| Shell positioning | Centered with clamp | Centered with clamp |
| Panel corner radius | 22px | 22px |
| Open animation | Slide + fade + bounce | Slide + fade |
| Close animation | Slide + fade | Slide + fade |
| Resize animation | Animated bounds change | Animated bounds change |
| Cmd+L vs Cmd+T | Separate modes | Separate modes |
| Cmd+L current-tab prefill | Prefill + select all | Verified, including long-URL leading slice in the top field |
| Native AI entry | AI mode | Verified Search / Ask Maho selector (`PaletteAction::kAskMaho`) |
| Site search | Supported | Supported |
| Recent searches | Supported | Supported |
| Empty-state recent search fallback | Supported | Supported |
| Divider under input | Supported | Supported |
| Result capping | 5 visible items | 5 visible items |
| Row heights | 54 / 66 | 46 / 56 |
| Selected row style | Expanded selected row | Selected row styling implemented |
| Inline subtitle | Present when unselected | Present when unselected |
| Shortcut badges | Present | Present |
| Current-tab visual strip | Present | Removed (2026-05-16) |
| Left selection rail | Present | Removed (2026-05-16) |
| Focus stealing loop | Not present | Removed (2026-05-16) |

## Notes on the verified current state

- The latest current-tab long-URL artifact is `.sisyphus/artifacts/command-palette-parity/after-fix/ui-pass-currenttab-longurl-20260508c_00_open.png`.
- The latest current-tab and populated artifacts are `.sisyphus/artifacts/command-palette-parity/after-fix/ui-pass-currenttab-20260508a_00_open.png` and `.sisyphus/artifacts/command-palette-parity/after-fix/ui-pass-populated-20260508a.png`.
- The verified native AI entry is the persistent Search / Ask Maho selector (`PaletteAction::kAskMaho`), not a transient palette AI mode. `MahoPrivateCapability::kAI` remains the privacy gate for the AI side panel/private capability.
- The 2026-05-07 after-fix captures remain the verification source for the then-current transient AI-mode flow, site search, keyboard navigation, recent-search activation, pointer hover/click, and popup resize.
- The remaining differences should be treated as polish-only, not blockers.

## 2026-05-16 UX cleanup

Five visible issues removed across two passes:

1. **Current-tab visual strip** — the frosted-glass rounded-rect container (`current_tab_shell_`) with the "Current tab" badge and URL label has been removed. `CommandOverlayMode::kCurrentTab` semantics are preserved: URL prefill, select-all, and `CURRENT_TAB` disposition are unchanged.
2. **Focus-stealing loop** — `ScheduleFocusRetentionPasses` and its 30-pass timer loop removed from the controller. One-shot `widget_->Activate()` + `overlay_ptr->RequestFocus()` at Show time is kept. Deactivation-hide timer is unchanged.
3. **Left purple selection rail** — `selection_indicator_` view and `kSelectionRailWidthDp`/`kSelectionRailCornerRadiusDp` constants removed from `MahoCommandResultRowView`. The selected-row fill and border are unchanged.
4. **Residual current-tab visual sizing** — removed `kCurrentTabPanelBottomInsetDp` (38dp extra bottom padding), `kCurrentTabFontSizeDeltaDp` (enlarged font), and collapsed the `kCurrentTabSearchRowHeightDp`/`kCurrentTabSearchTextfieldHeightDp` branches (both were identical to the shared values). `UpdateLayoutForMode()` and `UpdateTextfieldPresentation()` no longer branch on `kCurrentTab`.
5. **Residual current-tab input surface alpha** — `OnThemeChanged()` no longer applies mode-specific alpha tweaks to input fill (`0xFF` vs `0xFD`) or border (`0x52` vs `0x3A`). Both now use the color token directly.

## Deliberately not claimed here

- Exact pixel parity with Swift or Arc.
- Exact animation feel equivalence.
- Any unverified claim about match highlighting.

*Report updated 2026-07-24. Sources: current Chromium command-palette implementation and the latest after-fix runtime captures.*
