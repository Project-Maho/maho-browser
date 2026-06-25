# Theme Picker — Implementation Plan (2026-05-22)

**Architecture**: Per `.sisyphus/plans/theme-picker-architecture-decision.md` — WebUI at `chrome://maho-space-create`, hosted by `MahoSpaceThemePickerDialog`. **Permanent decision.** No `maho_space_config` migration.

**Goal**: Bring `chrome://maho-space-create` to functional + visual parity with `maho/reference_screenshots/create-theme.png`. Address Oracle-identified wire-format defect, hydration drift, visual deltas, and cancel race.

---

## Out of scope (explicit, do not violate)

1. ❌ `maho_space_config` integration — rejected per ADR
2. ❌ Native Views theme picker resurrection — deleted permanently
3. ❌ Mojom schema changes — `theme_json` stays opaque string
4. ❌ GN edits without explicit user approval (per `AGENTS.md`)
5. ❌ Integration tests, end-to-end tests, browser tests (user instruction: "유닛 테스트만, 통합테스트 금지")
6. ❌ Auto-screenshots / auto-cliclick (user instruction: "캡처 필요시 사용자에게 요청")
7. ❌ Production canvas-tap behavior change without product decision (Open Question Q2)

---

## Phase 0 — Verify production routing

**Status**: Already verified in this session.

- ✅ Live path: `MahoSidebarCreateSpaceView::OnThemePressed` → `MahoSpaceThemePickerDialog::Open` → `chrome://maho-space-create`
- ✅ React app at `browser/resources/maho_space_create/react/app.tsx` is the live UI
- ✅ Standalone dev harness at port 4072 confirmed working
- ✅ Beaded-arc visual is intentional (sine wave with `stroke-dasharray: 1.5 10` + `stroke-width: 10`)

**No work needed.**

---

## Phase 1 — Wire format adapter (CRITICAL — blocks everything else)

**Problem (Oracle blocking #1)**: Current `serializeThemeSelection` produces:
```json
{"mode":"sparkle","preset_index":0,"brightness":54}
```
Native parser at `maho-chromium/browser/ui/theme/maho_space_theme_state.cc:171` (`ExtractThemeDataFromThemeDict`) requires:
```json
{"type":"solid","color":{"hue":0.58,"saturation":0.8,"brightness":0.9,"grain":0}}
```
Native silently rejects current payloads → `previewTheme` calls are no-ops → user sees no live theme preview during selection.

### Files

- `maho-chromium/browser/resources/maho_space_create/react/types.ts` (modify)
- `maho-chromium/browser/resources/maho_space_create/react/store.ts` (modify minimally)
- `maho-chromium/browser/resources/maho_space_create/standalone/maho_space_create.mojom-webui.js` (modify SPARKLE_DEFAULT to valid wire shape)

### Concrete changes

**1.1** `types.ts` — Add wire schema types and adapters:

```typescript
// Native-accepted wire schema (mirrors SpaceTheme::Solid)
export interface SerializedSpaceColor {
  hue: number;        // [0, 1]
  saturation: number; // [0, 1]
  brightness: number; // [0, 1]
  grain: number;      // [0, 1]
}

export interface SerializedTheme {
  type: 'solid';
  color: SerializedSpaceColor;
  schemaVersion?: number;
}

// Frozen preset table — UI swatch index → HSB triple
// Colors derived from current .tp-swatch--N CSS background colors,
// converted to HSB once and committed.
const PRESETS: ReadonlyArray<{hue: number; saturation: number; brightness: number}> = Object.freeze([
  {hue: 0.094, saturation: 0.040, brightness: 0.957}, // 0 cream  #f4f0eb
  {hue: 0.945, saturation: 0.171, brightness: 0.906}, // 1 pink   #e7bfd0
  {hue: 0.756, saturation: 0.260, brightness: 0.678}, // 2 purple #9a81ad
  {hue: 0.969, saturation: 0.539, brightness: 0.827}, // 3 red    #d3616f
  {hue: 0.078, saturation: 0.622, brightness: 0.941}, // 4 orange #f0a35b
  {hue: 0.139, saturation: 0.673, brightness: 0.937}, // 5 yellow #efce4e
  {hue: 0.339, saturation: 0.530, brightness: 0.773}, // 6 green  #5cc56d
  {hue: 0.572, saturation: 0.546, brightness: 1.0  }, // 7 blue   #74b9ff
  {hue: 0.661, saturation: 0.553, brightness: 0.871}, // 8 indigo #6478de
] as const);

export function selectionToWireTheme(sel: ThemeSelection): SerializedTheme {
  const preset = PRESETS[normalizePresetIndex(sel.preset_index)];
  return {
    type: 'solid',
    color: {
      hue: preset.hue,
      saturation: preset.saturation,
      brightness: clampBrightness(sel.brightness) / 100,
      grain: 0,
    },
  };
}

export function wireThemeToSelection(json: string | undefined): ThemePickerState {
  if (!json) return createInitialState();
  try {
    const parsed = JSON.parse(json) as Partial<SerializedTheme>;
    if (!parsed || parsed.type !== 'solid' || !parsed.color) {
      return createInitialState();
    }
    const color = parsed.color;
    const hue = typeof color.hue === 'number' ? color.hue : 0;
    const presetIndex = nearestPresetIndex(hue);
    const brightness = typeof color.brightness === 'number'
      ? clampBrightness(Math.round(color.brightness * 100))
      : 0;
    return {
      selection: {
        mode: 'sparkle',
        preset_index: presetIndex,
        brightness,
      },
      loading: false,
    };
  } catch {
    return createInitialState();
  }
}

function nearestPresetIndex(hue: number): number {
  let best = 0;
  let bestDelta = Infinity;
  for (let i = 0; i < PRESETS.length; i++) {
    const delta = Math.min(
      Math.abs(PRESETS[i].hue - hue),
      Math.abs(PRESETS[i].hue - hue + 1),
      Math.abs(PRESETS[i].hue - hue - 1),
    );
    if (delta < bestDelta) {
      best = i;
      bestDelta = delta;
    }
  }
  return best;
}
```

**1.2** Replace `serializeThemeSelection`:
```typescript
export function serializeThemeSelection(sel: ThemeSelection): string {
  return JSON.stringify(selectionToWireTheme(sel));
}
```
Old `parseThemeSelection` becomes a thin wrapper that calls `wireThemeToSelection(json).selection`.

**1.3** `store.ts` — `loadInitial` becomes:
```typescript
loadInitial(json: string): void {
  const next = wireThemeToSelection(json);
  this.patch(draft => {
    draft.selection = next.selection;
    draft.loading = false;
  }, {emitPreview: false});
}
```

**1.4** Standalone stub — update `SPARKLE_DEFAULT`:
```javascript
const SPARKLE_DEFAULT = JSON.stringify({
  type: 'solid',
  color: {hue: 0.094, saturation: 0.040, brightness: 0, grain: 0},
});
```

### Acceptance
- `selectionToWireTheme(default)` produces native-valid JSON parsable by `ExtractThemeDataFromThemeDict`'s rules
- `wireThemeToSelection` round-trips a valid native payload back to a `ThemePickerState`
- Invalid JSON falls back to default without throwing
- Standalone init no longer triggers parse fallback (verify console has no warnings)

### Tests (Phase 5)
Listed in Phase 5 below.

---

## Phase 2 — Hydration roundtrip

**Problem**: `Page.Initialize(initial)` from `maho_space_create_page_handler.cc:31` sends a real `ThemeSelectionWire` with `theme_json` containing the current space's theme. Today it's parsed against the wrong schema → silently falls back to default → user opens picker for an existing space and sees default selection regardless of actual theme.

### Changes
After Phase 1 lands, `loadInitial` calling `wireThemeToSelection` automatically fixes hydration. No additional code, but **must verify** in standalone with seeded native-shaped JSON in stub.

### Acceptance
- Stub seeds `{type:'solid', color:{hue:0.572, saturation:0.55, brightness:0.5, grain:0}}` → picker mounts with **swatch 7 (light blue)** selected and brightness slider near middle
- Console shows no parse fallback

### Files
None additional beyond Phase 1.

### Tests
- `wireThemeToSelection('{"type":"solid","color":{"hue":0.572,"saturation":0.55,"brightness":0.5,"grain":0}}')` → preset_index = 7
- `wireThemeToSelection('{}')` → default state
- `wireThemeToSelection('garbage')` → default state, no throw

---

## Phase 3 — Visual GT parity

**Reference**: `maho/reference_screenshots/create-theme.png` (right panel only — left panel is the native sidebar form, separate concern).

### Defects vs GT (precise list)

| # | Element | Current | GT Reference | Fix |
|---|---|---|---|---|
| 3.1 | Mode chip selected (`tp-chip.is-selected`) | Circle (`border-radius: 999px`) + blue glow ring (`box-shadow`) + blue tint | **Rounded-rect tile** (~12-14px radius), darker fill, no glow ring, more square aspect | `border-radius: 12px`; remove `box-shadow`; change background to `var(--tp-surface-strong)` |
| 3.2 | Inactive chip text/icon | Same opacity as selected | Lower opacity (~60%) | Reduce icon color saturation on non-selected |
| 3.3 | Canvas glow blobs (`tp-glow--top`, `tp-glow--bottom`) | Two large blue radial glows visible | **Not present** in GT | Remove `tp-glow` divs OR set `display: none` |
| 3.4 | Default brightness | 54 (mid-range, dial indicator visible) | 0 (no indicator visible at idle) | Change `createDefaultSelection().brightness` to 0 |
| 3.5 | `tp-dial__indicator.is-level-0` | Indicator shown (rotated -138°) | Indicator hidden | Add `opacity: 0` to `is-level-0` rule |
| 3.6 | `tp-brightness-row` — `−` and `+` | Both at full opacity | `−` slightly dimmer (suggests floor state) | Style `−` button opacity based on `brightness <= 0` (optional polish) |
| 3.7 | Wave element (`tp-wave__path`) | Beaded-dash sine, `stroke-width: 10` (intentional per code analysis) | Same beaded look in GT | **No change needed** — false alarm in earlier analysis |
| 3.8 | "Tap to pick a color" text | Centered, large (~21px) | Same | ✓ matches |

### Files
- `maho-chromium/browser/resources/maho_space_create/space_create.css` (modify)
- `maho-chromium/browser/resources/maho_space_create/react/types.ts` (modify default brightness — already in Phase 1.4 stub change but confirm types.ts line 24 too)

### Concrete changes

**3.1 + 3.2** Replace `.tp-chip.is-selected` block (CSS lines 123-128):
```css
.tp-chip {
  /* keep existing 52x52 sizing, BUT change shape */
  border-radius: 12px;  /* was 999px */
}

.tp-chip.is-selected {
  background: var(--tp-surface-strong);  /* darker fill, not blue tint */
  border-color: rgba(255,255,255,0.10);   /* subtle border */
  color: var(--tp-text-primary);
  box-shadow: none;                       /* was outer blue glow ring */
}

.tp-chip:not(.is-selected) {
  color: var(--tp-text-secondary);  /* dimmer */
}
```

**3.3** Comment out / remove `tp-glow` JSX (lines 95-96 of `app.tsx`) OR add CSS:
```css
.tp-glow { display: none; }
```
Prefer JSX removal for clean DOM.

**3.4** `types.ts` line 24:
```typescript
export function createDefaultSelection(): ThemeSelection {
  return {
    mode: 'sparkle',
    preset_index: 0,
    brightness: 0,  // was 54
  };
}
```

**3.5** `space_create.css` `.tp-dial__indicator.is-level-0` block (line 423):
```css
.tp-dial__indicator.is-level-0 {
  transform: rotate(-138deg);
  opacity: 0;  /* hide indicator at brightness=0 */
}
```

**3.6** (optional) `.tp-brightness-btn:first-child` — set lower opacity if at floor. Skip for now.

### Acceptance
- Standalone screenshot vs GT side-by-side shows: rounded-rect selected chip, no glow blobs, dial empty at default, dimmer inactive chips
- No regressions in Mode chip click feedback or selected state visibility

### Tests
- Visual diff verified manually by user in standalone (per "캡처 필요시 사용자에게 요청")

---

## Phase 4 — Cancel race + interaction polish

**Problem**: `cancelTheme()` in `store.ts:95` calls `handler.cancelTheme()` but does not clear the 50ms debounce timer first. A `previewTheme` scheduled <50ms before cancel can fire after cancel resolves, leaving stale preview state on native side.

### Changes

**4.1** `store.ts` `cancelTheme`:
```typescript
async cancelTheme(): Promise<void> {
  this.clearPreviewTimer();        // ← add this BEFORE awaiting
  await this.handler.cancelTheme();
}
```

**4.2** Verify native side clears preview override on cancel:
- Read `maho_space_create_page_handler.cc` `CancelTheme()` method
- Confirm it calls `MahoSpaceThemeState::ClearPreviewOverride()` or equivalent
- If not, document as separate native fix in `.sisyphus/plans/theme-picker-native-cancel-fix.md` and stop (do not touch native here)

### Files
- `maho-chromium/browser/resources/maho_space_create/react/store.ts` (modify)
- (read-only verify) `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

### Acceptance
- After Phase 4 lands, debounced preview never fires after a cancel call (verifiable via test mocking timers)

### Tests
- Phase 5 includes timer race test

---

## Phase 5 — Unit tests (vitest, TS pure-fn level)

**Constraint**: User said "유닛 테스트만, 통합테스트 금지". These are pure TypeScript unit tests with mocked Mojo handler — no DOM, no real IPC.

### Test runner setup verification

- Confirm `website/node_modules/vitest` exists (already used by other Maho frontend code)
- If no vitest config in `maho_space_create/`, add minimal `vitest.config.ts`:
```typescript
import {defineConfig} from 'vitest/config';
export default defineConfig({
  test: {
    environment: 'node',  // pure functions, no DOM needed
    include: ['react/__tests__/**/*.test.ts'],
  },
});
```

### Files (new)
- `maho-chromium/browser/resources/maho_space_create/react/__tests__/types.test.ts`
- `maho-chromium/browser/resources/maho_space_create/react/__tests__/store.test.ts`
- `maho-chromium/browser/resources/maho_space_create/vitest.config.ts` (if needed)

### `types.test.ts` contracts

```typescript
describe('selectionToWireTheme', () => {
  it('produces native-shaped JSON', () => {
    const out = selectionToWireTheme(createDefaultSelection());
    expect(out.type).toBe('solid');
    expect(out.color).toMatchObject({
      hue: expect.any(Number),
      saturation: expect.any(Number),
      brightness: 0,
      grain: 0,
    });
  });
  it('clamps brightness to [0,1]', () => {
    const out = selectionToWireTheme({mode: 'sparkle', preset_index: 0, brightness: 250});
    expect(out.color.brightness).toBeLessThanOrEqual(1);
  });
  it('normalizes preset_index modulo 9', () => {
    expect(selectionToWireTheme({...createDefaultSelection(), preset_index: 11}).color.hue)
      .toBe(selectionToWireTheme({...createDefaultSelection(), preset_index: 2}).color.hue);
  });
});

describe('wireThemeToSelection', () => {
  it('round-trips a valid solid theme to nearest preset', () => {
    const wire = JSON.stringify({type:'solid', color:{hue:0.572, saturation:0.55, brightness:0.5, grain:0}});
    expect(wireThemeToSelection(wire).selection.preset_index).toBe(7); // blue
  });
  it('falls back on garbage', () => {
    expect(wireThemeToSelection('garbage').selection).toEqual(createDefaultSelection());
  });
  it('falls back on missing type', () => {
    expect(wireThemeToSelection('{"color":{}}').selection).toEqual(createDefaultSelection());
  });
  it('falls back on undefined', () => {
    expect(wireThemeToSelection(undefined).selection).toEqual(createDefaultSelection());
  });
});

describe('PRESETS table', () => {
  it('has exactly 9 entries', () => {
    expect(PRESETS).toHaveLength(9);
  });
  it('all hue/saturation/brightness in [0,1]', () => {
    for (const p of PRESETS) {
      expect(p.hue).toBeGreaterThanOrEqual(0);
      expect(p.hue).toBeLessThanOrEqual(1);
      expect(p.saturation).toBeGreaterThanOrEqual(0);
      expect(p.saturation).toBeLessThanOrEqual(1);
      expect(p.brightness).toBeGreaterThanOrEqual(0);
      expect(p.brightness).toBeLessThanOrEqual(1);
    }
  });
});
```

### `store.test.ts` contracts

Requires small DI refactor: make `ThemePickerStore` accept optional `{handler, router}` constructor args so tests can inject fakes. (This is a **behavior-preserving** change; production code calls `new ThemePickerStore()` with no args, falls back to real Mojo factory.)

```typescript
function createFakeHandler() {
  return {
    previewTheme: vi.fn().mockResolvedValue(undefined),
    commitTheme: vi.fn().mockResolvedValue(undefined),
    cancelTheme: vi.fn().mockResolvedValue(undefined),
  };
}
function createFakeRouter() {
  const listeners: Array<(w: ThemeSelectionWire) => void> = [];
  return {
    initialize: {addListener: (l) => listeners.push(l)},
    fire: (w) => listeners.forEach(l => l(w)),
  };
}

describe('ThemePickerStore', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('setPreset + setBrightness debounces preview to single call', () => {
    const handler = createFakeHandler();
    const store = new ThemePickerStore({handler, router: createFakeRouter()});
    store.setPreset(3);
    store.setBrightness(60);
    vi.advanceTimersByTime(50);
    expect(handler.previewTheme).toHaveBeenCalledTimes(1);
  });

  it('cancelTheme clears pending preview before resolving', async () => {
    const handler = createFakeHandler();
    const store = new ThemePickerStore({handler, router: createFakeRouter()});
    store.setBrightness(60);          // schedules preview at +50ms
    await store.cancelTheme();        // should clear timer
    vi.advanceTimersByTime(100);
    expect(handler.previewTheme).not.toHaveBeenCalled();
    expect(handler.cancelTheme).toHaveBeenCalled();
  });

  it('loadInitial does not emit preview', () => {
    const handler = createFakeHandler();
    const router = createFakeRouter();
    const store = new ThemePickerStore({handler, router});
    router.fire({theme_json: JSON.stringify({type:'solid', color:{hue:0.5, saturation:0.5, brightness:0.5, grain:0}})});
    vi.advanceTimersByTime(100);
    expect(handler.previewTheme).not.toHaveBeenCalled();
    expect(store.getSnapshot().loading).toBe(false);
  });

  it('every state mutation produces a parseable native wire payload', () => {
    const handler = createFakeHandler();
    const store = new ThemePickerStore({handler, router: createFakeRouter()});
    store.setMode('moon');
    store.setPreset(5);
    store.setBrightness(80);
    vi.advanceTimersByTime(50);
    const arg = handler.previewTheme.mock.calls.at(-1)![0];
    const parsed = JSON.parse(arg.theme_json);
    expect(parsed.type).toBe('solid');
    expect(parsed.color).toMatchObject({hue: expect.any(Number), saturation: expect.any(Number), brightness: expect.any(Number), grain: expect.any(Number)});
  });
});
```

### Acceptance
- All tests pass when run via `cd maho-chromium/browser/resources/maho_space_create && bunx vitest run`
- Tests run in <2s (pure functions only)

---

## Phase 6 — Manual QA in standalone

User-driven verification using existing standalone harness on port 4072.

### QA checklist (user runs)

After Phases 1-4 land:
- [ ] Open `http://127.0.0.1:4072/`
- [ ] Verify: dial indicator hidden at idle (default brightness=0)
- [ ] Verify: mode chip selected = rounded-rect dark fill, no glow
- [ ] Verify: canvas has no blue glow blobs
- [ ] Verify: clicking swatch 7 (blue) updates selection ring
- [ ] Verify: clicking `+` brightness multiple times shows dial indicator at increasing rotations
- [ ] Verify: clicking `−` from 0 stays at 0 (clamped)
- [ ] Verify: console shows `previewTheme` called with `{type:'solid', color:{...}}` shape
- [ ] Verify: clicking Done shows commit alert with native-shaped JSON
- [ ] Verify: clicking Cancel doesn't fire late preview (fast click cancel sequence)

### File
`.sisyphus/plans/theme-picker-implementation-qa-log.md` (new, written during QA)

---

## Phase 7 — Deferred (not this session)

These require either user product decisions or native code work:

### 7.1 Mode chip semantics (Sparkle/Sun/Moon)
**Open question**: Does Sparkle/Sun/Moon switch `SpaceTheme` variant (`solid`/`gradient`/`zen`), or just bias brightness/saturation curves on the same `solid` payload?
- Until decided, mode is **UI-only** flag. Wire payload always `type: 'solid'`.
- Documented as Open Question Q1 below.

### 7.2 Canvas-tap UX
**Open question**: "Tap to pick a color for this space" — current implementation cycles preset_index by 1. Reference design implies real coordinate-based color picking (hue from x, saturation from y) or opening a system color picker.
- Until decided, tap continues to cycle presets.
- Documented as Open Question Q2 below.

### 7.3 Sine-wave slider semantics
**Open question**: Currently `tp-wave` is a static decorative SVG. Should it be a draggable slider (e.g., `grain` 0-1)?
- Until decided, wave is decoration-only.
- Documented as Open Question Q3 below.

### 7.4 Dial drag interaction
**Open question**: Currently dial is click-only (9 discrete brightness levels). Reference design suggests continuous rotary control.
- Until decided, dial click steps brightness by 12.
- Documented as Open Question Q4 below.

### 7.5 Native cancel preview-clear verification
- Read-only check of `maho_space_create_page_handler.cc::CancelTheme()` for `ClearPreviewOverride` call
- If missing: separate plan, separate session

---

## Open questions (require user / product decision)

**Q1**: Mode chip → SpaceTheme variant mapping?
- Option A: All three modes use `type: 'solid'` with different brightness/saturation curves (UI-only mode)
- Option B: Sparkle = `solid`, Sun = `gradient` (light), Moon = `gradient` (dark)
- Option C: Sparkle = `zen`, Sun/Moon = `solid` with light/dark brightness presets
- **Default until answered**: Option A (UI-only)

**Q2**: Canvas-tap behavior?
- Option A: Cycle preset (current hack — keeps prompt text honest if reworded)
- Option B: Map (x,y) to (hue, saturation) — implement real 2D color picker
- Option C: Open system OS color picker
- **Default until answered**: Option A (no work)

**Q3**: Sine-wave slider role?
- Option A: Decoration only (current)
- Option B: Maps to `grain` 0-1
- Option C: Maps to gradient angle
- **Default until answered**: Option A

**Q4**: Dial role?
- Option A: Discrete 9-step brightness (current)
- Option B: Continuous rotary brightness 0-100
- Option C: Continuous rotary hue 0-360
- **Default until answered**: Option A

---

## Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Native parser has additional required fields not yet documented | Low | Medium | Phase 1 acceptance includes manual verification: send a Phase 1 wire payload to native and confirm no DCHECK / log warnings |
| Vitest runner not configured in `maho_space_create/` | Medium | Low | Phase 5 includes config bootstrap; `website/node_modules/vitest` already exists |
| Standalone stub diverges from real Mojo behavior | Low | Medium | Stub mirrors mojom.d.ts shape; tests cover both standalone and store-DI fakes |
| Visual changes break native-side embed (dialog widget sizing) | Low | Medium | Standalone uses same CSS; if dialog shows differently in dialog widget, it's a wrapper problem not picker problem |
| Cancel race fix breaks an existing test | Very low | Low | No existing tests touch the timer ordering |

---

## Implementation order summary

```
Phase 1 (wire format)        — 3-4h, blocks all else
Phase 2 (hydration)          — falls out of Phase 1, ~30min verify
Phase 3 (visual)             — 1-2h, after Phase 1 so previews actually work
Phase 4 (cancel race)        — 30min
Phase 5 (tests)              — 2-3h
Phase 6 (manual QA)          — user time, ~30min
Phase 7 (deferred)           — separate sessions, gated on Open Questions
```

**Total estimate**: ~7-9h of actual coding + iteration.

---

## Files touched (full list)

### Modified
- `maho-chromium/browser/resources/maho_space_create/react/types.ts`
- `maho-chromium/browser/resources/maho_space_create/react/store.ts`
- `maho-chromium/browser/resources/maho_space_create/react/app.tsx`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`
- `maho-chromium/browser/resources/maho_space_create/standalone/maho_space_create.mojom-webui.js`

### New
- `maho-chromium/browser/resources/maho_space_create/react/__tests__/types.test.ts`
- `maho-chromium/browser/resources/maho_space_create/react/__tests__/store.test.ts`
- `maho-chromium/browser/resources/maho_space_create/vitest.config.ts` (if no existing config)
- `.sisyphus/plans/theme-picker-implementation-qa-log.md` (during Phase 6)

### Read-only verification
- `maho-chromium/browser/ui/theme/maho_space_theme_state.cc` (parser shape)
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc` (cancel behavior)

### Untouched (per ADR / scope)
- All Mojom files (theme_json stays opaque)
- `MahoSpaceThemePickerDialog` (working as designed)
- `maho_space_config/` (forbidden destination)
- Native Views (deleted intentionally)

---

## Acceptance checklist for plan completion

- [ ] User reviews and approves this plan
- [ ] Phase 1 lands: native parser accepts payloads (verify via standalone console)
- [ ] Phase 2 verified: existing space themes hydrate correctly
- [ ] Phase 3 verified: GT visual diff acceptable to user
- [ ] Phase 4 verified: cancel race covered
- [ ] Phase 5: all tests green, ≤2s
- [ ] Phase 6 QA log written
- [ ] Open questions Q1-Q4 either answered or explicitly deferred
- [ ] No `maho_space_config` files touched
- [ ] No mojom changes
- [ ] No GN edits
