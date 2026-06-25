# Theme Picker WebUI Migration — Completion Plan (Phase 5/6 Standalone Path)

**Created:** 2026-05-22 (revision)
**Supersedes Phase 5/6 sections of:** `theme-picker-webui-migration-plan.md`
**Reason:** Chromium build blocked by external GN dependency issues that cannot be fixed without user-approved GN edits. AGENTS.md rule prohibits autonomous GN modification. This plan completes Phase 5/6 entirely outside the Chromium build via standalone bundle + stock Chrome.

**Oracle review:** Passed. Confidence HIGH for visual parity, MEDIUM-HIGH for interaction parity within ~1 hour of work.

---

## 0. Status Recap (All Phases 0-4 done on disk)

| Phase | Status | Where |
|---|---|---|
| Phase 0 cbindgen.toml + maho_ffi.h | ✅ disk | `maho/crates/maho-ffi/cbindgen.toml`, `maho-chromium/third_party/maho/maho_ffi.h` |
| Phase 1.1 webui scaffolding (6 files) | ✅ disk | `maho-chromium/browser/ui/webui/maho_space_create/` |
| Phase 1.2 React resources (10 files) | ✅ disk | `maho-chromium/browser/resources/maho_space_create/` |
| Phase 1.3 URL constants | ✅ disk | `maho-chromium/components/constants/webui_url_constants.h` |
| Phase 1.4 Mojo binder registration | ✅ disk | `chromium/src/chrome/browser/chrome_browser_interface_binders_webui_parts_desktop.cc` |
| Phase 2 native dialog wrapper | ✅ disk | `maho-chromium/browser/ui/views/space_create/` |
| Phase 3 native form rewire (`OnThemePressed`) | ✅ disk | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` |
| Phase 4 native theme picker code deletion (1725→778) | ✅ disk | (same file as Phase 3) |
| Phase 5 GT visual parity | ⏳ this plan | standalone path |
| Phase 6 interaction QA + commit | ⏳ this plan | partial standalone, partial deferred |

Build blockers (NOT fixed by this plan, deferred until user approves GN edits OR external sessions complete their work):
- Group A (`MahoTabIdHelper::*` undefined symbols) — 5+ consumer GN targets need `:maho_tab_id_helper` dep
- Group B (`MahoSpaceCreateUI::BindInterface/kWebUIControllerType`) — `chrome/browser/BUILD.gn:5331` needs `//maho/browser/ui/webui/maho_space_create` dep

---

## 1. Strategy: Standalone Bundle in Stock Chrome

The React + CSS app under `browser/resources/maho_space_create/` is genuinely independent of Chromium runtime APIs **except** for Mojo bindings. By providing a 2-file dev-only stub, the entire app renders pixel-equivalently in stock Chrome served from `python3 -m http.server`.

What standalone covers:
- ✅ Visual parity vs `maho/reference_screenshots/create-theme.png` (right-panel crop) — pixel diff verifiable
- ✅ Interaction state transitions: mode chips ✦/☀/🌙, color swatches, brightness ±, dial, Cancel, Done
- ✅ Debounced preview emission shape (observable in console via stub)
- ✅ Commit/Cancel callback contract (shape verified, not actual cross-process delivery)

What standalone does NOT cover (deferred to build-pass session):
- ❌ Mojo round-trip across renderer↔browser process (`Page::Initialize`, `PreviewTheme`, `CommitTheme`, `CancelTheme`)
- ❌ Native dialog frame Close-on-Done/Cancel + parent native form callback
- ❌ `MahoSpaceThemeState::SetPreviewOverride` affecting actual sidebar space dot color
- ❌ End-to-end footer "+" → New Space → form → "Choose a Theme" card → dialog flow
- ❌ Build green (compile + link succeed)

---

## 2. Phase 5 — GT Visual Parity (~30 min)

### 5.1 Add Mojo TS stub (NEW FILE, dev-only)

**File:** `maho-chromium/browser/resources/maho_space_create/maho_space_create.mojom-webui.js` (NEW)

Resolves the `external` import declared at `bundle_react.mjs:38`. Provides minimal `PageHandlerFactory` / `PageHandlerRemote` / `PageCallbackRouter` exports matching the React app's runtime expectations.

Content per Oracle's recommendation: see plan implementation.

Bootstrap: `PageCallbackRouter.initialize.addListener` callback fires synchronously on next microtask with default `theme_json`, clearing the `loading: true` deadlock.

### 5.2 Add dev HTML (NEW FILE, dev-only)

**File:** `maho-chromium/browser/resources/maho_space_create/space_create.dev.html` (NEW)

Skips the Trusted Types script-loader shim from `app.ts` and loads `app_bundle.js` directly. This is necessary because `app.ts` is a TypeScript file that needs compilation; the dev HTML loads the bundle directly.

```html
<!DOCTYPE html>
<html lang="en"><head>
  <meta charset="utf-8"><title>Theme picker (dev)</title>
  <link rel="stylesheet" href="space_create.css">
</head><body>
  <div id="root"></div>
  <script type="module" src="app_bundle.js"></script>
</body></html>
```

### 5.3 Build bundle + serve

```bash
cd maho-chromium/browser/resources/maho_space_create
node bundle_react.mjs . app_bundle.js
# Background: python3 -m http.server 8000
# Open http://localhost:8000/space_create.dev.html in Chrome
```

### 5.4 Visual diff iteration

1. Capture screenshot of standalone render (Chrome devtools or Playwright `page.screenshot`)
2. Crop GT (`create-theme.png`) to right-panel only (the part we own)
3. Compare via ImageMagick `compare -metric AE` or pixelmatch
4. Iterate `space_create.css` / `react/app.tsx` until pixel diff < 5% of cropped area
5. Verify all 12 GT elements render correctly (D1–D8, KP-3/5/6/7/10/11)

### 5.5 Acceptance

- All 14 `data-maho-el` markers present in DOM (already verified by Phase 1.2 subagent)
- Pixel diff < 5% on right-panel crop
- ✦/☀/🌙 chips render with selected ring on the active one
- 9 swatches render in row with chevrons; selected swatch has white ring
- Wave decoration + dial knob render at correct positions
- Cancel/Done buttons at bottom-right with correct styling

### 5.6 Cleanup

Add `.gitignore` entries for `app_bundle.js` (already gitignored if pattern catches it) and `maho_space_create.mojom-webui.js` (the stub — must NOT be committed, it would shadow the real Mojo-generated file in the production build).

Verify: `git check-ignore browser/resources/maho_space_create/{app_bundle.js,maho_space_create.mojom-webui.js}` returns both as ignored.

---

## 3. Phase 6 — Interaction QA + Commit (~30 min)

### 6.1 Interaction QA (standalone, what's possible)

In the dev HTML rendered in Chrome:

| Action | Expected observable | Verified by |
|---|---|---|
| Click ✦ chip | chip gets selected ring; store mode→sparkle; `[stub] previewTheme {...mode:'sparkle'...}` logged | DOM + console |
| Click ☀ chip | chip ring moves; store mode→sun; preview log | DOM + console |
| Click 🌙 chip | chip ring moves; store mode→moon; preview log | DOM + console |
| Click each color swatch | swatch ring moves; store preset_index updates; preview log | DOM + console |
| Click + brightness | brightness state +12 (clamp 100); preview log | DOM + console |
| Click − brightness | brightness state −12 (clamp 0); preview log | DOM + console |
| Click chevron `‹` | swatch row scrolls (KP-11 auto-scroll on selection) | visual |
| Click chevron `›` | same | visual |
| Click Cancel | `[stub] cancelTheme` logged | console |
| Click Done | `[stub] commitTheme {theme_json: '...'}` logged with serialized state | console |

Document results in `.sisyphus/plans/theme-picker-completion-qa-log.md` (NEW).

### 6.2 Build-dependent QA — **deferred, NOT done in this plan**

Explicit list (becomes the Phase 7 checklist when build unblocks):
- E1: footer "+" → New Space → native inline form opens in sidebar
- E2: form → "Choose a Theme" card click → MahoSpaceThemePickerDialog opens (real)
- E3: WebUI mounts inside dialog WebView, receives Page::Initialize
- E4: User picks color → SetPreviewOverride called → ColorProviderManager.ResetColorProviderCache → real sidebar dot color updates live
- E5: Cancel → preview cleared via ClearPreviewOverride + UpdateFromCore + cache reset; dialog closes; native form theme_button_ unchanged
- E6: Done → preview cleared; CommitTheme delivers theme_json; native form's ApplyThemeSelection called via BuildSelectionFromThemeJson; theme_button_ updates
- E7: Submit form → space created with selected theme via BuildThemeJsonForSelection → maho_theme::ApplyThemeJsonToSpace
- E8: New space dot in sidebar shows chosen color
- E9: omnibox_popup_closer crash regression — verified absent (Phase 3 IsVisible guard prevents)
- E10: No console errors in real chrome://maho-space-create at runtime (no failed maho_common imports, no trusted-types violations)

### 6.3 Atomic commits per phase (CAN be done now)

This step does NOT depend on build success. The disk state is consistent with our plan. Suggested commit sequence:

```
1. chore(maho-ffi): restore cstdint sys_includes for Chromium Clang Modules
   (Phase 0)

2. feat(constants): add kMahoSpaceCreate{Host,URL} + kMahoSpaceConfig{Host,URL}
   (Phase 1.3)

3. feat(maho-space-create): add WebUI surface (Mojom + handler + UI + BUILD.gn)
   (Phase 1.1)

4. feat(maho-space-create): add theme-picker-only React + esbuild bundle
   (Phase 1.2)

5. feat(chrome): register MahoSpaceCreateUI Mojo binder
   (Phase 1.4)

6. feat(space_create): add MahoSpaceThemePickerDialog WebView host wrapper
   (Phase 2)

7. feat(sidebar): rewire OnThemePressed to MahoSpaceThemePickerDialog::Open
   + add BuildSelectionFromThemeJson helper
   (Phase 3)

8. refactor(sidebar): remove native theme picker dialog (replaced by WebUI dialog)
   1725→778 lines, -55%
   (Phase 4)
```

Per user's commit policy: NO `Co-authored-by` / AI attribution lines. Plain user-style messages.

If build green: include Phase 5 polish + Phase 6 QA log as commits 9–10.

If build still blocked: commit 1–8 only, deliver standalone artifact (commit 11) as `chore(maho-space-create): add dev-only standalone bundle harness for visual QA`. The dev stub + dev HTML get gitignored or scoped to `dev/` subdir.

---

## 4. Phase 7 (NEW, build-pass session) — End-to-End Verification

When build unblocks (user approves GN edits OR external sessions clear `MahoTabIdHelper` consumer deps):

1. Build: `python3 maho-chromium/build/scripts/build_maho.py` → exit 0
2. Launch isolated Maho with `--user-data-dir=/tmp/maho-e2e` to avoid main profile crash from omnibox_popup_closer race (Phase 3 IsVisible guard should suffice)
3. Run E1–E10 from §6.2 manually
4. Capture screenshots at each step for verification log
5. If any E1–E10 fails: file as bug + fix in our scope
6. Commit final integration verification

---

## 5. Out of Scope (Reaffirmed)

- GN dep additions (Group A `:maho_tab_id_helper`, Group B `:maho_space_create`) — user must explicitly approve before any GN edit
- Upstream Chromium source modifications beyond what's already in `apply_chromium_src_overrides.py` patterns
- Other concurrent opencode sessions' work
- Tests (per user's earlier "테스트는 하지 말 것")

---

## 6. Acceptance Summary

| Item | Status target |
|---|---|
| V1 Native form intact | ✅ already done |
| V2 Footer routing native | ✅ already done |
| V3 New WebUI surface mounts | ⏳ verify in §5 standalone |
| V4 New dialog wrapper exists | ✅ already done |
| V5 Native theme picker code removed | ✅ already done |
| V6 Mojo wired (handler ↔ dialog ↔ form) | ⏳ §6.2 deferred |
| V7 Live preview affects sidebar dot | ⏳ §6.2 deferred |
| V8 Cancel reverts | ⏳ §6.1 stub-verified, §6.2 real-verified deferred |
| V9 Done persists | ⏳ §6.1 stub-verified, §6.2 real-verified deferred |
| V10 Build green | ⏳ blocked on user GN approval |
| V11 No upstream edits | ✅ already done (only the registration in interface_binders, mirrors existing pattern) |
| V12 Visual diff < tolerance | ⏳ verify in §5.4 |

---

## 7. Effort + Risk

| Step | Effort | Risk |
|---|---|---|
| §5 Standalone visual parity | 30 min | Low — Oracle reviewed; CSS is self-contained |
| §6.1 Interaction QA | 30 min | Low — stub captures all calls observably |
| §6.3 Atomic commits | 15 min | Low — disk is clean, just `git add -p` per phase |
| §7 E2E (when unblocked) | 1–2 hours | Depends on # of E1–E10 issues found |

Total time-to-completion (excluding build wait): **~75 min**.

---

## 8. Plan Critique Pending

This plan should be reviewed by Momus before execution begins. Pass `path=.sisyphus/plans/theme-picker-completion-plan.md` to a `momus` task.
