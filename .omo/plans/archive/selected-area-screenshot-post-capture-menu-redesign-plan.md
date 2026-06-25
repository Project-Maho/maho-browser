# Selected-Area Screenshot Post-Capture Menu Redesign

**Status:** Ready for execution
**Surface:** `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.{h,cc}` (post-capture bubble) + `maho_dom_screenshot_handler.{h,cc}` (capture lifecycle)
**Mode:** Reference-driven UI redesign + real behavior wiring for the actions implied by the reference, minus `New Easel` and `Javascript`
**Verification:** Provider/dialog unit tests + canonical `python3 maho-chromium/build/scripts/build_maho.py` + manual QA

---

## 1. Overview

The user's reference is a dark, vertically stacked action menu attached to the just-captured selection. It exposes (top to bottom): a header `Send To...` affordance, then the item rows `Copy`, `Save...`, `Retake`, `Edit`, `Send via iMessage...`, `Save to Library`. The reference also shows `New Easel` and `Javascript`, which the user has explicitly removed from scope. The current Maho post-capture surface is a small horizontal bubble with a `Copy` text button and a prominent `Download` button. This plan does two things at once:

1. Rebuilds the bubble to match the reference layout in the dark, vertical, anchored-to-selection style.
2. Wires the new rows to real behavior where existing Maho/Chromium plumbing is reachable, and explicitly flags the rows that have no reachable plumbing today as new integration work with open scope.

The reference's two removed rows (`New Easel`, `Javascript`) do not need code paths and are simply absent from the new menu model.

---

## 2. Scope

### Must ship

- Visual redesign of the post-capture bubble to match the reference: dark surface, rounded container, vertically stacked rows with leading SF-symbol-style icons and trailing labels, the `Send To...` header affordance at the top of the list, separator above the destructive-ish `Save to Library` if the reference shows one, and bubble anchoring preserved against the selection rect.
- A typed action model for the bubble's rows so the existing `Copy` / current `Download` callbacks compose with the new rows without re-architecting the handler.
- `Copy` row: keep current behavior (writes the bitmap to the system clipboard via `ui::ScopedClipboardWriter::WriteImage`).
- `Save...` row: maps to the existing download path. Reuses the bitmap to data URL plus `DownloadManager::DownloadUrl` flow already in `MahoDomScreenshotHandler::OnDownloadClicked()`. Label changes from `Download` to `Save...` to match the reference; the underlying flow is unchanged.
- `Retake` row: dismisses the current bubble and re-enters the selection capture flow by re-invoking `MahoDomScreenshotHandler::TriggerCapture(web_contents)` against the same `WebContents`. The existing `Finish()` teardown plus a fresh handler instance is the natural shape since the handler self-deletes on bubble dismissal.
- `Send To...` top affordance: wired through `sharing_hub::SharingHubBubbleController::CreateOrGetFromWebContents(...)`, the same pattern already used by the utility panel's `BuildShareAction` in `maho_location_bar_utility_panel_provider.cc`. The Sharing Hub does not natively accept an arbitrary in-memory image as its payload, so the wiring routes the share request through a temporary on-disk file (or a `data:` URL) referenced by the standard share intent. If routing the captured bitmap into the existing Sharing Hub surface is not reachable in this phase, ship the row behind a feature flag and fall back to a disabled stub for users.

### Must ship as new integration (open scope, may slip)

- `Edit` row: the current Maho `chrome://maho-inline-edit` surface is text-oriented (rewriting selected page text via the LLM). It is not an image editor and cannot be reused as-is for a screenshot bitmap. Upstream Chromium has `chrome/browser/image_editor/screenshot_flow.h` (`image_editor::ScreenshotFlow`) which carries selection plus postprocessing/editing semantics, but the current Maho DOM screenshot path bypasses it entirely and goes through CDP `Page.captureScreenshot`. Wiring `Edit` therefore requires either:
  - (a) introducing a thin Maho image-editor surface (likely a new view that hosts the captured bitmap with crop/annotate/markup), or
  - (b) routing through `image_editor::ScreenshotFlow`'s postprocessing entry points (which would mean either adopting the upstream flow wholesale or copying just its editor entry point), or
  - (c) shipping the row behind a feature flag and treating it as a stub for users until one of (a) or (b) lands.
  This plan takes option (c) as the conservative landing for the first commit and tracks (a) / (b) as a follow-up. No new FFI, no new Mojo, no new WebUI surface is promised at this stage.
- `Send via iMessage...` row: there is no existing macOS share-service integration anywhere in the Maho codebase. macOS exposes `NSSharingService` (`NSSharingServiceNameComposeMessage` is the iMessage entry point), and Chromium has a generic share/Sharing Hub abstraction, but neither is currently bridged from the Maho overlay to a captured bitmap. Wiring requires a small Objective-C++ shim in the macOS-touching shell layer (or a new helper inside `chrome/browser/ui/cocoa/`-style territory in the overlay) that:
  - takes the captured `gfx::Image`,
  - hands it to `NSSharingService serviceWithName:NSSharingServiceNameComposeMessage`,
  - and invokes `performWithItems:` against an `NSImage` derived from the bitmap.
  This plan ships the row behind a feature flag with the integration deferred to a follow-up. No new FFI, no new Mojo, no new WebUI surface is promised. Cross-platform: on non-macOS builds the row is hidden.
- `Save to Library` row: there is no existing screenshot-to-library code path in Maho today. There are notes/easel surfaces (`chrome://maho-notes`, `chrome://maho-easel`) but neither is plumbed to consume an arbitrary captured bitmap as a library entry. Wiring requires deciding what "library" means in product terms (likely a Maho-specific local image library backed by `maho-core` or by a directory under the user data dir), then plumbing the bitmap into it. This plan ships the row behind a feature flag with the integration deferred to a follow-up. No new FFI is promised here; the eventual implementation may want one.

### Out of scope

- A finished image editor (whether home-grown or upstream-derived) is not in this plan.
- `New Easel` and `Javascript` rows from the reference are deliberately not implemented; they are not part of the menu model.
- Region capture as a separate user flow. The existing handler already does selected-area; no second mode is introduced.
- Any new Mojo IDL.
- Any new FFI export in `maho-ffi` or `maho-core`.
- Any new WebUI surface.
- Migrating the bubble to WebUI.
- Any change to the `MahoDomScreenshotHandler` capture pipeline (CDP `Page.captureScreenshot` plus polling) beyond what is required to add `Retake` and to forward the captured bitmap to the new rows.

---

## 3. Grounded Current State

Files in scope (all exist on disk; symbols verified):

- `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_handler.h`
- `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_handler.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.h`
- `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view_unittest.cc`
- `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_script.js` (overlay script; not edited by this plan)

Verified current shape:

- `MahoDomScreenshotHandler::TriggerCapture(content::WebContents*)` is the immediate-capture entry point. It self-deletes on bubble dismissal, capture failure, or `WebContents` teardown.
- The capture pipeline is CDP-based: it injects an isolated-world overlay script, polls for a selected rect, then dispatches `Page.captureScreenshot` via `DevToolsAgentHostClient`.
- After capture, `ShowResultBubble(const gfx::Rect& anchor_rect_screen)` constructs `MahoDomScreenshotActionDialogView` via its static `Show(...)`, anchored against the selection rect.
- `MahoDomScreenshotActionDialogView` today carries exactly two `views::MdTextButton`s: `Copy` (kText style) and `Download` (kProminent style), in a horizontal `BoxLayout`. The header label is `Selected area captured` and the subtitle is `Copy or download this screenshot.`.
- `OnCopyClicked()` writes the captured bitmap to the system clipboard via `ui::ScopedClipboardWriter::WriteImage(bitmap)` and finishes.
- `OnDownloadClicked()` serializes the bitmap to a `data:` URL via `webui::GetBitmapDataUrl(bitmap)` and dispatches it through `DownloadManager::DownloadUrl(...)` with a generated filename. This is the path the new `Save...` row reuses.
- `MahoDomScreenshotActionDialogView::Show(...)` returns a `views::Widget*` that the handler keeps as `result_bubble_widget_` so it can be closed programmatically.
- `MahoDomScreenshotActionDialogView` already implements `views::WidgetObserver` and runs the dismiss callback exactly once (`action_invoked_` flag) when the user activates a button, presses Escape, or the widget closes.

Adjacent plumbing the new rows touch:

- `sharing_hub::SharingHubBubbleController::CreateOrGetFromWebContents(...)` is already used by `maho_location_bar_utility_panel_provider.cc`'s `ShowSharingHubForBrowser`. This is the reachable share entry point for `Send To...`, even though it does not natively consume an in-memory bitmap as its payload.
- `chrome/browser/image_editor/screenshot_flow.h` (`image_editor::ScreenshotFlow`) exists upstream. It owns selection UI plus postprocessing/editing semantics (region select, copy/save/edit modes). The current Maho DOM screenshot path does not use it; it goes straight to CDP `Page.captureScreenshot` instead. `Edit` would need either to adopt this flow's editor surface or to introduce a Maho image-editor surface.
- No macOS share-service (`NSSharingService`) integration was found anywhere in the Maho codebase; nothing routes a `gfx::Image` to `NSSharingServiceNameComposeMessage` today.
- No screenshot-to-library code path was found. `chrome://maho-notes` and `chrome://maho-easel` exist as WebUI surfaces but neither consumes an arbitrary captured bitmap as a library entry.
- `chrome://maho-inline-edit` is text-oriented (LLM rewrite of selected page text), not an image editor; it cannot be reused for a screenshot bitmap.

What this means for scope:

| Reference row | Already exists? | Reachable plumbing today? | Plan |
|---|---|---|---|
| `Send To...` (top) | No | Yes, via `SharingHubBubbleController` | Wire end to end; gate behind flag if bitmap-to-share-payload routing is not reachable in this phase |
| `Copy` | Yes | Yes (clipboard write) | Keep behavior; relabel/restyle for the new menu |
| `Save...` | Maps to existing | Yes (download path) | Reuse `OnDownloadClicked()`; relabel from `Download` to `Save...` |
| `Retake` | Maps to existing | Yes (re-trigger capture) | Dismiss bubble, re-invoke `TriggerCapture(web_contents)` |
| `Edit` | No | No (inline-edit is text-oriented; `image_editor::ScreenshotFlow` exists upstream but Maho bypasses it) | New work; ship behind flag with disabled-stub UI for users |
| `Send via iMessage...` | No | No (no macOS share-service integration found) | New work; ship behind flag with disabled-stub UI for users |
| `Save to Library` | No | No (no screenshot-to-library path found) | New work; ship behind flag with disabled-stub UI for users |
| `New Easel` | (out of scope) | (out of scope) | Not in menu |
| `Javascript` | (out of scope) | (out of scope) | Not in menu |

---

## 4. File-by-File Plan

### 4.1 `maho_dom_screenshot_action_dialog_view.h`

Replace the two `MdTextButton*` callbacks with a typed row model so the new rows compose without forcing the handler to grow N additional `base::OnceClosure` parameters in `Show(...)`:

```cpp
struct MahoDomScreenshotActionRow {
  std::u16string label;
  std::string sf_symbol_name;        // leading icon
  bool enabled = true;
  bool destructive = false;          // reserved for future "Delete"-style rows
  base::OnceClosure activate;        // invoked exactly once if enabled
};
```

`Show(...)` gains a new shape that takes a vector of rows plus an optional top header affordance and a single dismiss callback:

```cpp
struct MahoDomScreenshotActionMenu {
  std::u16string header_label;       // e.g. "Send To..."
  std::string header_sf_symbol_name; // e.g. "square.and.arrow.up"
  base::OnceClosure header_activate;
  std::vector<MahoDomScreenshotActionRow> rows;
  base::OnceClosure dismiss_callback;
};

static views::Widget* Show(gfx::NativeView parent_window,
                           const gfx::Rect& selection_rect_screen,
                           MahoDomScreenshotActionMenu menu);
```

Keep the existing two-closure `Show(...)` overload only if needed for in-flight tests; otherwise migrate the test surface to the new shape. The view stops storing raw `MdTextButton*` for `copy` / `download` and instead stores a `std::vector<raw_ptr<views::Button>> row_buttons_` plus an optional `header_button_`. `RequestFocus()` focuses the first enabled row (or the header affordance, depending on which the design pins as default).

Add `static const std::string kSendToSymbolName`, `kCopySymbolName`, `kSaveSymbolName`, `kRetakeSymbolName`, `kEditSymbolName`, `kImessageSymbolName`, `kSaveToLibrarySymbolName` constants in the .cc translation unit (not the header) for the leading icons.

### 4.2 `maho_dom_screenshot_action_dialog_view.cc`

Rebuild the layout to match the reference:

- Outer container becomes a vertical `BoxLayout` with the dark reference colors. The existing `ui::kColorSysSurfaceVariant` background switches to the dark surface tone the reference uses (likely `ui::kColorSysSurfaceContainerHigh` or a Maho-specific dark token already declared in `maho_color_id.h`; pick whichever already produces the reference's contrast in dark mode and falls back gracefully in light mode).
- The existing `Selected area captured` title and `Copy or download this screenshot.` subtitle are **removed**. The reference has no separate title/subtitle area; the menu is the entire surface.
- The first child becomes the `Send To...` header affordance: a horizontal row with leading icon, label, and a trailing chevron-style affordance, focusable, with `kButton` accessibility role. Bound to `menu.header_activate`.
- The remaining children are one row per `MahoDomScreenshotActionRow`, rendered as full-width buttons with leading SF-symbol-style icon and the label, hover/focus styled to match the reference's elevated-on-hover dark menu treatment. Disabled rows (`enabled = false`) render with reduced opacity and skip activation, matching the existing disabled treatment used by `MahoLocationBarUtilityPanelActionView`.
- A subtle separator between the `Send To...` header and the row group, and a second separator above `Save to Library` if the reference pins one (visual decision; review the reference once more during implementation and pick a single answer).
- Replace `SetPreferredSize(gfx::Size(kDialogWidth, GetPreferredSize().height()))` with a width that matches the reference; height comes from the row count.
- `AcceleratorPressed(ui::Accelerator)` keeps `VKEY_ESCAPE` as dismiss. Add up/down arrow handling to move focus through the rows.
- `OnCopyPressed` / `OnDownloadPressed` are deleted; activation is per-row through the closures the menu carries.
- `RunDismissCallback()` keeps its `action_invoked_` guard so the handler's `OnBubbleDismissed` runs exactly once.

Anchoring (`GetAnchorRectForSelection`, `GetArrowForSelection`) stays unchanged.

### 4.3 `maho_dom_screenshot_handler.h`

No new public API. Internal additions:

- `void OnSendToClicked();`
- `void OnSaveClicked();`            // identical body to current `OnDownloadClicked`; rename for clarity
- `void OnRetakeClicked();`
- `void OnEditClicked();`            // gated behind feature flag; otherwise unbound
- `void OnSendViaIMessageClicked();` // gated behind feature flag; otherwise unbound
- `void OnSaveToLibraryClicked();`   // gated behind feature flag; otherwise unbound

The existing `OnCopyClicked()` and `OnBubbleDismissed()` stay. `OnDownloadClicked()` is renamed to `OnSaveClicked()` for naming consistency with the new row label; the body is unchanged. If renaming risks a noisy diff against in-flight branches, keep `OnDownloadClicked` and have `OnSaveClicked` be a thin alias.

### 4.4 `maho_dom_screenshot_handler.cc`

`ShowResultBubble(const gfx::Rect& anchor_rect_screen)` builds a `MahoDomScreenshotActionMenu` and calls the new `Show(...)`. Concrete shape:

```cpp
MahoDomScreenshotActionMenu menu;
menu.header_label = u"Send To...";
menu.header_sf_symbol_name = "square.and.arrow.up";
menu.header_activate = base::BindOnce(&MahoDomScreenshotHandler::OnSendToClicked,
                                      weak_factory_.GetWeakPtr());

menu.rows.push_back({
    /*label=*/u"Copy",
    /*sf_symbol_name=*/"doc.on.doc",
    /*enabled=*/true,
    /*destructive=*/false,
    base::BindOnce(&MahoDomScreenshotHandler::OnCopyClicked,
                   weak_factory_.GetWeakPtr()),
});
menu.rows.push_back({
    u"Save...", "arrow.down.to.line", true, false,
    base::BindOnce(&MahoDomScreenshotHandler::OnSaveClicked,
                   weak_factory_.GetWeakPtr()),
});
menu.rows.push_back({
    u"Retake", "arrow.clockwise", true, false,
    base::BindOnce(&MahoDomScreenshotHandler::OnRetakeClicked,
                   weak_factory_.GetWeakPtr()),
});
menu.rows.push_back({
    u"Edit", "pencil.tip.crop.circle",
    /*enabled=*/IsScreenshotEditFlagOn(),
    false,
    IsScreenshotEditFlagOn()
        ? base::BindOnce(&MahoDomScreenshotHandler::OnEditClicked,
                         weak_factory_.GetWeakPtr())
        : base::OnceClosure(),
});
menu.rows.push_back({
    u"Send via iMessage...", "message",
    /*enabled=*/IsImessageShareFlagOn() && IsMacBuild(),
    false,
    (IsImessageShareFlagOn() && IsMacBuild())
        ? base::BindOnce(&MahoDomScreenshotHandler::OnSendViaIMessageClicked,
                         weak_factory_.GetWeakPtr())
        : base::OnceClosure(),
});
menu.rows.push_back({
    u"Save to Library", "tray.and.arrow.down",
    /*enabled=*/IsSaveToLibraryFlagOn(),
    false,
    IsSaveToLibraryFlagOn()
        ? base::BindOnce(&MahoDomScreenshotHandler::OnSaveToLibraryClicked,
                         weak_factory_.GetWeakPtr())
        : base::OnceClosure(),
});

menu.dismiss_callback = base::BindOnce(
    &MahoDomScreenshotHandler::OnBubbleDismissed, weak_factory_.GetWeakPtr());

result_bubble_widget_ = MahoDomScreenshotActionDialogView::Show(
    native_view, anchor_rect_screen, std::move(menu));
```

The three feature-flag helpers (`IsScreenshotEditFlagOn`, `IsImessageShareFlagOn`, `IsSaveToLibraryFlagOn`) are local file-scope helpers reading either `base::FeatureList` entries declared next to `kCaptureCommandId` or, if a Maho-side feature-flag header already exists at this layer, the equivalent constant from there. Pick whichever pattern is already used in the overlay; do not invent a new flag plumbing layer.

`OnSaveClicked()` is the renamed `OnDownloadClicked()`. Body unchanged.

`OnRetakeClicked()` shape:

```cpp
void MahoDomScreenshotHandler::OnRetakeClicked() {
  content::WebContents* wc = web_contents();
  finished_ = true;
  if (result_bubble_widget_) {
    result_bubble_widget_->CloseNow();
    result_bubble_widget_ = nullptr;
  }
  if (wc) {
    // The current handler self-deletes once the bubble is dismissed; trigger a
    // fresh capture against the same WebContents.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&MahoDomScreenshotHandler::TriggerCapture,
                       base::Unretained(wc)));
  }
  // Note: do not call Finish() here directly; CloseNow() drives
  // OnBubbleDismissed() which calls Finish() on the existing handler.
}
```

`OnSendToClicked()` shape:

```cpp
void MahoDomScreenshotHandler::OnSendToClicked() {
  content::WebContents* wc = web_contents();
  if (!wc) {
    Finish();
    return;
  }
  // Route the captured bitmap through the existing Sharing Hub. The Hub does
  // not natively accept an in-memory bitmap; serialize the bitmap to a
  // temporary on-disk file (or a data URL) and present that URL as the share
  // payload through SharingHubBubbleController::CreateOrGetFromWebContents.
  // If the routing surface is not reachable in this phase, fall through to
  // the disabled-stub state guarded by the IsSendToFlagOn() check above.
  Browser* browser = chrome::FindBrowserWithTab(wc);
  if (!browser) {
    Finish();
    return;
  }
  sharing_hub::SharingHubBubbleController* controller =
      sharing_hub::SharingHubBubbleController::CreateOrGetFromWebContents(wc);
  if (controller) {
    controller->ShowBubble(/*from_omnibox=*/false);
  }
  Finish();
}
```

If presenting the captured bitmap as the Sharing Hub's payload requires more than the snippet above (e.g., an extra hop through `share::ShareAttempt` or platform-specific delegate registration), gate the row behind a feature flag for now and document the integration as deferred. Do not invent a finished image-share pipeline.

`OnEditClicked()`, `OnSendViaIMessageClicked()`, `OnSaveToLibraryClicked()` are stubs in the first commit:

```cpp
void MahoDomScreenshotHandler::OnEditClicked() {
  // Deferred: requires either a Maho image-editor surface or adoption of
  // image_editor::ScreenshotFlow's editor entry point. The current
  // chrome://maho-inline-edit surface is text-oriented and cannot be reused.
  // When the prerequisite path lands, hand off captured_image_ to it here.
  Finish();
}
```

The `Send via iMessage...` and `Save to Library` stubs follow the same shape, each documenting the exact prerequisite that must land before the row can leave stub state (macOS `NSSharingService` shim plumbing for the former; a Maho image-library destination for the latter).

### 4.5 `maho_dom_screenshot_action_dialog_view_unittest.cc`

Migrate existing tests to the new menu shape. New tests:

1. `MenuRendersExpectedRowsInOrder` - given a built menu with all six rows, the rendered children are `Send To...` header + `Copy`, `Save...`, `Retake`, `Edit`, `Send via iMessage...`, `Save to Library` in that order. Pin the labels as constants in the test target.
2. `RetakeRowDismissesBubbleAndFiresActivate` - clicking `Retake` invokes the bound closure exactly once and triggers the dismiss callback.
3. `DisabledRowDoesNotFireActivate` - given a row with `enabled = false`, clicking it does not invoke its closure and does not dismiss the bubble.
4. `EscapeStillDismissesAndFiresDismissCallback` - regression guard for the existing accelerator path.
5. `HeaderAffordanceFiresActivateAndDismisses` - clicking the `Send To...` header invokes its closure and dismisses the bubble (since the share UI takes focus afterwards).

Keep `PressCopyForTesting()` / `PressDownloadForTesting()` semantics by exposing a generic `PressRowForTesting(size_t index)` helper instead, then update callers.

### 4.6 Feature flags

If the overlay does not already have a feature-flag header next to `MahoDomScreenshotHandler`, declare three minimal `BASE_FEATURE` entries in a small `maho_dom_screenshot_features.{h,cc}` file alongside the handler:

- `kMahoScreenshotEditAction` (default disabled)
- `kMahoScreenshotImessageShareAction` (default disabled, gated to `BUILDFLAG(IS_MAC)` at the row level)
- `kMahoScreenshotSaveToLibraryAction` (default disabled)

Each flag's only consumer is the corresponding `IsXxxFlagOn()` helper in `maho_dom_screenshot_handler.cc`. No new GN target is required; add the two files to the existing `maho_dom_screenshot` source set in the parent `BUILD.gn`. If touching the existing `BUILD.gn` would require a deeper edit than a literal source-list append, defer the flag wiring into the same file as the handler (file-scope `base::Feature` constants) instead.

### 4.7 Adjacent surfaces (read-only)

- `maho_dom_screenshot_script.js` is not edited.
- The CDP capture path (`Page.captureScreenshot` dispatch) is not edited.
- `MahoDomScreenshotHandler::TriggerCapture` is not edited beyond the renamed `OnSaveClicked` reference (if rename ships).
- The utility-panel `camera`/`viewfinder` slots covered by `address-bar-utility-panel-phase-2-plan.md` are not in this plan's scope; this plan only redesigns the post-capture bubble that shows up after capture finishes.

---

## 5. Risks & Open Questions

| Item | Likelihood | Note |
|---|---|---|
| Sharing Hub does not natively accept an in-memory bitmap as its share payload, so `Send To...` routing requires a temporary on-disk file or `data:` URL hop. | High | Ship behind a feature flag with disabled-stub UI if the routing surface is not reachable cleanly in this phase. Track the file-hop pattern as a follow-up. |
| `Edit` has no reachable plumbing today. The current Maho inline-edit surface is text-oriented, and `image_editor::ScreenshotFlow` exists upstream but the current Maho flow bypasses it. | High | Ship the row as a feature-flagged stub. The follow-up decides between a Maho-native image editor or adoption of the upstream flow's editor entry point. |
| `Send via iMessage...` requires a macOS-specific `NSSharingService` shim that does not exist anywhere in the Maho codebase today. Cross-platform builds need the row hidden. | High | Ship behind a feature flag, gate to `BUILDFLAG(IS_MAC)` at the row level so non-macOS builds simply don't render the row. The follow-up adds the Objective-C++ shim. |
| `Save to Library` has no library destination defined. Notes/Easel surfaces are unrelated and not plumbed for arbitrary captured bitmaps. | High | Ship behind a feature flag. The follow-up answers the product question of where "library" lives (likely a `maho-core`-backed local store) before integration begins. |
| `Retake` posts a new `TriggerCapture(wc)` while the current handler is still finishing teardown. Re-injecting the overlay script before the old one's isolated world has been torn down may double-bind the polling variable. | Medium | The post-task to the next UI loop tick gives the old handler time to `Finish()` and detach the DevTools client. If a real race surfaces, route `OnRetakeClicked` through `Finish()` first and bind a one-shot continuation that fires `TriggerCapture` after the old handler's destructor runs (e.g. via `base::WeakPtr<content::WebContents>`-keyed task). |
| Renaming `OnDownloadClicked` to `OnSaveClicked` collides with in-flight branches that reference the old symbol. | Low | If contention is real, keep `OnDownloadClicked` and add `OnSaveClicked` as a thin alias; update tests accordingly. |
| Dark-only color tokens regress light-mode contrast. | Low | Use the existing dark/light-aware tokens (e.g. `ui::kColorSysSurfaceContainerHigh` plus `ui::kColorSysOnSurface`) so the bubble remains legible on light themes; the reference's appearance is the dark-mode rendering of the same tokens. |
| Disabled feature-flagged rows visually clutter the menu. | Medium | If product wants a cleaner menu, render flagged-off rows as hidden rather than disabled. Decision: ship visible-but-disabled in the first commit (so the user can see the future behavior) and revisit before unflagging. |

Open product questions to confirm before unflagging the deferred rows:

1. Where does "Library" live? `maho-core`-backed local store vs. a directory under the user data dir vs. an integration into Maho Notes/Easel.
2. Does `Edit` mean a markup/annotation tool, a crop tool, both, or a full mini-editor? This decides between adopting `image_editor::ScreenshotFlow`'s editor surface and writing a Maho-native one.
3. For `Send To...`, is the Sharing Hub the right destination, or should this row open the macOS native share sheet directly (`NSSharingServicePicker`) on macOS? The current plan picks Sharing Hub for parity with the utility panel's `BuildShareAction`; the macOS-native picker is the obvious alternative.
4. Should `Send via iMessage...` collapse into `Send To...` on macOS instead of being a separate row? The reference shows both. The current plan honors the reference and ships them as distinct rows.

---

## 6. Verification

Acceptance gates:

1. **Unit tests green.** `cd maho && bun run test` passes. The new dialog tests in §4.5 pass under the existing `maho_dom_screenshot_action_dialog_view_unittest` target.
2. **Canonical build green.** `python3 maho-chromium/build/scripts/build_maho.py` exits 0 from the workspace root.
3. **Manual QA.** A reviewer runs the selected-area screenshot flow against a few representative pages and verifies, in order:
   - The post-capture bubble matches the reference's dark, vertical layout with the `Send To...` header on top and the rows in the correct order.
   - `Copy` writes the bitmap to the system clipboard.
   - `Save...` triggers the standard download flow with a sensible filename (same as today's `Download`).
   - `Retake` dismisses the current bubble and re-enters selection capture against the same `WebContents` without leaving the old overlay or the old handler resident.
   - `Send To...` opens the existing Sharing Hub bubble (or, if feature-flagged off, renders as a disabled stub).
   - `Edit`, `Send via iMessage...`, and `Save to Library` each render as visible-but-disabled stubs in the first commit; their hover/focus rendering still respects the reference styling.
   - Escape and click-outside still dismiss the bubble and run the dismiss callback exactly once.
   - The captured bitmap survives across `Retake` (i.e., the old image does not leak into the new flow).

Optional, non-gating:

- `cd maho && bun run typecheck` for early Rust regressions.
- `lsp_diagnostics` on each edited C++ file before commit.

Out of scope as gates:

- Interactive UI tests beyond the existing dialog unittest.
- Integration tests of the share, edit, library, or iMessage paths (their plumbing is feature-flagged off in the first commit; integration tests land with the follow-up that wires the prerequisite paths).
- Screenshot diffs.
