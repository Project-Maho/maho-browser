# Theme Picker Zen 100% Parity Implementation Plan

**Date**: 2026-05-22
**Status**: Draft, awaiting user approval
**Architecture Reference**: `.sisyphus/plans/theme-picker-architecture-decision.md` (ADR)
**Baseline**: Zen Browser `cd1616d1c00e875dfb5e64289f858f59e0b4e6ee` (`zen-browser/desktop`)
**Scope**: Bring Maho `chrome://maho-space-create` WebUI to full functional + visual parity with Zen's `ZenGradientGenerator`

---

## ADR Compliance

This plan does NOT violate any of the forbidden directions in the architecture decision:
- ❌ Migration to `maho_space_config` — **Not pursued**
- ❌ Native Views theme picker resurrection — **Not pursued**
- ❌ Second production WebUI surface for theme picking — **Not pursued**
- ❌ Mojom schema changes — **Not pursued (opaque `theme_json` string preserved)**
- ✅ All work happens inside `maho-chromium/browser/resources/maho_space_create/`

---

## Out of Scope

These items are NOT addressed by this plan:

1. **Haptic feedback** (`Services.zen.playHapticFeedback`) — Chrome WebUI does not have access to this Mozilla-only API. Out of scope permanently.
2. **`--zen-grainy-background-opacity` CSS variable + `grain-bg.png` overlay rendering on the actual sidebar** — This is a Maho native rendering concern, separate from the picker UI. Tracked separately.
3. **Workspace multi-edit synchronization** — Maho edits a single space's theme at a time; Zen's multi-workspace concurrent state is not applicable.
4. **GN edits / Chromium build verification** — Per AGENTS.md, no GN edits without explicit user approval. Native parity verification deferred until build environment unblocked.

---

## Phase 1 — Wire Format Alignment (CRITICAL)

**Goal**: Maho's `theme_json` output matches Zen's `getTheme()` output field-by-field.

### 1.1 `type` field

| Field | Current Maho | Zen | Action |
|---|---|---|---|
| `type` | `'zen'` | `'gradient'` | Change to `'gradient'` |

Maho's native parser at `maho_space_theme_state.cc:202` accepts both via `if (*type == "gradient" || *type == "zen")`. Safe to change.

### 1.2 Color stop format

Zen's stop schema:
```json
{
  "c": [r, g, b],
  "position": {"x": 233, "y": 157},
  "isPrimary": true,
  "algorithm": "analogous",
  "lightness": 85
}
```

Current Maho stop schema:
```json
{
  "hue": 0.58,
  "saturation": 0.80,
  "brightness": 0.90,
  "isCustom": false,
  "isPrimary": true
}
```

Maho's native parser supports BOTH `c` array AND HSB fields (line 50-69 of theme_state.cc has fallback). To match Zen exactly, write **`c` as primary** with HSB as backward-compat fallback:

```ts
interface SerializedStop {
  c: [number, number, number];      // RGB 0-255 (Zen primary)
  position: {x: number; y: number}; // canvas % (0-100)
  isPrimary: boolean;
  algorithm?: ColorHarmony;          // primary stop only
  lightness?: number;                // primary stop only (0-100)
  hue?: number;                      // backward compat (0-1)
  saturation?: number;               // backward compat (0-1)
  brightness?: number;               // backward compat (0-1)
}
```

### 1.3 `opacity` field

| Aspect | Current Maho | Zen | Action |
|---|---|---|---|
| Range | Fixed `1` | `0.25–0.9` (macOS: `0.30–0.9`) | Add `opacity` to ThemeSelection, range 0.25-0.9 |
| Driven by | Nothing | Wave slider | Wire to wave |

### 1.4 `texture` field

| Aspect | Current Maho | Zen | Action |
|---|---|---|---|
| Range | 0-1 (continuous) | 0-1 (16 step snap) | Restore 16-step snap: `Math.round(t * 16) / 16` |
| Driven by | Dial | Dial | ✅ Match |

### 1.5 `position` per stop

| Aspect | Current | Zen | Action |
|---|---|---|---|
| Stored in wire | No | Yes (per stop) | Add `position` to each stop |
| Hydration | Position recomputed from hue | Position from wire directly | Read position from wire on hydrate |

### Files Modified
- `react/types.ts` — schema types
- `react/store.ts` — wire serialization
- `standalone/maho_space_create.mojom-webui.js` — stub seed JSON

### Acceptance Criteria
- `JSON.stringify(selectionToWireTheme(state))` field-by-field equal to a sample Zen `getTheme()` output (manually crafted from Zen source)
- Maho native parser accepts the new wire (verified by reading parser code; runtime verification deferred until Chromium build)
- Round-trip: `wireThemeToSelection(serializeThemeSelection(state))` preserves all stop positions

### Estimated Time
3-4 hours

---

## Phase 2 — Polar Color Wheel + Per-Stop Lightness

**Goal**: Match Zen's `getColorFromPosition` exactly. Brightness derives from canvas position per stop, not from a separate slider.

### 2.1 Position → color mapping

Current (Cartesian):
```ts
const hue = xNorm;                     // x = hue
const saturation = 1 - yNorm * 0.7;    // y = saturation
```

Zen (Polar):
```ts
const cx = rect.width / 2;
const cy = rect.height / 2;
const radius = (rect.width - padding) / 2;
const angle = Math.atan2(y - cy, x - cx);
let angleDeg = angle * 180 / Math.PI;
if (angleDeg < 0) angleDeg += 360;
const distance = Math.sqrt((x - cx) ** 2 + (y - cy) ** 2);
const normalizedDistance = 1 - Math.min(distance / radius, 1);
const hue = angleDeg;                          // 0-360
const saturation = 90 + (1 - normalizedDistance) * 10;  // 90-100
const lightness = (1 - normalizedDistance) * 100;       // edge=0, center=100
```

Mapping:
- Canvas edge → light (lightness 0)
- Canvas center → dark (lightness 100)
- Angle around center → hue
- Saturation stays high (90-100)

### 2.2 Brightness origin

| Source | Current | Zen |
|---|---|---|
| Per-stop brightness | Shared global from wave | Computed per-stop from position |
| Wave function | Sets global brightness | Sets opacity (0.25-0.9) |

**Change**: Remove `selection.brightness` global; each stop's lightness derived from its `position`. Wave drives `selection.opacity`.

### 2.3 Circular canvas boundary

Zen clamps drag to circle:
```ts
if (distance > radius) {
  const angle = Math.atan2(pixelY - cy, pixelX - cx);
  pixelX = cx + Math.cos(angle) * radius;
  pixelY = cy + Math.sin(angle) * radius;
}
```

**Change**: Apply circular clamp on dot drag. Visual hint via inner circle border or radial gradient.

### Files Modified
- `react/app.tsx` (PreviewCanvas, ZenDot — drag handlers)
- `react/store.ts` (`setStopPosition` action; remove `setBrightness`, add `setOpacity`)
- `react/types.ts` (`lightness` field per stop, `opacity` global)

### Acceptance Criteria
- Drag dot to canvas edge → lightness 0 (light), wire reflects
- Drag dot to canvas center → lightness 100 (dark), wire reflects
- Drag outside circle → snaps to circle perimeter
- Wave drives `opacity` (0.25-0.9), not brightness

### Estimated Time
4-5 hours

---

## Phase 3 — Color Harmony System (6 options)

**Goal**: Match Zen's `colorHarmonies` array exactly.

### 3.1 Harmony definitions

```ts
export const HARMONIES: Record<ColorHarmony, number[]> = {
  complementary:      [180],
  singleAnalogous:    [310],
  splitComplementary: [150, 210],
  analogous:          [50, 310],
  triadic:            [120, 240],
  floating:           [],  // no auto-positioning
};

export const HARMONY_MAX_DOTS: Record<ColorHarmony, number> = {
  complementary: 2,
  singleAnalogous: 2,
  splitComplementary: 3,
  analogous: 3,
  triadic: 3,
  floating: 8,  // arbitrary cap
};
```

### 3.2 Primary-only drag with harmony auto-position

Current: All stops draggable independently.
Zen: Only primary draggable. Secondaries auto-positioned at harmony angle offsets.

```ts
function recomputeSecondaries(primary, harmony, canvasGeom) {
  const {cx, cy} = canvasGeom;
  const baseDx = primary.position.x - cx;
  const baseDy = primary.position.y - cy;
  const distance = Math.sqrt(baseDx ** 2 + baseDy ** 2);
  const baseDeg = Math.atan2(baseDy, baseDx) * 180 / Math.PI;

  return HARMONIES[harmony].map((offsetDeg, idx) => {
    const newDeg = (baseDeg + offsetDeg) % 360;
    const newRad = newDeg * Math.PI / 180;
    return {
      position: {
        x: cx + distance * Math.cos(newRad),
        y: cy + distance * Math.sin(newRad),
      },
      isPrimary: false,
      // Color computed from position via getColorFromPosition
    };
  });
}
```

### 3.3 Algorithm toggle button

UI element in canvas (Zen has it as `<button>` in actions bar). Click cycles through 6 options.

### 3.4 Add / remove dot buttons

- `+`: adds new stop at random angle within current harmony's max
- `−`: removes last secondary stop (never primary)
- Buttons disabled when at max / min for current harmony

### Files Modified
- `react/types.ts` — `ColorHarmony` enum, `HARMONIES` table, max-dots map
- `react/store.ts` — `setHarmony`, `addStop`, `removeStop`, `recomputeSecondaries` helper
- `react/app.tsx` — `<CanvasActions>` sub-component (algo toggle, +, − buttons)
- `space_create.css` — action buttons styling

### Acceptance Criteria
- All 6 harmony options selectable
- Primary drag rotates secondaries by correct angles
- Algorithm toggle cycles through 6 options
- + / − buttons enforce per-harmony max
- `floating` mode allows independent dot drag (current behavior)

### Estimated Time
5-6 hours

---

## Phase 4 — Dial 16-Step + Perimeter Dots

**Goal**: Match Zen's `texture-handler` exactly.

### 4.1 16-step snap

Restore Zen's exact algorithm:
```ts
let angleDeg = Math.atan2(dy, dx) * 180 / Math.PI + 90;
if (angleDeg < 0) angleDeg += 360;
let texture = angleDeg / 360;
texture = Math.round(texture * 16) / 16;
if (texture === 1) texture = 0;
```

### 4.2 Perimeter 16 dots visualization

16 small dots evenly spaced around dial perimeter. Active step highlighted, others faded.

```html
<div class="tp-dial">
  <div class="tp-dial-ticks">
    {Array.from({length: 16}, (_, i) =>
      <span className={`tp-dial-tick ${i === activeStep ? 'is-active' : ''}`}
            style={{transform: `rotate(${i * 22.5}deg) translateY(-${radius}px)`}} />
    )}
  </div>
  <div className="tp-dial-indicator" style={{transform: `rotate(${activeAngle}deg)`}} />
</div>
```

### 4.3 0° hidden indicator

When texture=0: indicator dot hidden (matches Zen — "no noise" state has no visible indicator).

### Files Modified
- `react/app.tsx` (BottomRow Dial component)
- `space_create.css` (perimeter ticks, active state)

### Acceptance Criteria
- Dial drag snaps to one of 16 positions (22.5° increments)
- 16 perimeter dots visible, only active highlighted
- texture=0 hides indicator dot

### Estimated Time
2-3 hours

---

## Phase 5 — Wave Opacity Mapping + Dynamic Thumb

**Goal**: Match Zen's `opacity` slider exactly.

### 5.1 Wave drives opacity (not brightness)

| Aspect | Current | Zen | Action |
|---|---|---|---|
| Drives | brightness | opacity | Change to opacity |
| Range | 0-100 | 0.25-0.9 | Re-range |

### 5.2 Path interpolation

Zen interpolates per-point between flat path and full sine path:
```ts
function interpolateWavePath(opacity: number): string {
  const normalized = (opacity - 0.25) / (0.9 - 0.25);  // 0..1
  // For each control point, lerp between flat and sine
  ...
}
```

Define both paths once:
```ts
const FLAT_PATH = "M0 38 L460 38";
const SINE_PATH = "M0 38 C28 14 56 14 84 38 ...";  // existing
```

### 5.3 Dynamic thumb size

Zen:
```ts
const normalized = (opacity - 0.25) / (0.9 - 0.25);
thumb.style.height = `${40 + normalized * 15}px`;  // 40-55
thumb.style.width = `${10 + normalized * 15}px`;   // 10-25
```

Apply via CSS variables:
```css
.tp-wave-thumb {
  width: var(--tp-thumb-w, 14px);
  height: var(--tp-thumb-h, 40px);
}
```

### Files Modified
- `react/app.tsx` (BottomRow Wave component)
- `react/types.ts` — opacity field
- `react/store.ts` — `setOpacity`
- `space_create.css` — thumb CSS vars

### Acceptance Criteria
- Wave drag → wire opacity 0.25-0.9
- Path interpolates flat ↔ sine smoothly
- Thumb width/height grow with value (10-25 / 40-55)

### Estimated Time
2-3 hours

---

## Phase 6 — Canvas Tap-to-Add Stop

**Goal**: Match Zen's add-on-tap behavior.

### 6.1 Click on empty canvas → add stop

```ts
function handleCanvasClick(event) {
  if (event.target.closest('.tp-zen-dot')) return;  // dot drag, not add
  if (event.target.closest('.tp-mode-chips, .tp-canvas-actions')) return;  // chip/button click
  if (selection.stops.length >= HARMONY_MAX_DOTS[selection.harmony]) return;  // at max

  const {x, y} = computeRelativePos(event);
  store.addStop({position: {x, y}, isPrimary: false});
}
```

### 6.2 Max enforcement

Current harmony's max determines whether tap adds.

### Files Modified
- `react/app.tsx` (PreviewCanvas onClick)
- `react/store.ts` (addStop with position)

### Acceptance Criteria
- Empty canvas tap → new stop at click position
- At max dots: tap ignored (no visible action)
- Tap on existing dot: drag, not add

### Estimated Time
1-2 hours

---

## Phase 7 — Swatch Preset Expansion (Zen-style)

**Goal**: Match Zen's preset structure with algorithm + position + multi-color.

### 7.1 Preset data structure

```ts
interface ZenPreset {
  id: string;
  lightness: number;             // 0-100
  algorithm: ColorHarmony;
  numDots: number;               // 1-3
  primaryPosition: {x: number; y: number};
  colors: Array<{r: number; g: number; b: number}>;  // numDots entries
}
```

### 7.2 Preset table

Port from Zen `theme-picker.inc:32-109`. Approximately 9-12 presets covering common color schemes.

### 7.3 Apply behavior

```ts
function applyPreset(preset: ZenPreset) {
  store.setHarmony(preset.algorithm);
  store.setStops(rebuildStopsFromPreset(preset));
}
```

### Files Modified
- `maho_common/maho_theme_utils.ts` — ZenPreset type, preset table
- `react/store.ts` — `applyPreset` action
- `react/app.tsx` — SwatchRow uses ZenPreset shape

### Acceptance Criteria
- Click swatch → algorithm + N stops + lightness all applied
- Swatch row scrollable (chevrons functional)
- Visual differentiation between presets clearly visible

### Estimated Time
2-3 hours

---

## Phase 8 — Tests + Manual QA

### 8.1 Unit tests added (~30 new)

**`types.test.ts`** additions:
- Polar mapping: edge → lightness 0, center → lightness 100
- Each of 6 harmony options: secondary positions correct
- 16-step snap rounding boundaries
- Wire round-trip preserves stop positions
- Wire `type: 'gradient'` (not 'zen')
- `c` array RGB output correctness
- `opacity` clamping to [0.25, 0.9]
- `lightness` 0-100 range
- Preset apply: algorithm + stops + lightness all set

**`store.test.ts`** additions:
- `setStopPosition` updates only target stop
- Primary drag triggers `recomputeSecondaries` for non-floating harmonies
- `addStop` enforces harmony max
- `removeStop` never removes primary
- `setHarmony` triggers position recompute
- `setOpacity` clamps to [0.25, 0.9]
- `applyPreset` triggers all sub-actions atomically

### 8.2 Manual QA in standalone

User-driven verification on `http://127.0.0.1:4072/`:
- All 6 harmony options visually correct
- Dial 16 step snap with perimeter dots
- Wave thumb size grows with value
- Tap empty canvas → stop added
- + / − buttons add/remove with proper limits
- Algorithm toggle cycles
- Mode chips → only `scheme` field changes in wire
- Cancel → state reverts
- Done → wire commit fires

### 8.3 Native parity (deferred, separate session)

Once Chromium build unblocks:
- Real native sidebar color reflects wire changes
- Preview / commit / cancel IPC reach native correctly

### Files Modified
- `react/__tests__/types.test.ts`
- `react/__tests__/store.test.ts`
- `.sisyphus/plans/theme-picker-zen-parity-qa-log.md` (new, written during QA)

### Acceptance Criteria
- All ~85 tests green (~55 existing + ~30 new)
- Manual QA log written with each item ✓ or noted issue
- No regressions in existing 55 tests

### Estimated Time
3-4 hours

---

## File Change Summary

### Modified (10)
- `react/types.ts` — wire schema, polar mapping, harmony enum, lightness, opacity
- `react/store.ts` — `setStopPosition`, `setHarmony`, `setOpacity`, `addStop`, `removeStop`, `applyPreset`
- `react/app.tsx` — PreviewCanvas (polar drag, primary-only), BottomRow (16-step dial, perimeter dots, dynamic thumb), CanvasActions (+, −, algo toggle), SwatchRow (Zen presets), tap-to-add
- `space_create.css` — perimeter dots, dynamic thumb CSS vars, harmony toggle button styles
- `react/__tests__/types.test.ts` — new ~20 cases
- `react/__tests__/store.test.ts` — new ~10 cases
- `standalone/maho_space_create.mojom-webui.js` — stub seed JSON aligned with Zen wire
- `maho_common/maho_theme_utils.ts` — Zen-style preset data structure
- `vitest.config.ts` — verify (no change expected)
- `space_create.html` — verify (no change expected)

### New
- `.sisyphus/plans/theme-picker-zen-parity-qa-log.md` (during Phase 8)

### Untouched
- All Mojom IDL files (opaque `theme_json` string preserved)
- All native C++ files (parser already supports Zen-format `c` arrays + HSB)
- All BUILD.gn files (no GN edits)

---

## Phase Order, Dependencies, and Effort

| Phase | Priority | Effort | Depends On | Parallel Possible |
|---|---|---|---|---|
| 1. Wire format | **CRITICAL** | 3-4h | — | — |
| 2. Polar + per-stop lightness | **CRITICAL** | 4-5h | 1 | — |
| 3. Color harmony 6 options | HIGH | 5-6h | 2 | — |
| 4. Dial 16-step + perimeter | MEDIUM | 2-3h | 1 | with 5 |
| 5. Wave opacity + thumb | MEDIUM | 2-3h | 1 | with 4 |
| 6. Tap-to-add | MEDIUM | 1-2h | 3 | — |
| 7. Swatch preset expansion | LOW | 2-3h | 3 | with 6 |
| 8. Tests + QA | HIGH | 3-4h | 1-7 | — |

**Total**: 22-30 hours (3-4 full working days)

**Recommended execution**:
1. Phase 1 + 2 sequentially (foundation)
2. Phase 3 (large UX shift, get user feedback before continuing)
3. Phases 4 + 5 in parallel (independent components)
4. Phases 6 + 7 in parallel (final UX polish)
5. Phase 8 last (comprehensive verification)

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| RGB ↔ HSB conversion precision loss | Medium | Low | Use existing `hsbToRgb` / `rgbToHsb` in `maho_theme_utils.ts` |
| Polar mapping changes UX dramatically | High | Medium | User verification after Phase 2 before continuing |
| Stop position changes break existing fns | High | High | DI-style helpers + per-phase tests; no parallel work on overlapping files |
| 16-step dial feels too coarse | Medium | Low | Strong perimeter-dot visual feedback compensates |
| Test suite becomes unwieldy | Medium | Low | Split tests into focused describe blocks per phase |
| Native parser rejects new wire | Low | High | Parser already supports `c` array + HSB; verified by reading source |
| `floating` harmony mode UX confusing | Medium | Medium | Document behavior; provide visual hint when in floating mode |

---

## Open Questions (require user decision before implementation)

1. **`lightness` vs `brightness` mapping**:
   Zen stores `lightness` 0-100 per stop (canvas position derived). Maho native parser expects `brightness` 0-1.
   **Question**: Convert `lightness / 100 = brightness`? Or use a separate non-linear curve?
   **Default if not answered**: linear `brightness = lightness / 100`.

2. **`opacity` platform range**:
   Zen uses `0.25-0.9` on most platforms, `0.30-0.9` on macOS.
   **Question**: Apply platform-specific range, or use single `0.25-0.9` everywhere?
   **Default if not answered**: single `0.25-0.9`.

3. **Swatch row count**:
   Zen has many presets (~12-20). Maho currently has 9.
   **Question**: Expand to Zen's full count, or keep 9?
   **Default if not answered**: keep 9 (Phase 7 ports their Zen-style structure but data set stays at 9).

4. **`floating` harmony mode entry**:
   When user switches AWAY from floating to another harmony, secondary positions are auto-recomputed. When user switches INTO floating, what positions do secondaries get?
   **Question**: Keep current positions? Or reset to harmony-default?
   **Default if not answered**: keep current positions (transition into floating preserves layout).

5. **Algorithm toggle button placement**:
   Zen has it as a small button inside canvas (top-right area).
   **Question**: Same position in Maho, or different location (e.g., near swatches)?
   **Default if not answered**: top-right of canvas (Zen position).

---

## Acceptance Checklist for Plan Completion

Each must be ✓ before plan is considered fully implemented:

- [ ] User reviewed and approved this plan
- [ ] Phase 1: Wire dump byte-for-byte equal to a hand-crafted Zen sample
- [ ] Phase 2: Edge → lightness 0, center → lightness 100; circular drag clamp works
- [ ] Phase 3: All 6 harmony options selectable; secondaries rotate correctly on primary drag
- [ ] Phase 4: 16-step snap visible; perimeter dots show active step
- [ ] Phase 5: Wave thumb size grows with value; opacity 0.25-0.9
- [ ] Phase 6: Empty canvas tap adds stop; max enforced
- [ ] Phase 7: Preset click sets algorithm + stops + lightness atomically
- [ ] Phase 8: ~85 tests green, manual QA log complete
- [ ] No `maho_space_config` files modified
- [ ] No Mojom IDL changes
- [ ] No GN edits
- [ ] Open Questions Q1–Q5 either resolved or explicitly deferred with default values

---

## References

- Visual GT: `maho/reference_screenshots/create-theme.png`
- Zen Browser source: `https://github.com/zen-browser/desktop/tree/cd1616d1c00e875dfb5e64289f858f59e0b4e6ee`
  - `src/zen/spaces/ZenGradientGenerator.mjs`
  - `src/zen/spaces/zen-gradient-generator.css`
  - `src/browser/base/content/zen-panels/theme-picker.inc`
- Maho native parser: `maho-chromium/browser/ui/theme/maho_space_theme_state.cc:171-235`
- Maho color utils: `maho-chromium/browser/resources/maho_common/maho_theme_utils.ts`
- ADR: `.sisyphus/plans/theme-picker-architecture-decision.md`
- Previous implementation plan (superseded): `.sisyphus/plans/theme-picker-implementation-plan-2026-05-22.md`
