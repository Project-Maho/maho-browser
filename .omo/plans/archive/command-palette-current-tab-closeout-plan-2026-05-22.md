# Command Palette Current-Tab Closeout Plan (2026-05-22)

## Status

- Planning only. No implementation is part of this document.
- This plan covers the **remaining closeout work** for the Cmd+L `kCurrentTab` open frame.
- Historical execution remains in `.sisyphus/plans/command-palette-current-tab-parity-execution-plan-2026-05-19.md` and `docs/cmd-l-open-state-delta.md`.
- This plan exists because the recent internal-page detour proved we can still hit the wrong branch even when code compiles and tests pass.

## Goal

Close the remaining first-frame visual gap for Cmd+L current-tab mode against the frozen reference:

- **Primary reference:** `maho/docs/screenshots/arc/palette-tab-active.png`
- **Binding checklist:** `docs/cmd-l-open-state-delta.md` (R0 + P7+ closeout checklist)

Done means:

1. The ordinary site current-tab open frame matches the frozen reference at dark mode, 2x.
2. The remaining structural code mismatch is removed.
3. The result is covered by tests that exercise the real current-tab open path.
4. The internal-page suppression patch is not allowed to distract this parity track again.

## Scope Clarification (plain language)

This closeout plan is about the UI you see when Cmd+L opens on a **normal site page** such as Hacker News.

It is **not** about browser-internal pages such as:

- `chrome://settings`
- `chrome://history`
- `chrome://newtab`
- `chrome://maho-ai`

Those internal pages are a separate branch. They may stay fixed, but they are **not** the acceptance bar for this parity closeout.

## Frozen reference decisions that this plan will not revisit

Taken from `docs/cmd-l-open-state-delta.md` R0:

- **D1:** No divider between the input row and seeded results; no inter-row divider.
- **D2:** No trailing accessory in `kCurrentTab`.
- **D3:** Selected seeded row shows the favicon inside a white rounded-square tile.
- **D4:** The active-tab URL is shown as bare selected text in the input. This is correct and **not a bug**.

### Consequence

The plan must **not** reopen these already-set decisions:

- Do **not** change ordinary-site URL prefill/select-all behavior unless the frozen reference changes.
- Do **not** broaden `IsInternalBrowserPage(...)` to ordinary sites.
- Do **not** treat “the URL is visible in the input” as a parity bug. The reference does that too.

## Current code audit: what is already landed vs what is still open

### Landed and treated as baseline

These are already on disk and are not the target of new code unless a regression is proven by a fresh capture:

1. **5-row seeded cap** is already 5.
   - `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
   - `kMaxSeededVisibleResults = 5`

2. **Single-line seeded row treatment** is already on disk.
   - `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`
   - title + separator + compact metadata are laid out inline

3. **Selected seeded favicon tile** is already implemented and unit-tested.
   - `maho_command_result_row_view.cc`
   - `maho_command_result_row_view_unittest.cc`
   - test: `SeededRow_SelectedFaviconUsesContainerTreatment`

4. **Trailing accessory hidden in current-tab mode** is already implemented and unit-tested.
   - `maho_command_overlay_view.cc`
   - `maho_command_overlay_view_unittest.cc`
   - test: `CurrentTabSeed_TrailingAccessoryIsHidden`

5. **Internal-page suppression** is already implemented and covered.
   - `maho_sidebar_state_models.h`
   - `maho_command_overlay_view.cc`
   - `chromium/src/chrome/browser/ui/views/frame/browser_view.cc`
   - This remains a separate guardrail, not a parity acceptance item.

### Confirmed remaining code mismatch

One concrete mismatch is still visible in the code audit:

1. **Divider still becomes visible in the current-tab seeded path.**
   - File: `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
   - Function: `UpdateResultViews(...)`
   - Current behavior in the seeded-results branch: `divider_->SetVisible(true)`
   - This violates R0 D1 (“no divider”).

### Coverage gap that let the wrong-branch mistake slip through

Current runtime coverage proves:

- URL prefill + select-all on open
- seeded rows render on first open

But it does **not** yet prove:

- divider stays hidden in the real ordinary-site current-tab path

That gap needs to be closed as part of this plan.

## Plan decision on the internal-page suppression patch

**Recommendation: leave it in place and treat it as out-of-track for this closeout.**

Why:

- it is orthogonal to the ordinary-site parity bug
- it is already covered by dedicated tests
- reverting it would re-open a real internal-page bug
- mixing it back into this closeout would recreate the exact wrong-branch mistake we just made

Rule for execution:

- no further edits to the internal-page suppression code during this plan
- no parity acceptance decision may cite `chrome://...` behavior unless a dedicated regression appears

## Full implementation plan

### Phase A — Close the remaining structural mismatch (divider)

#### Files allowed in this phase

- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view_unittest.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_interactive_uitest.cc`

#### A1. Add a focused test seam

Add a minimal test accessor on the overlay view for divider visibility.

Required outcome:

- tests can assert whether the divider is visible without geometry inference
- no new production API beyond a small `*ForTesting` accessor

#### A2. Add a failing unit test for the real rule

Add a unit test that creates `kCurrentTab` seeded state and asserts:

- seeded rows render
- divider is **not** visible

Add the positive control in the same file:

- in a non-current-tab result path (typed-query or recent-search path), divider remains visible when results are present

This makes the rule explicit instead of relying on visual memory.

#### A3. Add a runtime test on the real ordinary-site open path

Add an interactive/browser test that:

1. navigates through at least two normal site pages on the embedded server
2. opens Cmd+L through the real user-initiated focus path
3. confirms seeded state exists
4. confirms the divider is hidden in that real path

This is required because the previous detour proved unit tests alone are not enough.

#### A4. Fix the divider ownership point

In `UpdateResultViews(...)`, keep the divider hidden for the `kCurrentTab` seeded branch while preserving existing behavior for non-current-tab result lists.

Constraints:

- do not change row generation
- do not change `kMaxSeededVisibleResults`
- do not touch BrowserView opener logic
- do not touch internal-page suppression logic
- do not broaden the change into row rendering or color tokens

#### A5. Acceptance for Phase A

Phase A is complete only if all of the following hold:

1. new unit tests pass
2. new runtime current-tab test passes
3. existing command overlay tests still pass
4. no files outside the four listed above are modified
5. the divider is hidden only in the current-tab seeded branch; other list modes keep their existing divider behavior

### Phase B — Close the documentation and acceptance record

#### Files allowed in this phase

- `docs/cmd-l-open-state-delta.md`
- optionally this plan file if the execution reality needs a status note

#### B1. Record the closeout state clearly

Update `docs/cmd-l-open-state-delta.md` so it no longer leaves room for the recent confusion.

Required updates:

1. explicitly record that **P10 is N/A** because the frozen reference wants bare selected URL text
2. record that the internal-page suppression fix is a separate guardrail and not part of ordinary-site parity acceptance
3. mark the divider item as open until Phase A lands, then close it once verified

#### B2. Do not rewrite history

Append clarification; do not erase the historical plan or pretend the wrong-branch attempt never happened.

#### B3. Acceptance for Phase B

1. the document tells a reviewer exactly what is and is not part of parity acceptance
2. the document makes it impossible to misread normal-site Cmd+L behavior as an internal-page issue again

### Phase C — Build, run, and user capture verification

#### Verification strategy

Use narrow verification first, then runtime capture.

1. targeted command-overlay unit/browser coverage
2. lock-backed Chromium app build via `python3 maho-chromium/build/scripts/build_maho.py --skip-rust -- chrome`
3. launch the freshly built app
4. hand off to the user for a new capture at the binding conditions from `docs/cmd-l-open-state-delta.md`

#### Capture conditions (must match the frozen reference)

- dark mode
- 2x DPR
- first frame of `kCurrentTab`
- ordinary site page

#### Review checklist for the new capture

The review is limited to the frozen checklist:

1. no divider between input and first seeded row
2. no trailing accessory in the input row
3. selected seeded row favicon tile is visible
4. URL remains bare selected text in the input
5. no new structural mismatch was introduced by the divider fix

### Phase D — Conditional last step after capture

If the fresh capture shows no remaining visible delta:

- record **P11 N/A** (“no surviving tone delta”)
- close the track

If the fresh capture shows a **tone-only** mismatch and no structural mismatch:

- open a tiny follow-up under the existing P11 rules
- owning files limited to `maho_color_mixer.cc` / `maho_color_id.h`

If the fresh capture shows a **new structural** mismatch:

- do not improvise fixes in the same patch
- open a new narrowly named plan with the exact owning file and the exact checklist box that still reads “no”

## Explicit non-goals for this plan

Do not do any of the following as part of this closeout:

- change BrowserView URL prefill/select-all behavior for ordinary sites
- revert or broaden the internal-page suppression patch
- modify `maho_command_model.cc` or `maho_command_model.h`
- retune row-count, seeded ranking, or seed sources
- use full `interactive_ui_tests` as the acceptance bar when command-overlay-specific coverage is sufficient and unrelated sidebar blockers still exist
- make visual changes to `kNewTab`, `kAI`, or WebUI command surfaces

## Risks and mitigations

| Risk | Why it matters | Mitigation |
|------|----------------|------------|
| Divider fix accidentally hides separators in other modes | Search/recent lists may rely on current behavior | Add both a negative current-tab test and a positive non-current-tab control |
| Another wrong-branch change sneaks in | We just spent time on the wrong path | Freeze internal-page logic out of scope and name ordinary-site current-tab as the only acceptance target |
| Capture interpretation drifts again | Prior work mixed structural and orthogonal issues | Review only against the frozen R0/P7+ checklist and only at dark mode 2x |
| Documentation stays ambiguous | Same confusion can recur in later sessions | Phase B explicitly records what is acceptance vs guardrail |

## Completion bar

This plan is complete when all of the following are true:

1. divider is hidden in the real current-tab seeded path
2. there is unit coverage for the divider rule
3. there is runtime current-tab coverage for the divider rule
4. docs clearly separate ordinary-site parity from internal-page suppression
5. fresh user capture at the binding conditions shows either:
   - no visible delta, or
   - one explicitly named, narrower follow-up opened from the capture result

## Review request for Momus

When this file is saved, review it as:

- a **closeout plan**, not a replay of P1-P8
- a plan for the **ordinary-site current-tab path**, not browser-internal pages
- a plan that must prevent another wrong-branch implementation
