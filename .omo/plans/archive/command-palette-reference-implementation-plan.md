# Arc-like Command Palette: Final Implementation Plan

## Goal

Transform Maho's command palette into an Arc-style unified surface. The native Views overlay is the production path. Ship incremental improvements that make the palette faster, smarter, and closer to a single contextual surface, without attempting full omnibox replacement.

## Confirmed Current State

**Views overlay (production surface):**
- Lives under `maho-chromium/browser/ui/views/command/`.
- Controller, overlay view, result row view, model, and action handler are wired and functional.
- Keyboard routing (shortcut interception) to the overlay works today.

**WebUI surface (secondary, non-primary):**
- `maho-chromium/browser/ui/webui/maho_command/` and `maho-chromium/browser/resources/maho_command/` exist.
- This surface is not the production path and receives no further investment under this plan.

**Click-path unification (NOT verified as shipped):**
- Whether clicking the visible omnibox/address-bar area routes to the command palette overlay is unconfirmed. This is an open product and implementation decision, not existing behavior.

**Rust-side ranking:**
- `maho-core` provides ranked results. The C++ `CapResults()` bucket logic currently re-sorts and caps before display, undermining Rust ordering.

**Tab results and favicons:**
- `BuildTabsJson()` encodes favicon bytes directly into the hot-path payload. This is a latency concern at scale.

## Product Decisions to Make Upfront

1. **Click-path unification.** Should clicking the address-bar area open the command palette overlay instead of focusing the omnibox? This must be an explicit, tested decision, not an assumption. Recommendation: yes, with a feature flag so it can ship independently.
2. **Mode presentation.** Move toward one surface with implicit disposition/context bias (typing a URL biases navigation results; typing a command biases actions). This does NOT mean deleting all internal mode distinctions. It means the user sees one palette that adapts, not separate "tabs" or "URL" or "commands" modes.
3. **WebUI surface fate.** Freeze `chrome://maho-command`. No new features, no migration effort. Mark it deprecated in code comments. If a future product need arises, re-evaluate then.

## Scope

- Stabilize overlay controller lifecycle (geometry, focus, dismissal).
- Replace `CapResults()` bucket logic with simple truncation so Rust-ranked ordering is preserved end-to-end.
- Implement cross-type URL deduplication (same URL from history, bookmarks, and open tabs collapses to one row with combined signals).
- Remove favicon bytes from `BuildTabsJson()` hot path; load lazily on row render.
- Unify shell presentation toward implicit-mode single surface.
- Add targeted tests for overlay lifecycle, ranking preservation, and dedup correctness.
- Add lightweight latency tracing (trace events or histograms) for palette open-to-render and query-to-results paths.
- If click-path unification is approved: implement address-bar click routing behind a feature flag.

## Out of Scope

- Full omnibox removal or replacement.
- Changes to `MahoCommandModel` contract or `MahoCommandActionHandler` contract beyond dedup and cap fixes.
- WebUI redesign, migration, or feature parity work.
- Grouping, ordering, or selection model overhauls (beyond preserving Rust rank order).
- Anchored top-chrome / location-bar surface rearchitecture (evaluate only if product direction demands it after this plan ships).

## Phased Implementation Plan

### Phase 1: Ranking Fidelity and Latency Foundation

**Goal:** Results the user sees match Rust-ranked order, and we can measure where time goes.

1. Replace `CapResults()` bucket logic with a flat truncation at the display limit. Rust ranking flows through unmodified.
2. Implement cross-type URL dedup: collapse duplicate URLs across result types, keeping the highest-ranked instance and merging type badges.
3. Remove favicon byte encoding from `BuildTabsJson()`. Instead, pass tab IDs or URLs; load favicons lazily when rows render (async image fetch, placeholder on miss).
4. Add trace events: palette-open-to-first-paint, keystroke-to-results-updated, total-results-fetch-time.
5. Add unit tests for truncation behavior, dedup correctness, and favicon-less payload construction.

**Files:**
- `maho-chromium/browser/ui/views/command/maho_command_model.cc` (cap/dedup logic)
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc` (lazy favicon)
- `maho-chromium/third_party/maho/` bridge code if tab JSON construction lives there

### Phase 2: Overlay Controller Stabilization

**Goal:** Overlay opens, positions, and dismisses predictably every time.

1. Normalize geometry: palette always appears at a fixed position relative to the browser window (top-center, consistent width).
2. Tighten focus: opening the palette claims focus atomically; dismissal returns focus to the previously-focused web content or UI element.
3. Make hide/dismiss deterministic: Escape, click-outside, and focus-loss all dismiss cleanly without flicker or double-fire.
4. Add integration tests: open/close cycles under various window states (fullscreen, split, minimized-then-restored).

**Files:**
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc`
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.h`

### Phase 3: Single-Surface Mode Presentation

**Goal:** User perceives one palette that adapts to context, not discrete modes.

1. Remove visible mode-switcher UI if present. Input text and context determine result disposition implicitly.
2. Bias logic: URL-shaped input ranks navigation results higher; bare words rank commands and tabs higher; `>` prefix (if kept) forces command-only. All of this happens inside Rust ranking, not C++ re-sorting.
3. Update row presentation to show result type as a subtle badge rather than grouping results by type header.
4. Ensure keyboard navigation flows linearly across all result types (no internal group boundaries that trap arrow keys).

**Files:**
- `maho-chromium/browser/ui/views/command/maho_command_overlay_view.cc` (mode UI removal, layout)
- `maho-chromium/browser/ui/views/command/maho_command_result_row_view.cc` (badge, layout)
- `maho/crates/maho-core/` (bias/disposition logic if not already there)

### Phase 4: Click-Path Unification (Feature-Flagged)

**Goal:** Clicking the address-bar area opens the command palette instead of focusing the omnibox.

This phase requires the product decision from above. If approved:

1. Add a feature flag (`kMahoCommandPaletteClickPath` or similar).
2. Intercept mouse-down on the omnibox/location-bar view area when the flag is enabled; route to `MahoCommandOverlayController::Show()`.
3. Pre-populate the palette with the current page URL so the user can still see/copy/edit it.
4. Preserve right-click and drag-select behaviors on the location bar (don't break copy-URL workflows).
5. Add regression tests: click opens palette, URL is visible, Escape restores previous state.

**Files:**
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc` (click interception)
- `maho-chromium/browser/ui/views/command/maho_command_overlay_controller.cc` (pre-populate support)
- Feature flag definition in `maho-chromium/browser/` feature list

### Phase 5: Polish and Ship

1. Visual pass on overlay shell: rounded corners, shadow, animation (open/close slide or fade).
2. Accessibility audit: screen reader announces palette open/close, result count, selected item.
3. Performance validation against latency traces from Phase 1. Target: <50ms open-to-first-paint, <100ms keystroke-to-results on a 10k-history profile.
4. Freeze `chrome://maho-command` WebUI. Add deprecation comment. Remove from any new-tab or internal linking.

## Parallelizable Workstreams

These can proceed concurrently with Phase 1/2:

| Workstream | Owner concern | Notes |
|---|---|---|
| Rust bias/disposition logic | `maho-core` | Can develop and test in isolation with mock queries |
| Lazy favicon loading infra | Result row view | Independent of ranking changes |
| Latency tracing plumbing | Controller + model | Observability, no behavioral change |
| Click-path product decision | Product/design | Unblocks Phase 4 without blocking Phases 1-3 |
| Integration test harness | Test infra | Reusable across all phases |

## Verification Gates

Each phase must pass before the next begins:

| Phase | Gate |
|---|---|
| 1 | Unit tests pass. Results displayed match Rust rank order exactly (no re-sort). Dedup collapses duplicates. Favicon removal doesn't regress row rendering. Trace events fire. |
| 2 | Overlay opens/closes 100 times without geometry drift, focus leak, or crash. Integration tests green. |
| 3 | Manual QA confirms single-surface feel. No mode-switcher visible. Arrow keys traverse all results linearly. Rust bias logic has unit test coverage. |
| 4 | Feature flag off: existing omnibox behavior unchanged. Flag on: click opens palette with URL pre-populated. Right-click still works. Regression tests pass. |
| 5 | Latency targets met. Accessibility audit clean. WebUI frozen. |

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| `CapResults()` removal exposes Rust ranking bugs previously masked | Wrong result order visible to user | Add golden-file ranking tests in `maho-core` before removing C++ caps |
| Lazy favicon loading causes visible pop-in | Janky row rendering | Use a generic placeholder icon with smooth fade-in; preload favicons for top-5 results |
| Click-path unification breaks power-user URL workflows | Users can't copy/edit URLs easily | Feature flag + pre-populate palette with current URL + preserve right-click |
| Overlay controller stabilization reveals deep focus bugs in Chromium Views | Unbounded debugging | Timebox to 1 week; if systemic, ship with known-issue list and workarounds |
| Single-surface mode confuses users expecting explicit mode separation | Adoption friction | Keep `>` prefix as an explicit command-mode escape hatch; ship implicit bias gradually |
| WebUI deprecation breaks internal test or automation tooling | CI failures | Audit references to `chrome://maho-command` before freezing; add redirect or stub |

## First Shippable Milestone

**Phases 1 + 2 together form the first shippable milestone.**

After these land, the command palette:
- Shows results in correct Rust-ranked order (no bucket re-sort).
- Deduplicates cross-type URLs.
- Loads favicons without blocking the results payload.
- Has latency tracing active for ongoing measurement.
- Opens, positions, and dismisses reliably.

This milestone is internally shippable (dogfood) without any product decision on click-path unification or mode presentation changes. It de-risks the foundation before the more visible UX shifts in Phases 3-5.
