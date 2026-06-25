# Zen Boost — Runtime Verification

**Date**: 2026-05-30
**Author**: Sisyphus
**Status**: Ready for delegation
**Predecessor**: `.sisyphus/plans/zen-boost-implementation-record-2026-05-29.md` (deferred items 1–6)
**Depends on**: Plan A (`zen-boost-n3-grd-content-script-loading-2026-05-30.md`) must be complete for WU-3/WU-4/WU-5 zap tests

## Goal

Cover the 7 deferred runtime items from the implementation record (criteria #3, #5, #6, #7, #9 + gaps N6, N7, N8) plus produce a manual QA checklist. Deliver:
- Interactive C++ uitests for DOM equivalence, behavioral round-trip, lifecycle, and CodeMirror
- Python screenshot diff harness (SSIM-based)
- Rust unit tests for temp boost lifecycle
- Manual QA checklist document

## Acceptance Criteria

- [ ] All new C++ test files compile (registered in BUILD.gn test target)
- [ ] Rust tests pass: `cd maho && cargo test --package maho-core`
- [ ] Screenshot diff script runs without errors (even if SSIM < 0.85 initially — the harness must be functional)
- [ ] Manual QA checklist document exists at `.sisyphus/qa/zen-boost-manual-qa-checklist-2026-05-30.md`
- [ ] Each deferred item from implementation record maps to at least one WU
- [ ] No existing tests broken by additions
- [ ] `lsp_diagnostics` clean on all new/modified files

## Non-Goals

- Pixel-identical visual parity (CSS injection ≠ native Gecko paint — known, accepted divergence)
- Smart-invert engine-level parity (filter approximation only)
- Workspace gradient integration for magic-theme (architectural dependency)
- CI pipeline integration (test infra lives locally; CI setup is a separate plan)
- Achieving SSIM ≥ 0.85 in this plan (goal is harness creation; tuning is follow-up)
- Cross-platform test execution (macOS only)

## Constraints

- `.build.lock` serialization: build/test steps take the lock
- GN file edits for new test targets: **requires user permission** — WU-1/3/4/5 add new `_interactive_uitest.cc` files which must be registered in a `BUILD.gn` test target. Plan documents the exact GN additions needed; executor must confirm with user before editing GN.
- Reference screenshots (`maho/reference_screenshots/arc-boost.png`, `arc-boost-code.png`) must be present on disk (not git-committed per gap N5)
- Python dependencies (`scikit-image`, `numpy`, `Pillow`) must be available — install via `pip3 install scikit-image` if missing
- Plan A (N3 GRD loading) must be merged before zap-related behavioral tests can pass at runtime
- No commits (per session policy unless explicitly requested)

## Parallelism Topology

```
              ┌──────────┐
              │   WU-0   │
              │Entrypoint│
              │  Verify  │
              └─────┬────┘
                    │ (gate — documents findings for WU-6)
     ┌──────┬──────┼──────┬──────┬──────┬──────┐
     ▼      ▼      ▼      ▼      ▼      ▼      ▼
  ┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐
  │ WU-1 ││ WU-2 ││ WU-3 ││ WU-4 ││ WU-5 ││ WU-6 ││ WU-7 │
  │  DOM ││Screen││Behav.││Lifec.││CodeM.││Manual││ Rust │
  │Equiv.││ Diff ││Round ││ ycle ││ irror││  QA  ││Tests │
  └──┬───┘└──┬───┘└──┬───┘└──┬───┘└──┬───┘└──┬───┘└──┬───┘
     │       │       │       │       │       │       │
     └───────┴───────┴───────┼───────┴───────┴───────┘
                             ▼
                        ┌────────┐
                        │  WU-8  │
                        │ Build  │
                        │ Verify │
                        └────────┘
```

- **WU-0**: Gate — determines entrypoint (already resolved; passes findings to WU-6)
- **WU-1–7**: All in parallel (touch disjoint files)
- **WU-8**: Final gate after all others — build + run

## Work Units

---

### WU-0: Verify boost editor entrypoint

**Severity**: Trivial
**Owner**: `task(category="quick", load_skills=[], ...)`
**Estimated effort**: 5 minutes (already resolved)

#### Files

| Path | Edit type |
|---|---|
| (none — read-only verification) | grep/read |

#### Spec

Determine and document the actual boost editor entrypoint:
- Keyboard shortcut binding
- Command palette action
- UI buttons that call `ShowForActiveDomain`

**RESOLVED**:
- **Keyboard**: `Cmd+Opt+B` (macOS) / `Ctrl+Alt+B` (other) — `maho/crates/maho-core/src/shortcut_manager.rs:415`
- **Command palette**: action `"open_boost_editor"` — `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc:462`
- **Page info UI**: `maho-chromium/browser/ui/site_control/maho_page_info_ui.cc:134`

The implementation record's note about "Cmd+Opt+B = bookmarks" refers to Chrome's default binding; Maho's shortcut manager overrides it for `open_boost_editor`.

#### Verification

Output: documented in this plan (above). No code changes needed. Pass findings to WU-6 (manual QA checklist).

#### Risk

None.

---

### WU-1: DOM equivalence interactive uitest

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 60–90 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/ui/views/boost/maho_boost_dom_equivalence_interactive_uitest.cc` | New file |
| `maho-chromium/browser/ui/views/boost/BUILD.gn` | Add to test target (**USER PERMISSION REQUIRED**) |

#### Spec

Create an `InProcessBrowserTest` that:

1. Navigates the main browser to `embedded_test_server()->GetURL("/title1.html")`
2. Triggers boost editor via:
   ```cpp
   auto& controller = maho::MahoBoostWindowController::GetForBrowser(
       browser(), browser()->profile());
   controller.ShowForActiveDomain(
       browser()->tab_strip_model()->GetActiveWebContents());
   ```
3. Waits for the boost editor WebContents to finish loading using `base::test::RunUntil` polling `IsDocumentOnLoadCompletedInPrimaryMainFrame()`
4. Executes JavaScript in the boost editor's main frame to extract DOM contract:
   ```js
   (function() {
     const ids = [...document.querySelectorAll('[id]')].map(e => e.id);
     const classes = [...document.querySelectorAll('[class]')]
       .flatMap(e => [...e.classList]);
     return JSON.stringify({ids: [...new Set(ids)], classes: [...new Set(classes)]});
   })()
   ```
5. Parses the JSON result and asserts the following Zen-mandated DOM IDs exist:
   - `boost-editor`, `color-section`, `typography-section`, `zap-section`
   - `dot-primary`, `dot-secondary`, `color-wheel`
   - `font-grid`, `case-cycle-btn`, `size-cycle-btn`
   - `code-editor-container`, `custom-css-editor`
   - `advanced-color-toggle`, `advanced-color-popup`
   - `magic-theme-toggle`, `smart-invert-toggle`
6. Asserts key structural classes:
   - `boost-mode` OR `code-mode` (one must be present)
   - `color-wheel-container`, `font-button`, `zap-controls`

#### Test pattern (reference: `maho_ai_popup_lifetime_interactive_uitest.cc`)

```cpp
#include "base/test/run_until.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"

class MahoBoostDomEquivalenceTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }
};

IN_PROC_BROWSER_TEST_F(MahoBoostDomEquivalenceTest, ZenMandatedIdsPresent) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));

  auto& controller = maho::MahoBoostWindowController::GetForBrowser(
      browser(), browser()->profile());
  controller.ShowForActiveDomain(
      browser()->tab_strip_model()->GetActiveWebContents());

  // Wait for editor to load...
  // Execute JS to extract DOM...
  // Assert IDs present...
}
```

#### GN addition needed (USER PERMISSION GATE)

```gn
# In maho-chromium/browser/ui/views/boost/BUILD.gn (or chrome/test/BUILD.gn)
source_set("boost_interactive_uitests") {
  testonly = true
  sources = [
    "maho_boost_dom_equivalence_interactive_uitest.cc",
    "maho_boost_behavior_interactive_uitest.cc",
    "maho_boost_lifecycle_interactive_uitest.cc",
    "maho_boost_codemirror_interactive_uitest.cc",
  ]
  deps = [
    "//base/test:run_until",
    "//chrome/test:test_support",
    "//chrome/test/base:in_process_browser_test",
    "//content/public/test:test_support",
    "//maho/browser/ui/views/boost",
    # ... additional deps as needed
  ]
}
```

#### Verification

```bash
# After GN registration (with user permission):
autoninja -C out/Default maho_boost_dom_equivalence_interactive_uitest
./out/Default/maho_boost_dom_equivalence_interactive_uitest \
  --gtest_filter=MahoBoostDomEquivalenceTest.ZenMandatedIdsPresent
```

#### Risk

- **WebUI URL**: Boost editor uses `chrome://maho-boost` (confirmed from AGENTS.md WebUI table). Test must access the popup's WebContents, not the main browser tab.
- **Async load timing**: `base::test::RunUntil` with 10s timeout handles slow WebUI load. If WebUI has deferred module loading, may need longer timeout.
- **GN target creation**: Requires adding to test BUILD.gn. **User permission gate.**
- **Widget WebContents access**: Need to find how to get the boost editor's WebContents from the controller. May need to expose a getter or iterate popup browsers.

---

### WU-2: Screenshot diff harness

**Severity**: Medium
**Owner**: `task(category="unspecified-high", load_skills=[], ...)`
**Estimated effort**: 45–60 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho/scripts/boost-screenshot-diff.py` | New file |
| `maho/scripts/requirements-boost-diff.txt` | New file |

#### Spec

Create a Python script that:

1. **Validates prerequisites**:
   - Checks `scikit-image`, `numpy`, `Pillow` importable
   - Checks reference PNG exists at specified path
   - Checks `Maho.app` exists at `chromium/src/out/Default/Maho.app`

2. **Launches Maho.app**:
   ```python
   import subprocess
   proc = subprocess.Popen([
       'chromium/src/out/Default/Maho.app/Contents/MacOS/Maho',
       '--no-first-run', '--disable-sync', 'about:blank'
   ])
   time.sleep(5)  # Wait for startup
   ```

3. **Triggers boost editor** via AppleScript:
   ```python
   subprocess.run(['osascript', '-e', '''
     tell application "Maho" to activate
     delay 0.5
     tell application "System Events"
       keystroke "b" using {command down, option down}
     end tell
   '''])
   time.sleep(2)  # Wait for editor to render
   ```

4. **Positions window** using AppleScript Accessibility:
   ```python
   subprocess.run(['osascript', '-e', '''
     tell application "System Events"
       tell process "Maho"
         set position of window 1 to {100, 100}
       end tell
     end tell
   '''])
   ```

5. **Captures screenshot region**:
   ```python
   # Boost mode: 184×582 (offset from window position)
   # Code mode: 452×582
   subprocess.run(['screencapture', '-R100,100,184,582', '/tmp/boost-capture.png'])
   ```

6. **Computes SSIM**:
   ```python
   from skimage.metrics import structural_similarity as ssim
   from skimage.io import imread
   from skimage.transform import resize
   import numpy as np

   ref = imread(args.reference)
   cap = imread('/tmp/boost-capture.png')

   # Handle retina 2x: resize to match reference dimensions
   if cap.shape[0] > ref.shape[0] * 1.5:
       cap = resize(cap, ref.shape[:2], anti_aliasing=True)
       cap = (cap * 255).astype(np.uint8)

   # Resize reference to capture dimensions if needed
   if ref.shape != cap.shape:
       ref = resize(ref, cap.shape[:2], anti_aliasing=True)
       ref = (ref * 255).astype(np.uint8)

   score = ssim(ref, cap, channel_axis=2)
   ```

7. **Reports result**:
   - PASS: SSIM ≥ 0.85
   - WARN: 0.70 ≤ SSIM < 0.85
   - FAIL: SSIM < 0.70

8. **CLI interface**:
   ```
   python3 maho/scripts/boost-screenshot-diff.py \
     --reference maho/reference_screenshots/arc-boost.png \
     --mode boost \
     [--timeout 10]
   ```

#### `requirements-boost-diff.txt`

```
scikit-image>=0.21
numpy>=1.24
Pillow>=10.0
```

#### Known limitations

- **Window positioning**: macOS window position is non-deterministic without Accessibility API. Script positions explicitly via AppleScript.
- **Retina displays**: Reference `arc-boost.png` is 187×590. Capture on retina = 2x. Script normalizes by resizing.
- **Non-headless**: Requires a display session. Cannot run in CI without virtual display.
- **Coordinate drift**: Actual capture region depends on window chrome. May need manual calibration on first run.

#### Verification

```bash
pip3 install -r maho/scripts/requirements-boost-diff.txt
python3 maho/scripts/boost-screenshot-diff.py \
  --reference maho/reference_screenshots/arc-boost.png \
  --mode boost
# Expected: prints SSIM score, exits 0 on PASS/WARN, exits 1 on FAIL
```

#### Risk

- **AppleScript flakiness**: Keystroke simulation may fail if Maho isn't focused. Mitigation: `activate application "Maho"` before keystroke.
- **Window coordinates vary by display**: Script includes `--offset` flag for manual adjustment. Accept SSIM < 0.85 initially.
- **scikit-image not installed**: Script checks and prints install instructions if missing (graceful exit with message).
- **Maho crashes on startup**: Script checks process alive after 5s sleep; reports error if PID gone.

---

### WU-3: Behavioral round-trip interactive uitest

**Severity**: High
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 90–120 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/ui/views/boost/maho_boost_behavior_interactive_uitest.cc` | New file |
| `maho-chromium/browser/ui/views/boost/BUILD.gn` | Add to test target (**USER PERMISSION REQUIRED**) |

#### Spec

An `InProcessBrowserTest` covering 5 behavioral round-trips:

**Test 1: `ColorPickerDotDrag`**
- Open boost editor for `title1.html`
- Execute JS in editor WebContents:
  ```js
  const dot = document.getElementById('dot-primary');
  const rect = dot.getBoundingClientRect();
  dot.dispatchEvent(new PointerEvent('pointerdown', {clientX: rect.x+5, clientY: rect.y+5, bubbles: true}));
  document.dispatchEvent(new PointerEvent('pointermove', {clientX: rect.x+30, clientY: rect.y+30, bubbles: true}));
  document.dispatchEvent(new PointerEvent('pointerup', {bubbles: true}));
  ```
- Wait 500ms for Mojo round-trip
- Execute JS on **target page** WebContents:
  ```js
  const style = document.getElementById('maho-boost-style');
  style ? style.textContent : '';
  ```
- Assert: result contains `hue-rotate` or `filter` (color CSS was injected)

**Test 2: `MagicThemeToggle`**
- Open boost editor
- Execute JS: `document.getElementById('magic-theme-toggle').click()`
- Execute JS to read state: `document.getElementById('magic-theme-toggle').classList.contains('active')`
- Assert: true
- Click again → assert: false

**Test 3: `CaseCycleFourModes`**
- Open boost editor
- Click `#case-cycle-btn` 4 times sequentially
- After each click, read the button's `data-mode` attribute or text content
- Assert cycle: `none` → `uppercase` → `lowercase` → `capitalize` → `none`
- Verify on target page: `text-transform` CSS property matches

**Test 4: `SizeCycleFiveModes`**
- Open boost editor
- Click `#size-cycle-btn` 5 times sequentially
- After each click, read displayed percentage text
- Assert cycle: `100%` → `110%` → `125%` → `150%` → `90%` → `100%`
- Verify on target page: zoom level changed (via `WebContents::GetZoomPercent()` or equivalent)

**Test 5: `ZapModeRoundTrip`**
- Open boost editor
- Click zap button to enter zap mode
- Wait for content scripts to inject (poll for `window.__mahoBoost` on target page)
- Execute JS on target page to simulate element selection:
  ```js
  window.__mahoBoost.simulateZap && window.__mahoBoost.simulateZap('div.content');
  ```
  (Or dispatch events matching the selector component's listener)
- Assert: editor shows selector in zap list
- Click "Done" button
- Assert: selector persisted in boost state (query via JS in editor)

#### Verification

```bash
autoninja -C out/Default maho_boost_behavior_interactive_uitest
./out/Default/maho_boost_behavior_interactive_uitest --gtest_filter=*
```

#### Risk

- **Mojo state query**: Need access to page handler instance or DOM state to verify. Use `content::ExecuteJavaScriptForTests` on both editor and target WebContents.
- **Event simulation timing**: `base::test::RunUntil` with state predicates handles async updates. 500ms–2s delays for Mojo round-trips.
- **Zap mode depends on Plan A**: Content scripts must be loaded via GRD for zap to work. Test should `GTEST_SKIP()` gracefully if content scripts fail to inject.
- **Editor WebContents access**: The controller must expose the editor's WebContents for JS execution. May need to find it via `BrowserWindow` iteration or expose a getter.

---

### WU-4: Lifecycle interactive uitest

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 60–90 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/ui/views/boost/maho_boost_lifecycle_interactive_uitest.cc` | New file |
| `maho-chromium/browser/ui/views/boost/BUILD.gn` | Add to test target (**USER PERMISSION REQUIRED**) |

#### Spec

**Test 1: `CreateTempDiscardNoPersistence`**
- Navigate to `example.com` (via embedded test server)
- Open boost editor (creates temp boost)
- Close editor without committing — press Escape or destroy widget
- Assert: `controller.IsShowing() == false`
- Reopen editor for same domain
- Assert: boost state is fresh (no prior changes persisted — color at zen defaults)

**Test 2: `CreateTempCommitPersistence`**
- Open boost editor
- Make a change: execute JS `document.getElementById('magic-theme-toggle').click()`
- Close editor normally (simulate window close which triggers commit path)
- Wait for close: `base::test::RunUntil([&] { return !controller.IsShowing(); })`
- Reopen editor for same domain
- Assert: magic-theme toggle shows active state (change was persisted)

**Test 3: `TabSwitchClosesEditor`**
- Open boost editor for Tab A (index 0)
- Add a second tab: `chrome::AddTabAt(browser(), GURL("about:blank"), -1, true)`
- Switch to Tab B: `browser()->tab_strip_model()->ActivateTabAt(1)`
- Assert: `!controller.IsShowing()` (editor closed due to tab switch)

**Test 4: `NavigationClosesEditor`**
- Open boost editor for `a.example.com`
- Navigate parent tab to `b.example.com` (different domain)
- Assert: `!controller.IsShowing()` (per `ShowForActiveDomain` line 232 — different domain kills editor)

**Test 5: `SameDomainReshowReuses`**
- Open boost editor for `example.com`
- Hide editor: `controller.Hide()`
- Assert: `!controller.IsShowing()`
- Call `controller.ShowForActiveDomain(active_tab)` again (same domain)
- Assert: `controller.IsShowing()` (reuses existing, no new widget created)

#### Verification

```bash
autoninja -C out/Default maho_boost_lifecycle_interactive_uitest
./out/Default/maho_boost_lifecycle_interactive_uitest --gtest_filter=*
```

#### Risk

- **Persistence layer access**: Tests verify state by re-opening editor and checking DOM. Alternative: access `MahoCoreHolder` to query boost state directly.
- **Timing**: Widget show/hide is async. Use `base::test::RunUntil` with `!controller.IsShowing()` predicate and 5s timeout.
- **Tab close race**: `ActivateTabAt` triggers `TabStripModelObserver` which fires `OnTabStripModelChanged`. Editor close is async via observation callback.

---

### WU-5: CodeMirror round-trip interactive uitest

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 45–60 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/ui/views/boost/maho_boost_codemirror_interactive_uitest.cc` | New file |
| `maho-chromium/browser/ui/views/boost/BUILD.gn` | Add to test target (**USER PERMISSION REQUIRED**) |

#### Spec

**Test 1: `CodeModeResize`**
- Open boost editor (initial size 184×582)
- Verify initial width: `controller.GetWidget()->GetBounds().width()` approximately 184 (±10 for frame)
- Trigger code mode switch via JS: `document.querySelector('[data-mode="code"]').click()`
- Wait for resize: `base::test::RunUntil` polling widget width
- Assert: widget width approximately 452 (±10 for frame)

**Test 2: `CssPersistenceRoundTrip`**
- Open boost editor → switch to code mode
- Execute JS in editor to set CodeMirror content:
  ```js
  // Access CodeMirror 6 view instance
  const view = document.querySelector('.cm-editor').cmView?.view;
  if (view) {
    view.dispatch({
      changes: {from: 0, to: view.state.doc.length, insert: 'body { color: red; }'}
    });
  }
  ```
- Close editor (triggers commit with customCss)
- Reopen editor for same domain → switch to code mode
- Execute JS to read CodeMirror content:
  ```js
  document.querySelector('.cm-editor').cmView?.view?.state?.doc?.toString() || ''
  ```
- Assert: result equals `body { color: red; }`

**Test 3: `CodeMirrorMounted`**
- Open boost editor → switch to code mode
- Wait 1s for async CM6 initialization
- Execute JS: `document.querySelector('.cm-editor') !== null`
- Assert: true
- Execute JS: `document.querySelector('.cm-content') !== null` (inner editable area)
- Assert: true

#### Verification

```bash
autoninja -C out/Default maho_boost_codemirror_interactive_uitest
./out/Default/maho_boost_codemirror_interactive_uitest --gtest_filter=*
```

#### Risk

- **CodeMirror load timing**: CM6 initializes asynchronously. Poll for `.cm-editor` presence with 5s timeout.
- **Widget resize assertion**: May need `base::RunLoop().RunUntilIdle()` after mode switch for layout pass.
- **CM6 internal API**: `.cmView.view` is internal. If not accessible, fallback to setting content via simulated keyboard input (less precise but framework-independent).
- **Mode switch mechanism**: Need to verify what DOM element triggers the code mode. Could be a tab, button, or overlay click. Executor must inspect `app.ts` for the switch handler.

---

### WU-6: Manual QA checklist

**Severity**: Low
**Owner**: `task(category="writing", load_skills=[], ...)`
**Estimated effort**: 30 minutes

#### Files

| Path | Edit type |
|---|---|
| `.sisyphus/qa/zen-boost-manual-qa-checklist-2026-05-30.md` | New file |

#### Spec

Produce a step-by-step manual QA document with the following structure:

**Header**: Date, tester name placeholder, Maho build version, macOS version

**Prerequisites**:
- `Maho.app` built and launchable
- Internet connection (for navigating to test sites)
- macOS with Cmd+Opt+B available

**18 Test Scenarios** (each with: Steps, Expected Behavior, Pass/Fail criteria):

| # | Scenario | Key assertion |
|---|----------|---------------|
| 1 | Launch + Navigate | Maho opens, loads a page |
| 2 | Editor Open | Cmd+Opt+B → editor appears at 184×582 |
| 3 | Color Picker | Drag dot → page colors change real-time |
| 4 | Advanced Colors | Toggle → popup with contrast/brightness/saturation |
| 5 | Magic Theme | Toggle → button state changes |
| 6 | Smart Invert | Toggle → page inverts (images/videos exempt) |
| 7 | Font Selection | Click font → page font changes |
| 8 | Case Cycle | Click → cycles None/Upper/Lower/Capitalize |
| 9 | Size Cycle | Click → zoom changes (100/110/125/150/90) |
| 10 | Zap Mode | Enter → hover highlights → click zaps → Done |
| 11 | Picker Mode | Enter → hover → click returns selector |
| 12 | Code Mode | Tab → resize 452×582 → CM6 loads → type CSS |
| 13 | Commit | Close → reopen → changes persisted |
| 14 | Discard | Discard button → changes lost |
| 15 | Tab Switch Close | Switch tabs → editor closes |
| 16 | Navigation Close | Navigate other domain → editor closes |
| 17 | Multi-Domain | Different domains have separate boosts |
| 18 | Export/Import | Export → file → Import elsewhere → applied |

**Footer**: Known limitations section citing gaps #1–#8 from implementation record.

#### Verification

File exists at `.sisyphus/qa/zen-boost-manual-qa-checklist-2026-05-30.md` with all 18 scenarios.

#### Risk

None.

---

### WU-7: Rust unit tests for temp boost lifecycle

**Severity**: Low
**Owner**: `task(category="quick", load_skills=[], ...)`
**Estimated effort**: 20–30 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho/crates/maho-core/tests/test_batch_1.rs` | Append tests |

#### Spec

**IMPORTANT**: Before writing tests, the executor MUST read `maho/crates/maho-core/src/boost_manager.rs` to verify actual method signatures. The tests below use hypothesized signatures based on the plan's context gathering. Adapt to actual API.

Add the following tests to `test_batch_1.rs`:

```rust
// --- Boost lifecycle tests ---

#[test]
fn test_boost_create_temp_produces_valid_boost() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("example.com".to_string());
    assert_eq!(boost.domain, "example.com");
    assert!(!boost.id.as_str().is_empty());
    assert!(!boost.color.color_boost_enabled);
    assert_eq!(boost.color.magic_theme, false);
    assert_eq!(boost.typography.case_mode, CaseMode::None);
    assert_eq!(boost.typography.size_mode, SizeMode::K100);
    assert!(boost.zap_selectors.is_empty());
    assert!(boost.custom_css.is_empty());
}

#[test]
fn test_boost_commit_persists_state() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("example.com".to_string());
    let id = boost.id.clone();

    // Simulate update
    let update = BoostUpdate {
        magic_theme: Some(true),
        ..Default::default()
    };
    mgr.update_boost(&id, update);
    mgr.commit_boost(&id);

    // Verify persisted
    let retrieved = mgr.get_boost(&id).unwrap();
    assert!(retrieved.color.magic_theme);
}

#[test]
fn test_boost_discard_removes_temp() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("example.com".to_string());
    let id = boost.id.clone();

    mgr.discard_boost(&id);
    assert!(mgr.get_boost(&id).is_none());
    assert!(mgr.list_for_domain("example.com").is_empty());
}

#[test]
fn test_boost_lifecycle_full_roundtrip() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("test.org".to_string());
    let id = boost.id.clone();

    let update = BoostUpdate {
        case_mode: Some(CaseMode::Uppercase),
        size_mode: Some(SizeMode::K125),
        custom_css: Some("body { color: red; }".to_string()),
        ..Default::default()
    };
    mgr.update_boost(&id, update);
    mgr.commit_boost(&id);

    let boosts = mgr.list_for_domain("test.org");
    assert_eq!(boosts.len(), 1);
    assert_eq!(boosts[0].typography.case_mode, CaseMode::Uppercase);
    assert_eq!(boosts[0].typography.size_mode, SizeMode::K125);
    assert_eq!(boosts[0].custom_css, "body { color: red; }");
}

#[test]
fn test_boost_discard_after_update_loses_changes() {
    let mut mgr = BoostManager::new();
    let boost = mgr.create_temp("discard.com".to_string());
    let id = boost.id.clone();

    let update = BoostUpdate {
        magic_theme: Some(true),
        custom_css: Some("h1 { display: none; }".to_string()),
        ..Default::default()
    };
    mgr.update_boost(&id, update);
    mgr.discard_boost(&id);

    // Boost is gone — cannot retrieve
    assert!(mgr.get_boost(&id).is_none());
    assert!(mgr.list_for_domain("discard.com").is_empty());
}
```

**Import requirements** (add at top of test file if not already present):
```rust
use maho_core::boost_manager::BoostManager;
use maho_types::boost::{BoostUpdate, CaseMode, SizeMode};
```

#### Verification

```bash
cd maho && cargo test --package maho-core -- test_boost_create_temp test_boost_commit test_boost_discard test_boost_lifecycle
```

All 5 tests must pass. If any fail due to API mismatch, executor adapts to actual method signatures.

#### Risk

- **API mismatch**: `BoostManager` may use different method names (e.g., `create_temp` might be `create_temporary` or take additional params). Executor reads `boost_manager.rs` public API first.
- **Import paths**: `BoostUpdate`, `CaseMode`, `SizeMode` come from `maho_types::boost`. `BoostManager` from `maho_core::boost_manager`. Verify module visibility.
- **`BoostUpdate` fields**: The struct may use different field names. Cross-reference with `maho-types/src/boost.rs`.
- **Test isolation**: Each test creates its own `BoostManager::new()` — no shared state issues.

---

### WU-8: Build verification gate

**Severity**: Mandatory
**Owner**: Direct (no delegation — orchestrator runs this)
**Estimated effort**: 15–30 minutes (mostly build wall time)

#### Steps

1. Wait for WU-1–7 to all report success.
2. `cd maho && cargo test --workspace` — all Rust tests green (WU-7 included).
3. `lsp_diagnostics` on all new C++ files.
4. **With user permission for GN edits**: register new uitest files in `BUILD.gn`.
5. `python3 maho-chromium/build/scripts/build_maho.py` — exit 0.
6. Run uitests (WU-1, WU-3, WU-4, WU-5) against built binary.
7. Run screenshot diff (WU-2) — report SSIM score (informational, not gating).
8. Walk through manual QA checklist (WU-6) items 1–5 as smoke test.

#### On failure

| Failure | Action |
|---------|--------|
| Rust test fail | Fix imports/method signatures in test_batch_1.rs; re-run cargo test |
| C++ compile fail | Check includes, InProcessBrowserTest base class, missing deps |
| GN registration fail | Verify source_set structure; add missing deps |
| Uitest runtime fail | Increase RunUntil timeout; check WebUI URL correctness |
| Uitest assertion fail | Check DOM IDs/classes against actual rendered page; update assertions |
| Screenshot SSIM < 0.85 | Expected initially; document baseline score for tuning |

---

## §6 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| GN test target registration blocked (user permission) | Medium | High (can't compile C++ tests) | Document exact GN additions in WU-1 spec; executor asks user before editing |
| Boost editor WebUI URL wrong | Low | High (tests navigate wrong URL) | Confirmed `chrome://maho-boost` from AGENTS.md WebUI table |
| `base::test::RunUntil` timeout on slow machines | Medium | Medium (flaky tests) | Use generous 10s timeouts; document as potential flake source |
| Screenshot diff infeasible without display session | High | Medium (WU-2 can't run headless) | Accept as local-only tool; document limitation in script header |
| Content script injection fails (Plan A not merged) | Medium | Medium (WU-3 zap test fails) | Gate WU-3 zap sub-test on Plan A; use `GTEST_SKIP()` if scripts not present |
| `BoostManager` API differs from expected signatures | Medium | Low (test code needs adaptation) | Executor reads `boost_manager.rs` first; adapts method calls |
| Reference screenshots not at expected paths | Low | Medium (WU-2 fails) | Script checks existence; prints clear error with expected path |
| Editor WebContents not accessible from test | Medium | High (WU-1/3/4/5 blocked) | May need to expose getter on controller or iterate widget WebContents |
| CodeMirror `.cmView.view` internal API changes | Low | Medium (WU-5 test breaks) | Document as known fragility; fallback to keyboard simulation |

## §7 Deferred Items (with reason)

1. **CI integration**: Tests are local-only. CI pipeline for interactive uitests requires headless Xvfb or Metal-capable VM. Separate infrastructure plan.
2. **SSIM tuning**: Initial score may be < 0.85 due to CSS-vs-native rendering differences. Tuning (adjusting reference PNGs, cropping regions, threshold calibration) is follow-up work.
3. **Mojo transport verification**: Testing the full Mojo renderer-side IPC (gap N1 future state) is out of scope. Tests verify the current JS-injection transport only.
4. **DevTools pre-arming test**: Gap #7 (inspector integration) not testable without DevTools protocol scripting setup. Deferred to DevTools integration plan.
5. **Cross-platform verification**: All tests target macOS. Windows/Linux verification requires separate platform work.
6. **Pixel-identical comparisons**: SSIM is structural; `perceptual-diff` or `pixelmatch` could give tighter results. Deferred — SSIM is sufficient for MVP.

## §8 Atomic Commit Strategy

Multiple commits, grouped by concern:

```
# Commit 1: Rust tests (WU-7)
test(boost): add temp boost lifecycle unit tests

# Commit 2: C++ uitests (WU-1, WU-3, WU-4, WU-5 — after GN permission)
test(boost): add interactive uitests for DOM, behavior, lifecycle, CodeMirror

# Commit 3: Screenshot diff harness (WU-2)
feat(scripts): add boost screenshot SSIM diff harness

# Commit 4: Manual QA checklist (WU-6)
docs(qa): add zen boost manual QA checklist
```

Commits can be made independently as each WU completes. No cross-commit dependencies.

## §9 TDD Approach

For each WU category:

**Rust tests (WU-7)** — Classic TDD: write tests, run, adapt to actual API if signatures differ.

**C++ uitests (WU-1/3/4/5)** — Integration TDD: tests exercise the live WebUI; failures reveal DOM mismatches or timing issues.

**Screenshot diff (WU-2)** — Acceptance TDD: harness produces numeric SSIM score; threshold tuning follows.

**Manual QA (WU-6)** — Human-in-the-loop: documented expected behaviors as concrete pass/fail criteria.

## §10 Delegation Recipe

```python
# WU-0: Already resolved. No delegation needed.

# WU-1: DOM equivalence test
task(
  category="deep",
  load_skills=[],
  description="Boost DOM equivalence uitest",
  prompt="""See .sisyphus/plans/zen-boost-runtime-verification-2026-05-30.md §WU-1.

Create maho-chromium/browser/ui/views/boost/maho_boost_dom_equivalence_interactive_uitest.cc.
Pattern: InProcessBrowserTest (reference: maho-chromium/browser/ui/views/command/maho_ai_popup_lifetime_interactive_uitest.cc).

Test: navigate to test page, trigger ShowForActiveDomain on the controller,
poll editor WebContents load, execute JS to extract IDs/classes, assert
Zen-mandated DOM contract (IDs listed in plan spec).

Include all necessary #includes. Use base::test::RunUntil for async waits.

DO NOT edit BUILD.gn without user permission. Report the exact GN addition
needed (source_set name, sources list, deps list).""",
  run_in_background=true
)

# WU-2: Screenshot diff harness
task(
  category="unspecified-high",
  load_skills=[],
  description="Boost screenshot SSIM diff harness",
  prompt="""See .sisyphus/plans/zen-boost-runtime-verification-2026-05-30.md §WU-2.

Create two files:
1. maho/scripts/boost-screenshot-diff.py
2. maho/scripts/requirements-boost-diff.txt

Script flow: launch Maho.app, trigger boost editor via AppleScript keystroke
(Cmd+Opt+B), position window, screencapture region, compute SSIM via
scikit-image against reference PNG.

CLI: --reference <path> --mode [boost|code] --offset <x,y> --timeout <s>
Report: PASS (>=0.85), WARN (0.70-0.85), FAIL (<0.70).
Handle retina 2x scaling by resizing to match reference dimensions.
Include prerequisite validation (imports, file paths, Maho.app existence).
Exit codes: 0=PASS/WARN, 1=FAIL, 2=ERROR.""",
  run_in_background=true
)

# WU-3: Behavioral round-trip uitest
task(
  category="deep",
  load_skills=[],
  description="Boost behavioral round-trip uitest",
  prompt="""See .sisyphus/plans/zen-boost-runtime-verification-2026-05-30.md §WU-3.

Create maho-chromium/browser/ui/views/boost/maho_boost_behavior_interactive_uitest.cc.

5 test cases in one file:
1. ColorPickerDotDrag — simulate pointer events on dot, verify CSS on target page
2. MagicThemeToggle — click toggle, verify class state
3. CaseCycleFourModes — click 4x, verify cycle none→upper→lower→capitalize→none
4. SizeCycleFiveModes — click 5x, verify 100→110→125→150→90→100
5. ZapModeRoundTrip — enter zap, simulate element click, verify selector persisted

Use InProcessBrowserTest + content::ExecuteJavaScriptForTests.
Pattern reference: maho_ai_popup_lifetime_interactive_uitest.cc.
For zap test, add GTEST_SKIP() if content scripts not loaded (Plan A dependency).

DO NOT edit BUILD.gn without user permission.""",
  run_in_background=true
)

# WU-4: Lifecycle uitest
task(
  category="deep",
  load_skills=[],
  description="Boost lifecycle uitest",
  prompt="""See .sisyphus/plans/zen-boost-runtime-verification-2026-05-30.md §WU-4.

Create maho-chromium/browser/ui/views/boost/maho_boost_lifecycle_interactive_uitest.cc.

5 test cases:
1. CreateTempDiscardNoPersistence — open, close without commit, verify fresh on reopen
2. CreateTempCommitPersistence — open, change, close, verify persisted on reopen
3. TabSwitchClosesEditor — open editor, switch tabs, verify !IsShowing()
4. NavigationClosesEditor — navigate different domain, verify !IsShowing()
5. SameDomainReshowReuses — hide, re-show same domain, verify shows without new widget

Use MahoBoostWindowController::GetForBrowser, .ShowForActiveDomain, .IsShowing().
DO NOT edit BUILD.gn without user permission.""",
  run_in_background=true
)

# WU-5: CodeMirror round-trip uitest
task(
  category="deep",
  load_skills=[],
  description="Boost CodeMirror round-trip uitest",
  prompt="""See .sisyphus/plans/zen-boost-runtime-verification-2026-05-30.md §WU-5.

Create maho-chromium/browser/ui/views/boost/maho_boost_codemirror_interactive_uitest.cc.

3 test cases:
1. CodeModeResize — open editor (184w), switch to code mode, verify width ~452
2. CssPersistenceRoundTrip — set CM6 content "body { color: red; }", close, reopen, verify content
3. CodeMirrorMounted — switch to code mode, verify .cm-editor and .cm-content exist

For CM6 content access: try .cmView.view.state.doc.toString() via JS execution.
If internal API not accessible, document limitation and use input simulation fallback.
DO NOT edit BUILD.gn without user permission.""",
  run_in_background=true
)

# WU-6: Manual QA checklist
task(
  category="writing",
  load_skills=[],
  description="Boost manual QA checklist",
  prompt="""See .sisyphus/plans/zen-boost-runtime-verification-2026-05-30.md §WU-6.

Create .sisyphus/qa/zen-boost-manual-qa-checklist-2026-05-30.md

Structure:
- Header (date, tester, build version, macOS version)
- Prerequisites
- 18 test scenarios (per plan spec), each with:
  - Steps (numbered, specific)
  - Expected Behavior (precise, observable)
  - Pass/Fail Criteria (binary, no ambiguity)
  - Screenshot notes (what to capture as evidence)
- Known Limitations section (cite gaps #1-#8)
- Results Summary table (scenario # | status | notes)

Entrypoint info: Cmd+Opt+B opens editor. Also available via command palette
'Boost Editor' action and page info UI button.

Ensure the doc is actionable by someone unfamiliar with the codebase.""",
  run_in_background=true
)

# WU-7: Rust lifecycle tests
task(
  category="quick",
  load_skills=[],
  description="Boost Rust lifecycle tests",
  prompt="""See .sisyphus/plans/zen-boost-runtime-verification-2026-05-30.md §WU-7.

FIRST: Read maho/crates/maho-core/src/boost_manager.rs to verify:
- Method name for creating a temporary boost (create_temp? create_temporary? create?)
- Method name for committing (commit_boost? commit? save?)
- Method name for discarding (discard_boost? discard? delete?)
- Method name for updating (update_boost? apply_update?)
- BoostUpdate struct fields and how to construct Default

THEN: Add 5 tests to maho/crates/maho-core/tests/test_batch_1.rs:
1. test_boost_create_temp_produces_valid_boost
2. test_boost_commit_persists_state
3. test_boost_discard_removes_temp
4. test_boost_lifecycle_full_roundtrip (create, update, commit, retrieve, verify)
5. test_boost_discard_after_update_loses_changes

Add necessary imports at top of file.
Run: cd maho && cargo test --package maho-core -- test_boost
Report results.""",
  run_in_background=true
)
```

**Execution order:**
1. Fire WU-1, WU-2, WU-3, WU-4, WU-5, WU-6, WU-7 **all in parallel** (7 background tasks)
2. Collect all results
3. Ask user for GN permission if C++ tests compiled successfully in isolation
4. Run WU-8 (build verification gate) directly

---

**End of plan.**
