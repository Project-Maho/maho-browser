# Command Palette Current-Tab Parity Execution Plan (2026-05-19)

## Status

This is the **authoritative, current** execution plan for closing the Cmd+L (kCurrentTab) open-state visual gap. It supersedes:

- `command-palette-current-tab-reference-parity-plan.md`
- `command-palette-reference-parallel-plan.md`

Those documents contain stale assumptions (row-count-cap work as first-pass, capture-as-prerequisite, row-renderer overhaul scoped from scratch). Use this document for execution.

### 2026-05-20 Update: Acceptance Bar Tightened

P1 through P6 (input flatten, trailing accessory adjustment, selected seeded-row geometry, URL-preserving subtitle, title-weight policy, tests, rebuild/run recovery) have landed and are not to be redone. They closed the biggest blockers but did not close the parity gap.

The acceptance bar has been clarified and is now stricter than the original "first pass" framing in this document:

- **Done means: no first-frame visible delta versus the chosen authoritative reference capture.**
- "Close enough" / "biggest blocker fixed" is no longer acceptance.
- Specialist review confirmed real surviving deltas: selected-row favicon treatment, trailing input accessory still has pill chrome in code, and the divider/reference-disambiguation decision was never recorded.
- The white favicon tile is no longer classified as "disputed / optional." It is now in-scope closeout work (see P7).
- Color-token tuning is still last-resort and must not be used to mask a structural mismatch.

P7+ below is the closeout plan under this stricter bar. P1 through P6 history is preserved as-is below; do not edit those phases.

## Premise

The seeded render path, the 5-row cap, and the leading input-row favicon swap are already on disk. The remaining gap between the current build and the user's reference is concentrated in a handful of presentation seams. The work is small, file-bounded, and does not need new infrastructure to start.

Capture is a verification gate, not a precondition. The team can begin code work today; capture confirms each phase landed correctly.

## In Scope (First Pass)

1. Flatten/remove the inner pill chrome that currently wraps the input row content in `maho_command_overlay_view.cc`.
2. Replace or reshape the trailing input-row accessory badge (the right-side glyph/affordance next to the input field).
3. Tune selected seeded-row geometry (highlight rect, inset, radius, vertical fit) in `maho_command_result_row_view.cc`.
4. Rework the subtitle pipeline so seeded rows show a URL-preserving subtitle, not a host-only string truncated by tail-elide.
5. Decide and apply a title-weight policy for seeded rows (regular vs medium/semibold) and lock it in row code + a unit test.
6. Update the seeded unit test and interactive uitest to assert the new presentation contracts.
7. Update the manual capture harness notes (`docs/cmd-l-open-state-delta.md`) so each phase's verification step is checkable.

## Out of Scope (First Pass)

> Note (2026-05-20): The "first pass" out-of-scope list below applies to P1 through P6 history. It is preserved verbatim. Items that have since been promoted into closeout scope under the tightened acceptance bar are reclassified in the P7+ section, not by editing this list. In particular, the white favicon tile is no longer optional; see P7.

- White favicon tile / square favicon background. **Disputed.** Treated as optional and capture-gated. Do NOT introduce in first pass. If post-capture review still shows a clear delta, raise a follow-up scoped to the favicon container only.
- Blur, vibrancy, drop-shadow tuning, animation timing, motion polish.
- Row-count cap changes. The 5-row cap stays as shipped.
- Model-layer changes (`maho_command_model.cc/h`). Empty-query kCurrentTab continues to return `{}`.
- New sources beyond NavigationController back-stack (no bookmarks, no cross-tab history, no maho-core ranking).
- Section headers / group labels.
- Async favicon fetching. Sync cache only; fallback glyph on cache miss.
- Any work in `maho_command_overlay_controller.{cc,h}` (window geometry / centering is not a delta).
- Any work on kNewTab, kAI, or WebUI command surfaces.

## Already Implemented (Do Not Redo)

- Leading input-row favicon swap (the favicon area on the input row itself).
- Seeded 5-row cap (`kMaxSeededVisibleResults`).
- Synchronous seeded render path (seeds present on first paint, no async replacement).
- One-way seeded-state machine (typing exits seeded state, retyping the URL does not re-enter it).
- Helper-text suppression in seeded state.
- Active-descendant accessibility wiring on the seeded list.

These are verified by existing unit/interactive tests. Do not modify them.

## Phase Order (Sequential, No Skipping)

> History (2026-05-20): P1 through P6 below are **already implemented**. They are kept in this document as the executed first pass and are not to be re-scoped. New closeout work continues in P7+ at the bottom of this document.

| Phase | Theme | Files touched | Acceptance gate |
|-------|-------|---------------|-----------------|
| P1 *(done)* | Input-row chrome flatten | `maho_command_overlay_view.cc` (+`.h` only if a constant must be exposed) | Visual: input row reads as a single quiet row, no inner pill outline; `lsp_diagnostics` clean |
| P2 *(done)* | Trailing input accessory | `maho_command_overlay_view.cc` | Visual: trailing badge matches the reference shape/role; no layout shift on focus/blur |
| P3 *(done)* | Selected-row geometry | `maho_command_result_row_view.cc` (+`.h` only if a constant must be exposed) | Visual: selected row 0 highlight matches reference inset, radius, vertical fit |
| P4 *(done)* | Subtitle pipeline (URL-preserving) | `maho_command_result_row_view.cc` (+`.h` if a helper is added) | Each seeded row's subtitle shows a URL-preserving string, no premature host-only collapse, no tail-elide before the path is meaningful |
| P5 *(done)* | Title-weight policy | `maho_command_result_row_view.cc` | Decision recorded in code (single named constant), enforced by a unit test |
| P6 *(done)* | Tests + capture harness | `maho_command_overlay_view_unittest.cc`, `maho_command_result_row_view_unittest.cc`, `maho_command_overlay_interactive_uitest.cc`, `docs/cmd-l-open-state-delta.md` | All new asserts pass; capture harness step list updated to match the new visual contract |

Capture is run after each phase as a verification gate. It is not a prerequisite to starting P1.

---

## File-by-File Plan

### `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`

**Phases that touch this file:** P1, P2.

**P1 — Flatten inner input-row pill chrome**

- Locate the input-row container construction (the section that builds the textfield host plus its surrounding background/border).
- Remove (or neutralize) the inner pill background and border that currently visually box the input content. The outer card already provides containment; the inner pill is double-chrome.
- If a `views::Background` / rounded-rect border is being applied to the input row's content host, drop it. If a `views::FlexLayout` insets that pad the pill should leave with it, fold those into the row's outer insets so the typography position does not jump.
- Keep the leading favicon swap (already shipped) untouched.

**P2 — Trailing input accessory**

- Locate the trailing accessory area attached to the input row (the right-side glyph or affordance, e.g. clear button / mode badge / action icon).
- Replace its visual treatment to match the reference: correct glyph, correct size, no inner pill, correct trailing inset.
- If the accessory is currently a button with hover/focus chrome that does not exist in the reference, simplify to a static glyph or to the minimal interactive treatment the reference shows.
- Keyboard activation behavior (if any) must continue to work; do not regress focus order.

**Must NOT change here:**

- Do not call `TriggerSearchForCurrentText()` / `Search()` from `InitializeCurrentTabSeedState()`.
- Do not change `BuildCurrentTabSeedResults()` row-count loop or dedup.
- Do not modify `kMaxSeededVisibleResults`.
- Do not introduce blur/vibrancy/animation work.
- Do not adjust card centering or window geometry.

**Verification:**

- `lsp_diagnostics` clean.
- Manual capture: input row silhouette matches reference; trailing accessory matches reference.
- All existing seeded unit tests still pass unmodified.

---

### `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h`

**Phases that touch this file:** P1, P2 (only if a constant or accessor needs to be exposed for tests).

- Header changes are allowed only when:
  - A new constant introduced in `.cc` must be referenced from a unit test, or
  - A new internal helper needs forward declaration.
- Do not add public API for the input-row pill removal or accessory rework. These are internal layout decisions.
- Do not add accessors for ranking, query dispatch, or model state.

---

### `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`

**Phases that touch this file:** P3, P4, P5.

**P3 — Selected-row geometry**

- Identify the path that paints the selected highlight (background fill + radius + insets).
- Match the reference: the highlight is a tight pill that hugs row content with the correct horizontal inset from the card edge and the correct vertical inset from row top/bottom.
- Vertical fit: the highlight should not be a tall block that bleeds into the row's own padding. If row height was inflated for a two-line layout, drop it to fit the actual single-line content.
- Corner radius: use a single named constant. Do not scatter literal radii.
- Selection contrast must remain accessible against the card background.

**P4 — Subtitle pipeline (URL-preserving)**

- The current pipeline is **host-only + tail-elide**: the subtitle string is reduced to `host` (or `host + path-tail`) and then truncated. The reference preserves more of the URL.
- Replace the subtitle build step:
  - Source the subtitle from the full URL (or a URL-preserving formatter), not from `host` alone.
  - Apply elision only at render time, with head-elide or middle-elide where the reference indicates a path is preserved over the host. Use Chromium's URL formatting helpers (`url_formatter::FormatUrl` with appropriate flags) where available, rather than ad-hoc string trimming.
  - Keep the seeded `CommandSuggestion` payload unchanged on the model side; the rework is in how the row consumes it.
- Edge case: if the URL is `about:blank` or has no meaningful path, fall back to the host. Do not show a blank subtitle.
- The subtitle color / opacity / size policy should not change in this phase; only the **string content** changes. Typography stays as-is unless P5 explicitly touches it.

**P5 — Title-weight policy**

- Decide between regular and medium/semibold for seeded row titles. The reference is the tiebreaker; the chosen weight must match it.
- Encode the chosen weight as a single named constant (e.g. `kSeededRowTitleFontWeight`).
- Apply the weight only to seeded rows. Non-seeded (typed-query) row titles must continue to render with their existing weight; do NOT broaden this change to all rows.
- If seeded vs non-seeded row distinction does not currently exist on the row view, introduce a minimal flag (e.g. a `SetIsSeeded(bool)` setter, or a parameter on `SetSuggestion`) and gate the weight on it. Keep the flag scoped to typography decisions only.

**Must NOT change here:**

- Do not introduce a white favicon tile / square favicon container. That work is capture-gated and out of first-pass scope.
- Do not add async favicon loading.
- Do not add inter-row dividers.
- Do not add section header rendering.
- Do not change row activation semantics or click handling.

**Verification:**

- `lsp_diagnostics` clean.
- Unit test asserting title weight constant is applied to seeded rows.
- Manual capture: selected row matches reference highlight; subtitles preserve the URL; title weight matches reference.

---

### `maho-chromium/browser/ui/views/command/maho_command_result_row_view.h`

**Phases that touch this file:** P3, P4, P5 (only as needed).

- Allowed:
  - Expose a named constant if its value must be referenced from a unit test (e.g. `kSelectedRowCornerRadius`, `kSeededRowTitleFontWeight`).
  - Add a small setter/getter pair if a seeded-vs-non-seeded flag is introduced (`SetIsSeeded`, `IsSeededForTesting`).
- Forbidden:
  - New public API for favicon containers, animation, or model interaction.
  - Changes to selection model semantics.

---

### `maho-chromium/browser/ui/views/command/maho_command_model.cc`

**Phases that touch this file:** none.

- Must remain unchanged. Empty-query kCurrentTab returning `{}` is a precondition the seeded view depends on.
- `git diff maho_command_model.cc` after first pass should be empty.

---

### `maho-chromium/browser/ui/views/command/maho_command_model.h`

**Phases that touch this file:** none.

- No new fields on `CommandSuggestion`. No new methods. No new modes.
- `git diff maho_command_model.h` after first pass should be empty.

---

### `maho-chromium/browser/ui/views/command/maho_command_overlay_view_unittest.cc`

**Phases that touch this file:** P6.

Add the following tests. Each must be deterministic (no async waits) and must use the existing `ForTestingTag` constructor unless a real Browser is required (in which case it belongs in the interactive uitest).

| Test name | Asserts |
|-----------|---------|
| `CurrentTabSeed_InputRowHasNoInnerPillBackground` | The input-row content host does not paint a rounded-rect background distinct from the outer card |
| `CurrentTabSeed_TrailingAccessoryMatchesContract` | The trailing accessory child exists with the expected role/glyph and is laid out at the trailing edge |
| `CurrentTabSeed_HelperTextStillSuppressed` | Existing contract preserved after P1/P2 layout changes |

If exposing internal layout requires new test seams, prefer adding `*ForTesting` accessors over reflecting via geometry probes.

---

### `maho-chromium/browser/ui/views/command/maho_command_result_row_view_unittest.cc`

**Phases that touch this file:** P6.

| Test name | Asserts |
|-----------|---------|
| `SeededRow_SelectedHighlightUsesExpectedInsetAndRadius` | When `SetSelected(true)` is called on a seeded row, the highlight bounds match the named inset constant and radius constant |
| `SeededRow_SubtitlePreservesUrl` | Given a seeded suggestion with URL `https://example.com/some/path?q=1`, the subtitle string is URL-derived (contains path component) and is not reduced to `example.com` alone |
| `SeededRow_SubtitleFallsBackToHostForEmptyPath` | URL like `https://example.com/` produces a non-empty subtitle (host) |
| `SeededRow_TitleUsesSeededWeightConstant` | The seeded title label's font weight equals `kSeededRowTitleFontWeight`; non-seeded rows do not |

If `maho_command_result_row_view_unittest.cc` does not yet exercise selection geometry, this is the place to add a minimal harness rather than expanding the overlay-level unittest.

---

### `maho-chromium/browser/ui/views/command/maho_command_overlay_interactive_uitest.cc`

**Phases that touch this file:** P6.

| Test name | Asserts |
|-----------|---------|
| `CurrentTabSeed_InputRowChromeFlatAtRuntime` | At the open frame, the input-row layout has no inner-pill background view in the painted hierarchy |
| `CurrentTabSeed_RowSubtitlePreservesUrlPath` | After navigating to a URL with a non-trivial path and opening Cmd+L, the corresponding seeded row's rendered subtitle contains a path-derived substring |
| `CurrentTabSeed_TitleWeightMatchesPolicyAtRuntime` | The seeded title label's resolved `gfx::Font` weight matches `kSeededRowTitleFontWeight` |

Do not add tests that depend on pixel-exact comparisons; use view-tree and font-metric assertions. Pixel comparison stays manual via the capture harness.

---

### `docs/cmd-l-open-state-delta.md` (capture harness notes)

**Phases that touch this file:** P6 (and once after each preceding phase if findings need to be appended).

Update the document so each phase has a checkable verification step:

- P1 verification: input-row silhouette comparison checklist (no inner pill, leading favicon retained, trailing accessory present).
- P2 verification: trailing accessory shape, glyph, and inset checklist.
- P3 verification: selected-row highlight inset / radius / vertical fit checklist.
- P4 verification: subtitle string content checklist for at least three URL shapes (root path, deep path, query string).
- P5 verification: title-weight visual check + reference of the named constant.
- P6 capture sweep: full side-by-side; flag any remaining deltas as either follow-ups or scope-violations.

Do not delete prior content. Append a "2026-05-19 Execution Pass" section.

---

### `.sisyphus/artifacts/command-palette-parity/capture_command_palette.py`

**Phases that touch this file:** verification harness for P1 through P6. Not a prerequisite for starting P1.

This script is a verification instrument. Code work in P1 begins regardless of whether capture is wired up. Capture confirms each phase landed; it does not gate the start of any phase.

First-pass requirements when the script is exercised:

- **Launch-flag pinning.** The driver must launch the build with a fixed flag set so output is reproducible across runs and machines. Required flags:
  - `--network-service-in-process` (avoids out-of-process network races during the open frame)
  - `--no-first-run` (suppresses first-run UI that would shift layout)
  - explicit `--user-data-dir=<temp profile>` (no leakage from the developer's real profile, no theme/extension contamination)
- **Deterministic history fixture.** The seeded kCurrentTab rows depend on NavigationController back-stack contents. The script must serve a local history fixture (a small static set of pages) and drive navigation through it before opening Cmd+L, so the seeded rows are identical run-to-run. No live network, no real-profile history.
- **Dark mode and DPR documentation.** Every captured frame must record the color scheme (light / dark) and the device pixel ratio used. A capture taken at 2x in dark mode is not comparable to a capture taken at 1x in light mode. The harness writes both values into the capture's metadata sidecar.
- **Dwell / wait sufficient for the chain fixture.** The capture must wait long enough after Cmd+L for the seeded render path and any synchronous favicon cache hits to settle, but not so long that it captures a state past first frame. Document the chosen wait, justify it from the chain fixture's expected timing, and keep it as a single named constant in the script.

**Must NOT do here:**

- Do not promote this script to a P1 prerequisite. Code work starts without it.
- Do not introduce pixel-diff thresholds that gate CI. Capture stays manual; comparison stays human-reviewed against the reference.
- Do not depend on the developer's real Chromium profile or live network.

**Verification:**

- Two consecutive runs against the same build produce capture frames whose metadata (color scheme, DPR, wait) and history fixture state are identical.
- Captures from P1 through P6 are reviewable side-by-side against the reference without ambiguity about which mode/DPR they were taken in.

---

### `maho-chromium/browser/ui/theme/maho_color_mixer.cc` and `maho-chromium/browser/ui/theme/maho_color_id.h` (capture-gated follow-up)

**Phases that touch these files:** none in P1 through P6. **Capture-gated follow-up only.**

Do not start here. Tuning theme tokens before the layout work in P1 through P5 is locked is the wrong order: a token change masks a geometry/string bug instead of fixing it.

These files become candidates only if, after P1 through P6 each meet their acceptance gate AND the final side-by-side capture is recorded, manual review still shows a real first-frame color delta against the reference (e.g. selected-row fill, subtitle color, divider tone, card surface). In that case:

- Identify the specific token(s) producing the delta. Do not blanket-edit the mixer.
- If a new token is needed, declare it in `maho_color_id.h` and resolve it in `maho_color_mixer.cc`. Do not hardcode colors at the row/view layer.
- Keep the change scoped to the seeded command palette surface. Do not retune tokens shared with unrelated UI without an explicit follow-up.
- Re-run the capture harness after the change and append the comparison to `docs/cmd-l-open-state-delta.md`.

**Must NOT do here:**

- Do not edit these files as part of P1 through P6.
- Do not use a token tweak to compensate for a missing P3 highlight inset, a P4 subtitle string bug, or a P5 weight policy mistake. Fix the layout/string/typography first, then evaluate the token delta.
- Do not bundle the white favicon tile work into a token change. The favicon tile remains classified as **disputed / optional / capture-gated** and is a separate scoped follow-up if it survives review.

**Verification (only if this follow-up is taken):**

- Capture before and after the token change at the same color scheme and DPR.
- The remaining delta the follow-up was opened for is gone; no new delta has been introduced on adjacent surfaces.

---

## Acceptance Gates (Per Phase)

A phase is complete only when ALL of the following hold:

1. `lsp_diagnostics` clean for every file touched in that phase.
2. All existing unit + interactive tests in the command palette directory still pass.
3. New tests added for that phase pass.
4. Manual capture for that phase recorded in `docs/cmd-l-open-state-delta.md` and shows the expected change.
5. No file outside the phase's listed files was modified.
6. No item from "Out of Scope" was silently introduced.

The overall first pass is complete when P1 through P6 each meet their gate AND a final side-by-side capture shows no remaining first-frame visible delta in any in-scope category.

If the final capture shows a remaining delta, the response is one of:

- Re-open the corresponding phase and fix it, OR
- File a scoped follow-up (e.g. for the disputed white favicon tile) and ship the first pass without that item.

Do not silently broaden scope to absorb the delta.

---

## Risk Notes (File-Bound)

| Risk | File | Mitigation |
|------|------|------------|
| Removing input-row pill background also removes a focus ring users rely on | `maho_command_overlay_view.cc` (P1) | Keep the textfield's intrinsic focus indicator. Only remove the surrounding container chrome, not the textfield's own focus painting |
| Trailing accessory rework breaks keyboard focus order | `maho_command_overlay_view.cc` (P2) | Run interactive uitest covering Tab/Shift-Tab through the input row before/after |
| Selected-row geometry change affects non-seeded rows | `maho_command_result_row_view.cc` (P3) | If the change must be seeded-only, gate via the seeded flag introduced in P5; otherwise verify both seeded and typed-query rows still look correct |
| Subtitle pipeline change leaks to non-seeded rows | `maho_command_result_row_view.cc` (P4) | Apply the new subtitle formatter only when row source is `kNavigation` seeded; verify typed-query rows unchanged |
| Title-weight change broadens to all rows | `maho_command_result_row_view.cc` (P5) | Gate on the seeded flag. Add a typed-query-row test that asserts unchanged weight |
| White favicon tile sneaks in via P3 highlight rework | `maho_command_result_row_view.cc` (P3) | Highlight rework operates on the row background only; favicon container stays untouched. Code review must reject any `views::Background` added on the favicon view in P3 |
| `maho_command_model.cc` modified to "help" with seeded data | `maho_command_model.{cc,h}` | `git diff` of those two files must be empty at the end of P5; reject otherwise |

---

## Execution Checklist (Quick-Use)

- [ ] P1: flatten input-row inner pill in `maho_command_overlay_view.cc`; capture; lsp_diagnostics clean
- [ ] P2: rework trailing input accessory in `maho_command_overlay_view.cc`; capture; lsp_diagnostics clean
- [ ] P3: tune selected-row geometry in `maho_command_result_row_view.cc`; capture; lsp_diagnostics clean
- [ ] P4: rework subtitle pipeline to URL-preserving in `maho_command_result_row_view.cc`; capture; lsp_diagnostics clean
- [ ] P5: lock seeded-row title-weight policy in `maho_command_result_row_view.cc`; capture; lsp_diagnostics clean
- [ ] P6: add unit + interactive tests; update `docs/cmd-l-open-state-delta.md`; full side-by-side capture
- [ ] `git diff maho_command_model.cc maho_command_model.h` is empty
- [ ] No work landed in `maho_command_overlay_controller.{cc,h}`
- [ ] White favicon tile not introduced (or filed as a separate follow-up after capture review)

---

## Notes

- This plan keeps the model layer frozen and concentrates all visible change in the view + row files. That is intentional: the seeded path's correctness depends on the model returning empty for empty kCurrentTab queries.
- Capture is treated as the verification instrument, not the gating instrument. Code can move first; capture confirms.
- The white favicon tile is the most likely follow-up after the first pass. Treat it as a discrete, scoped item; do not let it pull in container chrome rework across the row.

---

# P7+ Closeout (2026-05-20+)

## Acceptance Bar (Restated)

Closeout is complete only when the open Cmd+L frame, captured under the harness in `docs/cmd-l-open-state-delta.md`, shows **no first-frame visible delta** versus the chosen authoritative reference capture, at the same color scheme and DPR.

"Biggest blocker fixed," "structurally close," and "matches in spirit" are not acceptance. The reference frame is the spec. Each P7+ phase is a discrete delta to close.

## Reference-Disambiguation Decision (Pre-Code Gate)

Two reference captures are in circulation (Arc-style and the prior internal capture). They differ on at least one structural item (presence/absence of inter-row dividers, and possibly the active-tab URL chip treatment in the input row). No P7+ code change may land before this is recorded.

**Pre-code task R0 (planning, not implementation):**

- File: `docs/cmd-l-open-state-delta.md`
- Action: append a "Reference Selection (2026-05-20)" subsection that names exactly one capture as authoritative, links it, and lists each point of divergence between the two candidates with the chosen resolution.
- Specifically resolve:
  - Inter-row dividers: present, absent, or hairline-on-selection-only.
  - Active-tab URL treatment in the input row: bare URL text, selection-chip, or selected-state input rendering.
  - Trailing input accessory shape: bare glyph, glyph + tonal background, or pill.
  - Selected-row leading favicon container: bare favicon, white tile/chip, or tonal chip.
- Acceptance gate for R0: the document update is committed, every later P7+ phase cites the chosen reference by name. No code phase may begin until R0 is recorded.

## P7 — Selected-Row Favicon Tile / Chip

**Reclassified:** previously "disputed / optional / capture-gated." Under the tightened bar, the favicon tile is in-scope closeout work. The selected seeded row in the reference shows a distinct favicon container treatment that the current build does not render. This phase closes that delta.

- Owning file: `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc` (+ `.h` only if a new constant or seam must be exposed for tests).
- Scope:
  - Introduce a favicon container view (or reuse an existing leading-icon host) that paints the reference's tile/chip background only on the seeded row's leading favicon.
  - Match the reference: container size, corner radius, fill tone, inset from row edge, and inset between container and favicon glyph. Encode each as a single named constant.
  - Apply the container only when the row is seeded (kNavigation seeded) AND, if the reference treatment is selection-conditional, only when selected. The decision (always-on vs selected-only) is taken from the R0 record.
  - Color: do not hardcode. If a new tone is required, route through `maho_color_id.h` + `maho_color_mixer.cc` (see P11).
- Must NOT:
  - Apply the tile/chip to non-seeded rows.
  - Repaint the row background as a workaround for a missing favicon container.
  - Add async favicon loading.
- Acceptance gate:
  - `lsp_diagnostics` clean on touched files.
  - Unit test `SeededRow_SelectedFaviconUsesContainerTreatment` asserts the favicon container view exists with the named radius and inset constants when the row is seeded and (if applicable) selected.
  - Manual capture: leading favicon area on selected seeded row matches the reference at the chosen color scheme and DPR. No regression on non-seeded rows.

## P8 — Trailing Input Accessory: Hide in kCurrentTab

P2 adjusted the accessory's pill chrome, but specialist review of the chosen reference confirmed there is **no trailing accessory at all** on the kCurrentTab open frame. Under R0 D2 (see `docs/cmd-l-open-state-delta.md`), the accessory must be hidden entirely on this surface, not reshaped to a bare glyph.

- Owning file: `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc` (+ `.h` only if a constant or seam must be exposed for tests).
- Scope:
  - Hide the trailing input accessory whenever the overlay is in kCurrentTab open state. Nothing is rendered at the trailing edge: no glyph, no pill, no tonal chip, no rounded-rect border.
  - Hiding may be implemented by skipping accessory construction in the kCurrentTab branch, by setting the host view's visibility off and excluding it from layout, or by an equivalent guard. The accessory must not paint, must not occupy layout space, and must not be focusable in the kCurrentTab view tree.
  - Fold any inset that existed only to position the now-hidden accessory back into the input row's outer trailing inset so the textfield's baseline and trailing edge do not shift between frames.
  - Preserve keyboard activation, focus order, and existing interactive behavior on any non-kCurrentTab surface that still renders the accessory. Do not regress the focus traversal that P2 verified on those surfaces.
- Must NOT:
  - Render the trailing accessory in any form (bare glyph, tonal chip, pill, rounded-rect background) on kCurrentTab.
  - Leave a zero-size or invisible-but-focusable accessory child in the kCurrentTab input row.
  - Touch the leading favicon swap.
  - Touch input-row pill flatten work from P1.
- Acceptance gate:
  - `lsp_diagnostics` clean.
  - Unit test `CurrentTabSeed_TrailingAccessoryNotVisible` asserts no trailing accessory view is present (or is non-visible and non-focusable, with no painted background) on the input row at the kCurrentTab open frame.
  - Interactive uitest covering Tab / Shift-Tab through the input row passes unchanged and does not stop on a hidden accessory.
  - Manual capture: the trailing edge of the input row matches the reference, with no accessory rendered.

## P9 — Divider / Reference-Disambiguation Application

Apply the divider decision recorded in R0 to the row layout. This is a separate phase from R0 because R0 is a planning record and P9 is the code change.

- Owning file: `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc` (+ `.h` only if a constant or seam must be exposed). If divider painting belongs at the list container level instead of the row level, the alternative owning file is `maho_command_overlay_view.cc`; the choice follows existing layout ownership in the codebase, not new infrastructure.
- Scope:
  - If R0 chose "no dividers": ensure no inter-row separator is painted; remove any leftover separator code or zero-height separator views.
  - If R0 chose "hairline dividers": paint a 1px separator between seeded rows (excluding above the first and below the last). Color must come from a named token, not a literal.
  - If R0 chose "selection-conditional hairline": paint only above/below the selected row, or per the recorded rule.
  - In all cases: divider applies only to the seeded list (kCurrentTab open state). Typed-query rows are unaffected.
- Must NOT:
  - Decide the divider rule inside this phase. The rule comes from R0.
  - Use a non-tokened color.
  - Introduce dividers on non-seeded rows.
- Acceptance gate:
  - `lsp_diagnostics` clean.
  - Unit test `SeededList_DividerPolicyMatchesReferenceRecord` asserts the divider behavior matches the R0 record (parameterized on the recorded rule).
  - Manual capture: row-to-row separation in the seeded list matches the reference.

## P10 — Active-Tab URL Selection-Chip / Input Selected-State (Conditional)

This phase runs **only if** R0 records that the authoritative reference shows a selection-chip or selected-state treatment on the active-tab URL inside the input row. If R0 records a bare URL, P10 is skipped and explicitly marked N/A in `docs/cmd-l-open-state-delta.md`.

- Owning file: `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc` (+ `.h` only if a constant or seam is exposed).
- Scope:
  - Apply the chosen treatment to the URL text inside the input row at the open frame: highlight background, selection range, or chip outline as recorded.
  - Treatment must clear the moment the user types a character. The seeded-state machine's exit condition does not change.
  - Color routes through `maho_color_id.h` + `maho_color_mixer.cc` if a new tone is required.
- Must NOT:
  - Apply outside the open frame (no persistent chip on subsequent renders).
  - Modify seeded-state entry/exit semantics from `InitializeCurrentTabSeedState()`.
  - Touch model-layer code.
- Acceptance gate:
  - `lsp_diagnostics` clean.
  - Interactive uitest `CurrentTabSeed_InputUrlSelectedStateAtOpenFrame` asserts the recorded treatment is present at the open frame and is gone after a single key press.
  - Manual capture: input-row URL treatment matches the reference at the open frame.
  - If skipped: `docs/cmd-l-open-state-delta.md` records "P10 N/A — reference is bare URL" and no code change is made.

## P11 — Color Token Follow-Up (Last Resort, Conditional)

Runs **only if** P7 through P10 are all green AND the side-by-side capture still shows a real color delta on the seeded surface (selected-row fill, subtitle color, divider tone, favicon container fill, or input-row URL treatment tone). Token tuning is not allowed before structural fixes.

- Owning files: `maho-chromium/browser/ui/theme/maho_color_mixer.cc`, `maho-chromium/browser/ui/theme/maho_color_id.h`.
- Scope:
  - Identify the specific token producing the surviving delta. Touch only that token (or add a new one scoped to the seeded surface).
  - If a new token is needed, declare it in `maho_color_id.h` and resolve it in `maho_color_mixer.cc`. Do not hardcode colors in row/view code.
  - Keep the change scoped to the seeded command palette surface. Do not retune tokens shared with unrelated UI.
- Must NOT:
  - Be used to hide a missing favicon container, missing divider, or wrong accessory treatment. If the delta has any structural component, return to P7-P10.
  - Blanket-edit the mixer.
  - Bundle multiple unrelated token changes.
- Acceptance gate:
  - Capture before and after at the same color scheme and DPR.
  - The targeted delta is gone; no new delta on adjacent surfaces.
  - `docs/cmd-l-open-state-delta.md` records the before/after.
  - If no color delta survives after P7-P10: P11 records "N/A — no surviving tone delta" and no code change is made.

## P7+ Phase Map (Owners)

| Phase | Owning file(s) / function area | Acceptance artifact |
|-------|-------------------------------|---------------------|
| R0 | `docs/cmd-l-open-state-delta.md` (Reference Selection section) | Reference capture named, divergences resolved |
| P7 | `maho_command_result_row_view.cc` (favicon container construction in row layout / paint) | `SeededRow_SelectedFaviconUsesContainerTreatment` + capture |
| P8 | `maho_command_overlay_view.cc` (trailing accessory hidden in kCurrentTab) | `CurrentTabSeed_TrailingAccessoryNotVisible` + focus uitest + capture |
| P9 | `maho_command_result_row_view.cc` or `maho_command_overlay_view.cc` (divider painting) | `SeededList_DividerPolicyMatchesReferenceRecord` + capture |
| P10 *(conditional)* | `maho_command_overlay_view.cc` (input row URL treatment at open frame) | `CurrentTabSeed_InputUrlSelectedStateAtOpenFrame` + capture, or recorded N/A |
| P11 *(conditional)* | `maho_color_mixer.cc`, `maho_color_id.h` | Before/after capture at same color scheme + DPR, or recorded N/A |

## Per-Phase Acceptance Gate (P7+)

A P7+ phase is complete only when ALL of the following hold:

1. R0 is recorded and cited by the phase.
2. `lsp_diagnostics` clean for every file touched in that phase.
3. All existing unit + interactive tests in the command palette directory still pass.
4. The phase's named test passes (or the phase is recorded N/A in `docs/cmd-l-open-state-delta.md` per its conditional rule).
5. Manual capture for that phase is recorded in `docs/cmd-l-open-state-delta.md` and shows the targeted delta closed against the chosen reference, at the same color scheme and DPR.
6. No file outside the phase's listed files was modified.
7. No structural delta was masked with a token change.

## Final Done Definition (Closeout)

Closeout is done when ALL of the following hold:

1. R0 is recorded; the reference is named; every divergence is resolved.
2. P7, P8, P9 each meet their acceptance gate.
3. P10 either meets its acceptance gate or is recorded N/A per R0.
4. P11 either meets its acceptance gate or is recorded N/A.
5. A final side-by-side capture, taken at the chosen reference's color scheme and DPR, shows **no first-frame visible delta** versus the reference on the open Cmd+L (kCurrentTab) surface.
6. `git diff maho_command_model.cc maho_command_model.h` is empty.
7. No work landed in `maho_command_overlay_controller.{cc,h}`.
8. No work landed in kNewTab, kAI, or WebUI command surfaces.

If a delta survives after P11, the response is one of:

- Re-open the corresponding phase (P7, P8, P9, P10, or R0) and fix the structural cause, OR
- File an explicitly-scoped follow-up that names the surviving delta and the file that owns it.

Do not silently broaden scope. Do not declare done while a visible delta remains.
