# Zen Boost — Magic Theme Workspace Gradient Wiring + Sparkle Button Layout

**Date**: 2026-06-01
**Author**: Sisyphus
**Status**: Partial (Rust + FFI shape applied, C++/CSS/tests pending)
**Predecessor**: `.sisyphus/plans/zen-boost-architectural-defer-cleanup-2026-05-30.md` §7-2 (Magic Theme deferred)
**Trigger**: User directive — complete the autoTheme port to match Zen behavior; place sparkle button exactly inside the gradient panel top-center.

---

## §1 Goal

Complete Zen `autoTheme` (Maho `magicTheme`) port so the toggle actually colors the page using the active workspace gradient's primary hue (not the manual color picker dot), and align the sparkle button visually with Zen's reference layout (centered at top of the gradient panel).

---

## §2 Acceptance Criteria

- [ ] Toggling the sparkle button on a regular URL changes the injected page CSS to use the active workspace gradient hue
- [ ] Switching workspaces while the editor is open updates the boost color in real time
- [ ] When magicTheme is on, the gradient picker is visually disabled (grayscale) AND non-interactive (pointer-events blocked)
- [ ] Sparkle button visually centered at top of the gradient panel (matches Zen reference screenshot)
- [ ] `cargo test --workspace` green
- [ ] `lsp_diagnostics` clean on all changed files
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` exit 0
- [ ] `Maho.app` launchable; sparkle toggle visibly tints page; manual smoke OK
- [ ] No new Rust panics, no new TSAN/UBSAN issues from atomic addition

---

## §3 Non-Goals

- Persisting the workspace hue inside the boost data (intentionally ephemeral; matches Zen's actor-fetched semantics)
- Rendering the actual workspace gradient as the picker panel background (Maho currently uses `MAGIC_THEME_FALLBACK_ACCENT = '#7e57c2'` hardcoded; live gradient panel would need a `GetWorkspaceGradientHue()` Mojo RPC — out of scope for this plan)
- Secondary gradient stop support (Zen uses primary only; secondary integration deferred)
- Linux/Windows verification (macOS only for this iteration)

---

## §4 Constraints

- `MahoBoostInjectionHandler::ProcessRequest` runs on **IO thread**; `MahoSpaceThemeState::GetThemeData()` requires UI thread → must use a thread-safe atomic cache
- `.build.lock` serializes Rust + Chromium build entrypoints
- GN file edits forbidden without explicit user permission (none expected here — all edits are inside source files referenced by existing GN targets)
- No commits, no git operations (per session contract)
- The recently-completed CSP hash fix (`'sha256-bf83pS3c1Zr2hZr1Fp0BhgiT88JFLyoDX6YZ8+tq6Y4='` in `maho_boost_ui.cc`) must remain — do not touch CSP

---

## §5 Current State (already applied in working tree, not committed)

| File | Status | Lines |
|---|---|---|
| `maho/crates/maho-core/src/boost_manager.rs:399` | ✅ Applied | `compose_css(&self, boost: &Boost, workspace_gradient_hue_deg: Option<f64>)` |
| `maho/crates/maho-core/src/boost_manager.rs:485-510` | ✅ Applied | magic_theme branch removed; color_on branch now uses `workspace_gradient_hue_deg.unwrap_or(boost.color.dot_angle_deg)` when `magic_theme=true` |
| `maho/crates/maho-core/src/maho_core.rs:1742-1750` | ✅ Applied | `compose_css_for_boost(boost_id, workspace_gradient_hue_deg)` signature updated |
| `maho/crates/maho-core/src/maho_core.rs:3871, 3957` | ✅ Applied | Internal callers pass `None` |
| `maho/crates/maho-ffi/src/lib.rs:862-880` | ✅ Applied | `maho_core_boost_compose_css(ptr, boost_id, workspace_gradient_hue_deg: f32)` with sentinel handling (`< 0` or non-finite = no override) |
| `maho-chromium/third_party/maho/maho_ffi.h:318` | ✅ Applied | C declaration updated to match |
| **Verification** | ✅ | `cargo check --workspace` passes |

---

## §6 Remaining Work Units

```
                 ┌──────────┐
                 │   WU-1   │  Atomic cache in MahoSpaceThemeState
                 │  (UI→IO  │  (~5 lines)
                 │   safe)  │
                 └─────┬────┘
                       │
                 ┌─────▼─────┐
                 │   WU-2    │  Bridge / injection_handler signature
                 │  C++ wire │  (3 files)
                 └─────┬─────┘
                       │
            ┌──────────┼──────────┐
            ▼                     ▼
       ┌────────┐            ┌────────┐
       │  WU-3  │            │  WU-4  │
       │  CSS   │            │ Rust   │
       │ layout │            │ tests  │
       └────┬───┘            └────┬───┘
            │                     │
            └──────────┬──────────┘
                       ▼
                 ┌──────────┐
                 │   WU-5   │  Build + smoke
                 │  Verify  │
                 └──────────┘
```

WU-1 must complete before WU-2 (compile-time dep). WU-3 / WU-4 can run in parallel after WU-2. WU-5 is final gate.

---

### WU-1: Add thread-safe hue cache to `MahoSpaceThemeState`

**Severity**: Foundation
**Owner**: `task(category="quick", load_skills=[], ...)`
**Estimated effort**: 10–15 min

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/ui/theme/maho_space_theme_state.h` | Add `<atomic>` include, static atomic, getter |
| `maho-chromium/browser/ui/theme/maho_space_theme_state.cc` | Define static, update from `UpdateFromCore` |

#### Spec

`.h`:
```cpp
#include <atomic>
// ... existing ...

class MahoSpaceThemeState {
 public:
  // ... existing ...
  // Thread-safe accessor — readable from any thread.
  // Returns 0..360 when an active gradient hue is cached, or a negative
  // value when no theme is active.
  static float GetActiveHueAtomic();

 private:
  // ... existing ...
  static std::atomic<float> active_hue_atomic_;
};
```

`.cc`:
```cpp
// near other static definitions, e.g. after `active_theme_`:
std::atomic<float> MahoSpaceThemeState::active_hue_atomic_{-1.0f};

// In UpdateFromCore (UI thread), AFTER active_theme_ is assigned:
if (active_theme_.has_value()) {
  active_hue_atomic_.store(active_theme_->primary_hsv.hue,
                           std::memory_order_relaxed);
} else {
  active_hue_atomic_.store(-1.0f, std::memory_order_relaxed);
}

// In Clear (UI thread):
active_hue_atomic_.store(-1.0f, std::memory_order_relaxed);

// New getter:
// static
float MahoSpaceThemeState::GetActiveHueAtomic() {
  return active_hue_atomic_.load(std::memory_order_relaxed);
}
```

#### Verification

```bash
python3 maho-chromium/build/scripts/build_maho.py  # only this should compile, no behavior change yet
```

#### Risk

- Atomic on `float` is well-defined for std::atomic since C++11
- `std::memory_order_relaxed` is correct here (no synchronization required — read is just an advisory color value)
- All writes happen on UI thread; reads from any thread

---

### WU-2: Wire workspace hue from injection handler to FFI

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 25–30 min

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/third_party/maho/maho_bridge.h` | Update `GetBoostInjectionCss` signature |
| `maho-chromium/third_party/maho/maho_bridge.cc` | Pass new param to FFI call |
| `maho-chromium/browser/net/maho_boost_injection_handler.cc` | Read atomic, pass to bridge |
| `maho-chromium/third_party/maho/BUILD.gn` | Verify deps allow `maho_space_theme_state.h` from injection handler (likely already OK) |

#### Spec

**`maho_bridge.h:53`** (add hue parameter):
```cpp
std::string GetBoostInjectionCss(MahoCore* core, const char* url,
                                 float workspace_gradient_hue_deg);
```

**`maho_bridge.cc:136-167`**:
```cpp
std::string GetBoostInjectionCss(MahoCore* core, const char* url,
                                 float workspace_gradient_hue_deg) {
  // ... existing logic up to line 167 ...

  // Replace line 167:
  char* css = maho_core_boost_compose_css(core, boost_id->c_str(),
                                          workspace_gradient_hue_deg);
  // ... rest unchanged ...
}
```

**`maho_boost_injection_handler.cc:245`**:
```cpp
#include "maho/browser/ui/theme/maho_space_theme_state.h"
// ...

// Replace line 245:
const float hue = MahoSpaceThemeState::GetActiveHueAtomic();
std::string css = maho::core::GetBoostInjectionCss(core, url.c_str(), hue);
```

**Note on threading**: `MahoBoostInjectionHandler::ProcessRequest` runs on the network/IO thread. `GetActiveHueAtomic()` is intentionally lock-free + thread-safe. No additional synchronization needed.

**GN dep check**: `maho-chromium/browser/net/BUILD.gn` already depends on chrome/browser/ui transitively. Verify `//maho/browser/ui/theme:theme` (or whatever the target name is) is reachable. If it's not in the existing deps, add it — this is a normal dep addition (not a new target). User has historically pre-approved similar additions; flag if uncertain.

#### Verification

```bash
python3 maho-chromium/build/scripts/build_maho.py
```

After build:
- launch `Maho.app`
- navigate to `https://example.com`
- toggle sparkle in boost editor
- inspect injected CSS in DevTools → `html::after { background-color: hsl(<workspace-hue>deg, ...) }` should appear with hue matching the active space's primary gradient color

#### Risk

- **GN dep gap**: If `maho-chromium/browser/net/BUILD.gn` cannot reach `maho_space_theme_state`, may need to add the dep (low risk — header is small and already used elsewhere)
- **Atomic-vs-write timing**: Possible race where IO thread reads stale hue right after a space switch. Acceptable — next compose_css call (e.g., on next URL request) will pick up the updated value
- **Hue=-1 means "no override"** in the FFI sentinel; `GetActiveHueAtomic()` returns `-1.0f` when no theme is active → naturally falls through to `dot_angle_deg`

---

### WU-3: CSS sparkle button centering + picker pointer-events

**Severity**: Low
**Owner**: `task(category="visual-engineering", load_skills=["frontend-ui-ux"], ...)`
**Estimated effort**: 15 min

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/resources/maho_boost/boost.css` | Two surgical rule additions |

#### Spec

**Make gradient panel a flex container** (replicates Zen's XUL `hbox` + `margin: auto` centering for the sparkle button):

Find the existing `.zen-boost-color-picker-gradient` rule (around line 814, but locate by selector). Add to the existing block:
```css
.zen-boost-color-picker-gradient {
  /* ... existing properties unchanged ... */
  display: flex;
  flex-direction: column;
  align-items: center;
}
```

This preserves the existing `position: relative; overflow: hidden` (needed for absolutely-positioned dots/circle children) and lets the sparkle button's `margin-left: auto; margin-right: auto` reliably center it horizontally at the top of the panel.

**Block dot drag when panel is disabled** (matches Zen's intended UX — when grayscale, no interaction):

Find `.zen-boost-panel-disabled` rule (line 482). Append a child rule:
```css
.zen-boost-panel-disabled .zen-boost-color-picker-dot {
  pointer-events: none;
}
.zen-boost-panel-disabled .zen-boost-color-picker-circle {
  pointer-events: none;
}
```

The sparkle button itself MUST remain clickable (so user can disable magic theme). It does NOT have the `.zen-boost-color-picker-dot` class so it's unaffected. Verify after edit.

#### Verification

- `lsp_diagnostics` on `boost.css` — clean
- Manual: with magic theme ON, dragging dots does nothing; clicking sparkle still toggles
- Sparkle button is visually centered at the top of the gradient panel

#### Risk

- Adding `display: flex` to a `position: relative` block container won't affect absolutely-positioned children (dots, circle) — they continue to use their own positioning
- `align-items: center` only affects in-flow children — only the sparkle button is in-flow; dots are `position: absolute`

---

### WU-4: Rust unit test for magic_theme + workspace hue

**Severity**: Low
**Owner**: `task(category="quick", load_skills=[], ...)`
**Estimated effort**: 15–20 min

#### Files

| Path | Edit type |
|---|---|
| `maho/crates/maho-core/src/boost_manager.rs` | Append test in existing `#[cfg(test)] mod tests` block |

#### Spec

Add three tests:

```rust
#[test]
fn compose_css_magic_theme_uses_workspace_hue() {
    let mgr = BoostManager::new();
    let mut boost = mgr.create_temp("example.com".to_string());
    boost.color.color_boost_enabled = true;
    boost.color.magic_theme = true;
    boost.color.dot_angle_deg = 0.0; // would-be red if magic_theme were off

    let css = mgr.compose_css(&boost, Some(240.0)); // workspace blue
    assert!(
        css.contains("hsl(240"),
        "expected workspace hue 240 to drive html::after; got: {}",
        css
    );
    assert!(
        !css.contains("Magic Theme: workspace integration pending"),
        "stub comment must be removed"
    );
}

#[test]
fn compose_css_magic_theme_falls_back_to_dot_when_no_hue() {
    let mgr = BoostManager::new();
    let mut boost = mgr.create_temp("example.com".to_string());
    boost.color.color_boost_enabled = true;
    boost.color.magic_theme = true;
    boost.color.dot_angle_deg = 90.0; // green fallback

    let css = mgr.compose_css(&boost, None);
    assert!(css.contains("hsl(90"), "expected dot fallback 90 deg; got: {}", css);
}

#[test]
fn compose_css_no_magic_theme_ignores_workspace_hue() {
    let mgr = BoostManager::new();
    let mut boost = mgr.create_temp("example.com".to_string());
    boost.color.color_boost_enabled = true;
    boost.color.magic_theme = false;
    boost.color.dot_angle_deg = 30.0;

    let css = mgr.compose_css(&boost, Some(180.0)); // workspace value provided but should be ignored
    assert!(css.contains("hsl(30"), "expected dot 30 deg; got: {}", css);
    assert!(!css.contains("hsl(180"));
}
```

Verify the existing tests at `boost_manager.rs:536-755` continue to pass (they used `compose_css(b)` — they must already have been updated to the new signature; if not, update them to pass `None`).

#### Verification

```bash
cd maho && cargo test --package maho-core --lib boost_manager:: -- compose_css_magic
cd maho && cargo test --package maho-core --lib boost_manager::    # full module
cd maho && cargo test --workspace                                   # regression
```

All green expected.

#### Risk

- Existing tests in `boost_manager.rs` may have been calling `compose_css(b)` with old single-arg signature. Cargo check passed earlier (during this session), so either no such test exists OR they were already adapted. If cargo test fails, update the call sites to `compose_css(b, None)`.

---

### WU-5: Build + smoke verification

**Severity**: Mandatory
**Owner**: Direct (no delegation)
**Estimated effort**: 10–15 min (mostly build wall time)

#### Steps

1. Wait for WU-1, WU-2, WU-3, WU-4 to all report success
2. `cd maho && cargo test --workspace` — all green
3. `lsp_diagnostics` on:
   - `maho-chromium/browser/ui/theme/maho_space_theme_state.{h,cc}`
   - `maho-chromium/third_party/maho/maho_bridge.{h,cc}`
   - `maho-chromium/browser/net/maho_boost_injection_handler.cc`
   - `maho-chromium/browser/resources/maho_boost/boost.css`
   - `maho/crates/maho-core/src/boost_manager.rs`
4. `python3 maho-chromium/build/scripts/build_maho.py` — exit 0
5. Smoke (driven directly by orchestrator via osascript, like the previous session):
   - Kill old Maho instance
   - Launch new build with stderr captured
   - Activate, navigate to `https://example.com`
   - Open boost editor (Cmd+Opt+B)
   - Toggle sparkle button
   - Verify in stderr: no `[MahoBoost]` errors, no CSP violations
   - Verify visually (screenshot via `screencapture`): sparkle button visually centered at top of gradient panel; gradient panel goes grayscale when sparkle is on
   - Verify on target tab: page tint hue corresponds to active space's gradient

#### On failure

- WU-1 fail → atomic include or member declaration typo; re-check
- WU-2 fail (compile) → GN dep missing; add `//maho/browser/ui/theme` to `browser/net/BUILD.gn` deps
- WU-2 fail (link) → atomic symbol not exported; verify `MahoSpaceThemeState` is in correct source_set
- WU-3 fail (visual) → flex centering didn't apply; check selector specificity
- WU-4 fail (test) → existing `compose_css(b)` calls in tests need `, None`

---

## §7 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| GN dep from `browser/net` to `browser/ui/theme` not allowed (layering) | Low | Medium | Verify with `git log --grep="maho_space_theme_state" maho-chromium/browser/net/`; if blocked, expose hue via a lower-layer accessor (e.g., put atomic in `maho-chromium/browser/maho_browser_main_extra_parts` or a new `maho_space_theme_atomic.cc`) |
| Atomic write/read race produces stale hue immediately after space switch | Medium | Low | Acceptable — next compose_css call within ~ms picks up new value; user-perceived flicker minimal |
| Existing Rust tests break due to signature change | Medium | Low | WU-4 explicitly checks; trivial to update with `, None` |
| Sparkle button centering breaks on Linux/Windows (no XUL hbox there anyway) | Low | Low | Out of scope — flex is well-supported cross-platform; would actually help non-macOS render |
| `maho_space_theme_state.h` not currently a public dep of `browser/net:net` source_set | Medium | Medium | If GN refuses, add the dep — trivial GN edit. Get user approval if needed. |
| CSP violation from any new inline style introduced by the edits | Low | High | None of the edits add inline scripts; `pointer-events` and `display: flex` are CSS-only. Hash from earlier session unaffected. |

---

## §8 Deferred Items (intentional)

1. **Live workspace gradient as picker panel background** — currently `MAGIC_THEME_FALLBACK_ACCENT = '#7e57c2'` hardcoded in `app.ts:1406-1408`. Making it reflect the real workspace gradient requires a `GetWorkspaceGradientHue()` Mojo RPC + TS update. Separate plan.
2. **Secondary gradient stop hue** — Zen reads `boost.workspaceGradient.forEach(c => if (c.isPrimary) ...)` then uses primary. Maho only supports primary. Multi-stop accent blending deferred.
3. **Workspace gradient change propagation while editor is open** — currently the editor caches color via Mojo update; after WU-2, page injection picks up new hue per request, but the in-editor preview won't update without a Mojo signal. UX gap, not blocking.
4. **Linux/Windows verification** — macOS-only iteration.

---

## §9 Atomic Commit Strategy

Single commit per WU:

| # | Commit message | Files |
|---|---|---|
| 1 | `feat(boost): add thread-safe active hue atomic to MahoSpaceThemeState` | maho_space_theme_state.{h,cc} |
| 2 | `feat(boost): wire workspace gradient hue through FFI to compose_css` | maho_bridge.{h,cc}, maho_boost_injection_handler.cc, BUILD.gn (if dep added) |
| 3 | `feat(boost): center magic-theme button + lock picker when grayscale` | boost.css |
| 4 | `test(boost): magic_theme uses workspace hue / falls back / inert when off` | boost_manager.rs |

Rust + FFI signature changes (already in working tree) ride with commit 2 since they're prerequisite. Or split into a "infra" commit prefacing 2 if preferred.

---

## §10 Delegation Recipe

```python
# WU-1 — atomic cache
task(
  category="quick",
  load_skills=[],
  description="MahoSpaceThemeState atomic hue cache",
  prompt="See .sisyphus/plans/zen-boost-magic-theme-workspace-wiring-2026-06-01.md §WU-1. Add std::atomic<float> active_hue_atomic_ to MahoSpaceThemeState. Update from UpdateFromCore + Clear (UI thread). Add static GetActiveHueAtomic() getter. Run build to verify compiles.",
  run_in_background=true,
)

# WU-2 — bridge + injection_handler wire-up
task(
  category="deep",
  load_skills=[],
  description="Wire workspace hue through FFI",
  prompt="See .sisyphus/plans/zen-boost-magic-theme-workspace-wiring-2026-06-01.md §WU-2. Update GetBoostInjectionCss signature to take float workspace_gradient_hue_deg. Read MahoSpaceThemeState::GetActiveHueAtomic() in maho_boost_injection_handler.cc. Pass through to maho_core_boost_compose_css FFI. If GN dep fails, ask user before adding.",
  run_in_background=false,  # sequential after WU-1
)

# WU-3 — CSS centering + pointer-events
task(
  category="visual-engineering",
  load_skills=["frontend-ui-ux"],
  description="Sparkle button centering + dot pointer-events block",
  prompt="See .sisyphus/plans/zen-boost-magic-theme-workspace-wiring-2026-06-01.md §WU-3. Add display:flex flex-direction:column align-items:center to .zen-boost-color-picker-gradient in boost.css. Add pointer-events:none on .zen-boost-panel-disabled .zen-boost-color-picker-dot/.zen-boost-color-picker-circle.",
  run_in_background=true,
)

# WU-4 — Rust tests
task(
  category="quick",
  load_skills=[],
  description="Magic theme workspace hue Rust tests",
  prompt="See .sisyphus/plans/zen-boost-magic-theme-workspace-wiring-2026-06-01.md §WU-4. Append 3 tests to boost_manager.rs tests module. Run cargo test --package maho-core. If existing tests break due to compose_css signature change, update them to pass None.",
  run_in_background=true,
)

# WU-5 — orchestrator runs directly: build + smoke via osascript (like prior session)
```

---

**End of plan.**
