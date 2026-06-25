# Command Palette Reference Parallel Plan

## Objective

Implement exact command-palette parity with the Swift/reference surface, and keep the work locked to one UI primitive only:

- **Target surface:** a centered floating command palette card.
- **Non-target surfaces:** no anchored omnibox, no dropdown bubble, no arrowed popup, no drawer, no inline address bar expansion.

If the design starts drifting toward an anchored or dropdown primitive, stop that track and correct the architecture before any more polish work continues.

This plan is written so subagents can work in parallel without crossing into the wrong primitive.

## Source of truth

Use these as the reference set, in this order:

1. Swift reference surface
   - `maho/macos-shell/Sources/CommandBar/CommandBarWindowController.swift`
   - `maho/macos-shell/Sources/CommandBar/CommandBarViewController.swift`
   - `maho/macos-shell/Sources/CommandBar/SuggestionCellView.swift`
2. Chromium command palette implementation under active work
   - `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`
   - `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
   - `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`
   - related headers and helpers in `maho-chromium/browser/ui/views/command/`
3. WebUI surface that currently reflects the command flow
   - `maho-chromium/browser/resources/maho_command/command.html`
   - `maho-chromium/browser/resources/maho_command/command.css`
4. Visual reference material
   - `maho/docs/screenshots/arc/command-palette.jpeg`
   - `maho/docs/screenshots/arc/palette-tab-active.png`
   - any nearby palette or overlay screenshots in `maho/docs/screenshots/arc/`

The Swift files define the intended shape, spacing, selection behavior, and motion. The Chromium views files are the implementation target. The screenshot folder is the visual check against drift.

## Hard architecture gate, first and non-negotiable

Before any feature work starts, confirm and document the following:

1. The command surface is a **centered floating card**.
2. It is sized and positioned as a palette, not attached to an anchor view.
3. It has no bubble arrow, no pointer, no anchored tail, no omnibox dropdown posture.
4. It can animate in and out while remaining visually centered.
5. It must be possible to inspect the code and immediately see that the primitive is a palette card, not a bubble.

This gate is serial. No parallel track may begin until this is locked.

## Parity target

The Chromium surface must match the Swift reference at the level that matters for user perception and regression safety:

- centered presentation geometry
- card-like chrome and border treatment
- input row placement and spacing
- result list spacing and row height rhythm
- selected row treatment
- empty state handling
- opening and closing motion
- keyboard navigation and submit behavior
- current-tab and new-tab presentation behavior where relevant to the command palette surface

Do not spend time on non-command-palette subsystems. Sidebar, AI panel, settings, and other product areas stay out of scope.

## Serial gates

### Gate 1, architecture confirmation

Owner: one subagent.

Deliverable:

- short written confirmation that the target is a centered floating card
- list of any files that currently encode anchored or dropdown assumptions
- explicit note on what must be changed to keep the primitive correct

Exit criteria:

- no ambiguity about the UI primitive
- no work begins in row styling or animation until this gate is approved

### Gate 2, reference extraction

Owner: one subagent.

Deliverable:

- a metric sheet distilled from the Swift files
- a behavior sheet for open, close, focus, selection, and empty state
- a visual notes sheet from the reference screenshots

The metric sheet should include, at minimum:

- window width and height target
- top inset and horizontal inset behavior
- input row height
- divider spacing
- result row height and selected row height
- maximum visible result area before scrolling
- corner radius and border feel

### Gate 3, implementation plan lock

Owner: one coordinator subagent.

Deliverable:

- a written mapping from reference behavior to implementation files
- explicit ownership for each parallel track
- a list of forbidden regressions, especially anything that turns into a dropdown

No track may start until this map is agreed.

## Parallel workstreams

After the serial gates are complete, the implementation can split into these parallel tracks.

### Workstream A, surface primitive and window geometry

Primary files:

- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.h`

Scope:

- keep the surface centered on screen or browser frame
- ensure sizing stays palette-like and not anchor-bound
- preserve floating card behavior during open and close
- handle animation and deactivation without changing the primitive

Success proof:

- opening the palette never snaps to an anchor edge
- the widget stays centered according to the chosen reference frame
- no bubble arrow or anchored geometry exists anywhere in the path

### Workstream B, chrome, input row, and spacing parity

Primary files:

- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h`

Scope:

- reproduce the Swift card structure in Views
- align input row, divider, and result area spacing to the Swift reference
- keep the card visually quiet and centered
- preserve palette chrome, not browser omnibox chrome

Success proof:

- the card reads as one floating surface with an input zone and results zone
- the shell layout does not introduce a dropdown silhouette
- metrics match the Swift reference within the accepted tolerance

### Workstream C, result rows and selection chrome

Primary files:

- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.h`

Scope:

- match row height, selection state, icon lane, text layout, and shortcut treatment
- keep row density and rhythm aligned with the Swift suggestion cell
- preserve selected row emphasis without turning rows into listbox chrome

Success proof:

- selected and unselected rows match the reference feel
- icons, titles, subtitles, and shortcuts sit in the same visual hierarchy as Swift
- no row styling leaks the omnibox dropdown look

### Workstream D, model, ranking, and behavior parity

Primary files:

- `maho-chromium/browser/ui/views/command/maho_command_model.cc`
- `maho-chromium/browser/ui/views/command/maho_command_model.h`
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.h`

Scope:

- keep command suggestions and actions consistent with the Swift behavior where they surface in the palette
- preserve keyboard execution paths
- preserve empty state, selection, and submit semantics

Success proof:

- same query inputs produce the same class of palette results
- keyboard submit and navigation act like the reference surface
- no behavior path depends on dropdown-specific assumptions

### Workstream E, input handling and shortcuts

Primary files:

- `maho-chromium/browser/ui/views/command/maho_shortcut_interceptor.cc`
- `maho-chromium/browser/ui/views/command/maho_shortcut_interceptor.h`

Scope:

- preserve the command palette entry shortcuts
- make sure activation and dismissal still behave like a floating palette
- keep tab, enter, escape, and arrow key handling aligned with the reference

Success proof:

- hotkeys open the palette in the expected state
- escape and outside-focus dismissal do not change the UI primitive
- keyboard focus stays in the command palette until dismissal or execution

### Workstream F, visual QA assets and screenshot parity

Primary files and folders:

- `maho/docs/screenshots/arc/`
- any new command-palette reference captures under `maho/docs/screenshots/`

Scope:

- capture comparison screenshots for the same states as the Swift reference
- annotate where the Chromium result matches or diverges
- keep the screenshots focused on the palette card, not the surrounding app chrome

Success proof:

- screenshots make it obvious that the card is centered and floating
- diffs are useful for spacing, typography, and selection review
- no screenshot shows the palette behaving like an anchored dropdown

## Parallel execution rules

1. Each workstream owns its files and does not change the primitive.
2. If a track discovers a dropdown assumption, it stops and reports back.
3. No track may invent new UI structures that are not already in the Swift reference or the current Views command path.
4. Shared decisions, such as the final height cap or exact centering rule, are coordinated once and then reused.
5. Workstreams can proceed in parallel only after the architecture gate is closed.

## Hard acceptance gates

The plan is not complete unless all of these are true:

### Primitive gate

- The command palette is a centered floating card.
- It is not anchored to a browser location.
- It does not use dropdown or bubble language in code, screenshots, or QA notes.

### Geometry gate

- Palette width and height are stable across open states.
- The palette is centered relative to the chosen reference frame.
- The palette remains within visible bounds.
- There is no arrow, pointer, or anchor tail.

### Visual gate

- Overall silhouette matches the Swift reference.
- Input row, divider, and result rows land in the same visual rhythm.
- Selection chrome feels like the reference, not like a listbox or omnibox dropdown.

### Behavior gate

- open, type, arrow navigation, enter, and escape all match the reference workflow
- outside click and app deactivation dismiss the palette cleanly
- current-tab and empty-state behaviors still work

### Scope gate

- only command-palette parity is touched
- no sidebar, AI settings, or unrelated chrome work is introduced

## Verification plan

Verification happens in two layers, runtime and manual.

### Runtime checks

1. Build the affected Chromium target.
2. Launch the app with the command palette entry path that the repo already uses.
3. Confirm the palette opens at the center of the chosen frame.
4. Confirm keyboard focus lands in the input immediately.
5. Confirm selection moves correctly with up and down arrows.
6. Confirm enter executes the selected result.
7. Confirm escape dismisses cleanly.
8. Confirm outside click and app deactivation dismiss cleanly.
9. Confirm height changes do not reclassify the surface into a dropdown.

### Manual QA checkpoints

Capture screenshots for these states:

- closed state, before opening
- opening state, just after animation begins
- empty query state, with only the palette card visible
- populated state with one result
- populated state with multiple results
- selected row state
- current-tab mode, if present in the surface being verified
- dismissal state after escape or outside click

Each screenshot must prove that the primitive is still a centered floating card.

### Screenshot rules

- crop to the command palette and enough surrounding context to prove centering
- store captures alongside the existing reference materials
- label the state clearly so diff review is fast
- reject any screenshot that looks like a dropdown attached to a browser control

## What success looks like

The finished implementation should let a reviewer say, without reading code, that the Maho Chromium command palette is the same surface family as the Swift reference:

- centered floating card
- same perceived spacing and rhythm
- same keyboard behavior
- same selection feel
- same open and close motion

If any implementation result reads as an anchored omnibox dropdown, it is a failure, even if the styling is polished.
