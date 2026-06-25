# Archive/Library Strict Execution Plan

**Created:** 2026-05-10  
**Status:** Active strict execution plan  
**Supersedes for execution discipline:** `arc-library-parity-parallel-plan.md`  
**Reference only:** the older plan remains useful for full-scope parity context, but this file is the binding execution contract.

---

## 0. Why this plan exists

The previous work drifted because the implementation plan was treated as guidance rather than as a binding constraint.

This plan fixes that by enforcing:
- **plan item ↔ code diff mapping**
- **no out-of-plan edits**
- **mandatory stop-and-replan on blockers**
- **stage-by-stage runtime evidence**
- **no workaround-first behavior**

This is not a brainstorming document. It is an execution contract.

---

## 1. Current verified code state

The codebase is no longer at the original "archive-only scaffold" stage. The following are already true and must be treated as the current baseline, not re-implemented blindly:

### 1.1 Current Library shell ownership
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

Current responsibilities:
- `MahoSidebarBodyMode` already includes `kLibrary`
- `SetBodyMode(...)` owns shell switching between tabs / library / create-space
- `ShowLibraryPane(...)` owns category routing
- shell-level focus / content hosting / visibility behavior lives here

### 1.2 Current Library rail ownership
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`

Current responsibilities:
- rail category definitions
- category selection callbacks
- back-button behavior
- rail item presentation

### 1.3 Current Archive pane ownership
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`

Current responsibilities:
- archive search/filter UI
- restore/delete actions
- empty state
- archive row grouping and interaction behavior

### 1.4 Current layout/token drift hotspot
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`

These are the highest-risk files for shortcutting through width hacks or shell-level spacing workarounds.

### 1.5 Current adjacent category files
- `maho_sidebar_downloads_view.h/.cc`
- `maho_sidebar_media_view.h/.cc`
- `maho_sidebar_library_action_pane_view.h/.cc`

These are already part of the current Library surface and must be considered part of the active surface contract.

---

## 2. Non-negotiable execution rules

These rules override convenience.

### Rule A — No edit without plan-item ownership
Every code edit must map to exactly one active plan item.

Required notation during execution:
- **Plan Item ID**
- **Files allowed**
- **Reason those files are in scope**

If a file change does not map cleanly to an active plan item, it is **forbidden** until the plan is updated.

### Rule B — No blocker workaround before replan
If a blocker appears, the sequence must be:
1. classify blocker
2. determine whether blocker is in-scope or out-of-scope
3. if out-of-scope, stop implementation
4. update plan explicitly
5. only then implement

Forbidden behavior:
- quick width-token workaround before shell diagnosis
- opportunistic unrelated compile cleanup folded into UI work without explicit scope change
- “temporary” hack before validating plan alignment

### Rule C — No evidence, no completion
A plan item is incomplete without its listed evidence.

Allowed evidence types:
- `lsp_diagnostics` result
- successful build/test command
- runtime screenshot artifact
- AX/runtime artifact
- Oracle PASS where required

### Rule D — Plan-update triggers are mandatory stops
The following conditions **force** a plan update before more code changes:
- any new width constant outside `maho_sidebar_layout_tokens.h`
- touching a file not listed under the active item
- unrelated build failure requiring source changes outside the active item
- changing accessibility labels, visible strings, or shell navigation semantics
- changing body-mode behavior or container visibility beyond the planned item
- fixing only one platform/runtime path while leaving the canonical validation path unverified

### Rule E — One active implementation item at a time
Even if multiple later items exist, implementation proceeds serially unless the plan explicitly marks a batch as safely parallel.

This prevents hidden overlap and accidental scope bleed.

---

## 3. Validation contract

Future Archive/Library work must validate against these concrete evidence paths.

### 3.1 Code validation checkpoints
- `maho_sidebar_view.cc` — body mode / active pane routing / shell visibility
- `maho_sidebar_container_view.cc` — panel width, reveal, persistence behavior
- `maho_sidebar_library_rail_view.cc` — category mapping, label/icon behavior, back button
- `maho_sidebar_archive_view.cc` — archive pane semantics and UI
- `maho_sidebar_layout_tokens.h` — centralized token-only width/spacing decisions

### 3.2 Automated validation checkpoints
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_navigation_interactive_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`
- `maho-chromium/browser/ui/views/sidebar/BUILD.gn`

These define the currently built/tested sidebar validation boundary.

### 3.3 Runtime evidence checkpoints
Canonical runtime evidence paths for this work:
- clean-profile runtime screenshot captures under `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/...`
- latest successful example from current state:
  - `maho-archive-verify-20260510f/library.png`
  - `maho-archive-verify-20260510f/archive.png`
- clean-profile launch sanity log:
  - `maho-launch-sanity-20260510/launch.log`

### 3.4 Manual smoke contract
For any future Archive/Library change, required smoke steps are:
1. launch clean-profile app
2. open Library
3. switch to Archived Tabs
4. verify visible shell structure
5. verify one primary Archive interaction relevant to the changed area
6. exit / re-enter if the change affects mode transitions

---

## 4. Drift hotspots to freeze

These are the exact places where shortcut behavior is most likely.

### 4.1 Width-token drift
Files:
- `maho_sidebar_layout_tokens.h`
- `maho_sidebar_container_view.cc`
- `maho_sidebar_view.cc`

Guardrail:
- No width/spacing workaround outside the token file unless the active plan item explicitly owns shell/container geometry.

### 4.2 Shell/body-mode drift
Files:
- `maho_sidebar_view.h/.cc`

Guardrail:
- No body-mode or content-host changes during a pane-only or rail-only task.

### 4.3 Rail-only cosmetic drift
Files:
- `maho_sidebar_library_rail_view.h/.cc`

Guardrail:
- No rail label/icon changes unless the active item includes runtime screenshot validation proving the visible result.

### 4.4 Unrelated compile-fix bleed
Any file outside the active item.

Guardrail:
- If the current work reveals an unrelated compile blocker, record it separately and update the plan before touching it.

---

## 5. Strict staged execution plan

This plan intentionally narrows execution to the Archive/Library surface discipline problem first.

---

## Gate S0 — Baseline freeze

**Purpose:** Establish the current working state as the baseline before any new implementation.

**Allowed files:** none  
**Allowed actions:** read-only verification only

**Must verify:**
- current shell ownership and rail ownership are exactly as mapped in Section 1
- current runtime evidence exists for the latest successful Library/Archive capture
- clean-profile launch sanity currently succeeds

**Acceptance evidence:**
- file map confirmed
- latest runtime artifact paths recorded
- launch sanity evidence recorded

**Stop condition:**
- if baseline cannot be reproduced, do not continue to implementation; create a recovery plan first

---

## Gate S1 — Scope declaration for the next change

**Purpose:** Prevent ambiguous implementation goals.

Before any new code change, define:
- one exact user-visible problem
- one exact owning file set
- one exact validation artifact set

**Template:**
- Problem:
- Plan Item ID:
- Allowed files:
- Disallowed files:
- Required runtime evidence:
- Required automated evidence:

**Acceptance evidence:**
- the item is specific enough that any extra file edit can be recognized as drift

---

## Gate S2 — Implement only within file boundary

**Purpose:** Enforce plan-as-constraint behavior.

For the active item:
- edit only allowed files
- if another file becomes necessary, stop and replan
- do not apply workaround-first fixes
- do not mix polish with shell changes unless the item explicitly owns both

**Acceptance evidence:**
- every changed file maps to the active item
- no incidental diff exists outside the item boundary

---

## Gate S3 — Validate the item before moving on

**Purpose:** Make each item self-proving.

For the active item, gather:
- diagnostics on changed files where meaningful
- build evidence if C++ changed
- runtime evidence if visible UI changed
- targeted test evidence if route/contract behavior changed

**Acceptance evidence:**
- item-specific evidence complete
- no “looks fine” completion without artifacts

---

## Gate S4 — Oracle review before closure

**Purpose:** prevent subtle drift or fragile cleanup from being silently accepted.

For non-trivial Archive/Library changes:
- Oracle reviews the actual updated code
- Oracle checks both code quality and requirement coverage
- PASS is required before the item is declared complete

**Acceptance evidence:**
- `<verdict>PASS</verdict>`

---

## 6. Allowed next implementation items

These are the only valid immediate next-step item types under this strict plan.

### Item P1 — Shell semantics change
**Only if** the user asks for shell behavior changes.

Allowed files:
- `maho_sidebar_view.h/.cc`
- `maho_sidebar_container_view.cc`
- `maho_sidebar_layout_tokens.h` only if the shell item explicitly owns token changes

Required evidence:
- build
- launch sanity
- Library open/back runtime capture
- relevant navigation tests

### Item P2 — Rail presentation or category interaction change
Allowed files:
- `maho_sidebar_library_rail_view.h/.cc`

Required evidence:
- build
- clean-profile Library screenshot
- Oracle PASS

### Item P3 — Archive pane behavior or layout change
Allowed files:
- `maho_sidebar_archive_view.h/.cc`
- `maho_sidebar_view.h/.cc` only if routing is directly required

Required evidence:
- build
- Archive screenshot
- relevant archive interaction smoke
- Oracle PASS

### Item P4 — Validation-only hardening
Allowed files:
- interactive/contract tests
- docs/artifact harness files

Required evidence:
- test pass or artifact generation

---

## 7. Explicit anti-shortcut rules

The following are forbidden unless they are the declared active item:
- “temporary” width increases to make a screenshot look better
- changing shell geometry to avoid fixing rail or pane layout correctly
- mixing compile-blocker cleanup into UI work silently
- claiming “reference parity” without artifact proof
- editing both shell and pane layers in one item unless the item explicitly owns both
- closing an item on visual intuition alone

---

## 8. Definition of done for this strict plan system

This strict plan is working only if future implementation follows this shape:
- one active item
- bounded files
- bounded validation
- stop-and-replan on drift
- Oracle PASS before closure

If those conditions are not followed, the process has failed even if the UI looks improved.

---

## 9. Recommended working mode going forward

For all future Archive/Library work, use this operating sequence:
1. pick one item from Section 6 or explicitly define a new one
2. restate allowed/disallowed files
3. implement only within that boundary
4. validate with the required evidence
5. Oracle review
6. close item or replan

This is the mechanism that prevents shortcuts. Without it, planning will drift again.
