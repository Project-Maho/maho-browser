# Space Create — shadcn Pattern Migration Plan (Pilot)

**Surface:** `maho_space_create` (single surface, pilot-only)
**Status:** Final, ready for execution
**Owner:** Sisyphus
**Created:** 2026-05-20
**Revised:** 2026-05-20 — re-grounded against the real `maho_space_create` source tree.

---

## Executive Decision

Adopt **shadcn-as-pattern** in a limited sense — component-per-file decomposition, clearer ownership boundaries, optional `React.forwardRef` where justified, and explicit local variant/state handling — inside the existing `maho_space_create` WebUI surface, **without** importing any shadcn dependency or styling system, and **without** changing the existing class model.

Binding constraints, restated and non-negotiable:

1. The pilot covers exactly one surface: `maho-chromium/browser/resources/maho_space_create/`. No second surface is in scope, planned, or implied by this work.
2. **Tailwind is out.** No utility-class migration, no `tailwind.config.*`, no `@apply`.
3. **No new dependencies.** Specifically excluded: `class-variance-authority` (`cva`), `clsx`, any `cn.ts` / `cn()` helper, `tailwind-merge`, shadcn's CLI scaffolding, and any token alias layer.
4. The existing `--sc-*` CSS custom properties and `sc-*` classes inside `space_create.css` remain the **authoritative** styling system for this surface (31 tokens / 125 selectors at HEAD). No HSL semantic tokens, no token alias layer, no theme-bridge file.
5. `maho-chromium/browser/resources/maho_common/react/primitives/` remains the **canonical** location for truly generic shared primitives. The pilot is permitted, and expected, to conclude with **zero new shared primitives** if no extraction is justified by reuse evidence.
6. No package restructuring, no `BUILD.gn` topology changes beyond appending new local component files to the existing explicit `inputs` list, no `bundle_guard` budget changes, no `.mojom` changes, no changes to `bundle_react.mjs`'s bundling strategy.

What "shadcn-as-pattern" means here, scoped to this pilot:

- One component per file under a new local folder `maho_space_create/react/components/`, giving each unit a clearer ownership boundary.
- Components MAY use `React.forwardRef` where it is justified (a single measurable / focusable DOM element a parent could plausibly need a ref to). It is **optional**, not a gate. Where used, components accept `...rest` HTML props and emit `data-*` attributes for variant state.
- Slot-based composition is **not** required. Composition is done by passing children where natural.
- Variant and local state are handled explicitly inside each component, not via a variant DSL.
- The existing class model is preserved. Class names are sourced from the authoritative `sc-*` set in `space_create.css`. Joining is the writer's choice: simple template strings or array `.join(' ')` are both acceptable. The existing `joinClassNames` helper at `maho_common/react/primitives/join_class_names.ts` is **available** to the pilot as an optional shared utility, **not** required for any component to count as "done."
- Variant selection uses a small inline lookup object (`const VARIANT_CLASS = { active: "sc-mode-tab active", idle: "sc-mode-tab" } as const`), **not** `cva`.

This pilot is structural and stylistic-internal only. The user-visible UI, tokens, classes, Mojo surface, and bundle output are unchanged.

---

## Phase 0 — Baseline Capture

**Goal:** Lock the "before" state so any drift introduced by the refactor is detectable.

Tasks:

1. Capture the current rendered DOM and classnames from `space_create.html` + `react/app.tsx` as a reference snapshot file at `.sisyphus/artifacts/space-create-shadcn-pattern/baseline-dom.md`. Record:
   - Element tree of the create-space surface in its idle state, with the name input focused, with a color swatch active, and with submit pending (`isCreating`).
   - Full class-name sets per element. The `sc-*` classes touched by the React tree at HEAD include (non-exhaustive but anchoring): `sc-left`, `sc-illustration`, `sc-illustration-orb`, `sc-title`, `sc-subtitle`, `sc-card-row`, `sc-card-row-label`, `sc-name-input`, `sc-icon-row`, `sc-icon-row-icon`, `sc-icon-row-text`, `sc-icon-row-title`, `sc-icon-row-desc`, `sc-pill-select`, `sc-pill-chevron`, `sc-theme-dot`, `sc-spacer`, `sc-create-btn`, `sc-cancel-btn`, `sc-right`, `sc-right-shell`, `sc-preview-controls`, `sc-right-layout`, `sc-right-top-cluster`, `sc-right-top-halo`, `sc-mode-tabs`, `sc-right-top-chips`, `sc-mode-tab` (+ `active`), `sc-mode-tab-icon`, `sc-preview-stage`, `sc-right-canvas-shell`, `sc-preview-frame`, `sc-preview-canvas`, `sc-stage-glow`, `sc-stage-dots`, `sc-preview-noise`, `sc-preview-instruction`, `sc-preview-badge`, `sc-preview-label`, `sc-right-mid-controls`, `sc-luminosity-btn`, `sc-luminosity-track-shell`, `sc-luminosity-range`, `sc-swatch-panel`, `sc-right-swatch-rail`, `sc-swatch-row`, `sc-swatch-chevron`, `sc-color-palette`, `sc-color-swatch` (+ `active`), `sc-right-bottom-controls`, `sc-wave-shell`, `sc-wave-svg`, `sc-wave-path`, `sc-wave-path-back`, `sc-wave-path-front`, `sc-dial-shell`, `sc-dial-rings`, `sc-dial-ring`, `sc-dial-ring-outer`, `sc-dial-ring-middle`, `sc-dial-ring-inner`, `sc-dial-core`, `sc-grain-hook`.
   - Computed values for the `--sc-*` custom properties read off `:root` (31 tokens at HEAD: `--sc-shell-bg`, `--sc-shell-glow`, `--sc-form-surface`, `--sc-form-border`, `--sc-form-shadow`, `--sc-preview-surface`, `--sc-preview-border`, `--sc-stage-surface`, `--sc-stage-border`, `--sc-stage-dot`, `--sc-row-fill`, `--sc-row-border`, `--sc-pill-fill`, `--sc-pill-border`, `--sc-create-btn`, `--sc-create-btn-hover`, `--sc-text-primary`, `--sc-text-secondary`, `--sc-text-muted`, `--sc-accent`, `--sc-accent-soft`, `--sc-noise-stroke`, `--sc-shell-divider`, `--sc-shell-soft`, `--sc-shell-strong`, `--sc-shell-track`, `--sc-notch-shadow`, `--sc-chip-fill`, `--sc-chip-active`, `--sc-wave-a`, `--sc-wave-b`).
   - A frozen list of every `data-maho-identifier` attribute the React tree emits today (`space.create.name`, `space.theme.creatorPreview`, `space.theme.luminosity.minus`, `space.theme.luminosity.plus`, `space.theme.chevron.left`, `space.theme.chevron.right`, `space.theme.swatch.<i>`, `space.theme.mode.<hookIndex>`, `space.theme.grainDial`).
2. Capture a baseline build artifact:
   - Run `python3 maho-chromium/build/scripts/build_maho.py` once cleanly.
   - Record bundle byte size of the `bundle_react` action output, namely `app_bundle.js` emitted to `$target_gen_dir` for `//maho/browser/resources/maho_space_create:bundle_react`. Log the absolute path resolved during the baseline run into the baseline doc.
3. Capture the current public exports of `maho_common/react/primitives/index.ts` verbatim into the baseline doc, so Phase 2 can prove "no new exports added." At HEAD they are: `joinClassNames`, `Button`, `TextArea`, `TextInput`, `TooltipProvider`.

Exit criteria for Phase 0:
- Baseline doc committed to `.sisyphus/artifacts/space-create-shadcn-pattern/baseline-dom.md`.
- Build is green at HEAD.
- No source files changed.

---

## Phase 1 — Component Decomposition

**Goal:** Break `maho_space_create/react/app.tsx` (currently a single 416-line file containing seven inline components plus the `MODE_ENTRIES` constant and `ModeEntry` type) into a tree of small, single-purpose components, each in its own file, each consuming the existing `sc-*` classes.

### 1.1 Inline components in `app.tsx` today

```
SpaceCreateApp     — root: subscribes to store, computes derived view state, mounts <MahoThemeProvider> + <LeftPanel> + <RightPanel>.
LeftPanel          — name field + profile picker row + theme summary row + create / cancel buttons.
RightPanel         — preview canvas, luminosity row, swatch rail, decoration + grain dial.
ModeTabs           — three-tab mode/visual selector consumed by RightPanel.
ColorPalette       — swatch grid consumed by RightPanel.
WaveDecoration     — pure SVG decoration consumed by RightPanel.
GrainDial          — circular range input consumed by RightPanel.
```

Plus: `type ModeEntry` and `const MODE_ENTRIES` literal.

### 1.2 Target layout

All paths under `maho-chromium/browser/resources/maho_space_create/react/`:

```
react/
  app.tsx                 # shell only: root element lookup, store init, <MahoThemeProvider> wiring, <LeftPanel> + <RightPanel> mount
  store.ts                # unchanged
  types.ts                # unchanged unless a new component needs a shared local view-prop type
  tsconfig.json           # unchanged (already includes "./**/*.ts" and "./**/*.tsx")
  components/
    left_panel.tsx        # extracted from app.tsx
    right_panel.tsx       # extracted from app.tsx
    mode_tabs.tsx         # extracted from app.tsx
    color_palette.tsx     # extracted from app.tsx
    wave_decoration.tsx   # extracted from app.tsx
    grain_dial.tsx        # extracted from app.tsx
    index.ts              # optional barrel; only added if it removes import noise from app.tsx
  constants.ts            # optional; created ONLY if both components/mode_tabs.tsx and another component end up needing MODE_ENTRIES. If only mode_tabs.tsx uses it, MODE_ENTRIES stays inside mode_tabs.tsx.
```

No other component files are introduced. The plan deliberately mirrors the live inline structure 1-to-1; it does not invent a hypothetical "create_space_dialog" decomposition.

### 1.3 Rules each new file MUST follow

1. **One default-or-named export per file**, named in PascalCase, matching the file's concern (`LeftPanel`, `RightPanel`, `ModeTabs`, `ColorPalette`, `WaveDecoration`, `GrainDial`).
2. **`React.forwardRef`** for components that wrap a single measurable / focusable DOM element where a parent could plausibly need the ref. For the live tree, candidates are: `GrainDial` (wraps the range input), `ColorPalette` (wraps the swatch container), `WaveDecoration` (wraps the SVG), and `ModeTabs` (wraps the tab strip). `LeftPanel` and `RightPanel` are container shells; `forwardRef` is **optional** for those and may be added later without recourse to the plan.
3. **Props match the live call-sites in `app.tsx`.** Specifically:
   - `LeftPanel`: `{store, state, illustrationBg, orbBg, themeDesc, previewColors}`.
   - `RightPanel`: `{store, state, previewColors}`.
   - `ModeTabs`: `{store, selectedVisualMode}`.
   - `ColorPalette`: `{store, selectedColorIndex}`.
   - `WaveDecoration`: `{}`.
   - `GrainDial`: `{store, grain}`.
   No additional public props are introduced. `data-*` attributes that already exist in the JSX (`data-maho-identifier`, `data-mode`, `data-visual-mode`) are preserved verbatim.
4. **Class-name composition** is left to the writer. Acceptable:
   - String literal: `"sc-mode-tab"`.
   - Template string with conditional active flag: `` `sc-mode-tab${active ? ' active' : ''}` `` (matches the current `app.tsx` shape).
   - Array `.join(' ')`.
   - Optional use of the shared `joinClassNames` from `//resources/maho_common/react/primitives/join_class_names.js`. The pilot does **not** require any component to import `joinClassNames`; the helper exists and is allowed but is not a gating dependency.
   Forbidden: `clsx`, `cva`, `tailwind-merge`, any new `cn()` helper, any string-concat helper introduced under `maho_space_create/`.
5. **Variant → class mapping** is a `const` object colocated at the top of the component file when there is a real variant. Today the only variant of this kind is the `active` flag on `sc-mode-tab` and `sc-color-swatch`. A two-key map is fine:
   ```ts
   const MODE_TAB_CLASS = {
     active: 'sc-mode-tab active',
     idle: 'sc-mode-tab',
   } as const;
   ```
   Components without variants do not need a map.
6. **No new CSS.** Every class used must already exist in `space_create.css`. If a component appears to "need" a new class, that is a signal to stop and re-evaluate the decomposition, not to add CSS.
7. **No new `--sc-*` tokens.** Same rule as #6.
8. **State as data attributes**: `data-state="active"`, `data-mode`, `data-visual-mode` are allowed. Only add a `data-*` attr that is either already present in the live JSX or is purely additive (no CSS selector keys off it). Adding selectors is out of scope for the pilot.
9. **Imports stay path-stable.** Existing `app.tsx` imports `//resources/maho_common/maho_theme_utils.js`, `//resources/maho_common/react/theme_provider.js`, `./store.js`, and `./types.js`. New component files re-import what they need from the same paths (`hsbToCss` for `ColorPalette`, the store types from `./store.js` and view types from `./types.js`). No new third-party imports are introduced.

### 1.4 Decomposition order

Sequential within Phase 1; later files depend on earlier files being importable. The order matches a leaf-up extraction:

1. `wave_decoration.tsx` — pure leaf, zero props, smallest blast radius. Copies the existing function body verbatim.
2. `grain_dial.tsx` — leaf, takes `{store, grain}`, no internal state.
3. `color_palette.tsx` — leaf, takes `{store, selectedColorIndex}`, imports `hsbToCss` from `maho_theme_utils`.
4. `mode_tabs.tsx` — leaf, takes `{store, selectedVisualMode}`. Owns `MODE_ENTRIES` and `ModeEntry` type unless step 0a below extracts them to `constants.ts`.
   - 4a (conditional): if a second component starts using `MODE_ENTRIES` in this pilot, extract it to `react/constants.ts`. Otherwise it stays in `mode_tabs.tsx`.
5. `right_panel.tsx` — composes `ModeTabs`, `ColorPalette`, `WaveDecoration`, `GrainDial`. Receives `{store, state, previewColors}`. Migrates the `canvasStyle` `useMemo` along with it.
6. `left_panel.tsx` — receives `{store, state, illustrationBg, orbBg, themeDesc, previewColors}`. Migrates `handleNameInput`, `handleProfileChange`, `handleCreate`, `handleCancel`.
7. `app.tsx` reduced to:
   - imports + bootstrap (the `useEffect`-gated `store.bootstrap()` call),
   - the `useSyncExternalStore` subscription,
   - the derived values currently computed in `SpaceCreateApp` (`previewColors`, `accentColor`, `illustrationBg`, `orbBg`, `modeLabel`, `colorHex`, `themeDesc`),
   - `<MahoThemeProvider>` wrapping `<LeftPanel>` + `<RightPanel>`,
   - the `is-theme-editor-engaged` `useLayoutEffect`,
   - the root-element lookup + `createRoot(...).render(...)` at module scope.

After each step, the surface must still render and behave identically. Phase 4 verification runs after step 7.

---

## Phase 2 — Shared Primitive Evaluation

**Goal:** Decide, with evidence, whether any component from Phase 1 should be promoted to `maho_common/react/primitives/`. The default answer is **no**.

Evaluation procedure (run **after** Phase 1 is complete and green):

1. For each new component in `react/components/` (`LeftPanel`, `RightPanel`, `ModeTabs`, `ColorPalette`, `WaveDecoration`, `GrainDial`), answer two questions:
   - **Reuse evidence:** Is there at least one other existing surface in `maho-chromium/browser/resources/` whose code already needs this exact shape? Grep evidence required, not speculation. (`LeftPanel`/`RightPanel` are clearly surface-specific and fail this check by inspection. `WaveDecoration`, `ModeTabs`, `ColorPalette`, `GrainDial` are the only candidates plausibly worth checking, and each is likely too coupled to space-creation copy or store shape to qualify.)
   - **Genericity:** Is the component free of `space_create`-specific copy, layout, and store coupling? Or could it be made so without contortion? In particular, components that take `{store: SpaceCreateStore}` are not generic without a refactor that is out of scope for this pilot.
2. A component is promoted **only if both answers are yes**. Otherwise it stays local under `maho_space_create/react/components/`.
3. The pilot's expected outcome is **zero promotions**. That is an acceptable, indeed preferred, conclusion. The decomposition value lands inside the surface; promotion is a separate decision gated on evidence the pilot is unlikely to surface alone.
4. If a promotion is justified, the procedure is:
   - Add the component file to `maho_common/react/primitives/`.
   - Re-export from `maho_common/react/primitives/index.ts`.
   - Replace the local copy in `maho_space_create/react/components/` with a thin re-export or a direct import-and-use.
   - Document the promotion in the Phase 2 evaluation log.
   - **Do not** restructure `maho_common`, do not split it, do not add subfolders.

Output of Phase 2: `.sisyphus/artifacts/space-create-shadcn-pattern/phase2-evaluation.md`, listing each Phase 1 component with reuse-evidence / genericity verdict and promotion decision.

---

## Phase 3 — Token Audit & Documentation

**Goal:** Confirm and document that the `--sc-*` token set in `space_create.css` is unchanged in count, name, value, and authority, and that the `sc-*` class set is unchanged.

Tasks:

1. Diff the `:root`-scoped (and any local-scoped) `--sc-*` declarations in `maho-chromium/browser/resources/maho_space_create/space_create.css` against the Phase 0 capture. Expected diff: empty. Expected count: 31.
2. Diff the set of `.sc-*` class selectors in `space_create.css` against the Phase 0 capture. Expected diff: empty. Expected count: 125.
3. Produce `maho-chromium/browser/resources/maho_space_create/TOKENS.md` containing:
   - The full list of `--sc-*` tokens currently defined, grouped by concern (shell surfaces, form surfaces, preview / stage surfaces, row + pill chrome, action button, text colors, accent, decoration / wave, chip + shell-glass tokens).
   - The full list of `sc-*` classes currently defined, grouped by component concern (`sc-left-*`, `sc-illustration-*`, `sc-card-row*`, `sc-name-input`, `sc-icon-row*`, `sc-pill-*`, `sc-theme-dot`, `sc-create-btn`, `sc-cancel-btn`, `sc-right*`, `sc-mode-*`, `sc-preview-*`, `sc-stage-*`, `sc-luminosity-*`, `sc-swatch-*`, `sc-color-*`, `sc-wave-*`, `sc-dial-*`, `sc-grain-hook`).
   - A short statement: "These are the authoritative styling primitives for the `maho_space_create` surface. They MUST NOT be aliased, re-themed, or replaced as part of the shadcn-pattern pilot."
4. Confirm in the same doc: no Tailwind, no `cva`, no `clsx`, no `cn.ts`, no semantic HSL tokens, no second surface. This is restated in the file as a guard for future contributors.

Phase 3 produces no functional change. It is a documentation lockdown.

---

## Phase 4 — Build Verification & Integration Gate

**Goal:** Prove the refactor is behavior-preserving and ship-safe.

Verification tasks (all required, run from repo root unless noted):

1. **Type check**
   - `cd maho && bun run typecheck`
   - Expectation: clean, zero new errors vs Phase 0 baseline.
2. **Workspace tests**
   - `cd maho && bun run test`
   - Expectation: clean.
3. **WebUI bundle build**
   - `python3 maho-chromium/build/scripts/build_maho.py`
   - Expectation: exit 0. The `bundle_react` action in `maho-chromium/browser/resources/maho_space_create/BUILD.gn` succeeds and emits `app_bundle.js`. Bundle size for the emitted `app_bundle.js` is within ±5% of Phase 0 baseline. No `bundle_guard` config edits permitted; if the bundle exceeds budget, **revert and reduce**, do not raise the budget.
4. **LSP diagnostics**
   - Run `lsp_diagnostics` on every file touched by Phase 1: `react/app.tsx`, every `react/components/*.tsx`, and `react/constants.ts` if it was created.
   - Expectation: zero errors.
5. **DOM snapshot diff**
   - Re-capture rendered DOM + classnames in the four states from Phase 0 (idle, name-focused, color-active, submit-pending).
   - Expectation: diff against Phase 0 baseline is empty (or limited to additive `data-*` attrs explicitly introduced in Phase 1; none are planned by default).
6. **Manual smoke**
   - Launch the built browser, open `chrome://maho-space-create`, exercise: name input, profile select, mode tab change, color swatch click, chevron L/R, luminosity +/-, grain slider, cancel, submit. Compare against Phase 0 screen recording.

Integration gate: all six checks pass. If any fails, the surface is reverted to HEAD and the failure is logged before any retry.

---

## File-by-File Modification Matrix

Paths are repo-relative.

| File | Change | Phase |
|---|---|---|
| `maho-chromium/browser/resources/maho_space_create/react/app.tsx` | Reduced to: imports, root-element lookup, store init, derived view-state hooks (`useSyncExternalStore`, `previewColors`/`accentColor`/`illustrationBg`/`orbBg`/`themeDesc` memos), `useLayoutEffect` for `is-theme-editor-engaged`, and `<MahoThemeProvider>` wrapping `<LeftPanel>` + `<RightPanel>`. All inline component bodies (`LeftPanel`, `RightPanel`, `ModeTabs`, `ColorPalette`, `WaveDecoration`, `GrainDial`) moved out. | 1 |
| `maho-chromium/browser/resources/maho_space_create/react/store.ts` | **No change.** | — |
| `maho-chromium/browser/resources/maho_space_create/react/types.ts` | **No change** unless a new component needs a shared local prop type, in which case the type is added here. | 1 (conditional) |
| `maho-chromium/browser/resources/maho_space_create/react/tsconfig.json` | **No change.** Already includes `./**/*.ts` and `./**/*.tsx`, so new files under `react/components/` are picked up automatically. | — |
| `maho-chromium/browser/resources/maho_space_create/react/components/left_panel.tsx` | **New.** Extracted from `app.tsx`'s `LeftPanel` function. | 1 |
| `maho-chromium/browser/resources/maho_space_create/react/components/right_panel.tsx` | **New.** Extracted from `app.tsx`'s `RightPanel` function (including the `canvasStyle` memo). | 1 |
| `maho-chromium/browser/resources/maho_space_create/react/components/mode_tabs.tsx` | **New.** Extracted from `app.tsx`'s `ModeTabs` function. Owns `MODE_ENTRIES` + `ModeEntry` unless `constants.ts` is created in the conditional branch below. | 1 |
| `maho-chromium/browser/resources/maho_space_create/react/components/color_palette.tsx` | **New.** Extracted from `app.tsx`'s `ColorPalette` function. Imports `hsbToCss` and `SPACE_THEME_COLOR_PRESETS` from `//resources/maho_common/maho_theme_utils.js`. | 1 |
| `maho-chromium/browser/resources/maho_space_create/react/components/wave_decoration.tsx` | **New.** Extracted from `app.tsx`'s `WaveDecoration` function. Pure, zero-prop. | 1 |
| `maho-chromium/browser/resources/maho_space_create/react/components/grain_dial.tsx` | **New.** Extracted from `app.tsx`'s `GrainDial` function. | 1 |
| `maho-chromium/browser/resources/maho_space_create/react/components/index.ts` | **New (optional).** Barrel re-exporting the six components. Add only if it noticeably reduces import noise in `app.tsx`. | 1 (conditional) |
| `maho-chromium/browser/resources/maho_space_create/react/constants.ts` | **New (conditional).** Holds `MODE_ENTRIES` + `ModeEntry`. Created only if `MODE_ENTRIES` ends up consumed by more than one component file in this pilot. Otherwise the constant stays in `mode_tabs.tsx`. | 1 (conditional) |
| `maho-chromium/browser/resources/maho_space_create/space_create.css` | **No change.** | — |
| `maho-chromium/browser/resources/maho_space_create/space_create.html` | **No change.** | — |
| `maho-chromium/browser/resources/maho_space_create/app.ts` | **No change.** Surface bootstrap is unchanged. | — |
| `maho-chromium/browser/resources/maho_space_create/BUILD.gn` | Append the new `react/components/*.tsx` files (and `react/components/index.ts` and/or `react/constants.ts` if they were created) to the explicit `inputs` list of the existing `bundle_react` action. **No target restructuring.** No new actions, no new deps, no `static_files` change, no `ts_files` change. The current `inputs` list is `["//third_party/node/node.py", "maho_space_create.mojom-webui.js.d.ts", "react/app.tsx", "react/store.ts", "react/tsconfig.json", "react/types.ts", "bundle_react.mjs"]`; the edit appends the new component file paths to it. | 1 |
| `maho-chromium/browser/resources/maho_space_create/bundle_react.mjs` | **No change.** The esbuild entry stays `react/app.tsx`; esbuild traces the new component imports transitively. The plan does not introduce a glob system. | — |
| `maho-chromium/browser/resources/maho_space_create/maho_space_create.mojom-webui.js.d.ts` | **No change.** | — |
| `maho-chromium/browser/resources/maho_common/react/primitives/index.ts` | **No change** in the default outcome. Edit only if Phase 2 promotes a primitive (expected: not). | 2 (conditional) |
| `maho-chromium/browser/resources/maho_common/react/primitives/join_class_names.ts` | **No change.** Available to the pilot as an optional shared utility; not required by any new component. | — |
| `maho-chromium/browser/resources/maho_common/react/primitives/button.tsx` | **No change.** | — |
| `maho-chromium/browser/resources/maho_common/react/primitives/text_input.tsx` | **No change.** | — |
| `maho-chromium/browser/resources/maho_common/react/primitives/text_area.tsx` | **No change.** | — |
| `maho-chromium/browser/resources/maho_common/react/primitives/tooltip_provider.tsx` | **No change.** | — |
| `maho-chromium/browser/resources/maho_space_create/TOKENS.md` | **New.** Documentation lockdown of authoritative `--sc-*` tokens + `sc-*` classes. | 3 |
| `.sisyphus/artifacts/space-create-shadcn-pattern/baseline-dom.md` | **New.** Phase 0 capture. | 0 |
| `.sisyphus/artifacts/space-create-shadcn-pattern/phase2-evaluation.md` | **New.** Promotion-evaluation log. | 2 |

Files explicitly **not** in scope, anywhere in the repo:

- Anything under `maho-chromium/components/` (Mojo IDL).
- Anything under `maho-chromium/browser/ui/views/` related to space-create.
- Anything under `maho-chromium/browser/ui/webui/maho_space_create/` (C++ WebUI bridge).
- Any other `maho-chromium/browser/resources/<surface>/` directory.
- Any file under `maho/` (Rust crates), `mail/`, `website/`, `chromium/`.
- The repo root `package.json`, `bun.lockb`, `Cargo.toml`, or any config introducing new dependencies.

### Rejected (non-plan) decompositions

These names appeared in earlier drafts and are explicitly **not** part of this plan, because they do not correspond to anything in the live source: `create_space_dialog.tsx`, `create_space_header.tsx`, `create_space_name_field.tsx`, `create_space_icon_picker.tsx`, `create_space_actions.tsx`, `create_space_submit_button.tsx`, `create_space_color_picker.tsx`, `create_space_color_swatch.tsx`. The live tree has no separate dialog / header / icon-picker / submit-button components, so inventing them would invent UI rather than restructure existing UI. If a future plan wants this kind of split, it is a separate decision out of scope here.

---

## Verification Matrix

| Check | Tool / Command | Pass Criterion | Phase |
|---|---|---|---|
| Repo typecheck | `cd maho && bun run typecheck` | Exit 0; no new errors vs Phase 0. | 4 |
| Repo tests | `cd maho && bun run test` | Exit 0. | 4 |
| WebUI bundle build | `python3 maho-chromium/build/scripts/build_maho.py` | Exit 0; `bundle_react` action emits `app_bundle.js`. | 0, 4 |
| Bundle size | Compare emitted `app_bundle.js` size vs Phase 0 capture | Within ±5%; no `bundle_guard` edits. | 4 |
| LSP errors on changed files | `lsp_diagnostics` on `react/app.tsx`, `react/components/*.tsx`, and `react/constants.ts` (if created) | Zero errors. | 1, 4 |
| Token set unchanged | Diff `--sc-*` declarations in `space_create.css` vs Phase 0 | Empty diff; count remains 31. | 3, 4 |
| Class set unchanged | Diff `.sc-*` selectors in `space_create.css` vs Phase 0 | Empty diff; count remains 125. | 3, 4 |
| `maho_common` exports unchanged (default path) | Diff `maho_common/react/primitives/index.ts` vs Phase 0 | Empty diff, unless Phase 2 produced a justified promotion. | 2, 4 |
| No banned imports introduced | Grep changed files for `clsx`, `class-variance-authority`, `cva`, `tailwind-merge`, `from "tailwindcss"`, `cn(`, `from ".*/cn"` | No matches. | 1, 4 |
| No new `--sc-*` tokens | Grep `space_create.css` Phase 0 vs Phase 4 for new `--sc-` declarations | No additions. | 3, 4 |
| BUILD.gn shape preserved | Diff `BUILD.gn` vs Phase 0 | Only additions to `inputs`; no new actions, no `static_files` / `ts_files` / `mojo_files` / `extra_grdp_*` change. | 1, 4 |
| `bundle_react.mjs` unchanged | Diff vs Phase 0 | Empty diff. | 4 |
| DOM snapshot parity | Re-render the four documented states; diff classnames + tree | No regressions; only additive `data-*` attrs allowed. | 4 |
| Manual smoke | Launch built browser; create-space flow end-to-end (name, profile, mode, swatch, chevrons, luminosity, grain, cancel, submit) | Behavior parity vs Phase 0 recording. | 4 |

---

## Out-of-Scope / Deferred Items

The following are explicitly **deferred** and not part of this pilot. They MUST NOT be introduced as a side effect.

- Adopting Tailwind, in any form (full, JIT, `@apply`, preset, or alias).
- Adopting `class-variance-authority` (`cva`) or any variant-DSL library.
- Adopting `clsx`, `tailwind-merge`, or any `cn()` helper file. The existing `joinClassNames` is **available** but not mandated; no new join helper is introduced under `maho_space_create/`.
- Introducing semantic theme tokens (e.g. shadcn's HSL `--background` / `--foreground` model).
- Introducing any token alias layer that re-exports `--sc-*` under different names.
- Restructuring `maho_common/react/primitives/` (no subfolders, no domain split, no barrel reorg).
- Editing `bundle_guard` budgets or any bundle-budget config.
- Changing `bundle_react.mjs` (entry point, plugins, externals, target, format).
- Editing any `.mojom` file, `mojom-webui.js.d.ts` shim, or any C++ under `browser/ui/views/` or `browser/ui/webui/maho_space_create/`.
- Migrating any second surface (e.g. command palette, sidebar, archive panel, AI side panel, sibling space surfaces). The pattern, if successful, is **available** for future surfaces but **not committed to** by this plan.
- Producing a "shadcn migration roadmap" doc covering the rest of the WebUI surfaces.
- Introducing new `--sc-*` tokens or new `sc-*` classes. Both sets are frozen for the pilot.
- Replacing `joinClassNames` with anything else, including a renamed version of itself.
- Inventing components that don't exist in the current `app.tsx` (e.g. a stand-alone "create space dialog", a stand-alone "header", a stand-alone "icon picker", a stand-alone "submit button"). The pilot mirrors the live structure 1-to-1.
- Adding component-library-style stories, MDX docs, or storybook tooling.

If during execution any of the above appears "necessary," the correct response is to stop, log it as a discovered constraint conflict in `.sisyphus/artifacts/space-create-shadcn-pattern/blockers.md`, and pause for re-planning, not to expand scope.

---

## Task Dependency Graph / Parallel Execution Plan

Phases run in strict order: **Phase 0 → Phase 1 → Phase 2 → Phase 3 → Phase 4.**

Within each phase, the parallelism rules below apply.

### Phase 0 (parallel)

```
P0a: capture DOM/classnames snapshot   ─┐
P0b: capture baseline build + app_bundle.js size ─┼─► Phase 0 done
P0c: capture maho_common exports       ─┘
```

P0a, P0b, P0c are independent and may run in parallel.

### Phase 1 (parallel where safe)

The six leaf extractions are mutually independent because none of them imports another:

```
P1.1 wave_decoration.tsx  ─┐
P1.2 grain_dial.tsx        ─┤
P1.3 color_palette.tsx     ─┼─► P1.5 right_panel.tsx ─┐
P1.4 mode_tabs.tsx         ─┘                          │
                                                       ├─► P1.7 app.tsx reduce ─► P1.8 BUILD.gn inputs append
                              P1.6 left_panel.tsx ─────┘
```

- P1.1, P1.2, P1.3, P1.4 may run in parallel. They only export new files; nothing imports them yet.
- P1.5 (`right_panel.tsx`) depends on P1.1–P1.4 because it imports all four.
- P1.6 (`left_panel.tsx`) is independent of P1.1–P1.5 and may run in parallel with P1.5; it imports nothing from `react/components/`.
- P1.7 (`app.tsx` reduction) depends on P1.5 and P1.6 being landed; it imports both.
- P1.8 (`BUILD.gn` `inputs` append) is the last step; it must list every new file produced. Done after P1.7 so the list is final.

After every `P1.x` lands: run typecheck + LSP diagnostics on the touched file(s). Do not batch.

### Phase 2 (parallel)

```
For each component C produced in Phase 1:
  evaluate(C) -> {keep_local | promote}
```

Each evaluation is independent. They may run in parallel. Result is a single aggregated doc.

### Phase 3 (sequential, small)

```
P3.1 token diff ─► P3.2 class diff ─► P3.3 write TOKENS.md
```

Sequential because each step's output feeds the next, and the doc is the final artifact.

### Phase 4 (parallel where safe)

```
P4a typecheck ─┐
P4b tests      ─┼─► P4d bundle build ─► P4e bundle size diff ─► P4f DOM snapshot diff ─► P4g manual smoke
P4c lsp        ─┘
```

- P4a, P4b, P4c are independent and may run in parallel.
- P4d (full WebUI build) must run after P4a/P4b/P4c are clean, to avoid burning a long build on code that fails cheap checks. The build acquires the workspace `.build.lock`; do not delete the lock file.
- P4e, P4f, P4g are sequential after P4d.

### Cross-phase gate

No phase starts until the previous phase's exit criteria are met. The integration gate at the end of Phase 4 is the single ship/no-ship decision point for the pilot.

---

## Closing Note

This plan deliberately produces a small, reversible, single-surface refactor. Its purpose is to evaluate whether shadcn's **structural** patterns improve maintainability of a Maho WebUI surface **without** importing shadcn's styling system, dependencies, or token model. The success criterion is "Phase 1 lands cleanly, Phase 4 gate passes, and the surface is byte-for-byte equivalent in token + class output." Anything beyond that is a separate decision made on separate evidence.
