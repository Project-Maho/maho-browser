# Archive/Library Reference-Driven Parallel Plan

**Created:** 2026-05-10  
**Status:** Implementation scheduling layer derived from the reference-driven roadmap  
**Base documents:**
- `.sisyphus/plans/arc-library-reference-driven-roadmap.md`
- `.sisyphus/plans/arc-library-strict-execution-plan.md`

This file is the scheduling layer for the same reference-driven work. It does not change the intent of the roadmap. It keeps the geometry-first logic, but it exposes only the concurrency that is actually safe.

---

## 0. Purpose

The purpose of this plan is to turn the reference-driven Archive and Library roadmap into a valid execution schedule.

The old roadmap was correct about file ownership, but it was serial by default. That was safe, yet too blunt. It hid the fact that some later work can run in parallel once the shell geometry seam is frozen.

This plan preserves the same reference baseline and the same file ownership rules. It simply separates:
- serial-only gates that must happen first
- safe parallel tracks that can run after the shell geometry freeze
- final serial verification and review gates

The goal is honest concurrency, not optimistic concurrency.

---

## 1. Source references

These are the source-of-truth references for every geometry decision in this plan:

- `maho/reference_screenshots/sidebar-reference.png`
- `maho/reference_screenshots/arc-sidebar.jpeg`
- `maho/reference_screenshots/arc-archive.jpeg`
- `maho/reference_screenshots/arc-file-archive.jpeg`

Current runtime comparison artifact:

- `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-archive-verify-20260510g/library.png`

The reference images define the target. The runtime artifact only shows the current failure state or the latest comparison result.

---

## 2. Why the old rail-only sequencing failed

The previous rail-first idea failed because it treated the rail as the main geometry owner when the visible mismatch was actually controlled by the shell and container.

That caused three problems:

1. It made local rail polish look like progress even when the outer composition stayed wrong.
2. It allowed work to start in a file that could not fix the dominant mismatch by itself.
3. It hid the real dependency, which is that the shell geometry must be corrected before rail alignment can be judged meaningfully.

In short, the rail-only sequence was serial in the wrong place. It delayed the real fix and made later work appear more independent than it actually was.

---

## 3. Collision and ownership map

This map shows why shell geometry correction must stay serial first, and why parallel work is only safe after that seam is frozen.

### 3.1 Serial geometry seam, overlapping ownership

These files all participate in the same visible shell geometry and must not be edited in parallel:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`

Why this is serial first:
- `maho_sidebar_container_view.cc` and `maho_sidebar_view.cc` both affect the visible shell shape and hosting behavior.
- `maho_sidebar_layout_tokens.h` is the shared geometry source, so width and spacing changes here affect both container and view behavior.
- `maho_sidebar_prefs.cc` participates in persistence and shell state decisions that can change the observed geometry path.
- These files overlap in user-visible effect, so parallel edits would blur causality and make runtime comparison unreadable.

### 3.2 Safe isolated track after shell freeze

This file is a narrower follow-up track, but only after Gate 1 is complete and frozen:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`

Why this can wait for parallelization:
- it owns rail internal alignment only
- it should not be used to compensate for shell width or container spacing defects
- once the shell seam is fixed, this file can be adjusted without touching the shell geometry owners

### 3.3 Validation-only surface

Test files are separate from production geometry files and can run as a disjoint track if they stay read-as-regression work:

- sidebar test files only

This is safe only when the test work does not require reopening shell geometry or rail presentation files.

---

## 4. Parallelization policy and ceiling

Parallelization is allowed only after the shell geometry freeze at Gate 1.

Policy:
- no parallel edits before Gate 1 completes
- no parallel edits inside the serial geometry seam
- parallel work may only use disjoint file sets
- if two tracks would touch the same file, the same behavior seam, or the same geometry decision, they are not parallel-safe
- any track that needs a file outside its allowed set must stop and trigger a replan before editing

Ceiling:
- maximum 2 concurrent tracks in the first parallel batch
- one track may edit a single production file only
- one track may edit sidebar tests only
- no third concurrent track unless a later plan explicitly proves a new disjoint surface

Why the ceiling is low:
- this work is still reference-driven and geometry-sensitive
- the runtime artifact is the real arbiter, so too much parallelism would make diffs hard to interpret
- the safe gain comes from disjoint ownership, not from raw thread count

---

## 5. Serial and parallel execution schedule

### Serial Gate 0, reference lock and geometry mismatch definition

**Item ID:** RG-0  
**Title:** Reference lock and geometry mismatch definition  
**Purpose:** Establish the exact mismatch between the references and the current runtime artifact before any code change is allowed.  
**Exact problem statement:** The current runtime image does not yet match the reference composition, so the dominant mismatch must be named in geometry terms before implementation starts. This prevents guesswork and keeps later edits tied to the real owner files.

**Allowed files:**
- none, read only

**Explicitly forbidden files:**
- every production file
- every test file
- every build file
- every plan file

**Required evidence:**
- a written mismatch note that cites the reference images and the current runtime artifact path
- a geometry description covering width, spacing, inset, composition, and rail versus shell dominance
- a statement naming the shell geometry owners that must move first

**Stop and replan triggers:**
- the mismatch cannot be described in geometry terms
- the comparison suggests rail polish is the first fix
- the analysis drifts into implementation before the mismatch is named clearly

**Execution sequence:**
1. Compare the current runtime artifact with the reference set.
2. Describe the mismatch using geometry language only.
3. Identify which owner files must change before rail work matters.
4. Stop once the mismatch is precise enough to drive shell correction.

**Acceptance criteria:**
- the dominant mismatch is documented clearly
- the shell geometry owners are named explicitly
- the rail file is not treated as the first fix

---

### Serial Gate 1, shell and container geometry correction

**Item ID:** SG-1  
**Title:** Shell and container geometry correction  
**Purpose:** Correct the visible shell geometry that controls what the user sees first.  
**Exact problem statement:** The outer sidebar composition still has the wrong proportions, spacing, or container behavior. The shell and container owners must be corrected before any rail tuning can be judged against the references.

**Allowed files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`

**Explicitly forbidden files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- sidebar test files
- build files
- archive pane files
- destination routing files
- unrelated sidebar feature files

**Required evidence:**
- `lsp_diagnostics` on all modified files with zero errors
- a fresh runtime artifact showing the shell geometry change
- a comparison note that ties the change back to the reference images

**Stop and replan triggers:**
- the fix needs rail internal alignment to make sense
- the visible issue moves outside the allowed shell owner set
- any change would require reopening the rail file or test files
- a compile or layout issue cannot be solved inside the allowed files

**Execution sequence:**
1. Use the mismatch definition from Gate 0.
2. Edit only the shell and container geometry owner files.
3. Correct width, spacing, and shell composition.
4. Run diagnostics on the modified files.
5. Capture a new runtime artifact.
6. Compare the result to the reference set.
7. Freeze the shell geometry seam before any parallel work begins.

**Acceptance criteria:**
- the runtime artifact visibly moves toward the reference composition
- the outer shell is the primary visible improvement
- no rail file was edited
- diagnostics are clean

---

### Parallel Batch 1, only after Gate 1 freeze

This batch is safe only after Gate 1 has completed, the shell geometry seam is frozen, and the new runtime artifact shows that shell-level mismatch is no longer the dominant problem.

#### Track PB1-A, rail internal alignment

**Item ID:** PB1-A  
**Title:** Rail internal alignment only  
**Purpose:** Make small rail alignment adjustments inside the corrected shell.  
**Exact problem statement:** The rail may still need padding, local spacing, or item alignment cleanup after the shell geometry is already correct. The rail file must not be used to repair shell width or container spacing.

**Allowed files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`

**Explicitly forbidden files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- sidebar test files
- archive pane files
- destination routing files
- unrelated sidebar feature files

**Required evidence:**
- `lsp_diagnostics` on the modified rail file with zero errors
- a runtime artifact showing the rail sits correctly inside the corrected shell
- a short note explaining why the shell geometry was already fixed first

**Stop and replan triggers:**
- the rail change starts asking for shell width or spacing edits
- the shell again becomes the dominant visible issue
- the change would drift into feature work or structural changes

**Execution sequence:**
1. Confirm Gate 1 is already complete and frozen.
2. Edit only `maho_sidebar_library_rail_view.cc`.
3. Make the smallest alignment improvement that fits the corrected shell.
4. Run diagnostics.
5. Capture runtime evidence.

**Acceptance criteria:**
- the rail is visually aligned inside the corrected shell
- the change stays local to the rail implementation file
- no shell file was reopened

#### Track PB1-B, regression hardening and tests

**Item ID:** PB1-B  
**Title:** Sidebar regression hardening tests  
**Purpose:** Protect the corrected geometry and current Library behavior without reopening production code.  
**Exact problem statement:** The implementation needs regression coverage that reflects the corrected shell and current Library behavior. The tests must remain a validation track, not a back door to feature expansion.

**Allowed files:**
- sidebar test files only

**Explicitly forbidden files:**
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- archive pane implementation files
- destination routing files
- unrelated sidebar feature files

**Required evidence:**
- `lsp_diagnostics` on the modified test files with zero errors
- the relevant test target passes
- the test intent is clearly regression protection, not feature expansion

**Stop and replan triggers:**
- the test needs production code changes outside the allowed set
- the test would encode a future wish list instead of the current contract
- the test failure can only be fixed by broad feature work

**Execution sequence:**
1. Identify the minimal regression surface to protect.
2. Edit only the allowed sidebar test files.
3. Add tests that describe the corrected behavior or geometry contract.
4. Run diagnostics.
5. Run the relevant test target.
6. Confirm the tests protect the current implementation.

**Acceptance criteria:**
- the corrected behavior is covered by tests
- the tests pass
- no production code was changed for this track

---

### Serial Verification Gate, runtime visual comparison

**Item ID:** V-1  
**Title:** Runtime visual comparison against references  
**Purpose:** Prove the implementation matches the reference-driven intent in the live runtime artifact.  
**Exact problem statement:** A plan is not complete until the updated runtime screenshot has been compared against the reference images and the visible result has been judged against the actual target composition.

**Allowed files:**
- no production code files
- no plan files
- only runtime verification artifacts

**Explicitly forbidden files:**
- all source files
- all build files
- all test files
- all plan files

**Required evidence:**
- a new runtime artifact path
- a comparison note that names the remaining visible differences, if any
- confirmation that the shell geometry change produced the visible improvement

**Stop and replan triggers:**
- the runtime artifact does not reflect the latest geometry changes
- the comparison shows the wrong files were changed first
- the remaining visible mismatch points back to shell geometry instead of rail alignment

**Execution sequence:**
1. Generate the runtime artifact after geometry work.
2. Compare it to the reference images.
3. Record the visible differences or the visual pass.
4. Decide whether any remaining item is still needed.

**Acceptance criteria:**
- the latest runtime artifact is available
- the result has been compared to the reference set
- the visual status is explicit, not implied

---

### Serial Review Gate, Oracle review and repository verification

**Item ID:** R-1  
**Title:** Oracle review and repo verification  
**Purpose:** Prevent subtle drift, missing requirements, or fragile cleanup from being accepted quietly.  
**Exact problem statement:** The implementation is not closed until Oracle has reviewed the final state and the repository-level verification steps have passed. This gate confirms that the code, tests, and requirements still agree.

**Allowed files:**
- no production code files
- no plan files
- only review notes and verification evidence

**Explicitly forbidden files:**
- all source files unless a failure forces a new plan
- all build files unless a failure forces a new plan
- all test files unless a failure forces a new plan
- all plan files unless a failure forces a new plan

**Required evidence:**
- Oracle review PASS
- `bun run typecheck`
- `bun run test`
- requirement cross-check against this plan and the reference-driven roadmap

**Stop and replan triggers:**
- Oracle finds a gap in requirement coverage or code quality
- typecheck or test fails
- the final state does not match the allowed-file boundaries defined above
- the review reveals drift that belongs in a new plan item

**Execution sequence:**
1. Run Oracle review on the final implementation state.
2. Run `bun run typecheck`.
3. Run `bun run test`.
4. Cross-check the final result against this plan and the reference-driven roadmap.
5. Replan if any result fails or reveals out-of-scope drift.

**Acceptance criteria:**
- Oracle PASS is recorded
- typecheck passes
- tests pass
- the requirement cross-check confirms the implementation stayed within the plan

---

## 6. How the parallelization actually works

The only safe parallelism in this work appears after Gate 1.

What can run together:
- PB1-A, rail alignment in `maho_sidebar_library_rail_view.cc`
- PB1-B, sidebar regression tests

What cannot run together:
- any two shell geometry edits from Gate 1
- any rail edit that needs shell geometry reopened
- any test work that is really a hidden production change

Why this is safe:
- the shell geometry seam is already frozen
- the rail track has one production file only
- the test track stays in test files only
- the two tracks do not share file ownership

---

## 7. Closure rule

This scheduling layer is complete only when:
- Gate 0 has defined the mismatch in geometry terms
- Gate 1 has fixed the shell and container geometry first
- the parallel batch has run only within its disjoint file sets
- the runtime comparison has been performed against the reference set
- Oracle review and repository verification have both passed

If any later task needs a file outside its allowed set, stop and write a new plan instead of widening this one.
