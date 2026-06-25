# Theme Picker WebUI Migration Plan

> **⚠️ SUPERSEDED (2026-05-22)**: This plan proposed the migration boundary at `OnThemePressed()` (correct) but routed to a different destination URL. See `.sisyphus/plans/theme-picker-architecture-decision.md` (ADR) — theme picker permanently uses `chrome://maho-space-create` only. The phase 0-4 work documented here was performed and is the current production state.

**Created:** 2026-05-22
**Supersedes:** `space-new-revert-to-webui-plan.md` (which was based on incorrect interpretation)
**Status:** Draft — pending user approval + Momus review

---

## 0. Why This Plan Exists (Correction Record)

User clarification (verbatim): _"왜 chrome://maho-space-create 를 만들어? 우리는 네이티브 inline 사이드바에서 Create theme 윈도우를 webui로 만드는걸 하느넥 아닌거야?"_

The previous plan (`space-new-revert-to-webui-plan.md`) misinterpreted Ground Truth (Image1 = `maho/reference_screenshots/create-theme.png`) as a single two-column standalone WebUI dialog replacing the entire native inline create-space surface. It deleted `MahoSidebarCreateSpaceView` (the native parent form) and made `MahoSpaceCreateDialog` host a full-page `chrome://maho-space-create` WebUI.

**That was wrong.** Correct intent: keep the native parent form, migrate ONLY the native theme-picker sub-dialog (`MahoCreateSpaceThemePickerDialogView`, currently at lines ~200–1232 of `maho_sidebar_create_space_view.cc`) to a WebUI-hosted dialog. The form ↔ theme-picker boundary is the migration boundary.

This plan reverts the misguided Phase 4–7 work and replans around the correct boundary.

---

## 1. Goal & Anti-Goal

### Goal (single sentence)
Migrate the **native theme-picker sub-dialog** opened from `MahoSidebarCreateSpaceView::OnThemePressed()` to a WebUI dialog backed by `chrome://maho-space-create`, while keeping the surrounding native sidebar form (`MahoSidebarCreateSpaceView`) and its entry points (`footer "+" → New Space → ShowCreateSpace()`) unchanged.

### Anti-Goal (explicitly NOT this)
- Do NOT delete `MahoSidebarCreateSpaceView`.
- Do NOT redirect `footer kNewSpace` away from `ShowCreateSpace()`.
- Do NOT put a form (Space name input, Profile picker, "Create a Space" title, Create Space button) inside the WebUI dialog — those belong to the native parent form.
- Do NOT touch `chrome://maho-space-config` create-mode logic; out of scope.
- Do NOT modify upstream Chromium files (`chrome/test/BUILD.gn`, `omnibox_popup_closer.cc`, etc) — per `AGENTS.md` rule.

### Visual GT
- `maho/reference_screenshots/create-theme.png` (= `.sisyphus/plans/assets/theme-parity-image1.png`).
- The right-panel portion of GT corresponds to the WebUI dialog content. The left-panel portion of GT corresponds to the native parent form (already implemented natively, kept as-is).

---

## 2. Current State Snapshot (verifiable)

| File / artifact | State on disk (2026-05-22) | Disposition |
|---|---|---|
| `maho/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` | 1675 lines, mtime 2026-05-22 08:32. Contains native form + `MahoCreateSpaceThemePickerDialogView` + `ShowThemePickerDialog` + `ThemeWaveView`/`ThemeDialView`/`ThemeCanvasView`. | KEEP form code; DELETE theme-picker portion (Phase 4) |
| `maho/browser/ui/views/sidebar/maho_sidebar_view.{h,cc}` | `kCreateSpace` enum / `ShowCreateSpace()` / `create_space_view_` member all present | KEEP unchanged |
| `maho/browser/ui/views/sidebar/maho_sidebar_footer_view.cc:450` | `kNewSpace` → `sidebar_view->ShowCreateSpace()` (correct routing) | KEEP unchanged |
| `maho/browser/ui/views/sidebar/BUILD.gn` | Has `maho_sidebar_create_space_view.cc/.h` in sources + Korean do-not-revert comment | KEEP comment + sources |
| `maho/browser/ui/views/space_create/maho_space_create_dialog.{h,cc}` | DELETED (other sessions removed our Phase 3 file) | RECREATE under new name (Phase 2) |
| `maho/browser/ui/webui/maho_space_create/` directory | DELETED entirely | RECREATE (Phase 1) |
| `maho/browser/resources/maho_space_create/react/app.tsx` | 418 lines, has LeftPanel + RightPanel + sc-create-btn + sc-cancel-btn | REWRITE to theme-picker-only (Phase 1) |
| `maho/browser/resources/maho_space_create/bundle_react.mjs` | exists | KEEP, adjust if needed |
| `maho/browser/resources/maho_space_create/maho_space_create.mojom-webui.js.d.ts` | exists | REGENERATE from new mojom |
| `maho/browser/ui/views/command/maho_command_action_handler.cc` | `new_space` action — current routing unknown, needs grep | - VERIFY routes to native form (`ShowCreateSpace()` or maho-core `create_space`) — out of scope to change |
| `chrome/src/chrome/test/BUILD.gn:8566` | `maho_acp_unittests` dep removed earlier (still removed?) | VERIFY; do NOT re-edit if other session re-added |
| `tab_list_view.cc` `IsVisible()` guard around `SynthesizeMouseMoveEvent()` | applied | KEEP (defensive, harmless) |
| `AGENTS.md` "GN 강제 수정 금지" rule | added (root + maho-chromium) | KEEP |

**Verification commands** (run before Phase 0):
```bash
cd maho-chromium
grep -c "ShowCreateSpace\|kCreateSpace\|MahoSidebarCreateSpaceView" \
  browser/ui/views/sidebar/maho_sidebar_view.h \
  browser/ui/views/sidebar/maho_sidebar_view.cc \
  browser/ui/views/sidebar/maho_sidebar_footer_view.cc
# Expect: ≥1 in each (native form path alive)

ls browser/ui/webui/maho_space_create 2>&1 | grep "No such" && echo "WebUI dir gone (expected, will recreate)"
```

---

## 3. Target Architecture

```
sidebar
└─ MahoSidebarView (container)
   └─ MahoSidebarCreateSpaceView   ← KEPT NATIVE (form: name input, profile, Choose-a-Theme card)
      ├─ theme_button_ (Choose-a-Theme card)
      └─ on click → OnThemePressed()
                    │
                    └─ MahoSpaceThemePickerDialog::Open(browser_, current_selection, on_done_callback)   ← NEW dialog wrapper (Phase 2)
                       └─ views::WebView hosting chrome://maho-space-create   ← WebUI (Phase 1)
                          ├─ React app: theme-picker-only
                          │  ├─ Mode chips (✦ ☀ 🌙)
                          │  ├─ Dot-grid canvas + "Tap to pick a color for this space"
                          │  ├─ Color swatches + chevron
                          │  ├─ Wave + dial
                          │  └─ Cancel / Done buttons
                          └─ Mojo:
                             ├─ Page→Handler: PreviewTheme(theme_json)   (live preview while picking)
                             ├─ Page→Handler: CommitTheme(theme_json)    (Done click)
                             └─ Page→Handler: CancelTheme()              (Cancel click)
                          ↓
                          callback fires with MahoSidebarCreateSpaceThemeSelection
                          ↓
                          MahoSidebarCreateSpaceView::ApplyThemeSelection(selection)
```

### Key invariants
- **Native form remains the root entry point** (`footer "+" → New Space → ShowCreateSpace() → SetBodyMode(kCreateSpace)`).
- **WebUI only owns the theme-picker UX**; form fields stay native.
- **Selection state lives in native form** (`MahoSidebarCreateSpaceView::theme_selection_`); WebUI is stateless w.r.t. persistence — it receives initial selection on dialog open and emits final selection on Done.
- **Live preview** uses `MahoSpaceThemeState::SetPreviewOverride(theme_json)` (existing API at `browser/ui/theme/maho_space_theme_state.{h,cc}`) followed by `ui::ColorProviderManager::Get().ResetColorProviderCache()` to flush themed colors. Cleanup on dialog close mirrors `maho_space_config_dialog.cc:64-68` (`HasPreviewOverride()` → `ClearPreviewOverride()` + `UpdateFromCore()` + `ResetColorProviderCache()`).
- **No upstream Chromium edits.**

---

## 4. Phase Breakdown (file-level)

### Phase 0 — Baseline cleanup & state verification (15 min)
**Goal:** Clean working tree to a known-good native baseline before adding new code; resolve `maho_ffi.h` stdint blocker.

**Files:**
- Verify all of §2 disposition with `grep`.
- **Resolve `maho_ffi.h` stdint blocker** (cause: another session changed `maho/crates/maho-ffi/cbindgen.toml` `sys_includes` to C-style `["stdint.h", "stddef.h"]`, breaking Chromium Clang Modules):
  - Edit `maho/crates/maho-ffi/cbindgen.toml`: `sys_includes = ["stdint.h", "stddef.h"]` → `sys_includes = ["cstdint", "cstddef"]`
  - Regenerate header: `cd maho && cbindgen --config crates/maho-ffi/cbindgen.toml --crate maho-ffi --output ../maho-chromium/third_party/maho/maho_ffi.h`
  - Verify line ~6 of regenerated header reads `#include <cstdint>` (not `#include <stdint.h>`)
- Run `python3 maho-chromium/build/scripts/build_maho.py` — expect pass.

**Acceptance:**
- `grep -c "ShowCreateSpace\|kCreateSpace\|MahoSidebarCreateSpaceView" sidebar/maho_sidebar_view.h` ≥ 1 (native form alive)
- `head -10 maho-chromium/third_party/maho/maho_ffi.h | grep cstdint` matches
- `python3 maho-chromium/build/scripts/build_maho.py` exit 0
- `git diff --stat` shows only intended files (no unauthorized GN edits)

**Commit:** `chore(maho-ffi): restore cstdint sys_includes for Chromium Clang Modules compatibility`

---

### Phase 1 — Recreate WebUI surface (theme-picker-only) (45 min)
**Goal:** Build `chrome://maho-space-create` as a WebUI exclusively for theme picking.

**New files:**

#### `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create.mojom`
```mojom
module maho_space_create.mojom;

struct ThemeSelection {
  string theme_json;            // Serialized MahoSidebarCreateSpaceThemeSelection
};

interface PageHandlerFactory {
  CreatePageHandler(pending_remote<Page> page,
                    pending_receiver<PageHandler> handler);
};

interface PageHandler {
  // Live preview while user is picking; handler forwards to
  // MahoSpaceProfileBridge so the active-space color updates immediately.
  PreviewTheme(ThemeSelection selection);

  // User clicked Done. Handler emits to the dialog wrapper which closes
  // and forwards to the parent form's callback.
  CommitTheme(ThemeSelection selection);

  // User clicked Cancel. Handler emits to the dialog wrapper which closes.
  CancelTheme();
};

interface Page {
  // Handler → page: initial selection on dialog open.
  Initialize(ThemeSelection initial);
};
```

#### `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_page_handler.{h,cc}`
- Class `MahoSpaceCreatePageHandler`:
  - Ctor: `(receiver, page, browser, web_contents, dialog_observer)`
  - `dialog_observer` is the `MahoSpaceThemePickerDialog` instance (or its weak ref).
  - `PreviewTheme(selection)` → `MahoSpaceThemeState::SetPreviewOverride(theme_json)` + `ui::ColorProviderManager::Get().ResetColorProviderCache()` (no thread-pool needed; `SetPreviewOverride` is UI-thread-only synchronous).
  - `CommitTheme(selection)` → notify `dialog_observer` with selection; observer closes widget.
  - `CancelTheme()` → notify `dialog_observer` cancel; observer closes widget.

#### `maho-chromium/browser/ui/webui/maho_space_create/maho_space_create_ui.{h,cc}`
- Standard `MojoWebUIController` boilerplate matching maho_notes_ui.cc style.
- Sets up data source, registers `kMahoSpaceCreateResources`.

#### `maho-chromium/browser/ui/webui/maho_space_create/BUILD.gn`
- Targets: `maho_space_create_handler` (cc/h), generated mojom binding deps, `maho_space_create_resources` (grit).

#### `maho-chromium/browser/resources/maho_space_create/space_create.html`
- Minimal: `<head>` with title "Theme picker", `<body>` with `<div id="root">`, `<script>` loading `app.js` via Trusted Types policy (use existing pattern in `app.ts`).

#### `maho-chromium/browser/resources/maho_space_create/app.ts`
- KEEP existing trusted-types pattern (already on disk and working in last build).

#### `maho-chromium/browser/resources/maho_space_create/react/app.tsx` — REWRITE
- DELETE: `LeftPanel`, `sc-create-btn`, `sc-cancel-btn` (form), all form-related state.
- KEEP/ADAPT: `RightPanel` (theme picker) — but rename CSS classes from `sc-right-*` to `tp-*` (theme-picker) for clarity.
- ADD: bottom-row `tp-footer` with `Cancel` + `Done` buttons (rendered inside WebUI; Phase 2 dialog uses `SetButtons(kNone)`).
- Mojo wiring:
  - On mount → `PageRemote.Initialize(initial)` callback → seed local state.
  - On any selection change (color/mode/brightness) → debounced `handler.PreviewTheme(selection)`.
  - On Done click → `handler.CommitTheme(selection)`.
  - On Cancel click → `handler.CancelTheme()`.

#### `maho-chromium/browser/resources/maho_space_create/space_create.css`
- DELETE `sc-left*` rules.
- KEEP `sc-right*` rules (already GT-aligned per old Phase 2 work).
- ADD `tp-footer` rules to position Cancel/Done.
- (Optional: rename `sc-` → `tp-` everywhere for clarity. NICE-TO-HAVE, not required.)

#### `maho-chromium/browser/resources/maho_space_create/BUILD.gn`
- Update `ts_files` / `bundle_react` action inputs to match new file set.
- Add Mojo TS binding dep.

#### `maho-chromium/browser/resources/maho_space_create/types.ts`
- Update `ThemeSelection` interface to match mojom.

**Acceptance:**
- `chrome://maho-space-create` loads cleanly in isolated launch (no trusted-types errors, no resource ERR_FAILED).
- DOM contains the 12 GT elements (mode chips, swatches, wave, dial, etc).
- Mojo round-trip works in DevTools console (manual: `chrome.maho_space_create.handler.previewTheme({theme_json: '...'})`).

**Commit:** `feat(maho-space-create): recreate WebUI as theme-picker-only surface (Mojom + handler + UI + resources)`

---

### Phase 2 — New dialog wrapper `MahoSpaceThemePickerDialog` (20 min)
**Goal:** Native `views::DialogDelegate` hosting `WebView(chrome://maho-space-create)` with `SetButtons(kNone)`.

**New files:**

#### `maho-chromium/browser/ui/views/space_create/maho_space_theme_picker_dialog.{h,cc}` (NEW DIRECTORY)
- Class `MahoSpaceThemePickerDialog`:
  - Static `Open(Browser*, MahoSidebarCreateSpaceThemeSelection initial, base::OnceCallback<void(MahoSidebarCreateSpaceThemeSelection)> on_done)`.
  - On open: serialize `initial` to theme_json, post to WebUI via `Page::Initialize` after handler is ready.
  - Holds the `on_done` callback; invokes it on `CommitTheme` or invokes `on_cancel` semantics on `CancelTheme`.
  - Width 680, height ~~ matches the existing native dialog (kThemeDialogWidth = 680, fits the canvas + footer + swatch row).
  - `SetButtons(kNone)`, `SetShowCloseButton(false)`, `SetShowTitle(false)`, `SetModalType(kWindow)`.
  - Implements `MahoSpaceCreatePageHandler::DialogObserver` (or registers callback) to receive Commit/Cancel signals.

#### `maho-chromium/browser/ui/views/space_create/BUILD.gn` (NEW)
- Target `maho_space_theme_picker_dialog`:
  - sources: `maho_space_theme_picker_dialog.{h,cc}`
  - deps: `//base`, `//chrome/browser/profiles:profile`, `//chrome/browser/ui/browser_window`, `//maho/browser:maho_theme_support`, `//maho/components/constants`, `//ui/base`, `//ui/views`, `//ui/views/controls/webview`, `//maho/browser/ui/webui/maho_space_create:handler` (for callback contract).

**Modified files:**
- `maho-chromium/browser/BUILD.gn` — add dep on `space_create:maho_space_theme_picker_dialog` if exposed there. (Mirror prior pattern.)

**Acceptance:**
- Dialog opens from a stub callsite (e.g., a temporary debug button); shows WebUI; Cancel/Done callbacks fire correctly.
- No `views::DialogDelegate` cycle errors.

**Commit:** `feat(space_create): add MahoSpaceThemePickerDialog WebView host wrapper`

---

### Phase 3 — Native form integration (15 min)
**Goal:** Redirect `MahoSidebarCreateSpaceView::OnThemePressed()` from native `ShowThemePickerDialog` to new `MahoSpaceThemePickerDialog::Open`.

**Modified files:**

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
- Replace body of `MahoSidebarCreateSpaceView::OnThemePressed()` (line ~1573):
  ```cpp
  void MahoSidebarCreateSpaceView::OnThemePressed() {
    if (!browser_) return;
    MahoSpaceThemePickerDialog::Open(
        browser_, theme_selection_,
        base::BindOnce(
            [](base::WeakPtr<MahoSidebarCreateSpaceView> view,
               MahoSidebarCreateSpaceThemeSelection selection) {
              if (view) view->ApplyThemeSelection(selection);
            },
            weak_factory_.GetWeakPtr()));
  }
  ```
- Add include: `#include "maho/browser/ui/views/space_create/maho_space_theme_picker_dialog.h"`.

#### `maho-chromium/browser/ui/views/sidebar/BUILD.gn`
- Add dep `//maho/browser/ui/views/space_create:maho_space_theme_picker_dialog` to `maho_sidebar_library` source_set (since `maho_sidebar_create_space_view.cc` lives there).

**Acceptance:**
- Theme card click in native form opens new WebUI dialog; selection round-trips correctly.

**Commit:** `feat(sidebar): route native create-space form theme card to WebUI picker dialog`

---

### Phase 4 — Remove native theme dialog code (30 min)
**Goal:** Delete the now-replaced native theme picker classes from `maho_sidebar_create_space_view.cc/.h`.

**Modified files:**

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`
DELETE these constructs (line numbers approximate, current file 1675 lines → ~600 lines after):
- Constants `kThemeDialogWidth`, `kThemeDialogSurfaceCornerRadius`, `kThemeChipSize`, `kThemeCanvasCornerRadius`, `kThemeCanvasHeight`, `kThemePreviewControlSize`, `kThemeChevronButtonSize`, `kThemeGrainDialSize`, `kThemeCanvasDotSpacing` (lines ~109–125).
- Theme-dialog paint/layout constants (lines ~166–199).
- Globals `g_theme_picker_widget`, `g_theme_picker_dialog_view` (lines ~202–205).
- Class forward decl `MahoCreateSpaceThemePickerDialogView`.
- All native theme-picker subview classes: `ThemeWaveView`, `ThemeDialView`, `ThemeCanvasView`, etc.
- Class `MahoCreateSpaceThemePickerDialogView` (entire class body).
- Function `ShowThemePickerDialog(...)` (line ~1170).
- Test-only accessors `MahoSidebarCreateSpaceView::theme_picker_widget_for_testing()`, `SetThemeSelectionForTesting`, `AcceptThemePickerForTesting` if they exist.
- Helper functions used only by deleted classes.

KEEP:
- `MahoSidebarCreateSpaceThemeSelection` struct + helpers (`DefaultThemeSelection`, `BuildThemeJsonForSelection`, `BuildThemeSelectionFromJson` if exists) — used by mojom serialization.
- `MahoSidebarCreateSpaceView` form class itself (form fields, `theme_button_`, `OnThemePressed`, `ApplyThemeSelection`, `PrepareForOpen`, `RequestNameFocus`, etc).
- All form-related layout/styling.

#### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.h`
- DELETE forward decl of `MahoCreateSpaceThemePickerDialogView`.
- DELETE test-only accessors related to theme picker dialog.
- KEEP form-related public API.

**Acceptance:**
- `grep -c "MahoCreateSpaceThemePickerDialogView\|ShowThemePickerDialog\|ThemeWaveView\|ThemeDialView\|ThemeCanvasView" maho_sidebar_create_space_view.cc` → 0.
- File length drops to ~600 lines.
- `bun run typecheck` + Chromium build pass.

**Commit:** `refactor(sidebar): remove native theme picker dialog (replaced by WebUI dialog)`

---

### Phase 5 — GT visual parity (V19/V20) (30 min)
**Goal:** Ensure WebUI rendering matches Image1 right-panel pixel-by-pixel within tolerance.

**Process:**
- Manual visual diff: launch isolated Maho, open theme picker dialog, screenshot, overlay against `theme-parity-image1.png` right-panel crop.
- Iterate CSS / React JSX until all 12 GT elements match (D1 dot grid, D2 mode chips, D3 brightness, D4 swatches, D5 chevrons, D6 wave, D7 dial, D8 footer; KP-3 reset on open, KP-5 sparkle ring, KP-6 sun ring, KP-7 moon ring, KP-10 dial blue dot, KP-11 swatch auto-scroll on selection).
- No code architecture changes — purely styling/layout polish.

**Files modified:**
- `maho-chromium/browser/resources/maho_space_create/space_create.css`
- `maho-chromium/browser/resources/maho_space_create/react/app.tsx`
- (Optionally) `maho-chromium/browser/resources/maho_space_create/react/store.ts`

**Acceptance:**
- All 12 GT elements present in DOM (`document.querySelectorAll('[data-maho-el]').length === 12`).
- Visual diff vs Image1 ≤ tolerance (define: cropped right-panel pixel diff < 5% via ImageMagick `compare`).

**Commit:** `polish(maho-space-create): GT-align theme picker WebUI to reference Image1`

---

### Phase 6 — Manual QA + final commit (per user "테스트는 하지 말 것", skip automated tests) (15 min)
**Goal:** End-to-end smoke check by user.

**Manual QA checklist (user performs):**
1. Footer "+" → New Space → native inline form appears in sidebar (left rail).
2. Form: Type name, pick profile.
3. Click "Choose a Theme" card → WebUI dialog opens (modal, ~680×~~ size).
4. Mode chips ✦/☀/🌙: click each, ring moves, mode state updates.
5. Color swatches: click → preview canvas updates color.
6. Brightness +/− or dial: live preview updates.
7. Cancel button: dialog closes, native form's prior theme selection unchanged.
8. Done button: dialog closes, native form's `theme_button_` updates to show new selection (e.g., color dot changes).
9. Submit native form: new space created with selected theme.
10. Verify in sidebar that new space dot has chosen color.

**Acceptance:** all 10 steps pass.

**Commit:** `chore(plan): mark theme-picker-webui-migration complete + QA notes`

---

## 5. Acceptance Criteria (overall)

| ID | Criterion | Verification |
|---|---|---|
| V1 | Native form intact | `grep "class MahoSidebarCreateSpaceView" sidebar/maho_sidebar_create_space_view.h` = 1 |
| V2 | Footer routing native | `grep "ShowCreateSpace" sidebar/maho_sidebar_footer_view.cc` ≥ 1 |
| V3 | New WebUI surface | `chrome://maho-space-create` loads; DOM count `[data-maho-el]` = 12 |
| V4 | New dialog wrapper | `grep "class MahoSpaceThemePickerDialog" space_create/*.h` = 1 |
| V5 | Native theme picker code removed | `grep "MahoCreateSpaceThemePickerDialogView" maho_sidebar_create_space_view.cc` = 0 |
| V6 | Mojo wired | DevTools console: `previewTheme()`/`commitTheme()`/`cancelTheme()` callable, no errors |
| V7 | Live preview | While picking, sidebar space dot color updates in real time |
| V8 | Cancel reverts | Cancel restores prior theme |
| V9 | Done persists | Done updates form's selection |
| V10 | Build green | `python3 build/scripts/build_maho.py` exit 0 |
| V11 | No upstream edits | `git status chromium/src/chrome/` only shows our intended files |
| V12 | Visual diff | Image1 right-panel match within tolerance |

---

## 6. Out of Scope
- `chrome://maho-space-config` create-mode (separate surface).
- `MahoSpaceCreateDialog` (full-page dialog from old plan) — not recreating.
- Footer/command palette routing changes — already correct.
- Test infrastructure (per user "테스트는 하지 말 것").
- `omnibox_popup_closer.cc` upstream patch (per AGENTS.md rule).
- `maho_acp_unittests` external blocker (per AGENTS.md rule).

---

## 7. Risks & Mitigation

| Risk | Likelihood | Mitigation |
|---|---|---|
| Other concurrent opencode sessions revert our changes mid-flight | High (observed pattern) | Apply each phase atomically + commit per phase; verify with grep before next phase |
| `MahoSpaceProfileBridge::SetActiveSpaceThemePreview` API doesn't exist yet | Medium | Phase 1 includes "add this API if missing"; check `maho_space_profile_bridge.{h,cc}` before relying |
| Mojo TS binding regen breaks existing imports | Low | Keep mojom interface name (`maho_space_create.mojom`) stable; only change interface contents |
| Native form selection state model mismatch with WebUI mojom | Medium | Use existing `MahoSidebarCreateSpaceThemeSelection` struct + JSON serialization end-to-end; do not introduce new types |
| Build blocked by `maho_ffi.h` stdint issue | High (currently observed) | Resolve in Phase 0 baseline OR document and request user decision before Phase 1 |
| Visual GT match fails | Medium | Phase 5 budgets 30 min for iteration; if exceeded, hand off to visual-engineering subagent |

---

## 8. Phase Dependency / Execution Order

```
Phase 0 (baseline)
  │
  ↓
Phase 1 (WebUI surface) ──────┐
  │                           │
  ↓                           │
Phase 2 (dialog wrapper) ←────┘
  │
  ↓
Phase 3 (native form integration)
  │
  ↓
Phase 4 (remove native theme picker code)
  │
  ↓
Phase 5 (GT visual parity)
  │
  ↓
Phase 6 (manual QA + commit)
```

Phases 1 and 2 are partially parallel (Phase 2 depends on Phase 1's mojom interface but can scaffold the dialog class first).

---

## 9. Estimated Total: ~2.5 hours of focused work (excluding waits for build / lock)
- Phase 0: 10 min
- Phase 1: 45 min
- Phase 2: 20 min
- Phase 3: 15 min
- Phase 4: 30 min
- Phase 5: 30 min
- Phase 6: 15 min

---

## 10. Plan Critique Pending
This plan should be reviewed by Momus (plan critic agent) before implementation begins. Pass `path=.sisyphus/plans/theme-picker-webui-migration-plan.md` to a `momus` task.
