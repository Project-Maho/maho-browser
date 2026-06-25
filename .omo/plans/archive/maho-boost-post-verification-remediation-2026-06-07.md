# Maho Boost — Post-Verification Remediation Plan

**Date**: 2026-06-07
**Status**: Draft — awaiting review
**Prerequisite**: `zen-boost-zap-silent-abort-root-cause-fix-2026-06-04.md` (completed)
**Triggered by**: Comprehensive verification of Boost system after the 6-fix Zap stability work

## Goal

Bring the entire Boost system from "Zap E2E works" to "production-ready, fully verified, no known regressions" by addressing the issues surfaced during comprehensive verification on 2026-06-07.

## Context

After 6 sequential root-cause fixes for Zap (pak inclusion, TS export strip, zap_dissolve loading, composedPath shadow piercing, DOMContentLoaded reinjection, INSERT OR REPLACE → UPSERT for FK preservation), the core Zap E2E flow now works including app-restart persistence.

A comprehensive verification on 2026-06-07 surfaced:

- ✅ **Working**: Zap apply, reload persistence, navigation persistence, app-restart persistence, subdomain isolation, different-domain isolation, shuffleBoost, font CSS injection, color filter injection.
- ❌ **Broken**: `disable boost` toggle has no effect; 10/18 C++ interactive tests fail (9 from one shared fixture race, 1 from pre-existing JS race in test environment).
- ⚠️ **Build hygiene**: `MahoSharePickerDelegate` ObjC class is linked into both `libbrowser_ui_views.dylib` and `libchrome_dll.dylib` causing per-launch objc warnings ("may cause spurious casting failures and mysterious crashes").
- ⚠️ **JS console**: WebGL `uniform1i: location is not from the associated program` and a `Cannot read properties of null (reading 'appendChild')` fire during zap (non-fatal but observable).
- ⏭️ **Unverified**: picker mode, `removeZapSelector` user flow, `commit/discard` branch, `reset/delete/export/import` (hidden in context menu), Custom CSS editor (CodeMirror), size override, case mode, smart invert, magic theme, multiple boosts per domain.

## Constraints & Preferences

- Build only via `python3 maho-chromium/build/scripts/build_maho.py` (workspace root).
- No GN file edits without explicit user approval (per workspace AGENTS.md).
- No deletion/skip of failing tests; fix the cause.
- Comments in code must be justified for crash/regression-prevention; no "agent memo" comments.
- Verbose log noise is acceptable during verification; remove temporary `eprintln!` traces when fixes land.
- All fixes must be verified live (CDP-driven UI flow + DB inspection where applicable), not by `lsp_diagnostics` alone.

---

## Scope

### In scope (this plan)

1. **Build hygiene** — eliminate `MahoSharePickerDelegate` duplicate-class warning.
2. **Functional regression** — make `zen-boost-disable` toggle actually disable the boost.
3. **JS error hygiene** — null-guard the WebGL `getUniformLocation` / `appendChild` paths.
4. **Test fixture repair** — fix the shared `OpenEditorAndWait` / `JsZapClickReachesInjectionHandler` race so the 9 dependent uitests recover at once.
5. **Unverified-feature smoke tests** — exercise each remaining boost editor API end-to-end and record pass/fail.

### Out of scope

- Redesigning the boost editor UI.
- Migrating away from `chrome-untrusted://maho-boost-editor`.
- Theme picker / Space create work (separate ADR).
- Subdomain matching policy decision (just record current behavior; no policy change here).

---

## Task Breakdown

Tasks are ordered by priority. Each task lists: target file(s), exact change, verification command, success criteria.

### Task 1 — `MahoSharePickerDelegate` duplicate-class fix (🔴 highest)

**File(s)**:
- `maho-chromium/browser/BUILD.gn` — `source_set("maho_theme_support")` block (line ~291) and the `is_mac` branch that adds `maho_notification_share_mac.{h,mm}` (line ~315–319).
- Optionally a new target.

**Change**:

The current `source_set("maho_theme_support")` is depended on by 6+ targets (sidebar, space_config, space_create, space_create views, browser, etc.) per `grep "//maho/browser:maho_theme_support" -- BUILD.gn`. GN inlines the `.mm` into every dependent's final library, so the same `@implementation MahoSharePickerDelegate` is compiled into both `libbrowser_ui_views.dylib` and `libchrome_dll.dylib` at component build time.

Two viable options — pick one with user input:

- **Option A (safer)**: Split out the ObjC bits into a sibling `static_library("maho_share_picker_mac")` that owns `maho_notification_share_mac.{h,mm}` and the `AppKit.framework` dependency. Move the `is_mac` `sources +=` block out of `maho_theme_support` and into the new target's own `sources` list. Add the new target to whatever single dependency edge actually needs it (likely the notifications path), then have other dependents continue to depend only on `maho_theme_support`.
- **Option B (single edge)**: Keep the file in `maho_theme_support` but verify only one of its dependent targets compiles into the boost / share path; mark the rest with `allow_circular_includes_from` or move the share invocation to a single location so the linker only pulls it once. Risk: more refactor needed in `maho_notification_overlay.cc` which currently `#include`s the share header.

Recommended: **Option A**. The `static_library` boundary is the correct scope for a self-contained ObjC class with framework deps.

**Verification**:

```bash
python3 maho-chromium/build/scripts/build_maho.py
# After build:
nm chromium/src/out/Default/libbrowser_ui_views.dylib | grep -E "T -\\[MahoSharePickerDelegate"
nm chromium/src/out/Default/libchrome_dll.dylib       | grep -E "T -\\[MahoSharePickerDelegate"
# Exactly one of these should print symbols; the other must print nothing.

# Then launch and confirm no objc warning:
./Maho.app/Contents/MacOS/Maho --user-data-dir=/tmp/maho-debug/profile_share 2>&1 | grep "Class MahoSharePickerDelegate"
# Expected: no output (zero matches).
```

**Success criteria**:

- `MahoSharePickerDelegate` defining symbols (`T`) appear in exactly one dylib.
- Per-launch objc warning is gone.
- Notification share sheet still opens correctly when the share button is invoked from `MahoNotificationOverlay` (manual smoke test).
- All passing C++ tests still pass.

**Risk**: This BUILD.gn change requires user approval per workspace AGENTS.md. Stop and ask before editing.

---

### Task 2 — `zen-boost-disable` toggle actually disables the boost (🟡)

**File(s)**:
- `maho-chromium/browser/resources/maho_boost/app.ts` — search for `zen-boost-disable` event wiring.
- Possibly `maho_boost_page_handler.cc` if a Mojo call is wired but inert.

**Observed bug** (from 2026-06-07 verification):

After applying zap + shuffle (which adds zap + font + color filter to the page), clicking `#zen-boost-disable` returns no effect:

- `getAttribute('enabled')` → `null` (no toggle attribute updated).
- Page `<style id="maho-boost-style">` still contains all CSS rules.
- `h1` still `display: none`.

This is consistent with the click handler being either missing or routing to a no-op.

**Change**:

1. Read the current `zen-boost-disable` handler in `app.ts`.
2. If it currently calls `pageHandler.exitZapMode()` only, replace with a flow that:
   - Calls `pageHandler.setActiveBoost(this.currentDomain, null)` to clear the active boost for the domain (which the existing `set_active_boost_persisted` path already supports).
   - `MahoBoostInjectionHandler::ReinjectForDomain` will then push empty CSS, which the existing `BuildBoostCssScript("")` branch already handles by removing `<style id="maho-boost-style">`.
3. The button visual state should toggle to "off" via the `enabled` attribute, mirroring the `zen-boost-zap` pattern (line ~593 in `app.ts`).
4. A second click should re-activate by calling `setActiveBoost(domain, this.currentBoost.id)`.

**Verification**:

```bash
# CDP script:
# 1. Apply boost (font + zap), confirm h1 hidden + maho-boost-style present.
# 2. Click #zen-boost-disable.
# 3. Within 2s: assert h1 display === 'block' AND maho-boost-style === null.
# 4. Click again: assert h1 display === 'none' AND maho-boost-style present again.
```

Use `/tmp/maho-debug/feature_test.py::test_disable_boost` as the starting harness, extend with assertions.

**Success criteria**:

- Disable toggles visually (button `enabled` attribute flips).
- Page CSS is removed on disable, re-added on re-enable.
- `domain_boost_state.active_boost_id` switches between NULL ↔ boost id correctly (verifiable via SQLite).

**Risk**: If `setActiveBoost(null)` permanently clears state, app restart might lose the boost. Verify by killing and restarting; expectation is that the boost row is still in `boosts` table and re-enable works.

---

### Task 3 — WebGL `uniform1i` null guard (🟡)

**File**: `maho-chromium/browser/resources/maho_boost_content_script/zap_dissolve.ts:283-284`

```ts
const loc = gl.getUniformLocation(this.program!, "u_DissolveTexture");
gl.uniform1i(loc, 0);
```

**Bug**: `getUniformLocation` returns `null` if the uniform does not exist or the program is not currently bound. Calling `uniform1i(null, 0)` is the source of the per-launch console warning. If the uniform name is wrong, the dissolve effect silently fails.

**Change**:

```ts
const loc = gl.getUniformLocation(this.program!, "u_DissolveTexture");
if (loc !== null) {
  gl.uniform1i(loc, 0);
} else {
  console.warn("[ZapDissolve] u_DissolveTexture uniform not found in program");
}
```

Apply the same null-check to other `getUniformLocation` callsites in the same file (line 328 and any siblings). Consider extracting a `safeUniformLocation(gl, program, name)` helper.

**Verification**:

```bash
# Run zap flow, capture page console:
grep "INVALID_OPERATION\|u_DissolveTexture" /tmp/maho-debug/*.log
# Expected: zero hits.
```

**Success criteria**:

- Page console shows no `INVALID_OPERATION: uniform1i` after applying zap.
- Dissolve animation still plays (visual smoke test).

**Risk**: Low. If the uniform was never being set, the dissolve was silently broken; the warn now surfaces it.

---

### Task 4 — Locate and fix the `appendChild on null` JS error (🟡)

**Files (candidates)**: Any `appendChild` call where the LHS could be `null`. Initial sweep:

- `selector_component.ts:75,80,98,210`
- `zap_overlay.ts:85,90,100,182,189,193`
- `zap_dissolve.ts:167`
- `app.ts:526,786,794,799,806,1240-1243`

**Approach**:

1. Reproduce the error with verbose logging (`Page.enable` + `Console.enable` via CDP).
2. Capture the script source URL + line number from the console event (currently the source field is empty: `""`, line 7 — likely an injected script).
3. Trace which `appendChild` call has a null LHS — most likely `zapList.appendChild(...)` or `this.shadowRoot!.appendChild(...)` after early teardown.
4. Add explicit null guard or precondition.

**Verification**:

```bash
# Apply zap, then exit, then re-apply zap. Console should be free of:
grep "Cannot read properties of null (reading 'appendChild')" /tmp/maho-debug/*.log
# Expected: zero hits.
```

**Success criteria**: Zero null-appendChild errors during a full apply→exit→re-apply cycle.

---

### Task 5 — `OpenEditorAndWait` / `JsZapClickReachesInjectionHandler` test fixture race (🟡)

**Files**:
- `maho-chromium/browser/ui/views/boost/maho_boost_behavior_interactive_uitest.cc:60-95` — `OpenEditorAndWait` helper, which 9 tests share.
- `maho-chromium/browser/ui/views/boost/maho_boost_lifecycle_interactive_uitest.cc:318` — `JsZapClickReachesInjectionHandler`.

**Observed**: All 5 BehaviorTest, 3 CodeMirrorTest, 1 DomEquivalenceTest tests fail at `ASSERT_TRUE(editor_wc)` returned by `OpenEditorAndWait`. Test logs show `[MahoBoost-trace] Editor opened for domain=127.0.0.1` (the controller did open) but `GetEditorWebContents(controller)` returns nullptr. The lifecycle test that DOES pass (`SameDomainReshowReuses`, `WebUIControllerHasValidController`, etc.) uses the same controller but reads `IsShowing()` rather than fetching `WebContents` directly.

**Hypothesis**: After `controller.ShowForActiveDomain(...)`, the boost editor's WebView is created but its WebContents pointer is not yet observable through whatever path `GetEditorWebContents` uses (likely walking the widget tree). A `RunUntil([&]() { return GetEditorWebContents(controller) != nullptr; })` would resolve the race.

**Change**:

1. Read `GetEditorWebContents` definition (likely at top of behavior_interactive_uitest.cc or a shared header).
2. In `OpenEditorAndWait`, replace:

```cpp
content::WebContents* editor_wc = GetEditorWebContents(controller);
if (!editor_wc) { return nullptr; }
```

with:

```cpp
content::WebContents* editor_wc = nullptr;
EXPECT_TRUE(base::test::RunUntil([&]() {
  editor_wc = GetEditorWebContents(controller);
  return editor_wc != nullptr;
})) << "Editor WebContents never became available";
if (!editor_wc) return nullptr;
```

3. For `JsZapClickReachesInjectionHandler`: read the test, identify what additional precondition (likely waiting for content script injection to complete) is needed; add the same `RunUntil` pattern.

**Verification**:

```bash
chromium/src/out/Default/interactive_ui_tests \
  --gtest_filter="*MahoBoost*:*BoostLifecycle*:*BoostBehavior*:*BoostCodemirror*:*BoostDom*" \
  --test-launcher-summary-output=/tmp/maho-debug/boost_test_summary_after_fix.json
# Expected: 18/18 PASS, 0 FAIL.
```

**Success criteria**: All 18 boost interactive tests pass, no regression to other tests.

**Risk**: Low. Adding a wait-for-condition is conservative; if the race is more fundamental (e.g. WebContents is created on a deferred task), the test may need a longer timeout or a different observable.

---

### Task 6 — Unverified-feature smoke tests (🟢)

**Goal**: Drive each remaining boost editor API end-to-end and record results. No fixes here — just discovery of additional bugs.

**Targets**:

| API | Test approach |
|-----|--------------|
| `pageHandler.commitBoost` | Make a change → close editor → SQLite shows `change_was_made=1`, boost preserved. |
| `pageHandler.discardBoost` | Open fresh boost → don't change anything → close → SQLite has no row for that boost. |
| `pageHandler.deleteBoost` | Click `zen-boost-edit-delete` (from context menu) → SQLite row removed. |
| `pageHandler.enterPickerMode` / `exitPickerMode` | Find trigger UI (likely `zen-boost-css-picker` or via Inspector) → click → page shows selector overlay → click element → returned selector flows back to editor. |
| `pageHandler.removeZapSelector` | Apply zap → find unzap UI → click → CSS rule removed → element visible. |
| `pageHandler.resetBoost` | Open context menu → `zen-boost-edit-reset` → all settings revert to defaults. |
| `pageHandler.exportBoost` | `zen-boost-save` → file dialog → JSON file written. |
| `pageHandler.importBoost` | `zen-boost-load` → file picker → boost loaded. |
| `pageHandler.openInspector` | Trigger → DevTools-like inspector opens for the target page. |
| Size override | Click `zen-boost-size` → cycles k50/k75/k100/k125/k150/k200 → CSS `font-size` rule reflects. |
| Case mode | Click `zen-boost-case` → cycles None/Upper/Lower/Capitalize → CSS `text-transform` rule reflects. |
| Magic theme | Click `zen-boost-magic-theme` → CSS color filter changes. |
| Smart invert | Click `zen-boost-invert` → page receives invert filter. |
| Custom CSS via CodeMirror | Click `zen-boost-code` → editor opens → type CSS → page receives the typed CSS. |
| Multiple boosts per domain | Create temp boost → commit → create second temp boost → both visible in some list. |

**Deliverable**: A new `/tmp/maho-debug/boost_full_feature_audit.json` with one entry per API: `{api, status: PASS/FAIL/SKIP, evidence_path}`. Convert PASS findings into regression coverage; convert FAIL findings into new tasks/sub-plans.

---

## Dependencies & Order

```
Task 1 (build hygiene) ──► (no other task depends on this; do first while waiting on review)
Task 2 (disable toggle) ──► Task 6 (smoke tests rely on a clean toggle)
Task 3 (WebGL guard) ──► (independent)
Task 4 (appendChild guard) ──► (independent, but Task 3 may share helper plumbing)
Task 5 (test fixture) ──► (must precede any future test additions)
Task 6 (smoke tests) ──► depends on Tasks 2-4 for clean baseline; runs last
```

Tasks 1, 3, 4, 5 can be parallelized after Task 1 review approval. Task 2 should be done after Task 5 so the test fixture can validate it.

---

## Verification Protocol (post-each-task)

1. `lsp_diagnostics` clean on changed files.
2. `python3 maho-chromium/build/scripts/build_maho.py` → exit 0.
3. Launch Maho with the same profile dir each time (`/tmp/maho-debug/profile_remediation`).
4. Run the verification command listed under the relevant task.
5. Re-run `interactive_ui_tests --gtest_filter="*MahoBoost*"` after Task 5; expect 18/18 PASS.
6. Re-run the full `feature_test.py` and `comprehensive_test.py` harnesses; record deltas.
7. Inspect SQLite DB (`/tmp/maho-debug/profile_remediation/MahoCore/maho.db`) for `boosts` and `domain_boost_state` consistency.
8. After all tasks: kill + restart Maho with the same profile, confirm zap and any newly-set boost data persists.

---

## Out of Scope (Explicit)

- Redesigning the boost editor's information architecture (rename/edit/context-menu placement).
- Subdomain matching policy change (`example.com` ↔ `www.example.com` currently isolated; that is recorded behavior, not in-scope to change here).
- Migrating off `INSERT OR REPLACE` in tables other than `boosts` (audit needed but out-of-scope).
- The pre-existing TODO(maho) comments in `maho_boost_lifecycle_interactive_uitest.cc:100,134` (magic-theme assertions) — separate plan.

---

## Open Questions

1. **Task 1 — Option A vs B**: which BUILD.gn refactor does the user prefer? Option A (split target) is recommended.
2. **Task 2 — disable semantics**: should `disable` clear `active_boost_id` (current state-clear semantics) or just set a per-boost `enabled=false` flag (additive)? Behavior affects what app restart does. Decision needed before Task 2 implementation.
3. **Task 6 — multiple boosts per domain**: is this a planned feature or accidental? UI for managing multiple boosts not surfaced in current editor.

---

## Success Criteria (whole plan)

- Zero per-launch ObjC warnings.
- Zero JS console errors during normal zap flow.
- 18/18 boost interactive tests pass.
- `disable boost` works as designed (see Task 2).
- Full feature audit recorded; every PASS confirmed reproducible; every FAIL has a follow-up task.
- App restart preserves boost state in every tested scenario.
