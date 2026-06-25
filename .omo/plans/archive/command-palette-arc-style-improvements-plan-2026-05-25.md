# Command Palette Arc-Style Improvements Plan

**Created:** 2026-05-25
**Status:** Draft, grounded in current main (commit `e2e5fbf`)
**Primary scope:**
- `maho/crates/maho-core/src/command_bar.rs`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.{cc,h}`
- `maho-chromium/browser/ui/views/command/maho_command_model.{cc,h}`
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.{cc,h}`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- New: `maho-chromium/browser/ui/views/command/maho_command_search_engine_picker_view.{cc,h}`

**Secondary scope:**
- `maho-chromium/browser/ui/views/command/BUILD.gn`
- `maho/crates/maho-core/tests/ranking_regression.rs`

---

## 1. Objective

Bring Maho's command palette UI in line with the Arc browser reference shared in the design discussion. The current Maho palette already has all required infrastructure (12 `CommandSuggestionType` variants, em-dash separator, favicon hydration pipeline, search engine ShellEvent API, a non-interactive "i" badge, etc.); the gaps are exposure logic, presentation polish, and one new picker popover.

Confirmed user decisions (2026-05-25):

1. **Title format:** All English. Use `"<query> — Search with <Engine>"` for the kSearch row.
2. **Search row for URL inputs:** Keep it visible. Do NOT add an `is_url` suppression guard. Current behavior (Navigation 2.0 + Search 1.5 both shown, Navigation first) is the desired final behavior.
3. **App-action rows** (e.g. Arc's "Contact the Team"): Out of scope. No new actions to register.
4. **Trailing "i" badge click:** Open a search engine picker popover that calls `ShellEvent::SetDefaultSearchEngine`.
5. **Rust ranking:** No changes needed — `SuggestionType::Search` already lands at index 0 for all non-URL queries based on the current scoring algorithm.

---

## 2. Current-State Review (verified by exhaustive search this session)

### 2.1 Rust pipeline (`command_bar.rs`)
- `CommandBarEngine::search()` always emits `SuggestionType::Search` for any non-empty query at score `1.5` (line 372).
- `is_url(query)` (lines 828–835) returns true only for `http://`, `https://` prefixes or `contains('.') && !contains(' ')`. It misses `chrome://`, `file://`, `about:`, `data:`, `mailto:`. These misses are not blocking for this plan but are noted as a side bug.
- Final sort: descending by score, with `type_priority` tiebreaker. Cap is `MAX_RESULTS = 12` in Rust.
- Concrete trace for `"naver"`: only `Search` (1.5) is emitted, no Navigation, no Calculator/UnitConversion. Result list: `[Search]`. Search is at index 0.
- Concrete trace for `"naver.com"`: Both `Navigation` (2.0) and `Search` (1.5) emitted. Final order: `[Navigation, Search, ...]`. This is the behavior we want to keep (per user decision #2).

### 2.2 C++ pipeline
- `MahoCommandModel::SearchOnPool()` (`maho_command_model.cc`) parses Rust JSON in array order via plain `push_back`. **No re-sort on the C++ side.**
- `CapResults()` (lines 895–908) caps to 8 for `kSearch`/`kCurrentTab`/`kNewTab`/`kAI`, 20 for `kCommandsOnly`/`kSiteSearch`. Truncates from the end via `resize()`, so an index-0 row always survives.
- `ApplyQuickActionGate()` (lines 882–892) only filters `kAction` rows under specific conditions; never affects `kSearch`.
- `kSearch` rows currently render via the magnifying-glass glyph (`DrawLineIcon` lines 305–318 in `maho_command_result_row_view.cc`, color `#A8C4F0`) because `suggestion.icon` is always `std::nullopt` for them.
- Title is rendered verbatim from Rust JSON `"title"` field. Subtitle goes through `HostCompactMetadataText()` which passes plain strings unchanged (and collapses URLs to bare host).

### 2.3 Trailing "i" badge
- Lives in `maho_command_overlay_view.cc` as `trailing_accessory_` (a plain `views::View`).
- Has `SetCanProcessEventsWithinSubtree(false)` set in both constructors (lines 418, 604) — currently receives zero events.
- Hidden in `kAI`, `kCurrentTab`, and `kCommandsOnly` modes (line 1096 region). Visible in `kSearch` and `kNewTab` modes only.
- Needs to become a clickable button that opens the search engine picker popover.

### 2.4 Search engine state
- Owned by Rust (`maho_core/src/command_bar.rs::CommandBarEngine.search_engines`) and persisted in SQLite (`search_engines` table in `maho-storage/src/sqlite.rs`).
- Five built-in engines: Google, DuckDuckGo, Bing, Brave, Ecosia.
- Write path: `maho_core_handle_event(core, "{type:'SetDefaultSearchEngine', id:'duckduckgo'}")` — already wired end-to-end.
- Read path: `maho_core_get_search_engines(core)` returns JSON.
- **No existing native UI** for picking a default search engine. The settings WebUI exposes a `Select` row in the view model but is not wired through `MahoSettingsPageHandler`.
- Reference popover pattern: `MahoSidebarActionPopoverView` in `browser/ui/views/sidebar/`.

### 2.5 Visual polish gap
- Current container corner radius is 16dp; reference looks closer to 20dp.
- Current input pill corner radius is 11dp; reference looks closer to 14dp.
- Current row spacing is 2dp; reference is more generous (~4dp).
- Current leading icon lane width is 20dp; reference favicons appear ~24–28dp.

---

## 3. Already Landed (do not redo)

These pieces of infrastructure exist and should be reused, not rebuilt:

- The `kSearch` `SuggestionType` and its hardcoded score (1.5).
- The `is_url()` guard on `Navigation` emission.
- `CommandSuggestion::icon: std::optional<ImageData>` field (already optional, ready to be populated for kSearch).
- `views::ImageView` favicon path inside `MahoCommandResultRowView::RebuildGlyph()` — once `icon` is set, the magnifying glass is automatically skipped.
- `trailing_accessory_` view, `kTrailingAccessoryInfoGlyph = u"i"`, and its rounded styling — only the event-passthrough flag needs to change.
- All 14 `kMahoColorCommandBar*` color tokens.
- `ShellEvent::SetDefaultSearchEngine` end-to-end (Rust + SQLite + `CoreUpdate::SearchEnginesUpdated`).
- The `MahoSidebarActionPopoverView` pattern that the new picker will mirror.

---

## 4. Phases and PR Breakdown

The work is split into five PRs ordered by risk and dependency. Each PR is independently shippable and verifiable.

### PR-1: Rust title format change for kSearch row

**Files:** `maho/crates/maho-core/src/command_bar.rs`, `maho/crates/maho-core/tests/ranking_regression.rs`

**Changes:**

1. `command_bar.rs` line 382 — change the title format string from
   ```rust
   title: format!("Search {} for \"{}\"", engine_name, query),
   ```
   to
   ```rust
   title: format!("{} — Search with {}", query, engine_name),
   ```
   The em-dash separator (` — `, U+2014 with surrounding spaces) matches Arc's row layout, and the C++ row view already renders the title verbatim.

2. `command_bar.rs` line 384 — change
   ```rust
   subtitle: Some("Search".to_string()),
   ```
   to
   ```rust
   subtitle: None,
   ```
   so the row renders without a redundant compact-metadata segment. The new title already contains the engine name.

3. **Do NOT** add an `&& !Self::is_url(query)` guard. URL inputs continue to show both Navigation and Search rows (per user decision).

4. Add regression tests to `ranking_regression.rs`:
   - `search_suggestion_first_for_non_url_query` — asserts `results[0].kind == SuggestionType::Search` for `["naver", "ㅇㅇ", "hello world"]`.
   - `search_suggestion_title_uses_em_dash_format` — asserts the kSearch row's title matches the `"<q> — Search with <engine>"` shape.
   - `search_suggestion_still_emitted_for_url_query` — asserts kSearch is present (not necessarily first) when input is `"naver.com"`.

**Verification:**
- `cd maho && bun run test` passes
- `cd maho && bun run typecheck` passes
- Manual: `cd maho && cargo test -p maho-core ranking_regression`

**Risk:** Low. Pure Rust, contained to one function and its tests.

---

### PR-2: Visual alignment with Arc reference

**Files:** `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`, `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc`

**Changes:** Adjust constexpr layout values at the top of each file. No logic changes.

In `maho_command_overlay_view.cc`:

| Constant | Current | New |
|---|---|---|
| `kPanelCornerRadiusDp` (line 89) | 16 | 20 |
| `kSearchRowHeightDp` (line 96) | 44 | 48 |
| `kSearchRowCornerRadiusDp` (line 97) | 11 | 14 |
| `kSearchIconLaneWidthDp` (line 99) | 20 | 28 |
| `kLeadingPageIconSizeDp` (line 101) | 16 | 20 |
| `kResultRowSpacingDp` (line 103) | 2 | 4 |

In `maho_command_result_row_view.cc`: no constant changes in this PR. Row internals stay at current values to keep the PR small and focused on the outer container.

**Verification:**
- `python3 maho-chromium/build/scripts/build_maho.py` succeeds
- Manual: open Cmd+L, compare side-by-side against the reference screenshot. Container should look more rounded, input pill taller and more rounded, rows breathe more.

**Risk:** Very low. Pure visual constants.

---

### PR-3: Mirror selected row's favicon into the input field's leading icon

**Files:** `maho-chromium/browser/ui/views/command/maho_command_overlay_view.{cc,h}`

**Goal:** When the highlighted suggestion changes (keyboard up/down or hover), update the input field's leading icon to display the selected row's favicon. Falls back to the current sparkle glyph when no favicon is available.

**Changes:**

1. In `maho_command_overlay_view.cc::UpdateSearchLeadingAccessory()` (~line 1122), extend the favicon source priority to:
   - First: `model_->results()[selected_index_].icon` if present
   - Second: `seeded_results_.front().icon` if present (current kCurrentTab behavior)
   - Else: hide `search_context_icon_` and show `SearchIconView` (current sparkle fallback)

2. In `maho_command_overlay_view.cc::UpdateSelection()` (~line 1539), after `row->SetSelected(true)`, call `UpdateSearchLeadingAccessory()` so selection movement re-evaluates the leading icon.

3. Decode and cache `ImageData` once per result update to avoid re-decoding on every selection change. Reuse the existing `DecodeFaviconImage()` helper from `maho_command_result_row_view.cc` (consider moving it to a shared anonymous namespace or a small util header if both files need it; an alternative is to expose a minimal decode helper from the row view's translation unit).

4. Do not change the icon when the highlighted row is a kSearch row that has no icon yet — the sparkle stays. PR-5 will give kSearch rows their engine favicon.

**Verification:**
- Build green
- Manual: type "github" with a tab open, press ↓ to select a Tab match, observe input leading icon switching to the tab's favicon.
- Manual: with no results, input shows sparkle (unchanged).

**Risk:** Low–medium. Touches one UI flow but only adds new visibility logic; existing behavior is preserved as fallback.

---

### PR-4: Make trailing "i" badge clickable and add search engine picker popover

**Files (modified):** `maho-chromium/browser/ui/views/command/maho_command_overlay_view.{cc,h}`, `maho-chromium/browser/ui/views/command/BUILD.gn`

**Files (new):**
- `maho-chromium/browser/ui/views/command/maho_command_search_engine_picker_view.h`
- `maho-chromium/browser/ui/views/command/maho_command_search_engine_picker_view.cc`

**Changes:**

1. Convert `trailing_accessory_` from `views::View*` to `views::MdTextButton*` (recommended over `ToggleImageButton` because the picker is a transient popover, not a toggleable panel — `MdTextButton` keeps the existing label-glyph approach simpler).
   - Header (line 181): `raw_ptr<views::MdTextButton> trailing_accessory_`
   - Both constructors: replace the raw `views::View` construction with `views::MdTextButton` and bind a `base::RepeatingCallback` to `OnSearchEngineBadgePressed`.
   - Remove `SetCanProcessEventsWithinSubtree(false)` (lines 418, 604).
   - Keep the existing rounded background, border, and `"i"` glyph styling. Use `SetStyle(ui::ButtonStyle::kText)`, `SetCornerRadius(kTrailingAccessoryCornerRadiusDp)`, `SetMinSize`/`SetMaxSize` to lock the 18×18dp dimensions.
   - Add a tooltip: `"Change search engine"`.
   - Disable ink drop and focus ring to preserve the current visual.

2. Update `UpdateLayoutForMode()` (~line 1096) to keep `trailing_accessory_` visible in `kSearch`, `kCurrentTab`, and `kNewTab`. Continue hiding it only in `kAI` and `kCommandsOnly` modes.

3. Add the new picker view. Mirror `MahoSidebarActionPopoverView` (`browser/ui/views/sidebar/maho_sidebar_action_popover_view.{cc,h}`) closely:
   - Class name `MahoCommandSearchEnginePickerView`.
   - Static factory `static views::Widget* Show(views::View* anchor_view, base::RepeatingCallback<void(std::string)> on_selected, raw_ptr<MahoCommandModel> model)`.
   - `views::BubbleDialogDelegate` content view, `BubbleBorder::TOP_RIGHT` (badge sits at top-right of the input row, popover should expand downward to avoid clipping the input).
   - `set_close_on_deactivate(true)`, zero margins, fixed width around 280–320dp.
   - Calls `maho_core_get_search_engines(core)` directly (same FFI as `MahoCommandModel::HydrateSiteSearchEngines()` — copy that JSON parsing locally to avoid coupling).
   - One `SearchEngineRow` (inner `views::View` subclass) per engine: 16dp favicon (loaded async via `LargeIconService` for the engine's homepage URL — same pattern as `RequestBookmarkFavicons()`), engine name label, trailing checkmark `views::ImageView` shown when `engine.is_default`.
   - Hover background via the `selection_background_` toggle pattern (copied from `ActionRow`).
   - On row click: invoke `on_selected.Run(engine.id)`, then call `Close()` on the bubble widget.

4. Wire the click in `MahoCommandOverlayView`:
   ```cpp
   void MahoCommandOverlayView::OnSearchEngineBadgePressed() {
     if (picker_widget_) {
       picker_widget_->Close();
       return;
     }
     picker_widget_ = MahoCommandSearchEnginePickerView::Show(
         trailing_accessory_,
         base::BindRepeating(&MahoCommandOverlayView::OnSearchEngineSelected,
                             weak_factory_.GetWeakPtr()),
         model_);
     picker_observation_.Observe(picker_widget_);
   }

   void MahoCommandOverlayView::OnSearchEngineSelected(const std::string& engine_id) {
     base::Value::Dict event;
     event.Set("type", "SetDefaultSearchEngine");
     event.Set("id", engine_id);
     std::string event_json;
     base::JSONWriter::Write(base::Value(std::move(event)), &event_json);
     char* result = maho_core_handle_event(maho::GetCore(), event_json.c_str());
     if (result) maho_string_free(result);

     // Refresh current results so the kSearch row reflects the new engine name.
     model_->RefetchCurrentQuery();
   }

   void MahoCommandOverlayView::OnWidgetDestroying(views::Widget* widget) {
     if (widget == picker_widget_) {
       picker_observation_.Reset();
       picker_widget_ = nullptr;
     }
   }
   ```

5. Add new fields to the header:
   ```cpp
   raw_ptr<views::Widget> picker_widget_ = nullptr;
   base::ScopedObservation<views::Widget, views::WidgetObserver> picker_observation_{this};
   base::WeakPtrFactory<MahoCommandOverlayView> weak_factory_{this};
   ```

6. Update `BUILD.gn` `maho_command_overlay` source_set to include the two new files.

7. If `MahoCommandModel` does not already expose a `RefetchCurrentQuery()` helper, add one that re-issues the last query (the model already stores it for debouncing; surfacing a public method is a one-line addition).

**Verification:**
- Build green
- Manual: open Cmd+L, click the "i" badge in the input row, picker appears below it. Select a non-default engine (e.g. DuckDuckGo). Picker closes. Type a query — the kSearch row title now reads `"<q> — Search with DuckDuckGo"`. Restart the browser, the change is persisted (SQLite).
- Manual: clicking the badge a second time toggles the picker closed.
- Manual: clicking outside the picker dismisses it (`close_on_deactivate`).
- Unit tests for the picker view (visibility of checkmark for the active engine, callback invocation on row click).

**Risk:** Medium. New file, new public surface, but every dependency is established and the popover pattern is borrowed verbatim from a working sibling.

---

### PR-5: Engine favicon on kSearch row

**Files:** `maho-chromium/browser/ui/views/command/maho_command_model.{cc,h}`

**Goal:** Replace the magnifying-glass glyph on kSearch rows with the current default search engine's favicon, matching the Arc reference.

**Implementation strategy:** C++-side hydration via Chromium's `LargeIconService`. Avoids changes to Rust types (no need to add `ImageData` to `SearchEngine` or bundle bitmap data in the Rust crate).

**Changes:**

1. Add `RequestSearchEngineFavicons()` to `MahoCommandModel`. Mirror `RequestBookmarkFavicons()` (in `maho_command_model.cc` near line 716) exactly:
   - Iterate `results_`; for each row with `type == CommandSuggestionType::kSearch && !icon`, look up the engine homepage URL.
   - Resolve homepage from the engine's `url_template` by stripping the `{query}` parameter and reducing to origin (e.g. `https://www.google.com/search?q={query}` → `https://www.google.com`). The model already calls `maho_core_get_search_engines()` for site-search hydration; reuse that data.
   - Issue `LargeIconService::GetLargeIconImageOrFallbackStyle()` for each unique engine origin.
   - On callback, build an `ImageData` from the returned bitmap and call `result_row->SetIcon(image_data)`.

2. Call `RequestSearchEngineFavicons()` from `OnResults()` after `CapResults()`, parallel to the existing `RequestBookmarkFavicons()` and `RequestTabFavicons()` calls.

3. Cache resolved engine favicons in `MahoCommandModel` to avoid re-fetching on every keystroke. A `base::flat_map<std::string, gfx::Image> engine_favicon_cache_` keyed by engine id is sufficient (entries persist for the model's lifetime, which matches one Browser).

4. Fallback: if the favicon service has nothing cached and the network fetch fails, the row keeps its magnifying glass — this is the existing fallback path inside `MahoCommandResultRowView::RebuildGlyph()` and requires no extra code.

**Verification:**
- Build green
- Manual: open Cmd+L, type a non-URL query. The kSearch row's left-side glyph is the Google "G" favicon (or whichever engine is default).
- Manual: switch engines via the PR-4 picker. The kSearch row's favicon updates within a debounce window.
- Manual: run with offline state — row falls back to magnifying glass without crashing.

**Risk:** Medium. Async favicon fetching is well-understood (mirrors bookmark hydration), but introduces a new lifetime concern with the cache.

---

## 5. Sequencing and Dependencies

| PR | Depends on | Can land independently? |
|---|---|---|
| PR-1 | None | Yes |
| PR-2 | None | Yes |
| PR-3 | None | Yes (gracefully no-ops for rows without favicons) |
| PR-4 | PR-1 (so the picker's effect is visible in the title) | Yes, but UX is muted without PR-1 |
| PR-5 | PR-1 (kSearch row's revised title), benefits from PR-3 (input mirror also lights up) | Yes |

Recommended landing order: **PR-1 → PR-2 → PR-3 → PR-4 → PR-5.**

---

## 6. Out of Scope (deferred)

The following items were considered during planning and explicitly deferred:

- **App-action rows** (Arc-style "Contact the Team" entries). User declined.
- **Suppressing kSearch row for URL inputs** (Arc's behavior). User chose to keep current behavior of showing both Navigation and Search.
- **Extending Rust `is_url()` to cover `chrome://`, `file://`, `about:`, `data:`, `mailto:`.** Side bug; affects whether Navigation appears for these schemes. Not blocking the visible UX work.
- **Bundling engine favicons in Rust** (alternative to PR-5's C++ `LargeIconService` path). Heavier, requires new types in `maho-types`.
- **Wiring `chrome://maho-settings` to actually fire `SetDefaultSearchEngine`.** Settings WebUI's view model already exposes the row but its handler is unwired; tracked separately.
- **Localization.** All strings remain English per user decision.

---

## 7. Verification Matrix

| Surface | PR-1 | PR-2 | PR-3 | PR-4 | PR-5 |
|---|---|---|---|---|---|
| Rust unit tests | Add 3 tests | — | — | — | — |
| C++ unit tests | — | — | — | Add picker view tests | Add favicon hydration test |
| `bun run typecheck` (root) | Pass | Pass | Pass | Pass | Pass |
| `bun run test` (root) | Pass | Pass | Pass | Pass | Pass |
| `python3 maho-chromium/build/scripts/build_maho.py` | Not required | Required | Required | Required | Required |
| Manual: type "naver", row 0 reads `"naver — Search with Google"` | Verify | — | — | — | — |
| Manual: container looks more rounded | — | Verify | — | — | — |
| Manual: input leading icon mirrors selected row | — | — | Verify | — | — |
| Manual: "i" badge opens picker, selection persists across restart | — | — | — | Verify | — |
| Manual: kSearch row shows Google "G" favicon instead of magnifying glass | — | — | — | — | Verify |

---

## 8. Open Questions

None blocking. All product decisions captured in section 1.

If discovered during implementation:
- If `MahoCommandModel::RefetchCurrentQuery()` does not exist, add it as a public method that re-runs the last query string through the existing debounced search path.
- If `views::MdTextButton` cannot exactly preserve the current 18×18dp circular badge appearance, fall back to wrapping the existing `views::View` in a `views::Button` subclass with a manual `OnMousePressed` override (Pattern B from the search investigation).
