# Command Palette Current-Tab Zero-Delta Execution Plan (2026-05-22)

## Status

- Planning only. No implementation is part of this document.
- This plan **supersedes** `.sisyphus/plans/command-palette-current-tab-closeout-plan-2026-05-22.md` for execution purposes.
- Historical work remains recorded in:
  - `docs/cmd-l-open-state-delta.md`
  - `.sisyphus/plans/command-palette-current-tab-parity-execution-plan-2026-05-19.md`

## Why this plan exists

The previous closeout framing was too optimistic. It treated the current state as if the remaining gap were probably just one known code mismatch. That is not honest enough for a zero-delta claim.

This plan is therefore built around a stricter rule:

> We do **not** claim full reference parity in advance. We claim a disciplined loop that continues until a fresh capture shows zero visible delta versus the frozen reference, or until each remaining delta is explicitly reopened as a narrowly owned follow-up.

## Goal

Match the frozen reference open frame for Cmd+L `kCurrentTab` on an **ordinary site page**.

- **Primary reference:** `maho/docs/screenshots/arc/palette-tab-active.png`
- **Secondary reference (non-binding):** `maho/docs/screenshots/arc/command-palette.jpeg`

## Hard scope boundary

### In scope

Only the ordinary-site current-tab open frame, e.g.:

- `https://news.ycombinator.com`
- `https://google.com`
- `https://youtube.com`

### Out of scope for acceptance

Browser-internal pages and their suppression logic, including:

- `chrome://settings`
- `chrome://history`
- `chrome://newtab`
- `chrome://maho-ai`

This plan treats internal-page suppression as a **separate guardrail**, not as parity acceptance work.

### Additional out-of-scope areas

- `maho_command_model.cc` / `maho_command_model.h`
- `maho_command_overlay_controller.cc` / `.h`
- `kNewTab`, `kAI`, WebUI command surfaces
- GN/build graph edits
- animation, blur, vibrancy, shadow polish
- new seed sources or seed ranking changes

## Non-negotiable truths for this execution

1. **No zero-delta promise before a fresh capture.**
2. **No parity decision based on internal `chrome://` behavior.**
3. **No code phase begins until disputed reference readings are re-certified by a human.**
4. **No “looks close enough.”** The exit bar is zero visible delta at the binding capture conditions.
5. **No silent scope growth.** Every newly discovered structural delta becomes its own named sub-phase.

## Known planning risks from the prior attempt

### Risk 1 — wrong branch fixation

Recent work landed on the internal-page branch while the user was evaluating the ordinary-site current-tab path. Compile- and test-green did not protect against solving the wrong problem.

### Risk 2 — stale captures

Older captures and scenario-matrix verdicts predate later P7+/closeout work and cannot be treated as proof of current parity.

### Risk 3 — frozen R0 may contain at least one wrong or stale read

Two items must be re-verified directly against `palette-tab-active.png` before implementation:

1. **Trailing accessory (R0 D2).** The current doc says “hidden in `kCurrentTab`,” but the reference may in fact show a trailing info icon.
2. **Input-row tonal treatment.** The current doc/plan history may have over-corrected by flattening the input row when the reference may actually show a recessed or tonally distinct input lane.

These are not implementation decisions to guess through. They are human verification gates.

## Current code-state audit (what is actually known)

### Landed on disk and treated as current baseline

These appear implemented already and are **not** to be reopened unless the fresh capture proves they are still visually wrong:

1. **5-row seeded cap**
   - `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
   - `kMaxSeededVisibleResults = 5`

2. **Inline single-line seeded row layout**
   - `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`

3. **Selected seeded favicon tile**
   - `maho_command_result_row_view.cc`
   - `maho_command_result_row_view_unittest.cc`
   - test coverage exists

4. **Internal-page suppression guardrail**
   - `maho_sidebar_state_models.h`
   - `maho_command_overlay_view.cc`
   - `chromium/src/chrome/browser/ui/views/frame/browser_view.cc`
   - remains frozen during this plan

### Confirmed open code mismatch

1. **Divider visibility in the seeded current-tab branch**
   - file: `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
   - function: `UpdateResultViews(...)`
   - current seeded branch behavior: `divider_->SetVisible(true)`
   - this contradicts the frozen “no divider” rule if the rule survives re-certification

### Confirmed coverage gaps

1. No direct `divider visible?` test seam on the overlay view.
2. No unit test for “divider hidden in seeded current-tab”.
3. No runtime/browser test that proves the real ordinary-site current-tab path satisfies the divider rule.
4. Existing passing tests do **not** prove the correct branch for the user’s ordinary-site screenshot.

## Phase 0 — Reference re-certification (human gate, pre-code)

No implementation work begins until this phase is recorded.

### 0.1 Re-certify trailing accessory against the frozen reference

Human reviewer examines `maho/docs/screenshots/arc/palette-tab-active.png` at native resolution and records a yes/no answer:

- Is there a visible trailing input accessory in the open `kCurrentTab` frame?
- If yes: record exact treatment (glyph, circle, tonal chip, spacing).
- If no: reaffirm R0 D2 as written.

### 0.2 Re-certify input-row surface treatment against the frozen reference

Human reviewer records whether the input row is:

- fully flush with the card surface, or
- tonally distinct / recessed / otherwise visually separated

This decision must be written in plain language, not inferred later from code.

### 0.3 Update the authoritative reference record

Append a new subsection to `docs/cmd-l-open-state-delta.md` that records the re-certification result and supersedes only the disputed items if needed.

### Acceptance gate for Phase 0

All of the following must be true:

1. the human reference read is recorded in `docs/cmd-l-open-state-delta.md`
2. disputed items are either reaffirmed or amended explicitly
3. every later code phase cites this recertified record, not the stale assumption set

## Phase 1 — Fresh baseline capture before assuming remaining delta count

The purpose of this phase is to discover reality, not to confirm a prior guess.

### 1.1 Build and launch the current app

Use the lock-backed wrapper only:

- `python3 maho-chromium/build/scripts/build_maho.py --skip-rust -- chrome`

### 1.2 Produce a binding capture of the current ordinary-site open frame

Binding conditions:

- dark mode
- 2x DPR
- ordinary site page
- deterministic local fixture / reproducible back-stack
- first visible Cmd+L frame

### 1.3 Enumerate every visible delta against the frozen reference

For each delta, record:

1. a short name
2. whether it is **structural** or **tonal-only**
3. the likely owning file
4. whether it was already known or newly discovered

### Acceptance gate for Phase 1

The delta list exists and is treated as the execution source of truth.

This plan makes **no assumption** that the list has length 1.

## Phase 2 — Structural delta execution loop (one delta at a time)

Each structural delta from Phase 1 becomes its own numbered sub-phase.

### Mandatory template for every structural delta

For delta `S.N`:

#### S.N.a — add minimal test seam if needed

- add only the smallest `*ForTesting` accessor or seam needed
- no behavior change

#### S.N.b — write failing unit test first

- the test must fail against the current code
- add a positive control if the same property should still hold in another mode/path

#### S.N.c — implement the smallest production fix

- touch only the owning files for that delta
- do not bundle unrelated deltas in the same patch

#### S.N.d — add runtime/browser coverage for the real ordinary-site path

- navigate through ordinary site pages
- open Cmd+L via the real user-initiated focus path
- assert the property in runtime, not only in unit harness

#### S.N.e — verify

- changed-file diagnostics clean
- existing command overlay tests still pass
- new tests pass
- no forbidden files modified

### Pre-identified structural delta candidate: divider visibility

Unless Phase 1 capture disproves it, the first structural delta is expected to be:

- **Name:** no divider in seeded current-tab open frame
- **Owning files:**
  - `maho_command_overlay_view.h`
  - `maho_command_overlay_view.cc`
  - `maho_command_overlay_view_unittest.cc`
  - `maho_command_overlay_interactive_uitest.cc`

Expected work:

1. add divider visibility test seam
2. add failing current-tab seeded test
3. add positive control for non-current-tab result path
4. fix the seeded branch visibility rule
5. add runtime test on the real ordinary-site current-tab path

Important constraint:

- do **not** alter divider behavior for unrelated result modes unless a failing positive-control test requires it

## Phase 3 — Rebuild and recapture after each structural delta batch

After each structural delta (or tightly related batch when the capture clearly showed they belong together):

1. rebuild
2. relaunch
3. recapture under the same binding conditions
4. compare against the frozen reference again

### Branching rule after recapture

- if a structural delta remains → return to Phase 2 with a new numbered sub-phase
- if only tonal-only deltas remain → move to Phase 4
- if zero deltas remain → skip to Final Sweep

## Phase 4 — Tonal-only follow-up (conditional, last resort)

This phase runs only if:

1. all structural deltas are closed, and
2. the fresh capture still shows a purely tonal mismatch

### Allowed files

- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- `maho-chromium/browser/ui/theme/maho_color_id.h`

### Must not do

- do not use token tuning to hide structural problems
- do not blanket-edit mixer values
- do not retune unrelated surfaces

### Acceptance gate for Phase 4

Before/after capture at the same conditions proves the tonal-only delta is gone and no new adjacent-surface regression was introduced.

## Phase 5 — Documentation and evidence record

Owning file:

- `docs/cmd-l-open-state-delta.md`

Required updates:

1. record the recertified reference decisions from Phase 0
2. record the fresh baseline capture from Phase 1
3. record each structural delta sub-phase and its owning files/tests
4. record each recapture outcome
5. explicitly state:
   - internal-page suppression is a separate guardrail, not ordinary-site parity acceptance
   - P10 remains N/A only if the recertified reference still confirms bare selected URL text
   - P11 is N/A unless Phase 4 ran

## Final Sweep — done definition

The track is done only when all of the following are true:

1. reference re-certification is recorded
2. a fresh ordinary-site capture at binding conditions exists
3. every structural delta found in that capture has either:
   - been fixed and verified, or
   - been reopened as an explicitly named follow-up with file ownership
4. the latest capture shows **zero visible delta** versus the frozen reference, or there is an explicit user-approved follow-up list for anything intentionally left open
5. forbidden files remain untouched:
   - `maho_command_model.cc`
   - `maho_command_model.h`
   - `maho_command_overlay_controller.cc`
   - `maho_command_overlay_controller.h`
   - internal-page suppression files for this track

## Verification rules

### Capture is mandatory

No code audit, no passing unit tests, and no Momus review can substitute for a fresh reference-comparable capture.

### Runtime ordinary-site test is mandatory

At least one browser/integration test must assert the key current-tab property on the **real ordinary-site path**, because the prior wrong-branch detour proved local/unit proof is not enough.

## Commit strategy

One delta, one commit chain.

Recommended chain per structural delta:

1. test seam commit
2. failing unit test commit
3. production fix commit
4. runtime test commit
5. docs/capture record commit

Do not bundle unrelated deltas together.

## Reopen / rollback rules

1. If recapture shows a still-open structural delta, reopen the plan immediately.
2. If a fix regresses another mode/path, revert that delta’s chain rather than piling speculative edits on top.
3. If Phase 0 re-certification overturns a stale R0 decision (for example trailing accessory visibility), then any conflicting previously landed code is treated as open work again.

## Honest feasibility statement

This plan can realistically get us to reference parity because it does **not** rely on a guessed remaining-delta count. It relies on:

- human recertification of disputed reference reads,
- capture-backed delta enumeration,
- one-delta-at-a-time structural fixes,
- runtime proof on the ordinary-site path,
- and a recapture loop until zero visible delta is achieved.

What it cannot honestly promise is: “one more patch will definitely finish it.”

It can honestly promise: “we will not declare victory without the capture proving it.”
