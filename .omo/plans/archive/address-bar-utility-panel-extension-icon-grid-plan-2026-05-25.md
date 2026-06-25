# Address Bar Utility Panel — Extension Icon Grid (Phase 3)

**Status:** Ready for execution (planning)
**Surface:** `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_*`
**Mode:** Phase 3 — replace the text-based extension chip row with a real extension icon grid that mirrors Chrome's native extension toolbar interactions
**Parent plans:**
- `.sisyphus/plans/address-bar-utility-panel-arc-parity-plan.md` (Phase 1, shipped)
- `.sisyphus/plans/address-bar-utility-panel-phase-2-plan.md` (Phase 2, in-flight; 2E per-extension callback overlaps with this plan and is superseded by Phase 3 §4.2 if Phase 3 lands first)
**Related upstream work:** `.sisyphus/plans/extension-state-sync-implementation-plan-2026-05-25.md` (settings extension pane source-of-truth fix; ships the `MahoExtensionStateBridge` this plan reuses)
**Verification:** Unit tests + canonical `python3 maho-chromium/build/scripts/build_maho.py` + manual smoke (extension popup invocation cannot be unit-tested end-to-end without an integration harness)

---

## 1. Overview

After Phase 1 / Phase 2, the address-bar utility panel shows extensions as **text chips** carrying name + version + Enabled/Disabled, with all click callbacks routing to `chrome://maho-settings#extensions`. This phase replaces that row with a **real extension icon grid** so the popup behaves like Chrome's native puzzle-piece extensions menu:

1. Each installed extension is rendered as its **actual extension icon** (manifest icon or per-tab `ExtensionAction` icon), not a uniform SF symbol with text label.
2. Clicking an icon **invokes the extension's action** (opens popup, or runs background action if no popup), exactly like Chrome's native toolbar.
3. Right-clicking an icon shows the **standard Chrome extension context menu** (Manage, Remove, Pin, View web permissions, etc.) via `extensions::ExtensionContextMenuModel`.
4. The `Extensions` section header gains a right-aligned **`Manage` affordance** that opens `chrome://maho-settings#extensions`.
5. The `+` add-extension chip stays, with a clarified target (Web Store or settings — picked at implementation time per §5.5).

Phase 3 also flips the panel's **extension data source** from `maho_core_get_installed_extensions` (Rust core JSON cache) to `extensions::ExtensionRegistry::Get(profile)` directly. This aligns the popup with the source-of-truth fix already shipped in `MahoExtensionStateBridge` and gives us live access to `Extension*` and `ExtensionAction*` objects, which the icon grid and action-runner paths need.

---

## 2. Scope

### Must ship

- **3A — Icon source switch.** Stop reading `maho_core_get_installed_extensions` for this row. Read enabled + disabled extensions from `extensions::ExtensionRegistry::Get(profile)` on the UI thread, mirroring `MahoSettingsPageHandler::BuildInstalledExtensionsFromRegistry()`.
- **3B — Real icon rendering.** Each chip is replaced by an icon-only square (target visual ≈28–32px) showing the extension's icon. Acquire the icon through `extensions::ExtensionActionIconFactory` (badge-aware, per-tab) when an `ExtensionAction` exists for the extension, else through `extensions::IconImage` over `extension->icons()`. Async load is handled with the existing `views::ImageView` and an `IconImage::Observer` so the chip refreshes when the bitmap arrives.
- **3C — Click invokes the extension action.** Use `extensions::ExtensionActionRunner::GetForWebContents(web_contents)->RunAction(extension, /*grant_active_tab=*/true)`. This handles popup vs. no-popup, host permission dialog, MV3 service worker wakeup. Our utility panel bubble must close before invocation so the extension popup can anchor at the location bar (§5.4).
- **3D — Right-click context menu.** Build `extensions::ExtensionContextMenuModel(extension, browser, ...)` and run it via `views::MenuRunner` anchored on the chip view. This matches Chrome's puzzle-piece menu behavior.
- **3E — Manage link in section header.** The `Extensions` zone header gets a right-aligned `Manage` text button. Activation calls `maho::OpenMahoSettingsPane(browser, "extensions")` and closes the bubble.
- **3F — Live refresh while popup is open.** Subscribe to `MahoExtensionStateBridge` from the panel view (or the coordinator) for the lifetime of the bubble. On `OnInstalledExtensionsChanged()`, rebuild the extensions zone in place. The bridge already exists from the settings-pane fix; we are only adding a second observer.
- **3G — Empty state.** When the registry has zero non-component, user-visible extensions, render a single muted "No extensions" tile and the trailing `+` chip. No mid-row text fallback.

### Optional (justified, gated, may slip)

- **3H — Pin badge on icons.** Chip overlay showing whether the extension is pinned to the toolbar (mirroring Chrome's puzzle-piece menu pin state). Requires reading `ExtensionsContainer::IsActionVisibleOnToolbar(...)` (or equivalent in our Maho overlay path). Ship if the read is synchronous and unit-testable; otherwise drop. No rework owed.
- **3I — Disabled-extension visual differentiation.** Extensions in `disabled_extensions()` render at reduced opacity / grayscale. Cheap and orthogonal; ship together with §3B unless it complicates `IconImage` plumbing.
- **3J — Keyboard navigation across the grid.** Arrow-key focus traversal between chips, Enter/Space to invoke, ContextMenu key to open the right-click menu. Ship if `views::FocusManager` plus the existing focus behavior on `MahoLocationBarUtilityPanelActionView` covers it without new infrastructure. Otherwise log a follow-up.

### Out of scope

- **Extension install flow inside the popup.** The `+` button still navigates externally (Web Store or settings). Phase 3 does not host an install UI in the bubble.
- **Pin/unpin from inside the popup outside the context menu.** The standard pin toggle lives inside `ExtensionContextMenuModel`; we surface it there, not as a top-level chip affordance.
- **Reordering by drag-and-drop** in the icon grid.
- **Replacing or hiding `chrome://extensions` (Chromium's native page).** That page remains the canonical full-management surface (developer mode, unpacked load, packaging, site access UI, error console).
- **Replacing the per-tab right-side native action toolbar** (extension actions pinned next to the omnibox). This plan only reshapes the in-bubble grid.
- Any new Mojo IDL, FFI export, or Rust-side change.
- Adding `relay/` or `mail/` as workspace members.
- Sidebar consumer fork — public builder signatures stay byte-identical (§5.6).
- WebUI migration of the location-bar utility panel.

---

## 3. Grounded Current State

Files in scope (verified to exist on disk):

- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.{h,cc}`
- `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider_unittest.cc`
- `maho-chromium/browser/extensions/maho_extension_state_bridge.{h,cc}` (built by `extension-state-sync-implementation-plan-2026-05-25`)
- `maho-chromium/browser/BUILD.gn` (this is where the `maho_location_bar_views` GN target lives — there is no separate `location_bar/BUILD.gn`)

Current behavior (verified by reading the provider/view/coordinator code):

- `BuildExtensionsZone(json, browser)` reads `ReadInstalledExtensionsJsonOrFallback()` → `maho_core_get_installed_extensions(core)` and parses JSON with `id`, `name`, `version`, `enabled` fields.
- `BuildInstalledExtensions(json, browser)` produces `MahoLocationBarUtilityPanelAction` items with:
  - `title = name` (or id, or "Unnamed extension")
  - `subtitle = "v{version} · Enabled/Disabled"`
  - `sf_symbol_name = "puzzlepiece.extension"` (the same SF Symbol for **every** extension)
  - `callback` bound to `OpenExtensionsSettingsPaneForBrowser` (a single shared callback that opens settings)
  - `close_after_activate = true`
- `MakeAddExtensionChip(browser)` produces a `+` chip that also routes to `OpenExtensionsSettingsPaneForBrowser`.
- The view renders chips via `MahoLocationBarUtilityPanelActionView::DisplayMode::kExtensionChip`. Each chip is `kChipHeight=30` tall with an SF Symbol icon plus text label and tail-elide subtitle. Chip layout uses `views::FlexLayout` with `kChipRowSpacing=8`.
- `MahoLocationBarUtilityPanelView::AddZoneHeader(...)` draws a single left-aligned label, no right-aligned action affordance.
- Bubble lifecycle: `MahoLocationBarUtilityBubbleCoordinator` manages a single `MahoLocationBarUtilityPanelView` widget and observes the tab strip to auto-close on tab switch. There is no extension-state observer wired anywhere in this surface today.
- The extensions sync work already shipped:
  - `MahoExtensionStateBridge` exists, owns a `base::ObserverList<Observer>`, observes `ExtensionRegistry`, and pushes notifications when extensions load / unload / install / uninstall.
  - `MahoExtensionStateBridge::FromProfile(profile)` returns the per-profile bridge or null for OTR / unmapped profiles.
- The `maho_settings` WebUI's `MahoSettingsPageHandler::BuildInstalledExtensionsFromRegistry()` is the existing reference pattern for reading the registry directly on the UI thread (`DCHECK_CURRENTLY_ON(content::BrowserThread::UI)`).

Chromium APIs we will call (verified to exist upstream; subject to local availability checks during implementation):

- `extensions::ExtensionRegistry::Get(BrowserContext*)` → enabled / disabled / terminated / blocklisted sets.
- `extensions::ExtensionActionManager::Get(BrowserContext*)->GetExtensionAction(const Extension&)` → may be null if no action.
- `extensions::ExtensionActionIconFactory(profile, extension, action, observer)` → manages tab-aware icon (badge state).
- `extensions::IconImage(profile, extension, icons_set, resource_size_in_dip, default_icon, observer)` → fallback when no `ExtensionAction`.
- `extensions::ExtensionActionRunner::GetForWebContents(WebContents*)->RunAction(const Extension&, bool grant_active_tab)` → invokes popup / background.
- `extensions::ExtensionContextMenuModel(extension, browser, button_visibility, delegate, can_show_icon_in_toolbar)` → standard right-click menu model.
- `extensions::Manifest::ShouldDisplayInExtensionSettings(...)` (or `extension->should_display_in_extension_settings()`) for filtering component / hidden extensions.

Known Chromium-side seams we may need (called out as risks if not directly reachable from the overlay GN target):

- Pinning state read: `ToolbarActionsModel::Get(profile)->IsActionPinned(action_id)` — gated by §3H.
- Bubble anchoring for the extension popup after our bubble closes: handled by Chromium's `ExtensionPopupViewer` once `RunAction` fires; the anchor it uses is the `ExtensionsToolbarContainer`. If our location bar exposes a per-extension button, the popup anchors there; otherwise it anchors at a fallback. Verified during 3C implementation; documented in §5.4.

Phase 1 / Phase 2 unit tests cover the existing chip-row layout (count + ordering + symbol name + callback presence). Phase 3 must update those tests to new contracts (icon-by-extension, per-extension callback, manage link) without regressing zoom / settings / footer / Boost coverage.

---

## 4. Target Reference Shape

```
┌──────────────────────────────────────────────────┐
│  [share]  [wand]  [camera]  [viewfinder]        │   ← Toolbar (Phase 1/2)
│                                                  │
│  Extensions                            Manage    │   ← Header + right-aligned action (NEW)
│  [icon] [icon] [icon] [icon]                    │   ← Real extension icons (NEW)
│  [icon] [icon] [icon]  [+]                      │
│                                                  │
│  Settings                                        │
│  [icon]  Developer Mode                          │
│  [icon]  Block Ads & Trackers           [tog.]  │
│  [icon]  Automatic Picture-In-Picture            │
│                                                  │
│  🔒 maho.com                              …      │
└──────────────────────────────────────────────────┘
```

Behavior matrix for the icon grid:

| Action | Behavior |
|---|---|
| Left click | Bubble closes → `ExtensionActionRunner::RunAction(extension, true)` |
| Right click (or Ctrl+click on Mac) | Open `ExtensionContextMenuModel` anchored on chip |
| Hover | Tooltip = extension name + (Disabled) suffix when applicable |
| Disabled extension chip | Reduced opacity (3I); left-click still tries `RunAction` (Chrome lets the registrar reject), or no-op if we want Chrome-style "click does nothing while disabled". Final pick at §5.3. |
| ContextMenu key (keyboard) | Same as right-click |
| Enter / Space | Same as left-click |
| `+` chip click | Opens add path (Web Store or settings, picked at §5.5) |
| `Manage` header link | `OpenMahoSettingsPane(browser, "extensions")` |

Live update: when any extension state changes while the bubble is open, the extensions zone rebuilds in place. Other zones stay untouched. Other surfaces (settings WebUI) are already handled by the existing bridge.

---

## 5. Key Design Decisions

### 5.1 Extension data source moves to `ExtensionRegistry`

The popup stops reading the Rust core JSON cache for this row. We read directly from `extensions::ExtensionRegistry::Get(profile)`, walking `enabled_extensions()` and `disabled_extensions()` (and optionally `terminated_extensions()` per §5.7). This gives us:

- Live `Extension*` objects needed by `ExtensionActionIconFactory`, `ExtensionActionRunner`, and `ExtensionContextMenuModel`.
- A single source of truth shared with the settings WebUI.
- No JSON parse cost on every popup open.

The Rust core cache via `MahoExtensionStateBridge::PushFullSnapshot()` keeps populating for native shells (iOS/Android), but the Chromium UI no longer consumes it for this surface.

### 5.2 `Profile*` is sourced from the browser

`BuildExtensionsZone` already takes `Browser*`. `Profile* profile = browser->profile()` for read; OTR profiles are treated like the bridge does — we still render the registry view (Chromium owns OTR registry separately), but mutation paths defer to whatever `ExtensionContextMenuModel` enforces. We do **not** introduce new OTR-specific behavior here.

### 5.3 Icon source resolution order

Per chip, in order:

1. If `ExtensionActionManager::Get(profile)->GetExtensionAction(extension)` returns non-null, use `ExtensionActionIconFactory(profile, extension, action, observer)`. This is what Chrome's toolbar uses; gives badge-aware, per-tab icons.
2. Otherwise, use `extensions::IconImage(profile, extension, IconsInfo::GetIcons(extension), kIconSizeDIP, default_icon, observer)`.
3. While the bitmap is loading, render a placeholder square in `ui::kColorSysSurface2`.
4. Disabled extensions get reduced opacity (§3I).

Each chip owns its `IconImage` / `ExtensionActionIconFactory` for its lifetime; both have `Observer` callbacks that we route to `views::ImageView::SetImage(...)` plus `chip->SchedulePaint()`.

Tooltip text for disabled extensions: `"{name} (Disabled)"`.

For "click while disabled": **left-click is a no-op visually** (we suppress invocation) and right-click still opens the context menu so the user can enable. This matches Chrome's behavior for the puzzle-piece menu and is simpler than partial enablement.

### 5.4 Bubble close + extension popup anchoring

Calling `ExtensionActionRunner::RunAction(...)` from inside our bubble while the bubble is still open creates two competing widgets — the extension popup tries to anchor itself, but our bubble may steal focus or block it. The accepted pattern in Chromium is:

1. Set `close_after_activate = true` on the chip's action so the bubble closes via the existing `MahoLocationBarUtilityPanelActionView::OnMousePressed` flow.
2. Schedule `RunAction` on the next UI-thread task (post via `content::GetUIThreadTaskRunner({})->PostTask(...)`) so the close completes first.
3. The extension popup then anchors at the upstream `ExtensionsToolbarContainer` location (next to the omnibox). This is the same anchor Chrome's puzzle-piece menu uses when its own popup closes.

If during implementation it turns out our overlay does not host an `ExtensionsToolbarContainer` and the popup has no visible anchor, the fallback is to anchor the popup at the location-bar utility icon view (`MahoLocationBarUtilityIconView`) — the same view our bubble was anchored to. Pick whichever yields a popup that lands within the browser window; document the choice in code.

### 5.5 `+` chip target

Two acceptable behaviors. Pick one at implementation time and pin it in tests:

- (A) Open Chrome Web Store (`chrome.google.com/webstore`) in a new tab.
- (B) Open `chrome://maho-settings#extensions` (current behavior, retained).

(A) matches Chrome's puzzle-piece menu's `Discover more extensions` affordance better. (B) is what's wired today and requires no new logic. Decide at implementation time based on whether we want add-flow consistency with Chrome (A) or want to keep settings as the single management hub (B).

### 5.6 Public builder signatures stay byte-identical

`BuildMahoLocationBarUtilityPanelModel(LocationBar*)` and `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)` keep their signatures. The sidebar consumer therefore needs no source edits.

`BuildExtensionsZone(...)` is `internal::`, so its signature can change. We add a richer per-extension model that carries `extension_id` plus a callback bag (run / context-menu / manage). The action struct is extended additively (§6.1).

### 5.7 Filter component / hidden extensions

Match Chrome's puzzle-piece menu: only show extensions where `extension->should_display_in_extension_settings()` is true. This excludes component, internal, and hidden extensions consistent with `chrome://extensions`. The settings WebUI bridge work (`BuildInstalledExtensionsFromRegistry()`) currently does **not** filter; aligning the two is a follow-up but not a Phase 3 must-ship if it would entangle scope (note in §10).

### 5.8 Live refresh subscription scope

The `MahoLocationBarUtilityPanelView` itself becomes a `MahoExtensionStateBridge::Observer` for the lifetime of its widget. On notify:

1. Rebuild the extensions zone using the same `BuildExtensionsZone(profile_or_browser)` path.
2. Replace the extensions section's `views::View` subtree in place. Keep other zones untouched.
3. `InvalidateLayout()` and `SizeToContents()` so the bubble grows/shrinks.

If a bridge does not exist for the active profile (OTR or pre-init), the view skips registration and behaves like Phase 2 (no live updates). Mutations through the context menu still work because `ExtensionContextMenuModel` talks to `ExtensionRegistrar` directly.

### 5.9 Verification approach

Unit tests cover the model-building and action-binding contracts. Hands-on browser execution covers what unit tests cannot reach: extension popup invocation, context menu rendering, and live refresh on registry events. Acceptance gates (§9) are the unit tests + canonical build only; manual smoke is documented but non-gating because the previous QA agent confirmed the local environment cannot sustain a Maho process for end-to-end browser automation.

---

## 6. File-by-File Implementation Plan

### 6.1 `maho_location_bar_utility_panel_provider.h`

**Change A — Extend `MahoLocationBarUtilityPanelAction` additively** so per-extension callbacks fit without breaking existing call sites:

```cpp
struct MahoLocationBarUtilityPanelAction {
  // ... existing fields unchanged ...

  // NEW. When set, the chip rendering layer should request the extension
  // icon from ExtensionActionIconFactory or IconImage rather than render an
  // SF symbol.
  std::string extension_id;

  // NEW. When set, used as the right-click context-menu builder. The
  // MenuRunner anchors itself on the chip view.
  base::RepeatingCallback<void(views::View* anchor)> context_menu_callback;
};
```

`extension_id` empty means "render SF symbol as today". Existing chips and toolbar buttons therefore see no behavior change.

**Change B — Declare new internal builders:**

```cpp
namespace internal {

// Builds the icon grid items for the extensions zone. Reads from
// ExtensionRegistry::Get(profile).
std::vector<MahoLocationBarUtilityPanelAction> BuildExtensionIconActions(
    Browser* browser);

// Builds the right-aligned "Manage" header action.
MahoLocationBarUtilityPanelAction BuildExtensionsManageAction(Browser* browser);

}  // namespace internal
```

**Change C — Extend `MahoUtilityPanelZone`** with an optional right-aligned header action:

```cpp
struct MahoUtilityPanelZone {
  // ... existing fields unchanged ...
  std::optional<MahoLocationBarUtilityPanelAction> header_trailing_action;
};
```

Only the extensions zone populates this in Phase 3. Toolbar / settings zones leave it `nullopt`.

### 6.2 `maho_location_bar_utility_panel_provider.cc`

**Change A — Replace text-chip extensions zone with icon-grid zone.**

Drop these helpers from the visible path (keep them around only if other tests still exercise them — otherwise delete):

- `ReadInstalledExtensionsJsonOrFallback()`
- `BuildInstalledExtensions(const std::string&, Browser*)` (the JSON parser)
- The shared `OpenExtensionsSettingsPaneForBrowser` callback wiring on per-extension chips (replaced by per-id callbacks)

Add `BuildExtensionIconActions(Browser* browser)`:

```cpp
std::vector<MahoLocationBarUtilityPanelAction> BuildExtensionIconActions(
    Browser* browser) {
  std::vector<MahoLocationBarUtilityPanelAction> actions;
  if (!browser) {
    return actions;
  }

  Profile* profile = browser->profile();
  if (!profile) {
    return actions;
  }

  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return actions;
  }

  auto append_set =
      [&](const extensions::ExtensionSet& set, bool enabled) {
        for (const auto& ext : set) {
          if (!ext->should_display_in_extension_settings()) {
            continue;
          }
          MahoLocationBarUtilityPanelAction action;
          action.title = base::UTF8ToUTF16(ext->name());
          action.accessible_name = enabled
              ? action.title
              : action.title + u" (Disabled)";
          action.enabled = true;  // disabled extensions still show; click
                                  // semantics handled in §5.3.
          action.data_state = MahoLocationBarUtilityPanelDataState::kLive;
          action.extension_id = ext->id();
          action.close_after_activate = true;

          // Left-click: invoke action runner (popup / background). Posted
          // to next UI task so our bubble closes first.
          action.callback = base::BindRepeating(
              &InvokeExtensionActionForBrowser, base::Unretained(browser),
              ext->id());

          // Right-click: build and run ExtensionContextMenuModel.
          action.context_menu_callback = base::BindRepeating(
              &ShowExtensionContextMenuForBrowser,
              base::Unretained(browser), ext->id());

          actions.push_back(std::move(action));
        }
      };

  append_set(registry->enabled_extensions(), /*enabled=*/true);
  append_set(registry->disabled_extensions(), /*enabled=*/false);
  return actions;
}
```

**Change B — Click invokes the action runner.**

```cpp
void InvokeExtensionActionForBrowser(Browser* browser,
                                     const std::string& extension_id) {
  if (!browser) {
    return;
  }
  content::WebContents* web_contents = GetActiveWebContents(browser);
  if (!web_contents) {
    return;
  }
  Profile* profile = browser->profile();
  if (!profile) {
    return;
  }
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }
  const extensions::Extension* extension =
      registry->GetInstalledExtension(extension_id);
  if (!extension) {
    return;
  }
  if (!extension->is_enabled()) {
    // §5.3: click on disabled chip is a no-op; right-click context menu
    // remains the path to enable.
    return;
  }
  // Defer until our bubble close completes so the extension popup can
  // anchor cleanly. See §5.4.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> wc,
             scoped_refptr<const extensions::Extension> ext) {
            if (!wc) return;
            auto* runner =
                extensions::ExtensionActionRunner::GetForWebContents(
                    wc.get());
            if (!runner) return;
            runner->RunAction(*ext, /*grant_active_tab=*/true);
          },
          web_contents->GetWeakPtr(),
          scoped_refptr<const extensions::Extension>(extension)));
}
```

`scoped_refptr<const Extension>` keeps the extension alive across the post.

**Change C — Right-click builds the context menu.**

```cpp
void ShowExtensionContextMenuForBrowser(Browser* browser,
                                        const std::string& extension_id,
                                        views::View* anchor) {
  if (!browser || !anchor) {
    return;
  }
  Profile* profile = browser->profile();
  if (!profile) {
    return;
  }
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  const extensions::Extension* extension =
      registry ? registry->GetInstalledExtension(extension_id) : nullptr;
  if (!extension) {
    return;
  }

  // Build the standard Chrome extension context menu. The exact constructor
  // signature varies across Chromium revisions; the implementer should
  // match the local one. The intent is the menu Chrome's puzzle-piece menu
  // builds (Manage extension, Remove from Maho, Pin/Unpin from toolbar,
  // View web permissions, Inspect popup when devtools).
  auto menu_model = std::make_unique<extensions::ExtensionContextMenuModel>(
      extension, browser, /* button_visibility */ kVisible, /* delegate */
      nullptr, /* can_show_icon_in_toolbar */ true);

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      anchor->GetWidget(), /* button_controller */ nullptr,
      anchor->GetBoundsInScreen(), views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
  // Keep the model alive until menu close — store it on the runner owner.
}
```

The implementer must verify the local `ExtensionContextMenuModel` constructor in this Chromium revision; if it differs, adapt arguments without changing call-site shape. `context_menu_runner_` ownership lives on whichever class hosts this callback (likely the panel view, see §6.4).

**Change D — `BuildExtensionsZone` rewrite:**

```cpp
MahoUtilityPanelZone BuildExtensionsZone(Browser* browser) {
  MahoUtilityPanelZone zone;
  zone.id = MahoUtilityPanelZoneId::kExtensions;
  zone.style = MahoUtilityPanelZoneStyle::kChipRow;
  zone.header_label = u"Extensions";
  zone.header_trailing_action = BuildExtensionsManageAction(browser);

  std::vector<MahoLocationBarUtilityPanelAction> items =
      BuildExtensionIconActions(browser);

  if (items.empty()) {
    MahoLocationBarUtilityPanelAction empty_chip;
    empty_chip.title = u"No extensions";
    empty_chip.enabled = false;
    empty_chip.data_state = MahoLocationBarUtilityPanelDataState::kLive;
    empty_chip.sf_symbol_name = kExtensionsSymbolName;
    zone.items.push_back(std::move(empty_chip));
  } else {
    for (auto& item : items) {
      zone.items.push_back(std::move(item));
    }
  }

  zone.items.push_back(MakeAddExtensionChip(browser));
  return zone;
}
```

`MakeAddExtensionChip(browser)` keeps current behavior unless §5.5 picks (A); in that case it's rewired to open the Web Store.

**Change E — `BuildExtensionsManageAction` builder:**

```cpp
MahoLocationBarUtilityPanelAction BuildExtensionsManageAction(Browser* browser) {
  MahoLocationBarUtilityPanelAction action;
  action.title = u"Manage";
  action.accessible_name = u"Manage extensions";
  action.enabled = browser != nullptr;
  action.data_state = MahoLocationBarUtilityPanelDataState::kLive;
  action.close_after_activate = true;
  if (browser) {
    action.callback = base::BindRepeating(
        &OpenExtensionsSettingsPaneForBrowser, base::Unretained(browser));
  }
  return action;
}
```

**Change F — `PopulateArcShapeModelData` plumbing:**

```cpp
void PopulateArcShapeModelData(MahoLocationBarUtilityPanelModel& model,
                               Browser* browser) {
  model.zones.push_back(BuildToolbarZone(browser));
  model.zones.push_back(BuildExtensionsZone(browser));
  model.zones.push_back(BuildSettingsZone(browser));
}
```

Note: `ReadInstalledExtensionsJsonOrFallback()` and the JSON parsing path leave the visible flow. If `BuildInstalledExtensions(const std::string&, Browser*)` is still referenced by tests, retain the function signature but mark it as legacy with a comment that the live path no longer uses it; otherwise delete.

### 6.3 `maho_location_bar_utility_panel_view.h`

Add a new `DisplayMode::kExtensionIconChip` (or equivalent) to `MahoLocationBarUtilityPanelActionView`:

```cpp
enum class DisplayMode {
  kToolbarButton,
  kExtensionChip,         // legacy text chip — kept for stub fallback path
  kExtensionIconChip,     // NEW — extension icon, no text
  kSettingsRow,
};
```

Add a `views::MenuRunner` slot on the panel view (or on a per-chip wrapper) to host the right-click menu lifetime.

Add an observer field:

```cpp
private:
  base::ScopedObservation<maho::MahoExtensionStateBridge,
                          maho::MahoExtensionStateBridge::Observer>
      bridge_observation_{this};
  raw_ptr<views::View> extensions_section_root_ = nullptr;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
```

The view implements `MahoExtensionStateBridge::Observer::OnInstalledExtensionsChanged()`.

### 6.4 `maho_location_bar_utility_panel_view.cc`

**Change A — Render `kExtensionIconChip` mode** in `MahoLocationBarUtilityPanelActionView`:

- 32×32 dip square (or whatever matches the reference design more precisely).
- ImageView positioned center; size 16×16 dip for the icon (Chrome's standard toolbar action icon size is 16 dip; verify against the reference screenshot).
- Background = `ui::kColorSysSurface1` rounded-rect; selection background follows existing `selection_background_` machinery for hover.
- Icon source resolution per §5.3.
- Owns a `std::unique_ptr<extensions::IconImage>` or `extensions::ExtensionActionIconFactory`; the factory's observer callback updates the `ImageView` and triggers `SchedulePaint()`.
- `OnMousePressed`:
  - Left button → existing callback path (which already runs `RunAction` via §6.2 Change B).
  - Right button or Ctrl+click → invoke `context_menu_callback` with `this` as the anchor view.
- `OnGestureEvent` for long-press → context menu (touch parity, optional).

**Change B — Render the `Manage` header trailing action.**

Update `AddZoneHeader(const std::u16string&)` to also accept a trailing action:

```cpp
void AddZoneHeader(const std::u16string& header_label,
                   std::optional<MahoLocationBarUtilityPanelAction>
                       trailing_action);
```

If `trailing_action` is set, render its title as a right-aligned focusable text button styled like a secondary link. Click invokes `trailing_action.callback`. Use `ui::kColorSysOnSurfaceSubtle` to match other header text.

**Change C — Live refresh subscription.**

In the view constructor (after `BuildContents()` runs):

```cpp
if (Browser* browser = LookupBrowser(); browser) {
  if (auto* bridge = maho::MahoExtensionStateBridge::FromProfile(
          browser->profile())) {
    bridge_observation_.Observe(bridge);
  }
}
```

`LookupBrowser()` is a small helper — easiest path is to plumb the `Browser*` into the view via the model or via the coordinator (§6.5). The plan already extracts a `Browser*` in `BuildExtensionsZone`; threading it through the model is a one-field addition (e.g. `MahoLocationBarUtilityPanelModel::owning_browser` as a `base::WeakPtr<Browser>` or a `raw_ptr<Browser>`). Pick whichever is consistent with how the Settings zone already grabs profile state.

`MahoLocationBarUtilityPanelView::OnInstalledExtensionsChanged()`:

```cpp
void MahoLocationBarUtilityPanelView::OnInstalledExtensionsChanged() {
  Browser* browser = ResolveOwningBrowser();
  if (!browser) {
    return;
  }
  RebuildExtensionsZone(browser);
  InvalidateLayout();
  SizeToContents();
}
```

`RebuildExtensionsZone(...)` removes the existing extensions zone subtree (`extensions_section_root_`) and re-adds it from a freshly-built zone. Other zones untouched.

**Change D — Bubble close before action invocation.**

`MahoLocationBarUtilityPanelActionView::OnMousePressed` already closes the bubble via `widget->CloseWithReason(...)` when `close_after_activate == true`. The action callback runs inside the same press handler. Move the callback invocation to **after** widget close, or rely on §6.2 Change B's `PostTask` to defer. Both work; pick `PostTask` because it's local to the extension-action callsite and does not change the existing close-on-activate flow.

### 6.5 `maho_location_bar_utility_bubble_coordinator.{h,cc}`

Minor: when the coordinator builds the model, ensure the `Browser*` flows into the model so the view can find it for live-refresh subscription (§6.4 Change C). If the existing coordinator already passes `Browser*` to `BuildMahoLocationBarUtilityPanelModelForBrowser(...)`, no edits beyond plumbing the result onto `MahoLocationBarUtilityPanelModel::owning_browser` are needed.

### 6.6 `maho_location_bar_utility_panel_provider_unittest.cc`

Phase 1 / 2 tests covering toolbar / extensions / settings ordering and footer state stay. Update or replace the extensions-related tests:

**Replaced:**

- The Phase-1 / 2 extensions-zone tests asserting text chips with `puzzlepiece.extension` SF symbol and shared callback are no longer correct. Rewrite them.

**New tests (provider unit):**

1. `ExtensionsZoneReadsFromExtensionRegistryNotCoreCache` — install a fake registry seeded with two enabled + one disabled extension; assert the zone exposes three icon actions in the right order, each carrying `extension_id` set.
2. `ExtensionsZoneFiltersByShouldDisplayInExtensionSettings` — seed registry with a component / hidden extension; assert it does **not** appear.
3. `ExtensionsZoneEmptyRegistryProducesNoExtensionsChipPlusAddChip` — exact-count two items.
4. `ExtensionsZoneAlwaysAppendsAddChipLast` — non-empty case.
5. `ExtensionsZoneHeaderTrailingActionIsManage` — `header_trailing_action.has_value()`, title `Manage`, callback non-null when browser supplied, none otherwise.
6. `ExtensionIconActionsBindPerExtensionRunCallback` — install a test seam (`internal::g_invoke_extension_action_for_testing`) and assert each chip's `callback` dispatches with the chip's own `extension_id`.
7. `ExtensionIconActionsBindPerExtensionContextMenuCallback` — same shape via a context-menu seam.
8. `DisabledExtensionChipIsRenderedButLeftClickIsNoOp` — left-click callback returns without calling the run-action seam when `extension->is_enabled()` is false.
9. (If §3H ships) `ExtensionIconActionsCarryPinnedFlag` — pin model integration.

**Existing test target changes:** add `//extensions/browser/test:test_support` (or whatever local target provides `extensions::TestExtensionRegistry` and friends) to the unittest GN deps. If the local overlay does not currently link extensions test support, fold the dep in alongside the Phase-3 commit that needs it.

### 6.7 `maho-chromium/browser/BUILD.gn` (`maho_location_bar_views` target)

Add to `source_set("maho_location_bar_views")` (located in `maho-chromium/browser/BUILD.gn`, not in a `location_bar/BUILD.gn` — there is no such separate file):

```gn
public_deps = [
  ...
  "//extensions/browser",
  "//extensions/common",
  "//chrome/browser/extensions",  # for ExtensionContextMenuModel
]
```

Verify against the existing `check_includes = false` setting on this target — if cycle avoidance is in play, this added dep may need to be in `deps` rather than `public_deps`.

### 6.8 Sidebar consumer (read-only)

`browser/ui/views/sidebar/maho_sidebar_state_adapter.{h,cc}` etc. consume `BuildMahoLocationBarUtilityPanelModelForBrowser(Browser*)`. Public signature stays. The sidebar inherits the icon grid because it uses the same model. This is a deliberate side-effect (sidebar gains parity with the address-bar popup). Verification is build-only; no source edits expected.

If the sidebar's existing visual layout is incompatible with the larger icon-only chips, gate the new `DisplayMode::kExtensionIconChip` rendering on a model flag (e.g. `MahoLocationBarUtilityPanelModel::use_icon_grid`) and let the sidebar opt out. Decide at implementation time after seeing the sidebar render.

---

## 7. TDD / Unit-Test Plan

The flow is test-first per phase, mirroring Phase 1 / 2. All tests run under the existing provider unittest target.

- Phase A (data-source switch + icon model) — tests 1, 2, 3, 4 above.
- Phase B (Manage link) — test 5.
- Phase C (run + context-menu callbacks) — tests 6, 7, 8.
- Phase D (live refresh) — a new test installing a fake bridge and verifying the view's observer fires `RebuildExtensionsZone(...)`. This is a view-level test; if the existing target only covers the provider, add a `MahoLocationBarUtilityPanelViewTest` fixture at this point. If a view test target does not exist at all in the overlay yet, gate D's coverage to provider-side observer-registration unit only and document the gap.
- Phase E (optional pin badge / disabled visual / keyboard nav) — only if those ship.

Hands-on smoke checks (non-gating):

- Open the popup, observe icon grid renders with real extension icons.
- Click an extension with a popup → observe popup opens.
- Right-click an extension → observe context menu with Manage / Remove / Pin entries.
- Install an extension from `chrome://extensions` while popup is open → observe popup adds the new chip live.
- Repeat in a non-initial regular profile → observe the same behaviors (this exercises the bridge per-profile fix).

---

## 8. Phased Execution Order

Each phase = one atomic commit. Each phase ends with green unit tests + canonical build pass before the next begins.

### Phase A — Data source + icon model (no behavior change yet)

1. Extend `MahoLocationBarUtilityPanelAction` with `extension_id` and `context_menu_callback`.
2. Extend `MahoUtilityPanelZone` with `header_trailing_action`.
3. Implement `BuildExtensionIconActions(Browser*)` reading from `ExtensionRegistry`.
4. Replace `BuildExtensionsZone` body to use the new builder; keep `kExtensionChip` `DisplayMode` so chips still render text-style with the new data (icons land in Phase B).
5. Update tests 1 / 2 / 3 / 4.
6. Run `python3 maho-chromium/build/scripts/build_maho.py`.

Acceptance: build green, provider unittest green, popup still shows extensions but now sourced from registry.

### Phase B — Real icon rendering + Manage link

1. Add `DisplayMode::kExtensionIconChip` to the view; render `IconImage` / `ExtensionActionIconFactory`.
2. Render `header_trailing_action` in `AddZoneHeader`.
3. Update tests 5.
4. Build green.

Acceptance: popup shows real extension icons; `Manage` link in the Extensions header opens settings.

### Phase C — Click runs the action; right-click opens the context menu

1. Wire `InvokeExtensionActionForBrowser` and `ShowExtensionContextMenuForBrowser`.
2. Update `MahoLocationBarUtilityPanelActionView` press handlers to dispatch the right callback per button.
3. Update tests 6 / 7 / 8.
4. Hands-on smoke per §7.

Acceptance: clicks invoke real extension popups; right-click shows the standard menu.

### Phase D — Live refresh subscription

1. Make the panel view a `MahoExtensionStateBridge::Observer` for the bubble lifetime.
2. Implement `RebuildExtensionsZone` and `OnInstalledExtensionsChanged`.
3. Add the view-level observer test (or document the gap if no view test target exists).
4. Hands-on smoke: install an extension while popup is open, confirm chip appears.

Acceptance: bubble updates without re-open.

### Phase E — Optional polish

1. §3H pin badge — only if reachable cheaply.
2. §3I disabled visual — usually drops in alongside Phase B with a tiny opacity tweak.
3. §3J keyboard navigation — promote from optional to must-ship if scope allows.

Acceptance: build green; anything skipped recorded as a follow-up.

---

## 9. Verification

Acceptance gates (exactly two):

1. **Unit tests green:** `cd maho && bun run test` plus the provider unittest target under the canonical build.
2. **Canonical build green:** `python3 maho-chromium/build/scripts/build_maho.py` exits 0.

Optional supporting checks (not gates):

- `lsp_diagnostics` per edited C++ file — known to be noisy in this overlay due to include-path resolution; treat warnings as informational.
- Hands-on smoke per §7. Documented as non-gating because the local environment cannot reliably sustain a Maho process for end-to-end automation (see prior `extension-state-sync-implementation-plan-2026-05-25.md` QA notes); each developer running this plan should still smoke-test before merge.

Out of scope as gates: integration tests, interactive uitests, screenshot diffs.

---

## 10. Risks & Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| `extensions::ExtensionContextMenuModel` constructor signature differs in this Chromium revision | Medium | Implementer adapts at call site; no plan-level rework. If the ctor requires a delegate that the overlay does not already provide, fall back to a minimal stub delegate or to running a smaller hand-built `ui::SimpleMenuModel` that exposes Manage / Remove / Pin only. Document the chosen path. |
| Extension popup anchoring fails after our bubble closes (no `ExtensionsToolbarContainer` in the overlay) | Medium | §5.4 fallback: anchor at `MahoLocationBarUtilityIconView`. If that also fails, defer 3C to a follow-up and ship Phase 3 with click → settings, keeping right-click context-menu. |
| `IconImage` async load races bubble close, leaks observer callbacks | Low | Each chip owns its `IconImage` / observer; chip view destruction tears it down. `base::WeakPtr` chains in the post-task callbacks. |
| Live refresh during context menu open causes use-after-free on extension pointer | Medium | Take a `scoped_refptr<const Extension>` at callback bind time so the menu's reference stays alive even if the registry mutates. Verified pattern in §6.2 Change B. |
| GN dep cycle when `//extensions/browser` is added to `maho_location_bar_views` | Medium | Phase 1 already used `check_includes = false` on this target. If a cycle surfaces, the dep moves from `public_deps` to `deps`, with the include declared via `check_includes` already suppressing direct enforcement. Verify build first. |
| Sidebar surface inherits an icon-only chip row that does not fit its layout | Medium | §6.8 escape hatch: gate `kExtensionIconChip` on a model flag and let the sidebar opt out for now. |
| `should_display_in_extension_settings()` filter excludes something the user expects to see (or vice versa) | Low | Match Chrome's puzzle-piece menu exactly; if behavior differs we follow Chrome. |
| `MahoExtensionStateBridge::FromProfile(profile)` returns null for OTR profile so live refresh doesn't subscribe | Low | Already the documented contract from the bridge plan. Popup degrades to "rebuild on next open" — acceptable for OTR. |
| Per-extension callback bag inflates the action struct memory footprint | Low | Two new fields, only set on extension chips. Toolbar/settings actions leave them empty. |
| Phase 2's 2E (per-extension callbacks routed to settings) conflicts with Phase 3's run-action callbacks | Medium | Phase 3 supersedes 2E. If Phase 2 ships first, Phase 3 replaces those bindings; if Phase 3 ships first, 2E becomes a no-op and is removed from the Phase 2 plan. Cross-reference noted at top of this file. |
| Settings WebUI extension list uses no `should_display_in_extension_settings` filter (popup gains it) | Low | Documented divergence in §5.7. Aligning the two is a follow-up, not a Phase 3 must-ship. |

---

## 11. Atomic Commit Strategy

One commit per phase, message prefix `[utility-panel]`. Commits must:

1. Compile under `python3 maho-chromium/build/scripts/build_maho.py`.
2. Pass the provider unittest target.
3. Touch only files in scope for that phase plus mechanical updates to keep the build green.
4. Avoid AI / agent attribution in messages.

Suggested messages:

- `[utility-panel] Source extension chips from ExtensionRegistry; carry per-extension id`
- `[utility-panel] Render extension icon grid via IconImage; add Manage header link`
- `[utility-panel] Wire chip click to ExtensionActionRunner and right-click to ExtensionContextMenuModel`
- `[utility-panel] Subscribe to MahoExtensionStateBridge for live refresh while popup open`
- `[utility-panel] (optional) Show pin badge / disabled visual / keyboard nav for extension grid`

---

## 12. Must-Ship vs Deferred

### Must ship in Phase 3

- Extension data sourced from `ExtensionRegistry::Get(profile)` (3A).
- Real extension icon rendering via `ExtensionActionIconFactory` / `IconImage` (3B).
- Click invokes `ExtensionActionRunner::RunAction` for the right extension (3C).
- Right-click opens `ExtensionContextMenuModel` (3D).
- `Manage` link in Extensions header opens settings extensions pane (3E).
- Live refresh via `MahoExtensionStateBridge::Observer` (3F).
- Empty-state tile + `+` chip (3G).
- Public builder signatures unchanged.
- Unit tests + canonical build green.

### Deferred (explicitly not Phase 3)

- Extension install UI in-bubble.
- Pin/unpin top-level affordance outside the context menu.
- Drag-and-drop reordering.
- Region screenshot mode (covered by Phase 2's 2G/2H envelope).
- WebUI migration of the location-bar utility panel.
- Filtering parity for the settings WebUI (`should_display_in_extension_settings`) — note in §5.7.
- Pin badge on chips (3H), disabled visual (3I), keyboard navigation (3J) if any of them slip the cost gate.

---

## 13. Definition of Done

Phase 3 is complete when **all** of the following hold simultaneously:

1. The address-bar utility panel renders extensions as a square icon grid using each extension's real icon.
2. Left-clicking a chip closes the bubble and invokes that extension's action via `ExtensionActionRunner` (popup or background).
3. Right-clicking a chip opens `ExtensionContextMenuModel` with Manage / Remove / Pin / View web permissions entries that all behave like Chrome's puzzle-piece menu.
4. The Extensions header has a right-aligned `Manage` link that opens `chrome://maho-settings#extensions`.
5. Installing or removing an extension from any source (Web Store, `chrome://extensions`, settings pane) updates the popup grid live while the bubble is open.
6. Empty registry state shows a single `No extensions` tile plus the `+` add chip.
7. Public builder signatures are byte-identical to pre-plan state.
8. Provider unittest covers tests 1–8 from §6.6 (plus 9 if §3H ships).
9. `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
10. Sidebar compiles and renders without source edits, OR §6.8's opt-out flag is wired and tested.
11. No commit message contains AI / agent attribution.
12. Smoke checks per §7 pass on at least one developer's machine before merge.

When all twelve hold, the surface matches the screenshot the user provided: a Chrome-faithful extension toolbar grid living inside the address-bar utility panel, with `chrome://extensions` retained as the developer-facing canonical management page and `chrome://maho-settings#extensions` retained as the settings-style management surface.
