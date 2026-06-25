# Create Space Screenshot UI Parity — Parallel Execution Plan

**Created:** 2026-05-08  
**Branch:** main  
**Status:** Draft — pending Momus review  
**Primary Scope:** `maho-chromium/browser/resources/maho_space_create/*`  
**Primary Goal:** Make the Chromium create-space UI visually indistinguishable from the user-provided screenshot before any further semantic or behavioral expansion.

---

## Objective

Rebuild the current Chromium create-space surface so it matches the screenshot composition, proportions, element ordering, and visual hierarchy as closely as possible. The first phase is **visual parity only**. Existing code semantics, current Chromium control names, and prior Swift-driven interpretation must not override what is visibly present in the screenshot.

---

## Source of Truth and Anti-Drift Rules

### Primary Reference Order

1. **User-provided screenshot in the current conversation** — absolute source of truth.
2. Repo-local reference assets only as tie-breakers when they do **not** contradict the screenshot:
   - `maho/reference_screenshots/create-space.jpeg`
   - `maho/reference_screenshots/create-theme.png`

### Hard Rules

1. If an element is **not visible** in the screenshot, it does **not belong** in Phase 1 parity.
2. Do **not** infer UI meaning from current code names like `solid`, `zen`, or `gradient` while doing visual parity.
3. Do **not** keep text labels in the top icon chips.
4. Do **not** keep extra editor chrome just because it already exists in the code.
5. Visual order, spacing, and silhouette take priority over behavior in the parity phase.
6. Sidebar/theme-store/edit-theme expansion work is out of scope for this plan.

---

## In-Scope Surfaces

### Primary Files

- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`

### Secondary / Validation Files

- `maho-chromium/demo-space-create.html`
- Optional validation artifacts under `.sisyphus/artifacts/create-space-ui-parity/`

### Explicitly Out of Scope

- `maho-chromium/browser/resources/maho_sidebar/*`
- Theme semantics beyond what is required to keep the screen rendering
- Sidebar theme store / theme browser / manage-spaces expansion
- Accessibility polishing beyond preserving existing required hooks
- New Mojo or backend contracts

---

## Screenshot-Derived Visible UI Inventory

### Left Column

1. Stacked illustration above title
2. `Create a Space` title
3. Subtitle block with multi-line marketing copy
4. `Space name...` input card
5. `Profile` selector card
6. `Choose a Theme` row card
7. Connector/notch between left column and right panel

### Right Panel

1. Outer rounded theme-creator frame
2. Three **icon-only** chips at the top
3. Large dotted canvas / preview field
4. Center instructional text inside the canvas
5. Minus and plus controls below the canvas
6. Swatch row with left/right chevrons
7. Wave graphic near the bottom-left
8. Circular dial near the bottom-right

---

## UI That Must Be Hidden or Removed During Phase 1

These elements may exist in the current code but are not visible in the screenshot and therefore must be removed from the visible parity surface during the first implementation pass:

- Text-labeled mode tabs such as `Solid`, `Zen`, `Gradient`
- Hex input row
- Gradient toggle button
- Harmony selector row
- Opacity slider
- Summary pill / metadata pill
- Verbose editor header copy inside the right panel
- Two-card control grids or boxed control sections not visible in the screenshot
- Any sidebar/editor affordance that makes the surface look like a separate settings panel instead of the integrated create-space layout

---

## Parallelization Strategy

The plan is intentionally divided into a **short serial seam-creation phase** followed by the **largest safe parallel batch**. The purpose of the serial phase is to carve clean ownership boundaries so several agents can work at once without repeatedly colliding in the same DOM/CSS region.

### Parallelism Ceiling

- Maximum recommended concurrent implementation tracks after seam creation: **6**
- Maximum recommended concurrent polish/verification tracks after first merge: **4**

---

## Serial Gates

## Gate 0 — Lock the Reference and Acceptance Rubric

**Purpose:** Make sure all later implementation work targets the same visual truth.

**Deliverables:**
- A written inventory of visible elements from the screenshot
- A short “must-hide” list for controls not visible in the screenshot
- A baseline Chromium screenshot capture for current-state comparison

**Acceptance Criteria:**
- No implementation track starts from a different interpretation of the screenshot
- The team agrees that screenshot appearance outranks current code semantics

---

## Gate 1 — Create Parallel Seams in the Current Create-Space Surface

**Purpose:** Reduce merge contention by carving the create-space UI into explicit ownership zones before multiple tracks start editing.

**Target Files:**
- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`

**Required Changes:**
- Split the DOM-building logic into clearly owned sections or helper builders for:
  - left column
  - right-panel top cluster
  - right-panel mid controls
  - right-panel bottom controls
- Introduce clearly marked CSS ownership blocks for those same sections
- Remove or isolate legacy right-panel blocks that are known to be out of screenshot scope

**Acceptance Criteria:**
- Later tracks can work within named DOM/CSS sections rather than fighting over the whole file
- Legacy controls outside the screenshot scope are no longer structurally intertwined with the visible parity layout

---

## Parallel Batch 1 — Major Surface Reconstruction

These tracks begin only after **Gate 1** completes.

### Track A — Right Panel Macro Layout and Frame Ownership

**Goal:** Match the large-scale structure and proportions of the entire right panel.

**Owned Surface:**
- Right panel outer frame
- Internal vertical stacking order
- Overall padding and dominant canvas height allocation

**Primary Files:**
- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`

**Must Deliver:**
- Right panel element order exactly matching the screenshot
- Canvas as the dominant visual block
- Bottom controls clearly separated beneath the canvas
- Removal of extra visible headers/sections not present in the screenshot

**Must Not Do:**
- Do not style chip internals, swatch details, wave geometry, or dial geometry here beyond what is necessary for layout placeholders

**Acceptance Criteria:**
- At low zoom, the right panel silhouette already matches the screenshot composition

---

### Track B — Top Cluster: Icon Chips and Canvas

**Goal:** Match the right-panel top section precisely.

**Owned Surface:**
- Three icon-only chips
- Canvas frame
- Dotted field density and corner radius
- Center instruction text placement

**Primary Files:**
- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`

**Must Deliver:**
- Icon-only chips with no visible text
- Correct chip geometry, spacing, and selected-state treatment
- Correct canvas border, radius, and dot-field feel
- Center text alignment and placement consistent with screenshot

**Must Not Do:**
- Do not reintroduce semantic labels or current-code mode names

**Acceptance Criteria:**
- The top cluster looks like a screenshot match even when isolated from the rest of the panel

---

### Track C — Mid Controls: Minus/Plus, Chevrons, and Swatch Rail

**Goal:** Match the screenshot’s mid-lower control strip.

**Owned Surface:**
- Minus/plus controls
- Left/right chevrons
- Swatch count, size, spacing, and selected state

**Primary Files:**
- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`

**Must Deliver:**
- Standalone minus/plus treatment matching the screenshot
- Correct chevron positioning relative to swatches
- Correct swatch circle sizing and spacing
- Correct selected-swatch outline behavior

**Must Not Do:**
- Do not keep a visible slider track or boxed control card if it is not in the screenshot

**Acceptance Criteria:**
- The swatch rail reads as the same control strip seen in the screenshot, not a re-skinned existing palette card

---

### Track D — Bottom Controls: Wave Graphic and Circular Dial

**Goal:** Recreate the screenshot’s lower texture-control area.

**Owned Surface:**
- Wave graphic
- Circular dial silhouette
- Relative alignment between both elements

**Primary Files:**
- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`

**Must Deliver:**
- A wave shape that visually matches the screenshot silhouette
- A circular dial that visually matches the screenshot silhouette
- Correct bottom padding and spacing relative to the swatch rail

**Must Not Do:**
- Do not over-design the wave or dial as a richer control than what is visible
- Do not let these controls pull spacing away from the canvas or swatch rail

**Acceptance Criteria:**
- The bottom row looks intentionally designed to match the screenshot, not like placeholders or generic controls

---

### Track E — Left Column Parity

**Goal:** Match the screenshot’s create-space form column.

**Owned Surface:**
- Hero illustration positioning
- Title and subtitle block
- Form card sizing and spacing
- Profile row alignment
- Choose-theme row alignment
- Connector/notch relationship with right panel

**Primary Files:**
- `maho-chromium/browser/resources/maho_space_create/app.ts`
- `maho-chromium/browser/resources/maho_space_create/space_create.css`

**Must Deliver:**
- Left column visual hierarchy matching the screenshot
- Correct spacing between title, subtitle, and form cards
- Correct row heights and rounded-card treatment
- Correct notch/connector treatment where left and right surfaces meet

**Must Not Do:**
- Do not expand scope into behavior or validation beyond preserving the currently needed create-flow state

**Acceptance Criteria:**
- The left column can be compared side-by-side with the screenshot and does not visually lag behind the right panel

---

### Track F — Validation Harness and Overlay Workflow

**Goal:** Ensure every parity pass is measured, not guessed.

**Owned Surface:**
- Screenshot capture workflow
- Overlay or diff workflow
- Artifact organization for before/after review

**Primary Files / Locations:**
- `maho-chromium/demo-space-create.html` if needed for stable capture setup
- `.sisyphus/artifacts/create-space-ui-parity/` for artifacts

**Must Deliver:**
- Baseline capture of current Chromium screen
- Updated captures after Batch 1 merge
- Overlay or diff workflow that makes spacing/proportion mismatches obvious
- Repeatable naming convention for artifacts

**Must Not Do:**
- Do not block implementation on building a perfect automation stack; lightweight repeatable captures are sufficient

**Acceptance Criteria:**
- Every merge gate can be reviewed with concrete visual evidence rather than subjective judgment

---

## Merge Gate 2 — Integrate Batch 1 and Resolve Cross-Section Drift

**Purpose:** Bring together the major parallel tracks and fix any conflicts in shared spacing, tokens, or DOM assumptions.

**Required Checks:**
- The right panel still reads as one coherent surface after tracks merge
- The left/right relationship still matches the screenshot
- No hidden legacy controls reappear due to merge drift
- The validation harness can capture the merged state cleanly

**Acceptance Criteria:**
- Batch 1 output is structurally stable enough for micro-adjustment passes

---

## Parallel Batch 2 — Micro-Parity and Regression Control

These tracks begin only after **Merge Gate 2** completes.

### Track G — Spacing and Proportion Polish

**Goal:** Close the last visible layout gaps.

**Owned Surface:**
- Gap sizes between major sections
- Internal panel padding
- Left/right width ratio
- Canvas height ratio relative to the whole right panel

**Acceptance Criteria:**
- Major layout values are within a visually negligible delta from the screenshot

---

### Track H — Color, Border, and Opacity Polish

**Goal:** Match the screenshot’s visual tone.

**Owned Surface:**
- Border opacity
- Fill colors
- Inactive/active chip states
- Swatch outlines
- Dial and wave opacity treatment
- Canvas border and frame contrast

**Acceptance Criteria:**
- The screen no longer looks “structurally similar but visually off”

---

### Track I — Legacy-State Regression Cleanup

**Goal:** Make sure hidden or removed controls do not leak back into view.

**Owned Surface:**
- Old mode-tab states
- Hex row visibility
- Gradient-toggle visibility
- Harmony/opacity rows
- Any stale CSS or DOM branch that can become visible during state changes

**Acceptance Criteria:**
- No screenshot-incompatible control appears in the parity surface under the default create-space state

---

### Track J — Viewport and Capture Consistency

**Goal:** Confirm parity is not accidental at a single size.

**Owned Surface:**
- Nominal viewport review
- Slightly narrower viewport review
- Stable capture framing for final comparison

**Acceptance Criteria:**
- The screenshot-parity layout remains intact at the intended primary size and does not collapse immediately at the next smaller checkpoint

---

## Merge Gate 3 — Final Visual Lock

**Purpose:** Confirm that the create-space screen is ready for sign-off before any semantics are revisited.

**Required Checks:**
- Overlay review against the screenshot
- Element-by-element parity checklist
- Final pass for accidental text labels or non-screenshot controls
- Final pass for left/right balance and notch treatment

**Acceptance Criteria:**
- The remaining differences are minor enough to list explicitly rather than broadly visible

---

## Recommended Agent Allocation for Later Execution

### Visual Workstreams

Use **`visual-engineering`** for:
- Track A
- Track B
- Track C
- Track D
- Track E
- Track G
- Track H

### Lower-Risk Structural / Validation Workstreams

Use **`small-impl`** or **`unspecified-low`** for:
- Track F
- Track I
- Track J

### Review Work

- Use **Momus** to critique the saved plan before implementation starts
- Use **Oracle** only after major UI implementation batches, not during screenshot-only planning

---

## Dependency Graph

```text
Gate 0: lock screenshot + rubric
    ↓
Gate 1: create app.ts/css ownership seams
    ↓
Parallel Batch 1
    ├── Track A: right panel macro layout
    ├── Track B: top cluster (chips + canvas)
    ├── Track C: mid controls (minus/plus + chevrons + swatches)
    ├── Track D: bottom controls (wave + dial)
    ├── Track E: left column parity
    └── Track F: validation harness
    ↓
Merge Gate 2
    ↓
Parallel Batch 2
    ├── Track G: spacing/proportion polish
    ├── Track H: color/border/opacity polish
    ├── Track I: legacy-state regression cleanup
    └── Track J: viewport/capture consistency
    ↓
Merge Gate 3: final visual lock
```

---

## Critical Path

The true critical path is intentionally short:

1. Lock the screenshot interpretation.
2. Create explicit seams in `app.ts` and `space_create.css`.
3. Run the major visual reconstruction tracks in parallel.
4. Merge once for structural stability.
5. Run micro-parity and regression tracks in parallel.
6. Lock the final visual state before touching semantics.

---

## Validation Checklist

### Structural Checklist

- [ ] Left and right columns match screenshot hierarchy
- [ ] Right panel element order matches screenshot exactly
- [ ] Top chips are icon-only
- [ ] Canvas is the dominant right-panel block
- [ ] Minus/plus are standalone controls, not part of an unrelated visible card
- [ ] Swatch row count and spacing match the screenshot
- [ ] Wave and dial both exist and match screenshot silhouettes
- [ ] No hex field or gradient/harmony/opacity controls are visible in the parity state

### Polish Checklist

- [ ] Left panel spacing matches screenshot
- [ ] Right panel padding matches screenshot
- [ ] Border radii feel identical
- [ ] Border opacity and fill tones match
- [ ] Selected chip and selected swatch states match screenshot
- [ ] Connector/notch relationship between left and right surfaces matches screenshot

### Evidence Checklist

- [ ] Baseline screenshot captured
- [ ] Batch 1 merged screenshot captured
- [ ] Final merged screenshot captured
- [ ] Overlay/diff artifacts saved
- [ ] Any remaining mismatch listed explicitly in pixels or visual terms

---

## Definition of Done

This plan is complete when all of the following are true:

1. The Chromium create-space screen visually matches the screenshot closely enough that differences are hard to spot without overlay comparison.
2. The parity surface contains only the controls and visual elements that are actually visible in the screenshot.
3. The work was executed through explicit seams and merge gates rather than multiple agents fighting over the whole file.
4. Validation artifacts exist for baseline, post-Batch-1, and final merged states.
5. The UI is considered visually locked before any renewed discussion of semantics, modes, or expanded editor behavior.

---

## Explicit Deferrals

These are intentionally deferred until after screenshot parity is accepted:

- Determining the real semantic meaning of the three top chips
- Reintroducing any hidden current-code controls unless the screenshot or later reference proves they belong
- Sidebar parity work
- Theme-store related flows
- Deeper theme computation refactors
- Broader accessibility or product copy work
<!-- OMO_INTERNAL_INITIATOR -->