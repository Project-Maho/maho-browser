# Address Bar Utility Panel — Arc Reference Parity Implementation Plan

**Status:** Ready for execution
**Surface:** `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_*`
**Mode:** Phase 1 (Arc-faithful skeleton with Share wired and existing settings rows reused)
**Verification:** Unit tests only + canonical `python3 maho-chromium/build/scripts/build_maho.py`

---

## 1. Goal & Scope

Bring the address-bar utility panel's structure, ordering, headers, and visual zones to match the Arc reference, replacing the prior Brave-style three-zone layout. After Phase 1 the panel should look and feel like Arc's address-bar utility popover, with one wired toolbar action and the remaining structure populated from existing builders rather than invented stubs.

### What ships in Phase 1

A panel that renders, top to bottom:

1. A **4-icon toolbar row** at the top (no header, no divider above).
2. An **`Extensions` section** with a model-driven header, one chip per installed extension via `BuildInstalledExtensions(const std::string&, Browser*)`, plus a trailing `+` add-extension chip. Empty state is a fallback chip plus the `+` chip, never a lone `+`.
3. A **`Settings` section** with a model-driven header containing exactly the rows produced by reusing existing builders:
   - `BuildDeveloperModeAction(const base::Value*)` for the Developer Mode row, wired with the same semantics it has today.
   - A new Phase-1 row builder for `Block Ads & Trackers` that calls existing synchronous `maho::core::GetSiteExceptions(...)` against the active origin. `BuildShieldsAction(Browser*)` is retained.
   - `BuildAutoPipSettingsRow(content::WebContents*, Profile*)` for the Automatic Picture-In-Picture row, reused as-is.
4. A **footer** with the existing secure pill on the left and a visible-but-non-interactive ellipsis (`…`) glyph on the right.

### Phase 1 wiring contract

- **Toolbar:** Share is the only wired toolbar action; the other three icons are honest disabled stubs. Real builder is `BuildShareAction(Browser*)`. No fake callbacks, no placeholder mappings.
- **Extensions:** Per-extension chip callbacks reuse what `BuildInstalledExtensions(json, browser)` already emits. They currently route to the same extensions settings pane; that is acceptable and must be documented in code comments, not hidden.
- **Settings rows:** No new row semantics are invented. Developer Mode and Automatic Picture-In-Picture reuse `BuildDeveloperModeAction(...)` and `BuildAutoPipSettingsRow(...)` exactly as they exist; their current behavior, labels, icons, and enabled state determination flow through unchanged.
- **Block Ads & Trackers row:** A new Phase-1 helper builds the row from existing synchronous `maho::core::GetSiteExceptions(...)` plus the current-origin check. `BuildShieldsAction(Browser*)` is retained; the new row builder is what gets surfaced in Phase 1 and is independently unit-tested.
- **Footer ellipsis:** View-only. No menu, no callback, no overflow contents in Phase 1. The footer struct is unchanged.

### Public API stability

Public builder signatures stay byte-identical so the sidebar consumer is not forced to fork or re-test in this phase:

- `BuildMahoLocationBarUtilityPanelModel(LocationBar*)`
- `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)`
- `BuildShareAction(Browser*)`
- `BuildDeveloperModeAction(const base::Value*)`
- `BuildAutoPipSettingsRow(content::WebContents*, Profile*)`
- `BuildInstalledExtensions(const std::string&, Browser*)`
- `BuildExtensionsSummaryAction(...)`
- `BuildShieldsAction(Browser*)`
- `BuildFooter(LocationBar*)`
- `BuildFooter(Browser*)`
- `BuildFooterFromModel(LocationBarModel*)`

---

## 2. Non-Goals

The following are explicitly **out of scope** for Phase 1. Do not implement them, do not stub them as if they were wired, and do not include them in tests.

- Real behavior for the three non-Share toolbar icons.
- Any overflow menu, popover, or callback behind the footer ellipsis.
- A footer struct change to carry an overflow action.
- Reload, Print, or Copy-Link as toolbar mappings (explicitly rejected).
- Per-extension distinct callbacks (current shared route is acceptable).
- Sidebar-specific builder forks.
- Integration tests, interactive UI tests as required acceptance gates, or manual QA gates.
- New Rust crates, new FFI surface, or any change to `maho-core` / `maho-ffi`.
- Adding `relay/` or `mail/` as workspace members.
- Inventing new toggle behavior for `BuildDeveloperModeAction(...)`.
- Treating `BuildAutoPipSettingsRow(...)` as a placeholder or disabled stub.
- Hard removal of legacy `MahoUtilityPanelZoneId` / `MahoUtilityPanelZoneStyle` values; that cleanup is optional and lives outside Phase 1's must-ship list.

---

## 3. Grounded Current-State Diagnosis

Files in scope:

- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider_unittest.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.{h,cc}`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.{h,cc}`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view_interactive_uitest.cc`

Today the provider builds three Brave-shaped zones with the existing enum:

- `MahoUtilityPanelZoneId::kPrimaryActions` with `kGrid` style, populated by Boost / Shields / Site Settings / Cookies & Site Data.
- `MahoUtilityPanelZoneId::kInlineActionStrip` with `kInlineRow` style, populated by Share / Reader / Zoom +/ -/ reset.
- `MahoUtilityPanelZoneId::kSecondaryShortcuts` with `kListRow` style, populated by `BuildExtensionsSummaryAction(raw_extensions, browser)`.
- `MahoUtilityPanelZoneId::kStatus` is reserved but unused.

The action struct already carries `enabled`, `data_state`, `callback`, and a `sf_symbol_name`. The zone struct has `id`, `style`, and `items` only. There is **no header field** on the zone today, and the view has no concept of a section header.

`BuildMahoLocationBarUtilityPanelModel(LocationBar*)` and `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` both delegate to `internal::PopulateCommonModelData(...)` and then attach the footer via `internal::BuildFooter(...)`. The footer struct has no overflow field and no callback for an ellipsis affordance.

Two consumers exist:
- The address-bar bubble (primary surface).
- The sidebar (via `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)`), which embeds the same model.

This means any provider/view change lands in the sidebar runtime as well. Risk is bounded because the public builder signatures stay stable; the visible blast radius is provider, view, and tests.

The shields/extensions data paths are already in place:
- `BuildShieldsAction(Browser*)` exists at `maho_location_bar_utility_panel_provider.cc:521`. It is **retained as-is** in Phase 1 but is not surfaced in the visible assembly path. The new `Block Ads & Trackers` row is built from a small Phase-1 helper that calls `maho::core::GetSiteExceptions(...)` directly with the current origin, mirroring the synchronous pattern already used by `maho_shield_bubble_coordinator.cc` and `maho_page_info_ui.cc`.
- `BuildInstalledExtensions(json, browser)` already emits per-extension actions with callbacks routing to the extensions settings pane.
- `BuildDeveloperModeAction(const base::Value*)` and `BuildAutoPipSettingsRow(content::WebContents*, Profile*)` already produce ready-to-render rows; the Settings zone reuses them verbatim.

Existing unit tests in `maho_location_bar_utility_panel_provider_unittest.cc` cover `BuildInstalledExtensions`, `BuildExtensionsSummaryAction`, and `BuildFooterFromModel` paths. They will be extended for the new zone shape, and they will keep covering retained builders.

---

## 4. Target Reference Shape

The Phase-1 panel renders four logically distinct sections, top to bottom, with clear vertical rhythm matching Arc:

```
┌────────────────────────────────────────────────┐
│  [share]  [wand.and.stars]  [camera]  [vf]    │   ← Toolbar zone (no header)
│                                                │
│  Extensions                                    │   ← Extensions header (model-driven)
│  [chip] [chip] [chip] [+]                      │   ← Extension chips + add chip
│                                                │
│  Settings                                      │   ← Settings header (model-driven)
│  [icon]  Developer Mode                        │   ← BuildDeveloperModeAction(...)
│  [icon]  Block Ads & Trackers           [n]    │   ← New Phase-1 helper using GetSiteExceptions
│  [icon]  Automatic Picture-In-Picture          │   ← BuildAutoPipSettingsRow(...)
│                                                │
│  🔒 maho.com                              …    │   ← Footer (ellipsis is view-only)
└────────────────────────────────────────────────┘
```

### Toolbar icon mapping (Phase 1)

| Slot | SF Symbol               | Wiring                                 |
|------|-------------------------|----------------------------------------|
| 1    | `square.and.arrow.up`   | **Wired via `BuildShareAction(Browser*)`** |
| 2    | `wand.and.stars`        | Disabled stub                          |
| 3    | `camera`                | Disabled stub                          |
| 4    | `viewfinder`            | Disabled stub                          |

The `link / arrow.clockwise / printer` mapping floated earlier is **rejected** and must not appear anywhere in code or tests.

---

## 5. Key Design Decisions

These are the locked-in decisions from the converged source of truth. Treat them as constraints, not as suggestions.

1. **Introduce new semantic zone IDs/styles for the Arc surface.** Add `kToolbar`, `kExtensions`, and `kSettings` to `MahoUtilityPanelZoneId`, and add matching styles (e.g. `kToolbarRow`, `kChipRow`, `kSettingsList`) to `MahoUtilityPanelZoneStyle` if a new layout primitive is required. Existing values (`kPrimaryActions`, `kInlineActionStrip`, `kSecondaryShortcuts`, `kStatus`, and their styles) **stay** in the enum during Phase 1 to minimize churn for callers that still reference them. Any cleanup of unused legacy enum values is an optional later step, not a Phase-1 must-ship.

2. **Model-driven section headers** via an additive `header_label` field on `MahoUtilityPanelZone`. Headers are never hardcoded in the view; the view renders only what the model carries. Use a single `std::u16string header_label;` defaulting to the empty string. The toolbar zone leaves it empty; the extensions and settings zones populate it. The plan stays consistent on this single representation.

3. **Footer ellipsis is view-only in Phase 1.** No struct change for an overflow callback or overflow items. The visible affordance signals where future overflow will land; behavior is intentionally absent.

4. **Toolbar Phase 1 is 1 wired + 3 honest stubs in this exact order:** Share, then `wand.and.stars`, then `camera`, then `viewfinder`. Disabled stubs must visually present as disabled (reduced opacity, no hover/press), must not register a click handler, and must not log a "not implemented" toast. They are inert by construction. Share is built by `BuildShareAction(Browser*)` and routed through the same handler the existing toolbar share path uses today; no new entry point is invented.

5. **Block Ads & Trackers row is real.** A new Phase-1 row helper, e.g. `internal::BuildBlockAdsAndTrackersRow(Browser*)`, calls existing synchronous `maho::core::GetSiteExceptions(...)` and applies the current-origin check. No new Rust/FFI APIs. `BuildShieldsAction(Browser*)` is retained but not assembled into the Phase-1 visible path.

6. **Optional blocked-count enrichment is gated.** Only attach `MahoAdBlockTabHelper::blocked_count()` to the row label/badge if (a) the read is synchronous and cheap, and (b) it can be exercised in a unit test by injecting a fake `WebContents` / helper. If either gate fails, ship without the count and revisit later. No rework owed.

7. **Extensions section is composed from existing `BuildInstalledExtensions(const std::string&, Browser*)`.** All chips share today's settings-pane callback; that is acceptable and documented in code. The trailing `+` chip is added by the extensions section assembler, not by `BuildInstalledExtensions` itself.

8. **Empty extensions state uses a fallback chip plus `+`.** Never a lone `+`. The fallback chip is non-interactive and uses the same disabled visual treatment as the disabled toolbar icons.

9. **`BuildExtensionsSummaryAction(...)` is retained but not surfaced in Phase 1.** It remains exported and unit-tested to keep the builder honest, but it is not assembled into the Phase-1 zones.

10. **Settings rows reuse existing builders verbatim.**
    - Developer Mode = `BuildDeveloperModeAction(parsed_settings)` with the same semantics it already has. The plan does not invent a new toggle, does not wire any new behavior, and does not redescribe the row as "wired if available". Whatever the row does today flows through unchanged.
    - Automatic Picture-In-Picture = `BuildAutoPipSettingsRow(web_contents, profile)`, reused unchanged. It is not a placeholder, not a stub, not disabled by default. Whatever state and label it produces today flow through to the Settings zone.

11. **Public builder signatures unchanged.** Sidebar consumer must compile and pass its existing tests without edits in Phase 1.

12. **Verification is unit tests + canonical build only.** Interactive UI tests, integration tests, and manual QA are not acceptance gates. The existing interactive uitest at `maho_location_bar_utility_icon_view_interactive_uitest.cc` may need minimal compile-only updates if a referenced symbol moves or renames, framed strictly as compile/seed maintenance, not as required validation.

---

## 6. File-by-File Implementation Plan

Each subsection lists the file, the change, and the rationale.

### 6.1 `maho_location_bar_utility_panel_provider.h`

**Change A — Extend `MahoUtilityPanelZoneId` additively:**

```cpp
enum class MahoUtilityPanelZoneId {
  // Legacy values — retained during Phase 1 to limit churn.
  kPrimaryActions,
  kInlineActionStrip,
  kSecondaryShortcuts,
  kStatus,

  // New Arc-shape zones for the Phase-1 surface.
  kToolbar,
  kExtensions,
  kSettings,
};
```

**Change B — Extend `MahoUtilityPanelZoneStyle` additively** with any new layout primitives the new zones require (e.g. `kToolbarRow`, `kChipRow`, `kSettingsList`). Legacy styles (`kGrid`, `kInlineRow`, `kListRow`) stay.

**Change C — Add the additive header field on the zone struct:**

```cpp
struct MahoUtilityPanelZone {
  MahoUtilityPanelZone();
  MahoUtilityPanelZone(const MahoUtilityPanelZone&);
  MahoUtilityPanelZone& operator=(const MahoUtilityPanelZone&);
  ~MahoUtilityPanelZone();

  MahoUtilityPanelZoneId id = MahoUtilityPanelZoneId::kPrimaryActions;
  MahoUtilityPanelZoneStyle style = MahoUtilityPanelZoneStyle::kGrid;
  std::u16string header_label;  // NEW — empty for header-less zones.
  std::vector<MahoLocationBarUtilityPanelAction> items;
};
```

The field is a plain `std::u16string` defaulting to empty. Empty means "no header". This stays consistent with the rest of the plan; the plan does not also describe a `std::optional` form.

**Change D — Public builder signatures unchanged.** A header comment reaffirms that `BuildMahoLocationBarUtilityPanelModel(LocationBar*)`, `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)`, `BuildShareAction(Browser*)`, `BuildDeveloperModeAction(const base::Value*)`, `BuildAutoPipSettingsRow(content::WebContents*, Profile*)`, `BuildInstalledExtensions(const std::string&, Browser*)`, `BuildExtensionsSummaryAction(...)`, `BuildShieldsAction(Browser*)`, `BuildFooter(LocationBar*)`, `BuildFooter(Browser*)`, and `BuildFooterFromModel(LocationBarModel*)` keep their current shape.

### 6.2 `maho_location_bar_utility_panel_provider.cc`

**Change A — Toolbar zone assembly:**

Add `internal::BuildToolbarZone(Browser*)` returning a `MahoUtilityPanelZone` with:

- `id = kToolbar`, `style = kToolbarRow` (or whichever new style is introduced), `header_label = u""`.
- Four `MahoLocationBarUtilityPanelAction` entries in this exact order: `BuildShareAction(browser)`, then a `wand.and.stars` stub, then a `camera` stub, then a `viewfinder` stub.
- Stubs use `enabled = false`, `callback = base::RepeatingClosure()`, and `sf_symbol_name` set to the literals above. The existing `enabled` field on `MahoLocationBarUtilityPanelAction` (already present at provider.h:45) is sufficient; no new fields on the action struct are required.

**Change B — Extensions zone assembly:**

Add `internal::BuildExtensionsZone(const std::string& extensions_json, Browser*)` that:

1. Calls `BuildInstalledExtensions(extensions_json, browser)`.
2. If the result represents an empty install set, prepend a single fallback chip (label like `No extensions`, `enabled = false`, no callback). Empty here means the underlying list is empty after the existing builder runs; the new code does not duplicate the unavailable-state handling that already lives inside `BuildInstalledExtensions`.
3. Append a trailing add-extension chip (`+`, callback to the existing extensions settings pane / `chrome://extensions`, mirroring the path the empty-state add chip already uses inside `BuildInstalledExtensions`).
4. Set `id = kExtensions`, `header_label = u"Extensions"`.

`BuildExtensionsSummaryAction(...)` is **not** called from this path. It remains exported and unit-tested.

**Change C — Settings zone assembly:**

Add `internal::BuildSettingsZone(Browser*)` that emits exactly the rows below, in this order, all via existing builders:

1. Developer Mode — produced by `BuildDeveloperModeAction(parsed_settings)` with `parsed_settings` obtained the same way it is today (no changes to how that input is sourced). Whatever the action's current behavior, label, icon, and enabled state are, those flow through unchanged.
2. Block Ads & Trackers — produced by a new Phase-1 helper, e.g. `internal::BuildBlockAdsAndTrackersRow(Browser*)`, that calls `maho::core::GetSiteExceptions(...)` and the current-origin check synchronously. The label is `Block Ads & Trackers`. `BuildShieldsAction(Browser*)` is unchanged and stays in the file; the new helper is what gets pushed into the Settings zone.
3. Automatic Picture-In-Picture — produced by `BuildAutoPipSettingsRow(web_contents, profile)` with the same `web_contents` and `profile` lookups it uses today, reused as-is. It is not framed as a placeholder.

**Optional blocked-count enrichment for row 2:** a small helper `internal::TryGetBlockedCountForActiveTab(Browser*)` returning `std::optional<int>`. If the helper is synchronous, cheap, and unit-testable by stubbing the `WebContents` chain, attach the count as a trailing badge/label suffix on the Block Ads & Trackers row. Otherwise, omit it. This decision is made at implementation time on cost grounds; no rework is owed if it is omitted.

Set `id = kSettings`, `header_label = u"Settings"`.

**Change D — Top-level builder rewrite:**

`BuildMahoLocationBarUtilityPanelModel(LocationBar*)` and `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` route through a new `internal::PopulateArcShapeModelData(...)` (replacing or co-existing with `internal::PopulateCommonModelData(...)` for Phase 1; the simplest path is to replace it for the Arc-shape callers and keep the legacy populator only if a non-Phase-1 caller still needs it).

```cpp
void PopulateArcShapeModelData(
    MahoLocationBarUtilityPanelModel& model,
    Browser* browser,
    const std::string& extensions_json) {
  model.zones.push_back(BuildToolbarZone(browser));
  model.zones.push_back(BuildExtensionsZone(extensions_json, browser));
  model.zones.push_back(BuildSettingsZone(browser));
}

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModel(
    LocationBar* location_bar) {
  MahoLocationBarUtilityPanelModel model;
  Browser* browser = location_bar ? location_bar->GetBrowser() : nullptr;
  std::string extensions_json = /* same lookup as today */;
  internal::PopulateArcShapeModelData(model, browser, extensions_json);
  model.footer = internal::BuildFooter(location_bar);
  return model;
}

MahoLocationBarUtilityPanelModel BuildMahoLocationBarUtilityPanelModelForBrowser(
    Browser* browser) {
  MahoLocationBarUtilityPanelModel model;
  std::string extensions_json = /* same lookup as today */;
  internal::PopulateArcShapeModelData(model, browser, extensions_json);
  model.footer = internal::BuildFooter(browser);
  return model;
}
```

`BuildExtensionsSummaryAction(...)` is no longer called from these top-level builders.

**Change E — Footer left untouched.** `BuildFooter(LocationBar*)`, `BuildFooter(Browser*)`, and `BuildFooterFromModel(LocationBarModel*)` are unchanged. The view will draw the disabled ellipsis purely from view-side state.

### 6.3 `maho_location_bar_utility_panel_provider_unittest.cc`

Existing tests keyed off the legacy zone IDs and the legacy summary-in-zone layout. Extend as follows. **Unit tests only.**

**Retained:**
- `BuildInstalledExtensions` empty / non-empty / multiple cases (no behavioral change inside this builder).
- `BuildExtensionsSummaryAction` for fallback and multi-extension inputs (kept since the builder is retained).
- `BuildFooterFromModel` null and live-host paths.
- `BuildShieldsAction` tests stay; the symbol is still exported.
- `BuildDeveloperModeAction` tests stay.
- `BuildAutoPipSettingsRow` tests stay.
- `BuildShareAction` tests stay.

**New tests:**

1. `BuildModelHasThreeArcShapeZonesInOrder` — toolbar, extensions, settings, in that order, in addition to whatever else `PopulateArcShapeModelData` puts on the model.
2. `ToolbarZoneHasFourActionsInExactOrder` — Share, then `wand.and.stars`, then `camera`, then `viewfinder`. Share is the only one with a non-null callback and `enabled = true`.
3. `ToolbarZoneHasNoHeader` — `header_label.empty()`.
4. `ExtensionsZoneHeaderLabelIsExtensions`.
5. `ExtensionsZoneEmptyJsonProducesFallbackPlusAddChip` — first item is the disabled fallback, last item is the `+` add-extension chip, total count is 2.
6. `ExtensionsZoneNonEmptyJsonAppendsOnlyAddChip` — extension chips first, `+` last, no fallback prepended.
7. `SettingsZoneHeaderLabelIsSettings`.
8. `SettingsZoneRowsInOrder` — Developer Mode (delegated to `BuildDeveloperModeAction`), Block Ads & Trackers, Automatic Picture-In-Picture (delegated to `BuildAutoPipSettingsRow`).
9. `SettingsBlockAdsRowUsesGetSiteExceptionsAndCurrentOrigin` — verifies the new row helper calls the synchronous `maho::core::GetSiteExceptions(...)` path. If existing shields tests already provide fakes, reuse them; otherwise assert label and `enabled` state under a fake browser.
10. `SettingsDeveloperModeRowDelegatesToBuildDeveloperModeAction` — assert the row equals what `BuildDeveloperModeAction(parsed_settings)` produces for the same input. No claim about specific row behavior beyond delegation.
11. `SettingsAutoPipRowDelegatesToBuildAutoPipSettingsRow` — assert the row equals what `BuildAutoPipSettingsRow(web_contents, profile)` produces for the same input. No claim about whether it is enabled or disabled; reuse the builder's own behavior.
12. `BlockedCountEnrichmentAttachesWhenAvailable` — only added if the enrichment ships; otherwise omitted.
13. `BuildExtensionsSummaryActionStillExportedAndExercised` — keep existing summary tests so the symbol remains live and Phase-1 assembly does not silently drop coverage.

All tests live in the existing `maho_location_bar_utility_panel_provider_unittest.cc` file and run under the existing test target.

### 6.4 `maho_location_bar_utility_panel_view.h`

Add the new visual layout primitives the new zones need. Whether this requires new `DisplayMode` values or new local layout helpers is a view-internal decision; the plan does not introduce any new field on `MahoLocationBarUtilityPanelAction` to carry display-mode hints. Rendering is driven by the zone `id` / `style` and the existing per-action fields (`sf_symbol_name`, `enabled`, `callback`, etc.).

Specifically:

- A header rendering helper, e.g. `RenderZoneHeader(const std::u16string&)`, that draws nothing when the label is empty and draws a styled section header otherwise.
- Per-zone layout helpers for the toolbar row, the chip row, and the settings list. These are private to the view and do not change the public action struct.
- A helper for rendering the disabled view-only ellipsis affordance in the footer.

If the view today carries a `DisplayMode` enum, additions to it stay additive in Phase 1; legacy values are not removed in Phase 1. Removal is an optional later cleanup gated on a fresh `git grep` of remaining consumers.

### 6.5 `maho_location_bar_utility_panel_view.cc`

**Change A — Zone iteration becomes header-aware:**

When iterating zones, if `zone.header_label` is non-empty, render the header label using a new section-header style (left-aligned, semi-bold, secondary text color, vertical padding matching the reference). Toolbar zone has an empty header and is rendered flush.

**Change B — Per-zone layout primitives:**

- `kToolbar` zone → horizontal row of 4 icon buttons, evenly spaced, fixed icon size matching the reference. Disabled icons render with reduced opacity, no hover/press states, and **no click target is registered** (see Change E).
- `kExtensions` zone → horizontal flex row of chips, wrapping if needed, with the trailing `+` chip styled distinctly (outlined / muted) and the optional disabled fallback chip styled identically to disabled toolbar icons.
- `kSettings` zone → vertical list of rows, each row honoring the existing per-action fields. The view does not invent extra row semantics for Developer Mode or Automatic Picture-In-Picture; whatever those builders set on the action is what gets drawn.

**Change C — New constants block** for the Arc-shape layout: `kToolbarRowHeight`, `kToolbarIconSize`, `kToolbarIconSpacing`, `kChipRowSpacing`, `kChipHeight`, `kChipCornerRadius`, `kChipHorizontalPadding`, `kAddChipWidth`, `kSettingsRowHeight`, `kSettingsRowHorizontalPadding`, `kSettingsIconSize`, `kSettingsRowCornerRadius`, `kZoneHeaderTopPadding`, `kZoneHeaderBottomPadding`, `kZoneHeaderFontSize`. Brave-era constants stay until their last consumer is removed; that removal is an optional later cleanup.

**Change D — Footer:**

Render the existing secure pill on the left exactly as today. On the right, render a static `…` glyph using a disabled label/icon style. **No mouse handler is attached.** The affordance is drawn purely view-side, not via a model entry; the footer struct is unchanged.

**Change E — Disabled-action contract:**

Any action with `enabled = false` is rendered with the disabled visual treatment, and **no click target** is registered. This applies uniformly to the three non-Share toolbar icons and the empty-extensions fallback chip. Settings rows that come from `BuildDeveloperModeAction(...)` and `BuildAutoPipSettingsRow(...)` carry their own enabled state; the view honors it without overriding.

### 6.6 `maho_location_bar_utility_bubble_coordinator.{h,cc}`

No structural change. Confirm the coordinator still hands a `LocationBar*` (or `Browser*`) to the provider and embeds the resulting view. The only edits expected are mechanical updates if a referenced enum value or display mode is renamed. If neither applies, this file is untouched.

### 6.7 `maho_location_bar_utility_icon_view.{h,cc}` and `maho_location_bar_utility_icon_view_interactive_uitest.cc`

The icon view is the address-bar entry point button; its rendering is independent of the panel's internal layout. Expected edits:

- None to the icon view source.
- The interactive uitest may reference enum values or display modes for assertions or seeds. Compile-only updates are sanctioned **only if required to keep the file compiling**. No new interactive coverage is added. Interactive tests are not acceptance gates for Phase 1.

### 6.8 Sidebar (read-only verification)

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.{h,cc}`, `maho_sidebar_view.cc`, and `maho_sidebar_search_view.cc` consume `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` and `BuildFooterFromModel(...)`. Public signatures are stable, so no edits are expected. Verification is build-only.

---

## 7. TDD / Unit-Test Plan

The flow is test-first per phase. Each phase below has its tests written first, then the production change, then the test must pass. All tests are unit tests under the existing provider unittest target.

### Provider tests (file: `maho_location_bar_utility_panel_provider_unittest.cc`)

1. **Zone-shape suite** (§6.3 items 1, 3, 4, 7): minimal browser fixture, no live web contents, verifies enum values, ordering, and headers.
2. **Toolbar suite** (§6.3 item 2): asserts `sf_symbol_name` strings, callback presence, and `enabled` flag on each toolbar action.
3. **Extensions suite** (§6.3 items 5, 6, 13): two cases for fallback and non-empty plus retained summary-action tests.
4. **Settings suite** (§6.3 items 8, 9, 10, 11, optional 12): delegation tests for Developer Mode and AutoPiP rows; behavior tests for Block Ads & Trackers via the new helper. If blocked-count enrichment ships, item 12 is added; otherwise it is omitted.
5. **Footer suite**: existing `BuildFooterFromModel` tests retained verbatim.

### View tests

Phase 1 does not introduce new view-level unit tests. View behavior changes are visual layout only and would require interactive coverage that is explicitly out of scope. The existing test target must continue to build cleanly — this is the only view-side acceptance gate.

### Test target

All tests run under the existing `maho_location_bar_utility_panel_provider_unittest` target. If the target name changes, update the GN reference in the same commit.

---

## 8. Phased Execution Order

Each phase is one atomic commit. Each phase ends with green unit tests + canonical build pass before the next begins.

### Phase A — Provider header refactor (additive model contract)

1. Extend `MahoUtilityPanelZoneId` with `kToolbar`, `kExtensions`, `kSettings`. Keep legacy values.
2. Extend `MahoUtilityPanelZoneStyle` with new layout styles if required. Keep legacy values.
3. Add `header_label` to `MahoUtilityPanelZone` as `std::u16string` defaulting to empty.
4. Update provider .cc only enough to compile. No assembly changes yet.
5. Update tests to construct zones with the new field and reference the new enum values. Existing tests for retained builders stay untouched.
6. Run the canonical build: `python3 maho-chromium/build/scripts/build_maho.py`.

Acceptance: build green, provider unittest green, sidebar still compiles untouched.

### Phase B — Toolbar zone

1. Implement `internal::BuildToolbarZone(Browser*)` with 4 actions in the agreed order: Share via `BuildShareAction(Browser*)`, then `wand.and.stars`, `camera`, `viewfinder` stubs.
2. Add the toolbar layout primitives in the view.
3. Add toolbar suite tests.

Acceptance: toolbar suite green, build green, Share opens the share menu when clicked through the address-bar bubble.

### Phase C — Extensions zone

1. Implement `internal::BuildExtensionsZone(const std::string&, Browser*)` with fallback + `+` logic.
2. Add chip-row layout in the view.
3. Render `Extensions` header from the model.
4. Add extensions suite tests.

Acceptance: extensions suite green, build green, address-bar bubble shows extension chips with trailing `+`, empty-state shows fallback + `+`.

### Phase D — Settings zone (incl. real Block Ads & Trackers row)

1. Implement `internal::BuildSettingsZone(Browser*)` reusing `BuildDeveloperModeAction(parsed_settings)` and `BuildAutoPipSettingsRow(web_contents, profile)` directly.
2. Implement `internal::BuildBlockAdsAndTrackersRow(Browser*)` using existing synchronous `maho::core::GetSiteExceptions(...)` plus the current-origin check.
3. Add settings list layout in the view.
4. Add settings suite tests (incl. or excl. blocked-count enrichment per the §6.2 gate).

Acceptance: settings suite green, build green.

### Phase E — Footer ellipsis (view-only)

1. Render disabled `…` glyph in the footer view.
2. No struct change, no callback registration.

Acceptance: build green. No new tests; this is a view-only visual change.

### Phase F — Optional cleanup (not Phase-1 must-ship)

1. If a fresh `git grep` confirms no remaining consumers of legacy enum values or legacy display modes, remove them.
2. Remove now-dead Brave-era view constants.
3. Confirm `BuildExtensionsSummaryAction(...)` is still exported and tested.
4. Final canonical build run.

Acceptance: build green, all unit tests green, sidebar compiles untouched. If grep finds remaining consumers, leave the legacy values in place and defer cleanup.

---

## 9. Verification

The acceptance gates are exactly two:

1. **Unit tests green:** `cd maho && bun run test` passes, and the provider unittest target passes under the canonical build.
2. **Canonical build green:** `python3 maho-chromium/build/scripts/build_maho.py` exits 0.

Optional supporting checks (not gates):
- `cd maho && bun run typecheck` for early Rust-side regressions.
- `lsp_diagnostics` on each edited C++ file before committing each phase.

Out of scope as gates: interactive uitests, integration tests, manual QA, screenshot diffs.

---

## 10. Risks & Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| Sidebar runtime regression from shared model changes | Low | Public builder signatures locked; sidebar build is part of the canonical build gate. |
| Blocked-count read turns out to be async or expensive | Medium | §6.2 gate: ship without enrichment if synchronous + testable cost is not met. No rework owed. |
| `enabled = false` actions still wire click handlers via the view's default path | Medium | View change in §6.5 explicitly skips click-target registration when `enabled = false`. Covered by code review per phase. |
| Footer ellipsis is mistaken for a wired affordance | Medium | Disabled visual treatment + no handler. Phase E commit message must call this out. |
| New zone IDs collide with assumptions in legacy code paths | Low | Legacy enum values stay in place during Phase 1; new values are additive. |
| Interactive uitest breaks on enum-name changes | Low | §6.7 sanctions a minimal compile-only update with no new coverage. |
| Per-extension chips share a single callback | Accepted | Documented in code; not a Phase 1 concern. |
| Reusing `BuildDeveloperModeAction(...)` / `BuildAutoPipSettingsRow(...)` produces a row whose visual style does not match the rest of the Settings list | Low | The view's settings-list layout reads only public fields on the action; consistent styling is enforced view-side. |

---

## 11. Atomic Commit Strategy

One commit per phase, message prefix `[utility-panel]`. Each commit must:

1. Compile under `python3 maho-chromium/build/scripts/build_maho.py`.
2. Pass the provider unittest target.
3. Touch only the files listed in its phase, plus mechanical updates needed to keep the build green.
4. Avoid AI attribution in the commit message.

Suggested messages:

- `[utility-panel] Add toolbar/extensions/settings zone IDs and zone header_label field`
- `[utility-panel] Build Arc-shape toolbar zone with Share wired and 3 disabled stubs`
- `[utility-panel] Build extensions zone with chips, add-chip, and empty-state fallback`
- `[utility-panel] Build settings zone reusing existing dev-mode and auto-pip rows plus real shields row`
- `[utility-panel] Render disabled ellipsis affordance in footer view`
- `[utility-panel] Optional: drop dead display modes and constants after Arc shape lands`

---

## 12. Must-Ship vs Deferred

### Must ship in Phase 1

- Three Arc-shape zones (toolbar / extensions / settings) in this exact order, alongside any retained legacy zones produced by remaining callers.
- Model-driven `Extensions` and `Settings` headers via the additive `header_label` field.
- 4-icon toolbar with Share wired via `BuildShareAction(Browser*)` and the three honest disabled stubs (`wand.and.stars`, `camera`, `viewfinder`) in that order after Share.
- Extensions chips from `BuildInstalledExtensions(json, browser)` plus trailing `+` and empty-state fallback chip.
- Real `Block Ads & Trackers` row built from a new Phase-1 helper using existing synchronous `maho::core::GetSiteExceptions(...)`.
- Developer Mode row produced by `BuildDeveloperModeAction(parsed_settings)`, reused with current semantics.
- Automatic Picture-In-Picture row produced by `BuildAutoPipSettingsRow(web_contents, profile)`, reused unchanged.
- Visible, non-interactive `…` in the footer.
- New zone IDs / styles introduced additively; legacy enum values retained.
- Public builder signatures unchanged: `BuildMahoLocationBarUtilityPanelModel(LocationBar*)`, `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)`, `BuildShareAction(Browser*)`, `BuildDeveloperModeAction(const base::Value*)`, `BuildAutoPipSettingsRow(content::WebContents*, Profile*)`, `BuildInstalledExtensions(const std::string&, Browser*)`, `BuildExtensionsSummaryAction(...)`, `BuildShieldsAction(Browser*)`, `BuildFooter(LocationBar*)`, `BuildFooter(Browser*)`, `BuildFooterFromModel(LocationBarModel*)`.
- `BuildExtensionsSummaryAction(...)` retained, exported, and unit-tested, but not surfaced in the visible Phase-1 assembly path.
- Unit tests + canonical build green.

### Deferred (explicitly not Phase 1)

- Real behavior for the three non-Share toolbar icons.
- Footer overflow menu, footer struct change, or any callback behind the ellipsis.
- Per-extension distinct callbacks.
- Sidebar-specific builder fork.
- Surfacing `BuildExtensionsSummaryAction(...)` in the visible panel.
- Blocked-count enrichment if it fails the §6.2 cost/testability gate.
- Any new Rust / FFI surface.
- Interactive UI tests, integration tests, manual QA gates.
- Removal of legacy `MahoUtilityPanelZoneId` / `MahoUtilityPanelZoneStyle` values and Brave-era view constants. Optional cleanup only, gated on a fresh consumer grep.

---

## 13. Definition of Done

Phase 1 is complete when **all** of the following hold simultaneously:

1. `maho_location_bar_utility_panel_provider.h` exposes the additive `MahoUtilityPanelZoneId` values (`kToolbar`, `kExtensions`, `kSettings`) and the additive `header_label` field on `MahoUtilityPanelZone`.
2. `BuildMahoLocationBarUtilityPanelModel(LocationBar*)` and `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` emit the three Arc-shape zones in order, with the headers and contents specified in §4 and §6.2.
3. The view renders the Arc-shaped panel with the correct toolbar, chip row, settings list, and footer ellipsis.
4. Share is the only wired toolbar action, built via `BuildShareAction(Browser*)`. The other three toolbar icons are inert.
5. The Block Ads & Trackers row is real and reflects the current origin's exception state via `maho::core::GetSiteExceptions(...)`.
6. The Settings zone's Developer Mode and Automatic Picture-In-Picture rows are produced by `BuildDeveloperModeAction(...)` and `BuildAutoPipSettingsRow(...)` respectively, with their existing semantics intact.
7. Public builder signatures are byte-identical to pre-plan state.
8. Provider unittest covers every test listed in §6.3 (modulo the §6.2 gate).
9. `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
10. Sidebar compiles and runs without source edits.
11. No commit message contains AI attribution.

When all eleven hold, the surface is at Arc parity for the Phase-1 envelope and ready for Phase 2 (real toolbar actions, footer overflow behavior, optional legacy enum cleanup).
