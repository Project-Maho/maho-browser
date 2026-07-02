# Window Vibrancy / Transparency — 3-Platform Implementation Plan

**Date:** 2026-07-02
**Branch:** `main` (HEAD: `4264cdbf`)
**Scope:** `maho-chromium/browser/ui/theme/`, `maho-chromium/browser/maho_features.{h,cc}`, `maho-chromium/chromium_src/chrome/browser/ui/views/frame/`
**Parent:** `.omo/plans/settings-migration-bugfix-plan-2026-07-02.md` — Bug #11 (deferred)

---

## Executive Summary

The `appearance.windowTransparency` toggle persists a bool in maho-core (via `maho_core_set_window_transparency` at `maho_ffi.h:1246`) but has zero desktop visual effect. This plan adds a `MahoVibrancyController` abstraction gated behind `kMahoWindowVibrancy` feature flag, with per-platform implementations: macOS (`NSVisualEffectView`), Windows 11 (Mica via `DwmSetWindowAttribute`), Linux (best-effort `_KDE_NET_WM_BLUR_BEHIND_REGION`/KWin blur protocol). Total estimated effort: 5–7 focused sessions across 5 phases.

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│  Settings Toggle (chrome://maho-settings → Appearance pane)          │
└──────────┬───────────────────────────────────────────────────────────┘
           │ HandleFfiSetting("appearance.window_transparency", "true")
           │ (maho_settings_page_handler.cc:1449-1451)
           ▼
┌──────────────────────────────────────────────────────────────────────┐
│  maho-core storage: appearance.windowTransparency: bool              │
│  (maho-ffi/src/lib.rs:4264 → settings_manager.rs:83, default false) │
└──────────┬───────────────────────────────────────────────────────────┘
           │ Write to Chromium pref: "maho.appearance.window_transparency"
           │ (NEW — mirrored so PrefChangeRegistrar can observe)
           ▼
┌──────────────────────────────────────────────────────────────────────┐
│  PrefChangeRegistrar → MahoVibrancyObserver::OnVibrancyPrefChanged() │
│  (browser/ui/theme/maho_vibrancy_observer.cc — NEW)                  │
└──────────┬───────────────────────────────────────────────────────────┘
           │ feature flag gate: base::FeatureList::IsEnabled(kMahoWindowVibrancy)
           │ per-window iteration: BrowserList::GetInstance()->begin()..end()
           ▼
┌──────────────────────────────────────────────────────────────────────┐
│  MahoVibrancyController::ApplyToBrowserWindow(BrowserWindow*, bool)  │
│  (browser/ui/theme/maho_vibrancy_controller.h — virtual interface)   │
└──────────┬─────────────────┬─────────────────┬───────────────────────┘
           │ macOS            │ Windows          │ Linux
           ▼                  ▼                  ▼
┌────────────────┐  ┌─────────────────┐  ┌──────────────────────────┐
│ NSVisualEffect │  │ DwmSetWindow    │  │ _KDE_NET_WM_BLUR_BEHIND  │
│ View (material │  │ Attribute(hwnd, │  │ _REGION (X11)            │
│ .behindWindow, │  │ DWMWA_SYSTEM    │  │ org_kde_kwin_blur_manager│
│ state .active) │  │ BACKDROP_TYPE,  │  │ (Wayland)                │
│ under content  │  │ DWMSBT_MAIN     │  │ Best-effort; no-op on    │
│ View subviews  │  │ WINDOW — Mica)  │  │ GNOME/unsupported        │
└────────────────┘  └─────────────────┘  └──────────────────────────┘
```

**Key design decisions:**
1. **Chromium pref mirror** — The FFI-only setting is mirrored to a Chromium pref (`"maho.appearance.window_transparency"`) so existing `PrefChangeRegistrar` infrastructure observes changes without polling. Same dual-write pattern as `sidebar_width` (`maho_settings_page_handler.cc:1456-1458`).
2. **Per-window application** — Controller iterates all open `BrowserWindow`s. On new window creation, `BrowserView::AddedToWidget()` reads the pref and applies.
3. **Feature flag kill-switch** — `kMahoWindowVibrancy` (disabled by default initially → enabled after QA pass).

---

## Phased Execution Plan

### Phase A — Shared Infrastructure

**Goal:** Feature flag + pref registration + vibrancy controller interface + observer skeleton.
**Parallel-safe:** Yes — no dependency on B/C/D.
**Commit:** `feat(vibrancy): add MahoVibrancyController interface + kMahoWindowVibrancy flag`

#### A.1: Feature flag

**Files:**
- `maho-chromium/browser/maho_features.h` — add `BASE_DECLARE_FEATURE(kMahoWindowVibrancy)`
- `maho-chromium/browser/maho_features.cc` — add `BASE_FEATURE(kMahoWindowVibrancy, "MahoWindowVibrancy", base::FEATURE_DISABLED_BY_DEFAULT)`

**Fix:**
```cpp
// maho_features.h — append before closing }
BASE_DECLARE_FEATURE(kMahoWindowVibrancy);

// maho_features.cc — append before closing }
BASE_FEATURE(kMahoWindowVibrancy,
             "MahoWindowVibrancy",
             base::FEATURE_DISABLED_BY_DEFAULT);
```

#### A.2: Chromium pref registration

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_prefs.h` — declare `kWindowTransparency` pref key
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_prefs.cc` — register bool pref, default false

**Design:** New pref `"maho.appearance.window_transparency"` (bool). Registered via `RegisterProfilePrefs()`. Default: `false`.

#### A.3: Dual-write in settings handler

**Files:**
- `maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc` — L1449-1451

**Current behavior (L1449-1451):**
```cpp
} else if (key == "appearance.window_transparency") {
    maho_core_set_window_transparency(core, value == "true");
    handled = true;
```

**Fix:**
```cpp
} else if (key == "appearance.window_transparency") {
    bool enabled = (value == "true");
    maho_core_set_window_transparency(core, enabled);
    prefs_->SetBoolean(maho::prefs::kWindowTransparency, enabled);
    handled = true;
```

#### A.4: MahoVibrancyController interface

**Files (NEW):**
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller.h`

**Design:**
```cpp
namespace maho {

class MahoVibrancyController {
 public:
  virtual ~MahoVibrancyController() = default;

  virtual void ApplyToBrowserWindow(BrowserWindow* window, bool enabled) = 0;
  virtual bool IsActive(BrowserWindow* window) const = 0;

  static std::unique_ptr<MahoVibrancyController> Create();
};

}  // namespace maho
```

#### A.5: Vibrancy observer (startup + live toggle)

**Files (NEW):**
- `maho-chromium/browser/ui/theme/maho_vibrancy_observer.h`
- `maho-chromium/browser/ui/theme/maho_vibrancy_observer.cc`

**Design:** Singleton. Constructed in `PostProfileInit` (needs prefs). Registers `PrefChangeRegistrar` on `kWindowTransparency`. On change or startup: if feature flag enabled, iterates `BrowserList`, calls `controller_->ApplyToBrowserWindow(w, enabled)` for each. Also implements `BrowserListObserver::OnBrowserAdded` to apply to new windows.

#### A.6: TDD

1. Write unit test: `maho_vibrancy_observer_unittest.cc` — mock `MahoVibrancyController`, toggle pref, assert `ApplyToBrowserWindow` called with correct bool.
2. Implement observer.
3. Write unit test: feature flag disabled → no calls.
4. Verify tests pass.

---

### Phase B — macOS Implementation

**Goal:** `NSVisualEffectView`-backed vibrancy on macOS.
**Depends on:** Phase A (interface + observer).
**Commit:** `feat(vibrancy/mac): implement NSVisualEffectView-based window vibrancy`

#### B.1: macOS vibrancy controller

**Files (NEW):**
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller_mac.h`
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller_mac.mm`

**Design:**
```objc
// In ApplyToBrowserWindow(window, enabled):
NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
NSView* content_view = [ns_window contentView];

if (enabled) {
  NSVisualEffectView* vibrancy_view = [[NSVisualEffectView alloc]
      initWithFrame:content_view.bounds];
  vibrancy_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  vibrancy_view.material = NSVisualEffectMaterialHUDWindow;
  vibrancy_view.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  vibrancy_view.state = NSVisualEffectStateActive;
  vibrancy_view.identifier = @"MahoVibrancyBackdrop";

  [content_view addSubview:vibrancy_view positioned:NSWindowBelow relativeTo:nil];
  [ns_window setOpaque:NO];
  [ns_window setBackgroundColor:[NSColor clearColor]];
} else {
  for (NSView* subview in content_view.subviews) {
    if ([subview.identifier isEqualToString:@"MahoVibrancyBackdrop"]) {
      [subview removeFromSuperview];
      break;
    }
  }
  [ns_window setOpaque:YES];
  [ns_window setBackgroundColor:[NSColor windowBackgroundColor]];
  [ns_window invalidateShadow];
}
```

**Z-order:** Insert at `NSWindowBelow` relative to `nil` (goes behind all existing subviews). If compositor layers conflict, apply `sortSubviewsUsingFunction:` to ensure vibrancy view stays at index 0.

#### B.2: Factory wiring

**Files:**
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller.cc` (NEW) — factory implementation with `#if BUILDFLAG(IS_MAC)` / `IS_WIN` / `IS_LINUX` dispatching to platform subclass

#### B.3: TDD

1. Write interactive_ui_test: `maho_vibrancy_mac_interactive_uitest.mm` — enable flag, set pref true, assert `contentView.subviews` contains view with identifier `"MahoVibrancyBackdrop"`.
2. Write test: disable → assert no such subview.
3. Write test: toggle rapidly 10× → no crash (lifecycle stress).
4. Implement.
5. Manual QA: screenshot with vibrancy on/off on macOS 14+.

---

### Phase C — Windows Implementation

**Goal:** Mica backdrop on Windows 11 22H2+.
**Depends on:** Phase A (interface + observer).
**Parallel-safe with:** Phase B (independent platform).
**Commit:** `feat(vibrancy/win): implement Mica backdrop via DwmSetWindowAttribute`

#### C.1: Windows vibrancy controller

**Files (NEW):**
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller_win.h`
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller_win.cc`

**Design:**
```cpp
void MahoVibrancyControllerWin::ApplyToBrowserWindow(
    BrowserWindow* window, bool enabled) {
  HWND hwnd = window->GetNativeWindow()->GetHost()->GetAcceleratedWidget();

  if (base::win::GetVersion() < base::win::Version::WIN11_22H2) {
    LOG(INFO) << "Window vibrancy: Windows 11 22H2+ required, skipping";
    return;
  }

  DWM_SYSTEMBACKDROP_TYPE backdrop_type =
      enabled ? DWMSBT_MAINWINDOW  // Mica
              : DWMSBT_AUTO;       // System default (opaque)

  HRESULT hr = DwmSetWindowAttribute(
      hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop_type, sizeof(backdrop_type));
  if (FAILED(hr)) {
    LOG(WARNING) << "DwmSetWindowAttribute failed: " << std::hex << hr;
    return;
  }

  if (enabled) {
    MARGINS margins = {-1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
  } else {
    MARGINS margins = {0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
  }

  BOOL use_dark = window->GetColorProvider()->GetColor(
      ui::kColorWindowBackground) < SkColorSetRGB(128, 128, 128);
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                         &use_dark, sizeof(use_dark));
}
```

#### C.2: TDD

1. Write unit test with mock HWND: version < 22H2 → no DWM calls (mock `base::win::GetVersion`).
2. Write test: enabled=true on 22H2+ → DwmSetWindowAttribute called with DWMSBT_MAINWINDOW.
3. Implement.
4. Manual QA on Windows 11 VM (screenshot on/off).

---

### Phase D — Linux Implementation (Best-Effort)

**Goal:** Blur-behind on KDE/KWin; graceful no-op elsewhere.
**Depends on:** Phase A (interface + observer).
**Sequential after:** Phase B/C (lower priority, higher risk).
**Commit:** `feat(vibrancy/linux): best-effort blur-behind via X11 atom + KWin Wayland protocol`

#### D.1: Linux vibrancy controller

**Files (NEW):**
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller_linux.h`
- `maho-chromium/browser/ui/theme/maho_vibrancy_controller_linux.cc`

**Design (X11 path):**
```cpp
void MahoVibrancyControllerLinux::ApplyX11(x11::Window xwindow, bool enabled) {
  auto* connection = x11::Connection::Get();
  x11::Atom blur_atom = x11::GetAtom("_KDE_NET_WM_BLUR_BEHIND_REGION");
  if (blur_atom == x11::Atom::None) {
    LOG(INFO) << "Window vibrancy: _KDE_NET_WM_BLUR_BEHIND_REGION not available";
    return;
  }

  if (enabled) {
    uint32_t data[1] = {0};
    connection->SetProperty(xwindow, blur_atom, x11::Atom::CARDINAL,
                            base::span<const uint32_t>(data, 1));
  } else {
    connection->DeleteProperty(xwindow, blur_atom);
  }
  LOG(INFO) << "Window vibrancy (X11): " << (enabled ? "enabled" : "disabled");
}
```

**Design (Wayland path):**
- Check for `org_kde_kwin_blur_manager` global in Wayland registry.
- If available: create blur object, set region (empty = full surface), commit.
- If unavailable: log info, no-op.

#### D.2: TDD

1. Write unit test with mock X11 connection: atom available → property set/deleted.
2. Write unit test: atom unavailable → no crash, log emitted.
3. Implement.
4. QA on KDE Plasma 6 (screenshot); verify GNOME → no-op (no error, stays opaque).

---

### Phase E — Integration + Verification

**Goal:** End-to-end validation, startup hydration, and QA.
**Depends on:** Phases A + at least one of B/C/D.
**Commit:** `test(vibrancy): add integration tests + startup hydration from maho-core`

#### E.1: Startup hydration

**Files:**
- `maho-chromium/browser/maho_browser_main_extra_parts.cc` — In `PostProfileInit()`, after extension bridge init:

```cpp
// Hydrate vibrancy pref from maho-core (FFI → Chromium pref sync on startup).
if (MahoCore* core = maho::GetCore()) {
  char* json = maho_core_get_settings(core);
  if (json) {
    auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    maho_string_free(json);
    if (parsed && parsed->is_dict()) {
      if (const auto* appearance = parsed->GetDict().FindDict("appearance")) {
        auto transparency = appearance->FindBool("windowTransparency");
        if (transparency.has_value()) {
          profile->GetPrefs()->SetBoolean(
              maho::prefs::kWindowTransparency, *transparency);
        }
      }
    }
  }
}
```

#### E.2: Regression tests

1. Toggle vibrancy 50× rapidly (loop in uitest) → no crash, no leak (Instruments/Valgrind).
2. macOS: validate `NSWindow.opaque` state matches pref.
3. Windows: validate no title-bar artifact by checking DWM composition state.
4. Linux GNOME: verify no error logs, window stays opaque.
5. All: verify feature flag=disabled means zero platform API calls.

#### E.3: Screenshot QA harness

**Files (NEW):**
- `maho-chromium/browser/ui/theme/vibrancy_interactive_uitest.cc` (cross-platform test runner)
- `maho-chromium/browser/ui/theme/vibrancy_interactive_uitest_mac.mm` (macOS-specific assertions)

---

## Dependency Graph

```
┌──────────────────────────────────────────────────┐
│              WAVE 1 — Infrastructure             │
│                                                  │
│  ┌────────────────────────────────────────────┐  │
│  │  Phase A: Flag + Pref + Interface +        │  │
│  │           Observer + Settings Handler      │  │
│  └────────────────────────────────────────────┘  │
└───────────────────────┬──────────────────────────┘
                        │
         ┌──────────────┼──────────────┐
         ▼              ▼              ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│  WAVE 2      │ │  WAVE 2      │ │  WAVE 3      │
│              │ │              │ │  (sequential) │
│  Phase B:    │ │  Phase C:    │ │              │
│  macOS impl  │ │  Windows     │ │  Phase D:    │
│              │ │  impl        │ │  Linux impl  │
└──────┬───────┘ └──────┬───────┘ └──────┬───────┘
       │                │                │
       └────────────────┴────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────┐
│              WAVE 4 — Verification               │
│                                                  │
│  ┌────────────────────────────────────────────┐  │
│  │  Phase E: Startup hydration + integration  │  │
│  │           tests + screenshot QA            │  │
│  └────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────┘
```

**Serialization constraints:**
- Phase A must complete before any platform phase starts.
- Phase B and Phase C are fully parallel (independent platforms).
- Phase D is sequential after B/C (lower priority; resource efficiency).
- Phase E requires Phase A + at least one platform implementation.

**Critical path:** A → B → E (macOS is the primary development platform).

---

## Files Touched Matrix

| File | Status | Phase | Description |
|------|--------|-------|-------------|
| `browser/maho_features.h` | MOD | A | Add `kMahoWindowVibrancy` declaration |
| `browser/maho_features.cc` | MOD | A | Add `kMahoWindowVibrancy` definition (disabled by default) |
| `browser/ui/webui/maho_settings/maho_settings_prefs.h` | MOD | A | Add `kWindowTransparency` pref key |
| `browser/ui/webui/maho_settings/maho_settings_prefs.cc` | MOD | A | Register pref |
| `browser/ui/webui/maho_settings/maho_settings_page_handler.cc` | MOD | A | Dual-write to pref on line 1449 |
| `browser/ui/theme/maho_vibrancy_controller.h` | NEW | A | Virtual interface |
| `browser/ui/theme/maho_vibrancy_controller.cc` | NEW | B | Platform factory |
| `browser/ui/theme/maho_vibrancy_observer.h` | NEW | A | Pref observer + BrowserListObserver |
| `browser/ui/theme/maho_vibrancy_observer.cc` | NEW | A | Implementation |
| `browser/ui/theme/maho_vibrancy_observer_unittest.cc` | NEW | A | Unit tests |
| `browser/ui/theme/maho_vibrancy_controller_mac.h` | NEW | B | macOS interface |
| `browser/ui/theme/maho_vibrancy_controller_mac.mm` | NEW | B | NSVisualEffectView impl |
| `browser/ui/theme/maho_vibrancy_controller_win.h` | NEW | C | Windows interface |
| `browser/ui/theme/maho_vibrancy_controller_win.cc` | NEW | C | Mica/DWM impl |
| `browser/ui/theme/maho_vibrancy_controller_linux.h` | NEW | D | Linux interface |
| `browser/ui/theme/maho_vibrancy_controller_linux.cc` | NEW | D | X11/Wayland blur |
| `browser/ui/theme/vibrancy_interactive_uitest.cc` | NEW | E | Cross-platform test |
| `browser/ui/theme/vibrancy_interactive_uitest_mac.mm` | NEW | E | macOS-specific test |
| `browser/maho_browser_main_extra_parts.cc` | MOD | E | Startup hydration |
| `browser/ui/theme/BUILD.gn` | MOD | A-E | Add new sources |

---

## Verification Harness

### macOS Interactive UI Test: `vibrancy_interactive_uitest_mac.mm`

```objc
IN_PROC_BROWSER_TEST_F(MahoVibrancyMacTest, EnableDisableVibrancy) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(maho::features::kMahoWindowVibrancy);

  browser()->profile()->GetPrefs()->SetBoolean(
      maho::prefs::kWindowTransparency, true);

  NSWindow* window = browser()->window()->GetNativeWindow().GetNativeNSWindow();
  NSView* vibrancy = FindSubviewByIdentifier(window.contentView,
                                              @"MahoVibrancyBackdrop");
  ASSERT_NE(vibrancy, nil);
  EXPECT_FALSE(window.isOpaque);

  browser()->profile()->GetPrefs()->SetBoolean(
      maho::prefs::kWindowTransparency, false);

  vibrancy = FindSubviewByIdentifier(window.contentView,
                                      @"MahoVibrancyBackdrop");
  EXPECT_EQ(vibrancy, nil);
  EXPECT_TRUE(window.isOpaque);
}
```

### Cross-Platform Observer Test: `maho_vibrancy_observer_unittest.cc`

```cpp
TEST_F(MahoVibrancyObserverTest, PrefChangeTriggersApply) {
  auto mock_controller = std::make_unique<MockMahoVibrancyController>();
  EXPECT_CALL(*mock_controller, ApplyToBrowserWindow(_, true)).Times(1);

  observer_->SetControllerForTesting(std::move(mock_controller));
  prefs()->SetBoolean(maho::prefs::kWindowTransparency, true);
  task_environment_.RunUntilIdle();
}

TEST_F(MahoVibrancyObserverTest, FeatureFlagDisabled_NoApply) {
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(maho::features::kMahoWindowVibrancy);

  auto mock_controller = std::make_unique<MockMahoVibrancyController>();
  EXPECT_CALL(*mock_controller, ApplyToBrowserWindow(_, _)).Times(0);

  observer_->SetControllerForTesting(std::move(mock_controller));
  prefs()->SetBoolean(maho::prefs::kWindowTransparency, true);
  task_environment_.RunUntilIdle();
}
```

---

## Commit Strategy

| Phase | Commit Message | Files Touched | Gate |
|-------|---------------|---------------|------|
| A | `feat(vibrancy): add MahoVibrancyController interface + kMahoWindowVibrancy flag` | 10 | `autoninja -C out/Default unit_tests && out/Default/unit_tests --gtest_filter="*Vibrancy*"` |
| B | `feat(vibrancy/mac): implement NSVisualEffectView-based window vibrancy` | 4 | `out/Default/interactive_ui_tests --gtest_filter="*VibrancyMac*"` |
| C | `feat(vibrancy/win): implement Mica backdrop via DwmSetWindowAttribute` | 2 | `out/Default/unit_tests --gtest_filter="*VibrancyWin*"` |
| D | `feat(vibrancy/linux): best-effort blur-behind via X11 atom + KWin protocol` | 2 | `out/Default/unit_tests --gtest_filter="*VibrancyLinux*"` |
| E | `test(vibrancy): integration tests + startup hydration + flip feature flag` | 3 | Full `interactive_ui_tests --gtest_filter="*Vibrancy*"` green |

Each phase is one atomic commit. Phase E flips `FEATURE_DISABLED_BY_DEFAULT` → `FEATURE_ENABLED_BY_DEFAULT` after all platform tests pass.

---

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| macOS NSVisualEffectView z-order regression (covers compositor layers) | Medium | High | Insert at `NSWindowBelow` relative to nil; validate in uitest that content is visible above |
| Windows Mica flicker on window drag | Low | Medium | Set `DWMWA_USE_IMMERSIVE_DARK_MODE` + `DWMWA_CAPTION_COLOR` to avoid DWM re-evaluation during drag |
| Linux compositor fragmentation (GNOME, wlroots, no standard) | High | Low | Explicit no-op path; log `INFO`-level message; document as "best-effort KDE support" |
| Performance: subview repaint cost on macOS | Low | Low | NSVisualEffectView is GPU-composited; only toggles on explicit user preference change |
| Feature flag left disabled in shipping build | — | — | Phase E explicitly flips to enabled; CI gate on interactive_uitests passing |
| Custom title bar interaction (tab strip over vibrant background illegible) | Medium | Medium | Color mixer already resolves dark/light from `kColorWindowBackground`; vibrancy controller nudges color provider key for transparent mode |

---

## Open Questions

1. **Default state**: Current default is `false` (opaque). Should this remain? **Recommendation:** Keep `false` — vibrancy is opt-in.
2. **Linux X11 support**: Ship or defer to Phase F? **Recommendation:** Ship as best-effort in Phase D; graceful no-op on unsupported compositors.
3. **Tab strip legibility**: Over vibrant backgrounds, tab text may lose contrast. **Recommendation:** If the color mixer's window-background color resolves < 40% opacity, auto-apply a subtle scrim behind tab row. Address post-E if observed.
4. **Interaction with space themes**: Space themes apply tints via `MahoSpaceThemeState`. Should vibrancy be disabled when a space theme with solid background is active? **Recommendation:** Allow both — user controls transparency independently of space theme.
5. **Multiple monitors with different DPI**: macOS `NSVisualEffectView` handles retina natively. Windows Mica is per-window. No action needed.

---

## Estimated Work

| Phase | Sessions | Risk | Notes |
|-------|----------|------|-------|
| A — Shared infra | 1 | Low | Interface + prefs + observer pattern well-established |
| B — macOS | 1.5 | Medium | NSVisualEffectView z-order requires careful testing |
| C — Windows | 1.5 | Medium | DWM API straightforward but needs Win11 VM for QA |
| D — Linux | 1.5 | High | Compositor fragmentation; best-effort |
| E — Verification | 1 | Low | Test harness + startup hydration |
| **Total** | **6.5** | | **Critical path: A → B → E (4 sessions)** |

---

## Parallel Execution Graph

```
Wave 1 (Start immediately):
└── Phase A: Shared infra (flag, pref, interface, observer)

Wave 2 (After Wave 1):
├── Phase B: macOS NSVisualEffectView (no dep on C/D)
└── Phase C: Windows Mica (no dep on B/D)

Wave 3 (After Wave 2):
└── Phase D: Linux blur-behind (resource-sequential, not blocked)

Wave 4 (After A + any platform phase):
└── Phase E: Integration tests + startup hydration + flag flip
```

Critical Path: A (1 session) → B (1.5 sessions) → E (1 session) = **3.5 sessions minimum**.
Total with parallelism: **~4.5 sessions** (C runs parallel to B; D is tail work).
