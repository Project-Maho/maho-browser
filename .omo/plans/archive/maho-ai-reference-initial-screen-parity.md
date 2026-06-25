# Maho AI initial no-session reference parity execution spec

## Objective

Produce a no-session `chrome://maho-ai` screen whose visible WebUI surface matches the supplied reference image as closely as possible.

This plan is intentionally strict. It exists to remove room for interpretation during implementation.

## Source of truth

- The supplied reference screenshot is the only visual authority.
- Existing Maho AI UI is not a source of truth.
- Existing copy, chips, hints, status treatments, and button labels survive only if they are visibly present in the reference.

## Scope lock

Implement only the initial screen where:

- `state.currentSessionId === null`
- `state.bootError === null`
- the screen is not read-only

Do not redesign or expand the active-session view except where isolation is required to prevent regressions.

## Verified technical facts

1. The visible top row is WebUI-owned.
   - Host: `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc`
   - DOM: `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
   - CSS: `maho-chromium/browser/resources/maho_ai/maho_ai.css`
2. `maho_ai.html` only mounts `#app`; the top row, stage, quick actions, and composer are created in TS.
3. The current no-session explanatory UI is presentational only. It is not required by backend/session recovery.
4. The quick actions and prompt submission behavior can remain while the non-reference UI is removed.

## Files allowed to change

- `maho-chromium/browser/resources/maho_ai/views/console_shell.ts`
- `maho-chromium/browser/resources/maho_ai/views/composer.ts`
- `maho-chromium/browser/resources/maho_ai/maho_ai.css`

## Files not allowed to change

- `app.ts`
- `store.ts`
- `types.ts`
- `maho_ai_ui.cc`
- any native-side panel/frame code

If implementation appears to require changes outside the allowed set, stop and reassess before broadening scope.

## Current no-session DOM to eliminate or isolate

### In `console_shell.ts`

Current no-session render includes all of the following:

- top header with:
  - spark brand mark
  - `Maho AI`
  - `Ready`
  - plus button
  - ellipsis button
- hero halo block
- hero center block
- hero icon
- hero headline: `Open a compact AI workspace`
- hero description: `Start a live session and keep the UI focused on one prompt at a time.`
- status card
- status text: `No live session`
- helper copy: `Use Analyze, Explain, or Summarize to draft the next prompt.`

### In `composer.ts`

Current no-session composer includes all of the following:

- page row
- `Page / Current page` chip
- `Context on/off` chip
- textarea placeholder: `Ask Maho to analyze this page…`
- footer plus button
- footer cancel button
- footer hint text
- labeled `Start` button with spark icon

## Target UI contract

The no-session screen must render exactly four visual regions:

1. **Top row**
2. **Empty center field**
3. **Quick actions row**
4. **Bottom composer**

No other visual regions are permitted on the no-session screen.

## Target top row contract

### Required

- top row remains present
- top row is icon-led, not text-led
- top row spacing and silhouette match the reference rather than the current implementation

### Forbidden

- no `Maho AI` text
- no `Ready` text
- no current brand-copy stack
- no current plus/ellipsis layout if it does not match the reference

### Implementation rule

Build the no-session header as its own branch instead of mutating the active-session header in place.

Recommended structure:

- one no-session header renderer dedicated to reference parity
- one existing/default header path kept for active session

This is required to avoid accidental regression in the active-session top row.

## Target empty center field contract

### Required

- large uninterrupted dark vertical field between the top row and the bottom controls
- no content cards
- no hero ornament
- no explanatory messaging

### Forbidden

- no halo
- no center icon
- no headline
- no description
- no status card
- no `No live session`
- no helper text

### Implementation rule

Do not simply delete `renderStage()` globally.

Instead:

- create a no-session stage branch that renders a spacer/empty field only
- keep active-session stage rendering separate

The no-session stage must still preserve the flex behavior that pushes quick actions and composer toward the bottom.

## Target quick actions contract

### Required

- exactly three visible actions:
  - `Analyze`
  - `Explain`
  - `Summarize`
- actions remain clickable
- actions still prefill the prompt exactly as they do now
- actions sit above the composer near the bottom, matching the reference vertical zone

### Visual requirements

- pill treatment must match the reference more closely than the current implementation
- icon treatment may be added only if the reference clearly shows icons
- row spacing must be compact and low-profile

### Forbidden

- no extra quick actions
- no descriptive helper copy attached to quick actions

## Target composer contract

### Required

- one large rounded composer container
- textarea remains editable
- submit from no-session still starts a session
- placeholder must be exactly:
  - `Ask a question about this page...`

### Forbidden

- no page chip
- no context chip
- no `Current page`
- no `Context on/off`
- no footer hint text
- no session-management copy
- no large labeled `Start` / `Send` button if not present in the reference

### Footer/control requirements

The bottom composer controls must visually match the screenshot’s control bar.

Implementation constraints:

- if a screenshot-visible control already maps cleanly to an existing behavior, reuse it
- if a screenshot-visible control has no obvious behavior in current code, keep behavior minimal and non-disruptive
- do not invent new product semantics just to justify a visual control

### Structural rule

Build a no-session composer branch rather than conditionally hiding ten sub-elements inside the existing active-session structure when that would leave dead wrappers and spacing artifacts.

Specifically:

- if `pageRow` has no surviving children, it must not be rendered at all
- if footer left-side actions are removed, the footer layout must be rebuilt, not left as an empty flex group

## Explicit keep / remove matrix

### `console_shell.ts`

#### Keep for no-session
- outer `console-shell`
- outer `compact-panel`
- `actionsHost`
- `composerHost`
- a dedicated top-row container
- a dedicated empty-field container

#### Remove from no-session
- `compact-panel__brand-copy`
- `compact-panel__eyebrow`
- `compact-panel__title`
- `compact-panel__halo`
- `compact-panel__center`
- `compact-panel__center-mark`
- `compact-panel__center-copy`
- `compact-panel__headline`
- `compact-panel__description`
- `compact-panel__status-card`
- `compact-panel__status-label`
- `compact-panel__status-copy`
- literal strings:
  - `Open a compact AI workspace`
  - `Start a live session and keep the UI focused on one prompt at a time.`
  - `No live session`
  - `Use Analyze, Explain, or Summarize to draft the next prompt.`

#### Replace for no-session
- current no-session header implementation
- current no-session stage implementation

### `composer.ts`

#### Keep for no-session
- textarea
- prompt change wiring
- enter-to-submit behavior
- no-session submit behavior

#### Remove from no-session
- `compact-composer__page-row`
- `compact-composer__page-chip`
- `compact-composer__page-chip-copy`
- `compact-composer__context-chip`
- `compact-composer__hint`
- footer plus button
- footer cancel button
- `compact-composer__send-label`
- current placeholder string:
  - `Ask Maho to analyze this page…`

#### Replace for no-session
- bottom control bar layout
- send affordance treatment
- placeholder text

### `maho_ai.css`

#### Remove or neutralize for no-session
- hero-stage spacing assumptions
- halo visuals
- status-card visuals
- chip visuals used only by removed no-session elements
- footer layout assumptions tied to removed actions/hint text

#### Replace for no-session
- top-row spacing
- empty-field spacing behavior
- quick-action row placement and styling
- composer height, radius, padding, footer alignment, and send-control silhouette

## Execution order

Implement in this exact sequence.

### Step 1 — isolate no-session rendering

- Add a no-session-specific header path in `console_shell.ts`
- Add a no-session-specific stage path in `console_shell.ts`
- Add a no-session-specific composer path in `composer.ts`

Do not start by editing CSS. First isolate structure so styling work is not fighting mixed DOM.

### Step 2 — remove non-reference no-session DOM

- Strip all forbidden no-session DOM from the isolated no-session branches
- Ensure no empty wrappers remain

Checkpoint:

- no-session screen should already have no hero/status/page/context/hint UI
- layout can still be ugly at this step, but DOM must be clean

### Step 3 — rebuild top row silhouette

- Rebuild the no-session header structure to match the reference silhouette
- Remove text-led branding from no-session only
- Align icon count, grouping, and spacing with the screenshot

Checkpoint:

- top row should visually read like the reference even before final spacing polish

### Step 4 — rebuild no-session composer structure

- Set placeholder to exact reference text
- rebuild footer/control row to match the reference silhouette
- keep textarea behavior intact

Checkpoint:

- composer contains only screenshot-supported controls

### Step 5 — layout and spacing pass in CSS

- create the large empty middle field
- push quick actions and composer to the correct bottom zone
- tune row gaps, paddings, radii, and borders
- then tune opacity/shadow/icon sizes last

Checkpoint:

- overall composition should resemble the screenshot before fine polish

### Step 6 — runtime verification and parity tuning

- build
- launch
- capture screenshot
- compare against reference
- adjust only mismatches that move toward the reference

## Non-negotiable implementation rules

1. No new copy anywhere on the no-session screen
2. No new controls beyond what the reference visually justifies
3. No preservation of current UI for convenience
4. No active-session logic breakage
5. No store/backend changes
6. No “improvement” that is not present in the reference

## Verification gates

The work cannot be called done unless every gate below passes.

### Gate A — DOM correctness

Verify the no-session screen does **not** contain any of the following strings:

- `Open a compact AI workspace`
- `Start a live session and keep the UI focused on one prompt at a time.`
- `No live session`
- `Use Analyze, Explain, or Summarize to draft the next prompt.`
- `Current page`
- `Context on`
- `Context off`
- `Enter send · Shift+Enter newline`

### Gate B — no-session visual correctness

Verify the no-session screen **does** contain:

- top row matching reference silhouette
- quick actions row with `Analyze / Explain / Summarize`
- composer with placeholder `Ask a question about this page...`
- large empty dark field in the middle

### Gate C — behavior correctness

- clicking each quick action prefills the prompt
- typing enables the submit affordance if appropriate
- submitting from no-session starts a session

### Gate D — active-session regression safety

- starting a session still renders an active-session screen
- active-session interaction remains functional

### Gate E — build correctness

- canonical build succeeds:
  - `python3 build/scripts/build_maho.py`

## Evidence required

Implementation is incomplete without all of the following evidence:

1. diagnostics/typecheck result on changed TS files
2. successful canonical build
3. launched app evidence
4. screenshot of the no-session screen after changes
5. note confirming active-session still works after starting a session

## Failure modes to guard against

1. **Collapsed layout**
   - removing stage content without preserving bottom anchoring moves quick actions too high
2. **Dead wrappers**
   - conditional hiding leaves empty `pageRow` / footer containers that still consume space
3. **Scope drift**
   - tweaking active-session UI without a no-session-only branch
4. **UX invention**
   - adding behavior/copy because a screenshot control is ambiguous
5. **False parity**
   - matching colors but leaving forbidden copy or chips in place

## Done definition

This task is done only when the initial no-session `chrome://maho-ai` screen is visually driven by the reference image rather than by current product affordances, and the existing quick-action/prompt-start behaviors still work.
