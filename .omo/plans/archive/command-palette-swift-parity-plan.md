# Command Palette Swift Parity Plan

## Goal
- Align the Chromium native command palette with the Swift reference for **real behavior and visual output**, using screenshots only for final visual tuning.

## Source of Truth
- **Behavior/state/keyboard/layout contract**: `maho/macos-shell/Sources/CommandBar/CommandBarViewController.swift`
- **Window sizing/positioning/animation contract**: `maho/macos-shell/Sources/CommandBar/CommandBarWindowController.swift`
- **Row visual contract**: `maho/macos-shell/Sources/CommandBar/SuggestionCellView.swift`
- **Visual polish reference**: `/Users/indo/code/project/maho-workspace/maho/reference_screenshots`
- **Gap inventory**: `docs/command-palette-ux-comparison.md`

## Non-Goals
- Do not broaden scope beyond command palette parity unless required for parity wiring.
- Do not redesign toward Arc if Arc screenshots disagree with Swift behavior.
- Do not refactor unrelated sidebar/address-bar systems while fixing parity gaps.

## Success Criteria
- Native palette matches Swift behavior for modes, keyboard flows, result shaping, row states, and animations.
- Native palette visually matches Swift first, then is polished against the screenshot folder.
- Full build succeeds and runtime QA passes for core flows, including IME.

---

## Phase 0 — Spec Lock and Constants Diff

### Objective
Produce a single implementation reference table before touching code.

### Files to read / use
- `maho/macos-shell/Sources/CommandBar/CommandBarViewController.swift`
- `maho/macos-shell/Sources/CommandBar/CommandBarWindowController.swift`
- `maho/macos-shell/Sources/CommandBar/SuggestionCellView.swift`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`

### Deliverables
- A constants diff table for:
  - panel width / initial height / max height
  - panel corner radius
  - search row height / padding / radius
  - row height normal / selected
  - row spacing / bottom inset / divider gap
  - search field typography
  - selection fill / border / shadow behavior
  - open / close / resize animation timings

### Acceptance criteria
- Every Swift metric used by the palette is mapped to the current Chromium value or marked “missing”.
- The team can point to one table and know exactly what must change.

---

## Phase 1 — Mode and State-Machine Parity

### Objective
Make Chromium mode behavior match Swift before visual tweaking.

### Target behavior
- Support all Swift-equivalent modes:
  - `search`
  - `currentTab`
  - `newTabDraft`
  - `siteSearch`
  - `ai`
- Preserve mode-aware placeholder text.
- Preserve mode-aware empty-state logic.
- Preserve mode-aware Escape behavior.

### Files likely to change
- `maho-chromium/browser/ui/views/command/maho_command_model.h`
- `maho-chromium/browser/ui/views/command/maho_command_model.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`

### Required changes
- Expand mode enum and model API to represent Swift-equivalent modes.
- Make `Show(...)` / query flow carry explicit mode, not infer everything from one generic path.
- Implement current-tab URL prefill + select-all behavior.
- Implement new-tab draft behavior as distinct from current-tab mode.
- Implement site-search entry/exit state.
- Implement AI mode entry/exit state and routing.

### Acceptance criteria
- Cmd+L opens current-tab mode with selected URL prefill.
- Cmd+T opens new-tab draft mode with its own placeholder and query semantics.
- `@prefix + Tab` enters site-search mode.
- `ai + Tab` enters AI mode.
- Escape exits AI/site-search mode before dismissing the palette.

---

## Phase 2 — Query and Result Logic Parity

### Objective
Match Swift result shaping and fallback behavior exactly.

### Files likely to change
- `maho-chromium/browser/ui/views/command/maho_command_model.h`
- `maho-chromium/browser/ui/views/command/maho_command_model.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`

### Required changes
- Preserve Swift query debounce and empty-query semantics.
- Implement recent-search fallback when the field is empty.
- Keep result shaping aligned with Swift:
  - max 5 visible rows
  - 2 tabs
  - 2 history
  - 1 navigation
  - fill remainder appropriately
- Preserve selection restoration rules after result refresh.
- Preserve “No results for \"query\"” messaging.
- Preserve site-search query wrapping and submission behavior.

### Acceptance criteria
- Empty query shows Swift-equivalent fallback rows, not arbitrary FFI output.
- Result count and ordering match Swift for the same input.
- Recent searches appear and are selectable/executable.
- Query-specific no-result text matches Swift behavior.

---

## Phase 3 — Keyboard and Submit Behavior Parity

### Objective
Close the remaining interaction gaps that screenshots cannot reveal.

### Files likely to change
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- `maho-chromium/browser/ui/views/command/maho_command_model.cc`

### Required changes
- Make Up/Down clamp like Swift if current implementation wraps.
- Implement `Shift+Enter` direct-navigation behavior.
- Make Enter fallback behavior match Swift when no selection exists.
- Make Tab serve mode-entry purposes before any selection-navigation role.
- Preserve IME composition guards while introducing mode-entry logic.

### Acceptance criteria
- Keyboard regression checklist passes:
  - Up / Down
  - Enter
  - Shift+Enter
  - Tab
  - Escape
  - IME composition + Enter/Escape interaction

---

## Phase 4 — Palette Frame, Layout, and Metric Parity

### Objective
Bring the overall card and content geometry to Swift metrics.

### Files likely to change
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`

### Required changes
- Match Swift width/initial-height/preferred-height behavior.
- Revisit vertical positioning and any clamping requirements.
- Match panel radius, search row radius, padding, divider spacing, and bottom inset.
- Match input field visual density and typography as closely as Chromium Views allows.
- Ensure theme tokens are used instead of hardcoded colors where possible.

### Acceptance criteria
- Card silhouette, spacing, and density feel materially identical to Swift.
- Height calculations are deterministic and no longer tuned by guesswork.

---

## Phase 5 — Row Renderer Overhaul

### Objective
Close the biggest visual gap: result rows.

### Files likely to change
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.h`
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`
- Possibly favicon/icon plumbing in browser-side support code if needed

### Required changes
- Replace letter-glyph identity where Swift uses real icons/favicons/placeholders.
- Match selected vs unselected row composition:
  - row height change
  - icon tile treatment
  - selection fill / border / shadow
  - subtitle visibility rules
  - title/subtitle hierarchy
- Match shortcut badge sizing/color behavior.
- Preserve matched-range emphasis in title text.

### Acceptance criteria
- Selected row immediately reads as Swift-equivalent.
- Tabs/history/bookmarks/navigation/actions are visually distinguishable without relying on text letters.
- First impression diff against screenshots is dramatically reduced.

---

## Phase 6 — Animation Parity

### Objective
Match Swift motion language instead of only matching static screenshots.

### Files likely to change
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`

### Required changes
- Open animation: fade + upward slide with Swift-style timing/easing.
- Close animation: fade + upward slide.
- Height resize animation for result changes.
- Verify animation sequencing does not reintroduce the previous double-hide/race bugs.

### Acceptance criteria
- Open/close/resize feel aligned with Swift recordings/reference expectations.
- No abrupt snap-resize remains in normal result updates.

---

## Phase 7 — Browser Wiring and Entry-Point Verification

### Objective
Ensure real entry points behave like Swift/expected Maho UX.

### Files likely to verify or change
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- Chromium BrowserView interception patch points in the active patch set
- Any address-bar/command-surface bridge code already touched

### Required changes
- Verify Cmd+L and address-bar click both route to the correct mode.
- Verify Cmd+T enters the new-tab draft path.
- Verify no stale integration path still opens an old suggestion surface.
- Verify focus ownership and deactivation behavior after mode additions.

### Acceptance criteria
- All production entry points open the same native command palette with the correct mode.

---

## Phase 8 — Runtime QA and Visual Finish

### Objective
Finish with real runtime verification, then tune against screenshots.

### Required QA matrix
- Cmd+L current-tab URL prefill
- Cmd+T new-tab draft
- address-bar click behavior
- site-search prefix + Tab
- `ai + Tab`
- recent searches when empty
- no-results messaging
- selection movement and submit behavior
- outside click dismiss
- app/window deactivation dismiss
- multi-window behavior
- Korean IME
- Japanese IME
- dark theme token fidelity

### Screenshot tuning pass
- Prioritize:
  - `maho/reference_screenshots/arc-command-pallete.jpeg`
  - `maho/reference_screenshots/pallte-when-tab-is-active.png`
- Tune only after Swift behavior/metrics already match.
- Restrict tuning to:
  - shadow weight
  - border strength
  - blue selection tone
  - spacing density
  - row emphasis

### Acceptance criteria
- Runtime QA passes without mode/focus/IME regressions.
- Screenshot comparison shows no major visual mismatch in card shape, row emphasis, or spacing density.

---

## Execution Order
1. Phase 0 — constants diff
2. Phase 1 — mode/state machine
3. Phase 2 — query/result logic
4. Phase 3 — keyboard/submit behavior
5. Phase 4 — frame/layout metrics
6. Phase 5 — row renderer overhaul
7. Phase 6 — animation parity
8. Phase 7 — browser wiring verification
9. Phase 8 — runtime QA + final screenshot polish

## Why this order
- Behavior must be correct before visual polish.
- Metrics should be stabilized before deep renderer tuning.
- Row renderer work is the largest visual delta, so it should happen after state/logic settle.
- Animation should come late to avoid repeated retuning while layout is still moving.

## Risks to actively manage
- Do not overfit to Arc screenshots if Swift code says otherwise.
- Do not regress previously fixed widget lifecycle bugs while changing animations/modes.
- Do not break IME while adding Tab/Escape mode handling.
- Do not add broad sidebar/address-bar refactors under the banner of palette parity.

## Verification Evidence Required Before Claiming Completion
- `autoninja -C out/Default chrome` passes
- changed command-palette files are diagnostics-clean
- runtime manual QA covers mode transitions and IME
- screenshots recorded before/after for final parity review
