# Archive/Library Full Active Roadmap

**Created:** 2026-05-10  
**Status:** Active remaining-work roadmap  
**Source baseline:** `arc-library-strict-execution-plan.md`  
**Context only, not executable checklist:** `arc-library-parity-parallel-plan.md`

---

## 0. Roadmap purpose

This document rewrites the Archive/Library work around the verified current baseline and only the remaining work.

The older parity plan is **not** an executable checklist anymore. It is stale context only. Any future implementation must follow the item boundaries in this roadmap, not the old full-scope sequencing.

This roadmap exists to prevent scope bleed, shortcutting, and rework. It is a binding execution contract, not a wishlist.

---

## 1. Verified completed baseline that must not be reimplemented

The following work is treated as already complete and must not be rebuilt, reopened, or re-litigated inside a remaining item unless the roadmap is explicitly revised first.

### 1.1 Shell routing and Library entry flow

Already verified:
- shell routing exists and owns Library mode switching
- `MahoSidebarBodyMode` already includes `kLibrary`
- `SetBodyMode(...)` already owns tabs, library, and create-space switching
- `ShowLibraryPane(...)` already owns category routing
- shell-level focus, content hosting, and visibility behavior already exist

Must not be reimplemented:
- Library shell creation from scratch
- body-mode redesign
- shell navigation semantics rewrite
- focus and visibility behavior rework unless a remaining item explicitly says so

### 1.2 Archive pane baseline

Already verified:
- Archive pane exists
- archive search and filter UI exists
- restore and delete actions exist
- empty state exists
- archive row grouping and interaction behavior exists

Must not be reimplemented:
- archive-pane feature parity work
- search, filter, restore, delete, or empty-state rebuild

### 1.3 Library rail baseline

Already verified:
- the multi-category rail exists
- category definitions exist
- category selection callbacks exist
- back-button behavior exists
- rail item presentation exists
- prior Oracle PASS exists for rail polish context

Must not be reimplemented:
- rail taxonomy reconstruction
- rail ordering redesign
- category plumbing rewrite
- rail polish that is already captured by the current runtime baseline

### 1.4 Downloads, Media, and action-pane integrations

Already verified:
- Downloads integration exists in the current Library surface
- Media integration exists in the current Library surface
- action-pane integrations already exist where relevant
- these are part of the current active surface contract

Must not be reimplemented:
- new destination scaffolding for Downloads or Media
- duplicate action-pane ownership
- fallback placeholder implementations that pretend the integration is new

### 1.5 Runtime evidence baseline

Already verified:
- runtime screenshots exist for the current Library and Archive baseline
- clean-profile launch sanity evidence exists
- the verified runtime baseline is the reference point for future changes

Must not be reimplemented:
- baseline capture work unless a remaining item explicitly requires fresh evidence after a code change

---

## 2. Non-negotiable execution rules

These rules apply to every remaining item.

1. One active item at a time. No parallel implementation unless this roadmap explicitly says a batch is safe.
2. No shortcutting. If the item cannot be completed within its allowed files, stop and replan.
3. No scope bleed. Do not pull in neighboring cleanup, unrelated compile fixes, or convenience edits.
4. No out-of-item file edits. Every changed file must belong to the active item.
5. No completion without evidence. If the evidence listed for an item is missing, the item is not done.
6. No pretending the old parity plan is a live checklist. It is only context for understanding the destination.

---

## 3. Remaining work items only

The items below are the only open workstreams that belong in this roadmap. If a future issue does not fit one of them, the correct response is to stop and re-decompose the roadmap before editing code.

---

### Item ARC-LIBRARY-001

**Title:** Library rail breathing room polish

**Purpose:** Reduce the cramped feel of the already-existing multi-category rail without reopening shell layout or category structure.

**Exact problem statement:** The Library rail is functionally in place, but the current presentation still feels too tight. The remaining issue is local presentation comfort, not a missing feature, not a new destination, and not a shell refactor.

**Allowed files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`

**Explicitly forbidden files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`
- downloads, media, action-pane, footer, build, test, docs, or harness files
- any unrelated sidebar feature file

**Required evidence:**
- `lsp_diagnostics` on the modified rail files with zero errors
- a runtime screenshot or equivalent artifact showing the updated rail presentation
- a short confirmation that the Library structure still matches the current verified baseline
- Oracle PASS if the change is visually non-trivial or if the implementation introduces any presentation nuance that could drift

**Stop-and-replan triggers:**
- the change needs shell, container, or width-token edits
- the visible issue turns out to be layout geometry instead of rail presentation
- the item starts affecting category routing, focus behavior, or back behavior
- any unrelated compile error appears and would require touching out-of-scope files
- the polish cannot be achieved cleanly inside the allowed rail files

**Execution sequence:**
1. Reconfirm the current baseline and keep the scope limited to rail presentation.
2. Edit only the allowed rail files.
3. Make the smallest change that improves perceived breathing room.
4. Do not modify category ownership, rail ordering, or shell geometry.
5. Run diagnostics on the modified files.
6. Capture runtime evidence of the updated rail.
7. Verify the verified baseline still holds.
8. Stop immediately if any out-of-scope dependency appears.

**Acceptance criteria:**
- the rail is visibly less cramped in the runtime artifact
- the current Library structure remains intact
- the first item still reads `Archive`
- only the allowed rail files were changed
- diagnostics are clean
- no shell, container, width-token, archive-pane, or destination-routing change was introduced

---

### Item ARC-LIBRARY-002

**Title:** Validation and regression hardening for the current Library surface

**Purpose:** Lock the already-existing Library surface so the verified baseline stays stable while remaining work is closed out.

**Exact problem statement:** The current Library surface exists and is already wider than the narrow active item. What remains is hardening the current state with explicit validation coverage and regression checks so later work does not silently break shell routing, rail behavior, or the already-integrated destinations.

**Allowed files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_navigation_interactive_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`
- `maho-chromium/browser/ui/views/sidebar/BUILD.gn`
- only the minimal supporting test files needed to express the current verified baseline

**Explicitly forbidden files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`
- downloads, media, action-pane, or footer implementation files
- any broad refactor or new feature surface

**Required evidence:**
- `lsp_diagnostics` on all modified test and build files with zero errors
- the relevant interactive or contract test target passes
- evidence that the current verified baseline is still covered by the test change
- if a build is needed for the test update, the build must succeed

**Stop-and-replan triggers:**
- the validation work starts needing production code edits outside the allowed test files
- the test intent changes from verification to feature expansion
- the item starts adding new Library destinations or behavior instead of covering the existing baseline
- a test failure would require out-of-scope production code changes
- the minimal coverage cannot represent the verified baseline honestly

**Execution sequence:**
1. Restate the exact baseline behavior that is being protected.
2. Edit only the allowed test or build files.
3. Add coverage that matches the current verified state, not the old parity wish list.
4. Run diagnostics on modified files.
5. Run the relevant test target or targets.
6. Confirm the tests describe the existing surface and not a future surface.
7. Stop and replan if the needed evidence requires production code changes.

**Acceptance criteria:**
- the current Library baseline is covered by explicit regression or contract tests
- the modified test files are diagnostically clean
- the relevant test targets pass
- no production code outside the allowed files was changed
- the item does not smuggle in feature work under the label of validation

---

## 4. Roadmap closure rule

This roadmap is complete only when every remaining item above has either been finished with its required evidence or intentionally replaced by a newly written roadmap item after a stop-and-replan event.

The older parity plan stays context only. New implementation must not use it as a live checklist.

If a task does not fit one of the remaining items, stop and re-decompose before changing code.
