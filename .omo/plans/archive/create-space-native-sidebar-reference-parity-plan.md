# Implementation Plan: Native Create Space Sidebar — Visual Parity with Reference Image (Revised)

## 1. Goal Statement

Restructure the constructor and layout of `MahoSidebarCreateSpaceView` (in `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc/.h`) so the native Chromium Views surface matches the narrow dark single-column reference image. The view is hosted in two places — the inline sidebar and a dialog wrapper — and both must remain functional. No functional changes to icon selection, profile selection, or theme selection flows.

---

## 2. Assumptions & Decisions

### A1: Shared class, shared composition, no mode enum (DEFAULT — finalize in 0B)
Default to one shared `MahoSidebarCreateSpaceView` class with one shared composition. Do NOT introduce an internal `kSidebarColumn` vs `kDialogWide` mode enum unless Phase 0B disproves current evidence. Current evidence says `MahoSpaceCreateDialog::Open` has zero call sites and the `space_create` dialog files are not wired into any `BUILD.gn` target, so a mode split would be paying complexity for a phantom branch. **Decision checkpoint:** if Phase 0B reproduces that the dialog host is effectively dead/inert, keep the shared composition; if Phase 0B finds a real built production caller, re-open A1 before Phase 1 and decide whether the dialog host can accept the same narrow composition at 456dp.

### A2: Dashed-plus icon area = non-functional visual placeholder (DECIDED)
The reference shows a dashed-square icon area. The FFI `maho_core_create_space(name, color_json, profile_id)` does not accept an icon parameter. Rust create path sets `icon: None`; icon is post-create only. **There is no create-time icon data path today.** The placeholder will be purely visual — no click handler, no data flow. Follow-up task required for functional icon picking (requires FFI signature change + Rust-side changes).

### A3: Profile pill = static non-functional visual element (DECIDED)
The reference shows a "Default" pill with dropdown chevron. The native sidebar does NOT call `maho_core_list_profiles` today. A real profile dropdown requires: (a) calling `maho_core_list_profiles` from C++, (b) holding selected profile state in the view, (c) passing selected profile to create. FFI backend support exists but the native sidebar has never wired it. The pill will display static "Default" text with rounded styling, no dropdown behavior. Follow-up task required.

### A4: Hero illustration approach = gated decision (DEFERRED TO PHASE 0)
The reference shows a small stacked-cards illustration. Two valid approaches exist:
- **Option A**: Custom `OnPaint` drawing overlapping rounded rects (~30 lines)
- **Option B**: `FillLayout` + layered child views with `SetPaintToLayer()` (standard Chromium pattern for overlapping compositions)

Phase 0 spike will evaluate which approach is cleaner given existing sidebar paint patterns. The plan sequences both options but defers the final choice to the spike output.

### A5: Error label position unchanged (DECIDED)
Error label stays between the Create button and Cancel button — identical to its current position. No relocation. Test accessors (`error_label_for_testing()`) continue to work without any structural changes to the error label's parent or position in the view tree.

### A6: Theme picker test hooks reference dead code — explicit gate required (DECIDED)
`GetThemePickerWidgetForTesting()`, `SetThemePickerSelectionForTesting()`, and `AcceptThemePickerDialogForTesting()` exercise the in-file `MahoCreateSpaceThemePickerDialogView` class. The live runtime path goes through `MahoSpaceConfigDialog::OpenForCreate()`. **Existing green theme-picker tests do NOT validate the live runtime theme flow.** Phase 0 must reconcile this mismatch before any layout refactor, to ensure we don't ship a refactor that passes tests but breaks live theme selection.

### A7: Bottom pagination dots — evaluate `views::DotIndicator` (DEFERRED TO PHASE 0)
Standard Chromium `views::DotIndicator` exists. Phase 0 spike will assess whether it's suitable for passive pagination dots or whether the current manual dot construction is simpler to keep.

### A8: Title and subtitle text changes (DECIDED)
- Title: `Create a new space` → `Create a Space`
- Subtitle: `Set up a focused home for tabs, notes, and future personalizations.` → `Separate your tabs for life, work, projects, and more.`

---

## 3. File-by-File Change List

### `maho_sidebar_create_space_view.h`
- Add member: `raw_ptr<views::View> icon_placeholder_ = nullptr;`
- Add member: `raw_ptr<views::View> profile_pill_ = nullptr;`
- Optionally add: `views::View* icon_placeholder_for_testing()` (if tests need it; Phase 0 audit determines)
- All existing test accessors (`name_field_for_testing`, `create_button_for_testing`, `error_label_for_testing`, `theme_button_for_testing`, `theme_summary_label_for_testing`) remain unchanged
- All existing static test hooks (`GetThemePickerWidgetForTesting`, `BuildThemeJsonForTesting`, `GetThemePickerSelectionForTesting`, `SetThemePickerSelectionForTesting`, `AcceptThemePickerDialogForTesting`) remain unchanged

### `maho_sidebar_create_space_view.cc`

#### Anonymous namespace additions:
- **`DashedIconPlaceholderView`**: Custom-painted `views::View`, dashed-border rounded rect via `SkDashPathEffect::Make` with centered `+` glyph. 44×44dp. This IS net-new custom paint (no in-repo dashed-border helper exists).
- **Hero illustration view** (name TBD by Phase 0 spike):
  - If custom paint: `CreateSpaceHeroIllustrationView` with `OnPaint` drawing 2-3 overlapping rounded rects
  - If layered composition: helper function returning a `FillLayout`-based view with layered `SetPaintToLayer()` children
- New layout constants for: name row gap, profile pill height/radius, hero illustration size, profile row insets

#### Constructor restructure (the bulk):
1. **Hero section**: Replace 48px badge (`kHeroBadgeSize`) with hero illustration view, centered
2. **Title label**: Text → `Create a Space`, style unchanged (`STYLE_HEADLINE_4`)
3. **Subtitle label**: Text → `Separate your tabs for life, work, projects, and more.`, style unchanged (`STYLE_BODY_5`)
4. **Name row**: Replace bordered name card with flat horizontal `BoxLayout` row:
   - Left: `DashedIconPlaceholderView` (stored as `icon_placeholder_`), no click handler
   - Right: existing `name_field_` textfield with placeholder `Space name...`, flex=1
   - Row gap ~12dp, no outer border, no outer background
5. **Profile row**: Replace `CreateInfoCard(card_stack, u"☺", ...)` with compact horizontal row:
   - Left: `Profile` label (`STYLE_BODY_3_MEDIUM`)
   - Flex spacer
   - Rounded pill view: `ui::kColorSysSurface3` background, ~14dp corner radius, `Default` label inside with ~12dp horizontal padding (stored as `profile_pill_`), no click handler
   - Info glyph: `ⓘ` label (`STYLE_BODY_5`, `ui::kColorSysOnSurfaceSubtle`)
6. **Theme row**: Simplify `theme_button_` internal layout:
   - Remove outer card background/border
   - Keep `theme_button_` as `MdTextButton` (preserves `theme_button_for_testing()`)
   - Content: `Choose a Theme` label + flex spacer + `theme_preview_dot_` + `›` chevron
   - `theme_summary_label_` MUST remain a real, visible child of the simplified theme row. It should continue to show the current theme summary with subtle styling so `theme_summary_label_for_testing()` maps to visible UI, not a hidden test-only artifact.
7. **Bottom region**: Structure unchanged:
   - Create button (full-width, prominent, disabled when name empty)
   - Error label (same position — between create button and cancel)
   - Cancel button (centered)
   - Pagination dots (3 dots, first active)

#### `OnThemeChanged()`
Minor updates for new view backgrounds if they use color provider colors.

#### Theme picker dialog code (lines 460-1446)
**NO CHANGES** — all dead `MahoCreateSpaceThemePickerDialogView` code, `g_theme_picker_widget`, and `g_theme_picker_dialog_view` remain untouched.

#### Test hook implementations (lines 1823-1875)
**NO CHANGES**

### `maho_space_create_dialog.cc`
- **NO CHANGES** — dialog host wraps `MahoSidebarCreateSpaceView` unchanged

### `maho_sidebar_view.cc`
- **NO CHANGES** — instantiation and mode switching unchanged

### `maho_sidebar_spaces_interactive_test.cc`
- Update text assertions if any check old title/subtitle strings
- Verify `theme_summary_label_for_testing()` still returns a visible label with valid text that matches the on-screen row
- Verify `theme_button_for_testing()->GetBoundsInScreen().CenterPoint()` clicks still land on a valid clickable surface

### `maho_sidebar_actions_interactive_test.cc`
- Verify `is_create_space_mode_for_testing()` assertions still pass (no structural change expected)

### `BUILD.gn`
- **NO CHANGES** expected (no new files)

---

## 4. Ordered Execution Phases

### Phase 0: Gating Audit & Spikes (MUST complete before any code changes)

**Phase 0 is non-negotiable. It produces go/no-go decisions that gate all subsequent phases.**

#### 0A: Reconcile theme picker test coverage vs live runtime path [CRITICAL GATE]
- Read `maho_sidebar_spaces_interactive_test.cc` in full (~1300 lines)
- Catalog every test that calls `GetThemePickerWidgetForTesting()`, `SetThemePickerSelectionForTesting()`, `AcceptThemePickerDialogForTesting()`
- For each such test, determine: does it exercise the **dead** in-file `MahoCreateSpaceThemePickerDialogView` path, or the **live** `MahoSpaceConfigDialog::OpenForCreate()` path?
- Identify which tests actually click `theme_button_for_testing()` and trigger the real `OnThemePressed()` → `MahoSpaceConfigDialog::OpenForCreate()` flow
- **Output**: A table mapping each theme-related test → dead path / live path / both. Identify any coverage gap for the live path.
- **Decision gate**: If there is zero test coverage for the live `MahoSpaceConfigDialog::OpenForCreate` path triggered from the sidebar create view, note this as a pre-existing gap (not something this task introduces) but DO NOT let it block the refactor. If there IS coverage and it depends on view structure we're changing, plan specific mitigations. Regardless of outcome, explicitly document that green tests do **not** by themselves validate the live runtime theme path until this mismatch is resolved.

#### 0B: Verify dialog host status [GATE]
- Search codebase for all call sites of `MahoSpaceCreateDialog::Open()`
- Confirm whether there are zero external production call sites and whether the `space_create` dialog files are still absent from all `BUILD.gn` targets
- If confirmed dead / not built: dialog host gets smoke-test-level verification only (build + no crash if manually wired for validation), not pixel-perfect parity validation
- If any real built production call site is found: elevate dialog host to full parity validation **and** re-open A1 before Phase 1

#### 0C: Catalog all test accessor dependencies [GATE]
- Read `maho_sidebar_spaces_interactive_test.cc` and `maho_sidebar_actions_interactive_test.cc`
- For each test accessor used, record:
  - Which test(s) call it
  - What they assert about the returned view (text content, visibility, enabled state, bounds)
  - Whether the assertion depends on view hierarchy position or just the accessor returning non-null
- **Output**: Risk-rated list of assertions. Categories: SAFE (accessor-only), AT-RISK (text content may change), BREAKING (depends on old layout structure)

#### 0D: Hero illustration approach spike [DECISION SPIKE]
- Search codebase for existing uses of `SetPaintToLayer()` + `FillLayout` for overlapping view compositions in `browser/ui/views/sidebar/`
- Search for existing uses of custom `OnPaint` for decorative illustrations
- Evaluate `views::DotIndicator` suitability for bottom pagination dots
- **Output**: Recommended approach for hero (custom paint vs layered composition) with rationale. Recommended approach for dots (keep manual vs use `DotIndicator`).

**Phase 0 Delegation:**
- Fire 0A, 0B, 0C, 0D in parallel (all are read-only)
- Category for each: `quick`, Skills: `[]`
- All four must complete before Phase 1 begins

### Phase 1: Net-New View Classes (no behavior change, dead code only)

#### 1A: Implement DashedIconPlaceholderView
- Add class in anonymous namespace of `maho_sidebar_create_space_view.cc`
- Custom `OnPaint`: dashed-border rounded rect via `SkDashPathEffect::Make`, centered `+` glyph
- 44×44dp preferred size
- `METADATA_HEADER` + `BEGIN_METADATA`/`END_METADATA`
- **Not wired into constructor yet** — dead code at this point

#### 1B: Implement hero illustration view
- Approach determined by Phase 0D output
- Add class/helper in anonymous namespace
- **Not wired into constructor yet** — dead code at this point

**Phase 1 Delegation:**
- Fire 1A, 1B in parallel
- Category for each: `small-impl`, Skills: `[]`
- QA gate: Build compiles (`python3 maho-chromium/build/scripts/build_maho.py`), no test regressions (no behavior change)

### Phase 2: Constructor Restructure (the main refactor)

#### 2A: Restructure constructor layout
- Prerequisite: Phase 0 gates passed, Phase 1 views available
- Rewrite constructor per Section 3 spec above
- Add `icon_placeholder_` and `profile_pill_` members to `.h`
- Update `OnThemeChanged()` if new views need theme-aware colors
- **Critical constraint**: `theme_summary_label_` must remain in the view tree and return valid text from `theme_summary_label_for_testing()`
- **Critical constraint**: All 5 existing test accessors must return non-null after construction
- **Critical constraint**: Error label position unchanged

**Phase 2 Delegation:**
- Single task, sequential (depends on all Phase 0 + Phase 1 outputs)
- Category: `opus-impl`, Skills: `[]`
- QA gate: Build compiles, code inspection confirms all 5 test accessors remain reachable, and `maho_sidebar_spaces_interactive_test` plus `maho_sidebar_actions_interactive_test` are run immediately after the refactor. G2 passes only when both tests are green.

### Phase 3: Test Updates + Constant Cleanup (parallel)

#### 3A: Update interactive test assertions
- Only needed if the immediate Phase 2 test run exposes residual assertion drift that is not already fixed in the refactor commit
- Fix text assertions that reference old title (`Create a new space`) or old subtitle
- Verify `theme_summary_label_for_testing()` assertions still target a visible label whose text is set by `UpdateThemeSummary()`
- Verify no coordinate-based assertions broke (interactive tests using `GetBoundsInScreen().CenterPoint()` are position-independent — just need the target view visible)
- If Phase 0A identified at-risk theme-picker tests that depend on view structure: apply mitigations

#### 3B: Clean up unused constants
- Remove constants from `maho_sidebar_create_space_view.cc` no longer referenced after restructure
- Candidates: `kHeroBadgeSize`, `kHeroBadgeCornerRadius`, `kHeroBadgeInsets`, `kCardInsets`, `kCardCornerRadius` — IF no longer used by any code including the theme picker dialog (lines 460-1446)
- DO NOT remove constants still used by `MahoCreateSpaceThemePickerDialogView` or its helpers
- Verify with grep that each removed constant has zero remaining references in the file

**Phase 3 Delegation:**
- Fire 3A, 3B in parallel
- 3A category: `small-impl`, Skills: `[]`
- 3B category: `quick`, Skills: `[]`
- QA gate: Build compiles, all interactive tests pass

### Phase 4: Build + Runtime Verification

#### 4A: Full build and test run
- Build: `python3 maho-chromium/build/scripts/build_maho.py`
- Run interactive tests: `maho_sidebar_spaces_interactive_test`, `maho_sidebar_actions_interactive_test`
- Verify no regressions in `maho_sidebar_contract_test`

#### 4B: Sidebar host visual verification
- Open sidebar → click footer `+` → verify Create Space surface renders with new layout
- Verify: hero illustration visible, title/subtitle text correct, name row with dashed placeholder, profile pill visible, theme row clickable (opens `MahoSpaceConfigDialog`), create button disabled, cancel button visible, dots visible
- Capture a screenshot artifact of the sidebar host after verification and save it under `.sisyphus/artifacts/` for comparison against the reference image

#### 4C: Dialog host smoke test
- If Phase 0B confirmed dialog host has zero production call sites: verify build doesn't crash if `MahoSpaceCreateDialog::Open()` is called programmatically
- If Phase 0B found production call sites: full visual verification of dialog host

**Phase 4 Delegation:**
- Sequential: 4A → 4B → 4C
- Category: `unspecified-high`, Skills: `[]`
- QA gate: Build green, all tests pass, visual output matches reference in sidebar host

---

## 5. Verification Gates

| Gate | Phase | What | How | Blocking? |
|------|-------|------|-----|-----------|
| G0-THEME | 0A | Theme picker test/runtime reconciliation | Table mapping each test → dead/live path | YES — blocks Phase 2 |
| G0-DIALOG | 0B | Dialog host status | Grep for `MahoSpaceCreateDialog::Open` call sites + confirm `BUILD.gn` linkage state | YES — blocks A1 finalization and Phase 4C scope |
| G0-TESTS | 0C | Test accessor dependency catalog | Risk-rated assertion list | YES — blocks Phase 2 |
| G0-HERO | 0D | Hero approach decision | Spike output with recommendation | YES — blocks Phase 1B |
| G1-BUILD | 1 | New views compile | `python3 maho-chromium/build/scripts/build_maho.py` | YES — blocks Phase 2 |
| G1-NOREGRESS | 1 | No test regressions from dead code additions | Run interactive tests | YES — blocks Phase 2 |
| G2-ACCESSORS | 2 | All 5 test accessors remain reachable and focused interactive tests are green | Code inspection + immediate run of `maho_sidebar_spaces_interactive_test` and `maho_sidebar_actions_interactive_test` | YES — blocks Phase 3 |
| G2-BUILD | 2 | Constructor restructure compiles | Build | YES — blocks Phase 3 |
| G3-TESTS | 3 | All interactive tests pass | Run tests | YES — blocks Phase 4 |
| G4-VISUAL | 4 | Sidebar host matches reference | Visual inspection | FINAL |
| G4-DIALOG | 4 | Dialog host doesn't crash | Smoke test | FINAL |

---

## 6. Commit Strategy

**Atomic commits, ordered for safe bisectability:**

### Commit 1: `audit: document theme picker test coverage gap and dialog host status`
- Phase 0 outputs as code comments or in commit message body
- No code changes — documentation-only commit (optional; can be omitted if outputs are ephemeral)

### Commit 2: `feat(sidebar): add hero illustration and dashed icon placeholder views`
- Adds `CreateSpaceHeroIllustrationView` (or equivalent) and `DashedIconPlaceholderView` in anonymous namespace
- Dead code only — not wired into constructor
- Build passes, all tests pass, zero behavior change

### Commit 3: `refactor(sidebar): restructure Create Space view for visual parity`
- Constructor rewrite + `.h` member additions + title/subtitle text updates
- If test assertions require matching updates (e.g., old title text), include those updates in this same commit to maintain green-on-green
- This is the primary commit — all visual changes land here
- Build passes, all tests pass

### Commit 4: `cleanup(sidebar): remove unused Create Space layout constants`
- Removes dead constants verified to have zero references
- Build passes, all tests pass

If Commit 3 cannot pass tests without test updates, the test updates MUST be in the same commit (not a separate commit that leaves a broken intermediate state).

---

## 7. Risks & Mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| R1 | `theme_summary_label_` removed or hidden from the visible row → tests stay green but UI semantics drift | Medium | High | Constraint: label MUST remain visible in the row with valid text from `UpdateThemeSummary()`. Phase 0C audit identifies exact assertions. |
| R2 | Dead theme-picker test hooks pass but live runtime picker is still unvalidated post-refactor | Medium | High | Phase 0A gate explicitly reconciles the mismatch and documents that green tests are only structural coverage until the live `MahoSpaceConfigDialog::OpenForCreate()` path gets direct test coverage. |
| R3 | `SkDashPathEffect::Make` not available or behaves differently in Chromium's Skia fork | Very Low | Low | Skia dash effects are standard. Fallback: solid thin border with spaced gaps drawn manually. |
| R4 | Dialog host (456dp) renders new layout poorly vs sidebar host (~288dp) | Low | Low | Phase 0B confirms dialog host is likely inert. Phase 4C smoke-tests it. Both widths are narrow single-column. |
| R5 | Removing `kCardInsets`/`kCardCornerRadius` breaks theme picker dialog code that reuses them | Medium | Medium | Phase 3B explicitly greps for all references in the file before removal. Theme picker dialog uses `kThemeDialogSurfaceCornerRadius`, `kThemeCanvasCornerRadius`, etc. — distinct constants. |
| R6 | Hero illustration looks wrong at different DPI/scale factors | Low | Low | Use dp-relative sizing. Verify at 1x and 2x if possible. |
| R7 | Interactive tests that click `theme_button_` by `GetBoundsInScreen().CenterPoint()` miss because button changed shape | Very Low | Medium | `CenterPoint()` is position-independent — just needs the button visible and large enough. Theme button remains a full-width row. |

---

## 8. Out of Scope / Follow-ups

| Item | Why Out of Scope | Follow-up Required |
|------|-----------------|-------------------|
| Functional icon picker | No create-time icon data path exists. FFI `maho_core_create_space` doesn't accept icon. Rust sets `icon: None`. | FFI signature change + Rust deserialization + native UI wiring |
| Functional profile dropdown | Native sidebar doesn't call `maho_core_list_profiles`. Requires new C++ state management + FFI calls. | C++ profile list fetching + dropdown UI + selected profile state |
| Dead theme picker code removal | `MahoCreateSpaceThemePickerDialogView` and `g_theme_picker_widget` are likely dead but test hooks depend on them | Separate cleanup task: migrate test hooks to `MahoSpaceConfigDialog` path, then remove dead code |
| WebUI React surface alignment | `browser/resources/maho_space_create/` is a separate WebUI surface | Separate task if WebUI needs matching updates |
| Enter/exit animations | No animation in reference image spec | Future polish task |
| Dialog host pixel-perfect parity | Current evidence says zero production call sites and no `BUILD.gn` linkage; Phase 0B must reconfirm | Only if dialog host becomes active or is linked into the build |

---

## 9. Execution Instructions (for ultrawork)

### Wave 0 — Gating (all parallel, all read-only)
```text
Fire in parallel:
  0A: task(category="quick", load_skills=[], run_in_background=true)
      "Reconcile theme picker test coverage vs live runtime path..."
  0B: task(category="quick", load_skills=[], run_in_background=true)
      "Search for all call sites of MahoSpaceCreateDialog::Open..."
  0C: task(category="quick", load_skills=[], run_in_background=true)
      "Catalog all test accessor dependencies in interactive tests..."
  0D: task(category="quick", load_skills=[], run_in_background=true)
      "Evaluate hero illustration approach and DotIndicator suitability..."
```
**Block**: Collect all four outputs. Review. Finalize A1 based on 0B, then confirm go/no-go.

### Wave 1 — New Views (parallel, after Wave 0 go)
```text
Fire in parallel:
  1A: task(category="small-impl", load_skills=[], run_in_background=false)
      "Implement DashedIconPlaceholderView..."
  1B: task(category="small-impl", load_skills=[], run_in_background=false)
      "Implement hero illustration view (approach from 0D output)..."
```
**Gate G1**: Build compiles, no test regressions. Commit 2.

### Wave 2 — Main Refactor (sequential, after Wave 1 gate)
```text
  2A: task(category="opus-impl", load_skills=[], run_in_background=false)
      "Restructure MahoSidebarCreateSpaceView constructor..."
```
**Gate G2**: Build compiles, all 5 test accessors remain reachable, and the two focused interactive tests are green.

### Wave 3 — Tests + Cleanup (parallel, after Wave 2 gate)
```text
Fire in parallel:
  3A: task(category="small-impl", load_skills=[], run_in_background=false)
      "Update interactive test assertions..."
  3B: task(category="quick", load_skills=[], run_in_background=false)
      "Clean up unused constants..."
```
**Gate G3**: Build compiles, all tests pass. Commit 3 + Commit 4.

### Wave 4 — Verification (sequential, after Wave 3 gate)
```text
  4A+4B+4C: task(category="unspecified-high", load_skills=[], run_in_background=false)
      "Full build, run all interactive tests, verify both hosts..."
```
**Gate G4**: Build green, tests green, visual parity confirmed.
