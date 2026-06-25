# Zen Boost Structural Port Plan (revision 2, post-source-audit)

**Created:** 2026-05-29
**Revised:** 2026-05-29 (post-source-audit: full read of ZenBoostsEditor.mjs / ZenBoostsManager.sys.mjs / actor pair / all CSS)
**Status:** Active. Supersedes `.sisyphus/plans/zen-boost-ui-parity-recovery-plan-2026-05-29.md` and the previous draft of this file.
**Source-code reference:** Zen Browser at commit `d76fa13eddf127eaea33b624e82981086987b138` (`/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/zen-desktop/`).
**Visual reference (acceptance baseline):** Arc Browser screenshots at `maho/reference_screenshots/arc-boost.png` (187×590, normal mode) and `maho/reference_screenshots/arc-boost-code.png` (902×1164 retina = 451×582, code mode). Arc and Zen are visually equivalent because Zen is itself a port of Arc Boost.

This plan replaces "build a Boost editor that looks like Zen" with **"structurally port Zen's Boost editor source code into Maho's WebUI, with Arc Browser screenshots as the visual acceptance baseline, and document every implementation gap that arises from Maho being Chromium and Zen being Firefox."**

Audit since revision 1 read all of `ZenBoostsEditor.mjs` (1631 lines), `ZenBoostsManager.sys.mjs` (785 lines), `ZenBoostsChild.sys.mjs` (555 lines), `ZenBoostsParent.sys.mjs` (185 lines), and the five CSS files. Findings reshape the plan in several places, all reflected below.

## 1. Why this plan exists

The previous recovery plan failed because it allowed an abstract "same control type" interpretation that produced a generic single-column form with sliders. The current Maho boost editor is not structurally related to Zen's actual UI. The fix is a literal-as-possible port: take Zen's XHTML, CSS, and MJS files and translate them mechanically into Chromium WebUI, then verify against Arc reference screenshots.

## 2. What "structural port" means here, with the Mozilla→Chromium implementation gap acknowledged

A literal port is impossible. Zen is Firefox-based. The XHTML uses XUL (`<vbox>`, `<hbox>`, `<panel>`, `<menupopup>`, `<commandset>`). The MJS uses Mozilla actors (JSWindowActorChild/Parent), `Services.obs`, `nsIFontEnumerator`, `windowUtils.loadSheet`, and Mozilla DevTools modules. None of those run inside `chrome-untrusted://maho-boost-editor`.

Beyond the XUL/MJS surface translation, **Zen's color-boost rendering is a native Gecko hook**, not CSS. `nsZenBoostsBackend.cpp` plus `nsPresContext.h`/`PresShell.cpp` patches accept a packed uint32 NSColor (RGB plus contrast in the alpha byte) on `browsingContext.zenBoostsData` and apply it during paint composition. `browsingContext.fullZoom` drives `sizeOverride` (page zoom, not CSS font-size). `browsingContext.isZenBoostsInverted` drives `smartInvert` natively. Maho cannot replicate these as native Chromium hooks within this plan's scope; **Maho will keep its existing CSS-injection approach** (`maho_boost_injection_handler.cc` composing a CSS string from boost data). This produces a visually similar but not pixel-identical effect on the host page itself. Editor-side parity (the panel UI) is unaffected.

A **structural port** for this plan is the union of:

1. **DOM structural equivalence.** Every element id and class name from `zen-boost-editor.inc.xhtml` appears in the Maho-rendered DOM at the same parent-child relationship and the same sibling order. XUL elements translate mechanically (table in §4 W3).
2. **CSS rule equivalence.** Every selector and declaration in `zen-boosts.css` and `zen-advanced-color-options.css` is preserved. Translation table for Mozilla-only features in §4 W3.
3. **Behavior equivalence.** `ZenBoostsEditor.mjs` is rewritten in TypeScript with the same method names, same DOM ids it reads/writes, same color-picker drag math (including the +100° offset), same case-cycle, same size-cycle, same temp-boost lifecycle. The Zap and Picker modes from `ZenBoostsChild.sys.mjs` and overlay/selector components are ported as Chromium content-scripts plus a Mojo channel.
4. **Visual acceptance** is judged against the Arc screenshots. Editor panel UI must hit ≥ 0.85 SSIM.
5. **Implementation gaps** with Mozilla-internal renderers are documented in §7. They are not bugs of this plan; they are constraints.

## 3. Inherited contract and explicit deletions

This plan keeps from prior planning, untouched:

- Per-domain multi-boost storage in `maho/crates/maho-core/src/boost_manager.rs`.
- Typed event channel (`BoostCreated`, `BoostUpdated`, `BoostDeleted`, `BoostActiveChanged`).
- Per-tab CSS injection with reinjection on active change (`maho_boost_injection_handler.cc`).
- The `chrome-untrusted://maho-boost-editor` host. Trusted `chrome://maho-boost` stays unreachable.
- Per-Browser controller scope (`MahoBoostWindowController::GetForBrowser`).
- The launch API: every entry point goes through `ShowForActiveDomain(WebContents*)`.

This plan **overwrites** the prior G3 Mojo IDL.

This plan **explicitly deletes** the following:

- `maho_sidebar_boosts_view.cc/h` and `maho_sidebar_boosts_data.cc/h`. Zen has no sidebar boost panel; user decision β is no Maho-only sidebar surface and no Zen `ZenSiteDataPanel` URL-bar popup either.
- The location-bar utility-panel "Boost" action in `maho_location_bar_utility_panel_provider.cc`.
- The shield-bubble "Boost this site" button in `maho_shield_bubble_view.cc`.

This plan **does NOT delete** `kCapitalize` from `TextCase`. Source audit corrects revision 1: `ZenBoostsEditor.mjs::onBoostCasePressed` cycles `none → uppercase → lowercase → capitalize → none`. Four modes, not three.

## 4. Workstreams

### W1. Mojo IDL re-shape, Rust contract update

**Goal.** Match Zen's data model field-for-field per the source audit, including everything Zap and lifecycle need.

**Default values** (from `ZenBoostsManager.sys.mjs::getEmptyBoostEntry`):

```
boostName: "My Boost"
dotAngleDeg: 0
dotPos: { x: 0.76, y: 0.66 }
dotDistance: 0
secondaryDotAngleDegDelta: 55
secondaryDotPos: { x: 0.5, y: 0.81 }
brightness: 0.5
saturation: 0.5
contrast: 0.75
fontFamily: ""             // empty string = no override
enableColorBoost: false
smartInvert: false
autoTheme: false
textCaseOverride: "none"
sizeOverride: 1
zapSelectors: []
customCss: ""
changeWasMade: false
```

**Mojo IDL**, `maho-chromium/browser/ui/webui/maho_boost/maho_boost.mojom`:

```
struct Point { float x; float y; };

enum CaseMode { kNone, kUpper, kLower, kCapitalize };  // four modes, kCapitalize KEPT
enum SizeMode { k90, k100, k110, k125, k150 };          // five modes, cycle order

struct ColorBoost {
  Point dotPos;                       // 0..1 normalized, default {0.76, 0.66}
  float dotDistance;                  // 0..1 normalized, default 0
  float dotAngleDeg;                  // 0..360, default 0; +100° offset applied in editor
  Point secondaryDotPos;              // default {0.5, 0.81}
  float secondaryDotAngleDegDelta;    // default 55
  bool magicTheme;                    // == autoTheme, default false
  bool colorBoostEnabled;             // == enableColorBoost, default false
  bool smartInvert;                   // default false
  float contrast;                     // 0..1, default 0.75 (note: editor stores it as raw 0..1, panel slider is 0.05..0.9)
  float brightness;                   // 0..1, default 0.5
  float saturation;                   // 0..1, default 0.5
};

struct TypographyBoost {
  string fontFamily;                  // "" = no override
  CaseMode caseMode;                  // default kNone
  SizeMode sizeMode;                  // default k100 (==1.0 zoom)
};

struct BoostInfo {
  string id;
  string name;                        // "My Boost" by default
  string domain;
  ColorBoost color;
  TypographyBoost typography;
  array<string> zapSelectors;
  string customCss;
  bool changeWasMade;
};

enum WindowMode { kBoost, kCode };

interface PageHandler {
  GetDomain() => (string domain);
  ListBoosts() => (array<BoostInfo> boosts);
  GetBoost(string id) => (BoostInfo? boost);
  GetActiveBoost() => (BoostInfo? boost);

  // Temp-boost lifecycle (Zen pattern)
  CreateTempBoost() => (BoostInfo? boost);    // creates a fresh empty boost AND makes it active
  CommitBoost(string id) => ();                // called on close if changeWasMade
  DiscardBoost(string id) => ();               // called on close if !changeWasMade

  UpdateBoost(string id, BoostUpdate update) => (BoostInfo? boost);
  DeleteBoost(string id) => (bool success);
  SetActiveBoost(string? id) => ();
  ComposeCss(string id) => (string css);

  ShuffleBoost(string id) => (BoostInfo? boost);
  ResetBoost(string id) => (BoostInfo? boost);
  ExportBoost(string id) => (string json);
  ImportBoost(string json) => (BoostInfo? boost);
  CloseDialog() => ();
  RequestModeResize(WindowMode mode) => ();

  // Zap and Picker (W3.Z)
  EnterZapMode(string boostId) => ();
  ExitZapMode() => ();
  EnterPickerMode(string boostId) => ();
  ExitPickerMode() => ();
  AppendZapSelector(string boostId, string selector) => ();
  RemoveZapSelector(string boostId, string selector) => ();
  OpenInspector() => ();

  // System font enumeration (W3.F)
  GetSystemFonts() => (array<string> fonts);
};

interface PageObserver {
  OnBoostsChanged();
  OnActiveChanged(string? boostId);
  OnZapStateUpdate(bool isOn, bool anyZapped);
  OnZapListUpdate();
  OnPickerStateUpdate(bool isOn);
  OnPickerSelectorPicked(string selector);
  OnEditorKilled();   // another editor opened, close this one
};
```

**Rust + storage migration.** `boost_manager.rs` mirrors the schema. SQLite migration: new columns get `getEmptyBoostEntry` defaults; existing `kCapitalize` rows stay valid; size column migrates from float to enum-int (1.0→k100, etc., with rounding).

**Lifecycle semantics (new for Maho)**: opening the editor for a domain auto-creates a temp boost AND sets it active. On close, if `changeWasMade` is true the boost persists; otherwise it is deleted and the previous active boost (if any) is re-activated.

**Acceptance.** `cargo check && cargo test -p maho-core` green. Mojo IDL compiles. Bindings regenerate. Storage migration ships with rollback test.

### W2. Window controller: two modes, frameless macOS, alwaysontop, lifecycle integration

**Goal.** `MahoBoostWindowController` produces a window matching Zen's window contract.

**Concrete changes.** `maho-chromium/browser/ui/views/boost/maho_boost_window_controller.{h,cc}`:

- Initial size: 184 × 582 (boost mode), per `zen-boosts.css` `min/max-width: 184px; min/max-height: 582px`.
- Resize-to (called from WebUI via Mojo `RequestModeResize`): 452 × 582 for code, 184 × 582 for boost. Use `views::Widget::SetBounds`.
- macOS: frameless top-level. `views::Widget::InitParams::remove_standard_frame = true`. Custom drag region via `-webkit-app-region: drag` on `#zen-boost-head-wrapper` (boost mode) and `#zen-boost-code-top-bar` (code mode).
- alwaysontop: `widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow)`.
- Top-level (not parented to BrowserView).
- Per-Browser singleton via `GetForBrowser`. Already correct, keep.
- Right-aligned offset on code mode: shift left by 265 px when editor is to the right of parent (per `onCodeButtonPressed`).
- **New auto-close triggers** (per `ZenBoostsManager.sys.mjs::openBoostWindow`):
  - Close on parent-tab `TabSelect` change (subscribe to `TabStripModelObserver::OnActiveTabChanged`).
  - Close on parent-tab top-level navigation (subscribe to `WebContentsObserver::DidFinishNavigation` on the parent's active `WebContents`, filter for top-level + not same-document).
  - Close all other Boost editors when a new one opens (broadcast `OnEditorKilled` via `PageObserver` to any other live editor).

**Acceptance.** Open at 184×582 frameless on macOS. Mojo `RequestModeResize(kCode)` resizes to 452×582 with right-align offset. Window is alwaysontop, top-level. Closes on TabSelect, on navigation, on another editor opening. Visual diff vs `arc-boost.png` ≥ 0.9 SSIM at 1× DPI on chrome only.

### W3. WebUI structural port

**Goal.** Maho's editor DOM matches Zen's XHTML 1:1 except for XUL-to-HTML translations. CSS preserves Zen's `zen-boosts.css` and `zen-advanced-color-options.css` rule-for-rule. Visual result matches Arc reference screenshots.

**Source files (port scope reduced after CSS audit):**

| Zen path | Lines | Maho destination | Treatment |
|---|---|---|---|
| `src/zen/boosts/zen-boost-editor.inc.xhtml` | 168 | `maho-chromium/browser/resources/maho_boost/boost.html` | XUL→HTML translation, full DOM literal port |
| `src/zen/boosts/zen-boosts.css` | 826 | `maho-chromium/browser/resources/maho_boost/boost.css` | Literal port + Mozilla→Chromium feature translation |
| `src/zen/boosts/zen-advanced-color-options.css` | 43 | `maho-chromium/browser/resources/maho_boost/zen-advanced-color-options.css` | Literal port |
| `src/zen/common/styles/zen-theme.css` | 345 | NOT ported wholesale | Extract only the variables actually referenced by `zen-boosts.css` (~30 lines: `--zen-border-radius`, `--zen-primary-color`, `--zen-branding-bg/dark/paper`, `light-dark()` chain). Inline at top of `boost.css`. |
| `src/zen/common/styles/zen-buttons.css` | 80 | NOT ported | XUL `<button>` and `<dialog>` parts only. Boost editor does not use these. |
| `src/zen/common/styles/zen-animations.css` | 102 | NOT ported | None of these keyframes are referenced by `zen-boosts.css`. |
| `src/zen/common/styles/zen-panel-ui.css` | 58 | NOT ported | `<panel>`/`<menupopup>` mica/macOS handlers. We use HTML popovers instead. |
| `src/zen/boosts/ZenBoostsEditor.mjs` | 1631 | `maho-chromium/browser/resources/maho_boost/app.ts` | TS rewrite, line-for-line behavior |
| `src/zen/boosts/actors/ZenBoostsChild.sys.mjs` | 555 | content script (W3.Z) | TS rewrite into Chromium content script |
| `src/zen/boosts/actors/ZenBoostsParent.sys.mjs` | 185 | absorbed into `maho_boost_page_handler.cc` | C++/Mojo |
| `src/zen/boosts/ZenZapOverlayChild.sys.mjs` | (~400) | content script (W3.Z) | TS rewrite |
| `src/zen/boosts/ZenSelectorComponent.sys.mjs` | (~700) | content script (W3.Z) | TS rewrite |
| `locales/en-US/browser/browser/zen-boosts.ftl` | 59 | inline TS l10n table inside `app.ts` | English-only |

**Required DOM tree (every id/class is mandatory):**

```
#zenBoostWindow [editor="boost"|"code"] [data-platform="macos"|"linux"|"windows"]
├── #zen-boost-editor-root
│   ├── #zen-boost-head-wrapper [-webkit-app-region: drag]
│   │   ├── button#zen-boost-close.subviewbutton.mod-button.title-button
│   │   ├── #zen-boost-name (flex)
│   │   │   └── #zen-boost-name-container
│   │   │       └── p#zen-boost-name-text
│   │   └── button#zen-boost-shuffle.subviewbutton.mod-button.title-button
│   └── #zen-boost-filter-wrapper
│       ├── .zen-boost-color-picker-gradient.zen-boost-panel-disabled
│       │   ├── button#zen-boost-magic-theme
│       │   ├── div#zen-boost-color-picker-dot-primary.zen-boost-color-picker-dot
│       │   ├── div#zen-boost-color-picker-dot-secondary.zen-boost-color-picker-dot
│       │   ├── div.zen-boost-color-picker-circle
│       │   └── svg.zen-boost-color-picker-arc-svg (created at runtime)
│       ├── #zen-boost-toolbar-wrapper-colors
│       │   ├── button#zen-boost-invert.subviewbutton.mod-button.small
│       │   ├── button#zen-boost-controls.subviewbutton.mod-button.small
│       │   └── button#zen-boost-disable.subviewbutton.mod-button.small
│       ├── #zen-boost-font-wrapper
│       │   ├── #zen-boost-font-grid (5×3 grid, 15 buttons of "Aa")
│       │   ├── .visible-separator
│       │   └── #zen-boost-font-toolbar
│       │       └── select#zen-boost-font-select.mod-button
│       ├── #zen-boost-toolbar-wrapper
│       │   ├── button#zen-boost-size.subviewbutton.mod-button.big-button.med
│       │   │   ├── p#zen-boost-size-text "Size"
│       │   │   └── p#zen-boost-size-value (e.g. "110%")
│       │   └── button#zen-boost-case.subviewbutton.mod-button.big-button.toggleable-button.med [case-mode="none|uppercase|lowercase|capitalize"] [mode="orange|orange-red|red|none"]
│       │       └── p#zen-boost-case-text "Case"
│       ├── button#zen-boost-zap.subviewbutton.mod-button.big-button.toggleable-button [enabled="true|false"] [hideicon="true|false"]
│       │   ├── p#zen-boost-zap-text "Zap"
│       │   └── p#zen-boost-zap-value (count when zapped)
│       └── button#zen-boost-code.subviewbutton.mod-button.big-button
│           └── p#zen-boost-code-text "Code"
├── #zen-boost-code-editor-root [display: none initially]
│   ├── #zen-boost-code-top-bar [-webkit-app-region: drag]
│   │   └── button#zen-boost-back.subviewbutton.mod-button.big-button
│   │       └── p#zen-boost-back-text "Back"
│   ├── #zen-boost-code-editor (CodeMirror mount; see W3.C)
│   └── #zen-boost-code-bottom-bar
│       ├── button#zen-boost-css-picker.subviewbutton.mod-button.big-button.toggleable-button [enabled="true|false"]
│       └── button#zen-boost-css-inspector.subviewbutton.mod-button.big-button
└── #mainPopupSet
    ├── #zen-boost-advanced-color-options-panel (custom popover; contrast/brightness/saturation sliders)
    │   ├── div > p[data-l10n-id="zen-bootst-color-contrast"] + input#zen-boost-color-contrast[type=range, min=0.05, max=0.9, value=0.75, step=0.01]
    │   ├── div > p[data-l10n-id="zen-bootst-color-brightness"] + input#zen-boost-color-brightness[type=range, min=0, max=1, value=0.5, step=0.01]
    │   └── div > p[data-l10n-id="zen-bootst-color-original-saturation"] + input#zen-boost-color-saturation[type=range, min=0, max=1, value=0.5, step=0.01]
    └── #zenBoostContextMenu (custom context menu)
        ├── menuitem#zen-boost-edit-rename "Rename Boost"
        ├── menuitem#zen-boost-edit-shuffle "Shuffle Vibes"
        ├── menuitem#zen-boost-edit-reset "Reset All Edits"
        ├── separator
        ├── menuitem#zen-boost-load "Import Boost"
        ├── menuitem#zen-boost-save "Export Boost"
        ├── separator
        └── menuitem#zen-boost-edit-delete "Delete Boost"
```

**XUL → HTML translation table:**

| XUL | HTML |
|---|---|
| `<vbox flex="1">` | `<div style="display:flex;flex-direction:column;flex:1">` |
| `<hbox flex="1">` | `<div style="display:flex;flex-direction:row;flex:1">` |
| `<panel type="arrow" popupalign="topmiddle">` | `<div role="dialog" data-popup-align="topmiddle" style="position:fixed">` opened by JS |
| `<menupopup>` + `<menuitem command="…">` | custom popover with `<button>` items, JS dispatches the named command |
| `<commandset>` + `<command id="cmd_…" oncommand="…">` | JS event handlers bound by id |

**CSS Mozilla → Chromium translation table:**

| Mozilla feature | Translation |
|---|---|
| `-moz-platform: macos` | `:root[data-platform="macos"]` (set by app.ts at load from `navigator.platform`) |
| `-moz-platform: linux` | `:root[data-platform="linux"]` |
| `-moz-platform: windows` | `:root[data-platform="windows"]` |
| `-moz-window-dragging: drag` | `-webkit-app-region: drag` |
| `-moz-window-dragging: no-drag` | `-webkit-app-region: no-drag` |
| `-moz-windows-mica` | dropped (macOS focus). Documented gap on Windows. |
| `(-prefers-color-scheme: dark)` | `(prefers-color-scheme: dark)` |
| `::-moz-range-thumb` | `::-webkit-slider-thumb` (with `appearance:none` on the input) |
| `chrome://global/skin/icons/arrow-down.svg` | inline SVG asset bundled at `zen-port/icons/arrow-down.svg` |
| `light-dark(a, b)` | keep verbatim (Chromium 123+); fallback `@media (prefers-color-scheme: dark)` if build is older |
| `color-mix(in srgb, …)` | keep verbatim (Chromium 111+) |
| `@property --foo` | keep verbatim (Chromium 85+) |

No other CSS edits are allowed.

**TS rewrite of `ZenBoostsEditor.mjs`. Concrete behaviors to preserve verbatim:**

- `commonFonts` array: 15 names exactly as in source (`Arial, Times New Roman, Courier New, Georgia, Comic Sans MS, Verdana, Trebuchet MS, Impact, Palatino Linotype, Tahoma, Helvetica, Garamond, Century Gothic, Arial Black, Papyrus`).
- `initFonts()`: 15 buttons in `#zen-boost-font-grid`, dropdown options in `#zen-boost-font-select`, default option `value=""` label `Default`. `fetchFontList()` is implemented via Mojo `GetSystemFonts()` (see W3.F), returning the full system font list. Grid uses `commonFonts` mapped against the enumerated list (per Zen `initFonts()` line 300-315).
- `onCodeButtonPressed()` / `onCodeBackButtonPressed()`: Mojo `RequestModeResize`, then display swap (`#zen-boost-editor-root` ↔ `#zen-boost-code-editor-root`).
- 2D color picker drag math (verbatim from `setDotPos` lines 712-815):
  ```
  padding = 50
  centerX = rect.left + rect.width / 2
  centerY = rect.top + rect.height / 2
  radius = (rect.width - padding) / 2
  distance = clamp(hypot(x - centerX, y - centerY), 0, radius)
  angle_rad = atan2(y - centerY, x - centerX)
  pixelX = centerX + cos(angle_rad) * distance
  pixelY = centerY + sin(angle_rad) * distance
  dotAngleDeg = ((angle_rad * 180/π + 100) % 360 + 360) % 360
  dotDistance = distance / radius
  dotPos.x = (pixelX - rect.left) / rect.width
  dotPos.y = (pixelY - rect.top) / rect.height
  ```
- Dot CSS variable (verbatim from `updateDot`):
  ```
  primary --zen-theme-picker-dot-color: hsl({dotAngleDeg}deg, {dotDistance*100}%, 55%)
  secondary --zen-theme-picker-dot-color: hsl({dotAngleDeg + secondaryDotAngleDegDelta}deg, {dotDistance*100}%, 20%)
  ```
- `magic-theme` button: toggles `autoTheme`. **Maho-specific deviation** (per §7): `autoTheme` in Zen reads workspace gradient. Maho has no Zen Workspaces concept. v1 behavior: when `autoTheme` is on, use a Maho theme accent color (read from `MahoCore::GetCurrentSpaceAccent()` if available, else fall back to a fixed neutral). Document as deviation in acceptance record.
- `case` button cycles **four** modes `none → uppercase → lowercase → capitalize → none`. Sets attribute `case-mode="..."` on the button and `mode="orange|orange-red|red|none"` for gradient styling.
- `size` button cycles **five** modes `1 → 1.1 → 1.25 → 1.5 → 0.9 → 1`. Sets attribute `mode="orange|orange-red|red|blue|none"`. Value text shows `Math.round(size*100)+"%"`.
- `invert / controls / disable` toolbar:
  - `#zen-boost-invert` → toggles `smartInvert`.
  - `#zen-boost-controls` → opens `#zen-boost-advanced-color-options-panel` anchored to button.
  - `#zen-boost-disable` → toggles `enableColorBoost` (note: the button is named "disable" because clicking it disables color boost; `enableColorBoost=false` adds class `zen-boost-button-active-transparent`).
- Context menu on `#zen-boost-name-container` click: 6 items (Rename / Shuffle / Reset / -- / Import / Export / -- / Delete). When `changeWasMade==false`, items Rename/Reset/Delete are disabled.
- Editor open lifecycle (verbatim from `loadBoost` + `ZenBoostsManager.openBoostWindow`): on open, `loadActiveBoostFromStore(domain)` returns the active boost or auto-creates an empty one; `makeBoostActiveForDomain(domain, boost.id)` is called; the editor mounts.
- Editor close lifecycle (verbatim from `handleClose`): if `currentBoostData` non-null AND `changeWasMade==true`, save (Mojo `CommitBoost`); else if non-null AND `!changeWasMade`, delete the boost (Mojo `DiscardBoost`).
- Editor singleton: on open, broadcast `OnEditorKilled` to any other live editor via `PageObserver`.
- Keyboard: `Escape` and `Cmd/Ctrl+W` close the window.
- `Services.obs` topics translated to `PageObserver` callbacks (table below).

| Zen `Services.obs` topic | Maho `PageObserver` method |
|---|---|
| `zap-state-update` | `OnZapStateUpdate(isOn, anyZapped)` |
| `zap-list-update` | `OnZapListUpdate()` |
| `selector-picker-state-update` | `OnPickerStateUpdate(isOn)` |
| `selector-picker-picked` | `OnPickerSelectorPicked(selector)` |
| `zen-boosts-kill-editor` | `OnEditorKilled()` |
| `zen-boosts-active-change` | implicit close on TabSelect (W2 controller handles) |

- l10n: `LOCALE_EN_US` map keyed by `data-l10n-id`; on DOMContentLoaded walk `[data-l10n-id]` and apply textContent / `.tooltiptext` → HTML `title` attribute.

#### W3.C. Code editor: CodeMirror

User decision is "implement everything from Zen baseline." Zen uses Mozilla DevTools Source Editor which wraps CodeMirror with `mode: css, lineNumbers: true`. Maho ships CodeMirror in WebUI to match.

**Concrete approach.** Vendor CodeMirror 6 via npm (`@codemirror/state`, `@codemirror/view`, `@codemirror/lang-css`, `@codemirror/theme-one-dark` or default). Bundle into `maho_boost` resources. Mount in `#zen-boost-code-editor`. Wire `change` events to `state.draft.customCss` and `pageHandler.updateBoost(...)`. CSS picker inserts at cursor via CodeMirror dispatch.

**Note.** CodeMirror is roughly 200KB minified. Acceptable for a per-feature WebUI bundle.

#### W3.Z. Zap and Picker full implementation

**Goal.** Click-to-hide-element-on-page (Zap) and click-to-pick-element-as-CSS-selector (Picker) work identically to Zen.

**Architecture.**

- A new **content script** in TypeScript, packaged into the Chromium binary, injected into the active tab in an isolated world by `MahoBoostInjectionHandler` when zap or picker mode is on. Source-of-truth: `ZenBoostsChild.sys.mjs` (555 lines), `ZenZapOverlayChild.sys.mjs`, `ZenSelectorComponent.sys.mjs`.
- The content script:
  - Captures **23 events** at document level in capture phase (per `ZenBoostsChild.OVERLAY_EVENTS` + `PREVENTABLE_EVENTS`): `click pointerdown pointermove pointerup scroll resize mousemove mousedown mouseup mouseenter mouseover mouseout mouseleave touchstart touchmove touchend dblclick auxclick keypress contextmenu pointerenter pointerover pointerout pointerleave`. Of these, the 17 `PREVENTABLE_EVENTS` are `preventDefault()`-ed before reaching content.
  - In Zap mode: hover overlays the candidate element with a highlight. Click computes a stable CSS selector via `ZenSelectorComponent` heuristic (port verbatim) and calls back to chrome. Hover-over-already-zapped element: `tempShowZappedElement` reveals it briefly so the user can un-zap.
  - In Picker mode: same overlay UX, but on click sends the selector back to the editor (which inserts at cursor in CodeMirror) without hiding the element.
  - On Esc: exits the active mode.
- A new **Mojo channel** between the editor page (`chrome-untrusted://`) and the active tab. Implementation: extend `MahoBoostInjectionHandler` with a bidirectional message router. Editor sends `EnterZapMode`/`EnterPickerMode`/etc. via `PageHandler`; handler broadcasts to the active `WebContents` via a per-frame Mojo interface. Content script returns `OnZapStateUpdate`/`OnZapListUpdate`/`OnPickerSelectorPicked` to the editor.
- Inspector button (`zen-boost-css-inspector`): opens Chromium DevTools inspector for the active tab. Use `DevToolsAgentHost::OpenURLForFrame` or the inspector toggle helper from `chrome/browser/devtools/devtools_window.h`.

**Concrete files.**

- New: `maho-chromium/browser/resources/maho_boost_content_script/{zap_overlay.ts, selector_component.ts, content_script.ts, content_script.css, BUILD.gn}`.
- Modified: `maho-chromium/browser/net/maho_boost_injection_handler.{h,cc}` — bidirectional message router with the editor and the active WebContents.
- Modified: `maho-chromium/browser/ui/webui/maho_boost/maho_boost_page_handler.{h,cc}` — new methods, observer wiring.
- Modified: `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.cc` — content script registration with the active-tab injector.
- Modified: `maho/crates/maho-core/src/boost_manager.rs` — `append_zap_selector`/`remove_zap_selector` per-domain operations, emits `BoostUpdated`.

**Acceptance.** With editor open and zap mode on, clicking a visible element on host page hides it via composed CSS reinjection; selector added to active boost's `zapSelectors`; helper bar shows count; Escape exits. Picker mode pastes selector into CodeMirror at cursor. Inspector button opens Chromium DevTools. Cross-tab consistency: zap selector update reinjects on every live `WebContents` on the domain. Selector heuristic matches Zen's `ZenSelectorComponent` strategy.

#### W3.F. System font enumeration

**Goal.** `#zen-boost-font-select` dropdown lists all system-installed fonts, matching Zen's `nsIFontEnumerator.EnumerateFonts(null, null)` output.

**Concrete approach.** Chromium already enumerates system fonts for `chrome://settings` font picker via `content::GetFontListAsync` (`content/browser/font_list_async.cc`). Reuse that pipeline:

- `maho_boost_page_handler.cc::GetSystemFonts`: call `content::GetFontListAsync` and return the resulting `base::Value::List` flattened to `array<string>` (each entry's font family name).
- `app.ts::fetchFontList()`: `await pageHandler.getSystemFonts()` → `string[]`.
- `initFonts()` then runs Zen's verbatim logic: 15 grid buttons map `commonFonts[i]` against the enumeration; dropdown is populated from the full list with the leading `Default` entry and `<hr>` separator per `ZenBoostsEditor.mjs` lines 318-322.

**Acceptance.** `#zen-boost-font-select` contains the `Default` option, the separator, and all system fonts in alphabetical order (Chromium's pipeline already returns sorted). Selecting a system-only font (one not in `commonFonts`) sets `has-selection="true"` on the select per Zen's CSS attribute hook.

### W4 (deleted)

Per user decision β. No sidebar boost panel, no Zen `ZenSiteDataPanel` URL-bar popup.

### W5. Surface deletion and launch path prune

**Goal.** Remove every Maho-only surface that Zen does not have.

**Wholesale-deleted files.**

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_view.{h,cc}`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_boosts_data.{h,cc}`
- All sidebar BUILD.gn entries that reference these files.
- The parent container code that mounts `MahoSidebarBoostsView`.

**Code strips (file kept, code removed).**

- `maho_location_bar_utility_panel_provider.cc`: remove `BuildBoostAction`, `ShowBoostForBrowser`, `kBoostSymbolName`, the `BuildBoostAction(browser)` push in `BuildToolbarZone`.
- `maho_shield_bubble_view.{h,cc}`: remove `boost_button_` and `OnBoostPressed`.
- `maho_page_info_ui.{h,cc}`: keep `ShowBoostEditor(Browser*)` helper (used by command palette); remove only the now-orphaned shields/location-bar callers.

**Kept entry points.** Command palette `open_boost_editor`, shortcut `Cmd+Option+B`. Both already route through `MahoBoostWindowController::GetForBrowser`.

**Acceptance.** Tree-wide grep for `BuildBoostAction|MahoSidebarBoostsView|maho_sidebar_boosts_data|"Boost this site"` returns no live code references. Command palette and shortcut still open the editor.

### W6 (deleted)

There are no deferred items in v1. CodeMirror (W3.C), Zap and Picker (W3.Z), system font enumeration (W3.F), and Inspector (W3.Z) are all in scope.

### W7. Build wiring and resource registration

**Goal.** All ported files are picked up by the Chromium GN build and the WebUI host.

- `maho-chromium/browser/resources/maho_boost/BUILD.gn`: list every new CSS file (`boost.css`, `zen-advanced-color-options.css`), every new icon under `zen-port/icons/`, vendored CodeMirror chunks.
- `maho-chromium/browser/resources/maho_boost_content_script/BUILD.gn`: new target for content script and CSS.
- `maho-chromium/browser/ui/webui/maho_boost/maho_boost_ui.cc`: register stylesheets, CodeMirror, content script with the untrusted host and the active-tab injector.
- `maho/crates/maho-core/Cargo.toml`: no new deps expected; storage migration uses existing sqlite.

### W8. Verification harness

**Goal.** Make acceptance machine-checkable wherever possible, human-reviewed where not.

**Three layers (option C):**

1. **DOM equivalence test.** Browser test or appium runner loads the editor and asserts every Zen-mandated id and class from §4 W3 is present at the correct parent. Implementation: hardcoded `(id, expected_parent_id, expected_classes)` tuples extracted from the XHTML.
2. **CSS rule audit.** Node script `maho/scripts/zen-boost-css-audit.mjs` parses Zen's two ported CSS files (`zen-boosts.css`, `zen-advanced-color-options.css`) at the pinned commit and the corresponding Maho ported files, and asserts each rule is present (modulo translation table). Output: JSON delta. Acceptance: empty delta or only translation-table entries.
3. **Screenshot diff against Arc references.** Captures at exact viewport sizes:
   - boost mode: 184 × 582 → diff vs `arc-boost.png` (cropped to inner panel, ±5px tolerance for chrome variance).
   - code mode: 452 × 582 → diff vs `arc-boost-code.png` (downsampled from retina).
   - SSIM threshold ≥ **0.85**. Threshold not 0.95 because: (a) macOS frameless titlebar will render slightly differently from Arc, (b) ported font rendering may differ at sub-pixel level, (c) Arc uses some custom glyphs we do not have, (d) we use CSS-based color rendering vs. Zen/Arc's native renderer (gap in §7). Below 0.85 = blocker. 0.85–0.95 = human review required.
4. **Behavioral round-trip:** color picker drag persists; magic-theme toggle persists; case cycle persists 4 modes; size cycle persists 5 modes; zap selector adds; picker selector inserts in CodeMirror; reload preserves all.

## 5. Strict acceptance criteria

The plan is **done** when **all** the following hold:

1. **Mojo IDL** matches W1 schema. `cargo check -p maho-types -p maho-core && cargo test -p maho-core` green. Storage migration ships with rollback test.
2. **Window** opens at 184×582, resizes to 452×582 on code mode with right-align offset, frameless on macOS, alwaysontop, top-level. Closes on parent TabSelect, on parent navigation, on another editor opening.
3. **DOM equivalence test** (W8.1) passes with no missing ids/classes.
4. **CSS rule audit** (W8.2) returns empty delta or delta-from-translation-table only.
5. **Screenshot diff** (W8.3) ≥ 0.85 SSIM against `arc-boost.png` (boost mode) and `arc-boost-code.png` (code mode).
6. **Behavior parity:** color picker drag updates `dotPos`/`dotDistance`/`dotAngleDeg` with +100° offset; magic-theme toggles (autoTheme); font grid 15 buttons + dropdown; case cycles 4 modes; size cycles 5 modes; code mode resizes window; advanced-color-options popover anchored to controls button with contrast/brightness/saturation sliders at the exact ranges (0.05-0.9, 0-1, 0-1); context menu items Rename/Shuffle/Reset/Import/Export/Delete behave per Zen; close button closes; Escape and Cmd/Ctrl+W close.
7. **Lifecycle parity:** opening editor auto-creates a temp boost and makes it active; closing without changes deletes the temp boost; closing with `changeWasMade==true` persists; previous active boost re-activates after temp discard.
8. **Zap parity (W3.Z):** zap mode toggles content-script overlay; clicks add stable CSS selectors via `ZenSelectorComponent`-port heuristic; helper bar shows count; un-zap by hover+click on already-zapped; Escape exits; reinjection updates on every live `WebContents` on the domain. Picker mode pastes CSS selectors into CodeMirror at cursor. Inspector button opens Chromium DevTools.
9. **Code editor (W3.C):** CodeMirror 6 mounted, `mode: css`, line numbers visible, edits round-trip via `customCss`.
10. **Surface deletions (W5):** sidebar boost view files gone; location-bar Boost action gone; shield-bubble Boost button gone. Tree-wide grep clean.
11. **Build green:** `python3 maho-chromium/build/scripts/build_maho.py` exits 0. `lsp_diagnostics` clean on every changed file.
12. **Known gaps recorded.** §7 implementation-gap list reproduced in acceptance record. (No W6 follow-ups; v1 scope is complete.)
13. **Commit references.** Source-code reference Zen `d76fa13eddf127eaea33b624e82981086987b138`. Visual reference `maho/reference_screenshots/arc-boost.png` and `arc-boost-code.png`. Both referenced in acceptance test artifacts.

## 6. Execution sequence

**Step 0. Asset extraction and reference fixing.**
- Copy all Zen source files listed in §4 W3 into `maho-chromium/browser/resources/maho_boost/zen-port/` as a verbatim read-only snapshot (kept for diffing during implementation, not used at runtime).
- Confirm `maho/reference_screenshots/arc-boost.png` and `arc-boost-code.png` are committed.

**Step 1. W1: Mojo IDL + Rust + storage migration.** Schema reshape, migration, rollback test.

**Step 2. W2: Window controller.** Two-mode resize, frameless macOS, alwaysontop, TabSelect/navigation/kill-editor close hooks.

**Step 3. W3: WebUI port.**
- 3a. Inline the ~30 lines of `zen-theme.css` variables into the top of `boost.css`. Port `zen-boosts.css` and `zen-advanced-color-options.css` literal with translation table.
- 3b. Replace `boost.html` with the full §4 W3 DOM.
- 3c. Rewrite `app.ts` against the new DOM with all behavior listed in W3.
- 3d. Add `LOCALE_EN_US` table from `zen-boosts.ftl`.

**Step 4. W3.C: CodeMirror.** Vendor, bundle, mount in `#zen-boost-code-editor`, wire to `customCss`.

**Step 5. W3.F: System font enumeration.** Mojo `GetSystemFonts` + page handler + `app.ts` wiring.

**Step 6. W3.Z: Zap, Picker, Inspector.** Content script, Mojo channel, editor wiring. Largest single sub-step.

**Step 7. W5: Surface deletion.** One commit per concern.

**Step 8. W7: Build wiring.** GN files, resource registration.

**Step 9. W8: Verification harness.** DOM test, CSS audit, screenshot diff against Arc.

**Step 10. Acceptance pass.** Run all gates. Fix until green.

**Step 11. Record known gaps from §7 in the acceptance record.**

## 7. Implementation gaps (Mozilla-internal hooks Maho cannot replicate as native)

These are documented constraints, not bugs.

1. **Color boost rendering.** Zen sets `browsingContext.zenBoostsData` (packed uint32 NSColor with contrast in alpha) and Gecko's native paint pipeline applies the filter. Maho continues to use CSS injection (composed `filter: hue-rotate(...) saturate(...) brightness(...) contrast(...)` plus `mix-blend-mode` on a fixed overlay where applicable). Visual results differ subtly. Acceptance does not require pixel-exact host-page rendering match against Zen.
2. **Smart invert.** Zen sets `browsingContext.isZenBoostsInverted` → native invert filter. Maho uses CSS `filter: invert()` on body or html with image/video exemptions per existing `maho_boost_injection_handler` heuristics. Difference in handling of fixed-position elements and SVG documents documented as known.
3. **Size override.** Zen sets `browsingContext.fullZoom = sizeOverride` (page zoom). Maho uses `WebContents::SetZoomLevel` via a new path-handler hook in `maho_boost_injection_handler.cc`. Equivalent semantics.
4. **AutoTheme / magic-theme.** Zen reads `gZenWorkspaces.getActiveWorkspace().theme.gradientColors`. Maho has no equivalent workspace gradient. v1 substitution: when `autoTheme` is on, Maho uses a Maho-side accent (read from `MahoCore::GetCurrentSpaceAccent()` if available, else fall back to a fixed neutral). Documented in acceptance record.
5. **`-moz-windows-mica`.** Dropped on Windows. Documented.
6. **macanimationtype="document".** Cosmetic, dropped. Documented.
7. **Inspector deep integration.** Zen's inspector shows the host page in DevTools with the picker pre-armed. Maho's `OpenInspector` opens DevTools but does not pre-arm the picker; that requires DevTools protocol scripting we defer. Documented.
8. **Storage layout.** Zen stores `zen-boosts.jsonlz4` + per-boost `<id>.css`. Maho keeps SQLite blob with full data. Functionally equivalent. Documented.

## 8. Risks (separate from §7 gaps)

- **Frameless macOS title bar interaction with traffic lights.** May need NSWindow tweak. If HIG breaks, fall back to thin standard frame and document.
- **CodeMirror bundle size** (~200KB). Acceptable for boost editor, would not be acceptable for every WebUI surface.
- **Selector heuristic robustness.** `ZenSelectorComponent` heuristic ported verbatim. Real-world brittleness on Tailwind/CSS-in-JS sites is acceptable for v1.
- **Storage migration safety.** Migration ships with rollback test; data loss risk minimal but non-zero on unanticipated production data shapes.
- **`light-dark()` and `@property` browser support.** Chromium 123+ for `light-dark()`, 85+ for `@property`. Verify our build version supports both at first compile.
- **License compliance.** All ported MPL 2.0 files keep MPL header verbatim. New TS files carry credit comment "structurally derived from Zen `<filename>` at d76fa13, MPL 2.0."

---

This plan is the **single source of truth** for Zen Boost UI parity work. The previous recovery plan and earlier drafts of this filename are **superseded** for everything in W1–W8. Subsequent work that conflicts with this plan must amend it explicitly.
