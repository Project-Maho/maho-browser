# Zen Boost — N3 GRD Content Script Loading

**Date**: 2026-05-30
**Author**: Sisyphus
**Status**: Ready for delegation
**Predecessor**: `.sisyphus/plans/zen-boost-implementation-record-2026-05-29.md` (gap N3)

## Goal

Replace the inline JS/CSS placeholder in `maho-chromium/browser/net/maho_boost_injection_handler.cc` (lines 265–301) with `LoadDataResourceString` + `ExecuteInIsolatedWorld` calls so the zap content scripts load from compiled grit resources. This resolves implementation gap N3 and unblocks criterion #8 (partial → ✅).

## Acceptance Criteria

- [ ] `python3 maho-chromium/build/scripts/build_maho.py` exits 0
- [ ] `Maho.app` launches; opening any page and triggering zap mode (Cmd+Opt+B → Zap button) injects the content scripts
- [ ] Runtime verification: DevTools console `window.__mahoBoost.SelectorComponent` returns the class constructor
- [ ] The TODO comments at lines 265–301 are replaced with actual loading code
- [ ] No new compile warnings in `maho_boost_injection_handler.cc`
- [ ] `DCHECK(!css.empty())` fires on debug build if resource fails to load (resource integrity gate)

## Non-Goals

- Mojo-based renderer-side transport (gap N1 — separate optimization plan)
- Dissolve animation for zap overlays (gap N2 — visual-only)
- Picker mode full verification (requires Plan C runtime tests)
- Content script TS source edits (already complete per implementation record)

## Constraints

- `.build.lock` serialization: the build step is the only lock-taking operation
- GN file edits forbidden without user permission — none needed here (dep `//maho/browser/resources/maho_boost_content_script:resources` already wired in `browser/net/BUILD.gn:28`)
- The grit header `chrome/grit/maho_boost_content_script_resources.h` is only available AFTER first `gn gen`; this plan assumes at least one prior successful `build_maho.py` run has generated it
- No commits (per session policy unless explicitly requested)

## Parallelism Topology

```
     ┌──────────┐
     │   WU-1   │
     │  Edit CC │
     │  + Build │
     └──────────┘
```

Single work unit — all edits are in one file, verification is build-only. No parallelization opportunity.

## Work Units

---

### WU-1: Replace GRD placeholder with LoadDataResourceString calls

**Severity**: Medium
**Owner**: `task(category="quick", load_skills=[], ...)`
**Estimated effort**: 20–30 minutes (5 min edit + 15–25 min incremental build)

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/net/maho_boost_injection_handler.cc` | Replace lines 265–301 |

#### Spec

The file already includes (line 25): `#include "ui/base/resource/resource_bundle.h"`, (line 11): `#include "base/strings/strcat.h"`, and (line 12): `#include "base/strings/string_util.h"`.

**Step 1** — Add grit header include. After line 25 (`#include "ui/base/resource/resource_bundle.h"`), add:

```cpp
#include "chrome/grit/maho_boost_content_script_resources.h"
```

**Step 2** — Replace the CSS placeholder block (lines 265–287). Delete the TODO comment block and the `css_inject` placeholder. Replace with:

```cpp
  // Load CSS from GRD and inject as global for Shadow DOM consumption.
  std::string css = ui::ResourceBundle::GetSharedInstance()
      .LoadDataResourceString(
          IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_CSS);
  DCHECK(!css.empty());
  std::string escaped_css;
  base::ReplaceChars(css, "\\", "\\\\", &escaped_css);
  base::ReplaceChars(escaped_css, "`", "\\`", &escaped_css);
  base::ReplaceSubstringsAfterOffset(&escaped_css, 0, "${", "\\${");
  std::string css_inject = base::StrCat({
      "window.__mahoBoost = window.__mahoBoost || {};",
      "window.__mahoBoostCSS = `", escaped_css, "`;"});
  ExecuteInIsolatedWorld(css_inject);
```

**Step 3** — Replace the JS placeholder block (lines 289–301). Delete the TODO comment block and the bare `content_script_injected_ = true;`. Replace with:

```cpp
  // Load JS content scripts in dependency order.
  // selector_component.js must precede zap_overlay.js (reads ns.SelectorComponent).
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_SELECTOR_COMPONENT_JS));
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_ZAP_OVERLAY_JS));
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_JS));

  content_script_injected_ = true;
```

#### Complete `InjectContentScript()` method after edits

For clarity, the full method body (lines 254–302 region) should read:

```cpp
void MahoBoostInjectionHandler::InjectContentScript() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (content_script_injected_) {
    return;
  }

  content::WebContents* wc = web_contents();
  if (!wc) return;
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive() || !rfh->IsActive()) return;

  // Load CSS from GRD and inject as global for Shadow DOM consumption.
  std::string css = ui::ResourceBundle::GetSharedInstance()
      .LoadDataResourceString(
          IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_CSS);
  DCHECK(!css.empty());
  std::string escaped_css;
  base::ReplaceChars(css, "\\", "\\\\", &escaped_css);
  base::ReplaceChars(escaped_css, "`", "\\`", &escaped_css);
  base::ReplaceSubstringsAfterOffset(&escaped_css, 0, "${", "\\${");
  std::string css_inject = base::StrCat({
      "window.__mahoBoost = window.__mahoBoost || {};",
      "window.__mahoBoostCSS = `", escaped_css, "`;"});
  ExecuteInIsolatedWorld(css_inject);

  // Load JS content scripts in dependency order.
  // selector_component.js must precede zap_overlay.js (reads ns.SelectorComponent).
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_SELECTOR_COMPONENT_JS));
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_ZAP_OVERLAY_JS));
  ExecuteInIsolatedWorld(
      ui::ResourceBundle::GetSharedInstance()
          .LoadDataResourceString(
              IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_JS));

  content_script_injected_ = true;
}
```

#### Load order justification

`zap_overlay.ts:49` instantiates `new ns.SelectorComponent(...)`. The `ns` namespace object is populated by `selector_component.ts` which assigns `SelectorComponent` onto `window.__mahoBoost`. Therefore:
1. `selector_component.js` — defines `SelectorComponent` on namespace
2. `zap_overlay.js` — reads `ns.SelectorComponent`
3. `content_script.js` — orchestrator; reads both

#### Template literal escape rationale

The CSS text is injected as a JS template literal (`` `...` ``). Three escapes required:
- `\` → `\\` (literal backslash in template)
- `` ` `` → `` \` `` (template delimiter)
- `${` → `\${` (template interpolation)

This matches the existing `BuildBoostCssScript()` pattern at lines 46–48 of the same file.

#### Verification

```bash
# From workspace root:
python3 maho-chromium/build/scripts/build_maho.py
# Expected: exit 0, Maho.app produced

# Runtime (manual):
# 1. Launch Maho.app
# 2. Navigate to any page (e.g. example.com)
# 3. Cmd+Opt+B to open boost editor
# 4. Click "Zap" button to enter zap mode
# 5. Open DevTools console on the target page
# 6. Verify: typeof window.__mahoBoost.SelectorComponent === 'function'
# 7. Verify: window.__mahoBoostCSS.length > 0
```

#### Risk

- **Empty resource at runtime**: If the build somehow excludes the content script resources, `DCHECK(!css.empty())` fires in debug builds. In release builds, the content script injection silently fails (zap mode won't work). Mitigation: the dep is already wired in `BUILD.gn:28`.
- **JS execution order not guaranteed?**: `ExecuteJavaScriptInIsolatedWorld` is synchronous per-call on the renderer's JS thread. Sequential calls from the same browser-side sequence execute in order on the renderer. No race condition.
- **Large CSS string**: If `content_script.css` grows very large, the template literal approach could hit parser limits. Current file is ~2KB — well within limits. Monitor if it grows past 64KB.

---

## §6 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Grit header not generated (no prior build) | Low | High (compile fail) | Pre-condition: must have run `build_maho.py` at least once previously. The header is generated during `gn gen`. |
| IDR constant name mismatch | Low | High (compile fail) | Names verified from BUILD.gn `grd_prefix = "maho_boost_content_script"` + file basenames. Convention: `IDR_{GRD_PREFIX_UPPER}_{FILENAME_UPPER}`. |
| Template literal escape incomplete (CSS contains `${`) | Medium | Medium (broken injection) | `ReplaceSubstringsAfterOffset` for `${` covers this. Pattern matches existing `BuildBoostCssScript` at line 47. |
| Incremental build fails due to stale ninja state | Low | Low (retry fixes) | `build_maho.py` runs `gn gen` if needed. If stale, `autoninja -C out/Default chrome` after manual `gn gen`. |
| `LoadDataResourceString` returns `std::string_view` not `std::string` | Low | Medium (type error) | Chromium 130+ returns `std::string`. Verify in `ui/base/resource/resource_bundle.h`. If `string_view`, wrap with `std::string(...)`. |

## §7 Deferred Items (with reason)

1. **Mojo renderer-side transport** (gap N1). Current transport works; Mojo migration is a perf optimization. Separate plan.
2. **Dissolve animation** (gap N2). Visual polish only. Separate follow-up.
3. **Runtime verification tests** (Plan C). This plan handles the build-time fix; Plan C handles runtime testing.
4. **DLOG fallback for release builds**. Currently `DCHECK` only. Could add `DLOG(ERROR)` + early return for release. Low priority — if resource is missing, build is broken anyway.
5. **CSS-only injection without JS wrapper**. Could inject CSS via `InsertCSS()` API instead of template literal. Current approach is consistent with existing `BuildBoostCssScript()` pattern. Optimization deferred.

## §8 Atomic Commit Strategy

Single commit after build verification:

```
feat(boost): wire GRD resource loading for zap content scripts

Replace inline JS/CSS placeholders in InjectContentScript() with
LoadDataResourceString calls loading compiled TypeScript from grit
resources. Load order: selector_component → zap_overlay → content_script.

Resolves implementation gap N3 from zen-boost-implementation-record.

File: maho-chromium/browser/net/maho_boost_injection_handler.cc
```

## §9 TDD Approach

This is a single-file infrastructure change with build-as-test:

1. **Red**: Current state — TODO placeholders, no resource loading, zap mode non-functional at runtime
2. **Green**: After edit — `build_maho.py` exits 0 (compile-time verification that IDR constants resolve)
3. **Refactor**: None needed — code is minimal and follows existing pattern

Runtime verification (DevTools console check) serves as the integration test. Formal automated verification is in Plan C (WU-3 zap mode round-trip test).

## §10 Delegation Recipe

```python
task(
  category="quick",
  load_skills=[],
  description="N3 GRD content script loading",
  prompt="""See .sisyphus/plans/zen-boost-n3-grd-content-script-loading-2026-05-30.md §WU-1.

Edit maho-chromium/browser/net/maho_boost_injection_handler.cc:

1. Add this include AFTER line 25 (after the ui/base/resource/resource_bundle.h include):
   #include "chrome/grit/maho_boost_content_script_resources.h"

2. Replace lines 265-287 (the CSS placeholder TODO block + css_inject assignment + ExecuteInIsolatedWorld(css_inject)) with the CSS loading code from the plan spec (LoadDataResourceString for CSS, escape with ReplaceChars/ReplaceSubstringsAfterOffset, inject via template literal).

3. Replace lines 289-301 (the JS placeholder TODO block + content_script_injected_ = true) with three sequential LoadDataResourceString + ExecuteInIsolatedWorld calls for:
   - IDR_MAHO_BOOST_CONTENT_SCRIPT_SELECTOR_COMPONENT_JS (first)
   - IDR_MAHO_BOOST_CONTENT_SCRIPT_ZAP_OVERLAY_JS (second)
   - IDR_MAHO_BOOST_CONTENT_SCRIPT_CONTENT_SCRIPT_JS (third)
   Then: content_script_injected_ = true;

The plan file contains the EXACT complete method body for reference.

Then run: python3 maho-chromium/build/scripts/build_maho.py
Report exit code and any warnings/errors.""",
  run_in_background=true
)
```

---

**End of plan.**
