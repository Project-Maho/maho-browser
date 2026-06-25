# Archive/Library Reference-Driven Roadmap

**Created:** 2026-05-10  
**Status:** Active execution contract  
**Supersedes for execution:** `.sisyphus/plans/arc-library-full-active-roadmap.md`

This roadmap replaces the failed narrow rail only contract. The prior roadmap is now context only because it kept the work inside `maho_sidebar_library_rail_view.cc` and `maho_sidebar_library_rail_view.h`, even though the visible result was dominated by shell and container geometry owned elsewhere. The remaining work must be driven by the reference images and by the files that actually control what the user sees first.

---

## 0. Roadmap purpose

This document is the execution contract for the remaining Archive and Library work. Its job is to keep the implementation reference driven, geometry first, and bounded to the files that own the visible result.

The target outcome is not abstract parity. The target outcome is a visible shape that matches the reference set closely enough that the runtime screenshot no longer looks like a narrow rail trapped inside the wrong shell geometry.

If an item cannot move the visible result because it is not allowed to touch the real geometry owners, the item is wrong and must stop.

---

## 1. Source of truth references

These images define the visual target and are the reference baseline for all geometry decisions.

* `maho/reference_screenshots/sidebar-reference.png`
* `maho/reference_screenshots/arc-sidebar.jpeg`
* `maho/reference_screenshots/arc-archive.jpeg`
* `maho/reference_screenshots/arc-file-archive.jpeg`

Current failed state comparison artifact:

* `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-archive-verify-20260510g/library.png`

The reference images are the source of truth. The runtime artifact is only a comparison point, never a design source.

---

## 2. Current failed state diagnosis

The previous active roadmap, `.sisyphus/plans/arc-library-full-active-roadmap.md`, failed as an execution contract because it narrowed the implementation to rail presentation work while the visible bottleneck lived in shell geometry.

The symptom was simple, almost nothing visibly changed. The cause was structural.

* The old plan allowed the rail file set, but the runtime artifact was still constrained by the fixed 64 dp rail width and by shell level layout decisions.
* The files that dominate the visible result were not in scope, so the work could improve local rail details without changing the overall composition.
* The plan therefore blocked the exact files that needed to move first, which meant the widest visual mismatch stayed untouched.
* In practice, that meant the old roadmap was a contract for local polish, not for geometry correction.

This new roadmap is explicitly different. It starts with the visible geometry owners, then moves inward to rail alignment only after the shell shape is corrected.

---

## 3. Geometry ownership map

These are the files that own the visible result, in order of impact.

### 3.1 Shell and container geometry owners

* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`

These files control the outer composition, sizing, spacing, and the geometry that the user notices first.

### 3.2 Rail internal alignment owner

* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`

This file is for internal rail alignment only. It is not the first lever, because rail local tweaks cannot fix a shell that is still sized or spaced incorrectly.

### 3.3 Files that are outside the execution contract unless an item explicitly allows them

* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
* archive pane files
* destination routing files
* unrelated sidebar feature files
* build system files unless a regression item explicitly requires them

---

## 4. Non negotiable rules

* One item at a time. No parallel implementation.
* Geometry first. If the runtime artifact still looks wrong at the shell level, do not spend effort on rail polish.
* Reference driven. Every geometry choice must be explainable against the reference images.
* No production code edits outside the allowed files for the active item.
* No rail first sequencing. The first actionable item must address reference locked geometry mismatch in the shell, not rail cosmetics.
* No completion without evidence. A claim of success without a comparison artifact is not acceptable.
* No silent contract drift. If the item needs files outside its allowed set, stop and replan before editing code.
* The old roadmap is superseded for execution. It may be read for history, but it is not the live checklist.

---

## 5. Remaining bounded items

### Item REF-LIBRARY-001

**Title:** Reference locked visual diff and geometry mismatch definition

**Purpose:** Establish the exact mismatch between the reference images and the current runtime artifact before any code changes are attempted.

**Exact problem statement:** The current runtime screenshot does not match the reference composition. The visible mismatch must be defined in geometry terms, such as overall sidebar width, rail width, content inset, vertical rhythm, header alignment, and spacing between shell regions, so later edits can target the real owner files instead of guessing.

**Allowed files:**
* none, read only comparison item

**Explicitly forbidden files:**
* every production code file
* every test file
* every build file
* every plan file except this roadmap if a note needs to be updated later

**Required evidence:**
* a written mismatch note that names the dominant geometry gaps against the reference images
* a comparison that points to the runtime artifact path and the reference paths
* a statement that confirms which owner files must move first

**Stop and replan triggers:**
* the mismatch cannot be described in geometry terms
* the comparison reveals that the shell files are actually the primary problem but the item cannot proceed because of scope confusion
* the analysis starts drifting into implementation before the mismatch is named clearly

**Execution sequence:**
1. Compare the current runtime artifact to the reference images.
2. Describe the mismatch using geometry language only.
3. Identify which owner files must change before rail alignment can matter.
4. Stop once the mismatch is precise enough to drive shell correction.

**Acceptance criteria:**
* the visible mismatch is documented clearly enough to guide implementation
* the shell geometry owners are named explicitly
* the rail file is not treated as the first fix

---

### Item GEO-LIBRARY-002

**Title:** Shell and container geometry correction

**Purpose:** Fix the outer geometry that controls what the user sees first.

**Exact problem statement:** The sidebar shell still carries the wrong visible proportions. The fixed width and spacing behavior must be corrected at the container and view level so the library surface can resemble the reference composition before any rail internal tuning is attempted.

**Allowed files:**
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`

**Explicitly forbidden files:**
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
* archive pane files
* destination routing files
* unrelated sidebar feature files
* build or test files unless a later regression item explicitly allows them

**Required evidence:**
* `lsp_diagnostics` on the modified files with zero errors
* a fresh runtime artifact showing the shell geometry change
* a comparison note that ties the change back to the reference images

**Stop and replan triggers:**
* the fix requires rail internal alignment to make sense
* the visible issue turns out to be elsewhere after shell changes are tried
* the change would need files outside the allowed shell owner set
* a compile or layout issue appears that cannot be solved inside the allowed files

**Execution sequence:**
1. Use the reference locked mismatch from Item REF-LIBRARY-001.
2. Edit only the shell and container geometry owner files.
3. Correct the width, spacing, and composition problems that dominate the runtime artifact.
4. Run diagnostics on the modified files.
5. Generate a fresh runtime artifact.
6. Compare the result to the reference set.

**Acceptance criteria:**
* the runtime artifact moves visibly toward the reference composition
* the outer shell geometry is the primary visible improvement
* no rail file was edited
* diagnostics are clean

---

### Item RAIL-LIBRARY-003

**Title:** Rail internal alignment after shell correction

**Purpose:** Fine tune the rail only after the outer shell geometry is already in the right shape.

**Exact problem statement:** Once the shell is corrected, the library rail may still need internal alignment so its items, padding, and local relationships sit cleanly inside the corrected container. This is a secondary geometry task, not the main fix.

**Allowed files:**
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`

**Explicitly forbidden files:**
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`
* archive pane files
* destination routing files
* unrelated sidebar feature files
* build or test files unless a later regression item explicitly allows them

**Required evidence:**
* `lsp_diagnostics` on the modified rail file with zero errors
* a runtime artifact showing the rail sits correctly inside the corrected shell
* a short note explaining why the shell geometry was already fixed first

**Stop and replan triggers:**
* the rail change starts asking for shell width or spacing edits
* the shell still looks wrong and the rail work would be cosmetic only
* the item starts drifting into feature work or structural changes

**Execution sequence:**
1. Confirm the shell correction is already in place.
2. Edit only `maho_sidebar_library_rail_view.cc`.
3. Make the smallest alignment improvement that fits the corrected shell.
4. Run diagnostics.
5. Capture runtime evidence.

**Acceptance criteria:**
* the rail is visually aligned inside the corrected shell
* the change stays local to the rail implementation file
* no shell file was reopened

---

### Item VERIFY-LIBRARY-004

**Title:** Runtime visual verification and comparison

**Purpose:** Prove the implemented geometry matches the reference driven intent in the live runtime artifact.

**Exact problem statement:** A plan is not complete until the updated runtime screenshot has been compared against the reference images and the visible result has been judged against the actual target composition.

**Allowed files:**
* no production code files
* no plan files
* only verification artifacts produced by the runtime harness

**Explicitly forbidden files:**
* all source files
* all build files
* all test files
* all plan files

**Required evidence:**
* a new runtime artifact path
* a comparison note that names the remaining visible differences, if any
* confirmation that the shell geometry change is what produced the visible improvement

**Stop and replan triggers:**
* the runtime artifact does not reflect the latest geometry changes
* the comparison shows the wrong files were changed first
* the remaining visible mismatch points back to shell geometry instead of rail alignment

**Execution sequence:**
1. Generate the runtime artifact after geometry work.
2. Compare it to the reference images.
3. Record the visible differences or the visual pass.
4. Decide whether any remaining item is still needed.

**Acceptance criteria:**
* the latest runtime artifact is available
* the result has been compared to the reference set
* the visual status is explicit, not implied

---

### Item TEST-LIBRARY-005

**Title:** Regression hardening, tests only

**Purpose:** Add or update tests so the geometry contract and current Library behavior do not regress after the visible fix lands.

**Exact problem statement:** The implementation needs test coverage that protects the corrected shell geometry and the current Library behavior without reopening production code or expanding scope into new features.

**Allowed files:**
* sidebar test files only
* sidebar BUILD files only if required to wire those tests
* the minimal supporting test files needed to express the current contract

**Explicitly forbidden files:**
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`
* `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
* archive pane implementation files
* destination routing files
* unrelated sidebar feature files

**Required evidence:**
* `lsp_diagnostics` on the modified test files with zero errors
* the relevant test target passes
* the test intent is clearly regression protection, not feature expansion

**Stop and replan triggers:**
* the test needs production code changes outside the allowed set
* the test would encode a future wish list instead of the current contract
* the test failure can only be fixed by broad feature work

**Execution sequence:**
1. Identify the minimal regression surface to protect.
2. Edit only the allowed test files.
3. Add tests that describe the corrected behavior or geometry contract.
4. Run diagnostics.
5. Run the relevant test target.
6. Confirm the tests protect the current implementation.

**Acceptance criteria:**
* the corrected behavior is covered by tests
* the tests pass
* no production code was changed for this item

---

## 6. Roadmap closure rule

This roadmap is complete only when all remaining items have either been finished with evidence or intentionally replaced by a newly written roadmap after a stop and replan event.

If a future task does not fit these items, stop and decompose it again before editing code.

The old roadmap, `.sisyphus/plans/arc-library-full-active-roadmap.md`, is superseded for execution because it kept the work away from the geometry owner files that dominate the visible result.
