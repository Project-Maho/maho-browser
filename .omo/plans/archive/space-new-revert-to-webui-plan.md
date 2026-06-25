# Space New Revert to WebUI — Proper Modification Plan (GT-Aligned)

> **⚠️ SUPERSEDED (2026-05-22)**: This plan proposed full architectural revert (Space New form back to WebUI). The current architecture keeps the Space New form **native** in the sidebar and uses WebUI **only for the theme picker**. See `.sisyphus/plans/theme-picker-architecture-decision.md`. Element inventory below (E1-E11b, KP-* list) remains the authoritative GT element specification.

**Status**: READY FOR EXECUTION
**Created**: 2026-05-20
**GT Spec**: `.sisyphus/plans/assets/theme-parity-image1.png` (binary present, sourced from `maho/reference_screenshots/create-theme.png`; textual enumeration in §2 also binding)
**Branch**: (to be created from `main`)
**Scope**: Full architectural revert — Space New owned by `chrome://maho-space-create` WebUI; remove native inline sidebar surface and shared-config-create-mode split entirely. WebUI output must match GT Image1 element-for-element.

---

## 1. Executive Decision

"최소 수정 하지 말고 제대로 된 수정이 필요함." — This is a full architectural revert, cleanup, and GT-alignment pass.

- **Space New** → owned end-to-end by the existing `chrome://maho-space-create` WebUI, served inside a `views::DialogDelegate` wrapper (`MahoSpaceCreateDialog`). The WebUI must match **Image1 GT** element-for-element — not the old WebUI as-shipped.
- **Space Edit** → stays on `chrome://maho-space-config` (edit-mode only after this cutover).
- **Native form** (`MahoSidebarCreateSpaceView`) → deleted entirely after its required behaviors are ported to the WebUI.
- **`is_create_mode_`** machinery in `maho_space_config` → removed entirely (handler, UI, store, app, dialog, mojom `Page::ThemeChosen`, kCreate state-machine, `OpenForCreate`, `InvokeCreateModeThemeChosenCallback`, `kDialogHeightCreate`).
- **FFI sequence** in `MahoSpaceCreatePageHandler::CreateSpace` → reconciled: `RegisterSpace → SwitchToSpace → ResetColorProviderCache`; drop standalone `maho_core_activate_space` and redundant `UpdateFromCore`.
- **Dialog close path** → `MahoSpaceCreatePageHandler::CloseDialog()` changed from `web_contents_->ClosePage()` to `MahoSpaceCreateDialog::CloseActiveDialog()`.
- **GT Image1** is the binding visual spec. Where old `chrome://maho-space-create` WebUI differs from GT, GT wins. Old WebUI is the *starting point* for code changes, not the parity anchor.
- **Verification** → `lsp_diagnostics` alone is NOT sufficient; manual QA, interactive tests, and a Visual Diff Gate against Image1 are mandatory gates.

---

## 2. GT Spec — Binding Image1 Element Enumeration

**Source**: `.sisyphus/plans/assets/theme-parity-image1.png` (binary present) + textual enumeration below (binding regardless of binary availability).

Two-panel dialog, dark theme.

### LEFT PANEL (vertical stack, top→bottom)

| ID | Element | Description |
|----|---------|-------------|
| E1 | Header illustration | Laptop + folder stack icon, top-left of left panel |
| E2 | Title text | Literal string `Create a Space`, large, bold/semibold weight |
| E3 | Subtitle text | Literal string `Separate your tabs for life, work, projects, and more.`, muted color, smaller than title |
| E4 | Name input row | Single horizontal flex line containing E4a + E4b |
| E4a | Dashed `+` icon | Rounded square with dashed border, `+` glyph centered, muted/grey stroke |
| E4b | Text input | Placeholder `Space name...` (literal, three-dot ellipsis). Caret visible at left of placeholder when focused |
| E5 | Profile row | Single horizontal flex line containing E5a + E5b + E5c + E5d |
| E5a | Face glyph icon | Smiley/face glyph icon (left) |
| E5b | Label text | `Profile` |
| E5c | Pill button | Labeled `Default` with a down-chevron `▾` |
| E5d | Info icon | `ⓘ` to the right |
| E6 | Theme button | Card-style, full row |
| E6a | Brush icon | Brush/paintbrush icon (left) |
| E6b | Label text | `Choose a Theme` |

### RIGHT PANEL (vertical stack, top→bottom)

| ID | Element | Description |
|----|---------|-------------|
| E7 | Mode chip strip | Top center of right panel, horizontal row of three chips |
| E7a | Sparkle chip | `✨`, currently active (square halo / highlighted background) |
| E7b | Sun chip | `☀`, idle |
| E7c | Crescent moon chip | `🌙`, idle |
| E8 | Preview canvas | Large rectangle, fills middle of right panel |
| E8a | Dot-pattern background | Grid of small dots, low contrast |
| E8b | Centered text | Literal `Tap to pick a color for this space` |
| E9 | Brightness controls | Horizontal, below preview canvas |
| E9a | Minus button | `−` (left) |
| E9b | Plus button | `+` (right) |
| E9c | Slider | Hidden in initial state. No visible range slider until a color is picked / brightness is adjusted. Element in DOM with `opacity:0` or `aria-hidden` until interaction |
| E10 | Color swatch row | Horizontal, below brightness |
| E10a | Left chevron | `‹` |
| E10b | Nine swatches | In order: white/cream, pink, mauve/lavender, red, orange, yellow, green, blue, navy/dark blue |
| E10c | Right chevron | `›` |
| E11 | Bottom decorations | Horizontal row at bottom of right panel |
| E11a | Wave decoration | Two overlapping sine-wave SVG paths, low-saturation, decorative only |
| E11b | Dial | Circular dotted dial, decorative initial state |

### Color/Spacing Notes

- Dark navy background for both panels.
- Left panel slightly deeper navy shell vs right panel.
- Preview canvas has subtle border/inset glow effect.
- All text light-on-dark.

---

## 3. Phase 0 — Image1 Placement Confirmation

**Goal**: Confirm that the GT binary is at the expected path. This is a single verification step.

### Steps

1. Verify that `.sisyphus/plans/assets/theme-parity-image1.png` exists on disk.
   - The binary IS present at this path (sourced from `maho/reference_screenshots/create-theme.png`, 703KB, verified element-for-element against Section 2). Phase 9 Visual Diff Gate is eligible to run.
   - The textual GT spec in Section 2 remains binding for all implementation work in Phases 1–8.

### Gate

- Acknowledgment of Image1 status (present or pending).
- No code changes. No baseline screenshots captured (GT replaces baseline-as-parity-anchor).

---

## 4. Phase 1 — SetThemePreview Mojo Path (KP-1)

**Goal**: Bridge the gap between native live theme preview override and the WebUI. The native view calls `MahoSpaceThemeState::SetPreviewOverride(preview_json)` + `ColorProviderManager::ResetColorProviderCache()` on every theme picker change (around `maho_sidebar_create_space_view.cc:1613-1617` and `:1471-1503`). The WebUI today only computes in-page CSS preview and has no Mojo path to set a server-side preview override.

### 4.1 Mojom

**File**: `browser/ui/webui/maho_space_create/maho_space_create.mojom`

Add to `interface PageHandler` (after `CloseDialog();`):

```mojom
// Live theme preview — applies (or clears) a transient preview override
// on the browser chrome without persisting to Rust core.
// Pass null to clear the override and revert to the persisted theme.
SetThemePreview(string? theme_json) => (bool success);
```

### 4.2 Page Handler Header

**File**: `browser/ui/webui/maho_space_create/maho_space_create_page_handler.h`

Add private override declaration:

```cpp
void SetThemePreview(const std::optional<std::string>& theme_json,
                     SetThemePreviewCallback callback) override;
```

### 4.3 Page Handler Implementation

**File**: `browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

Add `SetThemePreview` method:

```cpp
void MahoSpaceCreatePageHandler::SetThemePreview(
    const std::optional<std::string>& theme_json,
    SetThemePreviewCallback callback) {
  if (theme_json.has_value()) {
    const bool ok = MahoSpaceThemeState::SetPreviewOverride(theme_json.value());
    if (ok) {
      ui::ColorProviderManager::Get().ResetColorProviderCache();
    }
    std::move(callback).Run(ok);
  } else {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    ui::ColorProviderManager::Get().ResetColorProviderCache();
    std::move(callback).Run(true);
  }
}
```

Change destructor from `= default` to:

```cpp
MahoSpaceCreatePageHandler::~MahoSpaceCreatePageHandler() {
  if (MahoSpaceThemeState::HasPreviewOverride()) {
    MahoSpaceThemeState::ClearPreviewOverride();
    MahoSpaceThemeState::UpdateFromCore();
    ui::ColorProviderManager::Get().ResetColorProviderCache();
  }
}
```

(Mirrors `MahoSpaceConfigPageHandler` destructor pattern at `maho_space_config_page_handler.cc:296-301`.)

### 4.4 WebUI Store

**File**: `browser/resources/maho_space_create/react/store.ts`

Add method:

```typescript
private sendThemePreview(): void {
  const json = this.buildThemeJson();
  if (json) {
    void this.pageHandler.setThemePreview(json);
  }
}
```

Call `this.sendThemePreview()` at the end of: `selectColor()`, `setBrightness()`, `setGrain()`, `setMode()`.

In `closeDialog()`, add before the Mojo close call:

```typescript
void this.pageHandler.setThemePreview(null);
```

### 4.5 Auto-generated Type Declaration

**File**: `browser/resources/maho_space_create/maho_space_create.mojom-webui.js.d.ts`

Auto-regenerates from mojom. Verify `setThemePreview(themeJson: (string|undefined)): Promise<{success: boolean}>` appears after build.

### Gate

- `bun run typecheck` passes (from `maho/`)
- `autoninja -C out/Default chrome` builds
- Manual QA: open `chrome://maho-space-create`, change color swatch — browser chrome behind the page should live-update; close page — preview reverts to persisted theme

---

## 5. Phase 2 — GT-Aligned WebUI Port (Per-Element Checklist)

**Goal**: Bring old `chrome://maho-space-create` WebUI into element-for-element alignment with GT Image1, AND port all remaining required native behaviors. KP-2 (after-create browser switch) is deferred to Phase 7 (FFI reconciliation). KP-9 (theme picker keyboard accelerators) is deferred — not implemented.

The old WebUI is the **starting point**, not the parity anchor. Where old WebUI conflicts with GT, **GT wins**.

### 5.1 Per-Element GT Alignment Checklist

Every row below must be verified before Phase 2 is considered complete. Columns: Element ID (from GT Section 2), GT description, Old-WebUI delta (if any — `D*` code or `—` if already matching), target file path, DOM selector / classname / `data-maho-*` attr, verification step.

| Element ID | GT Description | Old-WebUI Delta | Target File | Selector / Attr | Verification |
|------------|---------------|-----------------|-------------|-----------------|--------------|
| E1 | Laptop + folder stack illustration, top-left of left panel | — (verify present) | `app.tsx` (LeftPanel) | `.sc-header-illustration` | Visual: illustration renders top-left of left panel |
| E2 | Title: `Create a Space`, large, bold/semibold | — (verify matches) | `app.tsx` (LeftPanel) | `.sc-title` | `textContent === 'Create a Space'` |
| E3 | Subtitle: `Separate your tabs for life, work, projects, and more.` | **D1**: Old text differs. Change to GT literal. | `app.tsx` (LeftPanel) | `.sc-subtitle` | `textContent === 'Separate your tabs for life, work, projects, and more.'` |
| E4 | Name input row, single horizontal flex line | — (verify flex layout) | `app.tsx` (LeftPanel) | `.sc-name-row` / `data-maho-el="name-row"` | Layout: single horizontal flex line containing E4a + E4b |
| E4a | Dashed `+` icon: rounded square, dashed border, `+` centered, muted/grey | **D3**: Missing in old WebUI. Add new element. | `app.tsx` (LeftPanel), `space_create.css` | `.sc-dashed-icon-placeholder` / `data-maho-el="dashed-plus-icon"` | Visual: dashed border square with `+` visible left of input |
| E4b | Text input, placeholder `Space name...` (three-dot ellipsis) | **D2**: Old placeholder `Enter a name`. Change to `Space name...`. | `app.tsx` (LeftPanel) | `input.sc-name-input` / `data-maho-el="name-input"` | `placeholder === 'Space name...'` |
| E5 | Profile row, single horizontal flex line | **D4**: Old layout has full description text + different icon. Rewrite to compact one-line. | `app.tsx` (LeftPanel), `space_create.css` | `.sc-profile-row` / `data-maho-el="profile-row"` | Layout: `[face-glyph] Profile [Default ▾] ⓘ` on single line |
| E5a | Smiley/face glyph icon (left) | **D4**: Old uses `👤`. Change to smiley/face glyph. | `app.tsx` (LeftPanel) | `.sc-profile-icon` / `data-maho-el="profile-icon"` | Visual: face glyph icon renders at left of profile row |
| E5b | Label text: `Profile` | **D4**: Old has `Profile` + description. Keep label, remove description. | `app.tsx` (LeftPanel) | `.sc-profile-label` | `textContent === 'Profile'` |
| E5c | Pill button: `Default` + down-chevron `▾` | **D4**: Old has different selector/dropdown. Rewrite as pill button. | `app.tsx` (LeftPanel), `space_create.css` | `.sc-profile-pill` / `data-maho-el="profile-pill"` | Renders as pill with `Default ▾` text |
| E5d | Info icon: `ⓘ` | **D4**: Missing in old WebUI. Add. | `app.tsx` (LeftPanel) | `.sc-profile-info-icon` / `data-maho-el="profile-info"` | Visual: `ⓘ` icon renders right of pill |
| E6 | Theme button, card-style, full row | **D5**: Old is dynamic summary row with `themeDesc`. Rewrite as static card-button. | `app.tsx` (LeftPanel), `space_create.css` | `.sc-theme-card` / `data-maho-el="theme-card"` | Card-style button with brush icon + `Choose a Theme` label |
| E6a | Brush/paintbrush icon (left) | **D5**: Old icon differs. Replace. | `app.tsx` (LeftPanel) | `.sc-theme-icon` | Visual: brush/paintbrush icon left of label |
| E6b | Label text: `Choose a Theme` | **D5**: Old shows dynamic `themeDesc`. Replace with static label. | `app.tsx` (LeftPanel) | `.sc-theme-label` | `textContent === 'Choose a Theme'` |
| E7 | Mode chip strip, horizontal row of 3 chips, top center of right panel | — (verify layout) | `app.tsx` (RightPanel) | `.sc-mode-strip` / `data-maho-el="mode-strip"` | Layout: 3 chips centered at top of right panel |
| E7a | Sparkle chip `✨`, active (highlighted background) | **D6**: Old uses `✦`. Change to `✨`. | `app.tsx` (RightPanel), `space_create.css` | `.sc-mode-chip[data-mode="sparkle"]` | Active state: highlighted background; glyph `✨` |
| E7b | Sun chip `☀`, idle | **D6**: Old uses `◌`. Change to `☀`. | `app.tsx` (RightPanel) | `.sc-mode-chip[data-mode="sun"]` | Idle state; glyph `☀` |
| E7c | Crescent moon chip `🌙`, idle | **D6**: Old uses `☽`. Change to `🌙`. | `app.tsx` (RightPanel) | `.sc-mode-chip[data-mode="moon"]` | Idle state; glyph `🌙` |
| E8 | Preview canvas, large rectangle, fills middle of right panel | **D7**: Old shows multi-element preview. Simplify to GT spec. | `app.tsx` (RightPanel), `space_create.css` | `.sc-preview-canvas` / `data-maho-el="preview-canvas"` | Large rectangle with dot-pattern and centered text |
| E8a | Dot-pattern background (grid of small dots, low contrast) | **D7**: Old has mode-aware solid/gradient bg. Replace with dot-pattern. | `space_create.css` | `.sc-preview-canvas::before` or `.sc-dot-pattern` | Visual: grid of small dots visible on canvas |
| E8b | Centered text: `Tap to pick a color for this space` | **D7**: Old shows `Choose a theme for your Space` + badge + label. Replace with single GT line. | `app.tsx` (RightPanel) | `.sc-preview-text` / `data-maho-el="preview-text"` | `textContent === 'Tap to pick a color for this space'` |
| E9 | Brightness controls, horizontal, below canvas | **D8**: Old has slider always visible. GT hides slider initially. | `app.tsx` (RightPanel), `space_create.css` | `.sc-brightness-controls` / `data-maho-el="brightness-controls"` | Layout: `−` and `+` buttons only in initial state |
| E9a | Minus button `−` | — (verify present) | `app.tsx` (RightPanel) | `.sc-brightness-minus` / `data-maho-el="brightness-minus"` | Button renders with `−` glyph |
| E9b | Plus button `+` | — (verify present) | `app.tsx` (RightPanel) | `.sc-brightness-plus` / `data-maho-el="brightness-plus"` | Button renders with `+` glyph |
| E9c | Slider hidden in initial state | **D8**: Old slider always visible. Add `opacity:0` / `aria-hidden="true"` until color is picked. | `app.tsx` (RightPanel), `space_create.css` | `.sc-brightness-slider` / `data-maho-el="brightness-slider"` | Initial state: `opacity: 0` or `aria-hidden="true"`; after color pick: visible |
| E10 | Color swatch row, horizontal | — (verify layout) | `app.tsx` (RightPanel / ColorPalette) | `.sc-swatch-row` / `data-maho-el="swatch-row"` | Layout: left chevron, 9 swatches, right chevron |
| E10a | Left chevron `‹` | — (verify present) | `app.tsx` (ColorPalette) | `.sc-swatch-chevron-left` | Glyph `‹` renders at left |
| E10b | Nine swatches: white/cream, pink, mauve/lavender, red, orange, yellow, green, blue, navy/dark blue | — (verify order and count) | `app.tsx` (ColorPalette) | `.sc-swatch` / `data-maho-el="swatch-N"` (N=0..8) | 9 swatches in GT order, correct colors |
| E10c | Right chevron `›` | — (verify present) | `app.tsx` (ColorPalette) | `.sc-swatch-chevron-right` | Glyph `›` renders at right |
| E11 | Bottom decorations, horizontal row at bottom of right panel | — (verify present) | `app.tsx` (RightPanel), `space_create.css` | `.sc-bottom-decorations` / `data-maho-el="bottom-decorations"` | Layout: wave left, dial right |
| E11a | Wave decoration: two overlapping sine-wave SVG paths, low-saturation | — (verify present) | `app.tsx` (RightPanel) | `.sc-wave-decoration` / `data-maho-el="wave-decoration"` | Visual: SVG wave paths render at left of decoration row |
| E11b | Dial: circular dotted dial, decorative initial state | — (verify present) | `app.tsx` (RightPanel) | `.sc-dial-decoration` / `data-maho-el="dial-decoration"` | Visual: dotted circular dial renders at right of decoration row |

### 5.2 Behavioral KP-* Checklist (interleaved with GT alignment)

These are ported native behaviors that must be present in the GT-aligned WebUI. Each modifies the same target files as GT alignment above.

| KP ID | Behavior | Target File | Selector / Location | Verification |
|-------|----------|-------------|---------------------|--------------|
| KP-3 | Form reset on open: `bootstrap()` resets state to `createInitialState()` before fetch | `store.ts` | `bootstrap()` method | Re-open dialog → form is blank, no stale state |
| KP-5 | Error display: error message shown below spacer, before create button | `app.tsx` (LeftPanel), `space_create.css` | `.sc-error-msg` | Trigger creation failure → error text visible in left panel |
| KP-6 | SelectAll on focus: name input text selected on mount | `app.tsx` (LeftPanel) | `nameInputRef` + `useEffect` | Open dialog → input text selected (if any) |
| KP-7 | Enter/Escape keyboard: Enter submits (if name valid + not creating), Escape closes | `app.tsx` (LeftPanel) | `onKeyDown` on name input | Enter with valid name → creates space; Escape → dialog closes |
| KP-10 | Brightness boundary disable: minus disabled at ≤15, plus disabled at ≥100 | `app.tsx` (RightPanel) | `disabled` attr on `.sc-brightness-minus` / `.sc-brightness-plus` | At min: minus disabled; at max: plus disabled |
| KP-11 | Swatch auto-scroll: active swatch scrolled into view | `app.tsx` (ColorPalette) | `activeSwatchRef` + `useEffect` + `scrollIntoView` | Select far swatch → scrolls into view |

### 5.3 Implementation Notes

**D1 (Subtitle copy)**: In `app.tsx` LeftPanel, change subtitle string from `Separate your tabs with calm, focused themes built for work, life, and everything in between.` to `Separate your tabs for life, work, projects, and more.`.

**D2 (Name input placeholder)**: In `app.tsx` LeftPanel, change `placeholder="Enter a name"` to `placeholder="Space name..."`.

**D3 (Dashed + icon)**: In `app.tsx` LeftPanel, add before the `<input>`:

```tsx
<div className="sc-dashed-icon-placeholder" data-maho-el="dashed-plus-icon" aria-hidden="true">
  <span>+</span>
</div>
```

CSS in `space_create.css`:

```css
.sc-dashed-icon-placeholder {
  width: 32px;
  height: 32px;
  border: 1.5px dashed rgba(255, 255, 255, 0.25);
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  margin-right: 10px;
  color: rgba(255, 255, 255, 0.3);
  font-size: 14px;
}
```

**D4 (Profile row)**: Rewrite profile section in `app.tsx` LeftPanel. Remove full-description layout. Replace with compact single-line: `[face-glyph] Profile [Default ▾] ⓘ`. Add `data-maho-el` attributes for each sub-element.

**D5 (Theme button)**: Replace dynamic `themeDesc` summary row with static card-button: `[brush-icon] Choose a Theme`. Add `.sc-theme-card` card-style background in CSS.

**D6 (Mode chip icons)**: In `app.tsx` RightPanel mode chips, change glyphs: `✦` → `✨`, `◌` → `☀`, `☽` → `🌙`. Update `data-mode` attribute values to `sparkle`, `sun`, `moon`.

**D7 (Preview canvas)**: Remove multi-element preview (badge, label, mode-aware background). Replace with:
- Dot-pattern background (CSS `radial-gradient` grid or SVG pattern).
- Single centered text: `Tap to pick a color for this space`.

**D8 (Brightness slider)**: Add state-conditional visibility for range slider. Initial state: `opacity: 0; pointer-events: none;` (or `aria-hidden="true"`). After color pick or brightness interaction: `opacity: 1; pointer-events: auto;`. Minus and plus buttons always visible.

**KP-5 (Error display)**: Add after spacer, before create button:

```tsx
{state.errorMessage && (
  <div className="sc-error-msg">{state.errorMessage}</div>
)}
```

CSS:

```css
.sc-error-msg {
  color: #ff6b6b;
  font-size: 12px;
  line-height: 1.4;
  padding: 0 24px;
  margin-bottom: 8px;
  text-align: center;
}
```

**KP-6 (SelectAll on focus)**: Add `useRef<HTMLInputElement>` + `useEffect(() => { ref.current?.select(); }, [])` + `ref={nameInputRef}` on the input.

**KP-7 (Enter/Escape keyboard)**: Add `onKeyDown` to name input:

```tsx
onKeyDown={(e: React.KeyboardEvent<HTMLInputElement>) => {
  if (e.key === 'Enter' && state.spaceName && !state.isCreating) {
    void store.createSpace();
  } else if (e.key === 'Escape') {
    store.closeDialog();
  }
}}
```

**KP-10 (Brightness boundary disable)**: Add `disabled={state.brightness <= 15}` on minus button, `disabled={state.brightness >= 100}` on plus button.

**KP-11 (Swatch auto-scroll)**: Add `activeSwatchRef` + `useEffect` with `scrollIntoView({ block: 'nearest', inline: 'nearest' })` on `selectedColorIndex` change.

**KP-3 (Form reset on open)**: In `store.ts` `bootstrap()`, add reset before fetch:

```typescript
async bootstrap(): Promise<void> {
  this.state = createInitialState();
  notifyListeners(this.listeners);
  const {profiles} = await this.pageHandler.getProfiles();
  // ...existing code...
}
```

### Gate

- `bun run typecheck` passes
- `autoninja -C out/Default chrome` builds
- Per-element visual verification against GT Section 2 (textual):
  - E1–E6: Left panel elements render in correct order, with correct text, icons, and layout
  - E7–E11: Right panel elements render in correct order, with GT glyphs, dot-pattern, hidden slider
  - D1–D8: All deltas resolved (old WebUI copy/icons/layout replaced with GT)
  - KP-3/5/6/7/10/11: All behavioral ports verified
- DOM assertions: every `data-maho-el` attribute listed in checklist is queryable

---

## 6. Phase 3 — Rebind `MahoSpaceCreateDialog` as WebView Host

**Goal**: Replace the current native-view-wrapping `MahoSpaceCreateDialog` with a `views::DialogDelegate` hosting a `views::WebView` loading `chrome://maho-space-create/`. The existing files at `browser/ui/views/space_create/maho_space_create_dialog.cc/.h` are dead code (no BUILD.gn target, zero external consumers) — they are rewritten in-place.

### 6.1 Dialog Header

**File**: `browser/ui/views/space_create/maho_space_create_dialog.h`

Rewrite entirely:

```cpp
#ifndef MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_

class Browser;

namespace views {
class Widget;
}  // namespace views

class MahoSpaceCreateDialog {
 public:
  static void Open(Browser* parent_browser);
  static void CloseActiveDialog();
  static views::Widget* GetWidgetForTesting();

 private:
  MahoSpaceCreateDialog() = delete;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPACE_CREATE_MAHO_SPACE_CREATE_DIALOG_H_
```

Removed: `MahoSidebarCreateSpaceView` forward decl, `GetCreateSpaceViewForTesting()`, `SetCloseCallback`, `RequestNameFocus`.
Added: `CloseActiveDialog()`.

### 6.2 Dialog Implementation

**File**: `browser/ui/views/space_create/maho_space_create_dialog.cc`

Rewrite entirely. Pattern mirrors `maho_space_config_dialog.cc`:

- Anonymous namespace:
  - `constexpr int kDialogWidth = 456;`
  - `constexpr int kDialogHeight = 600;` (matching prior native form height, slightly larger than `kDialogHeightCreate` = 560 to accommodate WebUI padding)
  - `views::Widget* g_space_create_dialog_widget = nullptr;`
  - `MahoSpaceCreateDialogDelegate : public views::DialogDelegate`:
    - Constructor: `SetButtons(kNone)`, `SetModalType(kWindow)`, no close button, no title, no resize, `set_fixed_width(kDialogWidth)`. Create `views::WebView(browser->profile())`, set preferred size `kDialogWidth x kDialogHeight`, `LoadInitialURL(GURL(maho::kMahoSpaceCreateURL))`.
    - `GetContentsView()` → returns `web_view_`.
    - `WindowClosing()`: clear `g_space_create_dialog_widget`, eagerly clear preview override (`MahoSpaceThemeState::HasPreviewOverride()` → `ClearPreviewOverride()` + `UpdateFromCore()` + cache reset), null `web_view_`.
- `MahoSpaceCreateDialog::Open(Browser*)`:
  - If `g_space_create_dialog_widget` exists: `Show()` + `Activate()` + return.
  - Else: create `MahoSpaceCreateDialogDelegate`, `CreateDialogWidget`, store in global, `Show()` + `Activate()`.
  - Fresh `WebView` is created each time (no caching — widget is destroyed on close, delegate + WebView are destroyed with it).
- `MahoSpaceCreateDialog::CloseActiveDialog()`:
  - If global widget exists: `widget->Close()`.
- `MahoSpaceCreateDialog::GetWidgetForTesting()`:
  - Return `g_space_create_dialog_widget`.

Required includes: `base/memory/raw_ptr.h`, `chrome/browser/ui/browser.h`, `chrome/browser/ui/browser_window.h`, `maho/browser/ui/theme/maho_space_theme_state.h`, `maho/components/constants/webui_url_constants.h`, `ui/base/mojom/dialog_button.mojom.h`, `ui/color/color_provider_manager.h`, `ui/gfx/geometry/size.h`, `ui/views/controls/webview/webview.h`, `ui/views/widget/widget.h`, `ui/views/window/dialog_delegate.h`, `url/gurl.h`.

### 6.3 BUILD.gn for Dialog

**File**: `browser/ui/views/space_create/BUILD.gn` — **NEW FILE**

```gn
# Copyright 2026 Maho Browser. All rights reserved.

source_set("maho_space_create_dialog") {
  sources = [
    "maho_space_create_dialog.cc",
    "maho_space_create_dialog.h",
  ]

  deps = [
    "//base",
    "//chrome/browser/profiles:profile",
    "//chrome/browser/ui",
    "//maho/browser:maho_theme_support",
    "//maho/components/constants",
    "//ui/base",
    "//ui/color",
    "//ui/gfx",
    "//ui/views",
    "//ui/views/controls/webview",
    "//url",
  ]
}
```

### 6.4 Wire Into Browser Build

**File**: `browser/BUILD.gn` (~line 194)

Add alongside existing `space_config_dialog` dep:

```gn
"//maho/browser/ui/views/space_create:maho_space_create_dialog",
```

### Gate

- `autoninja -C out/Default chrome` builds (dialog is not yet called from new entry points)
- Manual QA: temporarily invoke `MahoSpaceCreateDialog::Open(browser)` from DevTools or a test entry point — verify the dialog opens showing GT-aligned `chrome://maho-space-create` content, with live theme preview working

---

## 7. Phase 4 — Repoint Entry Points

**Goal**: All callers that previously opened the native inline sidebar surface or called FFI directly now open `MahoSpaceCreateDialog::Open(browser)`.

### 7.1 Footer Popover kNewSpace

**File**: `browser/ui/views/sidebar/maho_sidebar_footer_view.cc` (~line 462)

Change:

```cpp
// OLD:
sidebar_view->ShowCreateSpaceSurface();
```

To:

```cpp
// NEW:
MahoSpaceCreateDialog::Open(browser_);
```

Remove the `FindSidebarViewAncestor(this)` call + null check (no longer needed for this branch — `browser_` is directly available). Add `#include "maho/browser/ui/views/space_create/maho_space_create_dialog.h"`.

### 7.2 Command Palette new_space

**File**: `browser/ui/views/command/maho_command_action_handler.cc` (~lines 383-394)

Replace entire `new_space` branch:

```cpp
// OLD (lines 383-394):
} else if (action_id == "new_space") {
  MahoCore* core = maho::GetCore();
  if (core) {
    // ...direct FFI maho_core_create_space call...
  }
}
```

With:

```cpp
// NEW:
} else if (action_id == "new_space") {
  MahoSpaceCreateDialog::Open(browser);
}
```

Add `#include "maho/browser/ui/views/space_create/maho_space_create_dialog.h"`.

### 7.3 BUILD.gn Updates

**File**: `browser/ui/views/sidebar/BUILD.gn` — `maho_sidebar_core` deps (~line 190)

Add: `"//maho/browser/ui/views/space_create:maho_space_create_dialog",`

**File**: `browser/ui/views/command/BUILD.gn` — command overlay deps

Add: `"//maho/browser/ui/views/space_create:maho_space_create_dialog",`

### Gate

- `autoninja -C out/Default chrome` builds
- Manual QA:
  - Footer + action popover → kNewSpace: opens `MahoSpaceCreateDialog` with GT-aligned `chrome://maho-space-create` content
  - Command palette "new_space" action: opens `MahoSpaceCreateDialog` (not silent FFI create)
  - Creating a space through the dialog works end-to-end
  - Dialog closes after successful creation

---

## 8. Phase 5 — Delete Native Form + kCreateSpace Body Mode + Dead Dialog Wrapper

**Goal**: Remove `MahoSidebarCreateSpaceView` and all sidebar inline create-space surface machinery. This phase must also inline-delete the 7 DELETE-category tests to maintain compilability.

### 8.1 Delete Native Form Files

**DELETE**: `browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` (2150 lines)
**DELETE**: `browser/ui/views/sidebar/maho_sidebar_create_space_view.h` (111 lines)

### 8.2 Remove from BUILD.gn

**File**: `browser/ui/views/sidebar/BUILD.gn` — `maho_sidebar_library` source_set (~lines 130-131)

Remove from `sources`:

```gn
"maho_sidebar_create_space_view.cc",
"maho_sidebar_create_space_view.h",
```

Audit deps: remove any deps that were exclusively required by the create space view (e.g., `//maho/browser/ui/views/space_config:maho_space_config_dialog` if it was only consumed here — verify before removing).

### 8.3 Sidebar View Header Cleanup

**File**: `browser/ui/views/sidebar/maho_sidebar_view.h`

- Remove `MahoSidebarBodyMode::kCreateSpace` enum value (~line 62). Remaining enum values stay as-is — verify against current `.h` to confirm what other modes exist.
- Remove `ShowCreateSpaceSurface()` declaration (~line 117).
- Remove `raw_ptr<MahoSidebarCreateSpaceView> create_space_view_ = nullptr;` member (~line 211).
- Remove forward declaration of `MahoSidebarCreateSpaceView` (or `namespace maho` block containing it).
- Remove any `create_space_view_`-related getter/accessor declarations (~line 132 if present).

### 8.4 Sidebar View Implementation Cleanup

**File**: `browser/ui/views/sidebar/maho_sidebar_view.cc`

- Remove `#include "maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.h"`.
- Remove `ShowCreateSpaceSurface()` method (~lines 620-622).
- Remove `kCreateSpace` case in `SetBodyMode()` switch (~lines 301-348): remove `create_space_view_->SetVisible(true)`, `PrepareForOpen()`, `RequestNameFocus()` calls.
- Remove `create_space_view_` construction in `BuildUi()` (~lines 280-283): remove the `new MahoSidebarCreateSpaceView(browser_)` allocation, `SetVisible(false)`, `SetCloseCallback` wiring.
- Remove focus-routing exclusion for `create_space_view_` in `CanRestoreFocusToView()` (~line 420).
- Remove any other `create_space_view_` references.

### 8.5 Footer View Header Cleanup

**File**: `browser/ui/views/sidebar/maho_sidebar_footer_view.h`

- Remove dead `open_create_space_after_popover_close_` field (~line 132) — never set to true in current code.

### 8.6 Inline-Delete 7 Tests (Compilability Requirement)

**File**: `browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

Delete these 7 test bodies (they reference `MahoSidebarCreateSpaceView`, `kCreateSpace`, or `OpenForCreate` which no longer exist):

| Test | Line |
|------|------|
| `CreateSpaceOpensInlineSidebarSurface` | :190 |
| `CreateSpaceThemeRowOpensSharedConfigDialog` | :214 |
| `CreateSpaceEscapeReturnsToTabs` | :251 |
| `ThemeDialogZenSelectionBuildsValidatorCompatibleJson` | :401 |
| `CreateThemeButtonOpenSharedDialogInCreateMode` | :467 |
| `CreateThemeSharedDialogAcceptUpdatesCreateView` | :506 |
| `CreateThemeSharedDialogCancelPreservesCreateView` | :561 |

### Gate

- `autoninja -C out/Default chrome` builds with zero references to deleted files
- `lsp_diagnostics` clean on all modified files
- `grep -r "MahoSidebarCreateSpaceView\|ShowCreateSpaceSurface\|kCreateSpace\|create_space_view_\|GetCreateSpaceViewForTesting" maho-chromium/` returns zero matches
- Manual QA: sidebar shows tabs/library modes only; footer kNewSpace still opens dialog (Phase 4 wiring intact)

---

## 9. Phase 6 — Remove `is_create_mode_` from maho_space_config

**Goal**: `chrome://maho-space-config` becomes edit-mode only. All create-mode branches, UI, CSS, and state machinery are removed.

### 9.1 Mojom

**File**: `browser/ui/webui/maho_space_config/maho_space_config.mojom`

Remove from `interface Page` (~line 71-75):

```mojom
// DELETE THIS:
// Sent in create-mode when the user accepts a theme selection.
ThemeChosen(string theme_json);
```

`interface Page` becomes `interface Page {};` (empty).

### 9.2 Space Config Dialog Header

**File**: `browser/ui/views/space_config/maho_space_config_dialog.h`

- Remove `ActiveMode::kCreate` from enum (~line 24). Enum becomes `{kNone, kEdit}`.
- Remove `OpenForCreate()` static declaration (~lines 47-51).
- Remove `InvokeCreateModeThemeChosenCallback()` static declaration (~lines 56-57).

### 9.3 Space Config Dialog Implementation

**File**: `browser/ui/views/space_config/maho_space_config_dialog.cc`

- Remove `kDialogHeightCreate` constant (~line 23).
- Remove `GetCreateModeThemeChosenCallback()` static function (~lines 30-33).
- Remove `create_mode` parameter from `MahoSpaceConfigDialogDelegate` constructor (~line 55): always use `kDialogHeight` (280). Remove `?mode=create` and `?draft_theme=` URL param appending (~lines 68-73). Remove `create_mode` ternary for dialog height (~line 76).
- Remove `create_mode` parameter from `OpenDialogImpl()` (~line 117). Remove create-mode `ActiveMode` assignment (~lines 137-139) — always assign `kEdit`.
- In `Open()` (~lines 147-166): simplify — remove kCreate mode-conflict check (~lines 158-159). Only kEdit brings-to-front.
- Remove `OpenForCreate()` static entirely (~lines 169-196).
- Remove `InvokeCreateModeThemeChosenCallback()` static entirely (~lines 199-204).
- In `WindowClosing()` (~line 90): remove `GetCreateModeThemeChosenCallback().Reset()`.

### 9.4 Space Config Page Handler Header

**File**: `browser/ui/webui/maho_space_config/maho_space_config_page_handler.h`

- Remove constructor parameters: `bool is_create_mode`, `const std::string& draft_theme_json` (~lines 31-32).
- Remove member fields: `bool is_create_mode_ = false;` (~line 65), `std::string draft_theme_json_;` (~line 66).

### 9.5 Space Config Page Handler Implementation

**File**: `browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc`

- Remove `is_create_mode` and `draft_theme_json` constructor params (~lines 285-286). Remove initializer list entries (~lines 293-294).
- In `GetSpaceInfo()` (~lines 304-316): remove `if (is_create_mode_)` early-return branch. Only the ThreadPool post-task path remains.
- In `UpdateTheme()` (~lines 385-388): remove `if (is_create_mode_)` branch that calls `InvokeCreateModeThemeChosenCallback`. Only the ThreadPool `ApplyThemeOnPool` path remains.
- In `PreviewTheme()` (~lines 455-461): remove `if (is_create_mode_)` unconditional-apply branch. Only the `IsActiveSpaceOnPool` guard path remains.
- Remove `#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"` if the only remaining call site was `InvokeCreateModeThemeChosenCallback` (verify — `CloseDialog()` still calls `CloseActiveDialog()` so the include stays if needed).

### 9.6 Space Config UI Header

**File**: `browser/ui/webui/maho_space_config/maho_space_config_ui.h`

- Remove member fields: `bool is_create_mode_ = false;` (~line 54), `std::string draft_theme_json_;` (~line 55).

### 9.7 Space Config UI Implementation

**File**: `browser/ui/webui/maho_space_config/maho_space_config_ui.cc`

- In constructor (~lines 57-64): remove `mode` URL param parsing and `is_create_mode_` assignment. Remove `draft_theme` URL param parsing and `draft_theme_json_` assignment.
- In `CreatePageHandler()` (~lines 99-122): remove `!is_create_mode_` guard from second URL parse block (~line 99). Remove `mode` re-parse (~lines 109-112). Remove `draft_theme` re-parse (~lines 114-116). Remove `is_create_mode_` and `draft_theme_json_` from handler constructor call (~line 122).

### 9.8 Space Config WebUI Store

**File**: `browser/resources/maho_space_config/react/store.ts`

- Remove `isCreateMode` field from state interface (~line 32).
- Remove `isCreateMode: false` from initial state (~line 102).
- Remove `const isCreateMode = params.get('mode') === 'create';` (~line 155) and all 20 references to `isCreateMode` throughout the file.
- Remove `draft_theme` URL param parsing (~line 156).
- Simplify all conditional branches that checked `isCreateMode`.

### 9.9 Space Config WebUI App

**File**: `browser/resources/maho_space_config/react/app.tsx`

- Remove `isCreateMode` class toggling (~lines 66-73): `scfg-body-create`, `scfg-shell-create`.
- Remove `isCreateMode` conditional in auto-scroll effect (~line 99).
- Remove `isCreateMode` conditional in focus effect (~line 111).
- Remove `isCreateMode` conditional rendering (~line 131): delete the entire create-mode UI block (the `CreateModeView` or equivalent component, ~lines 310-520).
- Remove `scfg-root-create` class from remaining root element.

### 9.10 Space Config CSS

**File**: `browser/resources/maho_space_config/space_config.css`

Remove all ~108 `scfg-create*` CSS rules (variables, classes, pseudo-states, media queries). Also remove `scfg-body-create` and `scfg-shell-create` rules.

### Gate

- `autoninja -C out/Default chrome` builds
- `bun run typecheck` passes
- `grep -r "is_create_mode_\|OpenForCreate\|InvokeCreateModeThemeChosenCallback\|kDialogHeightCreate\|ThemeChosen\|isCreateMode\|scfg-create" maho-chromium/` returns zero matches
- Manual QA: `chrome://maho-space-config?space_id=...` still works for editing; `?mode=create` param is silently ignored

---

## 10. Phase 7 — FFI Reconciliation

**Goal**: Align the WebUI `CreateSpace` success path with the native behavior per Oracle decision (d). Fix `CloseDialog` to route through the dialog widget. Includes KP-2 (post-create SwitchToSpace).

### 10.1 Remove Pool-Thread Activation

**File**: `browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

In `CreateSpaceOnPool()` (~line 122), remove:

```cpp
// DELETE:
maho_core_activate_space(core, parsed_id->c_str());
```

Activation will be handled by `SwitchToSpace` on the UI thread.

### 10.2 Reconcile UI-Thread Reply Lambda

**File**: `browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

In the UI-thread reply lambda (~lines 201-215), change the success branch to:

```cpp
if (weak_self && result.success) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  bridge->RegisterSpace(result.space_id,
                        base::FilePath::FromASCII(profile_id));
  bridge->SwitchToSpace(weak_self->browser_, result.space_id);
  ui::ColorProviderManager::Get().ResetColorProviderCache();
}
```

Changes:
- `RegisterSpace` stays (same as before).
- `MahoSpaceThemeState::UpdateFromCore()` **removed** — `SwitchToSpace` already calls it internally (`maho_space_profile_bridge.cc:85`).
- `SwitchToSpace(browser_, result.space_id)` **added** — owns activation + theme state update.
- `ResetColorProviderCache()` stays.

### 10.3 Fix CloseDialog

**File**: `browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`

Change `CloseDialog()` (~lines 218-222):

```cpp
// OLD:
void MahoSpaceCreatePageHandler::CloseDialog() {
  if (web_contents_) {
    web_contents_->ClosePage();
  }
}

// NEW:
void MahoSpaceCreatePageHandler::CloseDialog() {
  MahoSpaceCreateDialog::CloseActiveDialog();
}
```

Add `#include "maho/browser/ui/views/space_create/maho_space_create_dialog.h"`.

### 10.4 BUILD.gn Update

**File**: `browser/ui/webui/maho_space_create/BUILD.gn`

Add to `deps`:

```gn
"//maho/browser/ui/views/space_create:maho_space_create_dialog",
```

### FFI Verification Matrix

| Item | Resolution | Verified |
|------|------------|----------|
| FD-1: Activation | `maho_core_activate_space` removed from pool thread; `SwitchToSpace` handles activation on UI thread | Phase 7 gate |
| FD-2: Icon field | WebUI sets `DEFAULT_ICON='\u229e'`; kept intentionally | No action |
| FD-3: Theme ordering | `update_space_config` event dispatches theme on pool thread before `SwitchToSpace` on UI thread. **VERIFY**: confirm with Rust core team that `update_space_config` is safe on non-active space. | Manual verification step |
| FD-4: UpdateFromCore | Removed; `SwitchToSpace` covers it | Phase 7 gate |
| FD-5: Profile fallback | No equivalent needed | No action |

### Gate

- `autoninja -C out/Default chrome` builds
- Manual QA:
  - Create space via dialog → new space appears, browser switches to it, theme applies correctly, dialog closes
  - Verify no double-activation (only `SwitchToSpace`, not `maho_core_activate_space`)
  - Verify theme applies correctly to the newly created space
  - Verify `CloseDialog` Mojo call closes the widget (not `ClosePage`)

---

## 11. Phase 8 — Test Cutover

**Goal**: Add replacement tests, migrate extractable portions, and add new coverage. The 7 DELETE-category tests were already removed in Phase 5.

### 11.1 Test Classification Matrix

| Test | File:Line | Action | Detail |
|------|-----------|--------|--------|
| `CreateSpaceOpensInlineSidebarSurface` | `maho_sidebar_spaces_interactive_test.cc:190` | DELETED in Phase 5 | Referenced `kCreateSpace` body mode |
| `CreateSpaceThemeRowOpensSharedConfigDialog` | `:214` | DELETED in Phase 5 | Referenced native theme row → config dialog create-mode |
| `CreateSpaceEscapeReturnsToTabs` | `:251` | DELETED in Phase 5 | Referenced inline sidebar escape → kTabs |
| `ThemeDialogZenSelectionBuildsValidatorCompatibleJson` | `:401` | DELETED in Phase 5 | Referenced native theme picker internals |
| `CreateThemeButtonOpenSharedDialogInCreateMode` | `:467` | DELETED in Phase 5 | Referenced `OpenForCreate` |
| `CreateThemeSharedDialogAcceptUpdatesCreateView` | `:506` | DELETED in Phase 5 | Referenced `InvokeCreateModeThemeChosenCallback` |
| `CreateThemeSharedDialogCancelPreservesCreateView` | `:561` | DELETED in Phase 5 | Referenced native create view preservation |
| `CreateSpaceSubmitSucceedsAndClosesSurface` | `:286` | REPLACE | Rewrite as WebUI browser test |
| `CreateSpaceFailureShowsInlineErrorAndPreservesName` | `:352` | REPLACE | Rewrite as WebUI browser test |
| `CreateSpaceWhitespaceOnlyNameKeepsCreateDisabled` | `:379` | REPLACE | Rewrite as WebUI browser test |
| `CreateThemeSharedDialogBrightnessStepperInteraction` | `:614` | MIGRATE | Extract brightness stepper `EvalJs` portion as edit-mode config test only |
| `FooterActionOpensSpacesSurface` | `:160` | KEEP | No dependency on deleted code |
| `SpaceDotSwitchesTabList` | `:719` | KEEP | No dependency on deleted code |
| `UpdateWithNewSpaceIdClearsStaleRows` | `maho_sidebar_contract_test.cc:615` | KEEP | No dependency on deleted code |

### 11.2 New Browser Test File

**ADD**: `browser/ui/webui/maho_space_create/maho_space_create_browsertest.cc`

Tests to implement:

**Page Handler Tests** (unit/browser scope):
- `GetProfiles_ReturnsNonEmpty` — verify profiles list is populated
- `GetSpaceCount_ReturnsCurrentCount` — verify count matches reality
- `CreateSpace_SuccessReturnsTrue` — create with valid params, assert success
- `CreateSpace_EmptyNameReturnsFalse` — create with empty name, assert failure
- `CloseDialog_ClosesWidget` — verify `MahoSpaceCreateDialog::CloseActiveDialog()` is invoked
- `SetThemePreview_AppliesOverride` — call with theme JSON, verify `HasPreviewOverride` returns true
- `SetThemePreview_NullClearsOverride` — call with null, verify override cleared

**E2E Browser Tests** (replacing deleted tests):
- `CreateSpaceSubmitSucceeds` — navigate to `chrome://maho-space-create` in dialog, `EvalJs` fill name + click create, assert space count increments, active space switches, dialog closes
- `CreateSpaceFailureShowsError` — stub failing Mojo response, assert error DOM element visible, name preserved
- `CreateSpaceWhitespaceNameDisablesButton` — `EvalJs` whitespace-only input, assert create button `disabled`
- `CreateSpaceEnterKeySubmits` — fill name, dispatch Enter keydown, assert creation
- `CreateSpaceEscapeKeyCancels` — dispatch Escape keydown, assert dialog closes
- `ThemePreviewLifecycle` — pick color, assert `HasPreviewOverride` true; close dialog, assert override cleared

**GT DOM Assertion Tests** (new — validates GT element presence):
- `GTElements_LeftPanel` — `EvalJs` query all `data-maho-el` attributes for E1–E6, assert each is non-null
- `GTElements_RightPanel` — `EvalJs` query all `data-maho-el` attributes for E7–E11, assert each is non-null
- `GTElements_PlaceholderText` — assert `input[data-maho-el="name-input"]` has `placeholder === 'Space name...'`
- `GTElements_SubtitleText` — assert `.sc-subtitle` `textContent` matches GT literal
- `GTElements_PreviewText` — assert `[data-maho-el="preview-text"]` `textContent` matches GT literal
- `GTElements_SliderHiddenInitially` — assert `[data-maho-el="brightness-slider"]` has `opacity: 0` or `aria-hidden`

### 11.3 Command Action Test

**ADD** test case to `browser/ui/views/command/maho_command_overlay_interactive_uitest.cc`:

- `NewSpaceActionOpensCreateDialog` — `ExecuteCommandAction(browser(), "new_space")`, assert `MahoSpaceCreateDialog::GetWidgetForTesting()` is non-null

### 11.4 Migrate Brightness Stepper

**MODIFY**: `browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`

The `CreateThemeSharedDialogBrightnessStepperInteraction` test at line 614 — extract only the EvalJs/ExecJs brightness stepper portion as a test against `chrome://maho-space-config` in **edit-mode** (not create-mode). Drop all `InvokeCreateModeThemeChosenCallback` / `theme_summary_label_for_testing()` assertions.

### 11.5 BUILD.gn Updates

**File**: `browser/ui/webui/maho_space_create/BUILD.gn`

Add:

```gn
source_set("browser_tests") {
  testonly = true
  sources = [ "maho_space_create_browsertest.cc" ]

  defines = [ "HAS_OUT_OF_PROC_TEST_RUNNER" ]

  deps = [
    ":maho_space_create",
    "//base",
    "//base/test:test_support",
    "//chrome/browser",
    "//chrome/browser/ui",
    "//chrome/test:test_support",
    "//chrome/test:test_support_ui",
    "//content/public/browser",
    "//content/test:test_support",
    "//maho/browser:maho_theme_support",
    "//maho/browser/ui/views/space_create:maho_space_create_dialog",
    "//testing/gtest",
    "//ui/views",
    "//ui/views:test_support",
  ]
}
```

**File**: `browser/ui/views/sidebar/BUILD.gn` — `interactive_tests` deps (~line 275)

- Remove `//maho/browser/ui/views/space_config:maho_space_config_dialog` if no remaining tests need it (verify).
- Add `//maho/browser/ui/views/space_create:maho_space_create_dialog` if the migrated brightness test references it.

### Gate

- `autoninja -C out/Default chrome` builds
- All remaining interactive_tests pass
- All new browser_tests pass (including GT DOM assertion tests)
- Zero skipped or disabled tests related to space create

---

## 12. Phase 9 — Verification, Integration Gate & Visual Diff Gate

**Goal**: Comprehensive verification. `lsp_diagnostics` alone is NOT sufficient — manual QA, interactive tests, and the Visual Diff Gate are mandatory.

### 12.1 Standard Verification Matrix

| # | Check | Tool/Command | Pass Criterion | Phase |
|---|-------|-------------|-----------------|-------|
| 1 | Typecheck | `bun run typecheck` from `maho/` | Zero errors | 9 |
| 2 | Chromium build | `autoninja -C out/Default chrome` | Zero errors | 9 |
| 3 | Rust tests | `bun run test` from `maho/` | All pass | 9 |
| 4 | Interactive tests | Run `interactive_tests` suite | All remaining pass | 9 |
| 5 | New browser tests | Run `maho_space_create` browser_tests | All pass (including GT DOM assertions) | 9 |
| 6 | LSP diagnostics | `lsp_diagnostics` on all modified files | Zero errors/warnings | 9 |
| 7 | Dead code scan | `grep -r` for all deleted symbols | Zero matches in `maho-chromium/` | 9 |
| 8 | Manual: Footer → New Space | Click footer + action → kNewSpace | Dialog opens with GT-aligned content | 9 |
| 9 | Manual: Command → new_space | Trigger command action | Dialog opens (not silent FFI create) | 9 |
| 10 | Manual: Create E2E | Fill name, pick theme, submit | Space created, switched, themed, dialog closes | 9 |
| 11 | Manual: Error path | Trigger creation failure | Error message visible in dialog | 9 |
| 12 | Manual: Keyboard | Enter/Escape in name field | Enter submits, Escape cancels | 9 |
| 13 | Manual: Theme preview | Change color swatch | Chrome behind dialog updates live | 9 |
| 14 | Manual: Dialog close | Close without creating | Preview reverts | 9 |
| 15 | Manual: Brightness | Click minus at 15, plus at 100 | Buttons disabled at boundaries | 9 |
| 16 | Manual: Swatch scroll | Select far-right swatch | Auto-scrolls | 9 |
| 17 | Manual: Config edit | Open space config for existing space | Works normally, no create-mode | 9 |
| 18 | FD-3: theme ordering | Confirm `update_space_config` safe on non-active space | Manual verification with Rust core team | 9 |

### 12.2 Visual Diff Gate (Image1)

**DEPENDENCY**: `.sisyphus/plans/assets/theme-parity-image1.png` is present on disk. This gate is eligible to run during Phase 9.

**Procedure**: When Image1 binary is available, perform per-element bbox/screenshot comparison:

| Element ID | GT Description | DOM Selector | Visual Check | DOM/Class Assertion |
|------------|---------------|--------------|--------------|---------------------|
| E1 | Laptop + folder illustration, top-left | `.sc-header-illustration` | Screenshot bbox: top-left quadrant of left panel contains illustration | Element exists, dimensions > 0 |
| E2 | Title: `Create a Space` | `.sc-title` | Font: large, bold/semibold, light-on-dark | `textContent === 'Create a Space'`, `font-weight >= 600` |
| E3 | Subtitle: `Separate your tabs for life, work, projects, and more.` | `.sc-subtitle` | Font: smaller than E2, muted color | `textContent` matches GT literal, `color` has reduced alpha/lightness vs E2 |
| E4a | Dashed `+` icon | `[data-maho-el="dashed-plus-icon"]` | Dashed border visible, `+` centered | `border-style: dashed`, child text `+` |
| E4b | Input: placeholder `Space name...` | `[data-maho-el="name-input"]` | Placeholder text visible, caret at left when focused | `placeholder === 'Space name...'` |
| E5 | Profile row: compact one-line | `[data-maho-el="profile-row"]` | Single line: face-glyph, Profile, Default pill, info icon | `display: flex`, child count ≥ 4 |
| E5c | Pill button: `Default ▾` | `[data-maho-el="profile-pill"]` | Pill shape with down-chevron | `textContent` contains `Default` and `▾` |
| E5d | Info icon: `ⓘ` | `[data-maho-el="profile-info"]` | Info circle icon visible | Element exists, visible |
| E6 | Theme card: brush + `Choose a Theme` | `[data-maho-el="theme-card"]` | Card background distinct from rows above | `.sc-theme-card` has distinct `background` |
| E7a | Sparkle chip `✨`, active | `.sc-mode-chip[data-mode="sparkle"]` | Highlighted background, square halo | Has active class/state, glyph `✨` |
| E7b | Sun chip `☀`, idle | `.sc-mode-chip[data-mode="sun"]` | No highlight | No active class, glyph `☀` |
| E7c | Moon chip `🌙`, idle | `.sc-mode-chip[data-mode="moon"]` | No highlight | No active class, glyph `🌙` |
| E8a | Dot-pattern background | `.sc-dot-pattern` or `.sc-preview-canvas::before` | Grid of small dots visible | `background-image` contains `radial-gradient` or SVG pattern ref |
| E8b | Text: `Tap to pick a color for this space` | `[data-maho-el="preview-text"]` | Centered in canvas | `textContent === 'Tap to pick a color for this space'`, `text-align: center` |
| E9a | Minus button `−` | `[data-maho-el="brightness-minus"]` | `−` glyph visible | Element contains `−` |
| E9b | Plus button `+` | `[data-maho-el="brightness-plus"]` | `+` glyph visible | Element contains `+` |
| E9c | Slider hidden initially | `[data-maho-el="brightness-slider"]` | Not visible in initial state | `opacity === '0'` or `aria-hidden === 'true'` |
| E10a | Left chevron `‹` | `.sc-swatch-chevron-left` | Glyph visible | Contains `‹` |
| E10b | Nine swatches | `.sc-swatch` | 9 swatch buttons in GT order | `querySelectorAll('.sc-swatch').length === 9` |
| E10c | Right chevron `›` | `.sc-swatch-chevron-right` | Glyph visible | Contains `›` |
| E11a | Wave decoration | `[data-maho-el="wave-decoration"]` | SVG wave paths visible, low saturation | Element contains `<svg>` with `<path>` children |
| E11b | Dial decoration | `[data-maho-el="dial-decoration"]` | Dotted circular dial visible | Element exists, dimensions > 0 |

**Overall Visual Diff**: Full-dialog screenshot at 456×600 viewport compared against `.sisyphus/plans/assets/theme-parity-image1.png`. Structural Similarity Index (SSIM) must exceed 0.85 after masking dynamic content areas (caret blink, system fonts). If SSIM < 0.85, per-element comparison above identifies divergent elements for remediation.

**Color/Spacing Assertions**:
- Background color: dark navy (`hsl` value within ±5 of GT sample)
- Left panel slightly deeper navy than right panel
- Preview canvas has subtle border/inset glow effect (`box-shadow` or `border` with low-alpha glow)
- All text light-on-dark (foreground lightness > 70%, background lightness < 30%)

### Dead Code Scan Patterns

```bash
grep -rn \
  "MahoSidebarCreateSpaceView\|ShowCreateSpaceSurface\|kCreateSpace\|create_space_view_\|GetCreateSpaceViewForTesting\|open_create_space_after_popover_close_\|is_create_mode_\|OpenForCreate\|InvokeCreateModeThemeChosenCallback\|kDialogHeightCreate\|ThemeChosen\|isCreateMode\|scfg-create\|scfg-body-create\|scfg-shell-create" \
  maho-chromium/
```

Expected: zero matches.

---

## 13. File-by-File Modification Matrix

### Files Modified (28 files)

| # | Path (repo-relative from `maho-chromium/`) | Phase | Action | Summary |
|---|---------------------------------------------|-------|--------|---------|
| 1 | `browser/ui/webui/maho_space_create/maho_space_create.mojom` | 1 | MODIFY | Add `SetThemePreview` method |
| 2 | `browser/ui/webui/maho_space_create/maho_space_create_page_handler.h` | 1, 7 | MODIFY | Add `SetThemePreview` decl; update destructor |
| 3 | `browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc` | 1, 7 | MODIFY | Add `SetThemePreview` impl + destructor cleanup; FFI reconciliation; `CloseDialog` → `CloseActiveDialog` |
| 4 | `browser/ui/webui/maho_space_create/BUILD.gn` | 7, 8 | MODIFY | Add dep on `space_create_dialog`; add `browser_tests` source_set |
| 5 | `browser/resources/maho_space_create/react/store.ts` | 1, 2 | MODIFY | Add `sendThemePreview()` calls; form-reset in `bootstrap()`; GT-aligned state defaults |
| 6 | `browser/resources/maho_space_create/react/app.tsx` | 2 | MODIFY | GT alignment (D1–D8, E1–E11 layout); KP-5 error, KP-6 select, KP-7 keyboard, KP-10 brightness disable, KP-11 autoscroll |
| 7 | `browser/resources/maho_space_create/space_create.css` | 2 | MODIFY | GT alignment styles; `.sc-error-msg`, `.sc-dashed-icon-placeholder`, `.sc-theme-card`, `.sc-profile-pill`, `.sc-dot-pattern`, slider hidden state, mode chip updates |
| 8 | `browser/ui/views/space_create/maho_space_create_dialog.h` | 3 | REWRITE | WebView host; add `CloseActiveDialog()`; remove native view refs |
| 9 | `browser/ui/views/space_create/maho_space_create_dialog.cc` | 3 | REWRITE | `DialogDelegate` + `WebView` loading `chrome://maho-space-create/` |
| 10 | `browser/BUILD.gn` | 3 | MODIFY | Add dep on `space_create_dialog` (~line 194) |
| 11 | `browser/ui/views/sidebar/maho_sidebar_footer_view.cc` | 4 | MODIFY | kNewSpace → `MahoSpaceCreateDialog::Open(browser_)` |
| 12 | `browser/ui/views/command/maho_command_action_handler.cc` | 4 | MODIFY | new_space → `MahoSpaceCreateDialog::Open(browser)` |
| 13 | `browser/ui/views/sidebar/BUILD.gn` | 4, 5, 8 | MODIFY | Add dialog dep; remove create_space_view sources; update test deps |
| 14 | `browser/ui/views/command/BUILD.gn` | 4 | MODIFY | Add dialog dep |
| 15 | `browser/ui/views/sidebar/maho_sidebar_view.h` | 5 | MODIFY | Remove kCreateSpace, ShowCreateSpaceSurface, create_space_view_ |
| 16 | `browser/ui/views/sidebar/maho_sidebar_view.cc` | 5 | MODIFY | Remove all kCreateSpace branches + construction + focus routing |
| 17 | `browser/ui/views/sidebar/maho_sidebar_footer_view.h` | 5 | MODIFY | Remove dead `open_create_space_after_popover_close_` |
| 18 | `browser/ui/views/space_config/maho_space_config_dialog.h` | 6 | MODIFY | Remove kCreate, OpenForCreate, InvokeCreateModeThemeChosenCallback |
| 19 | `browser/ui/views/space_config/maho_space_config_dialog.cc` | 6 | MODIFY | Remove create-mode paths, callback mechanism, kDialogHeightCreate |
| 20 | `browser/ui/webui/maho_space_config/maho_space_config.mojom` | 6 | MODIFY | Remove `ThemeChosen` from Page |
| 21 | `browser/ui/webui/maho_space_config/maho_space_config_page_handler.h` | 6 | MODIFY | Remove is_create_mode_, draft_theme_json_ |
| 22 | `browser/ui/webui/maho_space_config/maho_space_config_page_handler.cc` | 6 | MODIFY | Remove is_create_mode_ branches in GetSpaceInfo/UpdateTheme/PreviewTheme |
| 23 | `browser/ui/webui/maho_space_config/maho_space_config_ui.h` | 6 | MODIFY | Remove is_create_mode_, draft_theme_json_ |
| 24 | `browser/ui/webui/maho_space_config/maho_space_config_ui.cc` | 6 | MODIFY | Remove create-mode URL parsing + handler constructor params |
| 25 | `browser/resources/maho_space_config/react/store.ts` | 6 | MODIFY | Remove all isCreateMode branches (~20 refs) |
| 26 | `browser/resources/maho_space_config/react/app.tsx` | 6 | MODIFY | Remove create-mode UI block + class toggling |
| 27 | `browser/resources/maho_space_config/space_config.css` | 6 | MODIFY | Remove ~108 scfg-create* CSS rules |
| 28 | `browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc` | 5, 8 | MODIFY | Delete 7 tests (Phase 5); migrate brightness stepper (Phase 8) |

### Files Deleted (2 files)

| # | Path | Phase | Reason |
|---|------|-------|--------|
| 1 | `browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` | 5 | Native form — replaced by GT-aligned WebUI (2150 lines) |
| 2 | `browser/ui/views/sidebar/maho_sidebar_create_space_view.h` | 5 | Native form header (111 lines) |

### Files Added (3 files)

| # | Path | Phase | Reason |
|---|------|-------|--------|
| 1 | `browser/ui/views/space_create/BUILD.gn` | 3 | Build target for rebound dialog |
| 2 | `browser/ui/webui/maho_space_create/maho_space_create_browsertest.cc` | 8 | New test coverage: handler tests + E2E + GT DOM assertions |
| 3 | (test case added to existing `maho_command_overlay_interactive_uitest.cc`) | 8 | Command action → dialog open test |

### Auto-Regenerated (no manual edit)

| File | Trigger |
|------|---------|
| `browser/resources/maho_space_create/maho_space_create.mojom-webui.js.d.ts` | Mojom change in Phase 1 |
| `browser/resources/maho_space_config/maho_space_config.mojom-webui.js.d.ts` | Mojom change in Phase 6 |

---

## 14. Verification Matrix (Summary)

| # | Tool/Command | Pass Criterion | Phase Gate |
|---|-------------|----------------|------------|
| V1 | `bun run typecheck` | Zero errors | 1, 2, 6, 9 |
| V2 | `autoninja -C out/Default chrome` | Zero errors | 1-9 (every phase) |
| V3 | `bun run test` | All Rust tests pass | 9 |
| V4 | Run `interactive_tests` | All remaining tests pass | 5, 8, 9 |
| V5 | Run `maho_space_create:browser_tests` | All new tests pass (incl. GT DOM assertions) | 8, 9 |
| V6 | `lsp_diagnostics` on modified files | Zero errors | 1-9 (every phase) |
| V7 | Dead code grep (see Phase 9 patterns) | Zero matches | 9 |
| V8 | Manual QA: footer → dialog | GT-aligned dialog opens | 4, 9 |
| V9 | Manual QA: command → dialog | GT-aligned dialog opens | 4, 9 |
| V10 | Manual QA: create E2E | Full flow works | 7, 9 |
| V11 | Manual QA: error path | Error shown | 2, 9 |
| V12 | Manual QA: keyboard | Enter/Escape work | 2, 9 |
| V13 | Manual QA: live preview | Chrome updates behind dialog | 1, 9 |
| V14 | Manual QA: preview revert | Theme reverts on close | 1, 9 |
| V15 | Manual QA: brightness bounds | Buttons disable | 2, 9 |
| V16 | Manual QA: swatch scroll | Auto-scrolls | 2, 9 |
| V17 | Manual QA: config edit-mode | Works, no create remnants | 6, 9 |
| V18 | FD-3: theme ordering | Confirm `update_space_config` safe on non-active space | 7, 9 |
| V19 | Visual Diff Gate: per-element | Every GT element matches Image1 bbox/screenshot (see Phase 9 §12.2) | 9 (eligible — Image1 binary present) |
| V20 | Visual Diff Gate: full-dialog | SSIM > 0.85 vs Image1 at 456×600 | 9 (eligible — Image1 binary present) |

---

## 15. Out-of-Scope / Deferred Items

| Item | Reason |
|------|--------|
| Preserving old WebUI copy/visuals that conflict with GT (Image1 wins) | GT is the binding spec. Old WebUI elements that differ are overwritten, not preserved. |
| shadcn / Tailwind / cva / clsx / cn.ts | Explicitly prohibited by requirements |
| `is_create_mode_` retention in any form | Explicitly prohibited — full removal |
| Second-surface migration | Not part of this plan; only Space New affected |
| KP-9: theme picker keyboard accelerators (deferred) | UX design decision pending; native had VKEY_RETURN/ESCAPE in picker, WebUI inline editor has no "Done" step — different interaction model |
| KP-4: (not in evidence) | No KP-4 defined in evidence — intentional gap |
| Space config WebUI cleanup beyond is_create_mode_ | Only create-mode code removed; edit-mode code untouched |
| `open_create_space_after_popover_close_` deep audit | Dead field removed; no further audit of footer dead code |
| Visual Diff Gate completion without Image1 binary | No longer applicable — binary present at `.sisyphus/plans/assets/theme-parity-image1.png`. Gate is eligible to run in Phase 9. |

---

## 16. Atomic Commit Strategy

One commit per phase. Every commit compiles and passes tests.

| Commit # | Phase | Git Message | Compiles | Tests Pass |
|----------|-------|-------------|----------|------------|
| 0 | Phase 0 | `chore(space-create): confirm Image1 GT placement` | N/A | N/A |
| 1 | Phase 1 | `feat(space-create): add SetThemePreview Mojo path for live theme preview (KP-1)` | Yes | Yes |
| 2 | Phase 2 | `feat(space-create): align WebUI to GT Image1 element-by-element + port native behaviors (KP-3,5,6,7,D1-D8,KP-10,11)` | Yes | Yes |
| 3 | Phase 3 | `refactor(space-create): rebind MahoSpaceCreateDialog as WebView host` | Yes | Yes |
| 4 | Phase 4 | `refactor(space-create): repoint entry points to MahoSpaceCreateDialog::Open` | Yes | Yes |
| 5 | Phase 5 | `refactor(sidebar): delete native create-space view, kCreateSpace mode, and 7 dependent tests` | Yes | Yes |
| 6 | Phase 6 | `refactor(space-config): remove is_create_mode_ machinery entirely` | Yes | Yes |
| 7 | Phase 7 | `fix(space-create): FFI reconciliation — SwitchToSpace, drop maho_core_activate_space` | Yes | Yes |
| 8 | Phase 8 | `test(space-create): add replacement browser tests, GT DOM assertions, and new handler coverage` | Yes | Yes |
| 9 | Phase 9 | No commit — verification, manual QA, and Visual Diff Gate only | N/A | N/A |

**Critical ordering note**: Phase 5 must inline-delete the 7 DELETE-category tests to maintain compilability, since those tests directly reference `MahoSidebarCreateSpaceView` and `kCreateSpace` which are being deleted in the same commit.

---

## 17. Task Dependency Graph / Parallel Execution Plan

### Dependency Chain (strictly sequential at the commit level)

```
Phase 0 ─── Image1 placement confirmation (no code changes)
  │
  ▼
Phase 1 ─── SetThemePreview Mojo (mojom + handler + store)
  │          Modifies: mojom, handler .h/.cc, store.ts
  │
  ▼
Phase 2 ─── GT-aligned WebUI port (app.tsx + store.ts + CSS + KP-*)
  │          Depends on Phase 1: store.ts changes must not conflict
  │          GT Image1 is binding spec for all visual/text changes
  │
  ▼
Phase 3 ─── Rebind dialog (dialog .h/.cc rewrite + BUILD.gn)
  │          Depends on Phase 2: WebUI must be GT-aligned before hosting
  │
  ▼
Phase 4 ─── Repoint entry points (footer + command handler)
  │          Depends on Phase 3: dialog target must exist
  │
  ▼
Phase 5 ─── Delete native form + 7 tests
  │          Depends on Phase 4: all callers must be repointed first
  │
  ▼
Phase 6 ─── Remove is_create_mode_ from space_config
  │          Depends on Phase 5: native callers of OpenForCreate deleted
  │
  ▼
Phase 7 ─── FFI reconciliation
  │          Depends on Phase 3: CloseDialog → CloseActiveDialog needs dialog
  │          Could theoretically run after Phase 3, but ordered after Phase 6 for safety
  │
  ▼
Phase 8 ─── Test cutover (including GT DOM assertion tests)
  │          Depends on Phases 5, 6, 7: all code changes landed
  │
  ▼
Phase 9 ─── Verification, manual QA & Visual Diff Gate
             Depends on Phase 8: all tests must exist
             Visual Diff Gate eligible — Image1 binary on disk
```

### Within-Phase Parallelization

| Phase | Parallel Work Opportunities |
|-------|---------------------------|
| 1 | Mojom + handler changes must precede store.ts (Mojo types needed). But handler .h and .cc can be edited in parallel. |
| 2 | All GT-delta items (D1-D8) and KP-* items edit different sections of `app.tsx`, `store.ts`, `space_create.css` — can be developed in parallel within a branch, committed together. |
| 3 | Dialog .h, .cc, and BUILD.gn are independent. Browser BUILD.gn depends on the BUILD.gn existing. |
| 4 | Footer and command handler edits are independent files. BUILD.gn edits are independent. |
| 5 | Sidebar view .h/.cc must be consistent. Test deletions from the same file must be done together. |
| 6 | C++ changes (dialog, handler, UI) and TS/CSS changes (store, app, CSS) have no file-level overlap — can be developed in parallel. |
| 7 | All changes are in the same file (page_handler.cc) — sequential within the file. |
| 8 | New test file and existing test modifications are in different files — can be developed in parallel. |

### Cross-Phase Parallelization (Advanced — NOT Recommended for Safety)

Phases 6 and 7 share no files and could theoretically execute in parallel after Phase 5. However, the sequential ordering is clearer for code review and bisectability. **Recommendation: execute strictly sequentially.**

---

*Plan authored from binding GT Image1 textual enumeration + prior Oracle decisions. All file paths and line ranges should be re-verified at execution time against the live codebase, since some line numbers are approximations from the audit phase. The Visual Diff Gate in Phase 9 cannot pass until the user places the binary at `.sisyphus/plans/assets/theme-parity-image1.png`; the textual GT spec above is otherwise binding for all implementation work.*
