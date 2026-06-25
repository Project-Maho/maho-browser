# Maho AI plus-picker realization and reference parity plan

## Purpose

This plan locks the next implementation pass for the compact Maho AI plus-button picker before code changes begin.

The picker has improved from its earlier card-like prototype, but it is still only partially aligned with the supplied reference and only partially backed by real sources. The next pass must make every intended source honest and real where feasible, while removing or deferring anything the branch cannot support yet.

This plan exists to prevent two failure modes:

- shipping a visually polished picker that still contains fake or misleading source semantics
- wiring new sources ad hoc without a clear frontend → Mojo → page-handler → submit-path contract

## Current verdict

### UI parity

Current picker parity is **PARTIAL**.

It is closer to the reference than the earlier implementation because it now:

- uses a dedicated `compact-context-picker` surface instead of reusing `compact-thread__welcome`
- uses a darker dropdown surface
- removes fake `Bookmarks` / `Files` placeholder rows
- promotes search to the top area of the picker

It is still not a true reference match because it visibly retains non-native chrome:

- `Add context` title
- explanatory subtitle text
- `Search history` placeholder instead of a true unified-search meaning
- `Start new session` footer action inside the picker

### Functional reality

The picker currently exposes three real sources and two unrealized ones.

#### Already real

- `Current page`
- `Open tabs`
- `History`

#### Not yet real

- `Bookmarks`
- `Files`

## Verified code facts

### Current picker UI

- File: `maho-chromium/browser/resources/maho_ai/react/features/compact/composer.tsx`
- Styles: `maho-chromium/browser/resources/maho_ai/styles/composer.css`

The live structure is:

- top search area
- `HistorySection`
- `Current page`
- `TabsSection`
- footer action `Start new session`

### Current real source wiring

#### Current page

- frontend toggles browser context via `onToggleBrowserContext`
- submit path handles `ContextSourceKind::kCurrentPage`
- page content is extracted by the existing page-context extractor

#### Open tabs

- `store.loadOpenTabs()` calls `pageHandler.getOpenTabs()`
- `MahoAIPageHandler::GetOpenTabs()` is implemented
- submit path handles `ContextSourceKind::kOpenTab`

#### History

- `store.searchHistory()` calls `pageHandler.searchHistory()`
- `MahoAIPageHandler::SearchHistory()` is implemented
- submit path handles `ContextSourceKind::kHistory`

### Bookmarks backend state

Bookmarks are not wired through the picker stack, but the lower backend is already present.

Existing pieces found:

- `maho/chromium/third_party/maho/maho_ffi.h`
  - `maho_core_search_bookmarks(...)`
  - `maho_core_get_all_bookmarks(...)`
- `maho/crates/maho-ffi/src/lib.rs`
  - bookmark FFI exports implemented
- `maho/crates/maho-core/src/maho_core.rs`
  - bookmark search methods implemented
- `maho/crates/maho-storage/src/sqlite.rs`
  - bookmark storage/query implementation exists

Missing pieces:

- no C++ bridge wrapper in `maho_bridge.h/.cc`
- no Mojo `SearchBookmarks(...)`
- no `MahoAIPageHandler::SearchBookmarks(...)`
- no frontend bookmark state/store/section
- no `SubmitPrompt` handling for `ContextSourceKind::kBookmark`

### Files backend state

Files are not wired at any meaningful layer.

What exists:

- enum placeholder `ContextSourceKind::kFile`
- frontend attachment kind includes `file`

What does not exist:

- no file picker flow
- no file content transport contract
- no submit-path implementation for `kFile`
- no frontend store/UI source section

## Scope for the next execution pass

### In scope

1. Make `Bookmarks` real in the picker
2. Make the top search input truthful as a unified source search for sources supported on this branch
3. Remove or relocate UI elements that still prevent reference-like dropdown parity
4. Keep `Current page`, `Open tabs`, and `History` fully intact

### Explicitly out of scope for the first pass

1. Local filesystem upload if it requires a new security-sensitive architecture decision
2. PDF/image/document ingestion pipeline design
3. Full runtime UX redesign outside the plus picker
4. Native-side panel frame work
5. Any GN/build-graph cleanup unrelated to the picker

## Product decision lock

The ambiguous source is `Files`.

This plan defines two possible interpretations, but only one should proceed without further product confirmation.

### Safe default

Treat the next implementation pass as:

- make `Bookmarks` real
- keep `Files` absent or explicitly deferred
- make the unified search truthful for `tabs + history + bookmarks`

### Do not assume without confirmation

Do **not** assume `Files` means arbitrary local file ingestion unless explicitly approved.

That path expands scope significantly because it requires:

- file picker UX and permissions decisions
- file content size/type limits
- transport of file content or file references through Mojo
- submit-path handling and safety review

## Goals

1. Eliminate any remaining mismatch between visible source options and real branch-backed functionality
2. Make the picker’s top search field semantically honest
3. Move the picker closer to the supplied native-style reference without inventing unsupported behavior
4. Preserve clean submit semantics for every supported source kind

## Non-goals

1. Shipping fake `Bookmarks` / `Files` rows for visual parity only
2. Conflating session-management actions with source selection if the reference does not support it
3. Expanding into large file-ingestion architecture work without a deliberate decision

## Implementation phases

## Phase 1 — Bookmarks end-to-end realization

### Objective

Add a real `Bookmarks` source that behaves like existing `History` in picker UX and like a real attachment source in submit.

### Files expected to change

- `maho-chromium/third_party/maho/maho_bridge.h`
- `maho-chromium/third_party/maho/maho_bridge.cc`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.h`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc`
- `maho-chromium/browser/resources/maho_ai/types.ts`
- `maho-chromium/browser/resources/maho_ai/store.ts`
- `maho-chromium/browser/resources/maho_ai/react/app.tsx`
- `maho-chromium/browser/resources/maho_ai/react/features/compact/composer.tsx`

### Required work

1. Add a C++ bridge wrapper for bookmarks search
2. Extend the Mojo contract with bookmark search/result types
3. Implement bookmark search in the page handler
4. Add bookmark state and handlers in the frontend store
5. Add bookmark attachment serialization in `getSubmitContextAttachments()`
6. Add `kBookmark` handling in `SubmitPrompt()`
7. Add a `BookmarksSection` to the picker UI

### Output contract

After this phase:

- bookmarks can be searched from the picker
- bookmarks can be selected as attachments
- bookmark attachments survive submit and reach the backend path truthfully

## Phase 2 — Unified search becomes real rather than cosmetic

### Objective

Make the top search field truthfully represent the sources it claims to search.

### Current issue

The current top search field visually reads like a picker-wide search but functionally only drives `historySearch`.

### Required work

1. Promote the search field to a shared picker query
2. Pipe that query into:
   - history search
   - bookmark search
3. Filter currently loaded tabs client-side using the same query
4. Update section labels and empty states to reflect shared search behavior
5. Change placeholder text to match the actual supported sources

### Implementation note

For v1, results should remain section-segmented instead of building a ranked cross-source list. That keeps scope smaller and semantics clearer.

### Output contract

After this phase, the user-facing meaning of the top input is truthful for:

- tabs
- history
- bookmarks

## Phase 3 — Reference-parity cleanup inside the picker

### Objective

Remove or relocate UI elements that still make the picker feel like an in-app card instead of a native dropdown.

### Highest-priority parity mismatches

1. explanatory header copy
2. misleading history-only search placeholder
3. session-management footer action inside the picker

### Required work

1. Remove or radically minimize `Add context` title and subtitle
2. Change the top search placeholder to reflect real unified search semantics
3. Remove `Start new session` from the picker footer, or move it to a different affordance outside the context picker
4. Flatten the picker chrome further if needed:
   - less decorative gradient layering
   - more native menu-like row hover/active behavior
   - ensure picker rows own their hover/focus contract instead of depending on borrowed menu classes where that creates styling leakage

### Output contract

After this phase, the picker should read visually as a compact source chooser, not a mini dialog.

## Phase 4 — Files decision and implementation (separate execution track)

### Objective

Define and implement `Files` only after product meaning is locked.

### Recommended split

#### Option A — defer from current picker parity pass

Recommended if the immediate goal is truthful parity for the existing source chooser.

#### Option B — implement local file upload as a dedicated follow-up

If explicitly approved, recommended first version:

- hidden `<input type="file">` in WebUI
- frontend `FileReader` ingestion for text-first formats
- extend `ContextAttachment` to carry file content or a safe reference
- add `ContextSourceKind::kFile` handling in submit path

### Warning

This is the only part of the source matrix that can expand scope by an order of magnitude if handled casually. It must remain a separate track.

## Verification plan

### For bookmarks + unified search

Required verification:

1. `lsp_diagnostics` on all changed frontend files
2. diagnostics/build verification on changed C++/Mojo files
3. `bun run typecheck` in `maho-chromium/browser/resources/maho_ai`
4. relevant Chromium build validation via canonical build wrapper
5. runtime QA:
   - open picker
   - search bookmarks
   - search history
   - filter tabs
   - attach each source type
   - submit and confirm attachments are reflected truthfully

### For UI parity

Runtime checks must explicitly verify:

- no fake sources remain visible
- top search meaning matches supported sources
- no dialog-like subtitle/header remains if removed by the implementation
- no `Start new session` footer remains if moved out
- dropdown silhouette is closer to the reference than the current partial state

## Execution order

Implement in this order.

1. Phase 1 — bookmarks backend/frontend realization
2. Phase 2 — true unified search semantics
3. Phase 3 — UI parity cleanup
4. verify end-to-end
5. only then decide whether `Files` enters scope

## Stop conditions

Stop and reassess if any of the following happens:

- `Files` work starts to require new security-sensitive architecture decisions
- unified search starts requiring ranked cross-source merge logic to remain usable
- the picker UI begins reintroducing fake semantics to satisfy screenshot parity

## Success criteria

This plan is complete only when all of the following are true:

1. `Current page`, `Open tabs`, `History`, and `Bookmarks` are all real and submit truthfully
2. the top picker search field truthfully searches all supported sources it implies
3. the current partial UI mismatches are removed or intentionally relocated
4. no fake `Files` affordance remains unless `Files` itself is truly implemented
5. diagnostics and typecheck/build verification are clean
