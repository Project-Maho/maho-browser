# Command Palette Zen-Native Parity: File-by-File Implementation Plan (2026-05-23)

## Status

Planning only. No implementation is part of this document. This plan is intended to be picked up directly by an implementation agent or human contributor and executed file by file. It supersedes nothing; earlier command-palette plans remain in force for their narrower scopes:

- `.sisyphus/plans/command-palette-current-tab-zero-delta-execution-plan-2026-05-22.md` (visual zero-delta loop for the `kCurrentTab` open frame)
- `.sisyphus/plans/command-palette-current-tab-closeout-plan-2026-05-22.md` (historical closeout context)
- `.sisyphus/plans/command-palette-reference-implementation-plan.md` (Arc-style ranking + click path strategy)

This document is scoped to **Zen-native command palette parity** for the Views overlay at `maho-chromium/browser/ui/views/command/`. Any work that overlaps with the above plans must defer to whichever plan owns the narrower acceptance gate.

## Why this plan exists

The existing command palette is functional. Multiple targeted plans drove individual subsystems (visual zero-delta on `kCurrentTab`, Arc-style ranking, click-path unification flag). What is missing is a single document that:

1. States the parity baseline as **Zen native command palette behavior only**,
2. Walks each grounded file in `maho-chromium/browser/ui/views/command/` and the upstream override at `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.cc`,
3. Names the specific Zen-parity changes per file with explicit invariants, and
4. Forces the team to make the empty-query and click-path product decisions before implementation begins, instead of after another round of regressions.

The biggest correctness risk today is not a single missing feature. It is **disagreement between four overlapping pieces of state** at the moment of activation: the typed text in `textfield_`, the selected suggestion in `model_->selected_index()`, the seeded rows in `seeded_results_`, and the disposition resolved through `origin_mode_`. Zen native behavior is straightforward only because each of those four pieces of state is bound to a single source of truth. We must enforce the same invariant here, file by file.

## Goal and parity baseline

### Goal

Match Zen Browser's **native** command palette behavior for the three production modes that already exist on disk:

- `CommandOverlayMode::kCurrentTab` (Cmd+L, URL prefilled, select-all)
- `CommandOverlayMode::kNewTab` (Cmd+T, blank seed, foreground-tab disposition)
- `CommandOverlayMode::kSearch` (palette-from-action, blank seed, current-tab disposition)

Sub-modes `kSiteSearch` and `kAI` are inherited as transient overlays on top of an `origin_mode_`. They are kept exactly as-is; this plan only ensures their disposition resolution does not drift away from Zen.

### Parity baseline source

The only source of truth for "Zen native" in this plan is upstream Zen Browser's first-party command palette as shipped from [zen-browser/desktop](https://github.com/zen-browser/desktop). Specifically the URL bar / unified search experience that Zen ships by default.

### Out of scope for the parity baseline (explicit)

- **No third-party Zen mods.** Community mods to the Zen command palette (e.g. `zen-twilight`-flavored bars, mod-store enhancements, alternate URL bars) are explicitly **not** part of the parity baseline. We do not chase mod features.
- **No Arc-style behavior except where Zen also adopts it.** Arc-only patterns (e.g. seeded current-tab rows, the AI sparkle leading icon, the trailing info accessory tile) are Maho-specific UX and remain in their own product layer; see "Hard scope boundary" below.
- **No omnibox replacement.** The location bar is unchanged. Click-path unification stays flag-gated under `kMahoUnifiedClickPath`; see Phase 3.

## Hard scope boundary

### In scope

- Behavior parity for `kCurrentTab`, `kNewTab`, `kSearch` modes against Zen native:
  - Activation entry points (Cmd+L, Cmd+T, action-triggered `command_bar`)
  - Empty-query semantics (what is shown when the input is empty in each mode)
  - Selection model (initial selection, arrow keys, Enter/Cmd+Enter disposition)
  - Disposition correctness across mode and sub-mode transitions
  - Dismissal (Escape, click-outside, focus-loss, parent-widget destruction)
- Stabilizing the four-state agreement: `textfield_` text, `model_->selected_index()`, `seeded_results_`, `origin_mode_`.
- Click-path parity completion behind `kMahoUnifiedClickPath` (already declared, partially landed, must finish).

### Out of scope

- **Seeded current-tab rows** as Zen-parity work. Maho's seeded rows (`BuildCurrentTabSeedResults`, `kCurrentTabSeeded` row presentation, the 5-row `kMaxSeededVisibleResults` cap) are Arc/Maho UX, not Zen native. They stay in place as **optional product-layer behavior** and are governed by their own dedicated plan, not this one. **Do not delete them. Do not redesign them. Do not promote them into a Zen parity gate.**
- Visual zero-delta against frozen reference screenshots. That is owned by `command-palette-current-tab-zero-delta-execution-plan-2026-05-22.md`.
- Rust-side ranking, dedup, lazy favicons, mode bias logic. That is owned by `command-palette-reference-implementation-plan.md`.
- WebUI command surface (`chrome://maho-command`). Frozen.
- Any change to `MahoCommandActionHandler` action set. Action wiring is parity-complete; only Zen-native disposition correctness is in scope, not adding or removing actions.
- `MahoLittleArcWindow` and the little-arc interactive tests.

## Grounded current-state summary

The following is taken directly from the current code at the listed line ranges. This is the floor we are building from. Do not re-derive.

### Activation entry points (Zen-relevant)

| Trigger | Site | Routes to | Mode |
|---|---|---|---|
| Cmd+L | `maho_shortcut_interceptor.cc:211-227` | `BrowserView::ShowMahoCommandOverlayForCurrentTab()` | `kCurrentTab` |
| Cmd+T | `maho_shortcut_interceptor.cc:229-256` | `MahoSidebarContainerView::ShowCommandOverlayForNewTab()` -> `BrowserView::ShowMahoCommandOverlayForNewTab()` | `kNewTab` |
| `command_bar` action | `maho_command_action_handler.cc:289-297` | `BrowserView::ShowMahoCommandOverlayForSearch("")` | `kSearch` |
| Location-bar focus (flag) | `chromium_src/.../browser_view.cc` patch comment, `is_user_initiated && maho_sidebar_container_` | `ShowMahoCommandOverlayForCurrentTab()` | `kCurrentTab` |

The flag-gated path is documented in `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.cc` (line ~18) and the feature is declared at `maho-chromium/browser/maho_features.h:25` (`kMahoUnifiedClickPath`). The override comment shows the patch routes to `ShowMahoCommandOverlayForCurrentTab()` *unconditionally* once `is_user_initiated && maho_sidebar_container_` is true. The feature flag itself is **not yet read** at the override site. That is the incomplete part of click-path parity.

### Mode and disposition state

`maho_command_overlay_view.h:174-200` declares both `mode_` and `origin_mode_`. `origin_mode_` is set on entry to a sub-mode (`kSiteSearch`, `kAI`) and used by `DispositionForMode()` (defined at `maho_command_overlay_view.cc:1654-1672`) so that, e.g., a new-tab palette that transitioned into site-search still opens the result in `NEW_FOREGROUND_TAB`. The current implementation:

```cpp
const CommandOverlayMode effective_mode =
    (mode_ == CommandOverlayMode::kSiteSearch ||
     mode_ == CommandOverlayMode::kAI)
        ? origin_mode_
        : mode_;
switch (effective_mode) {
  case CommandOverlayMode::kNewTab:
    return WindowOpenDisposition::NEW_FOREGROUND_TAB;
  case CommandOverlayMode::kCurrentTab:
    return WindowOpenDisposition::CURRENT_TAB;
  default:
    return WindowOpenDisposition::CURRENT_TAB;
}
```

This is the only place disposition is resolved. **The `kCurrentTab` vs `kNewTab` split is intentional and already aligns with Zen at the semantic level**: Cmd+L navigates the active tab; Cmd+T opens a new foreground tab. Do not collapse them. Do not introduce a third "auto" mode that picks at runtime.

### Seeded current-tab state (Maho-specific, not Zen)

`maho_command_overlay_view.cc:940-963` seeds `seeded_results_` from the active tab URL when `mode_ == kCurrentTab` and `initial_text` is non-empty. `kMaxSeededVisibleResults = 5` is enforced in `UpdateResultViews(...)` around line 1377. `OnSearchResults(...)` at line 1355-1364 short-circuits backend results while `is_seeded_state_ == true`. `ShouldClearSeededStateForText(...)` at line 1066-1068 clears the seeded state the moment the user types anything not equal to the original URL.

This is Maho-specific UX. It coexists with Zen parity work but is **not** the Zen baseline. The parity work in this plan must not regress seeded-state behavior, and must not extend it.

### Empty-query branch

`maho_command_model.cc:367-378`:

```cpp
if (query.empty()) {
  debounce_timer_.Stop();
  results_.clear();
  if (mode == CommandOverlayMode::kNewTab) {
    debounce_timer_.Start(FROM_HERE, kDebounceDelay,
                          base::BindOnce(&MahoCommandModel::FireSearch,
                                         weak_factory_.GetWeakPtr()));
    return;
  }
  callback.Run({});
  return;
}
```

This is the explicit difference: `kNewTab` runs an empty-query search through the backend (which returns recents / contextual zero-state results) while `kSearch` and `kCurrentTab` short-circuit with no results, and `MahoCommandOverlayView::UpdateResultViews({})` then falls through to the recent-searches branch (line 1415+) or the seeded state.

**Zen native does not behave like this for both Cmd+L and Cmd+T.** A product decision is required; see Phase 0 below.

### Click-path / focus parity

The `kMahoUnifiedClickPath` feature is declared in `maho_features.h` but the override patch at `chromium_src/.../browser_view.cc` does not yet conditionally read the feature value. The patch comment in that file says "Disabled by default pending end-to-end QA sign-off" but the `if (is_user_initiated && maho_sidebar_container_)` test in the patch does not include a `base::FeatureList::IsEnabled(maho::features::kMahoUnifiedClickPath)` clause.

Said differently: **the feature flag exists; the gate that reads it is incomplete.** This is the parity hole that Phase 3 closes.

## Product decision points (must be answered before code phase begins)

These are not implementation tradeoffs. They are user-visible behavior decisions that change which file the Zen-parity edit lands in. Both decisions must be recorded in `docs/cmd-l-open-state-delta.md` (or a sibling doc) before Phase 1 begins.

### Decision 1: Empty-query behavior across modes

The current code only runs an empty-query backend search for `kNewTab`. Recommended default for Zen parity:

| Mode | Empty-query behavior (recommended) | Reason |
|---|---|---|
| `kCurrentTab` | Show seeded current-tab rows if seeding succeeded; otherwise show recent searches. **No backend search.** | Matches Zen URL-bar opening on a tab: shows current page first, then recent suggestions. Preserves Maho seeded UX. |
| `kNewTab` | Run backend empty-query search (current behavior). | Zen blank URL-bar in a new tab shows top sites / suggested. Closest Maho equivalent is the empty-query backend path. |
| `kSearch` | Show recent searches only. **No backend search.** | Zen has no direct equivalent; this is a Maho-only mode invoked from actions. Recents is the safe default. |

If the decision differs, the empty-query branch in `maho_command_model.cc:367-378` is the **only** place the change lands.

### Decision 2: Click-path unification flag default

`kMahoUnifiedClickPath` must be either:

- **Recommended default: enabled by default,** with the `chromium_src/.../browser_view.cc` patch updated to actually consult the flag (currently it does not), and the existing `MahoUnifiedClickPathBehaviorTest` suite expanded to cover both flag states, **or**
- **Alternative: keep disabled by default** indefinitely, and explicitly delete the partial patch that already ignores the flag. The existing comment "Disabled by default pending end-to-end QA sign-off" is misleading because no flag check is actually present.

Either way, the file-by-file work in Phase 3 must end with the runtime behavior matching the documented default.

## File-by-file implementation plan

The following table is the source of truth for which files this plan touches. Files not listed here are **out of scope**. The header AGENTS.md note about not editing GN files without explicit user approval applies here without exception.

| # | File | Phase | Why involved |
|---|---|---|---|
| 1 | `maho-chromium/browser/ui/views/command/maho_command_model.cc` | 1 | Empty-query branch ownership; disposition-relevant only via mode forwarding |
| 2 | `maho-chromium/browser/ui/views/command/maho_command_model.h` | 1 | No signature change expected; touched only if a test seam is needed |
| 3 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc` | 2 | Four-state agreement enforcement; `OnSearchResults`/`UpdateResultViews`/`ActivateSelected`/`NavigateTypedText` |
| 4 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h` | 2 | Add minimal test seams only if needed for four-state invariant tests |
| 5 | `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc` | 2 | Activation lifecycle parity (focus restore, dismissal determinism) |
| 6 | `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.h` | 2 | Header touch only if a new test accessor is required |
| 7 | `maho-chromium/browser/ui/views/command/maho_shortcut_interceptor.cc` | 2 | Cmd+L and Cmd+T path verification only (no behavior change expected) |
| 8 | `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | 2 | Verify `command_bar` and `focus_url_bar` route to the right `ShowMahoCommandOverlayFor*` |
| 9 | `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.cc` | 3 | Complete the `kMahoUnifiedClickPath` flag gate at `SetFocusToLocationBar` |
| 10 | `maho-chromium/browser/maho_features.h` | 3 | Update declaration comment to reflect the actual default chosen in Decision 2 |
| 11 | `maho-chromium/browser/ui/views/command/maho_command_overlay_view_unittest.cc` | 1, 2 | Add four-state invariant tests, empty-query mode tests |
| 12 | `maho-chromium/browser/ui/views/command/maho_command_overlay_interactive_uitest.cc` | 2, 3 | Runtime coverage on real ordinary-site path; click-path flag on/off |
| 13 | `maho-chromium/browser/ui/views/command/maho_unified_click_path_behavior_unittest.cc` | 3 | Cover both flag states; add focus-reason matrix |

### 1. `maho-chromium/browser/ui/views/command/maho_command_model.cc`

**Phase:** 1 (empty-query semantics)

**Changes:**

- Update the empty-query branch at lines 367-378 to match Decision 1 above. Concretely, when Decision 1 is the recommended default:
  - `kCurrentTab` empty-query: keep `results_.clear()` and call back with empty results. The view layer is responsible for falling through to seeded rows or recent searches. The model must not be the seeded-row source.
  - `kNewTab` empty-query: keep current behavior (debounced backend search).
  - `kSearch` empty-query: keep `callback.Run({})`. View layer falls through to recent searches.
- No other behavior change. No re-sort. No dedup. No bias. Those belong to the reference plan.

**Why this file:** This is the only place where empty-query is steered per mode. The decision has user-visible consequences in every entry point.

**Must not change:**

- `BuildTabsJson(...)` favicon encoding. That is the reference plan's territory.
- `CapResults(...)` semantics. Same.
- `MatchSiteSearch`/`HydrateSiteSearchEngines`/recent-search storage.
- The `'>'` actions-only prefix handling at lines 400-405.

**Tests added:**

- New unit test in `maho_command_overlay_view_unittest.cc` verifying that an empty Cmd+L open with no seeded results lands on the recent-searches fallback (existing branch in view).
- New unit test verifying that an empty Cmd+T open triggers a debounced backend search (existing) and that an empty Cmd+L open does **not**.

### 2. `maho-chromium/browser/ui/views/command/maho_command_model.h`

**Phase:** 1

**Changes:** None expected. If a `ShouldRunBackendOnEmptyQuery(CommandOverlayMode)`-style helper is extracted to make the empty-query rule testable, declare it here as a `static` so unit tests can call it without constructing the model.

**Why this file:** Header pairing only. The contract surface stays identical.

**Must not change:**

- Public method signatures of `Search`, `Cancel`, `SaveRecentSearch`, `GetRecentSearches`.
- The `CommandSuggestion` struct shape.
- `CommandOverlayMode` enum order or values.

**Tests added:** None directly; tests live in the unit test file.

### 3. `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc`

**Phase:** 2 (four-state agreement; Zen-native activation semantics)

This is the largest and most invariant-sensitive file in the plan. The work here is **not** about adding features. It is about making the four overlapping pieces of state agree at every transition.

**Four-state invariant (must hold at every public transition):**

> At any moment, exactly one of these is the user's "intent" and the other three follow it deterministically:
> 1. `textfield_->GetText()` (typed text)
> 2. `model_->selected_index()` (selected suggestion within `model_->results()`)
> 3. `seeded_results_` + `is_seeded_state_` (seeded fallback rows)
> 4. `origin_mode_` (effective disposition source)

The transitions that currently exist are:

- User types -> seeded state may clear (`ContentsChanged` -> `ShouldClearSeededStateForText`)
- Backend results arrive -> view ignores them while `is_seeded_state_` is true (`OnSearchResults` line 1361)
- Arrow keys -> `UpdateSelection`
- Enter -> `ActivateSelected` -> picks `results_[selected_index_]` if in range, else `NavigateTypedText`
- Tab -> `EnterSiteSearchMode` or `EnterAIMode` -> sets `mode_ = kSiteSearch|kAI` and `origin_mode_ = previous_mode_`
- Escape -> `Dismiss`

**Changes required in this file:**

- **Audit `ActivateSelected()` (line 1560-1576).** Confirm that when `is_seeded_state_` is true, `model_->results()` has been populated by `model_->set_results(seeded_results_)` (already done at line 1392) so `results[index]` matches what the user sees. Add a `DCHECK_EQ(results.size(), seeded_results_.size())` while `is_seeded_state_` is true to make any future drift loud.
- **Audit `NavigateTypedText()` (line 1593-1652).** When `is_seeded_state_` is true and the typed text equals the original URL, the user is "activating the seeded URL." Verify this matches the seeded row 0 navigation behavior. Today it falls through `GURL` parsing and may produce a different disposition path than the seeded row activation. Make them go through the same code path (preferred: route both through `OpenCommandSuggestion(browser_, seeded_results_[0], DispositionForMode())`).
- **Tighten `DispositionForMode()` for sub-mode parity.** No code change expected, but add a regression test (Phase 2 tests) covering: `kCurrentTab` -> Tab -> AI -> Enter still opens in `CURRENT_TAB`; `kNewTab` -> Tab -> AI -> Enter still opens in `NEW_FOREGROUND_TAB`. This is the `origin_mode_` round-trip.
- **Empty-text in `kCurrentTab` after seeded clear.** When the user types into a `kCurrentTab` palette and then deletes all text, the four states must converge to the empty-`kCurrentTab` policy chosen in Decision 1. Today this path goes through `ContentsChanged` -> `model_->Search("", kCurrentTab, ...)` which currently calls back with empty. Confirm `UpdateResultViews({})` then renders recent searches and not stale seeded rows. Add a test.
- **Activation traceability.** Add a single `LOG(WARNING) << "[maho-command-trace] activate mode=" << ... << " origin=" << ... << " selected=" << ... << " seeded=" << is_seeded_state_;` immediately before each `OpenCommandSuggestion(...)` and `Navigate(...)` call. This is non-behavioral; it makes the four-state agreement debuggable when a regression slips past tests.

**Why this file:** It is the only place where the four-state truth converges. Drift between any two of the four creates a parity bug. Zen Browser does not have this problem because it does not have seeded rows; we keep the rows but must treat the convergence as a hard invariant.

**Must not change:**

- `BuildCurrentTabSeed*(...)` family (lines 800-960). Seeded rows construction is Maho-specific UX; this plan does not touch it.
- `kMaxSeededVisibleResults = 5` cap.
- Sparkle leading icon, trailing accessory, mode chip presentation. Tonal/structural visual work is owned by the zero-delta plan.
- Divider visibility rules in the seeded branch (line 1395-1398). Owned by the zero-delta plan.
- The `'>'` actions-only and AI/site-search Tab handling at lines 1318-1348.

**Tests added:**

- "Four-state agreement at Enter" unit tests (one per mode) in `maho_command_overlay_view_unittest.cc`.
- "Origin-mode round-trip across sub-modes" unit tests.
- "Empty-text after seeded clear" unit test.
- Runtime test in `maho_command_overlay_interactive_uitest.cc` for an ordinary-site Cmd+L open -> select-all is preserved -> Enter activates `kCurrentTab` disposition.

### 4. `maho-chromium/browser/ui/views/command/maho_command_overlay_view.h`

**Phase:** 2

**Changes:**

- Add narrowly named test seams only if required by Phase 2 invariant tests. Candidates:
  - `bool FourStateAgreesForTesting() const;` returning true iff `model_->results().size()` matches the rendered row count and `is_seeded_state_` matches the row presentation style.
  - `WindowOpenDisposition ResolveActivationDispositionForTesting(int index) const;` that simulates `ActivateSelected` disposition resolution without firing `OpenCommandSuggestion`.
- Do not expose any new non-`ForTesting` API.

**Why this file:** Header-side test seam pairing.

**Must not change:**

- Public constructor signatures (production constructor and the `ForTestingTag` constructor).
- `TextfieldController` overrides.
- The view's `views::View`/`METADATA_HEADER` shape.

**Tests added:** N/A directly.

### 5. `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`

**Phase:** 2 (activation lifecycle parity)

**Changes:**

- **Verify focus restore parity.** `RestoreFocusToPreShowView()` exists; confirm that on Escape, click-outside, and parent-widget destruction it consistently returns focus to the pre-show view. Zen native always restores focus to the previously-focused web content or chrome view. Add a test in the controller test (or new test file) covering all three dismiss paths.
- **Verify dismissal determinism.** `OnWidgetActivationChanged(widget, /*active=*/false)` triggers `deactivation_hide_timer_`. Confirm there is no double-fire if Escape and deactivation happen in the same tick. Use a `base::OneShotTimer::IsRunning()` check before firing.
- **Latency trace event.** `open_start_ticks_` is recorded; ensure a corresponding `TRACE_EVENT*` or histogram emission exists at first-paint. If not, add it (this is the same trace work the reference plan calls out; either plan's owner can land it, but it is part of Zen-parity QA because Zen activation is perceptibly fast).

**Why this file:** Lifecycle parity is the silent half of "feels like Zen." If the overlay opens but focus restoration is flaky, users perceive it as broken even when the visible frame is correct.

**Must not change:**

- `Show()` / `Hide()` / `Toggle()` public signatures.
- Animation curves (`gfx::SlideAnimation`) parameters; visual polish belongs to its own plan.
- `ComputePopupBounds()` geometry. Geometry parity is owned by the zero-delta plan.

**Tests added:**

- Focus-restore-on-Escape, focus-restore-on-click-outside, focus-restore-on-parent-destroy controller tests.
- Open/close cycle stress test (100 cycles) verifying no leaked widget pointers and no focus drift.

### 6. `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.h`

**Phase:** 2

**Changes:** Add `ForTesting` accessors only if Phase 2 controller tests require them. Candidates: `bool deactivation_timer_running_for_testing() const;`. No production API change.

**Why this file:** Header pairing for new test seams.

**Must not change:** `WidgetObserver`/`AnimationDelegate` overrides; `friend` declarations; existing public API.

**Tests added:** N/A directly.

### 7. `maho-chromium/browser/ui/views/command/maho_shortcut_interceptor.cc`

**Phase:** 2 (verification only; no behavior change expected)

**Changes:**

- Confirm Cmd+L (line 211-227) routes to `BrowserView::ShowMahoCommandOverlayForCurrentTab()` exclusively. No fallback to legacy omnibox focus.
- Confirm Cmd+T (line 229-256) routes to `MahoSidebarContainerView::ShowCommandOverlayForNewTab()` and that container method delegates to `BrowserView::ShowMahoCommandOverlayForNewTab()` per the `chromium_src` comment.
- Confirm `ResolveActionForKeyEvent` does not steal Cmd+L or Cmd+T via Rust shortcut resolution. The two early-return blocks (`VKEY_L` and `VKEY_T`) must remain ahead of `ResolveActionForKeyComboJson`.
- The Cmd+E AI-panel fallback (line 262-266) is unrelated to Zen parity and must not be changed.

**Why this file:** It is the keyboard ingress for the entire palette. If Cmd+L or Cmd+T is intercepted somewhere else (Rust shortcut config, upstream accelerator), the four-state invariant never matters because the overlay never opens.

**Must not change:**

- `BuildKeyComboJson` for either `NativeWebKeyboardEvent` or `ui::KeyEvent`.
- The `LogShortcutTrace` plumbing.
- Any Rust FFI call (`maho_core_resolve_shortcut`) call signature.

**Tests added:**

- Browser-level test (in `maho_command_overlay_interactive_uitest.cc`): Cmd+L on an ordinary site opens the overlay in `kCurrentTab` mode with the URL prefilled and selected. Cmd+T opens in `kNewTab` mode with the field empty.

### 8. `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`

**Phase:** 2 (verification + minor correctness)

**Changes:**

- Confirm `command_bar` (line 289-297) calls `ShowMahoCommandOverlayForSearch(std::string())` and not the current-tab variant. Today it does. Keep it.
- `focus_url_bar` (line 209-210) currently calls `chrome::FocusLocationBar(browser)` (the upstream omnibox focus). When `kMahoUnifiedClickPath` is enabled and the override patch is completed (Phase 3), this path will go through `BrowserView::SetFocusToLocationBar(true)` -> `ShowMahoCommandOverlayForCurrentTab()`. Add a comment in this file naming that interaction so the next reader does not re-route the action.
- Audit `OpenCommandSuggestion` (line 438-578) for disposition correctness specifically when the suggestion has both a `tab_index >= 0` and a `disposition == NEW_FOREGROUND_TAB`. Today the tab-activate branch (line 514-521) wins regardless of disposition. Confirm this matches Zen native behavior of "selecting an existing tab activates it, never duplicates it."

**Why this file:** The action handler is the second-mile of activation. If `command_bar` opened the overlay in the wrong mode, every test below would still pass and the user experience would still be wrong.

**Must not change:**

- The action ID list. No additions, no removals as part of Zen parity.
- `OpenCommandSuggestion` signature.
- Recent-search persistence (`SaveRecentSearch` is in the model, called from the view; not handled here).

**Tests added:**

- Action-handler unit test verifying `command_bar` action -> `kSearch` mode (not `kCurrentTab`).
- Action-handler unit test verifying tab-index activation wins over disposition for an open-tab suggestion.

### 9. `maho-chromium/chromium_src/chrome/browser/ui/views/frame/browser_view.cc`

**Phase:** 3 (click-path parity completion)

This file is the **upstream override comment** (it documents the patch applied to the real `chrome/browser/ui/views/frame/browser_view.cc` in Chromium). The actual patch is applied via the Brave-style `#define` + `#include` mechanism elsewhere; this file is the canonical record of what the patch does. Both must be updated in lockstep.

**Changes:**

- Update the patch description (around line 18-22) and the corresponding real patch in upstream-overlay-land to read:

  ```cpp
  if (is_user_initiated && maho_sidebar_container_ &&
      base::FeatureList::IsEnabled(maho::features::kMahoUnifiedClickPath)) {
    ShowMahoCommandOverlayForCurrentTab();
    return;
  }
  ```

  The `base::FeatureList::IsEnabled` check is the missing piece. Today the patch routes unconditionally on `is_user_initiated && maho_sidebar_container_`, which means the flag is silently always-on for that codepath.

- Update the patch comment text "Disabled by default pending end-to-end QA sign-off" to match whichever default is selected in Decision 2. If enabled-by-default is chosen, the line must read "Enabled by default; flag retained as kill-switch."

- Confirm the three `ShowMahoCommandOverlayFor*` helper definitions (lines 34-75) are unchanged. They are correct.

**Why this file:** It is the only place click-path activation is wired into upstream. The flag declaration without a flag check is an active drift between intent and behavior.

**Must not change:**

- Any sidebar container wiring (lines 9-17, 27-31).
- The `NonClientHitTest` block (lines 93-105).
- The split-view controller plumbing (lines 117-151).
- The `PreHandleKeyboardEvent` injection point (lines 161-171).

**Tests added:** Covered by Phase 3 changes in `maho_unified_click_path_behavior_unittest.cc`.

### 10. `maho-chromium/browser/maho_features.h`

**Phase:** 3

**Changes:**

- If Decision 2 chooses enabled-by-default: update the doc comment and ensure the corresponding `BASE_FEATURE` definition (`maho_features.cc` or wherever it lives) sets `base::FEATURE_ENABLED_BY_DEFAULT`. **Note:** `maho_features.cc` is not in the grounded set of this plan. Verify it exists and update it; if it does not exist, add it as a paired file in Phase 3, but do not introduce any other features there.
- If Decision 2 keeps disabled-by-default: update the comment to clarify "enabling routes location-bar mouse focus through the overlay; left-click already routes through `SetFocusToLocationBar(true)` upstream."

**Why this file:** It is the source of truth for the feature flag. The comment is currently misleading and the default state must align with the override patch.

**Must not change:**

- The feature name `kMahoUnifiedClickPath`. Renaming would break the existing test fixture name `MahoCommandOverlayUnifiedClickPathEnabledTest`.
- The namespace `maho::features`.

**Tests added:** N/A directly.

### 11. `maho-chromium/browser/ui/views/command/maho_command_overlay_view_unittest.cc`

**Phase:** 1 + 2

**New tests required:**

- `EmptyQueryInCurrentTabModeShortCircuits` (Phase 1): seeded results disabled, empty input, mode `kCurrentTab` -> view falls through to recent-searches branch, no backend search debounce timer is started.
- `EmptyQueryInNewTabModeRunsDebouncedBackend` (Phase 1): mode `kNewTab`, empty input -> debounce timer starts, backend search fires.
- `EmptyQueryInSearchModeShortCircuits` (Phase 1): mode `kSearch`, empty input -> recent-searches branch, no backend.
- `FourStateAgreementOnEnterCurrentTab` (Phase 2): seeded `kCurrentTab` open with default selected index 0, Enter -> `OpenCommandSuggestion` called with `seeded_results_[0]` and `CURRENT_TAB` disposition.
- `FourStateAgreementOnEnterNewTab` (Phase 2): blank `kNewTab` open with one backend result, arrow-down to index 0, Enter -> `OpenCommandSuggestion` called with the visible row and `NEW_FOREGROUND_TAB` disposition.
- `OriginModeRoundTripCurrentTabAiCurrentTab` (Phase 2): `kCurrentTab` -> Tab into AI mode -> back out -> Enter resolves to `CURRENT_TAB`.
- `OriginModeRoundTripNewTabSiteSearchNewTab` (Phase 2): `kNewTab` -> Tab into site-search -> activate result resolves to `NEW_FOREGROUND_TAB`.
- `EmptyTextAfterSeededClearShowsRecents` (Phase 2): seeded `kCurrentTab` open, type and clear -> recent-searches visible, seeded rows gone.

**Why this file:** It is the unit harness. It already supports the test-only constructor (`ForTestingTag`) which means most invariants can be exercised without a live `Browser*`.

**Must not change:**

- Existing test names (no churn).
- Existing helpers like `MahoCommandOverlayViewTestBase` if such exists in this file (not verified here; if absent, add minimally).

### 12. `maho-chromium/browser/ui/views/command/maho_command_overlay_interactive_uitest.cc`

**Phase:** 2 + 3

**New tests required:**

- `OrdinarySiteCmdLOpensCurrentTabModeWithUrlPrefilled` (Phase 2): navigate to `https://example.com`, press Cmd+L, assert overlay open, mode `kCurrentTab`, textfield text equals current URL, text is selected.
- `OrdinarySiteCmdTOpensNewTabModeWithEmptyField` (Phase 2): press Cmd+T from any state, assert overlay open, mode `kNewTab`, textfield empty, debounced backend search has been requested.
- `EscapeRestoresFocusToPreviouslyFocusedView` (Phase 2): focus a web-content element, open palette, press Escape, assert focus returned.
- `ClickOutsideRestoresFocus` (Phase 2): same as above, dismiss by activating a different widget.
- `ClickPathFlagOffOmniboxFocusUnchanged` (Phase 3): `kMahoUnifiedClickPath` disabled, focus the location bar via keyboard, overlay does **not** open.
- `ClickPathFlagOnLocationBarFocusOpensCurrentTab` (Phase 3): `kMahoUnifiedClickPath` enabled, focus the location bar via keyboard, overlay opens in `kCurrentTab` mode with the active tab URL prefilled.

**Why this file:** Runtime/browser tests are the only proof Zen-parity survives the real chrome path. Unit tests around `MahoCommandOverlayView` cannot catch shortcut-interceptor or upstream override regressions.

**Must not change:**

- Existing tests at lines 142-1178. Add new tests after the last existing one.
- The little-arc tests (`MahoLittleArcInteractiveTest`).

### 13. `maho-chromium/browser/ui/views/command/maho_unified_click_path_behavior_unittest.cc`

**Phase:** 3

**New tests required:**

- `FlagOff_KeyboardFocusReason_DoesNotOpenOverlay`: `is_user_initiated && focus_reason == kKeyboard`, flag disabled -> overlay must not open. Today, because the flag check is missing, this test would currently fail; landing it captures the regression and the fix in the same change.
- `FlagOff_MouseFocusReason_DoesNotOpenOverlay`: same, mouse focus reason.
- `FlagOn_KeyboardFocusReason_OpensOverlay`: flag enabled -> overlay opens.
- `FlagOn_MouseFocusReason_OpensOverlay`: flag enabled -> overlay opens.
- `FlagOn_RightClickContextMenu_DoesNotOpenOverlay`: parity with the AGENTS-style note in `maho_features.h` that right-click is excluded.
- `FlagOnOff_FocusOnNewBrowserView_DoesNotCrash`: regression cover for `maho_sidebar_container_ == nullptr` early.

**Why this file:** This is the single most direct gate on click-path parity. The existing tests at lines 169-500 already cover several scenarios; we are filling the flag-off matrix.

**Must not change:**

- Existing test names.
- The `MahoUnifiedClickPathBehaviorTest` fixture's friend access (`friend class MahoCommandOverlayController`).

## Phase breakdown with dependencies

```
Phase 0  Decision points (Decision 1, Decision 2)
   |
   v
Phase 1  Empty-query semantics
         Files: maho_command_model.cc, maho_command_model.h (optional),
                maho_command_overlay_view_unittest.cc (Phase 1 tests)
   |
   v
Phase 2  Four-state agreement + activation lifecycle
         Files: maho_command_overlay_view.cc/.h,
                maho_command_overlay_controller.cc/.h,
                maho_shortcut_interceptor.cc (verification),
                maho_command_action_handler.cc (verification),
                maho_command_overlay_view_unittest.cc (Phase 2 tests),
                maho_command_overlay_interactive_uitest.cc (Phase 2 tests)
   |
   v
Phase 3  Click-path flag completion
         Files: chromium_src/.../browser_view.cc (override comment + real patch),
                maho_features.h (and its .cc),
                maho_command_overlay_interactive_uitest.cc (Phase 3 tests),
                maho_unified_click_path_behavior_unittest.cc (Phase 3 tests)
```

Phase 0 must complete (decisions recorded in `docs/`) before Phase 1 begins. Phase 1 and Phase 2 can overlap when, and only when, the empty-query branch in `maho_command_model.cc` is structurally separable from the four-state agreement audit in the view. In practice, Phase 1 lands first because Phase 2 tests depend on the chosen empty-query default.

Phase 3 depends on Phase 2 only for shared test infrastructure (the runtime `IN_PROC_BROWSER_TEST_F` harness). It is otherwise independent.

## Per-file invariants to preserve

These invariants must hold before and after this plan lands. If any of them break during implementation, **stop and report**, do not continue:

1. `kCurrentTab` and `kNewTab` remain semantically distinct enum values. They are not collapsed.
2. `DispositionForMode()` remains the single resolver. No call site outside the view computes its own disposition.
3. `origin_mode_` is set once when entering a sub-mode (`kSiteSearch`, `kAI`) and read by `DispositionForMode()`. No other code reads or writes it.
4. `seeded_results_` is populated only in `InitializeCurrentTabSeedState(...)` and `BuildCurrentTabSeedResults(...)`. Backend results never overwrite seeded rows while `is_seeded_state_ == true`.
5. `kMaxSeededVisibleResults = 5` is enforced wherever seeded rows are rendered.
6. The Cmd+L early-return in `MahoShortcutInterceptor::MaybeHandleKeyEvent` precedes Rust shortcut resolution.
7. The Cmd+T early-return precedes Rust shortcut resolution and routes through the sidebar container.
8. `kMahoUnifiedClickPath` is consulted by **every** override patch that documents it. No silent always-on path.
9. The action handler does not change the action ID list as part of this plan.
10. `MahoCommandOverlayController::RestoreFocusToPreShowView()` is called exactly once per dismissal (Escape, click-outside, parent-widget destruction, `Hide()`).

## Verification plan

### Unit tests

All Phase 1 and Phase 2 unit tests live in `maho_command_overlay_view_unittest.cc`. Build and run via:

```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust -- maho_command_overlay_view_unittest
chromium/src/out/Default/maho_command_overlay_view_unittest --gtest_filter='*FourState*:*EmptyQuery*:*OriginModeRoundTrip*'
```

Phase 3 unit tests live in `maho_unified_click_path_behavior_unittest.cc`:

```bash
chromium/src/out/Default/maho_command_overlay_view_unittest --gtest_filter='*UnifiedClickPath*'
```

(The test target may be a separate binary; if so, build that target instead. Do not edit GN to change targets without explicit user approval.)

### Interactive / browser tests

```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust -- interactive_ui_tests
chromium/src/out/Default/interactive_ui_tests --gtest_filter='MahoCommandOverlay*:*UnifiedClickPath*'
```

### Manual verification matrix

Run on dark mode, ordinary HTTP/HTTPS sites (e.g. `https://news.ycombinator.com`):

| Action | Expected (Zen-parity) |
|---|---|
| Cmd+L on a tab | Overlay opens; current URL is prefilled and fully selected; first row is the seeded current-tab entry |
| Cmd+T from anywhere | Overlay opens; field is empty; backend results / contextual zero-state appears |
| `command_bar` action via menu | Overlay opens in `kSearch` mode; field empty; recent searches visible |
| Cmd+L, type "hn", Tab | Site-search mode entered for the configured `@hn` engine if present |
| Cmd+L, type "ai", Tab | AI mode entered |
| Cmd+L, Enter on default selection | Activates seeded current-tab row; navigation in `CURRENT_TAB` |
| Cmd+T, type a URL, Enter | Opens in `NEW_FOREGROUND_TAB` |
| Cmd+L, Tab into AI, Enter typed text | Opens in `CURRENT_TAB` (origin-mode round-trip) |
| Cmd+T, Tab into site-search, Enter typed text | Opens in `NEW_FOREGROUND_TAB` (origin-mode round-trip) |
| Cmd+L, Escape | Overlay dismissed; focus returns to the previously-focused web content |
| Click outside the overlay | Overlay dismissed; focus returns sensibly |
| With `kMahoUnifiedClickPath` enabled, click in the location bar | Overlay opens in `kCurrentTab` mode |
| With `kMahoUnifiedClickPath` disabled, click in the location bar | Upstream omnibox focus path; no overlay |

### Diagnostics

After every modified file, run `lsp_diagnostics` for that file. Zero errors required.

After the full plan lands, run from `chromium/src`:

```bash
autoninja -C out/Default chrome
out/Default/chrome --use-mock-keychain
```

Smoke-test the manual matrix once more. The build lock at workspace root is required for both build and test runs; do not delete `.build.lock` (see workspace AGENTS.md).

## Out of scope (explicit)

The following are deliberately **not** part of this plan, listed here because each has historically been mistaken for parity work and dragged into command-palette PRs:

- Visual zero-delta against frozen reference screenshots. Owned by `command-palette-current-tab-zero-delta-execution-plan-2026-05-22.md`.
- Removal or redesign of seeded current-tab rows. Maho-specific UX; preserved as-is. **Not Zen parity work.**
- Rust-side ranking, dedup, lazy favicon loading, mode bias logic. Owned by `command-palette-reference-implementation-plan.md` (Phase 1 there).
- WebUI command surface (`chrome://maho-command`). Frozen.
- `MahoLittleArcWindow` and little-arc interactive tests.
- New action IDs in `MahoCommandActionHandler`.
- Geometry, animation, blur, vibrancy, shadow polish on the overlay.
- GN file edits (forbidden without explicit user approval per AGENTS.md).
- Internal-page suppression rules for `chrome://settings`, `chrome://history`, `chrome://newtab`, `chrome://maho-ai`. Owned by the closeout plan.
- Mode-switcher UI removal toward "single surface implicit mode." Owned by the reference plan, Phase 3.

## Honest feasibility statement

This plan is finishable because it does not invent new UX, does not move file ownership, and does not require a Chromium upstream change beyond the existing override file. Its risk surface is concentrated in one file (`maho_command_overlay_view.cc`), one invariant (four-state agreement), and one flag (`kMahoUnifiedClickPath`).

What it cannot promise: that no further Zen-native behavior gap will surface after Phase 3 lands. If one does, it must be reopened as a named follow-up with file ownership, not bundled into this plan.

What it can promise: after Phase 3, the three things this codebase has historically gotten wrong, namely empty-query behavior across modes, four-state disagreement at activation, and a click-path flag that did nothing, will all be either fixed or explicitly chosen and recorded.
