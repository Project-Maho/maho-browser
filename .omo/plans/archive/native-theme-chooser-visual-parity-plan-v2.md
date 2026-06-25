# Native Theme Chooser Visual Parity Plan v2

## Status
- Draft implementation plan
- Scope: native chooser-only modal parity for the create-space theme dialog
- Out of scope: WebUI create-space flow, command-palette new-space path, unrelated remediation work

## Acceptance Contract
### Allowed
- standalone native modal
- existing create-space caller flow remains intact

### Required
- dialog contents are theme chooser only
- visual target is the chooser visual language of `maho/reference_screenshots/create-theme.png`
- final acceptance requires runtime screenshot, manual QA, build pass, and targeted test pass

### Forbidden
- dialog-internal left create-space form UI, including:
  - `Create a Space`
  - subtitle marketing copy
  - `Space name...`
  - `Profile`
  - left-side `Choose a Theme` card
  - left-side `Close` / `Use Theme`

### Important Interpretation Rule
- The reference screenshot may show a split composition, but the latest accepted scope override is stricter: preserve the chooser visual language while removing the left create-space form from the native modal.

## Assumed Interaction Model
- `Enter` accepts the current selection
- `Esc` or window close cancels the dialog
- preview changes remain local until accept
- if product semantics change, this plan must be updated before implementation

## In-Scope Files
### Primary
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`

### Secondary
- `maho-chromium/browser/ui/views/space_create/maho_space_create_dialog.cc`
- `maho-chromium/browser/ui/views/space_create/maho_space_create_dialog.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

### Validation / behavior-sensitive
- `maho-chromium/browser/ui/theme/maho_space_theme_io.cc`
- `maho-chromium/browser/ui/theme/maho_space_theme_state.cc`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

## Explicit Non-Goals
- `browser/resources/maho_space_create/*`
- `OpenSpaceCreateSurface()` WebUI path
- `maho_command_action_handler.cc` new-space path
- `maho_sidebar_layout_tokens.h`

## Phase 0 — Semantics Gate
### Goal
- lock the chooser-only interaction model before code edits

### Required decisions
- confirm that left-panel action buttons are not returning in the native modal
- confirm that accept/cancel behavior is keyboard/window driven unless product explicitly requests footer buttons

### Done criteria
- one accepted interaction model is written down and used consistently in implementation and tests

## Phase 1 — Structural Scope Correction
### Goal
- convert `MahoCreateSpaceThemePickerDialogView` from a split create-space/theme dialog into a single-surface chooser dialog

### Required changes
- remove the entire left-panel subtree from the dialog constructor:
  - `left_panel`
  - `ThemeHeroBadgeView`
  - title/subtitle labels
  - `name_card`
  - `profile_card`
  - inner dialog `theme_button_`
  - `left_spacer`
  - `action_row`
  - `dismiss_button_`
  - `apply_button_`
- replace the split `shell` composition with a chooser-only root container
- delete `kThemeDialogLeftPanelWidth`
- recompute `kThemeDialogWidth` for chooser-only composition
- keep the chooser elements:
  - `preview_view_`
  - chip row
  - message row
  - brightness controls
  - swatch row and chevrons
  - `wave_view_`
  - `grain_dial_view_`

### Cleanup requirements
- remove left-panel-only dialog members:
  - inner `theme_button_`
  - `theme_summary_label_`
  - `theme_preview_dot_`
  - `dismiss_button_`
  - `apply_button_`
- remove dead `UpdateUi()` branches for those members
- delete `OnThemePressed()` if it becomes unused
- delete `ThemeHeroBadgeView` if it no longer has call sites

### Done criteria
- dialog source contains no split-layout create-space form subtree
- chooser dialog body is the sole visible content

## Phase 2 — Default State and Reopen Behavior
### Goal
- make first-open and reopen state deterministic and reference-aligned

### Required changes
- reconcile `MahoSidebarCreateSpaceThemeSelection` defaults with `DefaultThemeSelection()`
- define one source of truth for:
  - `preset_index`
  - `brightness`
  - `grain`
  - `mode`
- specify reopen behavior:
  - brand-new create-space flow uses default selection
  - reopened chooser reflects the current create-space selection
- ensure cancel leaves outer create-space summary unchanged
- ensure accept updates outer selection and summary exactly once

### Done criteria
- first-open state and reopen state can be explained without ambiguity
- cancel and accept paths are behaviorally distinct and testable

## Phase 3 — Chooser Visual Rebuild
### Goal
- make the remaining chooser read as a standalone native modal rather than the right half of a removed split layout

### Required tuning surface
#### Layout constants
- `kThemeDialogWidth`
- `kThemeDialogSurfaceCornerRadius`
- `kThemeChipSize`
- `kThemeCanvasHeight`
- `kThemePreviewControlSize`
- `kThemeChevronButtonSize`
- `kSwatchSize`
- `kThemeGrainDialSize`
- `kThemeCanvasDotSpacing`

#### Insets and spacing
- outer content padding
- chooser shell padding
- canvas insets
- chip spacing
- brightness row spacing
- swatch rail and chevron spacing
- wave/dial lower-row spacing

#### Color helpers
- `GetThemeDialogShellColor()`
- `GetThemeDialogRightPanelColor()`
- `GetThemeDialogCardBorderColor()`
- `GetThemeDialogSoftTextColor()`
- `GetThemeDialogMutedStrokeColor()`
- `GetThemeCanvasBackgroundColor()`

#### Custom painters
- `ThemeCanvasView`
- `ThemeWaveView`
- `ThemeDialView`

### Done criteria
- chooser silhouette, density, and control hierarchy plausibly match the intended reference language
- layout no longer looks like a surviving right-side subpanel

## Phase 4 — Persisted Theme and Tint Consistency
### Goal
- ensure the accepted chooser state matches persisted theme output and post-create tint behavior

### Required review surface
- `BuildSkColorForSelection()`
- `BuildThemeJsonForSelection()`
- `BuildThemeSummaryForSelection()`
- `CreateSpaceOnPool()`
- `ApplyThemeJsonToSpace()`
- `MahoSpaceThemeState::UpdateFromCore()`
- `ui::ColorProviderManager::Get().ResetColorProviderCache()`
- `maho_color_mixer.cc` tint application

### Required decisions
- document whether `kZen` remains preview-only or affects persistence
- treat `selection_.brightness` as the authoritative persisted brightness unless product scope expands
- ensure preview color, summary text, saved theme, and applied tint do not drift apart

### Done criteria
- accepted selection produces consistent preview, summary, and saved/applied theme behavior

## Phase 5 — Host Path Validation
### Goal
- validate both host paths that can surface the create-space view

### Required checks
- sidebar inline create-space flow remains correct
- `MahoSpaceCreateDialog::Open()` standalone wrapper path remains correct
- chooser dialog sizing/focus/parentage does not break under the standalone host
- no clipping or obvious positioning regressions appear in the secondary host path

### Done criteria
- both host paths behave correctly with the chooser-only modal

## Phase 6 — Test and Verification Strengthening
### Goal
- turn the current artifact-capture flow into a meaningful acceptance gate

### Required test work in `maho_sidebar_spaces_interactive_test.cc`
#### Strengthen `CreateSpaceThemeRowOpensNativeThemeDialog`
- assert forbidden content is absent from the dialog:
  - `Create a Space`
  - `Space name...`
  - `Profile`
- assert chooser dialog width/bounds are correct
- capture the screenshot after layout/paint has stabilized enough for proof use

#### Add dedicated behavior coverage
- first-open default-state test
- cancel behavior test
- accept behavior test
- reopen persistence test

#### Keep helper-based tests in perspective
- `SetThemePickerSelectionForTesting()` and `AcceptThemePickerDialogForTesting()` may remain helpers, but must not be the only evidence for chooser correctness

### Required verification steps
- regenerate `.sisyphus/artifacts/theme_dialog_proof.png`
- run `python3 maho-chromium/build/scripts/build_maho.py`
- run targeted interactive chooser tests
- run adjacent sidebar interactive tests if chooser changes ripple outward
- perform manual QA against the chooser portion of `maho/reference_screenshots/create-theme.png`

### Manual QA checklist
- no left create-space form UI
- chooser-only width and silhouette
- correct shell radius/border
- correct chip count/spacing
- dominant canvas and centered message
- correct minus/plus placement
- correct swatch size/count/spacing
- correct chevron alignment
- correct wave placement
- correct dial placement
- correct default selected state

### Done criteria
- build passes
- targeted tests pass
- proof screenshot is current
- manual QA passes without hand-waving visible mismatches

## Phase 7 — Final Review Gate
### Goal
- avoid another false PASS caused by the wrong review scope

### Required review inputs
- chooser-only acceptance contract
- final dialog source
- regenerated runtime screenshot
- build/test evidence

### Required review question
- `PASS/FAIL: does the current dialog satisfy chooser-only scope and visual parity target?`

### Explicit anti-patterns
- unrelated remediation-only Oracle reviews
- generic code-quality-only signoff
- treating "dialog opens" as equivalent to visual parity

### Done criteria
- final review explicitly covers scope, technical evidence, and visual evidence together

## Execution Order
1. Phase 0 semantics gate
2. Phase 1 structural scope correction
3. Phase 2 default state and reopen behavior
4. Phase 3 chooser visual rebuild
5. Phase 4 persisted theme and tint consistency
6. Phase 5 host path validation
7. Phase 6 test and verification strengthening
8. Phase 7 final review gate

## Practical Bottom Line
- This plan is implementation-ready but not a guarantee of success.
- A true 100% success claim is only valid after code changes, build/test success, regenerated runtime proof, manual visual QA, and a correctly scoped final review.
