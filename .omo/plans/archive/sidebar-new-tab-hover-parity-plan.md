# Sidebar New-Tab Hover Parity Plan

## Goal

Reuse the existing `+ New Tab` hover highlight treatment for the main sidebar list rows so the hover language is consistent across the tab tree.

## Chosen default target

Implement the hover effect on:

1. `SidebarTabRowView`
2. `SidebarFolderRowView`

This is the safest default because both live in the same list as `+ New Tab`, already use the same sidebar hover token family, and must preserve pinned/normal symmetry.

## Explicit non-goals

- no changes to favorites grid hover behavior
- no changes to library rail buttons
- no changes to archive view rows
- no redesign of text weight, iconography, or row height
- no new animation/shimmer system beyond reusing the existing `+ New Tab` hover treatment

## Current code findings

### Source of truth: `+ New Tab`

File:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

Relevant implementation:

- `SidebarInlineRow` (`+ New Tab` owner)
- `AddInlineRow(...)`
- `new_tab_button_ = AddInlineRow(..., /*active=*/false, /*subtle=*/true)`

Actual hover treatment:

- hover is not a separate shimmer animation system
- it is a rounded background swap using `kMahoColorSidebarHover`
- implementation lives in `SidebarInlineRow::UpdateAppearance()`
- on hover/pressed, it calls:
  - `SetBackground(CreateRoundedRectBackground(kMahoColorSidebarHover, kRowCornerRadiusDp))`
- at rest, background is `nullptr`

### Candidate target rows

Same file:

- `SidebarTabRowView::UpdateAppearance()`
- `SidebarFolderRowView::UpdateAppearance()`

Both already use:

- `kMahoColorSidebarHover`
- `kRowCornerRadiusDp`
- the same sidebar list render context

So the implementation target is not cross-file styling reuse; it is hover-treatment parity inside one file.

## Acceptance criteria

### Visual behavior

1. Idle, non-active `SidebarTabRowView` hover treatment matches the `+ New Tab` hover highlight treatment.
2. Idle, non-drop-target `SidebarFolderRowView` hover treatment matches the `+ New Tab` hover highlight treatment.
3. Pinned and normal rows remain visually identical except for state-driven differences that already exist outside this hover treatment.
4. Active tab rows still preserve active-state precedence.
5. Drag-over drop-target visuals still preserve drop-target precedence.

### Architectural behavior

6. The hover treatment is defined from one shared helper/spec instead of being independently hand-written in three places.
7. The change does not create new pinned-only or normal-only hover branching.
8. The change does not spread into favorites/library/archive contexts.

### Verification

9. Unit tests lock that tab rows and folder rows still preserve structural parity across pinned/normal.
10. New or updated tests verify hover contract at the row level, either through direct background/border assertions or through a minimal shared helper contract.

## Implementation strategy

### Phase 1 — Freeze the hover spec in one place

Primary file:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

Actions:

1. Extract the `+ New Tab` hover highlight treatment into a tiny local shared helper in the same file.
2. The helper should represent only the hover chrome itself:
   - rounded hover background
   - no border at idle hover
3. Do not extract `subtle` text styling or inline-row-specific label logic.

Preferred shape:

- helper returning the background/border pair or directly applying the passive hover chrome
- keep scope local to this file because all three target classes already live here

Exit criteria:

- `SidebarInlineRow` uses the helper
- no behavior change to `+ New Tab`

### Phase 2 — Apply the same hover chrome to tab rows

Primary file:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

Target function:

- `SidebarTabRowView::UpdateAppearance()`

Actions:

1. Keep current precedence order:
   - drag-over drop target
   - active row
   - hovered idle row
   - rest
2. In the hovered idle row branch, use the same shared helper/spec as `SidebarInlineRow`.
3. Do not change:
   - close button visibility behavior
   - title button colors
   - active row selected border behavior

Exit criteria:

- non-active tab row hover uses the exact same hover chrome source as `+ New Tab`

### Phase 3 — Apply the same hover chrome to folder rows

Primary file:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

Target function:

- `SidebarFolderRowView::UpdateAppearance()`

Actions:

1. Keep current precedence order:
   - drag-over drop target
   - hovered idle folder row
   - rest
2. In the hovered idle branch, use the same shared helper/spec as `SidebarInlineRow`.
3. Do not change:
   - chevron/icon update logic
   - drop-target border/highlight behavior
   - rename editing visibility logic

Exit criteria:

- idle folder hover matches the same hover chrome source

### Phase 4 — Test lock

Primary files:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`
- optionally `maho_sidebar_tab_list_view.h` only if a tiny testing accessor is required

Actions:

1. Extend row-level unit tests so a pinned and normal tab row still have identical structural slots after the hover change.
2. Extend folder-row parity tests the same way.
3. Add the smallest possible hover-contract test for row views.

Preferred testing strategy:

- avoid screenshot/golden complexity
- prefer direct assertion on view state after synthetic hover if feasible
- if direct hover/background inspection is awkward, add a tiny testing seam only for the hover helper contract, not a broad styling API

Exit criteria:

- hover change is locked by unit tests without weakening pinned/normal parity guarantees

### Phase 5 — Verification

Verification steps:

1. run diagnostics on changed sidebar files
2. run the relevant sidebar tests if available in current environment
3. verify no behavior drift in:
   - active tab styling
   - drop-target styling
   - pinned/normal row parity

## Risks

1. `SidebarTabRowView` and `SidebarFolderRowView` already visually resemble `+ New Tab` hover, so the real implementation delta may be small; avoid over-refactoring for a minimal result.
2. If the user's perceived "sparkle" is actually from a nuance outside the background token swap, we may need a follow-up visual refinement after this parity pass.
3. Hover tests in Views can be awkward; keep the test seam minimal and local.
4. Do not accidentally propagate the effect into unrelated sidebar surfaces that intentionally use other hover tokens.

## Suggested implementation order

1. extract shared hover helper from `SidebarInlineRow`
2. wire `SidebarTabRowView` hovered idle branch to the helper
3. wire `SidebarFolderRowView` hovered idle branch to the helper
4. add/update row parity + hover contract tests
5. run verification

## Done definition

The work is done only when all of the following are true:

1. `+ New Tab`, idle tab rows, and idle folder rows all use the same hover chrome source
2. active and drop-target branches still override hover correctly
3. pinned and normal rows remain symmetric
4. tests cover the intended parity contract
5. no unrelated sidebar surfaces were changed
