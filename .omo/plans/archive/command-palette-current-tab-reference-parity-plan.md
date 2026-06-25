# Cmd+L Current-Tab Open-State Reference Parity

## Goal

When the user presses Cmd+L (current-tab mode), the Chromium Views overlay must produce a first frame that matches the user's stated desired outcome. This plan defines the exact scope, evidence requirements, and **file-by-file implementation contract**.

This is NOT a general command palette plan. It targets one surface only: the zero-query open-state of `kCurrentTab` mode.

---

## Visual Parity Is In Scope

Every visual detail visible in the reference first frame is a shipping requirement. There is no category of "follow-up polish" for anything the user can see on the first paint.

Concretely, if the reference screenshot shows it, the implementation must match it before the work ships:

- **Title/subtitle typography hierarchy:** font size, weight, line-height, color, and relative sizing between title and subtitle text.
- **Row text layout (single-line inline):** evidence shows each row uses a compact `Title — domain` inline format on a single line. Not two separate lines for title and subtitle. The implementation must use this single-line treatment.
- **Selection fill, border, radius, and contrast:** the highlight on the selected row must match the reference's color, corner treatment, and contrast ratio.
- **Icon or favicon treatment:** whatever glyph or image appears at the leading edge of each row in the reference must appear in the implementation. If it's a favicon, the sync-cached version is used. If it's a fallback glyph, it must be the same glyph.
- **Row spacing and alignment:** vertical space between rows, horizontal padding, icon-to-text gap, and text baseline alignment.
- **Card height and absence of extraneous whitespace:** the overlay's total height must fit the content tightly. No dead space below the last row.
- **Row layout treatment (single-line vs. multi-line):** if the reference shows inline single-line `Title — domain` rows, the implementation must match. No two-line stacking unless the reference uses it.
- **Divider between input and results:** if the reference shows a hairline separator between the input field and the result list, match its weight, color, and vertical position. Evidence confirms NO dividers or separators between result rows themselves.

A visible mismatch in any of the above is a blocking defect. It does not ship as a later pass.

---

## Spec Lock for First Pass

The following is the exact acceptance bar for the first shipping implementation. Nothing less ships, and nothing beyond this list is silently added.

1. **Input field** contains the current page URL and the full text is selected.
2. **Row 1** is the current page. It is visibly selected (highlighted background, not merely model-selected).
3. **Rows 2-5** are populated with relevant prior pages from the current tab's session context (NavigationController back-stack), in reverse-chronological order, deduplicated by URL.
4. **Helper text** is completely absent. No "Type to search...", no "Search the web...", no hint of any kind.
5. **No inter-row dividers.** Evidence confirms the reference shows no separators or dividers between result rows. The list is a flat sequence with spacing only. (Input-to-results boundary treatment is a separate detail to be verified, but between rows: nothing.)
6. **Section headers** are absent. Evidence capture (G1) confirms the reference does not show them. Explicitly out of scope.
6a. **Single-line row layout.** Each row uses an inline `Title — domain` format on one line. No two-line stacking of title above subtitle.
7. **No visual flicker.** Seeded rows appear on the very first paint frame. There is no async replacement, no shimmer, no layout shift.
8. **Title/subtitle typography** matches the reference: correct font size, weight, and color for both title and subtitle lines in each row.
9. **Selection highlight** matches the reference: correct fill color, border radius, and contrast.
10. **Icon/favicon treatment** matches the reference: correct glyph or cached favicon at the leading edge of each row.
11. **Row spacing and alignment** match the reference: correct vertical rhythm, horizontal padding, icon-to-text gap.
12. **Card height** fits content tightly with no extraneous bottom whitespace.

If any of these bullets cannot be met, the implementation does not ship. The bullets above are non-negotiable and non-deferrable.

---

## What This Plan Will Not Decide During Implementation

The following decisions are explicitly forbidden from being silently resolved mid-implementation. Each requires a separate discussion, separate evidence, or a separate PR:

- **Section headers / group labels.** If the reference doesn't show them, they don't exist in first-pass. If evidence later shows they're needed, that's a follow-up scope expansion with its own plan.
- **Richer sources beyond current-tab session context.** Bookmarks, cross-tab URLs, history from other sessions, maho-core ranked data: none of these enter the first-pass result set.
- **Immediate-query replacement behavior.** The Swift-like pattern of prefilling the URL and then firing a live query that replaces seeded rows is NOT part of the first-pass implementation. It is a separate experiment branch (described below) and must not be quietly blended in.
- **Row icon sources beyond NavigationEntry.** If favicons require async loading that isn't already cached, the first-pass uses the synchronous fallback icon. Async favicon fetching is a follow-up.
- **Animation timing on open.** The first-pass target is correct static state, not motion design.

---

## Source of Truth Ordering (Binding)

This ordering is explicit and non-negotiable. When any implementation question arises, consult in this order:

| Priority | Source | Role | How to use |
|----------|--------|------|------------|
| **1 (Primary)** | User-provided desired runtime / reference first frame | The product owner's stated desired outcome | If the user describes what the open-state should look like, that description is the spec. All other sources serve it. |
| **2 (Secondary)** | User-provided reference image(s) | Visual ground truth for layout, spacing, row count | Screenshot or mockup provided by the user. If it conflicts with Swift behavior, the image wins. |
| **3 (Tertiary)** | Swift macOS shell behavior | Implementation reference for logic and mechanics | `CommandBarViewController.swift:658-667` (open path), `:238-259` (immediate query schedule), `SuggestionCellView.swift` (row layout). Used to understand HOW to achieve the spec, not to override WHAT the spec is. |
| **4 (Tiebreaker)** | Arc screenshots | Disambiguation for ambiguous details only | `docs/screenshots/arc/palette-tab-active.png`, `command-palette.jpeg`. Only consulted when sources 1-3 leave a detail genuinely unresolved. |

**Key constraint:** Swift is not the product. It is a prior implementation that partially achieved the desired outcome. Where Swift behavior diverges from the user's stated target, the user's target wins.

---

## Confirmed Current Facts from Code

### 1. Model layer returns nothing on empty query in kCurrentTab

`maho_command_model.cc:359-377`:
```cpp
if (query.empty()) {
    debounce_timer_.Stop();
    results_.clear();
    if (mode == CommandOverlayMode::kNewTab) {
        // Only kNewTab fires a backend search on empty query
        ...
        return;
    }
    callback.Run({});  // kCurrentTab hits this → empty results
    return;
}
```

The model does not contribute to the initial open-state. All visible content comes from the view-layer seed path.

### 2. View-layer seed path produces synchronous rows

`maho_command_overlay_view.cc:566-617`:
- `BuildCurrentTabSeedResults(browser, url)` constructs up to `kMaxSeededVisibleResults` (currently **8**) rows from the current page's navigation history.
- Row 0 = current page (URL + title from `WebContents::GetTitle()`).
- Rows 1..N = backward navigation entries, deduplicated by URL.

`maho_command_overlay_view.cc:619-631`:
- `InitializeCurrentTabSeedState(initial_text)` stores these seeds and calls `UpdateResultViews({})` to render them immediately.
- `is_seeded_state_` flag prevents helper text from appearing.

### 3. Seeded results render synchronously, first row selected

`maho_command_overlay_view.cc:855-889`:
- When `is_seeded_state_ && !seeded_results_.empty()`, the empty-results codepath renders seeded rows directly.
- Row 0 gets `SetSelected(true)`.
- No inter-row separators are inserted (evidence confirms none in reference).
- Helper/hint label is suppressed.
- Accessibility announces active-descendant.

### 4. Swift reference does more than seed

`CommandBarViewController.swift:658-667`:
```swift
case .currentTab:
    CoreUpdateDispatcher.shared.sendAndDispatch(.commandBarOpened)
    reloadTableViewClearingTransientSelection()
    updateEmptyState()
    notifyPreferredHeight()
    view.window?.makeFirstResponder(searchField)
    prefillCurrentTabURL()
    if !searchField.stringValue.isEmpty {
        scheduleQueryUpdate(for: searchField.stringValue, mode: mode)
    }
```

Swift prefills the URL, selects all, and then schedules an actual query for that non-empty text. The query returns real ranked results (history, bookmarks, etc.) after the debounce. The user sees seeded/prefilled content instantly, then results refine within ~150ms.

**Important:** This Swift behavior (the immediate-query-and-replace) is noted as context but is NOT part of the first-pass target. See the experiment branch section below.

### 5. Test coverage today

Unit tests (`maho_command_overlay_view_unittest.cc:360-535`) cover:
- Single seeded row renders synchronously
- Empty backend response preserves seed
- User edit clears seeded state
- Re-typing original URL does not re-enter seeded state
- Active descendant set on seed

**Gap**: All unit tests use a `ForTestingTag` constructor with no real `Browser*`. Only 1 seed row is tested (no navigation history). No test verifies 5-row open-state or visual properties.

Interactive tests (`maho_command_overlay_interactive_uitest.cc`) cover:
- Overlay opens via `SetFocusToLocationBar(true)`
- URL prefill + select-all contract
- Escape dismisses
- Repeated open/hide cycles
- Window deactivation hides overlay
- Feature flag intercept
- Navigation-history seeding with >=2 entries (verifies seeded mechanics with real Browser)

**Gap**: No interactive test currently asserts an exact 5-row layout with title content verification for each row.

### 6. What existing tests prove vs. what they don't

Existing tests prove the seeded-open mechanics work correctly: rows render synchronously, selection is applied, helper text is suppressed, the state machine transitions are correct.

Existing tests do NOT prove visual parity with the reference. Typography, spacing, icon presence, highlight color, and exact row count matching the reference remain unverified until post-implementation screenshot comparison.

---

## Scope

| In scope | Description |
|----------|-------------|
| kCurrentTab open-state | First paint after Cmd+L when a page is loaded |
| Seeded rows content | Current page + 4 prior navigation entries (5 visible rows, confirmed by evidence) |
| Input field state | URL prefilled, fully selected, cursor at end |
| Single-line row layout | Each row renders as inline `Title — domain` on one line (evidence-confirmed) |
| Visual parity (all first-frame details) | Typography, colors, spacing, icons, selection highlight, card height must match reference |
| Row selection | Row 0 visually selected with solid blue/indigo pill highlight |
| Result count | 5 rows when history exists; fewer only when history is shorter |
| Flat list, no separators | No dividers between rows, no section headers (evidence-confirmed) |
| Full-color favicons | Sync-cached favicons at leading edge of each row (evidence-confirmed, G2) |
| Textfield placeholder | Empty (URL is prefilled content, not placeholder) |

## Out of Scope

- Ranking algorithm changes for typed queries
- New-tab mode open-state
- AI mode open-state
- WebUI command palette surface
- Click-path unification (already shipped)
- Animation timing on open/close
- Section headers / group labels (G1 resolved: NO, confirmed absent in reference)
- Site-search mode prefill behavior
- Full omnibox replacement strategy
- Immediate-query behavior (see experiment branch, explicitly separate)
- Cross-tab or cross-session history sources
- Bookmark inclusion in open-state

---

## Acceptance Criteria

All criteria must be verifiable by runtime observation or screenshot comparison. No abstract criteria.

| # | Criterion | Verification method |
|---|-----------|-------------------|
| 1 | Pressing Cmd+L with a page loaded shows the overlay with the page URL prefilled and fully selected | Visual inspection + interactive test assert |
| 2 | The result list shows exactly the current page as row 1, visually selected (correct highlight color, radius, contrast per reference) | Screenshot comparison, interactive test with >=5 nav entries |
| 3 | Rows 2-5 are populated with the most recent distinct navigation entries from the current tab's back-stack | Interactive test asserts row count and content |
| 4 | No helper text is visible | Unit test + screenshot |
| 5 | No visible dividers or separators between result rows (flat list with spacing only) | Screenshot comparison — no hairlines between rows |
| 6 | The overlay height fits content tightly (no empty whitespace gap at bottom) | Screenshot measurement |
| 7 | Side-by-side screenshot against user-provided reference shows full visual parity for all first-frame details: row count, selection state, no inter-row dividers, typography, colors, icons, spacing, card height | Manual screenshot diff |
| 8 | Typing anything clears the seeded state and triggers a real backend query | Existing unit test (already passes) |
| 9 | Pressing Enter on the seeded URL navigates to it (no-op reload) | Interactive test |
| 10 | Title/subtitle in each row uses correct font size, weight, and color per reference | Screenshot comparison |
| 11 | Row icons/favicons match reference treatment (cached favicon or correct fallback glyph) | Screenshot comparison |
| 12 | Row spacing, padding, and icon-to-text alignment match reference | Screenshot comparison |

**Criterion 7 is the primary visual gate.** It requires that every detail visible in the reference first frame is matched. There is no "structural match only" tier. If a delta is visible, it blocks shipping.

---

## Required Pre-Implementation Evidence Capture

Before writing any implementation code, capture and document:

### Step 1: Fresh screenshots

1. Launch Swift macOS shell. Navigate to a page with >=5 back-stack entries. Press Cmd+L.
2. Screenshot the open-state at the exact frame the overlay appears.
3. Launch Chromium build. Same page, same history. Press Cmd+L.
4. Screenshot the open-state.

### Step 2: Numbered delta list

Create a delta document (`docs/cmd-l-open-state-delta.md`) with numbered observations:

```
1. [Row count] Reference shows N rows; Chromium shows M rows.
2. [Row 1 content] Reference: title + URL; Chromium: URL only (or title + URL).
3. [Selection highlight] Reference: fill #RRGGBB, radius Npx; Chromium: fill #RRGGBB, radius Npx.
4. [Section header] Reference: present/absent — record which.
5. [Divider] Reference: thickness Npx, color #RRGGBB, position Npx below input; Chromium: same measurements.
6. [Input text state] Reference: fully selected; Chromium: fully selected (verify).
7. [Row typography] Title: font size, weight, color. Subtitle: font size, weight, color. Measure both.
8. [Row icons] Favicon present? Globe icon? Document icon? Size? Leading padding?
9. [Row spacing] Row height px, vertical gap between rows, icon-to-text gap px.
10. [Card height] Total overlay height px, any bottom padding px.
```

Each delta item becomes a blocking defect that must be resolved before shipping.

### Step 3: Decision on section headers

If evidence capture shows that the reference does NOT display section headers/group labels in current-tab open-state, they are explicitly out of scope. If it does, add them to scope before implementation, with a separate mini-plan.

**Implementation must not begin until this evidence document exists and is reviewed.**

---

## File-by-File Execution Plan

This is the core implementation contract. Each file section tells the implementer exactly what to inspect, what to change, what to leave alone, and how to verify. Work proceeds in the cluster order defined in the Sequencing section.

---

### FILE: `maho_command_overlay_view.h`

**Why in scope:** Owns the member state (`seeded_results_`, `is_seeded_state_`, `initial_url_`) and declares all public/testable seams that the open-state flows through. Any behavioral change in the view layer touches this header for declarations.

**Exact members/functions involved:**

| Symbol | Role | Expected change |
|--------|------|-----------------|
| `seeded_results_` | Stores seed rows built by `BuildCurrentTabSeedResults` | No change to type. Verify it holds >=5 entries. |
| `is_seeded_state_` | Gate flag for seeded rendering path | No change. |
| `initial_url_` | Stores the URL used to detect "user typed the same thing back" | No change. |
| `InitializeCurrentTabSeedState()` | Public entry point, called at overlay open | No signature change. |
| `ShouldClearSeededStateForText()` | Determines whether typed text exits seeded state | No change. |
| `UpdateResultViews()` | Renders seeded rows on first frame | No signature change. |
| `TriggerSearchForCurrentText()` | Called when user edits text; transitions away from seeded | **Must NOT be called in first-pass open path.** |
| `BuildCurrentTabSeedResults()` | The seed-builder seam | May gain a constant adjustment (kMaxSeededVisibleResults). |
| `GetRenderedResultCountForTesting()` | Test accessor for row count | Verify it already returns correct count for multi-row seeds. If not, expose `GetSeededResultCountForTesting()`. |
| `kMaxSeededVisibleResults` | Currently 8 | May be lowered to 5 if evidence shows 5 is the reference target. Expose in header if tests reference it. |

**Expected change type:** Declarative (constant adjustment, possibly one new test accessor). No behavioral logic changes in the header.

**What must NOT change here:**
- Do not add a `TriggerSearchForCurrentText()` call to any initialization path.
- Do not add `CommandOverlayMode` branching that would route kCurrentTab through the model.
- Do not add section/group fields to seed results.

**Verification:**
- `lsp_diagnostics` clean.
- Downstream `.cc` compiles without new warnings.
- No new public API surface beyond what tests need.

---

### FILE: `maho_command_overlay_view.cc`

**Why in scope:** Contains ALL runtime logic for the seeded open-state. This is the primary implementation file. Every behavioral change for the first-pass lives here.

**Exact functions involved:**

| Function | Role | Expected change |
|----------|------|-----------------|
| `BuildCurrentTabSeedResults()` | Constructs seed rows from NavigationController | **Primary seam.** Verify loop reaches 5 entries. Possibly adjust `kMaxSeededVisibleResults` constant. Verify deduplication works across >=5 entries. Verify title comes from `NavigationEntry::GetTitle()` not URL. |
| `InitializeCurrentTabSeedState()` | Stores seeds, calls `UpdateResultViews({})` | **Open-state entry point.** Verify it does NOT call `TriggerSearchForCurrentText()`. Verify `is_seeded_state_` is set before render. No logic change expected — this is a verification-only seam. |
| `UpdateResultViews()` seeded branch | When `is_seeded_state_ && !seeded_results_.empty()`, renders rows | **First-frame render seam.** Verify it creates exactly `seeded_results_.size()` row views. Verify `SetSelected(true)` on row 0. Verify no inter-row dividers are inserted. Verify hint/helper label hidden. May need adjustment if row creation doesn't propagate favicon/icon from seed data. |
| `ContentsChanged()` | Fires on any text edit | Verify it calls `ShouldClearSeededStateForText()` and only clears state when text ≠ `initial_url_`. **No change expected** — verify only. |
| `TriggerSearchForCurrentText()` | Dispatches real query to model | **Must NOT be called during open-state initialization in first-pass.** Verify no call path from `InitializeCurrentTabSeedState()` reaches here. |
| `ResizeWidgetToContents()` | Calculates overlay height | Verify tight fit for input + spacing + 5 single-line rows. If bottom padding creates a gap, adjust padding constant. |

**Expected change type:** Behavioral — constant adjustment, possibly loop verification, possibly icon propagation to row view. Small and contained.

**Implementation checklist for this file:**

1. Trace `BuildCurrentTabSeedResults()`:
   - Does the backward loop (`current_index - 1` downward) iterate enough? With 6+ nav entries, does it produce 5 seeds after dedup?
   - Is the loop capped by `kMaxSeededVisibleResults`? If cap is 8 and target is 5, lower to 5.
   - Does each seed row carry: title (from `GetTitle()`), URL (from `GetURL()`), type `kNavigation`?
   - Does row 0 (current page) use `WebContents::GetTitle()` or `NavigationEntry::GetTitle()`? Verify which matches reference.

2. Trace `UpdateResultViews()` seeded branch:
   - For each seed, is a `MahoCommandResultRowView` created with correct title/subtitle/icon?
   - Is the icon propagated from `CommandSuggestion` to the row view? If the suggestion carries a favicon URL, does the row view use it?
   - Is `SetSelected(true)` called on index 0 only?

3. Verify `ResizeWidgetToContents()`:
   - After rendering 5 rows, measure total height = input_height + spacing + (5 × single_line_row_height).
   - If there's bottom padding, verify it matches reference (or remove it).

**What must NOT change here:**
- Do not call `TriggerSearchForCurrentText()` from `InitializeCurrentTabSeedState()`.
- Do not add model-layer queries (`Search()`) to the open path.
- Do not add debounce/timer logic for the seeded state.
- Do not introduce section headers or group labels into the seed result list.
- Do not change the `ContentsChanged()` → clear-seeded-state flow.

**Verification:**
- Unit tests (multi-row seed) pass.
- Interactive test (5 rows with real Browser) passes.
- Runtime: open overlay with 6+ nav entries → exactly 5 rows visible, no flicker, no helper text.

---

### FILE: `maho_command_model.h`

**Why in scope:** Documents that the model layer is intentionally NOT touched for the first-pass mainline path. The implementer must understand why this file stays unchanged.

**Exact symbols relevant:**

| Symbol | Relevance |
|--------|-----------|
| `CommandSuggestion` struct | Currently has no section/group field. **Do not add one.** |
| `Search()` | Returns `{}` for empty-query kCurrentTab. **Do not modify.** |
| `SearchWithPrefill()` | Does not exist. **Do not create it in first-pass.** |

**Expected change type:** **None.** This file is documented here to explicitly mark it as off-limits.

**Why it stays unchanged:**
- The seeded open-state is fully handled in the view layer.
- The model's empty-query-returns-`{}` behavior is correct and desired — it means the view's seed is the only content source.
- Modifying `Search()` to return results for empty kCurrentTab queries would break the seeded determinism (Spec Lock bullet 7).

**What must NOT change here:**
- No new methods.
- No new fields on `CommandSuggestion`.
- No changes to `Search()` dispatch logic.

**Verification:**
- `git diff` shows zero changes to this file after first-pass implementation.

---

### FILE: `maho_command_model.cc`

**Why in scope:** Same as header — explicitly documented as unchanged. The empty-query kCurrentTab path returning `{}` is a precondition that the seeded view relies on.

**Exact code path relevant:**

```cpp
// Line ~367-377
if (query.empty()) {
    ...
    callback.Run({});  // kCurrentTab: returns empty, view uses seeds
    return;
}
```

**Expected change type:** **None.**

**When this file WOULD change (experiment branch only):**
- If the experiment branch (immediate-query) is pursued in a separate PR, `Search()` would need to handle "query = full URL" as a special case for kCurrentTab mode.
- That change would add logic like: if mode == kCurrentTab && query looks like a URL, search history/bookmarks for it.
- **This is NOT first-pass work.**

**What must NOT change here:**
- No changes to empty-query path.
- No new search modes.
- No URL-specific query handling.

**Verification:**
- `git diff` shows zero changes to this file after first-pass implementation.

---

### FILE: `maho_command_result_row_view.h`

**Why in scope:** Defines the visual structure of each result row. Every presentation delta between current Chromium and the reference (icon, title/subtitle hierarchy, selection highlight, spacing) is a blocking defect that gets fixed here.

**Exact members/functions involved:**

| Symbol | Role | Expected change |
|--------|------|-----------------|
| `SetSelected(bool)` | Applies highlight background | Must produce solid blue/indigo pill fill, with radius and height matching single-line row per reference. |
| Title/domain label(s) | Primary text content | Must switch to single-line inline format: `Title — domain`. May be one combined label or two inline labels. |
| Icon view | Left-side glyph | Must show full-color sync-cached favicon. Fallback glyph only on cache miss. |
| `SetSuggestion(CommandSuggestion)` | Populates row from data | Must read title, URL, icon from suggestion and format as single-line inline layout. |

**Expected change type:** Visual — color constants, font metrics, icon selection logic, spacing constants. All changes are blocking requirements, not optional polish.

**What must NOT change here:**
- Do not add section header rendering logic.
- Do not add group/category visual treatment.
- Do not add async favicon loading (sync cache only for first-pass).
- Do not change the selection model (single-selection semantics).

**Verification:**
- Screenshot comparison: selected row 0 matches reference highlight exactly (color, radius, contrast).
- Screenshot comparison: title/subtitle font sizes, weights, and colors match reference.
- Screenshot comparison: icon type, size, and leading padding match reference.
- Screenshot comparison: row height, spacing, and alignment match reference.
- Unit test (if row-level tests exist) still passes.

---

### FILE: `maho_command_result_row_view.cc`

**Why in scope:** Implementation of all row-level rendering. This is where all visual parity fixes land. Every delta found in evidence capture that relates to row appearance is resolved here.

**Exact areas to inspect/change:**

| Area | What to check | Required change |
|------|---------------|-----------------|
| Row layout structure | Currently two-line (title label + subtitle label stacked vertically) | **Convert to single-line inline:** one label with `Title — domain` text, or two labels side-by-side on the same line with a separator |
| `SetSelected()` implementation | Background color value, corner radius | Match reference: solid blue/indigo pill fill, tight height for single-line row |
| Title/domain label setup | Font: family, size dp, weight, color for inline format | Title portion: regular weight; domain portion: lighter/smaller or muted color; separated by ` — ` or equivalent |
| Icon setup | Which icon is used for `kNavigation` type suggestions | Use sync-cached favicon (full-color). Fallback to glyph only on cache miss. |
| Layout constants | Row height, horizontal padding, icon-to-text spacing | Reduce row height to single-line; match reference density |
| Inter-row separators | Check if any divider views exist between rows | Remove if present. Evidence confirms none in reference. |

**Expected change type:** Visual constants and possibly icon selection logic. Every change is gated by evidence capture. No change is speculative, but every measured delta is blocking.

**Implementation checklist for this file:**

1. After evidence capture (DONE), convert row layout:
   - Replace two-line stacked layout with single-line inline format.
   - Title and domain appear on the same line: `Page Title — example.com`
   - The separator between title and domain should be a muted dash or similar.
   - Domain portion uses lighter color/opacity than title.

2. Fix selection highlight:
   - Solid blue/indigo pill fill (match reference color exactly).
   - Pill radius (fully rounded or high corner radius).
   - Vertical extent: tight around single-line content, not tall two-line block.

3. Fix icon to use sync favicon:
   - In `SetSuggestion()`, check if `suggestion.url` has a cached favicon via `FaviconService::GetRawFaviconForPageURL()` with `CACHE_ONLY` flag.
   - If cache hit: use full-color favicon. If cache miss: use fallback glyph.
   - **Do not** trigger async favicon fetch.

4. Remove any inter-row dividers/separators if present. Evidence confirms none in reference.

5. Adjust spacing constants for single-line density:
   - Row height reduced from two-line to single-line.
   - Horizontal padding and icon-to-text gap matched to reference.

**What must NOT change here:**
- Do not add section header row type.
- Do not add animation/transition on selection change.
- Do not change row click/activation semantics.
- Do not add async operations.

**Verification:**
- Side-by-side screenshot: every row detail matches reference (per evidence capture delta list). All deltas resolved.
- Interactive test: rows are visually distinguishable (selected vs unselected).
- No new layout warnings or clipping at runtime.

---

### FILE: `maho_command_overlay_view_unittest.cc`

**Why in scope:** Proves seeded mechanics in isolation (no real Browser). Currently tests 1-row seed. Must be extended to prove multi-row seed and content ordering.

**Current coverage (preserve all):**
- `CurrentTabSeedResultRendersSynchronously` — 1 seed row appears immediately
- `CurrentTabEmptyBackendResponsePreservesSeed` — backend `{}` doesn't clear seeds
- `CurrentTabUserEditClearsSeededState` — typing clears `is_seeded_state_`
- `CurrentTabRetypingUrlDoesNotReenterSeededState` — state machine correctness
- `CurrentTabSeedSetsActiveDescendant` — a11y contract

**New tests to add:**

| Test name | What it proves | Setup |
|-----------|---------------|-------|
| `CurrentTabSeedProducesMultipleRows` | >=5 rows render when NavigationController has >=6 entries | Mock NavigationController with 6+ entries; call `BuildCurrentTabSeedResults()`; assert `seeded_results_.size() >= 5` |
| `CurrentTabSeedRowOrderIsReverseChronological` | Row 0 = current, row 1 = most recent back entry, row N = oldest | Assert `seeded_results_[0].url == current_url` and `seeded_results_[1].url == nav_entry[-1].url` etc. |
| `CurrentTabSeedDeduplicatesByUrl` | Duplicate URLs in back-stack produce only one row | Add same URL at multiple positions; verify row count < entry count |
| `CurrentTabSeedRowTitleComesFromNavigationEntry` | Title field is `GetTitle()`, not URL | Assert `seeded_results_[1].title == expected_title` (non-URL string) |
| `CurrentTabSeedNoHelperTextWithMultipleRows` | Helper/hint label hidden when 5 seeds present | After `InitializeCurrentTabSeedState()` with 5 seeds, assert helper label not visible |

**What NOT to test here:**
- Do not test favicon loading — unit test has no favicon service.
- Do not test immediate-query behavior — that's experiment branch.
- Do not test model-layer `Search()` — it's intentionally not called.
- Do not test keyboard navigation between rows — that's interactive test territory.

**What must NOT change in existing tests:**
- Do not modify existing test expectations. They test correct behavior.
- Do not change the `ForTestingTag` constructor interface unless absolutely necessary.

**Verification:**
- `bun run test` passes with all new + existing tests green.
- No flaky timing dependencies (all seeded tests are synchronous).

---

### FILE: `maho_command_overlay_interactive_uitest.cc`

**Why in scope:** Proves runtime behavior with a real `Browser*`, real `NavigationController`, real rendering pipeline. This is where the 5-row open-state and row content correctness are proven at runtime quality.

**Current coverage (relevant subset):**
- `FocusPath_CurrentTabSeedUsesNavigationHistory` — already navigates to multiple pages and verifies >=2 seed rows with real Browser.

**New tests to add:**

| Test name | What it proves | Setup |
|-----------|---------------|-------|
| `CurrentTabSeed_FiveRowsWithRealHistory` | Opening overlay after 6+ navigations shows exactly 5 rows | Navigate to 6 distinct pages sequentially; open overlay; assert rendered row count == 5 |
| `CurrentTabSeed_RowContentMatchesNavigation` | Row titles and URLs correspond to actual navigation entries | After opening overlay with history, read row 0 title (== current page title), row 1 title (== previous page title), etc. |
| `CurrentTabSeed_EnterOnRow0NavigatesToCurrentUrl` | Pressing Enter on the selected seed row loads current URL | Open overlay → press Enter → verify URL didn't change (no-op navigation to same page) |
| `CurrentTabSeed_ArrowDownMovesSelection` | Down-arrow changes selected row from 0 to 1 | Open overlay → press Down → verify selection index == 1 |
| `CurrentTabSeed_NoFlickerOnOpen` | Seeded rows are present on first paint, not replaced | Open overlay → immediately read row count → assert == 5 (no wait needed) |

**What NOT to test here:**
- Do not test model-layer behavior — it's not involved in seeded open.
- Do not test the experiment branch (immediate-query-then-replace).
- Do not test cross-tab or bookmark sources.

**What must NOT change in existing tests:**
- `FocusPath_CurrentTabSeedUsesNavigationHistory` must continue to pass. It may be the base for new assertions but should not have its existing checks weakened.

**Verification:**
- Interactive test suite passes end-to-end (`maho_command_overlay_interactive_uitest` target).
- No flaky failures from timing (seeded tests should be deterministic — no waits for async results).

---

### FILE: Swift reference (read-only)

`maho/macos-shell/Sources/CommandBar/CommandBarViewController.swift` — read-only. Used to understand mechanics (how Swift builds the open-state), not to define the product spec. No modifications.

`maho/macos-shell/Sources/CommandBar/SuggestionCellView.swift` — read-only. Used to compare row layout (icon, title, subtitle positions) if evidence capture needs clarification.

---

## Implementation Sequencing by File Cluster

Work proceeds in this order. Each cluster builds on the previous one's output. Do not skip ahead.

### Cluster 0: Evidence Capture — COMPLETE

**Files produced:** `docs/cmd-l-open-state-delta.md`  
**Gates resolved:** G1 (NO section headers), G2 (YES favicons), G3 (cap = 5), G4 (height needs adjustment).

All gates answered. Implementation clusters 1-3 are unblocked.

---

### Cluster 1: Seed Builder / Open-State (`overlay_view.h`, `overlay_view.cc`) (~2 hours)

**Goal:** The seeded open-state produces 5 correct rows with correct content, renders synchronously, selects row 0, and sizes the overlay correctly for 5 single-line rows.

| Step | File | Action |
|------|------|--------|
| 1.1 | `overlay_view.h` | Change `kMaxSeededVisibleResults` from 8 to 5 (confirmed by evidence, G3 resolved). |
| 1.2 | `overlay_view.cc` | In `BuildCurrentTabSeedResults()`: verify backward loop reaches 5 entries after dedup. Add diagnostic logging (temporary) to confirm at runtime. |
| 1.3 | `overlay_view.cc` | In `BuildCurrentTabSeedResults()`: verify each seed carries title from `NavigationEntry::GetTitle()` (not URL-as-title fallback). |
| 1.4 | `overlay_view.cc` | In `UpdateResultViews()` seeded branch: verify all `seeded_results_.size()` rows are created, not just 1. |
| 1.5 | `overlay_view.cc` | In `ResizeWidgetToContents()`: adjust for 5 single-line rows (evidence shows current card is much too short). Tight fit, no bottom gap. |
| 1.6 | `overlay_view.cc` | Verify `InitializeCurrentTabSeedState()` does NOT call `TriggerSearchForCurrentText()`. (Read-only verification.) |

**Verification for this cluster:**
- Build Chromium. Navigate to 6+ pages. Press Cmd+L. Visually confirm 5 rows, no helper text, no inter-row dividers, row 0 selected.
- No model-layer changes have been made.

---

### Cluster 2: Row Renderer (`result_row_view.h`, `result_row_view.cc`) — REQUIRED (~3 hours)

**Goal:** Each row's visual presentation matches the reference for every detail visible in the first frame. The primary structural change is converting from two-line stacked layout to single-line inline `Title — domain` format. This cluster is mandatory, not conditional cleanup.

| Step | File | Action | Blocking? |
|------|------|--------|-----------|
| 2.1 | `result_row_view.cc` | Convert row layout from two-line (title above, subtitle below) to single-line inline `Title — domain` | YES — fundamental layout mismatch with reference |
| 2.2 | `result_row_view.cc` | Adjust selection highlight to solid blue/indigo pill sized to single-line row height | YES — blocks shipping |
| 2.3 | `result_row_view.cc` | Adjust title and domain typography for inline single-line treatment | YES — blocks shipping |
| 2.4 | `result_row_view.cc` | Add sync favicon cache lookup for kNavigation rows (G2 resolved YES) | YES — reference shows full-color favicons |
| 2.5 | `result_row_view.cc` | Adjust row height, horizontal padding, icon-to-text spacing for single-line density | YES — blocks shipping |
| 2.6 | `result_row_view.cc` | Remove any inter-row divider/separator rendering if present (evidence: none in reference) | YES — no dividers between rows |
| 2.7 | `result_row_view.h` | Expose any new constants needed for tests | Only if 2.1-2.6 introduced named constants |

**Verification for this cluster:**
- Screenshot of Chromium open-state. Compare each row against reference delta list.
- All delta items from Cluster 0 evidence capture that concern row visuals are resolved. Zero remaining visual deltas for first-frame details.

**This cluster must be complete before Cluster 4 (screenshot sign-off) can begin.**

---

### Cluster 3: Test Cluster (`*_unittest.cc`, `*_interactive_uitest.cc`) (~2 hours)

**Goal:** Automated proof that the 5-row seeded open-state is correct and stable.

| Step | File | Action |
|------|------|--------|
| 3.1 | `_unittest.cc` | Add `CurrentTabSeedProducesMultipleRows` test |
| 3.2 | `_unittest.cc` | Add `CurrentTabSeedRowOrderIsReverseChronological` test |
| 3.3 | `_unittest.cc` | Add `CurrentTabSeedDeduplicatesByUrl` test |
| 3.4 | `_unittest.cc` | Add `CurrentTabSeedRowTitleComesFromNavigationEntry` test |
| 3.5 | `_unittest.cc` | Add `CurrentTabSeedNoHelperTextWithMultipleRows` test |
| 3.6 | `_interactive_uitest.cc` | Add `CurrentTabSeed_FiveRowsWithRealHistory` test |
| 3.7 | `_interactive_uitest.cc` | Add `CurrentTabSeed_RowContentMatchesNavigation` test |
| 3.8 | `_interactive_uitest.cc` | Add `CurrentTabSeed_EnterOnRow0NavigatesToCurrentUrl` test |
| 3.9 | `_interactive_uitest.cc` | Add `CurrentTabSeed_ArrowDownMovesSelection` test |

**Verification for this cluster:**
- `bun run test` (unit tests) all pass.
- Interactive test target passes locally.
- No flaky failures on 3 consecutive runs.

---

### Cluster 4: Final Screenshot Comparison (~30 min)

**Prerequisites:** Clusters 1, 2, and 3 must all be complete. Cluster 2 is not optional.

**Goal:** Visual sign-off confirming full first-frame parity.

| Step | Action |
|------|--------|
| 4.1 | Rebuild after all changes. |
| 4.2 | Screenshot Chromium open-state with 6+ history entries. |
| 4.3 | Place side-by-side with reference screenshot from Cluster 0. |
| 4.4 | Verify all Spec Lock bullets (1-12) are met. |
| 4.5 | If any visible first-frame delta remains, return to Cluster 2 and fix it. Do not ship with visible deltas. |

**This cluster does not produce a "remaining polish" list. It either passes (zero visible deltas) or the work returns to Cluster 2.**

---

### Cluster 5 (Optional, Separate PR): Experiment Branch (`command_model.h/.cc`) (~3 hours)

**This cluster is NOT part of first-pass. It ships separately, if ever.**

**Gating criteria (ALL must be true):**
1. First-pass seeded implementation has shipped and is stable.
2. Evidence shows reference includes results that CANNOT come from NavigationController alone.
3. OR product owner explicitly requests richer open-state.

**If pursued:**

| Step | File | Action |
|------|------|--------|
| 5.1 | `overlay_view.cc` | In `InitializeCurrentTabSeedState()`, after seed render, call `TriggerSearchForCurrentText()` |
| 5.2 | `command_model.cc` | Handle "query = full URL" in kCurrentTab mode: search history/bookmarks |
| 5.3 | `command_model.h` | Possibly add `CommandSuggestion` group/section field if results need categorization |
| 5.4 | `overlay_view.cc` | In result callback, if new results match seed order, skip re-render (flicker prevention) |
| 5.5 | `_unittest.cc` | Test that immediate-query results replace seeds without flicker |
| 5.6 | `_interactive_uitest.cc` | Test that open-state stabilizes within 200ms |

---

## File-Level Risk Notes

These are the most likely failure modes during implementation. Each is tied to a specific file and a specific mistake.

### Risk 1: Accidentally restoring seeded state after edit

**File:** `maho_command_overlay_view.cc` — `ContentsChanged()` / `ShouldClearSeededStateForText()`

**Failure mode:** Implementer changes `ShouldClearSeededStateForText()` logic (e.g., to handle multi-row state) and inadvertently allows the seeded state to be re-entered after the user edits text and then re-types the original URL.

**Why it's dangerous:** The existing test `CurrentTabRetypingUrlDoesNotReenterSeededState` catches this, but if that test is modified to accommodate new behavior, the regression is silent.

**Mitigation:** Do not modify `ShouldClearSeededStateForText()` in first-pass. The one-way state machine (seeded → not-seeded, never back) is correct.

---

### Risk 2: Calling `TriggerSearchForCurrentText()` in mainline open path

**File:** `maho_command_overlay_view.cc` — `InitializeCurrentTabSeedState()`

**Failure mode:** Implementer adds a query dispatch call "for completeness" or "to match Swift" during the open-state initialization. This introduces the async round-trip and violates Spec Lock bullet 7 (no flicker).

**Why it's dangerous:** The code "works" — seeded rows render, then get replaced. It looks correct at low latency. But it's the experiment branch behavior injected into mainline.

**Mitigation:** Code review must reject any call to `TriggerSearchForCurrentText()` or `Search()` from within the open-state initialization path. The test `CurrentTabSeed_NoFlickerOnOpen` should catch this if result replacement produces a layout change.

---

### Risk 3: Changing `Search()` in the model and muddying ownership

**File:** `maho_command_model.cc` — `Search()` empty-query kCurrentTab branch

**Failure mode:** Implementer makes `Search()` return non-empty results for kCurrentTab empty query, thinking "the model should provide the data." This breaks the view-layer seed ownership and makes the open-state dependent on async model responses.

**Why it's dangerous:** Subtle — the view still renders seeds first, but now the model also fires results that arrive later and replace them. The flicker may be invisible locally (fast machine) but appear in CI or on slower hardware.

**Mitigation:** `git diff maho_command_model.cc` must show zero changes. If the implementer wants model-layer data in the open-state, that's the experiment branch (Cluster 5, separate PR).

---

### Risk 4: Coupling screenshot parity fixes to broad ranking or typing behavior

**File:** `maho_command_result_row_view.cc` or `maho_command_overlay_view.cc`

**Failure mode:** While fixing visual parity (font, color, icon), the implementer notices that typed-query results also look "wrong" and fixes those too. This broadens scope beyond kCurrentTab open-state.

**Why it's dangerous:** Changes to row rendering affect ALL modes (kNewTab, AI, search). A typography fix for seeded rows accidentally changes typed-query result appearance.

**Mitigation:** If row visual changes must be scoped to seeded-state only, introduce a flag/style variant. But strongly prefer: if the visual change is correct for all rows, it's fine. The key rule is: do not change ranking, content, or behavior for typed queries.

---

### Risk 5: Adding section/group fields or headers before evidence proves them

**File:** `maho_command_model.h` (section field), `maho_command_overlay_view.cc` (header rendering)

**Failure mode:** Implementer "anticipates" that section headers will be needed (because Swift has them) and adds the field/rendering preemptively.

**Why it's dangerous:** Adds dead code, increases review surface, and creates an attractive nuisance for future implementers who activate headers without the product decision.

**Mitigation:** Gate G1 must be explicitly resolved as NO before implementation begins. If YES, stop and create a separate plan. No anticipatory infrastructure.

---

### Risk 6: `kMaxSeededVisibleResults` changed without corresponding test update

**File:** `maho_command_overlay_view.h` (constant), `_unittest.cc` (assertions)

**Failure mode:** Implementer lowers the constant from 8 to 5 but existing tests assert `GetRenderedResultCountForTesting() >= 1` — they still pass but don't prove the new constraint.

**Why it's dangerous:** The test suite looks green but doesn't actually prove the 5-row contract.

**Mitigation:** New unit test `CurrentTabSeedProducesMultipleRows` must assert `== 5` (not `>= 1`) when given sufficient history. The constant change and the test must land in the same commit.

---

### Risk 7: `ResizeWidgetToContents()` height incorrect for 5 rows

**File:** `maho_command_overlay_view.cc`

**Failure mode:** The height calculation has a hardcoded assumption (e.g., designed for 1-3 rows) and produces too much or too little space for 5 rows.

**Why it's dangerous:** Visual — the overlay either clips the 5th row or has a large empty gap at the bottom. Both fail the screenshot comparison but might not fail automated tests (which don't measure pixel height).

**Mitigation:** After Cluster 1 implementation, visually verify height at runtime before proceeding. If incorrect, fix immediately in Cluster 1 (not deferred to Cluster 4).

---

## Decision Gates

| Gate | Question | Blocks | Resolution |
|------|----------|--------|------------|
| G1 | Does the reference show section headers in kCurrentTab open-state? | Implementation of section headers | **RESOLVED: NO.** Evidence capture confirms no section headers. Explicitly out of scope. |
| G2 | Does the reference show favicons per row (synchronously available)? | `MahoCommandResultRowView` changes | **RESOLVED: YES.** Reference shows full-color favicons. Sync cache lookup required; fallback glyph for cache misses. |
| G3 | Is `kMaxSeededVisibleResults = 8` the right cap? | Constant change | **RESOLVED: NO.** Reference shows 5 rows. Lower constant to 5. |
| G4 | Does the existing `ResizeWidgetToContents()` produce correct height for 5 rows? | Layout fix needed? | **RESOLVED: LIKELY NO.** Current Chromium shows much shorter card. After single-line conversion + 5-row target, height must be verified and adjusted. |

---

## Evidence Capture Status (Cluster 0)

**Status: COMPLETE.** See `docs/cmd-l-open-state-delta.md` for the full numbered delta document.

### Key findings that override prior assumptions:

1. **No inter-row dividers.** The plan previously assumed a visible divider might be required between rows. Evidence confirms the reference shows NO separators between result rows. Flat list, spacing only.
2. **Single-line row layout.** The plan assumed a title+subtitle stacked layout was acceptable. Evidence shows the reference uses an inline single-line `Title — domain` treatment per row.
3. **Row count = 5.** Confirmed by reference screenshot.
4. **Favicons required.** Full-color, from sync cache.
5. **Selection highlight = solid blue/indigo pill.** Tight vertical fit around single-line content.
6. **No section headers.** Confirmed absent.

### Implications for implementation:

- Cluster 2 (row renderer) is more extensive than originally estimated because the row layout must change from two-line to single-line inline format.
- `kMaxSeededVisibleResults` drops from 8 to 5.
- The selection highlight's vertical extent and shape change (pill sized to single-line, not a tall block around two-line content).
- The "divider" acceptance criterion (old bullet 5) is replaced by a "no inter-row dividers" requirement.

---

## Experiment Branch: Swift-Like Immediate Query (Separate PR, Not First-Pass)

This section describes a follow-up experiment. It is NOT part of the first-pass implementation. It must NOT be quietly blended into the mainline steps. It ships in a separate PR, behind a separate decision.

### What it is

The Swift-like pattern: prefill the URL, render seeded rows, then schedule a real query for that text. The results that come back from the model replace the seeded state after ~150ms.

### Why it might eventually be wanted

- Results could include bookmarks, history from other tabs, and maho-core ranked data.
- Closer behavioral match to Swift (which does fire a query).
- Future-proof for personalized/ranked results in the open-state.

### Why it is explicitly excluded from first-pass

- Adds ~150ms latency before the result list stabilizes. Violates Spec Lock bullet 7 (no visual flicker).
- Requires the backend to handle "query = full URL" gracefully (today, `MahoCommandModel::Search` with a URL as query text may produce irrelevant autocomplete results).
- Introduces visual flicker if seeded rows are replaced by different results.
- Harder to test deterministically.
- The user's stated first-pass target is a clean, stable open-state. Not a two-phase render.

### Gating for this experiment

This branch should only be pursued if ALL of the following are true:
1. The first-pass seeded implementation has shipped and is stable.
2. Evidence capture shows that the reference open-state includes results that CANNOT come from NavigationController back-stack alone.
3. OR the product owner explicitly requests richer open-state beyond navigation history.

### If pursued (separate PR), implementation would be:

1. In `InitializeCurrentTabSeedState()`, after rendering seeded rows, call `TriggerSearchForCurrentText()`.
2. When `OnSearchResults()` fires, replace seeded rows with real results (current behavior already handles this transition).
3. Ensure no visual flicker: if real results match seeded order, skip re-render.
4. Add model-layer logic to handle "query is a URL" differently from "query is a search term" in kCurrentTab mode.

---

## Test and Verification Plan

### Verification Matrix

| Category | What exists today | What must be added | Evidence type |
|----------|-------------------|-------------------|---------------|
| Unit: single seed row | 9 tests covering single-row seeded state | — | Automated, CI |
| Unit: multi-row seed | None | Test with mocked 6-entry NavigationController | Automated, CI |
| Unit: row content correctness | `BuildCurrentTabSeedProducesNavigationSuggestion` | Expand to verify title from NavEntry (not URL fallback) | Automated, CI |
| Unit: deduplication | None | Test with duplicate URLs in history | Automated, CI |
| Interactive: overlay opens with URL | `FocusPath_OverlayPrefillsCurrentUrl` | — | Automated, CI |
| Interactive: seeded mechanics with history | Exists (>=2 seed entries verified) | Expand to assert exact 5-row layout with title verification | Automated, CI |
| Interactive: seed row activation | None | Press Enter, verify no navigation change (same URL) | Automated, CI |
| Interactive: arrow-key through seeds | None | Arrow-down, verify selection index changes | Automated, CI |
| Screenshot: full visual parity | None | Side-by-side comparison covering all first-frame details (typography, color, spacing, icons, selection, card height, no inter-row dividers) | Manual, stored in docs/ — BLOCKS SHIPPING |

### Minimum pass criteria for shipping first-pass

1. All existing unit tests pass (no regressions).
2. New multi-row unit test passes with >=5 rows.
3. New interactive test passes with real navigation history.
4. Side-by-side screenshot shows full visual parity with reference for all first-frame details: row count, selection highlight, no inter-row dividers, typography, colors, icons, spacing, card height. No visible delta is acceptable.
5. No visual flicker on open (seeded rows appear on first paint frame).
6. The Spec Lock bullets (all 12) are met.
7. No code from the experiment branch has been introduced.
8. `git diff` shows zero changes to `maho_command_model.h` and `maho_command_model.cc`.

---

## Deferred Work

These items are explicitly NOT part of this plan and must not be folded in during implementation:

- **Immediate-query behavior** (separate experiment branch, separate PR, separate decision)
- **Ranked results in open-state** (requires model changes, backend ranking for URL-as-query)
- **Section headers / group labels** (evidence confirms absent in reference; permanently out of scope for first-pass)
- **Bookmark rows in open-state** (only if evidence shows reference includes them)
- **Cross-tab suggestions** (other open tabs) in the seed list
- **Cross-session history** beyond the current tab's NavigationController
- **Animation on open/close** (motion design, not first-frame static appearance)
- **New-tab mode parity** (separate plan)
- **WebUI command palette open-state** (different surface entirely)
- **Async favicon fetching** (only sync-cached favicons are in scope; async loading is a follow-up)

Nothing in this deferred list is visible in the first-frame reference screenshot. If a detail IS visible in the reference, it is not deferred. It is blocking.

---

## Estimated Effort

| Cluster | Effort | Dependencies |
|---------|--------|--------------|
| 0: Evidence capture | COMPLETE | None |
| 1: Seed builder / open-state | ~2 hours | Cluster 0 complete (done) |
| 2: Row renderer (visual parity, single-line conversion) | ~3 hours | Cluster 0 complete (can parallel with Cluster 1 if evidence is clear) |
| 3: Test cluster | ~2 hours | Clusters 1 and 2 complete |
| 4: Final screenshot sign-off | ~30 min | Clusters 1, 2, and 3 complete |
| **Total first-pass remaining** | **~7.5 hours** | |
| 5: Experiment branch (optional, separate PR) | ~3 hours | First-pass shipped + gating criteria met |
