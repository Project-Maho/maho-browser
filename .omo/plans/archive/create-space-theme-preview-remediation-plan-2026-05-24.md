# Create-Space "Choose a Theme" Preview Remediation Plan

**Created:** 2026-05-24
**Status:** Draft, analysis only. No code changes implied yet.
**Primary scope:**
- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`
- `maho-chromium/browser/ui/theme/maho_space_theme_state.cc`
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
- The browser's main content area view (the consumer that should react to `kMahoColorContentBackground`)

**Secondary scope (tuning, not blocker):**
- Sidebar tint blend factor inside `maho_color_mixer.cc`

---

## 1. Context

The new space "Choose a Theme" dialog (the WebUI hosted at `chrome://maho-space-create`, embedded in `MahoSpaceThemePickerDialog`) lets the user pick a theme mode, swatch, brightness, and so on. The intended runtime behavior is:

1. Each interactive change in the picker emits a debounced `previewTheme(theme_json)` Mojo call.
2. The browser-side handler installs the preview into the live theme state.
3. All Maho-themed surfaces (sidebar, main content area, gradient, overlays) repaint to reflect the previewed theme.
4. `Done` commits the previewed theme as persistent state. `Cancel` clears the preview and reverts the surfaces.

What is currently observed:

- Edits inside the picker do not visibly change the **main content area**.
- Edits inside the picker do not visibly change the **sidebar** in a clearly perceptible way.
- After `Done` or `Cancel`, the surfaces do not always match the new persisted state immediately.

This plan documents why, based on a static read of the existing code, and proposes a remediation sequenced by impact.

---

## 2. Confirmed Findings (Static Evidence)

These findings are confirmed by reading the current source. No runtime instrumentation was added.

### 2.1 Preview path is wired on the browser side

`MahoSpaceCreatePageHandler::PreviewTheme` calls into the live theme state and triggers a repaint:

- `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc`
  - `PreviewTheme(...)` → `MahoSpaceThemeState::SetPreviewOverride(theme_json)` → `RepaintBrowserAfterThemeStateChange(browser_)`
  - `RepaintBrowserAfterThemeStateChange(...)` calls `RootView::ThemeChanged()` and `Widget::NotifyColorProviderChanged()` on the browser window.

So the preview path itself is structurally connected: edits do reach the theme state, and a repaint is requested on the browser window.

### 2.2 Sidebar is a real consumer of the previewed theme

`MahoSidebarView::OnThemeChanged()` reads `kMahoColorSidebarBackground` from the color provider and applies it as the sidebar rail background:

- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc::OnThemeChanged`
  - Pulls `kMahoColorSidebarBackground` from `GetColorProvider()` and applies it via the rail background.

That color id is computed in `maho_color_mixer.cc` and is tinted by the previewed theme:

- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
  - Default mapping at the early mixer block sets `kMahoColorSidebarBackground` and `kMahoColorContentBackground` to mode-aware defaults.
  - Later, the tint mixer block recomputes `kMahoColorSidebarBackground` via `maho_theme::BlendColors(... GetSidebarBlendForKind(theme_data->kind) * opacity_bias + grain_bias)`.

Conclusion: when a preview is installed and the color provider cache is reset, the sidebar **will** receive a new color from the mixer. So the sidebar consumer wiring is correct, even if the visible delta is subtle.

### 2.3 Main content area is not a confirmed consumer of `kMahoColorContentBackground`

The mixer also recomputes `kMahoColorContentBackground` based on the previewed theme, including the gradient stops:

- `maho-chromium/browser/ui/theme/maho_color_mixer.cc`
  - `tint_mixer[kMahoColorContentBackground] = { maho_theme::BlendColors(...) }`
  - Gradient stops `base_gradient_top` / `base_gradient_bottom` are blended with `primary_tint` / `secondary_tint`.

However, on a static search, the only place `kMahoColorContentBackground` is consumed in the browser views layer is:

- `maho-chromium/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.cc`
  - `color_provider->GetColor(kMahoColorContentBackground)` is read once and combined with an alpha to render the spaces overlay.

The `MahoSpacesOverlayView` is not the main content area. It is the overlay surface used when the spaces panel is open. So the recomputed `kMahoColorContentBackground` (and the recomputed gradient stops) currently has **no visible main-content consumer** that repaints on theme change.

This is the primary reason "edits do not apply to the main content area": the color is being computed but nobody draws the main content with it.

### 2.4 Gradient and zen themes lose grain on the parse path

`MahoSpaceThemeState` parses the theme JSON. For the gradient and zen branches, the grain field is dropped to zero:

- `maho-chromium/browser/ui/theme/maho_space_theme_state.cc`
  - The "gradient" / "zen" parse paths build the runtime theme record with `grain = 0.0f` rather than threading the parsed grain value.
  - Other branches read grain from `color_dict->FindDouble("grain")`.

Because `maho_color_mixer.cc` uses `theme_data->grain` to compute `grain_bias = theme_data->grain * 0.04f` and feeds that into the sidebar blend factor, gradient/zen themes always preview with zero grain bias. That alone does not block the preview from taking effect, but it does mean grain edits on gradient/zen themes are silently dropped, which makes the preview look even less responsive than it should.

### 2.5 Commit and Cancel do not request a full repaint

`MahoSpaceCreatePageHandler::CommitTheme` and `MahoSpaceCreatePageHandler::CancelTheme` both clear the preview override:

- `MahoSpaceCreatePageHandler::CommitTheme(...)` → `MahoSpaceThemeState::ClearPreviewOverride()`
- `MahoSpaceCreatePageHandler::CancelTheme()` → `MahoSpaceThemeState::ClearPreviewOverride()`

Neither one calls `RepaintBrowserAfterThemeStateChange(browser_)` (which is the helper that issues `RootView::ThemeChanged()` + `Widget::NotifyColorProviderChanged()`). `PreviewTheme` does call the helper. `CommitTheme` and `CancelTheme` do not.

Net effect on commit/cancel:

- Cancel: the preview is dropped from the cache, but the browser window is not asked to re-resolve colors. Surfaces may keep showing stale preview-derived colors until something else triggers a theme change.
- Commit: the preview is dropped, but the new persistent theme state is not pushed into the visible surfaces in the same call. The next external theme event eventually catches up, but there is a visible "snap-back" or stall window.

This is the primary reason the dialog feels like commit/cancel "does not apply" cleanly to the surfaces.

---

## 3. Root-Cause Split

The user-visible bug "edits in the new space Choose a theme dialog do not apply to the sidebar and main content area" is not a single defect. It is at least three independent issues that compound. Splitting them is required so they can be fixed and verified separately.

### 3.1 Main content area: missing consumer

- `kMahoColorContentBackground` is computed and tinted on every preview.
- The only confirmed in-tree consumer is `MahoSpacesOverlayView`, which is not the main content area.
- No other view or paint path was found that reads `kMahoColorContentBackground` and draws the main content background or gradient using it.

So edits reach the color provider but never reach pixels in the main content area. This is the primary blocker for "main content does not change".

### 3.2 Sidebar: real but visually subtle

- `MahoSidebarView::OnThemeChanged` consumes `kMahoColorSidebarBackground`.
- `maho_color_mixer.cc` tints `kMahoColorSidebarBackground` via `BlendColors(... GetSidebarBlendForKind(theme_data->kind) * opacity_bias + grain_bias)`.
- The blend factor is small for some kinds, and `grain_bias` is very small (`grain * 0.04f`), and gradient/zen themes report `grain = 0.0f` (see 2.4). So the sidebar color delta between two theme picks is often tiny.

So the sidebar is wired correctly but the visible delta is small. This is **not** a primary blocker. It is a tuning / perception issue and is treated as a secondary track.

### 3.3 Commit and Cancel: missing repaint propagation

- `CommitTheme` and `CancelTheme` clear the preview override but never call `RepaintBrowserAfterThemeStateChange(browser_)` (or any equivalent of `ThemeChanged()` / `NotifyColorProviderChanged()`).
- `PreviewTheme` does call the helper, so previews look reactive while the dialog is open.
- After commit or cancel, the browser window is not actively asked to re-resolve colors against the new persistent state.

So the dialog feels "applied while moving sliders" but the closing handshake leaves the surfaces in a stale state until something else (focus change, window event, theme controller event) triggers a refresh.

---

## 4. Exact File and Function References

| Concern | File | Symbol |
|---|---|---|
| Preview entry from picker | `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc` | `MahoSpaceCreatePageHandler::PreviewTheme` |
| Repaint helper | `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc` | file-local `RepaintBrowserAfterThemeStateChange(Browser*)` |
| Commit | `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc` | `MahoSpaceCreatePageHandler::CommitTheme` |
| Cancel | `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.cc` | `MahoSpaceCreatePageHandler::CancelTheme` |
| Live theme state | `maho-chromium/browser/ui/theme/maho_space_theme_state.h/.cc` | `MahoSpaceThemeState::SetPreviewOverride`, `ClearPreviewOverride`, gradient/zen parse path |
| Color computation | `maho-chromium/browser/ui/theme/maho_color_mixer.cc` | mixer block that writes `kMahoColorSidebarBackground` and `kMahoColorContentBackground`, gradient stop blend block |
| Sidebar consumer | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc` | `MahoSidebarView::OnThemeChanged` |
| Spaces overlay consumer (not main content) | `maho-chromium/browser/ui/views/spaces_overlay/maho_spaces_overlay_view.cc` | usage of `color_provider->GetColor(kMahoColorContentBackground)` |
| Color id declarations | `maho-chromium/browser/ui/theme/maho_color_id.h` | `kMahoColorContentBackground`, `kMahoColorSidebarBackground` |

The intended-but-missing main-content consumer is described in the remediation phases below.

---

## 5. Remediation Plan (Phased)

This plan addresses the three root causes from Section 3 in priority order. Sidebar tuning is intentionally last because it is not a blocker.

### Phase 1 — Main content consumer wiring (PRIMARY)

**Goal:** Make the main content area actually consume `kMahoColorContentBackground` and the recomputed gradient stops, so previews are visible there.

**Required steps:**

1. Identify the view that paints the main content area background today (the surface that hosts the active web contents area in the Maho window). Confirm whether it currently reads any Maho-themed color id, or whether it falls back to a Chromium-default color.
2. Add a consumer path on that view:
   - Override `OnThemeChanged()` (or the equivalent paint hook) to read `kMahoColorContentBackground` from `GetColorProvider()`.
   - Apply that color as the background fill, layered behind the active web contents view.
3. If gradient rendering is desired (the mixer already prepares blended top and bottom gradient stops), expose a small accessor on `MahoSpaceThemeState` (or a helper next to the mixer) so that the main content view can read the two gradient stops, then paint a vertical gradient using those stops. If the gradient stops are not yet exposed via a public API, add a minimal getter that returns the same blended stops the mixer is already computing.
4. Confirm that the color id is updated whenever `RepaintBrowserAfterThemeStateChange` runs (`RootView::ThemeChanged()` + `Widget::NotifyColorProviderChanged()` will reach the new view as long as it is in the widget tree and overrides `OnThemeChanged`).

**Required invariant after Phase 1:**

- Moving any swatch / brightness / mode control in the picker visibly changes the main content area background within the debounce window.
- The change is consistent across light and dark mode.
- Removing the preview (Cancel after Phase 3) returns the main content background to the persisted theme.

### Phase 2 — Gradient and zen grain restoration

**Goal:** Stop dropping `grain` to zero on the gradient and zen parse paths so previews of those themes carry the same grain expression as other themes.

**Required steps:**

1. In `maho_space_theme_state.cc`, locate the gradient and zen branches that currently build their runtime record with `grain = 0.0f`.
2. Read grain from the same source the non-gradient/zen branch uses (the `color_dict->FindDouble("grain")` path), apply the same clamping, and store the actual value into the runtime record.
3. Keep the rest of the parse contract identical. Do not change the field shape, the kind enum, or any unrelated default.

**Required invariant after Phase 2:**

- Gradient and zen themes carry a non-zero grain value into `maho_color_mixer.cc::grain_bias` when the theme JSON sets one.
- Themes that explicitly set `grain = 0` still parse to zero.
- No behavior change for non-gradient/zen themes.

### Phase 3 — Repaint propagation on commit and cancel

**Goal:** Make the browser window re-resolve colors at the moment the dialog commits or cancels, so the surfaces match the new resolved state immediately.

**Required steps:**

1. In `maho_space_create_page_handler.cc`, after `MahoSpaceThemeState::ClearPreviewOverride()` inside `CommitTheme`, call the same `RepaintBrowserAfterThemeStateChange(browser_)` helper that `PreviewTheme` already uses.
2. Do the same in `CancelTheme` after `ClearPreviewOverride`.
3. Confirm both helpers are still called on the UI thread context the rest of this file already assumes.
4. Keep the structure of `CommitTheme` and `CancelTheme` otherwise unchanged. They should remain thin shims that adjust the live theme state and request a repaint.

**Required invariant after Phase 3:**

- After `Done`, the visible state of the sidebar, content background, and gradient matches the just-committed theme without waiting for an external trigger.
- After `Cancel`, the visible state reverts to the pre-dialog persistent theme without waiting for an external trigger.

### Phase 4 — Sidebar visibility tuning (SECONDARY, NOT A BLOCKER)

**Goal:** Once Phases 1, 2, and 3 are done, decide whether the sidebar still feels visually under-responsive, and tune the blend factors if needed.

**Required steps:**

1. With Phases 1–3 in place, manually inspect whether the sidebar shows a clearly perceptible difference between two adjacent swatches.
2. If the delta is still imperceptible, evaluate two narrow knobs in `maho_color_mixer.cc`:
   - The blend factor returned by `GetSidebarBlendForKind(theme_data->kind)`.
   - The grain bias multiplier in `grain_bias = theme_data->grain * 0.04f`.
3. Adjust only one knob at a time. Keep the change minimal and inside `maho_color_mixer.cc`.
4. Do not change `maho_color_id.h`, the parse layer, the consumer wiring, or any handler.

**Required invariant after Phase 4:**

- The sidebar reflects theme picks visibly enough that a user can tell two adjacent picks apart.
- No regression to non-Maho UI surfaces. The blend change must remain scoped to the Maho color ids.

---

## 6. Risks

1. **Wrong main-content consumer.** If the chosen main-content view is not the actual paint owner (for example, if the active web contents area itself paints over the background), the new consumer wiring will compute the right color but the user will still not see it. Mitigation: confirm the paint owner before wiring; verify by toggling an obvious color manually before committing the real wiring.
2. **Gradient stop exposure.** If the mixer-side gradient stops are file-local and not exposed via a public API today, naively exposing them risks leaking internal helpers. Mitigation: add the smallest possible accessor on `MahoSpaceThemeState` or a sibling helper, returning only the two stop colors the main content view needs.
3. **Grain clamping divergence.** The non-gradient/zen branch already clamps grain. Restoring grain on the gradient/zen branches must use the same clamp. Mitigation: factor the clamp into a tiny local helper if needed; do not duplicate inline math.
4. **Repaint loops on commit/cancel.** Calling `RepaintBrowserAfterThemeStateChange` during commit/cancel must not re-enter the picker or the page handler. Mitigation: confirm by code reading that `RootView::ThemeChanged()` + `Widget::NotifyColorProviderChanged()` does not transitively reopen or remount the dialog WebUI.
5. **Sidebar tuning regressions.** Adjusting blend factors can affect kinds we did not intend to touch. Mitigation: keep Phase 4 strictly secondary; do not bundle it into the same change as Phase 1.
6. **Subtle flash on Cancel.** If commit/cancel propagation lands without main-content wiring, Cancel will flash the old persistent state into a still-broken main content area. Mitigation: prefer landing Phase 1 before Phase 3, or land them together.

---

## 7. Verification Checklist

Static checks (can run without a Chromium build):

- `lsp_diagnostics` clean on every file touched in Phases 1–4.
- `kMahoColorContentBackground` has at least one main-content consumer site beyond `MahoSpacesOverlayView`.
- `MahoSpaceCreatePageHandler::CommitTheme` and `MahoSpaceCreatePageHandler::CancelTheme` both invoke `RepaintBrowserAfterThemeStateChange(browser_)` after `ClearPreviewOverride`.
- The gradient and zen parse branches in `maho_space_theme_state.cc` no longer hard-code `grain = 0.0f`.
- `kMahoColorContentBackground` and `kMahoColorSidebarBackground` declarations in `maho_color_id.h` are unchanged unless a documented reason exists.
- No edits outside Maho-owned code. No upstream Chromium files modified.

Runtime checks (only when a Chromium build is run; this plan itself does not run a build):

- Open `New Space → Choose a Theme` from the sidebar footer.
- Click multiple mode chips, swatches, brightness adjustments. The main content background must visibly change after the debounce window each time.
- Click `Done`. Surfaces remain at the chosen theme without a stale flash.
- Reopen the picker, click `Cancel`. Surfaces revert to the previously persistent theme without a stale flash.
- Pick a gradient or zen theme that defines a non-zero grain in JSON. The resulting sidebar tint must not be identical to the same theme with grain forced to zero.

---

## 8. Out of Scope

The following are explicitly out of scope for this plan:

1. Migrating `chrome://maho-space-create` away from WebUI. AGENTS forbids this direction. The picker stays where it is.
2. Any change to upstream Chromium code in `chromium/src/` outside the existing override patterns.
3. Any GN file edit. Build wiring is unaffected by this plan.
4. The Mojo contract between the picker UI and the page handler (`PreviewTheme`, `CommitTheme`, `CancelTheme` shapes are unchanged).
5. The UI layout of the picker dialog itself. This plan addresses the runtime application of picker output, not the picker chrome.
6. The persistence layer that stores the committed theme. This plan assumes commit already persists correctly.
7. Tests. This plan does not specify test additions.
8. `mail/`. Out of scope.

---

## 9. Recommended Execution Order

1. Phase 1 (main content consumer) lands first because it is the primary user-visible blocker.
2. Phase 3 (commit/cancel repaint) lands next, ideally in the same change set as Phase 1 to avoid commit/cancel exposing a half-wired content area.
3. Phase 2 (grain restoration) lands as a small follow-up. It is independent and safe.
4. Phase 4 (sidebar tuning) is evaluated only after Phases 1–3 are visibly working. Skip if no longer needed.
