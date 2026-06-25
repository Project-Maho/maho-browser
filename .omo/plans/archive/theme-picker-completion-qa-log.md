# Theme Picker Standalone QA Log

**Date:** 2026-05-22
**Plan ref:** theme-picker-completion-plan.md §6.1
**Method:** Standalone bundle served at http://127.0.0.1:4072/, Chromium headless + DOM inspection

---

## Visual Parity (Phase 5)

Screenshot captured and compared against `maho/reference_screenshots/create-theme.png` right-panel crop.

| GT element | Present in render | Notes |
|---|---|---|
| Mode chips inside canvas, top-left | ✅ | Chips are absolutely positioned inside `tp-canvas` |
| Active chip (✦) = rounded-rect, dark fill, subtle border | ✅ | `border-radius: var(--tp-chip-radius)` = 14px |
| Inactive chips = muted color | ✅ | `color: var(--tp-text-muted)` |
| Canvas = dark gray + dot-grid, no glows | ✅ | CSS `radial-gradient` dot-grid on `#282b38` base |
| Prompt text "Tap to pick a color for this space" | ✅ | Centered in canvas |
| Brightness − / + at canvas bottom | ✅ | `tp-brightness-row` positioned at `bottom: 20px` |
| 9 swatches in row | ✅ | `tp-swatch--0` through `tp-swatch--8` |
| Left chevron hidden (matches GT) | ✅ | `.tp-chevron--left { visibility: hidden }` |
| Right chevron `›` visible | ✅ | `data-maho-el="D5"` |
| First swatch (white) selected with ring | ✅ | `tp-swatch--0 is-selected`, `aria-pressed="true"` |
| Wave = smooth continuous arcs | ✅ | Two `<path>` elements, no stroke-dasharray |
| Dial = dotted ring, no blue dot at default | ✅ | `is-level-0` → `opacity: 0` |
| Cancel / Done buttons right-aligned | ✅ | `tp-footer` flex justify-content: flex-end |

All 12 `data-maho-el` markers confirmed in DOM:
D1 KP-3, D2, D3, D4 KP-11, D5, D6, D7, D8, KP-10, KP-5, KP-6, KP-7

---

## Interaction QA (Phase 6.1 — stub-level)

All interactions verified via store logic unit tests:

| Action | Expected | Verified |
|---|---|---|
| Default state | mode=sparkle, preset_index=0, brightness=0, loading=false | ✅ (stub fires Initialize on microtask) |
| Sparkle chip selected on load | `aria-pressed="true"` on KP-5 | ✅ DOM confirmed |
| Dial indicator hidden at default | `is-level-0` → opacity:0 | ✅ CSS + DOM confirmed |
| brightness=0 → dialLevel=0 (hidden) | `getDialLevel(0) === 0` | ✅ unit test |
| brightness=10 → dialLevel=1 | `getDialLevel(10) === 1` | ✅ unit test |
| brightness=100 → dialLevel=8 | `getDialLevel(100) === 8` | ✅ unit test |
| Preset wraps: 9→0, -1→8 | `normalizePresetIndex(9) === 0` | ✅ unit test |
| Brightness clamp: 110→100, -5→0 | `clampBrightness(110) === 100` | ✅ unit test |

---

## Deferred (Phase 7 — requires Chromium build)

- E1–E10 from completion-plan.md §6.2 (Mojo round-trip, live sidebar preview, etc.)
- Build blockers: Group A (MahoTabIdHelper deps), Group B (maho_space_create BUILD.gn dep)

---

## Status: Phase 5 ✅ COMPLETE, Phase 6.1 ✅ COMPLETE
