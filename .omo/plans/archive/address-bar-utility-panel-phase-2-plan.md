# Address Bar Utility Panel — Phase 2 Implementation Plan

**Status:** Ready for execution
**Surface:** `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_*`
**Mode:** Phase 2 (real Boost wiring + footer overflow + per-extension callbacks)
**Parent plan:** `.sisyphus/plans/address-bar-utility-panel-arc-parity-plan.md` (Phase 1, shipped)
**Verification:** Unit tests only + canonical `python3 maho-chromium/build/scripts/build_maho.py` + manual QA

---

## 1. Overview

Phase 1 shipped the Arc-shape skeleton (toolbar / extensions / settings zones, model-driven headers, Arc-only zone enums and styles already in place, real Block Ads & Trackers row with inline toggle, footer security pill plus a static view-only ellipsis label). Phase 2 closes the remaining deferred items that are now justified, in priority order:

1. Replace the `wand.and.stars` toolbar stub with a real Boost launcher.
2. Optionally enrich the Block Ads & Trackers row with a per-tab blocked count if it can be read synchronously and unit-tested.
3. Introduce a footer overflow model carrying Shields, Zoom In, Zoom Out, Reset Zoom, Find in Page, and Print.
4. Render that overflow section in the utility-panel view in a shape that matches the existing native panel structure (zone-style list, no new chrome, no new bubble).
5. Replace the shared "open extensions settings pane" callback wired into every extension chip with a per-extension callback that targets that extension's settings pane (or, if no per-extension target is reachable through current APIs, route through a per-extension URL fallback).
6. Wire the `camera` toolbar slot to a screenshot launcher (the standard screenshot flow), conditional on first establishing a native screenshot command path that Maho does not yet have.
7. Wire the `viewfinder` toolbar slot to a dedicated **full-page capture** action, conditional on the same capture command path landing locally. This is a deliberate Maho-specific decomposition of Zen's single screenshot capability into two distinct toolbar affordances; it is not a claim that Zen has a separate `viewfinder` feature.
8. Optional, gated cleanup of orphaned test registrations / dead helpers if any remain after items 1 to 7 land.

Phase 2 must start from the current shipped state. The plan does not re-plan Phase 1 items and does not assume legacy enum cleanup is still pending — Phase 1 already shipped Arc-only zone enums and Arc-only zone styles.

**Camera vs. viewfinder framing.** Zen Browser exposes a single screenshot action (`Browser:Screenshot`) and does not have a separate standalone `viewfinder` feature; Zen's research therefore grounds the *capability* (the browser knows how to capture a page), not a 1:1 toolbar mapping. Maho's product decision, given by user clarification, is to split that one capability across two toolbar slots: the `camera` slot launches the standard screenshot flow, and the `viewfinder` slot triggers a direct full-page capture. Both slots therefore become real wired actions in Phase 2, sharing the same conservative prerequisite — Maho currently has no native screenshot or full-page capture command path in the Chromium overlay, and Phase 2 must establish one before user-facing enablement. If the prerequisite path cannot land cleanly inside Phase 2's envelope, the panel-side wiring for either or both slots ships behind a feature flag and is deferred to a follow-up. No new FFI, no new Mojo, no new WebUI surface is promised.

---

## 2. Scope

### Must ship

- **2A — Boost toolbar wiring.** `wand.and.stars` becomes a wired toolbar action backed by `MahoBoostBubbleCoordinator::GetForBrowser(browser).Show(browser)`. Disabled when there is no active page; otherwise enabled, with `close_after_activate = true` so the panel dismisses before the Boost bubble takes focus.
- **2C — Footer overflow model.** `MahoLocationBarUtilityPanelFooter` gains a typed list of overflow actions (`Shields`, `Zoom In`, `Zoom Out`, `Reset Zoom`, `Find in Page`, `Print`) populated by the provider. Enabled state and subtitles come from existing builders where they exist (`BuildShieldsAction`, `BuildZoomInActionForState`, `BuildZoomOutActionForState`, `BuildZoomResetActionForState`) and from new tiny helpers for `Find in Page` and `Print` (no Mojo, no FFI).
- **2D — Footer overflow rendering.** The view replaces the static `…` label with an interactive overflow affordance that, when activated, expands a settings-list-style section inside the panel (same `kSettingsList` visual style already used by the Settings zone) listing the overflow rows. No new bubble, no new popover. The existing security summary stays where it is. Activation can be by click or keyboard.
- **2E — Per-extension callbacks.** Each extension chip's callback targets that specific extension's settings (per-extension chrome URL or the same shared pane keyed by extension id). The current shared `OpenExtensionsSettingsPaneForBrowser` callback used for every chip is replaced with a callback bound to the extension's id, using the path that is already reachable from `maho::OpenMahoSettingsPane(browser, "extensions")` plus extension id, or a per-extension URL fallback if a per-id pane route is not currently reachable. No new FFI is introduced.
- **2G — Camera screenshot launcher.** The `camera` toolbar slot becomes a wired action that launches the standard screenshot capture flow. The capability source is Zen's single `Browser:Screenshot` command (Zen exposes that command in two surfaces — a camera-icon quick action in the site-data panel and a `Capture Screenshot` entry in URL-bar global actions); Maho's `camera` slot mirrors that *standard screenshot* surface. Because Maho does not yet have a native screenshot command path in the Chromium overlay, this work item subsumes the prerequisite of establishing that path: register a screenshot action ID in `maho_command_action_handler.{h,cc}`, route it through whichever Chromium-side capture entry point lands cleanly in the local tree (preferred: `chrome::ExecuteCommand(browser, IDC_TAKE_SCREENSHOT)` or the equivalent screenshot `IDC_*` already wired in `chrome/browser/ui/browser_commands.cc`), and only then bind the utility-panel `camera` slot to that action ID. If the prerequisite path cannot be established cleanly inside Phase 2's envelope, ship the panel-side wiring behind a feature flag and keep the slot as a disabled stub for users until the path lands. No new FFI, no new Mojo.
- **2H — Viewfinder full-page capture.** The `viewfinder` toolbar slot becomes a wired action that triggers a **full-page capture** of the active tab. This is a Maho product directive (deliberate decomposition of the screenshot capability into two distinct toolbar slots), not a literal Zen feature; Zen has no separate `viewfinder` action. Because the local capture command path is the same prerequisite as 2G, 2H rides on the same establishment work: register a second action ID (e.g. `kMahoFullPageCaptureAction`) in `maho_command_action_handler.{h,cc}` alongside the screenshot action ID, dispatch it through whichever Chromium-side full-page capture entry point lands cleanly in the local tree, and bind the `viewfinder` slot to that action ID. The plan stays conservative: there is no upstream Chromium IDC for full-page capture today (`IDC_TAKE_SCREENSHOT` covers visible-area only), so the dispatch target is whichever entry point can be reached without new FFI / Mojo / WebUI — most plausibly a thin shell-side helper next to the same surface that 2G uses, or a deferred follow-up if no such surface is reachable in Phase 2's envelope. As with 2G, if the path cannot land cleanly, ship the panel-side wiring behind a feature flag and keep the slot as a disabled stub for users until the path lands. No new FFI, no new Mojo.

### Optional (justified, gated, may slip)

- **2B — Blocked-count enrichment for Block Ads & Trackers.** Attach a small badge / suffix to the Block Ads & Trackers row driven by `MahoAdBlockTabHelper::blocked_count()` only if (a) the read is synchronous on the UI thread and (b) it can be exercised via a fake `WebContents` chain in a unit test. If either gate fails, ship without it. No rework owed.
- **2F — Orphaned-test / dead-helper cleanup.** A `git grep` after 2A/C/D/E land may surface orphaned tests or unused helpers; if it does, fold a pure-deletion commit into Phase 2. Skip otherwise. GN-related cleanup is deferred and not in must-ship.

### Out of scope

- A region-only top-level screenshot toolbar action — region capture is a mode within Zen's single `Browser:Screenshot` flow, not a separate top-level command. Maho's two slots are decomposed by *capture target* (visible page vs. full page), not by selection mode. If Maho's eventual screenshot path supports a region mode, it lives inside the `camera` flow, not as a parallel toolbar action.
- A finished cross-platform screenshot or full-page capture pipeline — Phases 2G and 2H commit to wiring the launchers end-to-end through whatever Chromium-side capture entry point already exists (or can be reached with minimal shell-side glue). They do not invent a new full-page or region-capture pipeline; if no clean entry point exists, the panel-side wiring ships behind a feature flag.
- Any new WebUI surface, Mojo IDL, or FFI.
- Migration of the panel to WebUI.
- Any change to the `maho-core` Rust crate or the `maho-ffi` bridge.
- Any change to the public `BuildMahoLocationBarUtilityPanelModel(LocationBar*)` / `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` signatures.
- Footer overflow shipped as a popover, menu, or new bubble.
- Adding `relay/` or `mail/` as workspace members.
- Sidebar-specific builder fork.
- Removal of legacy `MahoUtilityPanelZoneId` / `MahoUtilityPanelZoneStyle` values (not real anymore — current header already only declares the Arc enums).

---

## 3. Grounded Current State

Files in scope (all exist on disk, exact symbols verified):

- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider_unittest.cc`

Current shipped reality (verified):

- `MahoUtilityPanelZoneId` is already Arc-only: `kToolbar`, `kExtensions`, `kSettings`. There is no legacy `kPrimaryActions` / `kInlineActionStrip` / `kSecondaryShortcuts` / `kStatus` to clean up.
- `MahoUtilityPanelZoneStyle` is already Arc-only: `kToolbarRow`, `kChipRow`, `kSettingsList`. No legacy `kGrid` / `kInlineRow` / `kListRow` to clean up.
- `MahoUtilityPanelZone` already carries `header_label` (not optional, plain `std::u16string`).
- `MahoLocationBarUtilityPanelAction` already carries inline toggle fields: `has_toggle`, `toggle_is_on`, `toggle_on_subtitle`, `toggle_off_subtitle`, `toggle_callback`, and `close_after_activate`.
- `BuildBlockAdsAndTrackersRow(Browser*)` already calls `maho_core_get_site_exceptions` for the active origin, calls `IsOriginExceptedInSiteExceptions` to decide blocking state, and binds `SetBlockAdsAndTrackersEnabledForOrigin(origin, ...)` as a `toggle_callback` (not a `callback`). The row is an inline toggle, not a bubble launcher.
- `SetBlockAdsAndTrackersEnabledForOrigin` writes site exceptions directly via `maho::core::AddSiteException` / `maho::core::RemoveSiteException`. Tests cover both directions plus the row metadata.
- `BuildShareAction(Browser*)` is fully wired through `ShowSharingHubForBrowser`.
- `BuildToolbarZone(Browser*)` puts Share first, then three `MakeDisabledStubAction` entries with symbols `wand.and.stars`, `camera`, `viewfinder`. Each stub uses `enabled = false`, `data_state = kUnavailable`, no callback. The `kCameraSymbolName` and `kViewfinderSymbolName` constants are already declared next to `kMagicWandSymbolName` in the provider .cc file.
- `BuildExtensionsZone(json, browser)` uses `BuildInstalledExtensions(json, browser)`, prepends a `No extensions` fallback when the parsed list is empty, and always appends `MakeAddExtensionChip(browser)`. Per-extension callbacks all bind `OpenExtensionsSettingsPaneForBrowser`. The add chip binds the same shared callback.
- `BuildSettingsZone(Browser*)` emits Developer Mode (`BuildDeveloperModeAction`), Block Ads & Trackers (`BuildBlockAdsAndTrackersRow`), Automatic Picture-In-Picture (`BuildAutoPipSettingsRow`).
- `BuildFooterFromWebContents(content::WebContents*)` already exists, drives the security summary, and is the canonical footer builder. `BuildFooterFromModel(LocationBarModel*)` and the two `BuildFooter(...)` overloads delegate down to it. Footer struct fields today: `title`, `subtitle`, `primary_action_label`, `sf_symbol_name`, `primary_action_enabled`, `close_after_primary_action`, `data_state`, `security_level`, `primary_action`. There is no overflow list.
- `BuildZoomInActionForState`, `BuildZoomOutActionForState`, `BuildZoomResetActionForState` exist, are unit-tested, and produce `MahoLocationBarUtilityPanelAction` values directly. `IsAtMaxZoom` / `IsAtMinZoom` helpers exist for `zoom::ZoomController*`.
- `MahoBoostBubbleCoordinator::GetForBrowser(Browser*)` exists at `maho-chromium/browser/ui/views/boost/maho_boost_bubble_coordinator.{h,cc}` with a `Show(Browser*)` overload that anchors itself off the location bar. This is the existing native Boost plumbing.
- `MahoAdBlockTabHelper::blocked_count()` returns `uint32_t` and lives at `maho-chromium/browser/net/maho_ad_block_tab_helper.{h,cc}`.
- The view renders the footer in `MahoLocationBarUtilityPanelView::AddFooterSummary()`. It currently composes `MahoLocationBarUtilityPanelSecuritySummaryView` plus a static `…` label that is non-interactive (`SetCanProcessEventsWithinSubtree(false)`, `SetEnabled(false)`). There is no overflow model or expand-collapse state.
- `MahoLocationBarUtilityPanelActionView` already implements three `DisplayMode`s: `kToolbarButton`, `kExtensionChip`, `kSettingsRow`. Inline toggles work in `kSettingsRow` via `views::ToggleButton`. The view honors `enabled = false` by setting opacity and skipping click registration.

Screenshot / capture infrastructure (verified):

- Zen Browser exposes a single screenshot action via `command="Browser:Screenshot"`. The same command surfaces in two places in Zen's UI: the camera-icon quick-action button in the site-data panel, and as `Capture Screenshot` in the URL-bar global-actions list. The command opens a screenshot flow that includes overlay/selection UI plus visible-area and full-page modes; region capture is a mode within that flow, not a distinct top-level command. Zen does **not** expose a separate standalone `viewfinder` action.
- Maho's product directive (per user clarification) is to decompose Zen's single screenshot capability into two distinct toolbar slots: `camera` for the standard screenshot launcher, `viewfinder` for direct full-page capture. This decomposition is a Maho-specific toolbar choice informed by Zen's capability source; it is not a claim that Zen has a separate `viewfinder` feature.
- Maho does NOT currently have a native screenshot or full-page capture command path in the Chromium overlay. Specifically:
  - `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` already dispatches actions like Print and Copy URL (typically through `chrome::ExecuteCommand(browser, IDC_*)`), but it has no screenshot action ID, no full-page capture action ID, and no corresponding dispatch case today.
  - There is no full-page or visible-area screenshot FFI export in `maho-ffi`, and no Mojo IDL for a screenshot capture flow.
  - Existing capture primitives are limited to tab-preview thumbnail capture plumbing and a macOS-shell-only `WKWebView` snapshot path; neither is reachable from the Chromium-side overlay action dispatcher.
  - Upstream Chromium has `IDC_TAKE_SCREENSHOT` for visible-area capture, which is the most plausible dispatch target for the `camera` slot. There is no equivalent upstream IDC for full-page capture; the most plausible target for the `viewfinder` slot is therefore a thin shell-side helper that the prerequisite work in Phase 2H lands alongside the screenshot action ID, with no new FFI / Mojo / WebUI surface.
- Existing utility-panel integration points stay the same for both screenshot and full-page capture wiring: `maho_location_bar_utility_panel_provider.{h,cc}`, `maho_location_bar_utility_panel_provider_unittest.cc`, and `maho_command_action_handler.{h,cc}`. The view (`maho_location_bar_utility_panel_view.{h,cc}`) needs no edits for 2G or 2H — both slots reuse the existing `kToolbarButton` `DisplayMode` and the existing enabled / disabled rendering path.

Phase 1 unit tests cover toolbar order, header labels, fallback chip + add chip, settings row order and delegation, the inline toggle metadata for Block Ads & Trackers, the synchronous `SetBlockAdsAndTrackersEnabledForOrigin` direct-write semantics, footer null/live paths, and zoom action state. Phase 2 must extend this suite without regressing it.

---

## 4. File-by-File Implementation Plan

Each subsection lists the file, the change, and the rationale. All changes are concrete and grounded in the symbols above.

### 4.1 `maho_location_bar_utility_panel_provider.h`

**Change A — Add overflow list to the footer struct.** Extend `MahoLocationBarUtilityPanelFooter` additively with a typed overflow vector. Existing fields and field order stay untouched so no caller is forced to reorder initializers:

```cpp
struct MahoLocationBarUtilityPanelFooter {
  // ... existing fields unchanged ...
  std::vector<MahoLocationBarUtilityPanelAction> overflow_actions;
};
```

`overflow_actions` is empty by default. Existing callers and existing tests therefore see no behavior change unless the provider populates the field.

**Change B — Declare new internal helpers.** Add the following declarations under `namespace internal`:

```cpp
MahoLocationBarUtilityPanelAction BuildBoostAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildFindInPageAction(Browser* browser);

MahoLocationBarUtilityPanelAction BuildPrintAction(Browser* browser);

std::vector<MahoLocationBarUtilityPanelAction> BuildFooterOverflowActions(
    Browser* browser);

// Per-extension callback target. Returns the route that should be opened for
// the given extension id; today this is a thin wrapper around
// maho::OpenMahoSettingsPane plus extension id, or a per-extension URL
// fallback if no per-id route is reachable.
void OpenExtensionSettingsForId(Browser* browser, const std::string& id);

// Optional 2B. Returns nullopt if the helper is not reachable synchronously
// or if no active tab can supply a value.
std::optional<int> TryGetBlockedCountForActiveTab(Browser* browser);
```

`BuildShieldsAction(Browser*)` already exists and is reused for the overflow Shields entry. No new declaration is needed for it.

**Change C — Public API stability.** `BuildMahoLocationBarUtilityPanelModel(LocationBar*)` and `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` keep their signatures byte-identical. The sidebar consumer at `browser/ui/views/sidebar/` therefore does not need source edits.

### 4.2 `maho_location_bar_utility_panel_provider.cc`

**Change A — Wire the Boost toolbar action (2A).** Add a private helper and a builder:

```cpp
void ShowBoostBubbleForBrowser(Browser* browser) {
  if (!browser) {
    return;
  }
  MahoBoostBubbleCoordinator::GetForBrowser(browser).Show(browser);
}

MahoLocationBarUtilityPanelAction BuildBoostAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Boost",
      has_active_page ? u"Customize this site" : u"Current page unavailable",
      kMagicWandSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&ShowBoostBubbleForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}
```

The `kMagicWandSymbolName` constant already exists. Replace `BuildToolbarZone`'s second item from `MakeDisabledStubAction(u"Unavailable action", kMagicWandSymbolName)` to `BuildBoostAction(browser)`. The `camera` slot is wired by §4.9 / Phase G (standard screenshot), and the `viewfinder` slot is wired by §4.10 / Phase H (full-page capture); both are out of scope for this Boost-focused builder change.

Include `maho/browser/ui/views/boost/maho_boost_bubble_coordinator.h` at the top of this file.

**Change B — Optional blocked-count enrichment (2B).** Add `TryGetBlockedCountForActiveTab(Browser*)`:

```cpp
std::optional<int> TryGetBlockedCountForActiveTab(Browser* browser) {
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return std::nullopt;
  }
  MahoAdBlockTabHelper* helper =
      MahoAdBlockTabHelper::FromWebContents(web_contents);
  if (!helper) {
    return std::nullopt;
  }
  return static_cast<int>(helper->blocked_count());
}
```

If, during implementation, this read turns out to require a `CreateForWebContents` step that has UI-thread side effects beyond a no-op, drop 2B in this commit. The plan owes no rework.

When the helper returns a value `n > 0`, append a small suffix to the Block Ads & Trackers row's `subtitle` and `accessible_name`, e.g. `"Blocked on every visit · 12 blocked"`. When the helper returns `nullopt` or `0`, leave the subtitle untouched. Both Phase 1 toggle subtitles must continue to roundtrip correctly when the toggle is flipped (the view rewrites the subtitle from `toggle_on_subtitle` / `toggle_off_subtitle` on toggle change, so the suffix can only live on the immediately-rendered subtitle, not on the toggle-state subtitles). If preserving the suffix across a toggle flip would require a structural change to `UpdateInlineTogglePresentation`, drop 2B for this commit.

Include `maho/browser/net/maho_ad_block_tab_helper.h` only if 2B ships.

**Change C — Footer overflow builder (2C).** Add concrete builders for the actions that don't yet have one and assemble the list:

```cpp
constexpr char kFindInPageSymbolName[] = "magnifyingglass";
constexpr char kPrintSymbolName[] = "printer";

void OpenFindInPageForBrowser(Browser* browser) {
  if (!browser) {
    return;
  }
  chrome::Find(browser);
}

void PrintActiveTabForBrowser(Browser* browser) {
  if (!browser) {
    return;
  }
  chrome::Print(browser);
}

MahoLocationBarUtilityPanelAction BuildFindInPageAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Find in Page",
      has_active_page ? u"Search this page" : u"Current page unavailable",
      kFindInPageSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&OpenFindInPageForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

MahoLocationBarUtilityPanelAction BuildPrintAction(Browser* browser) {
  const bool has_active_page = HasActivePageForSiteControls(browser);
  MahoLocationBarUtilityPanelAction action = MakeAction(
      u"Print",
      has_active_page ? u"Print this page" : u"Current page unavailable",
      kPrintSymbolName,
      has_active_page ? MahoLocationBarUtilityPanelDataState::kLive
                      : MahoLocationBarUtilityPanelDataState::kUnavailable,
      has_active_page);
  action.close_after_activate = true;
  if (has_active_page && browser) {
    action.callback = base::BindRepeating(&PrintActiveTabForBrowser,
                                          base::Unretained(browser));
  }
  return action;
}

std::vector<MahoLocationBarUtilityPanelAction> BuildFooterOverflowActions(
    Browser* browser) {
  std::vector<MahoLocationBarUtilityPanelAction> actions;
  actions.push_back(BuildShieldsAction(browser));

  zoom::ZoomController* zoom_controller = nullptr;
  std::optional<int> zoom_percent;
  bool at_default_zoom = true;
  bool has_active_page = false;
  if (auto* contents = GetActiveWebContents(browser); contents) {
    zoom_controller = zoom::ZoomController::FromWebContents(contents);
    has_active_page = true;
    if (zoom_controller) {
      zoom_percent = static_cast<int>(zoom_controller->GetZoomPercent());
      at_default_zoom =
          blink::ZoomValuesEqual(zoom_controller->GetZoomLevel(),
                                 zoom_controller->GetDefaultZoomLevel());
    }
  }

  MahoLocationBarUtilityPanelAction zoom_in =
      BuildZoomInActionForState(has_active_page, IsAtMaxZoom(zoom_controller));
  MahoLocationBarUtilityPanelAction zoom_out =
      BuildZoomOutActionForState(has_active_page, IsAtMinZoom(zoom_controller));
  MahoLocationBarUtilityPanelAction zoom_reset =
      BuildZoomResetActionForState(has_active_page, at_default_zoom,
                                   zoom_percent);
  // Wire activation for zoom actions when enabled. The existing builders
  // produce no callback; bind chrome::Zoom(...) here so the overflow list is
  // self-contained.
  if (zoom_in.enabled && browser) {
    zoom_in.callback = base::BindRepeating(
        &chrome::Zoom, base::Unretained(browser), content::PAGE_ZOOM_IN);
  }
  if (zoom_out.enabled && browser) {
    zoom_out.callback = base::BindRepeating(
        &chrome::Zoom, base::Unretained(browser), content::PAGE_ZOOM_OUT);
  }
  if (zoom_reset.enabled && browser) {
    zoom_reset.callback = base::BindRepeating(
        &chrome::Zoom, base::Unretained(browser), content::PAGE_ZOOM_RESET);
  }

  actions.push_back(std::move(zoom_in));
  actions.push_back(std::move(zoom_out));
  actions.push_back(std::move(zoom_reset));
  actions.push_back(BuildFindInPageAction(browser));
  actions.push_back(BuildPrintAction(browser));
  return actions;
}
```

Implementation note: the exact `chrome::Zoom` parameter type is `content::PageZoom` enum; if the public surface differs locally, fall back to the existing `chrome::ExecuteCommand(browser, IDC_ZOOM_*)` path used in `maho_command_action_handler.cc`. Both options keep this in shell-only code with no Mojo or FFI changes. Pick whichever is already linked into this target so no new GN dependency is introduced.

Update `BuildFooterFromWebContents(content::WebContents*)` to populate `footer.overflow_actions` only when the caller can resolve a `Browser*`. Since `BuildFooterFromWebContents` accepts only a `content::WebContents*`, plumb the `Browser*` through one of these two options:

1. Resolve `Browser*` inside `BuildFooterFromWebContents` via `chrome::FindBrowserWithTab(web_contents)` (already used elsewhere in the chromium tree). Preferred — keeps the existing public footer builder signatures byte-identical.
2. Add a `BuildFooterFromWebContents(content::WebContents*, Browser*)` overload and route the existing `BuildFooter(Browser*)` and `BuildFooter(LocationBar*)` callers through it; keep the single-arg overload for null-browser paths. Acceptable fallback if option 1 is not reachable.

Either way, when no `Browser*` is resolvable, leave `overflow_actions` empty. The `BuildFooterFromModel(LocationBarModel*)` path also leaves `overflow_actions` empty because it has no browser context. This matches the existing tests for the model path (which assert other footer fields, never overflow content).

**Change D — Per-extension callbacks (2E).** Replace the shared `OpenExtensionsSettingsPaneForBrowser` callback in `BuildInstalledExtensions(json, browser)` with a per-id binding:

```cpp
void OpenExtensionSettingsForId(Browser* browser, const std::string& id) {
  if (!browser) {
    return;
  }
  if (id.empty()) {
    maho::OpenMahoSettingsPane(browser, "extensions");
    return;
  }
  maho::OpenMahoSettingsPane(browser, "extensions/" + id);
}
```

In the per-extension loop, capture the extension id (already parsed via `dict->FindString("id")`) and bind:

```cpp
const std::string extension_id = id ? *id : std::string();
action.callback = base::BindRepeating(&OpenExtensionSettingsForId,
                                      base::Unretained(browser),
                                      extension_id);
```

The empty fallback chip (`"No installed extensions"`) and `MakeAddExtensionChip(browser)` continue to bind to the shared `OpenExtensionsSettingsPaneForBrowser` route. They are not per-extension.

If `maho::OpenMahoSettingsPane(browser, "extensions/<id>")` cannot reach a per-id pane today, fall back to opening the same pane and surface `extension_id` via a query-string suffix (e.g. `"extensions?focus=<id>"`) so the eventual UI consumer can read it. No new FFI, no new Mojo. Pick whichever route is already reachable from the existing `maho::OpenMahoSettingsPane` plumbing; document the choice in code comments above `OpenExtensionSettingsForId`.

**Change E — Top-level builders unchanged.** `BuildMahoLocationBarUtilityPanelModel(LocationBar*)` and `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` keep their bodies. They route through `internal::PopulateArcShapeModelData(...)` and `internal::BuildFooter(...)` exactly as today; the footer builder is what populates `overflow_actions`.

### 4.3 `maho_location_bar_utility_panel_view.h`

Add private state to `MahoLocationBarUtilityPanelView` so the footer can switch between the collapsed (icon + ellipsis) and expanded (settings-list-style overflow rows) presentations:

```cpp
private:
  void AddFooterSummary();          // existing
  void AddFooterOverflowSection();  // NEW — settings-list-style rows
  void OnFooterOverflowToggled();   // NEW — flip expand state
  bool footer_overflow_expanded_ = false;
  raw_ptr<views::View> footer_overflow_section_ = nullptr;
```

No new field on `MahoLocationBarUtilityPanelAction`. No new `DisplayMode`.

### 4.4 `maho_location_bar_utility_panel_view.cc`

**Change A — Replace the static `…` label.** In `AddFooterSummary()` the existing static `…` label becomes an interactive control. Two acceptable implementations; pick one and stay consistent:

1. Reuse the existing `MahoLocationBarUtilityPanelActionView` machinery by synthesizing a non-toggling `kToolbarButton`-style action whose icon is `ellipsis` (or the existing `…` glyph), `enabled = !model_.footer.overflow_actions.empty()`, and whose callback flips `footer_overflow_expanded_` and rebuilds the overflow section. Preferred — reuses the existing accessibility plumbing.
2. Wrap the existing `views::Label` in a focusable container with an `OnMousePressed` / `OnKeyPressed` override mirroring `MahoLocationBarUtilityPanelActionView`. Acceptable fallback.

When `model_.footer.overflow_actions` is empty, the affordance stays visually present but is disabled (no callback, reduced opacity), preserving the current Phase 1 visual.

**Change B — Render the overflow section (2D).** Add `AddFooterOverflowSection()`:

```cpp
void MahoLocationBarUtilityPanelView::AddFooterOverflowSection() {
  if (model_.footer.overflow_actions.empty()) {
    return;
  }
  if (footer_overflow_section_) {
    contents_view_->RemoveChildViewT(footer_overflow_section_.get());
    footer_overflow_section_ = nullptr;
  }
  if (!footer_overflow_expanded_) {
    return;
  }

  // Build a synthetic settings-list zone so the section is visually identical
  // to the existing Settings zone. No new layout primitives are introduced.
  MahoUtilityPanelZone overflow_zone;
  overflow_zone.id = MahoUtilityPanelZoneId::kSettings;
  overflow_zone.style = MahoUtilityPanelZoneStyle::kSettingsList;
  overflow_zone.header_label = std::u16string();
  overflow_zone.items = model_.footer.overflow_actions;
  AddSettingsListZone(overflow_zone);
  footer_overflow_section_ = contents_view_->children().back();
}
```

Insertion order: in `BuildContents()`, render zones, then the footer summary, then conditionally `AddFooterOverflowSection()` so the overflow rows appear directly under the security pill. This matches the natural reading order. The expanded section is part of the same panel; no new bubble.

When the user toggles the affordance, call `AddFooterOverflowSection()` and `InvalidateLayout()` (or `PreferredSizeChanged()` then `SizeToContents()` — pick whichever already works elsewhere in this view) so the panel grows / shrinks.

**Change C — Disabled-action contract preserved.** Overflow rows whose underlying builder set `enabled = false` (e.g. zoom rows when the active page is at the limit, or `Find in Page` when there is no active page) render with the existing disabled treatment from `MahoLocationBarUtilityPanelActionView`. No new behavior.

**Change D — Accessibility.** The expand affordance gets an `ax::mojom::Role::kButton` role and a name like `Show more actions` / `Hide more actions` keyed off `footer_overflow_expanded_`. The synthetic settings-list rows reuse the per-action a11y plumbing already in `MahoLocationBarUtilityPanelActionView`.

### 4.5 `maho_location_bar_utility_panel_provider_unittest.cc`

All Phase 1 tests stay. Add the following new tests, all unit tests (no integration, no interactive UI tests).

**For 2A — Boost toolbar:**

1. `ToolbarSecondActionIsBoostWiredWhenBrowserHasActivePage` — assert `zone.items[1].title == u"Boost"`, `sf_symbol_name == "wand.and.stars"`, `enabled == true`, `callback` non-null, `close_after_activate == true`. Use a fake browser with an active page.
2. `ToolbarSecondActionIsBoostUnavailableWhenNoActivePage` — null browser case: title `Boost`, subtitle `Current page unavailable`, `enabled == false`, no callback.
3. `ToolbarThirdAndFourthActionsRemainCameraAndViewfinderStubs` — explicit guard against accidental rewiring of the `camera` / `viewfinder` slots **during Phase A (Boost wiring) only**. This guard is intentionally narrow and temporary: when Phase G lands, the third-slot half is replaced by tests 13 / 14 / 15 below; when Phase H lands, the fourth-slot half is replaced by tests 17 / 18 / 19. The Phase A test asserts the still-unwired state at that commit boundary.

**For 2B — blocked-count enrichment (only if it ships):**

4. `BlockAdsAndTrackersRowAppendsBlockedCountWhenAvailable` — drive `MahoAdBlockTabHelper` by attaching it to a fake `WebContents` and pre-incrementing its counter; assert the suffix in `subtitle` / `accessible_name`. If 2B is dropped at implementation time, this test is omitted.
5. `BlockAdsAndTrackersRowOmitsCountWhenZeroOrUnavailable` — counterpart guard.

**For 2C — footer overflow model:**

6. `FooterOverflowActionsAreEmptyWhenBrowserUnavailable` — `BuildFooterFromWebContents(nullptr)` and `BuildFooterFromModel(nullptr)` both emit `overflow_actions.empty()`.
7. `FooterOverflowActionsHaveExpectedTitlesInOrder` — with a browser supplied, expect titles in this order: `Shields`, `Zoom In`, `Zoom Out`, `Reset Zoom`, `Find in Page`, `Print`.
8. `FooterOverflowActionsRespectActivePageEnabledState` — when `HasActivePageForSiteControls(browser)` is false, every action with the `Current page unavailable` subtitle is `enabled == false` and has no callback.
9. `FooterOverflowZoomEntriesReflectZoomControllerState` — drive a fake `WebContents` with a stubbed `zoom::ZoomController` such that `IsAtMaxZoom` is true; assert `Zoom In` is disabled and `Zoom Out` is enabled. Mirror the inverse case.

**For 2E — per-extension callbacks:**

10. `InstalledExtensionsBindPerExtensionCallback` — given a list with two distinct extensions, the callbacks are bound to distinct ids. Verify by capturing the bound id through a test seam: introduce a function pointer `internal::g_open_extension_settings_for_id_for_testing` (or a `base::RepeatingCallback<void(Browser*, const std::string&)>` test override) inside the cc file, set it from the test, and assert it sees each id when the callback fires. No production behavior change in the absence of a test override.
11. `ExtensionsAddChipStillBindsSharedExtensionsPaneCallback` — confirm `MakeAddExtensionChip(browser)` continues to bind the shared route, not a per-id route.
12. `ExtensionsFallbackChipIsDisabledAndUnbound` — explicit guard that the empty-list fallback (`"No extensions"` chip in `BuildExtensionsZone`) does not gain a callback as a side effect of the per-extension refactor.

**For 2G — camera screenshot launcher:**

13. `ToolbarThirdActionIsScreenshotWiredWhenBrowserHasActivePage` — assert `zone.items[2].title == u"Screenshot"`, `sf_symbol_name == "camera"`, `enabled == true`, `callback` non-null, `close_after_activate == true`. Use a fake browser with an active page. Replaces the third-slot half of test 3 once Phase G lands.
14. `ToolbarThirdActionIsScreenshotUnavailableWhenNoActivePage` — null browser case: title `Screenshot`, subtitle `Current page unavailable`, `enabled == false`, no callback.
15. `ToolbarThirdActionDispatchesScreenshotCommandIdViaTestSeam` — install a `base::RepeatingCallback<void(Browser*)>` (or equivalent action-ID dispatch override) on the provider's test seam, fire the callback returned by `BuildScreenshotAction(browser)`, and assert the seam observed exactly one dispatch with the expected screenshot action ID. The seam keeps the screenshot unit test free of a real capture pipeline.
16. (deleted; the previous "viewfinder remains permanent disabled stub" guard is no longer correct — the slot is now wired by Phase H).

**For 2H — viewfinder full-page capture:**

17. `ToolbarFourthActionIsFullPageCaptureWiredWhenBrowserHasActivePage` — assert `zone.items[3].title == u"Full-Page Capture"` (or whichever short label the implementation chooses; the test fixture should pin the chosen label as a constant in the test target), `sf_symbol_name == "viewfinder"`, `enabled == true`, `callback` non-null, `close_after_activate == true`. Use a fake browser with an active page. Replaces the fourth-slot half of test 3 once Phase H lands.
18. `ToolbarFourthActionIsFullPageCaptureUnavailableWhenNoActivePage` — null browser case: same title constant, subtitle `Current page unavailable`, `enabled == false`, no callback.
19. `ToolbarFourthActionDispatchesFullPageCaptureCommandIdViaTestSeam` — install a separate `base::RepeatingCallback<void(Browser*)>` (or equivalent action-ID dispatch override) on the provider's test seam for the full-page capture action, fire the callback returned by `BuildFullPageCaptureAction(browser)`, and assert the seam observed exactly one dispatch with the expected full-page capture action ID — distinct from the screenshot action ID asserted in test 15. Like test 15, this validates command dispatch / builder metadata only; it does not exercise a real capture pipeline.

Plus, in the existing command-handler unit-test target alongside the print / copy-URL dispatch tests:

- A new test asserting that dispatching the new screenshot action ID through `MahoCommandActionHandler` reaches the expected `chrome::ExecuteCommand(browser, IDC_TAKE_SCREENSHOT)` (or equivalent) call. Mirror the shape of the existing print / copy-URL dispatch tests; use whichever seam those tests already use.
- A second new test asserting that dispatching the new full-page capture action ID through `MahoCommandActionHandler` reaches whichever full-page capture entry point Phase H lands (a thin shell-side helper, an upstream IDC if one becomes available, or a flagged stub if user enablement is deferred). The test pins the dispatch contract; it does not assert the capture pipeline produces a real image.

**For 2F — orphan cleanup (only if real):**

20. If after 2A/C/D/E/G/H land a `git grep` reveals orphan tests or a now-dead helper (e.g. an unused `MakeDisabledStubAction` reference, an unused symbol constant), fold a deletion-only commit. No new tests; existing suite must stay green. Skip this commit entirely if the grep is empty.

All tests run under the existing provider unittest target. No new test target.

### 4.6 `maho_location_bar_utility_bubble_coordinator.{h,cc}` and `maho_location_bar_utility_icon_view.{h,cc}`

No structural change expected. The footer overflow is rendered inside the existing panel view; the coordinator and icon view do not need new entry points or new state. Touch only if a referenced symbol in the provider header changes name (it does not under this plan).

### 4.7 `maho_location_bar_utility_icon_view_interactive_uitest.cc`

Compile-only updates if any. Not a required acceptance gate.

### 4.8 Sidebar consumer (read-only)

`browser/ui/views/sidebar/maho_sidebar_state_adapter.{h,cc}` and the sidebar views consume `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` and `BuildFooterFromModel(...)`. Public signatures stay byte-identical. The sidebar inherits the footer overflow when a browser is supplied, and inherits an empty overflow under the model-only path. Verification is build-only.

### 4.9 Camera screenshot wiring (2G)

This work item has two halves. Half one establishes a native screenshot command path that Maho does not yet have. Half two wires the utility-panel `camera` slot to that path. Both halves must land before the user-facing slot leaves disabled-stub state. The plan stays conservative: it does not invent a finished screenshot implementation that does not exist locally, and it does not pre-commit to file paths for capture-pipeline files that have not been written yet.

**Grounded framing.**

- Zen Browser's screenshot capability source is a single action via `Browser:Screenshot`, exposed in two places (camera-icon quick-action button + URL-bar global actions as `Capture Screenshot`). Region capture is a mode within the screenshot flow, not a separate top-level command.
- Maho's product directive is to use that capability as the source for two distinct toolbar slots: the `camera` slot (this section) for the standard screenshot launcher, and the `viewfinder` slot (§4.10) for direct full-page capture. This decomposition is a Maho-specific toolbar choice, not a claim that Zen has a separate `viewfinder` action.
- Locally, no Chromium-side screenshot command path exists in the overlay yet; `maho_command_action_handler.cc` has no screenshot action ID, no full-screenshot FFI exists in `maho-ffi`, and the only capture-related primitives are tab-preview thumbnail capture plumbing and a macOS-shell-only `WKWebView` snapshot path.

**File surface (likely).**

- `maho-chromium/browser/ui/views/command/maho_command_action_handler.h` — declare a new screenshot action ID (e.g. `kMahoScreenshotCaptureAction`) alongside the existing print / copy-URL action IDs.
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` — register the new action ID and dispatch it via `chrome::ExecuteCommand(browser, IDC_TAKE_SCREENSHOT)` (or the equivalent screenshot `IDC_*` already wired in `chrome/browser/ui/browser_commands.cc`). This mirrors the existing print / copy-URL dispatch precedent. No new FFI, no new Mojo. If the local `IDC_*` enum does not contain a usable screenshot identifier, the prerequisite work expands to either landing the upstream-style `IDC_TAKE_SCREENSHOT` plumbing (preferred) or introducing a thin shell-side helper that drives whichever capture surface is reachable in the overlay; the file path for that helper is determined during prerequisite-path implementation rather than fabricated here.
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h` — declare `MahoLocationBarUtilityPanelAction BuildScreenshotAction(Browser* browser);` under `namespace internal`.
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc` — define `BuildScreenshotAction(Browser*)` mirroring the shape of `BuildBoostAction`. Replace the third entry in `BuildToolbarZone(Browser*)` (the `camera` `MakeDisabledStubAction(u"Unavailable action", kCameraSymbolName)`) with `BuildScreenshotAction(browser)`. Set `close_after_activate = true`. Title `Screenshot`; subtitle `Capture this page` when an active page is available, `Current page unavailable` otherwise; `sf_symbol_name = kCameraSymbolName`; callback dispatches the new screenshot action ID through `MahoCommandActionHandler` (or directly through the same `chrome::ExecuteCommand(browser, IDC_TAKE_SCREENSHOT)` wrapper, whichever matches the precedent of `BuildPrintAction` from §4.2 Change C and the existing handler's print case).
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider_unittest.cc` — add tests asserting the third toolbar slot is `Screenshot`-titled, uses `kCameraSymbolName`, has a non-null callback when an active page exists, is disabled with no callback otherwise, and that the dispatch reaches the new screenshot action ID through a `MahoCommandActionHandler` test seam (mirroring the existing dispatch tests in that target).
- `maho_location_bar_utility_panel_view.{h,cc}` — **not edited by 2G.** The `camera` slot reuses the existing `kToolbarButton` `DisplayMode` and the existing enabled / disabled rendering path. No new view-level state.
- Whichever additional Chromium-side capture integration file is required once the actual screenshot path is chosen — likely a small helper next to existing capture plumbing, or no new file if `IDC_TAKE_SCREENSHOT` plus the existing `chrome::ExecuteCommand` route is sufficient. Concrete path is determined during prerequisite-path implementation.

**Conservative framing.** Maho does not yet have a ready native screenshot command path. Phase 2G must establish that path before the toolbar action can be wired end-to-end. If the prerequisite path cannot land cleanly inside Phase 2's envelope (for example, because reaching a working capture entry point requires upstream Chromium integration that is non-trivial inside the overlay), 2G ships the panel-side wiring behind a feature flag and keeps the slot as a disabled stub for users until the path lands. The plan does not invent a finished screenshot implementation that does not exist locally, does not promise region capture as a must-ship parity item (region is a mode inside the single screenshot flow per Zen), and does not promise any new FFI or Mojo surface.

### 4.10 Viewfinder full-page capture wiring (2H)

This work item also has two halves. Half one extends the same native capture command path established in 2G with a second action ID for full-page capture. Half two wires the utility-panel `viewfinder` slot to that action ID. Both halves must land before the user-facing slot leaves disabled-stub state. The plan stays conservative: there is no upstream Chromium IDC for full-page capture today, so the dispatch target is described as the shape of the work, not a finalized implementation.

**Grounded framing.**

- This is a Maho-specific product directive: the `viewfinder` slot is a dedicated full-page capture launcher. The capability source is Zen's single screenshot capability (which has full-page as an internal mode); the toolbar decomposition is Maho's choice, not a Zen 1:1 mapping.
- Locally, the prerequisite path is the same one 2G establishes. 2H rides on top of that work by adding a second action ID and a second dispatch case. If 2G's prerequisite path cannot land cleanly inside Phase 2, 2H is in the same boat — its panel-side wiring ships behind a feature flag and user enablement is deferred.
- Upstream Chromium does not expose a single IDC for full-page capture. The most plausible local dispatch targets are: (a) a thin shell-side helper that drives whichever full-page capture surface is reachable from the overlay, (b) a DevTools-style protocol invocation kept entirely on the C++ side (no new Mojo / FFI), or (c) a deferred follow-up if neither (a) nor (b) is reachable in Phase 2's envelope. The plan does not pre-commit to which.

**File surface (likely).**

- `maho-chromium/browser/ui/views/command/maho_command_action_handler.h` — declare a second action ID (e.g. `kMahoFullPageCaptureAction`) alongside the screenshot action ID added in 2G.
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` — register the new action ID and dispatch it through whichever full-page capture entry point Phase H lands. If 2G's `IDC_TAKE_SCREENSHOT` route does not have a full-page variant locally, the dispatch target is a thin shell-side helper file determined during implementation, not fabricated here. No new FFI, no new Mojo, no new WebUI surface.
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h` — declare `MahoLocationBarUtilityPanelAction BuildFullPageCaptureAction(Browser* browser);` under `namespace internal`.
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc` — define `BuildFullPageCaptureAction(Browser*)` mirroring the shape of `BuildScreenshotAction`. Replace the fourth entry in `BuildToolbarZone(Browser*)` (the `viewfinder` `MakeDisabledStubAction(u"Unavailable action", kViewfinderSymbolName)`) with `BuildFullPageCaptureAction(browser)`. Set `close_after_activate = true`. Title `Full-Page Capture`; subtitle `Capture the full page` when an active page is available, `Current page unavailable` otherwise; `sf_symbol_name = kViewfinderSymbolName`; callback dispatches the new full-page capture action ID through `MahoCommandActionHandler`. The chosen title constant is pinned in the test fixture per test 17.
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider_unittest.cc` — add tests 17 / 18 / 19 from §4.5 asserting the fourth toolbar slot is full-page-capture-titled, uses `kViewfinderSymbolName`, has a non-null callback when an active page exists, is disabled with no callback otherwise, and that the dispatch reaches the new full-page capture action ID through a `MahoCommandActionHandler` test seam — distinct from the screenshot action ID's seam.
- `maho_location_bar_utility_panel_view.{h,cc}` — **not edited by 2H.** Reuses the existing `kToolbarButton` `DisplayMode`. No new view-level state.
- Whichever additional Chromium-side full-page capture integration file is required once the path is chosen — described conservatively here, finalized during implementation. The plan does not promise that file exists, does not promise its name, and does not promise its API.

**Conservative framing.** Maho does not yet have a ready native full-page capture command path; reaching one is harder than reaching the visible-area screenshot path because there is no upstream IDC equivalent. Phase 2H must establish that path on top of 2G's work before the toolbar action can be wired end-to-end. If the prerequisite path cannot land cleanly inside Phase 2's envelope, 2H ships the panel-side wiring behind a feature flag and keeps the slot as a disabled stub for users until the path lands. Tests 17 / 18 / 19 in §4.5 validate command dispatch / builder metadata, not a real full-page image; that conservatism is intentional. The plan does not invent a finished full-page capture implementation, does not promise any new FFI or Mojo surface, and does not promise a new WebUI surface.

---

## 5. Phased Execution Order

Each phase is one atomic commit. Tests are written first per phase, then production. Each phase ends with green unit tests + canonical build + manual QA before the next begins. Optional sub-phases are clearly marked.

### Phase A — Boost toolbar wiring (2A)

1. Add `BuildBoostAction(Browser*)` declaration to provider.h and definition to provider.cc; include the boost coordinator header.
2. Replace the second slot in `BuildToolbarZone(Browser*)` with `BuildBoostAction(browser)`.
3. Add tests 1, 2, 3 from §4.5.
4. Run `python3 maho-chromium/build/scripts/build_maho.py`.

Acceptance: tests green, build green, manual QA shows clicking the wand icon opens the Boost bubble anchored on the location bar.

### Phase B — Footer overflow model (2C, half)

1. Add `overflow_actions` field to `MahoLocationBarUtilityPanelFooter`.
2. Add `BuildFindInPageAction`, `BuildPrintAction`, `BuildFooterOverflowActions` plus the small `OpenFindInPageForBrowser` / `PrintActiveTabForBrowser` / zoom callback bindings.
3. Plumb `Browser*` resolution into `BuildFooterFromWebContents` (option 1 from §4.2 Change C preferred).
4. Populate `footer.overflow_actions` in `BuildFooterFromWebContents` when a browser is resolvable; leave empty otherwise.
5. Add tests 6, 7, 8, 9 from §4.5.
6. Run canonical build.

Acceptance: tests green, build green, model exposes overflow rows. View still renders nothing new in this phase.

### Phase C — Footer overflow rendering (2D)

1. Add `footer_overflow_expanded_`, `footer_overflow_section_`, `AddFooterOverflowSection`, and `OnFooterOverflowToggled` to the view.
2. Replace the static `…` label in `AddFooterSummary` with the interactive affordance.
3. Render the overflow section as a synthetic settings-list zone when expanded.
4. No new view-level unit tests (Phase 1 precedent — view layout is not unit-tested in this codebase).
5. Run canonical build.

Acceptance: build green, manual QA confirms expand/collapse works, all overflow rows activate their respective actions, panel resizes correctly, focus order is sensible, accessibility name flips between expanded and collapsed.

### Phase D — Per-extension callbacks (2E)

1. Add `OpenExtensionSettingsForId(Browser*, const std::string&)`.
2. Replace the per-chip `OpenExtensionsSettingsPaneForBrowser` binding inside `BuildInstalledExtensions(json, browser)` with a per-id binding. Keep the empty fallback and add chip on the shared route.
3. Introduce the test seam (pointer / repeating callback override) used by test 10.
4. Add tests 10, 11, 12 from §4.5.
5. Run canonical build.

Acceptance: tests green, build green, manual QA confirms per-extension chip activation routes to that extension's settings target (or per-id query-string fallback if a per-id pane is not reachable yet).

### Phase E — Optional blocked-count enrichment (2B)

1. Add `TryGetBlockedCountForActiveTab(Browser*)`.
2. Append the blocked-count suffix in `BuildBlockAdsAndTrackersRow(Browser*)` only when the helper returns a positive value, only for the immediately-rendered subtitle, and only if the toggle-flip path can preserve correctness (see §4.2 Change B).
3. Add tests 4, 5 from §4.5.
4. Run canonical build.

Acceptance: tests green, build green, manual QA confirms the count appears when the active tab has logged blocked requests and disappears on a clean tab. If gates fail, **skip this phase** and continue to G.

### Phase G — Camera screenshot launcher (2G)

This phase has two halves. Both must land before the user-facing slot leaves disabled-stub state.

1. **Prerequisite — screenshot command path.** Register a screenshot action ID in `maho_command_action_handler.{h,cc}` and dispatch it through whichever Chromium-side capture entry point lands in the local tree (preferred: `chrome::ExecuteCommand(browser, IDC_TAKE_SCREENSHOT)`, matching the existing print / copy-URL dispatch precedent). If the existing `IDC_*` enum or capture entry point is not reachable in the overlay, expand the work to introduce the minimal shell-side helper required, with no new FFI and no new Mojo. Add the matching dispatch test to the existing command handler unit-test target.
2. **Panel wiring.** Add `BuildScreenshotAction(Browser*)` to provider.h/.cc; replace the third entry in `BuildToolbarZone` (the `camera` slot) with `BuildScreenshotAction(browser)`; bind the callback to the new screenshot action ID; set `close_after_activate = true`; reuse `kCameraSymbolName`. Add the §4.5 / §4.9 unit tests (titled-`Screenshot` slot, has active-page callback wiring, disabled fallback, screenshot dispatch seam).
3. Run canonical build.

Acceptance: tests green, build green, manual QA shows clicking the camera icon launches the screenshot capture flow end-to-end. The `viewfinder` slot is wired separately by Phase H below.

If the prerequisite-path work in step 1 cannot land cleanly inside Phase 2, ship the panel-side wiring (step 2) behind a feature flag and **defer** the user-visible enablement to a follow-up. The plan does not invent a finished screenshot implementation that does not exist locally.

### Phase H — Viewfinder full-page capture (2H)

This phase rides on the prerequisite path established in Phase G. Both halves must land before the user-facing slot leaves disabled-stub state.

1. **Prerequisite — full-page capture command path.** Register a second action ID (e.g. `kMahoFullPageCaptureAction`) in `maho_command_action_handler.{h,cc}` alongside the screenshot action ID from Phase G, and dispatch it through whichever full-page capture entry point lands in the local tree. There is no upstream IDC for full-page capture, so this step is more open-ended than Phase G's: the most plausible target is a thin shell-side helper, kept entirely on the C++ side with no new FFI / Mojo / WebUI. Add the matching dispatch test to the existing command handler unit-test target.
2. **Panel wiring.** Add `BuildFullPageCaptureAction(Browser*)` to provider.h/.cc; replace the fourth entry in `BuildToolbarZone` (the `viewfinder` slot) with `BuildFullPageCaptureAction(browser)`; bind the callback to the new full-page capture action ID; set `close_after_activate = true`; reuse `kViewfinderSymbolName`. Add the §4.5 / §4.10 unit tests (titled-`Full-Page Capture` slot, has active-page callback wiring, disabled fallback, full-page capture dispatch seam — distinct from the screenshot dispatch seam).
3. Run canonical build.

Acceptance: tests green, build green, manual QA shows clicking the viewfinder icon triggers the full-page capture flow end-to-end. The `camera` slot continues to dispatch the standard screenshot.

If the prerequisite-path work in step 1 cannot land cleanly inside Phase 2 (the upstream-IDC absence makes this likelier than for Phase G), ship the panel-side wiring (step 2) behind a feature flag and **defer** the user-visible enablement to a follow-up. The plan does not invent a finished full-page capture implementation that does not exist locally.

### Phase F — Optional orphan / dead-helper cleanup (2F)

1. `git grep` for orphan symbols introduced or left behind by phases A through H.
2. If any are real, delete in a pure-deletion commit. No GN edits.
3. Run canonical build.

Acceptance: build green, all phase-1 and phase-2 tests still green. Skip the phase entirely if the grep is empty.

---

## 6. TDD / Unit-Test Plan

Test-first per phase. Each new test from §4.5 is added before the matching production change. The pre-existing Phase 1 suite must continue to pass at every commit boundary.

Test target: `maho_location_bar_utility_panel_provider_unittest` (the existing target, no new test binary). All tests live in `maho_location_bar_utility_panel_provider_unittest.cc`. Phases G and H additionally extend the existing command-handler unit-test target to cover the new screenshot and full-page capture action ID dispatches (mirroring existing print / copy-URL dispatch tests in `maho_command_action_handler`'s test file). No new test binary is added.

Test seam pattern for 2E: a process-local `base::RepeatingCallback<void(Browser*, const std::string&)>` test override inside the unnamed namespace of provider.cc, settable by tests through a header-friendly setter such as `internal::SetOpenExtensionSettingsForIdCallbackForTesting(...)`. When unset, the production `OpenExtensionSettingsForId` runs. This pattern matches existing seams in this directory.

Test seam pattern for 2G and 2H: same shape — two distinct `base::RepeatingCallback<void(Browser*)>` overrides (or equivalent action-ID dispatch overrides) inside the provider.cc unnamed namespace, settable from tests, one for the screenshot dispatch and one for the full-page capture dispatch. When unset, the production paths dispatch through `MahoCommandActionHandler` / `chrome::ExecuteCommand` / the chosen full-page helper. This keeps both screenshot and full-page capture tests free of a real capture pipeline and validates the dispatch contract without relying on a finished implementation.

No view-level unit tests are added. Phase 1 set the precedent that visual layout is exercised through the canonical build plus manual QA. Interactive UI tests and integration tests remain non-required.

---

## 7. Verification

The acceptance gates for Phase 2 are:

1. **Unit tests green.** `cd maho && bun run test` passes, and the provider unittest target passes under the canonical build. Specifically the new tests in §4.5 must pass for whichever phases ship.
2. **Canonical build green.** `python3 maho-chromium/build/scripts/build_maho.py` exits 0 from the workspace root.
3. **Manual QA.** A reviewer opens the address-bar utility panel and verifies, in order:
   - Wand icon opens the Boost bubble.
   - Footer ellipsis is interactive and toggles the overflow list.
   - Each overflow row activates: Shields opens the shield bubble, Zoom In / Out / Reset adjust the page zoom, Find in Page opens the find bar, Print opens the print preview.
   - Per-extension chips route to their own extension's settings target (or per-id fallback).
   - Block Ads & Trackers toggle still flips correctly; blocked-count suffix appears only when 2B shipped and the active tab has logged blocks.
   - Camera icon launches the standard screenshot capture flow when 2G's prerequisite path has landed; if 2G is still feature-flagged, the camera slot remains a disabled stub for users.
   - Viewfinder icon triggers the full-page capture flow when 2H's prerequisite path has landed; if 2H is still feature-flagged, the viewfinder slot remains a disabled stub for users. The two icons must dispatch to **distinct** capture targets — the camera slot must not behave like the viewfinder slot, and vice versa.

Optional, non-gating supporting checks:

- `cd maho && bun run typecheck` for early Rust regressions on the way into a Chromium build.
- `lsp_diagnostics` on each edited C++ file before each commit.

Out of scope as gates:

- Interactive UI tests at `maho_location_bar_utility_icon_view_interactive_uitest.cc`. Compile-only updates allowed.
- Integration tests of any kind.
- Screenshot diffs.

---

## 8. Risks & Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| `MahoBoostBubbleCoordinator::Show(Browser*)` requires the location bar's anchor element to already be live; calling it from a closing panel deactivates the anchor before the bubble opens. | Medium | `close_after_activate = true` already triggers a synchronous `widget->Close(...)` on the panel. The Boost bubble re-anchors to the location bar via `LocationBar::GetAnchorOrNull()` at `Show(...)` time, which re-resolves at runtime. Verified manually by reviewer in Phase A. If a race shows up, post the `Show(browser)` call to the next UI loop tick via `base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask`. |
| Resolving `Browser*` from `content::WebContents*` in `BuildFooterFromWebContents` introduces an unwanted dependency. | Low | `chrome::FindBrowserWithTab(...)` is already used elsewhere in this directory. If linkage is a concern, fall back to overload option 2 in §4.2 Change C. |
| Footer overflow expansion makes the panel taller than the screen in low-resolution windows. | Low | The synthetic settings-list zone uses the existing `kSettingsListRow` layout primitives that already wrap reasonably; if the panel exceeds available height it scrolls in the existing bubble container. No new size constraints. |
| `chrome::Zoom(Browser*, content::PageZoom)` parameters differ in the local build. | Low | Fall back to `chrome::ExecuteCommand(browser, IDC_ZOOM_IN / IDC_ZOOM_OUT / IDC_ZOOM_NORMAL)` already used in `maho_command_action_handler.cc`. |
| `MahoAdBlockTabHelper::FromWebContents(...)` returns nullptr early in tab lifetime. | Medium | `TryGetBlockedCountForActiveTab` returns `nullopt`, the row falls back to the Phase 1 subtitle. Test 5 covers this. |
| Per-extension URL fallback (`extensions/<id>` or `extensions?focus=<id>`) is not currently consumed by the settings UI. | Medium | This is acceptable for Phase 2: the goal is a per-id callback, not a per-id pane. The downstream pane consumer can read the suffix in a later phase. Documented in code comments above `OpenExtensionSettingsForId`. |
| Toggle-flip path on Block Ads & Trackers overwrites the blocked-count suffix. | Medium | If preserving the suffix across a flip would require touching `UpdateInlineTogglePresentation` in the view, drop 2B. No rework owed. |
| Sidebar regression from footer struct change. | Low | Public builder signatures stay byte-identical; default `overflow_actions = {}` makes the new field a no-op for sidebar consumers that don't render it. Sidebar build is part of the canonical-build gate. |
| No native screenshot command path exists in the Maho overlay yet, so 2G must establish one before the camera slot can be wired end-to-end. | High | Treat the prerequisite path as part of 2G's scope. Preferred landing: register `kMahoScreenshotCaptureAction` in `maho_command_action_handler.{h,cc}` and dispatch through `chrome::ExecuteCommand(browser, IDC_TAKE_SCREENSHOT)` (or the equivalent `IDC_*` already plumbed in `chrome/browser/ui/browser_commands.cc`). If that entry point is not reachable in the overlay, ship the panel-side wiring behind a feature flag and defer user enablement; do NOT invent a capture pipeline that does not exist. The plan owes no rework if 2G ships flagged. |
| No native full-page capture command path exists in the Maho overlay either, and there is no upstream IDC for full-page capture, so 2H's prerequisite is harder than 2G's. | High | Treat 2H's prerequisite as a separate but parallel work item to 2G's. Preferred landing: register `kMahoFullPageCaptureAction` in `maho_command_action_handler.{h,cc}` alongside the screenshot action ID, and dispatch through whichever full-page capture entry point is reachable on the C++ side (a thin shell-side helper, a DevTools-style protocol invocation kept entirely on C++ side, etc.) — no new FFI / Mojo / WebUI. If no clean entry point is reachable in Phase 2's envelope, ship the panel-side wiring behind a feature flag and defer user enablement; do NOT invent a capture pipeline that does not exist. Tests 17 / 18 / 19 in §4.5 deliberately validate dispatch metadata only, not a real image. |
| Camera and viewfinder slots accidentally dispatch the same action ID (visually distinct but functionally identical). | Medium | Tests 15 and 19 install two **distinct** test seams and assert each slot dispatches its own action ID exactly once. Manual QA explicitly checks the two icons trigger different flows. Code review must reject any change that collapses the two builders or seams into one. |
| Region capture is incorrectly treated as a separate Zen-parity action. | Low | Region capture is a mode within Zen's single `Browser:Screenshot` flow, not a top-level command. Maho's two slots are decomposed by *capture target* (visible page vs. full page), not by selection mode. If region selection ever ships in Maho, it lives inside the `camera` flow, not as a third toolbar slot. |

---

## 9. Atomic Commit Strategy

One commit per phase, message prefix `[utility-panel]`. Each commit must:

1. Compile under `python3 maho-chromium/build/scripts/build_maho.py`.
2. Pass the provider unittest target.
3. Touch only the files listed in its phase, plus mechanical updates required to keep the build green.
4. Avoid AI attribution in the commit message.
5. State plainly which Phase 2 item it addresses.

Suggested messages:

- `[utility-panel] Wire wand toolbar slot to Boost bubble`
- `[utility-panel] Add footer overflow_actions populated by provider`
- `[utility-panel] Render footer overflow as expandable settings-list section`
- `[utility-panel] Bind per-extension callbacks keyed off extension id`
- `[utility-panel] Append blocked-count suffix to Block Ads & Trackers row` (only if 2B ships)
- `[utility-panel] Register screenshot action ID in command handler` (Phase G prerequisite)
- `[utility-panel] Wire camera toolbar slot to screenshot capture flow` (Phase G panel wiring)
- `[utility-panel] Register full-page capture action ID in command handler` (Phase H prerequisite)
- `[utility-panel] Wire viewfinder toolbar slot to full-page capture flow` (Phase H panel wiring)
- `[utility-panel] Drop dead helpers after Phase 2 refactor` (only if 2F has real targets)

---

## 10. Must-Ship vs Deferred Recap

### Must ship in Phase 2

- 2A — Boost toolbar wiring via `MahoBoostBubbleCoordinator::GetForBrowser(browser).Show(browser)`.
- 2C — `MahoLocationBarUtilityPanelFooter::overflow_actions` populated by the provider.
- 2D — Footer overflow rendered inside the panel as a synthetic settings-list zone.
- 2E — Per-extension callbacks bound to each chip via `OpenExtensionSettingsForId`.
- 2G — `camera` toolbar slot wired to the standard screenshot capture flow via a new screenshot action ID in `maho_command_action_handler`. The capability source is Zen's single `Browser:Screenshot` command; the toolbar mapping is Maho's choice. If the prerequisite native screenshot command path cannot land cleanly inside Phase 2, ship the panel-side wiring behind a feature flag and defer user enablement; the panel-side wiring itself still ships.
- 2H — `viewfinder` toolbar slot wired to a dedicated full-page capture flow via a second action ID (`kMahoFullPageCaptureAction`) in `maho_command_action_handler`. This is a Maho-specific decomposition of Zen's screenshot capability into two distinct toolbar slots, not a Zen analog. If the prerequisite full-page capture command path cannot land cleanly inside Phase 2 (more likely than for 2G because no upstream IDC exists), ship the panel-side wiring behind a feature flag and defer user enablement; the panel-side wiring itself still ships.
- Public builder signatures unchanged.
- Phase 1 unit tests still green; new tests from §4.5 / §4.9 / §4.10 added per phase.
- Canonical build green and manual QA pass.

### Optional / gated

- 2B — Blocked-count enrichment for Block Ads & Trackers, only if synchronous-and-testable.
- 2F — Orphan / dead-helper cleanup, only if real after 2A/C/D/E/G/H.

### Explicitly deferred (not Phase 2)

- A region-only top-level screenshot toolbar action — region capture is a mode inside the `camera` flow, not a parallel toolbar slot. Maho's two slots are decomposed by capture target, not by selection mode.
- A finished cross-platform screenshot or full-page capture pipeline — Phases 2G and 2H wire the launchers through whatever Chromium-side capture entry point is reachable; building a richer capture pipeline (overlay UI parity with Zen's selection mode, alternative full-page implementations, format / annotation controls, etc.) is a follow-up.
- Footer overflow shipped as a popover, menu, or new bubble.
- WebUI migration of any part of this panel.
- Any new Mojo IDL or new FFI surface.
- New Rust crates or `maho-core` / `maho-ffi` changes.
- Adding `relay/` or `mail/` as workspace members.
- Sidebar-specific builder fork.
- GN-related cleanup (BUILD.gn dependency adjustments, target reorganization). This is not part of must-ship and is not folded into 2F. Defer entirely.

---

## 11. Definition of Done

Phase 2 is complete when **all** of the following hold simultaneously:

1. The wand toolbar slot opens the Boost bubble through `MahoBoostBubbleCoordinator`. The `camera` toolbar slot launches the standard screenshot capture flow through the new screenshot action ID in `maho_command_action_handler` (or, if 2G is feature-flagged, the `camera` slot remains a disabled stub for users until the prerequisite path lands). The `viewfinder` toolbar slot triggers the dedicated full-page capture flow through the second action ID added in `maho_command_action_handler` (or, if 2H is feature-flagged, the `viewfinder` slot remains a disabled stub for users until the prerequisite full-page capture path lands). The two slots dispatch **distinct** action IDs and represent distinct capture targets — visible page (camera) vs. full page (viewfinder).
2. `MahoLocationBarUtilityPanelFooter` carries an `overflow_actions` vector populated by the provider with `Shields`, `Zoom In`, `Zoom Out`, `Reset Zoom`, `Find in Page`, `Print`, in that order, with correct enabled state and callbacks.
3. The view renders the overflow section as an expandable settings-list-style block under the security pill, using the existing `kSettingsList` layout, no new bubble.
4. Each extension chip's callback is bound to that extension's id via `OpenExtensionSettingsForId`. The empty fallback chip and the add chip continue to use the shared route.
5. Public `BuildMahoLocationBarUtilityPanelModel(LocationBar*)` and `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` signatures are byte-identical to pre-plan state.
6. Provider unittest covers the new tests in §4.5 / §4.9 / §4.10 for whichever phases shipped, plus the new screenshot and full-page capture dispatch tests in the existing command handler unit-test target.
7. `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
8. Manual QA passes the §7 checklist.
9. Sidebar compiles and runs without source edits.
10. No commit message contains AI attribution.
11. No new Mojo / FFI was added; no new WebUI surface was added; no GN file was edited as a side effect. The `camera` and `viewfinder` slots were modified only through the additive screenshot and full-page capture action IDs in `maho_command_action_handler` and the new `BuildScreenshotAction` / `BuildFullPageCaptureAction` builders in the provider.

When all eleven hold, the Phase 2 envelope is closed and the panel is at its target shipping shape. The toolbar's two capture slots reflect Maho's deliberate decomposition of Zen's single screenshot capability into a standard screenshot launcher and a dedicated full-page capture action, both wired end-to-end (or behind a feature flag if their prerequisite paths could not land cleanly inside Phase 2).
