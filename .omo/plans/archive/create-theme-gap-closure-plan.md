# Create Theme — Full Gap Closure Plan

**Created:** 2026-05-06
**Branch:** TBD
**Status:** Draft — pending Momus review

## Problem Statement

The "create theme" feature has significant implementation gaps between the Swift shell reference and the Chromium overlay:

1. **C++ theme pipeline is solid-only** — `MahoSpaceThemeState` ignores gradient themes, `maho_color_mixer` only blends a single tint
2. **Export/Import are stubs** — context menu items exist but `FileSelected()` is a no-op
3. **Space Config editor is solid-only** — no gradient/harmony/opacity controls
4. **Space Create editor is partial** — gradient mode exists but hard-coded complementary with no harmony selector or opacity slider
5. **Native Views create-space** — `maho_sidebar_create_space_view.cc` shows placeholder text, no theme editing

## Scope

Close ALL gaps. After completion, gradient themes should render in browser chrome, export/import should work, and all WebUI surfaces should support the full theme model.

---

## Phase 1: C++ Theme Pipeline — Gradient Support

**WHY FIRST:** Everything downstream (export, UI, rendering) depends on the C++ pipeline correctly parsing and applying gradient themes. Without this, gradient themes silently fail.

### Task 1.1: MahoSpaceThemeState Gradient Parsing

**File:** `maho-chromium/browser/ui/theme/maho_space_theme_state.cc`

**Current:** `UpdateFromCore()` (lines 57-115) scans space view models for a single `"color"` string field. Gradient themes have no `"color"` at top level → state stays empty → no tint applied.

**Change:**
- Parse the `theme` field from the active space view model JSON
- If `type == "solid"` → extract `color` from `color.hex` (existing path)
- If `type == "gradient"` → extract the **primary color** (first `gradientColors` entry) as the dominant tint
- Store gradient metadata separately for Phase 1.2

**Acceptance:**
- Gradient theme active → `GetChromeTint()` returns a valid SkColor derived from gradient's primary color
- Solid themes continue to work unchanged
- Unit/manual test: activate a gradient space → chrome tints correctly with primary gradient color

### Task 1.2: Color Mixer Gradient Rendering

**File:** `maho-chromium/browser/ui/theme/maho_color_mixer.cc`

**Current:** Phase 3 (lines 329-346) blends a single `space_tint` into titlebar (15%) and sidebar (6%).

**Change — Two options (decide during implementation):**

**Option A (Minimal — recommended first):** Keep single-tint approach but use gradient primary color. This means gradients tint chrome the same way solids do, just using the dominant gradient color. Visual gradient rendering stays in WebUI only.

**Option B (Full):** Add gradient awareness to the mixer — e.g., titlebar could have a subtle CSS-like gradient overlay. This requires significant changes to the color provider system and may conflict with upstream Chromium assumptions.

**Recommendation:** Ship Option A first. Option B can be a follow-up if the visual effect isn't satisfying.

**Acceptance:**
- Gradient space active → titlebar/sidebar tinted with gradient's primary color
- No visual regression for solid themes
- No type-safety suppression

### Task 1.3: Theme State Header Updates

**File:** `maho-chromium/browser/ui/theme/maho_space_theme_state.h`

**Change:** Add any new fields/methods needed for gradient metadata storage (if Option B is pursued later).

---

## Phase 2: Export/Import Theme

**WHY:** Context menu UX already exists. Users see "Export Theme…" / "Import Theme…" but clicking does nothing useful. Fix the stubs.

### Task 2.1: Export Theme Implementation

**File:** `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc`

**Current:** `ExportTheme()` (lines 337-366) opens a save dialog. `FileSelected()` (lines 384-393) is a no-op.

**Change:**
- In `FileSelected()`, when `pending_file_op_ == kExport`:
  - Get the target space's theme JSON from core (via `maho_core_get_space_view_models` → find by space_id → extract `theme` field)
  - Write the theme JSON to the selected file path
  - Show success notification or silently complete

**Reference:** Swift shell `SpaceContextMenu.swift:108-131` — serializes `SpaceTheme` to JSON and writes via `NSSavePanel`.

**Acceptance:**
- Right-click space → Export Theme → select file → `.mahotheme` (or `.json`) file created with valid theme JSON
- File contains full theme data (solid or gradient)

### Task 2.2: Import Theme Implementation

**File:** `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc`

**Current:** `ImportTheme()` (lines 368-381) opens an open-file dialog. `FileSelected()` is a no-op.

**Change:**
- In `FileSelected()`, when `pending_file_op_ == kImport`:
  - Read the selected file contents
  - Validate it's valid theme JSON (parse and check `type` field)
  - Apply via `maho_core_update_space_theme(core, space_id, theme_json)` or `DispatchShellEvent("update_space_config", ...)`
  - Refresh `MahoSpaceThemeState` and clear color cache

**Reference:** Swift shell `SpaceContextMenu.swift:133-146` — reads file, decodes `SpaceTheme`, calls `MahoBridge.shared.updateSpaceConfig(...)`.

**Acceptance:**
- Right-click space → Import Theme → select valid `.mahotheme` file → theme applied immediately
- Invalid file → graceful failure (no crash, ideally a toast/notification)
- Chrome tinting updates immediately after import

---

## Phase 3: Space Config — Gradient Support

**WHY:** Users editing an existing space can only choose solid colors. Should match sidebar editor parity.

### Task 3.1: Add Gradient UI to Space Config

**File:** `maho-chromium/browser/resources/maho_space_config/app.ts`

**Current:** Lines 137-180 — `buildThemeJson()` serializes solid-only. UI is preset swatches only.

**Change:**
- Add gradient toggle (like sidebar editor)
- Add harmony selector (complementary, analogous, triadic, etc.)
- Add opacity slider
- Update `buildThemeJson()` to serialize gradient themes when gradient mode is active

**Reference:** `maho_sidebar/app.ts:799-1210` — the sidebar's `createThemeEditorPanel()` is the gold standard.

**Decision Point:** Should Space Config embed the SAME editor component as sidebar, or keep a simplified version? If the sidebar editor is a reusable module, extract and share it.

**Acceptance:**
- Space config page shows gradient toggle + harmony + opacity when gradient mode selected
- Saving with gradient mode produces valid gradient theme JSON
- Theme applies correctly (Phase 1 pipeline handles it)

---

## Phase 4: Space Create — Gradient Editor Enrichment

**WHY:** Create flow has partial gradient support but hard-codes complementary harmony and 0.8 opacity.

### Task 4.1: Enrich Gradient Controls in Space Create

**File:** `maho-chromium/browser/resources/maho_space_create/app.ts`

**Current:** Lines 383-405 — hard-coded `harmony: "complementary"`, `opacity: 0.8`, texture from grain only. Lines 262-288 — only brightness and grain sliders.

**Change:**
- Add harmony selector UI (dropdown or button group)
- Add opacity slider
- Wire selected harmony/opacity into gradient JSON builder
- Remove hard-coded complementary fallback

**Reference:** Same sidebar editor (`maho_sidebar/app.ts:799-1210`).

**Decision Point:** Same as Phase 3 — extract shared component vs duplicate.

**Acceptance:**
- Space create shows harmony selector + opacity slider in gradient mode
- Created gradient theme uses user-selected harmony and opacity
- Preview reflects actual gradient settings

---

## Phase 5: Shared Theme Editor Component (Optional Refactor)

**WHY:** Three surfaces (sidebar, space-config, space-create) need the same editor. DRY.

### Task 5.1: Extract Reusable Theme Editor

**From:** `maho-chromium/browser/resources/maho_sidebar/app.ts` (lines 799-1210)
**To:** `maho-chromium/browser/resources/maho_common/theme_editor.ts` (new file)

**Change:**
- Extract `createThemeEditorPanel()` and its state/behavior into a standalone module
- Export a function/class that can be embedded in any WebUI surface
- Import and use in sidebar, space-config, space-create

**Acceptance:**
- All three surfaces use the shared component
- No behavioral regression
- Shared CSS via `maho_common/maho_theme.css` (already exists)

---

## Phase 6: Native Views Create-Space Theme UI

**WHY:** `maho_sidebar_create_space_view.cc` (line 260-264) shows "Dark theme shell for now. Styling options come next." — this is the last placeholder.

### Task 6.1: Assess Native Views Feasibility

**Question:** Should the native Views create-space panel embed a WebUI widget for theme editing, or build native Views controls?

**Option A (WebUI embed):** Use a `WebView` inside the native Views panel that loads the shared theme editor component. Less code, consistent with other surfaces.

**Option B (Native Views):** Build native Chromium Views controls (color picker, sliders, buttons). More work, but native feel.

**Recommendation:** Depends on what the current native create-space panel does. If it already uses WebUI for other parts, go with Option A. If it's pure Views, decide based on effort.

### Task 6.2: Implement Theme Controls

**File:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

**Change:** Replace placeholder text with actual theme selection UI (per decision from 6.1).

**Acceptance:**
- Native create-space panel shows theme color selection
- Selected theme is included in space creation payload
- Matches the fidelity of at least solid theme selection

---

## Dependency Graph

```
Phase 1 (C++ Pipeline) ──┐
                          ├── Phase 2 (Export/Import) ← independent after Phase 1
                          ├── Phase 3 (Space Config)  ← independent after Phase 1
                          ├── Phase 4 (Space Create)  ← independent after Phase 1
                          └── Phase 5 (Shared Component) ← depends on 3+4 decisions
Phase 6 (Native Views) ← can start independently, but benefits from Phase 5
```

## Execution Order

1. **Phase 1** first — unblocks everything
2. **Phase 2** second — quick win, stubs → real code
3. **Phase 3 + 4** parallel — WebUI gradient parity
4. **Phase 5** if 3+4 reveal duplication worth extracting
5. **Phase 6** last — biggest unknown, needs design decision

## Files Changed (Summary)

| File | Phase | Change |
|------|-------|--------|
| `browser/ui/theme/maho_space_theme_state.cc/h` | 1 | Gradient parsing |
| `browser/ui/theme/maho_color_mixer.cc` | 1 | Gradient-aware tinting (or primary-color fallback) |
| `browser/ui/context_menu/maho_space_context_menu.cc` | 2 | Export/Import file I/O |
| `browser/resources/maho_space_config/app.ts` | 3 | Gradient editor UI |
| `browser/resources/maho_space_create/app.ts` | 4 | Harmony selector + opacity slider |
| `browser/resources/maho_common/theme_editor.ts` (new) | 5 | Shared editor component |
| `browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` | 6 | Replace placeholder |

## Risks

1. **Color mixer Option B complexity** — gradient rendering in native Chromium color system may be architecturally difficult. Stick with Option A (primary-color tint) unless there's a clear visual need.
2. **Theme JSON schema drift** — three surfaces building theme JSON independently. Phase 5 mitigates this.
3. **Upstream Chromium conflicts** — `maho_color_mixer.cc` hooks into upstream color provider. Test with upstream updates.
4. **Native Views scope creep** — Phase 6 could balloon. Time-box the design decision.
