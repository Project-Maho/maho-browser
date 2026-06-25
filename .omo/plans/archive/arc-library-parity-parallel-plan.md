# Arc-style Library Parity — Parallel Implementation Plan

**Created:** 2026-05-10  
**Branch:** main  
**Status:** Draft — revised after Momus QA feedback  
**Primary Scope:** `maho-chromium/browser/ui/views/sidebar/*`, download bridge surfaces, and existing Easel / Boost / Spaces entry points  
**Primary Goal:** Deliver a true Arc-like sidebar Library experience, not just an Archived Tabs panel, by shipping the full multi-category Library surface end-to-end.

---

## Objective

Turn the current native sidebar archive mode into a full Library mode that supports the Arc-style left rail and right-pane experience across these categories:

- Archived Tabs
- Downloads
- Media
- Easels
- Spaces
- Boosts

This plan is intentionally **full-scope**. The shell refactor is only the seam-creation step. The project is not done until Downloads and Media are real Library destinations, Easels / Spaces / Boosts are integrated into the same Library experience, and the final surface behaves like one coherent product.

---

## Evidence-Based Starting Point

### Confirmed current state

1. The current native Library rail supports **only one category**:
   - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
   - `MahoSidebarLibraryRailView::Category` contains only `kArchivedTabs`

2. The current sidebar body modes are only:
   - `kTabs`
   - `kArchive`
   - `kCreateSpace`
   - Defined in `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`

3. Archived Tabs is already implemented in native Views:
   - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.h`
   - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`

4. Downloads already have lower-level product capability, but **no Library category or surface**:
   - `maho-chromium/third_party/maho/maho_ffi.h`
   - Existing APIs include `maho_core_get_download_view_models`, pause, resume, cancel, remove, complete, restore-name
   - Download theming exists in `maho-chromium/browser/ui/downloads/maho_download_theme.cc`

5. Media does **not** have a current dedicated user-facing surface in the Library path.

6. Easel and Boost already exist as standalone WebUI surfaces:
   - `chrome://maho-easel/`
   - `chrome://maho-boost/`
   - Registered in `maho-chromium/components/constants/webui_url_constants.h`

7. Spaces exists as a real sidebar subsystem, but **not** as a Library category:
   - Existing native sidebar and page-handler code already own Spaces behavior
   - This must be integrated, not re-invented

8. The current footer exposes an archive toggle, not a generic Library opener:
   - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.h`
   - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`

9. Sidebar test targets already exist in `maho-chromium/browser/ui/views/sidebar/BUILD.gn`:
   - `source_set("interactive_tests")` includes:
     - `maho_sidebar_navigation_interactive_test.cc`
     - `maho_sidebar_spaces_interactive_test.cc`
   - `source_set("unit_tests")` and `source_set("browser_tests")` also exist for the sidebar area

### Architectural implication

The codebase currently has a **mini-library scaffold** for Archived Tabs only. It does **not** yet have a general-purpose Library shell.

---

## Target End State

### User-visible outcome

The left-bottom sidebar opens a unified Library surface with a left rail and right content pane. The rail exposes:

- Archived Tabs
- Downloads
- Media
- Easels
- Spaces
- Boosts

The right pane changes based on the selected item while preserving consistent:

- focus behavior
- back behavior
- theming
- loading / empty / error states
- keyboard navigation
- window caption hit-testing

### Backing model by category

| Category | Delivery Type | Backing Path |
|---|---|---|
| Archived Tabs | Native content view | Existing `MahoSidebarArchiveView` migrated onto generalized shell |
| Downloads | Native content view | Existing core / FFI download APIs |
| Media | Native content view | New Library view + media contract, seeded from download/file metadata as needed |
| Easels | Integrated launch or embedded surface | Existing `chrome://maho-easel/` WebUI |
| Spaces | Integrated subsystem bridge | Existing sidebar Spaces flows / management surfaces |
| Boosts | Integrated launch or embedded surface | Existing `chrome://maho-boost/` WebUI |

### Non-negotiable product rule

This work is complete only when the Library behaves like **one product surface**, not a collection of unrelated buttons bolted onto the archive panel.

---

## In-Scope Files

### Core shell files to modify

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc`

### Existing feature surfaces to integrate

- `maho-chromium/third_party/maho/maho_ffi.h`
- `maho/crates/maho-ffi/src/lib.rs`
- `maho-chromium/browser/ui/downloads/maho_download_theme.cc`
- `maho-chromium/browser/ui/webui/maho_easel/maho_easel_ui.cc`
- `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.cc`
- `maho-chromium/components/constants/webui_url_constants.h`

### Likely new native Library views

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_downloads_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_downloads_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_media_view.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_media_view.cc`
- optional shared helper if needed for common empty/loading/error states inside Library category panes

### Test files to extend or add

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_navigation_interactive_test.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`
- additional sidebar interactive tests for Downloads / Media / Library navigation if needed
- unit coverage for category routing and any new adapter logic

---

## Explicitly Out of Scope

- No unrelated redesign of the regular sidebar tabs surface
- No command palette rewrite
- No assumption that Media already exists
- No fake Arc parity by adding dead rail items without real destinations
- No rewrite of Spaces ownership away from the existing sidebar subsystem
- No commit instructions in this plan

---

## Parallelization Strategy

This project has **one required serial seam-creation phase**, followed by the largest safe parallel batch.

### Why a serial seam is mandatory

Without a seam, every category implementation would compete for the same files and keep reintroducing special cases for Archived Tabs. The first objective is to make the Library shell category-driven instead of archive-driven.

### Parallelism ceiling

- Recommended concurrent implementation tracks after shell seam: **5**
- Recommended concurrent polish / verification tracks after first merge: **3**

---

## Serial Gates

## Gate 0 — Lock Library Taxonomy and Entry Semantics

**Purpose:** Prevent later workstreams from modeling categories inconsistently.

**Required decisions locked by this gate:**
- The footer entry becomes a **Library opener**, not an archive-only toggle
- Archived Tabs remains the first migrated category
- Downloads and Media are native Library panes
- Easels and Boosts are integrated through existing WebUI surfaces
- Spaces is integrated as a subsystem bridge, not rewritten as a fake document library

**Acceptance criteria:**
- One written category taxonomy is agreed and encoded in code comments / plan
- No workstream starts from a different interpretation of category ownership

**Executable QA scenario:**
- Tool: code review + targeted file read
- Files to inspect:
  - `maho_sidebar_library_rail_view.h`
  - `maho_sidebar_view.h`
  - `maho_sidebar_footer_view.h`
- Expected result:
  - one taxonomy owns all Library destinations
  - one footer entry semantic is documented
  - no duplicate or conflicting category definitions remain

---

## Gate 1 — Create the Generalized Library Shell

**Purpose:** Convert the current archive-specific shell into a reusable Library shell.

**Target files:**
- `maho_sidebar_footer_view.h/.cc`
- `maho_sidebar_library_rail_view.h/.cc`
- `maho_sidebar_view.h/.cc`
- `maho_sidebar_archive_view.h/.cc`

**Required changes:**
- Rename `MahoSidebarBodyMode::kArchive` to `kLibrary` and update the footer action semantics from archive-only toggle to Library opener semantics
- Expand `MahoSidebarLibraryRailView::Category` to all planned Library items
- Replace archive-specific rail logic with category-driven selection
- Replace archive-specific content host logic with generic Library content routing
- Update the footer entry so it opens the generalized Library surface
- Generalize focus capture / restore so Library entry and exit are handled at the shell level, not as an archive-only special case
- Generalize caption hit-testing so the shell checks the currently visible Library pane instead of hardcoding `archive_view_`
- Preserve Archived Tabs behavior while moving it onto the new shell contract

**Acceptance criteria:**
- The Library shell can host more than one category without further shell refactors
- Archived Tabs still works as before
- `kLibrary` is the canonical body mode for the Library container and no archive-only shell naming remains in the routing layer
- Back behavior, focus restore, and caption hit testing remain correct

**Executable QA scenario:**
- Tools:
  - `lsp_diagnostics` on `maho-chromium/browser/ui/views/sidebar/`
  - sidebar interactive tests from `browser/ui/views/sidebar/BUILD.gn`
- Required checks:
  - no new diagnostics errors in modified sidebar files
  - archive navigation interactive coverage still passes
  - spaces interactive coverage still passes if footer opener semantics changed
- Manual smoke:
  1. launch browser with sidebar visible
  2. click footer Library opener
  3. confirm Library surface opens instead of archive-only special casing
  4. click back and confirm return to tabs
- Expected result:
  - diagnostics clean
  - interactive tests green
  - open/back flow works with no broken focus or window hit-testing
  - Library panes beyond Archived Tabs inherit correct focus and caption behavior from the shell seam

---

## Gate 2 — Shared Library Pane Contract Exists

**Purpose:** Make category panes independently implementable in parallel.

**Required changes:**
- Standardize pane-level concepts:
  - title
  - icon
  - loading state
  - empty state
  - error state
  - per-item action hooks
- Define category registration / routing in one place
- Ensure `MahoSidebarView` does not need a new special case for every category
- Define lazy pane construction: pane views are created on first selection and cached thereafter, rather than all being eagerly constructed in `BuildUi()`

**Acceptance criteria:**
- Downloads, Media, Easels, Spaces, and Boosts can land without another shell rewrite
- Adding a category no longer requires spreading new conditionals across multiple files
- `library_content_host_` supports exactly one visible pane at a time, with deferred creation for panes not yet visited

**Executable QA scenario:**
- Tools:
  - code review
  - `lsp_diagnostics`
  - lightweight category switching test or smoke harness
- Required checks:
  - a temporary stub category can be added by changing only the registry / routing seam
  - `MahoSidebarView` does not gain repeated `if category == ...` special cases per destination
- Expected result:
  - category registration is centralized
  - diagnostics clean
  - switching logic is seam-based, not category-copy-paste-based
  - pane construction is lazy and does not require eager creation of every Library destination at startup

---

## Parallel Batch 1 — Full Feature Buildout

These tracks start only after **Gate 2** is complete.

### Track A — Archived Tabs Migration and Library Regression Protection

**Goal:** Preserve the existing archive experience on top of the new shell.

**Owned surface:**
- Archived Tabs pane behavior under the generalized Library mode
- Archive selection defaults and back behavior

**Primary files:**
- `maho_sidebar_archive_view.h/.cc`
- `maho_sidebar_view.h/.cc`
- `maho_sidebar_navigation_interactive_test.cc`

**Must deliver:**
- No regression in search, filter, restore, delete, or empty states
- Archived Tabs selectable from the new multi-item rail
- Footer entry now opens Library while still making Archived Tabs reachable by default if no other category has been selected before

**Acceptance criteria:**
- Existing archive tests stay green
- Archived Tabs still feels native and first-class inside the new Library shell

**Executable QA scenario:**
- Tools:
  - existing archive interactive coverage in `maho_sidebar_navigation_interactive_test.cc`
  - `lsp_diagnostics` for archive and shell files
- Manual steps:
  1. open Library
  2. select Archived Tabs
  3. search for an archived row
  4. change filter
  5. restore one row
  6. delete one row
- Expected result:
  - all archive tests pass
  - search/filter/restore/delete still work
  - focus and back behavior remain stable

---

### Track B — Downloads Library Pane

**Goal:** Ship the first real post-archive Library category.

**Owned surface:**
- Native Downloads pane
- Download list rendering
- Download item actions
- Download empty/error/loading states

**Primary files:**
- new `maho_sidebar_downloads_view.h/.cc`
- `maho_sidebar_view.h/.cc`
- `maho-chromium/third_party/maho/maho_ffi.h`
- `maho/crates/maho-ffi/src/lib.rs` if view-model shaping is required
- optional adapter logic inside sidebar model / controller code

**Must deliver:**
- Render real download items from existing core APIs
- Support pause, resume, cancel, remove, and open/reveal actions as available
- Reuse existing download theming where reasonable instead of inventing a separate styling system

**Acceptance criteria:**
- Downloads is a real Library destination, not just a launcher
- The pane displays live download data and supports actions end-to-end

**Executable QA scenario:**
- Tools:
  - `lsp_diagnostics` on Downloads view files and bridge files
  - targeted Rust tests if FFI or download view-model shaping changes
  - sidebar interactive or smoke coverage for Downloads pane navigation
- Manual steps:
  1. seed or trigger at least one active and one completed download
  2. open Library → Downloads
  3. confirm both rows render with correct status
  4. pause and resume the active download
  5. remove a completed download
  6. open or reveal one finished file
- Expected result:
  - action state updates are visible in-pane
  - diagnostics clean
  - Rust/bridge tests pass if changed
  - navigation into Downloads works from the Library rail

---

### Track C — Media Library Pane

**Goal:** Ship Media as a true Library category, not a dead placeholder.

**Owned surface:**
- Native Media pane
- Media item list or grid
- File-type grouping and filtering
- Media-specific empty/error/loading states

**Primary files:**
- new `maho_sidebar_media_view.h/.cc`
- any new bridge / adapter file required to provide media-friendly metadata
- `maho-chromium/third_party/maho/maho_ffi.h` and `maho/crates/maho-ffi/src/lib.rs` if current download models lack fields needed for Media

**Implementation rule:**
- Media v1 is explicitly a **downloads-filtered media view**, grounded in existing download/file metadata and filtered by media-capable MIME / filename heuristics until a richer index exists later.

**Must deliver:**
- Browsable Media category in the rail
- Clear distinction from Downloads, including different presentation (for example grid or preview-first layout rather than the same list treatment)
- At minimum: thumbnails or file-type icons, title/path, type/date grouping, open/reveal actions

**Acceptance criteria:**
- Media is a real pane with meaningful content logic
- Media does not require another shell refactor to evolve later

**Executable QA scenario:**
- Tools:
  - `lsp_diagnostics` on Media files and any adapters
  - targeted unit tests for media filtering/grouping if implemented outside the view
- Manual steps:
  1. ensure local test data includes image, video, and PDF files
  2. open Library → Media
  3. confirm only media-capable items appear
  4. apply type filter or grouping change if implemented
  5. open or reveal one media file
- Expected result:
  - Media shows distinct content from Downloads
  - filters/grouping behave deterministically
  - diagnostics and adapter tests are clean

---

### Track D — Easels and Boosts Library Integration

**Goal:** Bring existing standalone surfaces under the Library umbrella without duplicating them.

**Owned surface:**
- Easels Library rail item and destination behavior
- Boosts Library rail item and destination behavior

**Primary files:**
- `maho_sidebar_library_rail_view.h/.cc`
- `maho_sidebar_view.h/.cc`
- `maho-chromium/components/constants/webui_url_constants.h`
- existing `maho_easel` and `maho_boost` WebUI entry points

**Implementation rule:**
- Reuse existing `chrome://maho-easel/` and `chrome://maho-boost/` surfaces as **tab-launch integrations**. Do not fork their UI into second copies and do not attempt sidebar embedding in v1.

**Must deliver:**
- Easels and Boosts appear in the Library rail
- Selecting them reaches the existing product surfaces through a coherent Library flow, using tab launch as the canonical integration path

**Acceptance criteria:**
- Easels and Boosts are discoverable from Library
- Integration feels intentional, not bolted on

**Executable QA scenario:**
- Tools:
  - `lsp_diagnostics` on Library shell files and any integration wrappers
  - manual browser smoke for `chrome://maho-easel/` and `chrome://maho-boost/`
- Manual steps:
  1. open Library
  2. select Easels and confirm the existing Easel surface opens or hosts correctly
  3. return to Library and select Boosts
  4. confirm the existing Boost surface opens or hosts correctly
- Expected result:
  - both items are reachable from Library
  - no duplicate surface implementations were introduced
  - route handoff is clean

---

### Track E — Spaces Library Integration

**Goal:** Add Arc-style Spaces presence inside Library while preserving the existing sidebar subsystem as the source of truth.

**Owned surface:**
- Spaces Library item
- Spaces summary / launcher / management entry behavior inside Library

**Primary files:**
- `maho_sidebar_view.h/.cc`
- existing sidebar Spaces management files and page handlers
- optional new native Spaces summary pane if direct launch is not sufficient

**Implementation rule:**
- Spaces should integrate as a bridge into the existing Spaces system, not replace it.

**Must deliver:**
- Library rail item for Spaces
- Reachable management or summary experience from Library
- Clean handoff into the existing Spaces subsystem

**Acceptance criteria:**
- Spaces is present in Library like Arc, but ownership remains technically sane
- No duplicate space-management logic is introduced unnecessarily

**Executable QA scenario:**
- Tools:
  - existing Spaces interactive coverage in `maho_sidebar_spaces_interactive_test.cc` where relevant
  - `lsp_diagnostics` on modified sidebar and Spaces files
- Manual steps:
  1. open Library → Spaces
  2. trigger the summary or management flow
  3. create or open a Space through the integrated path
  4. confirm the existing sidebar Spaces subsystem remains authoritative
- Expected result:
  - Library can reach Spaces management
  - no duplicated state appears
  - existing Spaces tests remain green

---

### Track F — Test and Verification Scaffolding

**Goal:** Prevent the generalized Library shell from becoming a regression magnet.

**Owned surface:**
- Shared Library navigation coverage
- Category switching coverage
- Pane-specific smoke coverage

**Primary files:**
- `maho_sidebar_navigation_interactive_test.cc`
- `maho_sidebar_spaces_interactive_test.cc`
- any new sidebar interactive tests
- unit tests for category routing and adapters

**Must deliver:**
- coverage for rail selection
- coverage for back behavior
- coverage for Library opener semantics
- coverage for Downloads actions and Media visibility
- coverage for Easel / Boost / Spaces launch integration

**Acceptance criteria:**
- test scaffolding lands before polish phase so regressions are caught during integration, not after

**Executable QA scenario:**
- Required automated checks:
  - category switching test
  - footer opener test
  - Archived Tabs regression test
  - Downloads pane smoke test
  - Easel / Boost / Spaces integration smoke tests
- Expected result:
  - every new Library destination has at least one automated routing test and one pane-specific smoke path

---

## Parallel Batch 2 — Parity Polish and Behavior Unification

These tracks start after Batch 1 feature streams are merged.

### Track G — UX Consistency and Shared States

**Goal:** Make all categories feel like one Library product.

**Must deliver:**
- unified spacing, header, divider, selection, and focus behavior
- shared loading, empty, and error state treatment
- category-consistent search / filter affordances where applicable
- persistence of last selected Library category between opens if desired by product direction

**Primary files:**
- Library shell files
- all pane files introduced in Batch 1

**Acceptance criteria:**
- no category feels like a prototype attached to the side of another category

**Executable QA scenario:**
- Manual review checklist:
  1. open every category in sequence
  2. compare header spacing, rail selection state, empty states, and loading treatment
  3. confirm last-selected behavior if implemented
- Expected result:
  - category transitions feel visually and behaviorally consistent
  - no one-off styling or state logic leaks remain obvious

---

### Track H — Footer, Focus, and Caption Hit-Testing Hardening

**Goal:** Ensure the new Library does not break native window behavior.

**Note:** First-time focus and caption-hit-test generalization belongs in **Gate 1**. This track is only for regression hardening after all Batch 1 feature work lands.

**Must deliver:**
- correct footer opener behavior
- stable focus restore when entering / leaving Library
- preserved caption hit-testing for draggable window regions
- consistent back behavior from all categories

**Acceptance criteria:**
- native window interactions remain correct after Library expansion

**Executable QA scenario:**
- Tools:
  - existing sidebar navigation / interactive tests
  - manual desktop smoke on draggable regions
- Manual steps:
  1. open Library from footer using mouse and keyboard
  2. move focus into pane controls and back out
  3. drag the window from non-interactive caption regions
  4. click interactive controls near the caption edge
- Expected result:
  - focus restore is deterministic
  - footer open/back behavior is stable
  - drag regions still work where intended

---

### Track I — Final QA and Release Hardening

**Goal:** Validate the entire Library experience end-to-end.

**Must deliver:**
- buildable overlay
- diagnostics clean on changed files
- sidebar smoke pass across all categories
- regression confirmation for Archived Tabs and Spaces

**Acceptance criteria:**
- full Library shell is ship-ready, not just structurally present

**Executable QA scenario:**
- Required commands:
  - `bun run typecheck` in `maho/` when Rust-side bridge files change
  - targeted Rust tests for any changed `maho-ffi` or core adapter logic
  - relevant Chromium-side sidebar test targets after shell and pane work
  - final overlay build validation using the repo-standard build path if required by scope
- Required manual smoke sequence:
  1. open Library from the footer
  2. visit Archived Tabs, Downloads, Media, Easels, Spaces, Boosts in order
  3. perform one primary action in each category
  4. exit back to tabs and reopen Library
- Expected result:
  - build and tests are green
  - diagnostics are clean
  - all categories can be entered and exercised without broken navigation or empty shell failures

---

## Dependency Graph

- **Tracks A-F** all depend on **Gate 2**
- **Track B (Downloads)** depends on existing download APIs and may require small bridge shaping
- **Track C (Media)** depends on a concrete media-friendly metadata contract and may require new bridge work
- **Track D (Easel / Boost)** depends on shell routing only, not on Downloads or Media internals
- **Track E (Spaces)** depends on shell routing and existing Spaces subsystem entry points
- **Tracks G-I** depend on Batch 1 landing

---

## Risks and Mitigations

### Risk 1 — Shell refactor churn breaks Archived Tabs
**Mitigation:** Move Archived Tabs first onto the new contract before any other category lands.

### Risk 2 — Media becomes a fake category with no real value
**Mitigation:** Do not ship Media until it has real content semantics and explicit data contract support.

### Risk 3 — Downloads actions duplicate existing action routing patterns
**Mitigation:** Route download actions through one consistent sidebar adapter path and reuse existing core APIs.

### Risk 4 — Easels / Boosts / Spaces are modeled as the wrong destination type
**Mitigation:** Keep explicit distinction between:
- native content pane
- launch / embed surface
- subsystem bridge

### Risk 5 — Focus and caption regressions in native Views
**Mitigation:** Treat these as first-class acceptance criteria, not polish afterthoughts.

---

## Verification Plan

### Global required evidence

- `lsp_diagnostics` clean on all modified files
- sidebar interactive tests updated and passing where routes changed
- pane-specific tests added for every newly introduced Library destination
- bridge or Rust-side tests added where FFI shaping changes were required
- targeted build or typecheck passes for every touched layer

### Global command checklist

- `bun run typecheck` in `maho/` when Rust-side bridge files change
- targeted Rust tests for any changed `maho-ffi` or core adapter logic
- relevant Chromium-side sidebar tests after shell and pane work
- final overlay build validation using the repo-standard build path if required by scope

### Global manual smoke checklist

1. Open Library from footer
2. Switch between all categories
3. Return to tabs via back behavior
4. Confirm Archived Tabs still supports search/filter/restore/delete
5. Confirm Downloads renders real items and actions work
6. Confirm Media renders correctly from real backing data
7. Confirm Easels and Boosts open through Library
8. Confirm Spaces handoff works without duplicate logic
9. Reopen Library and ensure category state / focus behavior remains stable

### Release gate

No feature stream is considered done until its executable QA scenario has passed and its required diagnostics / tests have been recorded as green.

---

## Execution Order

1. Lock category taxonomy and Library opener semantics
2. Build generalized Library shell
3. Migrate Archived Tabs onto the new shell and verify no regression
4. Run Downloads, Media, Easel / Boost, Spaces, and test-scaffolding tracks in parallel
5. Merge feature streams
6. Run UX consistency, focus hardening, and final QA tracks
7. Ship only after the whole Library feels coherent and diagnostics / tests are clean

---

## Definition of Done

The work is done only when:

- the footer opens a real Library surface rather than an archive-only mode
- the Library rail contains all target categories
- Archived Tabs still works
- Downloads is a real Library destination backed by real data and actions
- Media is a real Library destination backed by a real contract
- Easels and Boosts are integrated into Library through the existing surfaces
- Spaces is integrated into Library without breaking the existing sidebar subsystem
- focus, caption hit-testing, loading states, and empty states are consistent
- verification passes across the full Library experience

The intended end result is **Arc-style Library parity as a product surface**, not just category labels added to an Archived Tabs panel.
