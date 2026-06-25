# Theme Parity Reference Index (Phase 1)

> **⚠️ SUPERSEDED MAPPING (2026-05-22)**: This index maps `maho_space_create` components to `maho_space_config` for migration. The migration is forbidden per `.sisyphus/plans/theme-picker-architecture-decision.md`. The component **inventory** here remains a useful reference for understanding the React picker; the **migration mapping** is rejected.

**Status:** Draft, evidence-only. No production behavior changes.
**Scope:** Lock the inputs to Phases 2 through 7 of `theme-parity-shared-file-plan.md` (sections 113 to 145 and 248 to 280). Every claim below is sourced from a specific file and line range. Conflicts are recorded, not resolved.

This index is the spec input for:
- Phase 2 (preview lifecycle hardening)
- Phase 3 (test migration off dead native-picker hooks)
- Phase 4 (create vs edit React split)
- Phase 5 (create-mode composition port)
- Phase 6 (visual parity to Image1)
- Phase 7 (test/golden hardening on the shared surface)

---

## 0. Blocker: Image1 asset is missing

`.sisyphus/plans/assets/theme-parity-image1.png` is the canonical visual target for create-mode parity per the plan (sections 117 to 119, 295 to 296). As of this index:

- `.sisyphus/plans/` contains plan markdown files only.
- There is no `assets/` subdirectory.
- The image has not been committed.

Consequences:

1. Visual parity (Phase 6) cannot be frozen. Any "matches Image1" claim is unverifiable until the asset lands.
2. Phase 5 should still proceed on **structural and behavioral parity** against the old `maho_space_create` React surface (this index is the spec for that). It must NOT claim visual parity.
3. Phase 7 goldens that depend on Image1 (full-create-mode screenshot diff) must wait. JSON-shape goldens and behavior-only tests can proceed earlier.

Action item: commit Image1 at `.sisyphus/plans/assets/theme-parity-image1.png` before Phase 6 starts. Until then, Phases 1 to 5 and the non-visual half of Phase 7 are unblocked.

---

## 1. Current production create flow (chain of evidence)

The live, user-reachable create flow is:

1. Footer plus button. `MahoSidebarFooterView` constructor wires `plus_button_` with tooltip `u"Create space"` and `OnPlusPressed`. See `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` lines 338 to 342.
2. Action popover. `OnPlusPressed` calls `OpenSpaceCreationSurface`, which calls `MahoSidebarActionPopoverView::Show(plus_button_, ...)`. Same file, lines 417 to 452.
3. `New Space` row. `HandleActionPopoverAction(kNewSpace)` walks up to `MahoSidebarView` and calls `ShowCreateSpaceSurface()`. Lines 454 to 465.
4. Inline create surface. `MahoSidebarView::ShowCreateSpaceSurface` mounts `MahoSidebarCreateSpaceView` (native Views) inside the sidebar. The View owns the name field, profile pill, theme button, theme summary, create/cancel buttons. Header in `maho_sidebar_create_space_view.h` lines 41 to 107.
5. Theme button. `MahoSidebarCreateSpaceView::OnThemePressed()` calls `MahoSpaceConfigDialog::OpenForCreate(profile, native_window, current_theme_json, callback)`. See `maho_sidebar_create_space_view.cc` lines 1871 to 1897.
6. Shared dialog. `MahoSpaceConfigDialog` opens in `kCreate` mode and hosts `chrome://maho-space-config?mode=create&draft_theme=...` (the shared WebUI surface).

So the **shared `maho_space_config` WebUI is reached only as the theme picker** for create. The Space-name input, profile selection, and the actual `maho_core_create_space` FFI call all live in the native `MahoSidebarCreateSpaceView`, not in the shared WebUI.

Compare with the old `maho_space_create` WebUI, which owned the entire create surface (name + profile + theme) inside one React app and called `pageHandler.createSpace(...)` itself.

This split is the source of the parity dispute recorded in section 6.

---

## 2. Old `maho_space_create` React surface

File: `maho-chromium/browser/resources/maho_space_create/react/app.tsx` (416 lines).

### 2.1 Components and responsibilities

| Component | Lines | Responsibility |
|-----------|-------|----------------|
| `SpaceCreateApp` | 28 to 90 | Root. Subscribes to `SpaceCreateStore` via `useSyncExternalStore`, kicks `store.bootstrap()` once, toggles `is-theme-editor-engaged` on the host element, computes `previewColors`, accent, illustration/orb backgrounds, and theme description. Renders `MahoThemeProvider` wrapping `LeftPanel` + `RightPanel`. |
| `LeftPanel` | 92 to 197 | Hero illustration card, "Create a Space" title (line 131), subtitle (132 to 135), **Space name input** (lines 137 to 147, `data-maho-identifier="space.create.name"`, autoFocus, trims on input), Profile pill `<select>` (149 to 166), Choose-a-Theme summary row with preview dot (168 to 180), primary CTA `Create Space` disabled until name + not creating (184 to 190), Cancel (192 to 194). |
| `RightPanel` | 199 to 312 | Preview canvas (`sc-preview-canvas`, `data-maho-identifier="space.theme.creatorPreview"`) with mode-aware solid or radial-gradient background, glow, dots, noise overlay (opacity = grain/200), "Choose a theme for your Space" instruction, "New theme" badge, and **preview label bound to `state.spaceName || 'Space'`** (lines 247 to 249). Brightness controls (luminosity minus, range, plus), swatch panel with chevron navigation, wave decoration, and grain dial. |
| `ModeTabs` | 314 to 338 | Three mode chips: `solid`, `zen`, `solid-accent`, each with `data-maho-identifier="space.theme.mode.{0,1,2}"`. Calls `store.setMode(id, visualId)`. |
| `ColorPalette` | 340 to 361 | Renders `SPACE_THEME_COLOR_PRESETS` as swatch buttons with `data-maho-identifier="space.theme.swatch.{i}"`. |
| `WaveDecoration` | 363 to 381 | Decorative SVG only. |
| `GrainDial` | 383 to 404 | Hidden range input (0 to 100) styled as a dial, `data-maho-identifier="space.theme.grainDial"`. |

Mount: `document.querySelector('maho-space-create')` then `createRoot(...).render(<SpaceCreateApp ...>)` (lines 406 to 416).

### 2.2 Old `SpaceCreateState` shape

File: `maho-chromium/browser/resources/maho_space_create/react/types.ts`.

- `ThemeMode = 'solid' | 'zen'` (two modes only, line 3).
- `SpaceCreateState` fields: `spaceName`, `profiles`, `selectedProfileId`, `selectedColorIndex`, `mode`, `selectedVisualMode`, `grain`, `brightness`, `themeEditorEngaged`, `isCreating`, `errorMessage`.
- Defaults via `createInitialState()`: `selectedColorIndex=7`, `mode='zen'`, `selectedVisualMode='zen'`, `grain=0`, `brightness=90`. These are the seeds Phase 5 must reuse when porting create-mode (per plan section 100 invariant).

### 2.3 Old `SpaceCreateStore` actions and page-handler calls

File: `maho-chromium/browser/resources/maho_space_create/react/store.ts`.

| Store action | Lines | Page-handler call | Effect |
|--------------|-------|-------------------|--------|
| `bootstrap()` | 44 to 51 | `pageHandler.getProfiles()` | Loads profile list, defaults selected profile to `isDefault`. |
| `setSpaceName(name)` | 53 to 57 | none | Local state only. Drives Create button disabled state and preview label. |
| `setSelectedProfileId(id)` | 59 to 63 | none | Local. |
| `selectColor(index)` | 65 to 72 | none | Local. Also resets brightness to preset default and sets `themeEditorEngaged=true`. |
| `shiftColor(delta)` | 74 to 79 | none | Wraps modulo preset count; calls `selectColor`. |
| `setBrightness`, `stepBrightness` | 81 to 91 | none | Local, clamp [15,100]. |
| `setGrain` | 93 to 98 | none | Local. |
| `setMode(mode, visualMode)` | 100 to 106 | none | Local. |
| `buildPreviewColors`, `getColorHex`, `buildThemeJson` | 108 to 147 | none | Pure helpers. `buildThemeJson` emits `{type:"solid",color:{...}}` or `buildZenTheme(...)` depending on mode. |
| `createSpace()` | 149 to 176 | `pageHandler.createSpace(name, colorHex, DEFAULT_ICON='⊞', profileId or null, themeJson, null)`, then on success `pageHandler.closeDialog()` | Sets `isCreating`, surfaces `errorMessage` on failure. |
| `closeDialog()` | 178 to 180 | `pageHandler.closeDialog()` | Cancel/close path. |

There is **no** `previewTheme` and **no** `revertPreview` call in the old store. The old WebUI did not drive a preview lifecycle through the page handler at all.

---

## 3. Old `MahoSpaceCreatePageHandler` (C++)

Files:
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

| Method | Lines (.cc) | C++ side effect |
|--------|-------------|-----------------|
| `GetProfiles(cb)` | 168 to 174 | Posts `BuildProfilesOnPool` to a thread-pool task. Reads `maho_core_list_profiles` JSON, builds `mojom::ProfileInfo` list. |
| `GetSpaceCount(cb)` | 176 to 183 | Posts `CountSpacesOnPool`. Reads `maho_core_get_space_view_models` and returns list size. |
| `CreateSpace(name, color, icon, profile_id?, theme?, theme_json?, cb)` | 185 to 216 | Posts `CreateSpaceOnPool`. That helper (lines 89 to 152) calls `maho_core_create_space`, parses response id, calls `maho_core_activate_space`, then if a theme JSON is present dispatches a `kind=update_space_config` event via `maho_core_handle_event`. On reply, registers the space via `MahoSpaceProfileBridge::RegisterSpace`, calls `MahoSpaceThemeState::UpdateFromCore`, and resets `ColorProviderManager` cache. |
| `CloseDialog()` | 218 to 222 | `web_contents_->ClosePage()`. |

**Preview / revert verbs:** none. The old surface had no apply-preview / revert-preview symmetry. This is the gap that Phase 2 of the plan formalizes on the shared handler. Section 100 of the plan calls out that defaults must be ported verbatim, but it does NOT promise to back-port preview lifecycle to the old handler. The shared handler already has it (see section 4).

---

## 4. Shared `maho_space_config` page handler today

Files:
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc`

Methods (header lines 39 to 57):

| Method | Notes |
|--------|-------|
| `GetSpaceInfo(cb)` | Create-mode short-circuits to a synthetic empty `SpaceInfo` carrying `draft_theme_json_`. Edit-mode posts `LoadSpaceOnPool` (.cc 304 to 336). |
| `GetProfiles(cb)` | Same shape as old handler. |
| `UpdateName(name, cb)` | Edit-mode rename via `maho_core_rename_space`. |
| `UpdateIcon(icon, theme_json?, cb)` | Edit-mode. Re-applies theme alongside icon. |
| `UpdateTheme(theme_json, color_hex, cb)` | **Mode-split** (.cc 381 to 408). Create-mode calls `MahoSpaceConfigDialog::InvokeCreateModeThemeChosenCallback(theme_json)` and returns `true` without touching core. Edit-mode posts `ApplyThemeOnPool` and updates theme state + color cache on reply. |
| `UpdateColor(color_hex, theme_json?, cb)` | Edit-mode recolor wrapper. |
| `UpdateProfile(profile_id?, cb)` | Calls `MahoSpaceProfileBridge::RegisterSpace`. |
| `PreviewTheme(theme_json, cb)` | Create-mode applies override directly (.cc 452 to 462). Edit-mode posts `IsActiveSpaceOnPool` and only applies if currently active. |
| `RevertPreview(cb)` | Always clears override, refreshes theme state, resets color cache (.cc 491 to 497). |
| `CloseDialog()` | Delegates to `MahoSpaceConfigDialog::CloseActiveDialog()`. |

Destructor (.cc 296 to 302) clears any stale preview override on teardown, which is the foundation Phase 2 hardens.

There is **no `CreateSpace` mojo verb** on the shared handler. Production space creation goes through the native `MahoSidebarCreateSpaceView::CreateSpaceOnPool` (see `maho_sidebar_create_space_view.cc` 958 to 1011). Phase 5 of the plan (sections 269 to 270) requires any new create-mode mojo verbs to map 1:1 onto an old `MahoSpaceCreatePageHandler` method, named under the shared convention. If Phase 5 keeps creation in the native View, no new verb is needed. If Phase 5 moves creation into the WebUI, a `CreateSpace` verb must be added with semantics matching old `CreateSpace` plus its post-create side effects (`RegisterSpace`, `UpdateFromCore`, color-provider reset).

---

## 5. Shared `maho_space_config` React surface today

File: `maho-chromium/browser/resources/maho_space_config/react/app.tsx` (797 lines).

Top-level routing (lines 53 to 148) reads `state.isCreateMode` from `SpaceConfigStore` and renders one of two roots:

- `EditModeView` (150 to 273). Owns the existing edit UI: name input, icon input, color settings card, mode tabs (`solid` | `zen` | `gradient`), grain slider, brightness slider, gradient harmony controls, profile pill, Cancel + Save.
- `CreateModeView` (275 to 323) and its panels:
  - `CreateLeftPanel` (325 to 401). Illustration, **title `"Choose a Theme"`**, subtitle, theme summary row, copy block, primary CTA `"Choose Theme"` (line 390), Cancel.
  - `CreateRightPanel` (403 to 524). Preview canvas with hard-coded `<div className="scfg-create-preview-label">Space</div>` (line 455), brightness +/- with range, swatch palette + chevrons, wave, grain dial.
  - Helper components `CreateModeTabs`, `CreateColorPalette`, `CreateWaveDecoration`, `CreateGrainDial`.

Notably absent from `CreateLeftPanel`:
- No Space-name `<input>`.
- No Profile `<select>`.
- No bound preview label tied to a `spaceName` field.

Store (`maho-chromium/browser/resources/maho_space_config/react/store.ts`):

- `bootstrap()` reads `mode=create` from `window.location.search`; create-mode skips `loadProfiles()` (line 260 to 262).
- Preview pipeline: every setter calls `schedulePreview()` (lines 415 to 426), which debounces 120ms and calls `pageHandler.previewTheme(buildThemeJson())`.
- `save(selectedProfileId)` create-branch (lines 432 to 437) calls `pageHandler.updateTheme(themeJson, colorHex)` then `closeDialog()`. Edit-branch fans out to up to four mojo calls.
- `cancel()` (478 to 485) clears any pending preview timer, calls `pageHandler.revertPreview()`, then `closeDialog()`.

Defaults differ from old surface: shared store seeds `mode='solid'`, `selectedVisualMode='solid'` for non-create, but flips to `mode='zen'`, `selectedVisualMode='zen'` when `isCreateMode` (lines 167, 172). This matches old defaults for create. Phase 5 should keep this alignment.

---

## 6. Mapping table: old `maho_space_create` to shared `maho_space_config` create-mode

| Old surface element (file:lines) | Shared create-mode counterpart | Status / gap | Phase to fix |
|----------------------------------|--------------------------------|--------------|--------------|
| Hero illustration `LeftPanel` (app.tsx 125 to 129) | `CreateLeftPanel` `scfg-create-illustration` (app.tsx 344 to 352) | Matched structurally. Visual parity unverified pending Image1. | Phase 6 |
| Title `"Create a Space"` (app.tsx 131) | Title `"Choose a Theme"` (app.tsx 354) | **Differs intentionally.** Shared dialog is theme-only. Conflict, see section 7. | Phase 5 decision |
| Subtitle (app.tsx 132 to 135) | Subtitle (app.tsx 355 to 358) | Matched. | Phase 6 polish |
| Space name input `data-maho-identifier="space.create.name"` (app.tsx 137 to 147) | None in `CreateLeftPanel` | **Gap.** Lives in native `MahoSidebarCreateSpaceView` instead. Conflict, see section 7. | Phase 5 decision |
| Profile pill `<select>` (app.tsx 149 to 166) | None in `CreateLeftPanel` (edit-mode has it at app.tsx 232 to 252) | **Gap.** Native view also has a profile pill placeholder; production resolves profile via `MahoSpaceProfileBridge`. | Phase 5 decision |
| Theme summary row with preview dot (app.tsx 168 to 180) | `CreateLeftPanel` summary row (app.tsx 360 to 372) | Matched. | Phase 6 polish |
| Preview canvas with `state.spaceName \|\| 'Space'` label (app.tsx 247 to 249) | `CreateRightPanel` hard-coded `Space` label (app.tsx 455) | **Gap.** Bound label is missing because there is no `spaceName` in shared create state. Depends on name-input decision. | Phase 5 decision |
| Mode tabs `space.theme.mode.{0,1,2}` (app.tsx 314 to 338) | `CreateModeTabs` with same identifiers (app.tsx 673 to 697) | Matched. | None |
| Brightness +/- and range, identifiers `space.theme.luminosity.{minus,plus}` (app.tsx 254 to 279) | `CreateRightPanel` (app.tsx 460 to 489) with same identifiers | Matched. Test `CreateThemeSharedDialogBrightnessStepperInteraction` exercises this end-to-end. | None |
| Swatch palette + chevrons `space.theme.chevron.{left,right}`, `space.theme.swatch.{i}` (app.tsx 281 to 302, 340 to 361) | `CreateRightPanel` + `CreateColorPalette` (app.tsx 491 to 514, 699 to 720) | Matched. | None |
| Grain dial `space.theme.grainDial` (app.tsx 304 to 307, 383 to 404) | `CreateGrainDial` (app.tsx 742 to 769) | Matched. | None |
| Primary CTA `Create Space` disabled until name (app.tsx 184 to 190) | `Choose Theme` button (app.tsx 385 to 391) | **Gap, intentional.** Shared dialog only commits a theme; space creation runs in native view's `OnCreatePressed`. | Phase 5 decision |
| Cancel button (app.tsx 192 to 194) | `Cancel` (app.tsx 393 to 398) | Matched. Cancel routes through store `cancel()` which calls `revertPreview()` + `closeDialog()`. | None |
| `pageHandler.createSpace(...)` (store.ts 149 to 176) | None in shared handler. Native `MahoSidebarCreateSpaceView::CreateSpaceOnPool` instead. | **Gap.** Phase 5 must decide whether to add a shared-handler `CreateSpace` verb or keep creation in the native view. | Phase 5 |
| `pageHandler.closeDialog()` | `pageHandler.closeDialog()` -> `MahoSpaceConfigDialog::CloseActiveDialog()` | Matched. | None |
| Old preview lifecycle | None (old handler had no preview verbs) | Shared surface is **stronger** here. `previewTheme` + `revertPreview` exist with debounce and explicit cancel cleanup. Phase 2 hardens edge cases (esc, OS close, double-cancel). | Phase 2 |

---

## 7. Open parity / spec disputes (do NOT resolve in Phase 1)

### 7.1 Should shared `maho_space_config` create-mode include the Space name input?

- Old `maho_space_create/react/app.tsx` lines 137 to 147 had a Space-name `<input>` inside the same React app, with `autoFocus`, `data-maho-identifier="space.create.name"`, and a `state.spaceName || 'Space'` binding into the preview label (lines 247 to 249).
- Current shared `maho_space_config/react/app.tsx` `CreateLeftPanel` (lines 325 to 401) has no name input. The preview label is hard-coded `Space` (line 455). The shared store has no `spaceName` field in create-mode initialization.
- Current production routes the name input through the native `MahoSidebarCreateSpaceView` (Views) and treats the shared dialog as a theme-only step. The interactive tests reflect this (`SetNameForTesting` on the native view, theme JSON via `InvokeCreateModeThemeChosenCallback` on the dialog).

This is an unresolved dispute, not a bug. Recording it for Phase 5 to decide:

- **Option A:** Port the name input into shared `CreateLeftPanel` to match old WebUI parity literally. Removes the native split for name entry. Requires either re-introducing a `CreateSpace` verb on the shared handler or proxying the typed name back into the native view that still owns the FFI call.
- **Option B:** Keep shared create-mode theme-only, treat the missing name input as **expected divergence** from old WebUI, and rewrite Image1 expectations accordingly. Requires updating the plan's "structural parity" claim in Phase 5 (sections 262 to 263 list "Space name input" as required parity surface).

Phase 1 does NOT pick a winner. Phase 5 owns the decision, and the choice cascades into Phase 6 (visual parity scope) and Phase 7 (which tests are golden-worthy).

### 7.2 Profile selection symmetry

Same shape as 7.1. Old WebUI owned the profile pill; current production owns it natively in `MahoSidebarCreateSpaceView` (header line 98). Resolve alongside 7.1 for consistency.

### 7.3 `CreateSpace` mojo verb on the shared handler

If 7.1 lands as Option A, the shared handler will need a `CreateSpace(name, color, icon, profile_id?, theme?, theme_json?, cb)` verb whose semantics match `MahoSpaceCreatePageHandler::CreateSpace` plus the post-create side effects (`RegisterSpace`, `UpdateFromCore`, color-provider reset). Plan section 269 already commits to "no new mojo verbs that did not exist in the old handler", so the verb is permitted only if it has a 1:1 old counterpart. The old `CreateSpace` qualifies.

---

## 8. Native inline create view: what stays, what is dead

File: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` and `.h`.

### 8.1 Live, used in production and tests

- `OnThemePressed()` -> `MahoSpaceConfigDialog::OpenForCreate(...)` (cc 1871 to 1897). This is the single path the user can reach today.
- `OnCreatePressed()` -> `CreateSpaceOnPool` (FFI) -> `OnSpaceCreated` (cc 1857 to 1869, 1987 to 1999). This owns space creation in production.
- `ApplyThemeJson(theme_json)` (cc 1972 to 1985). Called from the `OpenForCreate` callback when the WebUI accepts a theme. Sets `overridden_theme_json_` and updates the theme summary label to `u"Custom theme"` plus the preview dot color.
- `BuildThemeJsonForTesting(selection)` (.h line 69, cc 1931 to 1934). Used by `ThemeDialogZenSelectionBuildsValidatorCompatibleJson` to validate JSON shape without opening a dialog. Pure helper, no picker dependency. Keep through Phase 9.
- Inline accessors used by tests: `name_field_for_testing`, `create_button_for_testing`, `error_label_for_testing`, `theme_button_for_testing`, `theme_summary_label_for_testing`, `SetNameForTesting`, `CompleteCreateSpaceForTesting` (.h 58 to 67). Live, used by Phase 7 successor tests.

### 8.2 Dead / unreached native picker symbols

These are still compiled and statically linked but no production code path reaches them. The interactive test file does **not** call them either (verified by grep on `maho_sidebar_spaces_interactive_test.cc`).

| Symbol | Source | Reachability today |
|--------|--------|--------------------|
| `MahoCreateSpaceThemePickerDialogView` | cc 1051 onward | Constructed only by code paths that no longer run; production now goes through `MahoSpaceConfigDialog::OpenForCreate`. |
| `g_theme_picker_widget` (file-static) | cc 217 | Only reset inside the dead picker's `OnWidgetDestroying` (cc 1396 to 1406). |
| `g_theme_picker_dialog_view` (file-static) | cc 220 | Same as above. |
| `MahoSidebarCreateSpaceView::GetThemePickerWidgetForTesting` | .h 68, cc 1925 to 1928 | Static, returns `g_theme_picker_widget`. No test reference. |
| `MahoSidebarCreateSpaceView::GetThemePickerSelectionForTesting` | .h 71, cc 1936 to 1943 | Static. No test reference. |
| `MahoSidebarCreateSpaceView::SetThemePickerSelectionForTesting` | .h 73, cc 1945 to 1951 | Static. No test reference. |
| `MahoSidebarCreateSpaceView::AcceptThemePickerDialogForTesting` | .h 75, cc 1953 to 1963 | Static. No test reference. |
| `ThemePickerContentsView`, `ThemeCanvasView`, `ThemeWaveView`, `ThemeDialView`, `ThemeDialControlView`, `DashedIconPlaceholderView`, `CreateSpaceHeroIllustrationView` | cc 229 to 956 | Auxiliary classes for the dead picker. Some may be reused by other Views surfaces; verify before deleting in Phase 9. |
| Constants `kColorPresets`, `kSwatchSize`, `kThemeChip*`, `kChooser*`, `kDial*`, `kCanvas*`, `kWave*` (cc 96 to 226) | Source for dead-picker geometry. Some leak into `BuildSkColorForSelection`, `BuildThemeJsonForSelection`, `ExtractSkColorFromThemeJson`, which **are** used by the live `BuildThemeJsonForTesting` and `ApplyThemeJson` paths. Do not delete blindly. |

Phase 3 task: stop tests from referencing **any** of the dead picker statics. The current test file already does not, so Phase 3 is largely a verification task plus a grep gate. Phase 9 owns the actual symbol deletion.

---

## 9. Interactive test surface (Phase 7 input)

File: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc` (755 lines).

Tests already aligned with the shared dialog (these are the **pattern** Phase 7 should extend):

| Test | Lines | What it asserts |
|------|-------|-----------------|
| `FooterActionOpensSpacesSurface` | 159 to 187 | Plus button click opens either a navigation to `chrome://maho-space*` or the popover. |
| `CreateSpaceOpensInlineSidebarSurface` | 189 to 211 | After plus -> New Space, sidebar mounts `MahoSidebarCreateSpaceView` full-bleed and hides the tabs surface, top bar, and footer. |
| `CreateSpaceThemeRowOpensSharedConfigDialog` | 213 to 248 | Clicking the theme button opens `MahoSpaceConfigDialog` in `ActiveMode::kCreate`. The create form stays mounted under the dialog. `CloseActiveDialog` tears down cleanly. |
| `CreateSpaceEscapeReturnsToTabs` | 250 to 283 | Esc on the inline create view returns to the tabs surface and re-shows the footer. |
| `CreateSpaceSubmitSucceedsAndClosesSurface` | 285 to 349 | Theme accept via `InvokeCreateModeThemeChosenCallback`, then name set, then `CompleteCreateSpaceForTesting(true, ...)` -> bridge gains a registered space and active space switches. |
| `CreateSpaceFailureShowsInlineErrorAndPreservesName` | 351 to 376 | `CompleteCreateSpaceForTesting(false, ...)` keeps the form mounted, shows `error_label_`, preserves the typed name. |
| `CreateSpaceWhitespaceOnlyNameKeepsCreateDisabled` | 378 to 398 | Name validation gates the Create button. |
| `ThemeDialogZenSelectionBuildsValidatorCompatibleJson` | 400 to 458 | Pure JSON-shape assertion via `BuildThemeJsonForTesting`. Validates `type`, `harmony`, `opacity`, `texture`, `gradientColors[*].isPrimary/isCustom/hue/saturation/brightness`. Phase 7 should extend this for the **shared** create-mode JSON output too. |
| `CreateThemeButtonOpenSharedDialogInCreateMode` | 466 to 501 | Same as 213 to 248 but the slice-2 named test. |
| `CreateThemeSharedDialogAcceptUpdatesCreateView` | 505 to 556 | Accept callback sets summary to `u"Custom theme"`. |
| `CreateThemeSharedDialogCancelPreservesCreateView` | 560 to 607 | Cancel does NOT change summary or close the form. |
| `CreateThemeSharedDialogBrightnessStepperInteraction` | 613 to 716 | Drives the WebUI brightness-plus button via `EvalJs` against `data-maho-identifier="space.theme.luminosity.plus"`, asserts the range input value increments by 5 (clamped to 100), and confirms the dialog stays open. This is the canonical pattern Phase 7 should follow for further WebUI-driven assertions: locate the input by `data-maho-identifier`, click via `ExecJs`, read state via `EvalJs`. |

Phase 7 routing recommendation: new create-mode goldens and behavior tests should live alongside this file and follow the same `OpenCreateSpaceSurface` -> click theme -> `RunUntil` on `MahoSpaceConfigDialog::GetWidgetForTesting()` -> `EvalJs/ExecJs` shape, then either `InvokeCreateModeThemeChosenCallback` (accept) or `dialog_widget->Close()` (cancel). The dead-picker statics in section 8.2 must NOT be reintroduced; if a behavior is observable only through them today, expose a minimal `TestObserver` on `MahoSpaceConfigDialog` instead (per plan section 199).

---

## 10. Phase routing summary

| Phase | Inputs from this index |
|-------|------------------------|
| 2 (preview lifecycle) | Section 4 (current preview/revert verbs and destructor cleanup) is the starting point. Section 3 confirms the old handler had nothing to back-port; symmetry must be **added**, not copied. |
| 3 (test migration) | Section 8.2 enumerates dead picker symbols. Section 9 confirms the test file already avoids them; Phase 3 is mostly a grep gate plus any new behavior currently tested only via the dead picker (none found in the current test file). |
| 4 (React split) | Section 5 documents the split that already exists in `app.tsx` lines 53 to 148. Phase 4 should consider this split present and focus on the mojom `mode` plumbing if it is currently inferred rather than set. |
| 5 (create-mode port) | Sections 2, 3, 6, 7. Open disputes in section 7 must be resolved here. |
| 6 (visual parity) | Blocked on section 0 (Image1). |
| 7 (tests + goldens) | Section 9 is the pattern. Section 2.1 lists the `data-maho-identifier` hooks that goldens and EvalJs locators should target. |
| 9 (cleanup) | Section 8.2 lists the deletion candidates. Verify reuse of helpers like `BuildSkColorForSelection` and `ExtractSkColorFromThemeJson` before removing surrounding code.

---

## 11. Verification status of this index

- All file paths resolved against the working tree at this index's commit.
- All line ranges spot-checked.
- Image1 absence confirmed by directory listing of `.sisyphus/plans/`.
- No production code or test files were modified to produce this index.
- Phases 2 to 7 are NOT marked complete; this index only describes inputs.
- Section 7 disputes are recorded as open; nothing here resolves them.
