# Cmd+L Open-State Delta: Reference vs. Current Chromium

## Reference Assets

- **Primary reference:** `maho/docs/screenshots/arc/palette-tab-active.png` (Arc current-tab mode, active page)
- **Secondary reference:** `maho/docs/screenshots/arc/command-palette.jpeg` (Arc command palette general)

## Current Chromium Asset

- **Runtime capture:** `.sisyphus/artifacts/command-palette-parity/after-fix/ui-pass-currenttab-20260508a_00_open.png`

## Observation Summary

| Property | Reference (Arc) | Current Chromium |
|----------|----------------|-----------------|
| Visible rows | 5 | 2 |
| Section headers | None | None |
| Row separators/dividers | None visible between rows | None visible between rows |
| Highlighted row treatment | Solid blue/indigo pill, single-line | Two-line treatment |
| Row text layout | Inline single-line: Title — subtitle/domain | Two-line (title on one line, subtitle on second) |
| Icon treatment | Full-color favicon | Differs (glyph/monochrome) |
| Card shape | Dark rounded card, tight content fit | Much shorter total height |

---

## Numbered Deltas

### 1. Row count

Reference shows 5 visible rows. Chromium shows 2 visible rows.

**Blocking.** The seed builder must produce 5 rows when history is sufficient.

### 2. Row text treatment (single-line vs. two-line)

Reference uses a compact single-line layout per row: `Title — subtitle/domain` on one line. The highlighted row follows this same pattern (solid pill wrapping the single-line content).

Chromium currently renders the highlighted row as a two-line treatment (title above, subtitle below).

**Blocking.** Row view must switch to inline single-line `Title — domain` format.

### 3. Selection highlight shape

Reference: solid blue/indigo pill fill wrapping the full row width. Tight vertical fit around the single-line text content. Rounded corners (pill radius).

Chromium: highlight treatment differs (larger vertical extent because of two-line layout, likely different radius/color).

**Blocking.** Selection highlight must become a pill-shaped fill matching reference color and radius, sized to single-line row height.

### 4. Section headers

Reference: absent. Chromium: absent.

**No delta.** Confirmed out of scope.

### 5. Row separators / dividers between rows

Reference: no visible separators or dividers between result rows. The list is a flat sequence of rows with spacing only.

Chromium: no visible separators between rows.

**No delta.** No divider between rows is the correct target. The plan's previous reference to a "divider between input and results" is a separate concern (input-to-results boundary), but between rows specifically, there is nothing to add.

### 6. Icon/favicon treatment

Reference shows full-color favicons (site icons) at the leading edge of each row.

Chromium shows a different glyph/icon treatment (likely monochrome fallback globe or document icon).

**Blocking.** Rows must display cached favicons when available, falling back to a glyph only when the sync cache has no entry.

### 7. Card height and content fit

Reference: dark rounded card, height fits tightly around 5 rows of single-line content. No extraneous whitespace below the last row.

Chromium: total card height much shorter (only 2 rows, and possibly padded differently).

**Blocking.** Once row count is fixed to 5 and row height is reduced to single-line, card must resize to tightly fit content.

### 8. Row spacing and alignment

Reference: rows are closely spaced with minimal vertical gap. Horizontal padding is consistent. Icon-to-text gap is tight.

Chromium: with only 2 rows and two-line treatment, spacing characteristics differ fundamentally.

**Blocking.** After switching to single-line rows, row height, gap, and padding must be tuned to match reference density.

### 9. Typography

Reference: title text appears as regular weight, subtitle/domain text appears lighter or smaller, both on the same line separated by an em-space or dash character.

Chromium: title and subtitle rendered on separate lines with potentially different size/weight treatment.

**Blocking.** Typography must collapse to single-line with appropriate inline separator between title and domain.

### 10. Input field state

Both reference and Chromium show the URL prefilled and selected. No observed delta on input state itself.

**No delta.**

---

## Resolved Gates

| Gate | Question | Resolution |
|------|----------|------------|
| G1 | Section headers present in reference? | **NO.** Reference shows no section headers. Explicitly out of scope for first-pass. |
| G2 | Favicons per row? | **YES.** Reference shows full-color favicons. Implementation must use sync-cached favicons, fallback glyph for cache misses. |
| G3 | Row count cap correct? | **NO.** `kMaxSeededVisibleResults = 8` should be lowered to `5` to match reference. |
| G4 | Card height correct for 5 rows? | **UNKNOWN until row treatment is fixed.** After single-line row conversion and 5-row target, `ResizeWidgetToContents()` must be verified. Almost certainly needs adjustment. |

---

## First-Pass Implementation Must Change

The following changes are required to reach parity with the reference open-state:

1. **Row count:** Seed builder must produce 5 rows (lower `kMaxSeededVisibleResults` from 8 to 5).
2. **Row layout:** Switch from two-line (title + subtitle stacked) to single-line inline (`Title — domain`) per row.
3. **Selection highlight:** Change to a solid blue/indigo pill fill sized to the new single-line row height.
4. **Icons:** Add sync favicon cache lookup; display full-color favicons where cached.
5. **Card sizing:** After the above changes, verify and adjust overlay height to tightly fit input + 5 single-line rows.
6. **Row spacing:** Tune vertical rhythm, horizontal padding, and icon-to-text gap to match reference density.
7. **No row dividers:** Confirm no separators are rendered between rows (already correct, keep it that way).
8. **No section headers:** Confirm none are rendered (already correct, keep it that way).

---

## 2026-05-19 Execution Pass

This execution pass follows `.sisyphus/plans/command-palette-current-tab-parity-execution-plan-2026-05-19.md` exactly for the native `kCurrentTab` open state first pass.

### P1 verification — input-row silhouette

- Confirm the current-tab input row has **no inner pill background or border** distinct from the outer card.
- Confirm the **leading favicon swap remains intact** when the active tab has a cached favicon.
- Confirm the trailing accessory is still present at the far trailing edge.

### P2 verification — trailing accessory shape/glyph/inset

- Confirm the current-tab trailing accessory remains the **`i` info affordance**.
- Confirm it renders with the **flattened/quiet treatment** for current-tab open state rather than a keycap-style badge.
- Confirm the accessory sits flush to the trailing edge with the expected inset and **does not shift on focus/blur**.

### P3 verification — selected-row geometry

- Confirm seeded row 0 uses the tightened selected highlight geometry.
- Check the highlight for the expected **horizontal inset**, **reduced corner radius**, and **tighter vertical fit**.
- Confirm no white favicon tile was introduced as part of the selection geometry work.

### P4 verification — subtitle string content

Check at least these URL shapes in the seeded current-tab list:

- **Root path:** `https://example.com/` → subtitle falls back to `example.com` and is non-empty.
- **Deep path:** `https://example.com/some/path` → subtitle preserves `/some/path` instead of collapsing to host-only.
- **Query string / fragment:** `https://example.com/some/path?q=1#frag` → subtitle preserves the path and suffix rather than tail-eliding to host-only on first render.

### P5 verification — title-weight policy

- Seeded row titles use the named constant `kSeededRowTitleFontWeight`.
- Compare seeded rows against non-seeded rows to ensure the weight policy stays **seeded-only**.

### P6 capture sweep

- Run the local capture/QA path side-by-side against the Arc reference.
- Classify any remaining deltas as either:
  - a follow-up inside P1–P5 scope that must be fixed, or
  - an explicit follow-up outside first pass scope, such as the **disputed white favicon tile**.
- Do **not** use the capture sweep to broaden scope into blur, vibrancy, shadows, animation, row-count, or model-layer work.

---

## Reference Selection (2026-05-20)

This section is the R0 record required by `.sisyphus/plans/command-palette-current-tab-parity-execution-plan-2026-05-19.md` (P7+ closeout). It freezes exactly one authoritative reference for the current closeout pass and resolves every previously drifting question with a yes/no statement. No P7+ code phase may begin or be re-started without citing this section.

### Authoritative reference (primary, frozen)

- **Primary, frozen:** `maho/docs/screenshots/arc/palette-tab-active.png`.
  - This is the Arc current-tab open state at the active page. It is the spec for the open Cmd+L (kCurrentTab) first frame.
  - Every P7+ phase (P7, P8, P9, P10, P11) cites this file by name as the comparison target.
  - Closeout captures must be taken at the same color scheme and DPR as this reference.

### Supporting reference (secondary, non-binding)

- **Secondary, supporting only:** `maho/docs/screenshots/arc/command-palette.jpeg`.
  - General Arc command palette context. Useful for sanity-checking shared chrome (card silhouette, corner radius, drop tone).
  - Not authoritative. Where the secondary disagrees with the primary, the primary wins.
  - Do not cite the secondary as the gate for any P7+ acceptance.

### Resolved divergences (yes/no, no further debate)

The four questions that kept drifting between attempts are resolved here, against the frozen primary reference. Each P7+ phase must implement the resolution as written.

| # | Question | Resolution |
|---|----------|------------|
| D1 | Is there a divider between input row and results, or between result rows? | **No.** No inter-row divider. No input-to-results divider. The card is a flat stack of rows over a single surface tone. P9 must implement the "no dividers" branch and remove any leftover separator code. |
| D2 | What shape/treatment does the trailing input accessory take? | **Hidden in kCurrentTab.** No trailing accessory is rendered in the current-tab open state. **No glyph, no pill, no tonal chip, no rounded-rect border.** Nothing paints at the trailing edge of the input row, and no focusable accessory child remains in the view tree. P8 hides the accessory entirely on the kCurrentTab surface (skip construction or set visibility off in that branch); the trailing inset that previously padded the accessory folds back into the input row's outer trailing inset so the textfield baseline does not shift. |
| D3 | What treatment does the selected-row leading favicon container take? | **White tile/chip.** The selected seeded row paints a small white (light-mode) rounded-square tile behind its favicon glyph. The tile is bounded by a single named radius constant and a single named inset constant. It is **selection-conditional and seeded-only**: it does not appear on non-seeded rows, and it does not appear on unselected seeded rows. P7 owns this. |
| D4 | What treatment does the active-tab URL take inside the input row at the open frame? | **Bare URL text, fully selected (text selection range), no chip outline.** The URL prefills the textfield and is shown with the standard selected-text background; no chip border, no tonal pill behind the URL. P10 is therefore **N/A** under this R0 record and must be marked N/A in this document when P10 is signed off. |

### What this changes for the plan

- P7 (selected-row favicon tile): implement the **white tile/chip**, selection-conditional, seeded-only.
- P8 (trailing accessory): **hide entirely in kCurrentTab open state**. No glyph, no pill, no chip, no rounded-rect background; the accessory must not paint or take focus on this surface.
- P9 (dividers): implement the **no dividers** branch.
- P10 (active-tab URL): record **N/A**. The reference is bare URL with text selection. Ship no code under P10.
- P11 (color tokens): runs only if a real tone delta survives P7-P9 against the primary reference.

### Capture conditions (binding)

Every closeout capture compared against the primary reference must record, in its sidecar metadata:

- Color scheme: **dark** (the primary reference is a dark-mode capture).
- DPR: **2x** (Retina), unless the harness records a documented exception.
- History fixture: deterministic local fixture per the harness contract; no live network, no real profile.
- Wait after Cmd+L: the single named constant in `capture_command_palette.py`.

A capture taken at a different color scheme or DPR is not a valid comparison artifact for this closeout.

---

## P7+ Closeout Checklist

A small, **capture-observable** checklist. Each item is a yes/no judgment a reviewer can make from a single side-by-side image of the open Cmd+L (kCurrentTab) first frame against `palette-tab-active.png`. Items intentionally do not restate already-landed P1-P6 history.

Use this checklist as the approve/reject bar at the end of P7, P8, P9, and the final closeout sweep. If any box reads "no" against a current capture, that delta is open and the owning phase is not done.

### Selected seeded row (P7)

- [ ] The selected (row 0) favicon sits inside a **white rounded-square tile**.
- [ ] The tile fits the favicon with even visible padding on all four sides (no glyph touching the tile edge).
- [ ] The tile's corner radius is small and consistent (single radius, no compound rounding).
- [ ] No tile appears on **unselected** seeded rows.
- [ ] No tile appears on **non-seeded** (typed-query) rows.
- [ ] The tile is the only added container on the row; the row background pill (selection highlight) is unchanged from P3.

### Trailing input accessory (P8)

- [ ] **No trailing accessory is visible** on the input row in the kCurrentTab open frame.
- [ ] **No glyph, no pill, no rounded-rect background, no tonal chip** is painted at the trailing edge.
- [ ] The input row's textfield baseline and trailing inset match the reference; nothing shifted as a side effect of hiding the accessory.
- [ ] No focusable accessory child remains on the input row; Tab / Shift-Tab traversal does not stop on a hidden accessory.

### Row-to-row separation (P9)

- [ ] **No hairline divider** is visible between any two seeded rows.
- [ ] **No divider** is visible between the input row and the first result row.
- [ ] Row-to-row separation is achieved by spacing alone, matching the reference's vertical rhythm.

### Active-tab URL in the input row (P10, recorded N/A)

- [ ] The input field shows the active tab's URL as **bare text** (no chip outline, no tonal pill behind the URL).
- [ ] The URL is rendered with the standard **text-selection range** highlight (selection background only, applied to the text run).
- [ ] No persistent selection-chip remains after the first keystroke.

### Surface tone (P11, only if invoked)

- [ ] Selected-row pill fill matches the reference at dark-mode 2x.
- [ ] Subtitle text color matches the reference at dark-mode 2x.
- [ ] Favicon tile fill (P7) is white at dark-mode 2x and reads as the reference does.
- [ ] No adjacent surface (typed-query rows, non-seeded list, kNewTab) shifted tone as a side effect.

### Final closeout sweep

- [ ] Side-by-side capture taken at dark-mode, 2x, deterministic history fixture.
- [ ] Capture sidecar records color scheme, DPR, and wait constant.
- [ ] All P7, P8, P9 boxes above read **yes**.
- [ ] P10 is recorded **N/A** in this document with the reason "reference is bare URL with text selection."
- [ ] P11 is recorded **N/A** unless a tone delta survived P7-P9; if invoked, P11 boxes read **yes**.
- [ ] `git diff maho_command_model.cc maho_command_model.h` is empty.
- [ ] No file outside each phase's listed owners was modified.
