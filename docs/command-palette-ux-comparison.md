# Command Palette & Address Bar UX Comparison

**Swift Maho → Chromium Maho → current verified state**

## Executive summary

The Chromium command palette is now materially aligned with the Swift flow in the verified path:

- Current-tab prefill is verified, including long-URL visibility in the top field.
- The shell is 720dp wide, 220dp tall on open, centered, and clamped within the work area.
- Open, close, and resize motion all exist in the current implementation.
- Cmd+L, Cmd+T, AI mode, site search, and recent-search fallback are distinct implemented modes/behaviors.
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
| AI mode | Supported | Supported |
| Site search | Supported | Supported |
| Recent searches | Supported | Supported |
| Empty-state recent search fallback | Supported | Supported |
| Divider under input | Supported | Supported |
| Result capping | 5 visible items | 5 visible items |
| Row heights | 54 / 66 | 46 / 56 |
| Selected row style | Expanded selected row | Selected row styling implemented |
| Inline subtitle | Present when unselected | Present when unselected |
| Shortcut badges | Present | Present |

## Notes on the verified current state

- The latest current-tab long-URL artifact is `.sisyphus/artifacts/command-palette-parity/after-fix/ui-pass-currenttab-longurl-20260508c_00_open.png`.
- The latest current-tab and populated artifacts are `.sisyphus/artifacts/command-palette-parity/after-fix/ui-pass-currenttab-20260508a_00_open.png` and `.sisyphus/artifacts/command-palette-parity/after-fix/ui-pass-populated-20260508a.png`.
- The 2026-05-07 after-fix captures remain the verification source for AI mode, site search, keyboard navigation, recent-search activation, pointer hover/click, and popup resize.
- The remaining differences should be treated as polish-only, not blockers.

## Deliberately not claimed here

- Exact pixel parity with Swift or Arc.
- Exact animation feel equivalence.
- Any unverified claim about match highlighting.

*Report updated 2026-05-08. Sources: current Chromium command-palette implementation and the latest after-fix runtime captures.*
