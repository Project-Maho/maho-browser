# SF Symbol → Lucide Icon System Migration

**Date**: 2026-05-30
**Author**: Sisyphus
**Status**: Ready for delegation
**Predecessor**: `.sisyphus/plans/icon-system-cross-platform-registry-migration-plan-2026-05-25.md` (superseded — this plan replaces the registry-only approach with a concrete Lucide migration)

---

## §1 Goal

Replace all Apple SF Symbol references with **Lucide** (ISC-licensed, cross-platform) as the unified icon system across the Maho Browser monorepo — macOS Native, iOS Native, Android Compose, Chromium Native Views (C++), and Chromium WebUI (HTML/CSS/TS + React) — achieving a single source-of-truth icon library with identical aesthetic across all five surfaces.

---

## §2 Acceptance Criteria

- [ ] No `imageWithSystemSymbolName:`, `Image(systemName:)`, `UIImage(systemName:)`, `NSImage(systemSymbolName:)` calls remain in any Swift code
- [ ] No SF Symbol literal strings (`"[a-z]+(\.[a-z0-9]+)+"` pattern) remain in `maho-chromium/browser/` C++/header source files
- [ ] `maho_ai_sf_symbol_source.{h,cc,mm}` deleted
- [ ] `maho_mac_sf_symbol_helper.{h,mm}` deleted
- [ ] `maho_sidebar_top_bar_mac_sf_symbol_helper.{h,mm}` deleted
- [ ] All `MahoIcon.swift` (macOS 89 cases, iOS 43 cases) enums migrated to Lucide-backed implementations
- [ ] `MahoIcon.kt` migrated from Material Icons to Lucide AVD resources
- [ ] `cargo test --workspace` green
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` exit 0
- [ ] `Maho.app` launchable; visual smoke check on all icon-bearing UI surfaces (sidebar, toolbar, AI panel, settings, location bar utility panel, screenshot dialog, library rail, archive view, boost editor)
- [ ] Lucide ISC license attribution added to `LICENSES/lucide.txt`
- [ ] Unified mapping JSON `maho/assets/icons/sf-to-lucide-mapping.json` exists with all entries
- [ ] Each WU's verification step demonstrably passes (LSP clean, tests green, visual diff acceptable)

---

## §3 Non-Goals

- Visual pixel-parity with SF Symbol's hierarchical/multicolor variants (Lucide is single-color line — known divergence)
- Custom-drawing icons not present in Lucide's 1500-icon set (accept closest match)
- Changing icon sizes or button hit areas
- Refactoring icon usage patterns into a shared Icon component (swap implementations only)
- Redesigning Android UI to look "non-Material"
- Migrating all Material Icons usage scattered outside `MahoIcon.kt` (broader sweep deferred)
- Icon animation / Lottie assets

---

## §4 Constraints

- `.build.lock` serializes Chromium build steps
- GN file edits for new `aggregate_vector_icons` targets require **explicit user permission** (per `AGENTS.md`)
- License attribution required before shipping
- macOS-specific SF Symbol helpers must be deleted ONLY after all callers migrated; phase ordering critical
- Chromium overlay symlink: `maho/browser/...` paths used from within `chromium/src/`
- Three Cargo workspaces: `maho/`, `maho/relay/`, `mail/`
- No modifications to upstream `chromium/src/` files

---

## §5 Parallelism Topology

```
Phase 0 (Foundation)
┌──────────────────────┐
│  WU-0: Mapping JSON  │
│  + License + Docs    │
└──────────┬───────────┘
           │
Phase 1 (WebUI — parallel)
┌─────────────┐ ┌──────────────┐ ┌──────────────┐
│  WU-1: Mail │ │  WU-2: AI    │ │  WU-3: Boost │
└──────┬──────┘ └──────┬───────┘ └──────┬───────┘
       └───────────────┼───────────────┘
                       │
Phase 2 (Chromium Native — sequential, GN approval gate)
               ┌───────────────┐
               │  WU-4: C++    │
               │  Native Views │
               └───────┬───────┘
                       │
Phase 3 (Swift shells — parallel)
         ┌─────────────┼─────────────┐
   ┌─────┴─────┐ ┌────┴────┐
   │  WU-5:    │ │  WU-6:  │
   │  macOS    │ │  iOS    │
   └─────┬─────┘ └────┬────┘
         └────────────┼─────────────┐
                                    │
Phase 4 (Android — independent)
               ┌──────┴──────┐
               │  WU-7:      │
               │  Android    │
               └──────┬──────┘
                      │
Phase 5 (Verification)
               ┌──────┴──────┐
               │  WU-8:      │
               │  Cleanup    │
               └─────────────┘
```

Key ordering:
- WU-0 must complete before all subsequent WUs (mapping JSON is input)
- WU-1, WU-2, WU-3 are independent (parallel)
- WU-4 depends on WU-2 completion (AI WebUI deletion affects C++ `maho_ai_ui.cc`)
- WU-5, WU-6 depend on WU-4 (C++ helper deletion after all C++ callers migrated)
- WU-7 independent of WU-5/6 but ordered after WU-0
- WU-8 final gate

---

## §6 Work Units

---

### WU-0: Master Mapping JSON + License + Architecture Doc

**Severity**: Foundation — blocks all subsequent WUs
**Owner**: `task(category="writing", load_skills=[], ...)`
**Estimated effort**: 30 minutes

#### Files

| Path | Edit type |
|------|-----------|
| `maho/assets/icons/sf-to-lucide-mapping.json` | New |
| `LICENSES/lucide.txt` | New |
| `docs/icon-system.md` | New |

#### Spec

1. Create `maho/assets/icons/sf-to-lucide-mapping.json` with all entries from §A APPENDIX (schema: `sf_symbol`, `lucide_kebab`, `lucide_js`, `lucide_swift`, `avd_filename`, `cpp_symbol`)
2. Create `LICENSES/lucide.txt` with ISC notice (see APPENDIX B)
3. Create `docs/icon-system.md` documenting Lucide as canonical, per-platform naming conventions, and how to add new icons

#### Verification

```bash
python3 -c "import json; data = json.load(open('maho/assets/icons/sf-to-lucide-mapping.json')); names = [e['sf_symbol'] for e in data]; assert len(names) == len(set(names)), f'Duplicates'; print(f'OK: {len(data)} entries')"
```

#### Risk

None.

---

### WU-1: Maho Mail — `lucide-react` Integration

**Severity**: Low
**Owner**: `task(category="visual-engineering", load_skills=["frontend-ui-ux"], ...)`
**Estimated effort**: 20 minutes

#### Files

| Path | Edit type |
|------|-----------|
| `mail/package.json` | Modify (add dependency) |
| `mail/src/**/*.tsx` | Modify (replace icon usages) |

#### Spec

1. `cd mail && bun add lucide-react`
2. Find all icon usages
3. Replace with `lucide-react` per-icon imports:
   ```tsx
   import { ArrowLeft, Settings, Search } from 'lucide-react';
   ```
4. Ensure tree-shaking via named imports

#### Verification

```bash
cd mail && bun run typecheck && bun run test
```

#### Risk

Minimal. Mail is isolated Tauri app.

---

### WU-2: maho-chromium AI WebUI — Replace SF Symbol URL Handler

**Severity**: High
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 90 minutes

#### Files

| Path | Edit type |
|------|-----------|
| `maho-chromium/browser/resources/maho_ai/views/icons.ts` | Rewrite |
| `maho-chromium/browser/resources/maho_ai/standalone/icons_stub.ts` | Rewrite |
| `maho-chromium/browser/resources/maho_ai/react/ui/icon.tsx` | Modify |
| `maho-chromium/browser/resources/maho_ai/react/ui/icon-button.tsx` | Modify |
| `maho-chromium/browser/resources/maho_ai/react/features/compact/conversation-message.tsx` | Modify |
| `maho-chromium/browser/resources/maho_ai/icons/` | New directory — 16 Lucide SVGs |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai_sf_symbol_source.{h,cc,mm}` | Delete |
| `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.cc` | Modify (remove sf-symbol URL handler) |
| `maho-chromium/browser/ui/webui/maho_ai/BUILD.gn` | Modify (remove sf_symbol sources) |

#### Spec

**Phase A — Add Lucide SVGs:**
Copy 16 Lucide SVGs (matching the allowlist) into `browser/resources/maho_ai/icons/`:
`arrow-up-right.svg`, `sparkles.svg`, `copy.svg`, `pencil.svg`, `ellipsis.svg`, `globe.svg`, `plus.svg`, `rotate-cw.svg`, `settings.svg`, `circle-stop.svg`, `thumbs-down.svg`, `thumbs-up.svg`, `trash-2.svg`, `panel-right.svg`, `x.svg`. Each must use `currentColor` for stroke.

**Phase B — Rewrite `views/icons.ts`:**
```typescript
export type IconSymbol =
    'arrowUpRight'|'composerSpark'|'copy'|'edit'|'ellipsis'|'globe'|
    'panelSpark'|'plus'|'retry'|'settingsGear'|'stop'|'thumbDown'|
    'thumbUp'|'trash'|'viewModeToggle'|'xmark';

const ICON_MAP: Record<IconSymbol, string> = {
  arrowUpRight: 'arrow-up-right',
  composerSpark: 'sparkles',
  copy: 'copy',
  edit: 'pencil',
  ellipsis: 'ellipsis',
  globe: 'globe',
  panelSpark: 'sparkles',
  plus: 'plus',
  retry: 'rotate-cw',
  settingsGear: 'settings',
  stop: 'circle-stop',
  thumbDown: 'thumbs-down',
  thumbUp: 'thumbs-up',
  trash: 'trash-2',
  viewModeToggle: 'panel-right',
  xmark: 'x',
};

export function getIconUrl(symbol: IconSymbol): string {
  return `chrome://maho-ai/icons/${ICON_MAP[symbol]}.svg`;
}
```

**Phase C — Update React components:**
For files using `lucide-react` directly (compact panel):
```tsx
import { ArrowUpRight, Sparkles, Copy } from 'lucide-react';
```

**Phase D — Remove SF Symbol infrastructure:**
1. Delete `maho_ai_sf_symbol_source.{h,cc,mm}`
2. Remove the `sf-symbol/` URL path handler from `maho_ai_ui.cc`
3. Update `BUILD.gn` to remove deleted source files
4. Add `icons/` directory to GRIT resource bundle

#### Verification

```bash
python3 maho-chromium/build/scripts/build_maho.py
# Visual: chrome://maho-ai panel renders all 16 icons correctly
```

#### Risk

- BUILD.gn edit (removing sources, adding GRIT resources) — standard pattern
- React component API stays same (`IconSymbol` type unchanged) — downstream consumers unaffected

---

### WU-3: maho-chromium Boost WebUI — Unify Hand-SVGs with Lucide

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 45 minutes

#### Files

| Path | Edit type |
|------|-----------|
| `maho-chromium/browser/resources/maho_boost/icons/*.svg` | Replace (12 files) |

#### Spec

Replace 12 hand-authored SVGs with Lucide equivalents (preserve filenames):

| Current file | Lucide icon |
|---|---|
| `back.svg` | `arrow-left` |
| `close.svg` | `x` |
| `code.svg` | `code` |
| `controls.svg` | `sliders-horizontal` |
| `disable.svg` | `circle-slash` |
| `inspector.svg` | `search` |
| `invert.svg` | `contrast` |
| `magic.svg` | `sparkles` |
| `picker.svg` | `crosshair` |
| `shuffle.svg` | `shuffle` |
| `zap.svg` | `zap` |
| `arrow-down.svg` | `chevron-down` |

Each replacement must use `currentColor`, 24×24 viewBox, `stroke-width="2"`, `stroke-linecap="round"`, `stroke-linejoin="round"`.

#### Verification

```bash
python3 maho-chromium/build/scripts/build_maho.py
# Visual: chrome://maho-boost editor shows all 12 toolbar icons
```

#### Risk

Low — pure SVG swap, filenames preserved.

---

### WU-4: maho-chromium Native Views (C++) — Lucide Vector Icons

**Severity**: Critical — largest WU
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 4-6 hours

#### Files

| Path | Edit type |
|------|-----------|
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc` | Major rewrite |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc` | Rewrite |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc` | Modify |
| `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.cc` | Modify |
| `maho-chromium/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc` | Major rewrite |
| `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.cc` | Modify |
| `maho-chromium/browser/ui/views/location_bar/maho_dom_screenshot_handler.cc` | Modify |
| `maho-chromium/browser/ui/views/maho_mac_sf_symbol_helper.{h,mm}` | Delete |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_mac_sf_symbol_helper.{h,mm}` | Delete |
| `maho-chromium/browser/ui/views/maho_lucide_icons/*.icon` | New (~28 files) |
| `maho-chromium/browser/ui/views/maho_lucide_icons/BUILD.gn` | New |
| `maho-chromium/browser/ui/views/maho_lucide_icons/vector_icons.{cc,h}.template` | New |

#### Spec

**Step 1 — Create `.icon` files:**
Use Skiafy (see APPENDIX C) to convert ~28 Lucide SVGs → `.icon`:
`chevron_left.icon`, `chevron_right.icon`, `chevron_down.icon`, `rotate_cw.icon`, `x.icon`, `panel_left.icon`, `arrow_down_to_line.icon`, `archive.icon`, `copy.icon`, `images.icon`, `pencil_line.icon`, `notebook_text.icon`, `undo_2.icon`, `filter.icon`, `share.icon`, `puzzle.icon`, `shield_half.icon`, `picture_in_picture_2.icon`, `zoom_in.icon`, `zoom_out.icon`, `shield_check.icon`, `shield_alert.icon`, `shield_x.icon`, `shield_question.icon`, `inbox.icon`, `sliders_horizontal.icon`

**Step 2 — Create GN target** (USER PERMISSION REQUIRED):
```gn
# maho-chromium/browser/ui/views/maho_lucide_icons/BUILD.gn
import("//components/vector_icons/vector_icons.gni")

aggregate_vector_icons("maho_lucide_icons") {
  icon_directory = "."
  sources = [
    "archive.icon",
    "arrow_down_to_line.icon",
    # ... all icons
  ]
}

source_set("maho_lucide_icons") {
  sources = [
    "$target_gen_dir/vector_icons.cc",
    "$target_gen_dir/vector_icons.h",
  ]
  deps = [ ":maho_lucide_icons", "//ui/gfx", ]
}
```
Plus copy `vector_icons.cc.template` and `vector_icons.h.template` from `chromium/src/components/vector_icons/`.

**Step 3 — Migrate callsites:**
Replace `CreateMahoMacSFSymbol("symbol.name", ...)` with:
```cpp
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"

ui::ImageModel::FromVectorIcon(
    maho_lucide_icons::kArchiveIcon, color_id, size)
```

`RailCategorySpec` struct loses `sf_symbol_name` field:
```cpp
struct RailCategorySpec {
  Category category;
  std::u16string_view title;
  std::u16string_view visible_label;
  raw_ptr<const gfx::VectorIcon> icon;  // Lucide-backed
  std::string_view ascii_title;
  bool visible_in_first_entry_rail;
};
```

**Step 4 — Delete SF Symbol helpers:**
After verifying all callers migrated:
- Delete `maho_mac_sf_symbol_helper.{h,mm}`
- Delete `maho_sidebar_top_bar_mac_sf_symbol_helper.{h,mm}`
- Update respective `BUILD.gn` to remove from `sources`

**Step 5 — Update tests:**
Replace `EXPECT_EQ(*.sf_symbol_name, "...")` with vector icon pointer comparisons:
```cpp
EXPECT_EQ(action.icon, &maho_lucide_icons::kShareIcon);
```

#### Verification

```bash
cd chromium/src && export PATH="/Users/indo/depot_tools:$PATH" && autoninja -C out/Default chrome
python3 maho-chromium/build/scripts/build_maho.py
# Grep verification:
grep -rn "sf_symbol\|CreateMahoMacSFSymbol\|CreateSidebarTopBarMacSFSymbol" maho-chromium/browser/  # must be 0
```

#### Risk

- **GN edit gate**: requires explicit user permission for new `aggregate_vector_icons` target
- **Skiafy manual**: ~28 icons must be converted via web tool (not automated)
- **Struct ABI change**: `sf_symbol_name` string → VectorIcon pointer is non-trivial refactor; all consumers updated simultaneously
- **Test assertions**: ~35 references must be updated
- **Cross-platform**: Linux/Windows previously got empty images (helper was macOS-only); now get Lucide vector icons — net improvement

---

### WU-5: macOS Swift Shell — `lucide-icons-swift` Migration

**Severity**: High
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 2-3 hours

#### Files

| Path | Edit type |
|------|-----------|
| `maho/macos-shell/Package.swift` | Modify (add dependency) |
| `maho/macos-shell/Sources/Shared/MahoIcon.swift` | Major rewrite (89 cases) |

#### Spec

**Step 1 — Add SwiftPM dependency:**
```swift
.package(url: "https://github.com/JakubMazur/lucide-icons-swift", from: "1.0.0"),
```

**Step 2 — Rewrite `MahoIcon.swift`:**
Preserve public API (`MahoIcon` enum with `.image: NSImage?` property). Internal `lucideImage` switch maps each case to `Lucide.camelCase`:
```swift
import AppKit
import LucideIcons

public enum MahoIcon: String, CaseIterable {
    case navBack
    // ... all 89 cases ...

    public var image: NSImage? {
        return lucideImage?.withRenderingMode(.template)  // enable tinting
    }

    private var lucideImage: NSImage? {
        switch self {
        case .navBack: return Lucide.chevronLeft
        case .close: return Lucide.x
        case .reloadSync: return Lucide.rotateCw
        // ... per APPENDIX A mapping ...
        }
    }
}
```
Remove `symbolName` property and `dynamicSymbol` method. Audit any direct callers.

#### Verification

```bash
cd maho/macos-shell && make app && make test
grep -r "systemSymbolName\|systemName" maho/macos-shell/Sources/  # must be 0
```

#### Risk

- Package adds ~5-8 MB to app bundle (PDF xcassets) — confirm acceptable
- API compatibility: callers using `.symbolName` directly need updates
- `dynamicSymbol(named:)` callers (runtime arbitrary names) will break — must audit

---

### WU-6: iOS Swift Shell — `lucide-icons-swift` Migration

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 1-2 hours

#### Files

| Path | Edit type |
|------|-----------|
| `maho/ios-shell/Package.swift` or `project.yml` | Modify |
| `maho/ios-shell/Support/MahoIcon.swift` | Rewrite (43 cases) |

#### Spec

Same package (universal). Returns `UIImage` instead of `NSImage`:
```swift
import UIKit
import SwiftUI
import LucideIcons

public enum MahoIcon: String, CaseIterable {
    // ... 43 cases ...

    public var image: UIImage? {
        return lucideUIImage?.withRenderingMode(.alwaysTemplate)
    }

    public var imageVector: Image {
        return Image(uiImage: image ?? UIImage())
    }

    private var lucideUIImage: UIImage? {
        switch self {
        case .navBack: return Lucide.chevronLeft
        // ... per APPENDIX A ...
        }
    }
}
```

#### Verification

```bash
cd maho/ios-shell && make test
grep -r "systemName" maho/ios-shell/  # must be 0
```

#### Risk

- Same package size concern as WU-5
- iOS simulator visual testing recommended

---

### WU-7: Android Compose Shell — Lucide AVD Migration

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)`
**Estimated effort**: 2-3 hours

#### Files

| Path | Edit type |
|------|-----------|
| `maho/android-shell/app/src/main/res/drawable/ic_*.xml` | New (~70 AVD files) |
| `maho/android-shell/app/src/main/java/dev/maho/browser/ui/icons/MahoIcon.kt` | Rewrite |

#### Spec

**Step 1 — Generate AVDs:**
Extract Lucide SVGs from `lucide-static`. Convert via Android Studio Vector Asset Studio or `vd-tool` CLI:
```bash
for svg in arrow-left.svg chevron-left.svg x.svg ...; do
  vd-tool -c -in "$svg" -out "app/src/main/res/drawable/ic_${svg%.svg}.xml"
done
```

**Step 2 — Post-process AVDs:**
Replace hardcoded `android:strokeColor="#000000"` with `?attr/colorOnSurface` for theming.

**Step 3 — Rewrite `MahoIcon.kt`:**
```kotlin
package dev.maho.browser.ui.icons

import androidx.annotation.DrawableRes
import dev.maho.browser.R

enum class MahoIcon(@DrawableRes val drawableRes: Int) {
    NavBack(R.drawable.ic_chevron_left),
    NavForward(R.drawable.ic_chevron_right),
    Close(R.drawable.ic_x),
    ReloadSync(R.drawable.ic_rotate_cw),
    // ... per APPENDIX A ...
}
```

**Step 4 — Update Compose callers:**
```kotlin
Icon(
    painter = painterResource(id = MahoIcon.NavBack.drawableRes),
    contentDescription = "Back",
    tint = MaterialTheme.colorScheme.onSurface,
)
```

#### Verification

```bash
cd maho/android-shell && ./gradlew app:testDebugUnitTest
grep -r "Icons\." maho/android-shell/app/src/main/java/dev/maho/browser/ui/icons/  # must be 0
```

#### Risk

- AVD theming: hardcoded stroke colors must be post-processed
- Compose API change: `imageVector: ImageVector` → `@DrawableRes Int` changes caller patterns
- `vd-tool` may need Android SDK cmdline tools installed

---

### WU-8: Final Cleanup + Verification Gate

**Severity**: Mandatory
**Owner**: Direct (no delegation)
**Estimated effort**: 30 minutes

#### Steps

1. Wait for WU-1 through WU-7 success
2. Comprehensive grep:
   ```bash
   grep -rn "systemSymbolName\|systemName\|NSImage(systemSymbolName" maho/macos-shell/Sources/ maho/ios-shell/
   grep -rn "sf_symbol\|SFSymbol\|CreateMahoMacSFSymbol\|CreateSidebarTopBarMacSFSymbol" maho-chromium/browser/
   grep -rn "sf-symbol" maho-chromium/browser/resources/
   ```
   All must return 0.
3. `python3 maho-chromium/build/scripts/build_maho.py`
4. `cd maho && bun run test`
5. Update `AGENTS.md` (root) — add icon system row to "WHERE TO LOOK"
6. Update `maho-chromium/AGENTS.md` to reference Lucide vector icons location
7. Run test matrix per `maho/docs/test-matrix.md`

---

## §7 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Skiafy is manual web tool — bottleneck for WU-4 (~28 icons) | High | Medium | Batch sessions; cache outputs; consider minimal SVG→icon path parser |
| `lucide-icons-swift` adds 5-8 MB to app bundle | Medium | Low | Confirm acceptable; alternative: vendor only needed icons as PDFs |
| Lucide lacks compositional SF Symbols (`sparkles.rectangle.stack`) | Medium | Low | Use closest match; document in mapping |
| Android AVD hardcoded `strokeColor` | High | Medium | Post-process script: replace `#000000` with theme attr |
| Removing helper before all callers migrated breaks build | High | High | Phase ordering enforces deletion only in WU-4 Step 4 |
| SF Symbol filled variants collapse to line variants | Medium | Low | Accepted divergence; document in mapping |
| `aggregate_vector_icons` GN target needs user approval | Low | High | Explicitly request approval before WU-4 |
| Utility panel struct change cascades to ~35 test assertions | High | Medium | Update tests as part of WU-4 Step 5 |
| Cross-platform behavior change: Linux/Windows get real icons (was empty) | Low | Low (improvement) | Net positive |
| `lucide-react` bundle size impact on AI WebUI | Low | Low | Tree-shakable imports; only 16 icons; <10 KB total |

---

## §8 Deferred Items (with reason)

1. **Visual regression testing automation** (per-icon screenshot diff) — separate plan
2. **Custom-drawn icons** for SF Symbol compositions without Lucide equivalent — accept closest match in this plan; revisit if user feedback demands
3. **Material Icons → Lucide for ALL Android Compose UI** — this plan covers `MahoIcon.kt` enum only; broader sweep deferred
4. **Icon-set version pinning policy** — separate ops plan
5. **Lucide icon font approach** — per-icon SVG tree-shaking is preferred for bundle size
6. **`aggregate_vector_icons` automation script** (SVG→`.icon` CLI) — manual Skiafy acceptable for initial 28 icons
7. **Removing `symbolName` property from Swift `MahoIcon`** — follow-up audit
8. **Workspace gradient integration for `magic-theme`** — Plan §7-2 of zen-boost cleanup; orthogonal

---

## §9 Atomic Commit Strategy

One commit per WU. Phase-by-phase, never mix platforms.

| Commit | Message | Scope |
|--------|---------|-------|
| 1 | `feat(icons): add Lucide mapping JSON and license attribution` | WU-0 |
| 2 | `feat(mail): migrate to lucide-react icons` | WU-1 |
| 3 | `feat(ai-webui): replace SF Symbol URL handler with Lucide SVGs` | WU-2 |
| 4 | `feat(boost-webui): replace hand-drawn SVGs with Lucide` | WU-3 |
| 5 | `feat(chromium-views): add Lucide vector icons, remove SF Symbol helpers` | WU-4 |
| 6 | `feat(macos-shell): migrate MahoIcon to lucide-icons-swift` | WU-5 |
| 7 | `feat(ios-shell): migrate MahoIcon to lucide-icons-swift` | WU-6 |
| 8 | `feat(android-shell): migrate MahoIcon to Lucide AVDs` | WU-7 |
| 9 | `chore(icons): final cleanup, update AGENTS.md, verify zero SF references` | WU-8 |

Each commit independently buildable. WU-4 deletes SF Symbol helpers WITHIN its commit (not split) to prevent broken intermediate states.

---

## §10 Delegation Recipe

```python
# WU-0 — Foundation (sequential, blocks everything)
task(
  category="writing",
  load_skills=[],
  description="Lucide mapping JSON + license + docs",
  prompt="See .sisyphus/plans/sf-symbol-to-lucide-migration-2026-05-30.md §WU-0. Create maho/assets/icons/sf-to-lucide-mapping.json from APPENDIX A, LICENSES/lucide.txt with ISC notice, docs/icon-system.md architecture doc.",
  run_in_background=false
)

# Phase 1 — WebUI parallel
task(category="visual-engineering", load_skills=["frontend-ui-ux"], description="Mail lucide-react", prompt="See §WU-1...", run_in_background=true)
task(category="deep", load_skills=[], description="AI WebUI Lucide migration", prompt="See §WU-2...", run_in_background=true)
task(category="deep", load_skills=[], description="Boost WebUI Lucide SVGs", prompt="See §WU-3...", run_in_background=true)

# Phase 2 — C++ Native (sequential, GN approval required)
task(category="deep", load_skills=[], description="C++ Lucide vector icons", prompt="See §WU-4. GN edit pre-approved by user.", run_in_background=false)

# Phase 3 — Swift parallel
task(category="deep", load_skills=[], description="macOS lucide-icons-swift", prompt="See §WU-5...", run_in_background=true)
task(category="deep", load_skills=[], description="iOS lucide-icons-swift", prompt="See §WU-6...", run_in_background=true)

# Phase 4 — Android (parallel-safe with Phase 3)
task(category="deep", load_skills=[], description="Android Lucide AVDs", prompt="See §WU-7...", run_in_background=true)

# Phase 5 — WU-8 direct
```

---

## §A APPENDIX — SF Symbol → Lucide Mapping Table

Full mapping table covering all unique SF Symbol names found in the inventory (118 unique). Schema columns: `SF Symbol | Lucide kebab | JS PascalCase | Swift camelCase | AVD filename | C++ symbol`

| # | SF Symbol | Lucide | JS | Swift | AVD | C++ |
|---|---|---|---|---|---|---|
| 1 | `app.badge.checkmark` | `app-window` | `AppWindow` | `Lucide.appWindow` | `ic_app_window.xml` | `kAppWindowIcon` |
| 2 | `app.connected.to.app.below.fill` | `app-window-mac` | `AppWindowMac` | `Lucide.appWindowMac` | `ic_app_window_mac.xml` | `kAppWindowMacIcon` |
| 3 | `archivebox` | `archive` | `Archive` | `Lucide.archive` | `ic_archive.xml` | `kArchiveIcon` |
| 4 | `archivebox.fill` | `archive` | `Archive` | `Lucide.archive` | `ic_archive.xml` | `kArchiveIcon` |
| 5 | `arrow.backward` | `arrow-left` | `ArrowLeft` | `Lucide.arrowLeft` | `ic_arrow_left.xml` | `kArrowLeftIcon` |
| 6 | `arrow.clockwise` | `rotate-cw` | `RotateCw` | `Lucide.rotateCw` | `ic_rotate_cw.xml` | `kRotateCwIcon` |
| 7 | `arrow.counterclockwise` | `rotate-ccw` | `RotateCcw` | `Lucide.rotateCcw` | `ic_rotate_ccw.xml` | `kRotateCcwIcon` |
| 8 | `arrow.down.circle` | `arrow-down-circle` | `ArrowDownCircle` | `Lucide.arrowDownCircle` | `ic_arrow_down_circle.xml` | `kArrowDownCircleIcon` |
| 9 | `arrow.down.circle.fill` | `arrow-down-circle` | `ArrowDownCircle` | `Lucide.arrowDownCircle` | `ic_arrow_down_circle.xml` | `kArrowDownCircleIcon` |
| 10 | `arrow.down.to.line` | `arrow-down-to-line` | `ArrowDownToLine` | `Lucide.arrowDownToLine` | `ic_arrow_down_to_line.xml` | `kArrowDownToLineIcon` |
| 11 | `arrow.forward` | `arrow-right` | `ArrowRight` | `Lucide.arrowRight` | `ic_arrow_right.xml` | `kArrowRightIcon` |
| 12 | `arrow.left.and.right` | `move-horizontal` | `MoveHorizontal` | `Lucide.moveHorizontal` | `ic_move_horizontal.xml` | `kMoveHorizontalIcon` |
| 13 | `arrow.left.arrow.right` | `arrow-left-right` | `ArrowLeftRight` | `Lucide.arrowLeftRight` | `ic_arrow_left_right.xml` | `kArrowLeftRightIcon` |
| 14 | `arrow.right` | `arrow-right` | `ArrowRight` | `Lucide.arrowRight` | `ic_arrow_right.xml` | `kArrowRightIcon` |
| 15 | `arrow.triangle.2.circlepath` | `refresh-cw` | `RefreshCw` | `Lucide.refreshCw` | `ic_refresh_cw.xml` | `kRefreshCwIcon` |
| 16 | `arrow.up.forward` | `arrow-up-right` | `ArrowUpRight` | `Lucide.arrowUpRight` | `ic_arrow_up_right.xml` | `kArrowUpRightIcon` |
| 17 | `arrow.up.left` | `arrow-up-left` | `ArrowUpLeft` | `Lucide.arrowUpLeft` | `ic_arrow_up_left.xml` | `kArrowUpLeftIcon` |
| 18 | `arrow.up.right` | `arrow-up-right` | `ArrowUpRight` | `Lucide.arrowUpRight` | `ic_arrow_up_right.xml` | `kArrowUpRightIcon` |
| 19 | `arrow.uturn.backward` | `undo-2` | `Undo2` | `Lucide.undo2` | `ic_undo_2.xml` | `kUndo2Icon` |
| 20 | `bell` | `bell` | `Bell` | `Lucide.bell` | `ic_bell.xml` | `kBellIcon` |
| 21 | `bolt` | `zap` | `Zap` | `Lucide.zap` | `ic_zap.xml` | `kZapIcon` |
| 22 | `book` | `book-open` | `BookOpen` | `Lucide.bookOpen` | `ic_book_open.xml` | `kBookOpenIcon` |
| 23 | `bookmark` | `bookmark` | `Bookmark` | `Lucide.bookmark` | `ic_bookmark.xml` | `kBookmarkIcon` |
| 24 | `bubble.left.and.bubble.right` | `messages-square` | `MessagesSquare` | `Lucide.messagesSquare` | `ic_messages_square.xml` | `kMessagesSquareIcon` |
| 25 | `checkmark` | `check` | `Check` | `Lucide.check` | `ic_check.xml` | `kCheckIcon` |
| 26 | `checkmark.circle` | `circle-check` | `CircleCheck` | `Lucide.circleCheck` | `ic_circle_check.xml` | `kCircleCheckIcon` |
| 27 | `checkmark.circle.fill` | `circle-check` | `CircleCheck` | `Lucide.circleCheck` | `ic_circle_check.xml` | `kCircleCheckIcon` |
| 28 | `checkmark.shield` | `shield-check` | `ShieldCheck` | `Lucide.shieldCheck` | `ic_shield_check.xml` | `kShieldCheckIcon` |
| 29 | `chevron.backward` | `chevron-left` | `ChevronLeft` | `Lucide.chevronLeft` | `ic_chevron_left.xml` | `kChevronLeftIcon` |
| 30 | `chevron.down` | `chevron-down` | `ChevronDown` | `Lucide.chevronDown` | `ic_chevron_down.xml` | `kChevronDownIcon` |
| 31 | `chevron.forward` | `chevron-right` | `ChevronRight` | `Lucide.chevronRight` | `ic_chevron_right.xml` | `kChevronRightIcon` |
| 32 | `chevron.left` | `chevron-left` | `ChevronLeft` | `Lucide.chevronLeft` | `ic_chevron_left.xml` | `kChevronLeftIcon` |
| 33 | `chevron.left.chevron.right` | `chevrons-left-right` | `ChevronsLeftRight` | `Lucide.chevronsLeftRight` | `ic_chevrons_left_right.xml` | `kChevronsLeftRightIcon` |
| 34 | `chevron.left.forwardslash.chevron.right` | `code` | `Code` | `Lucide.code` | `ic_code.xml` | `kCodeIcon` |
| 35 | `chevron.right` | `chevron-right` | `ChevronRight` | `Lucide.chevronRight` | `ic_chevron_right.xml` | `kChevronRightIcon` |
| 36 | `chevron.up` | `chevron-up` | `ChevronUp` | `Lucide.chevronUp` | `ic_chevron_up.xml` | `kChevronUpIcon` |
| 37 | `circle` | `circle` | `Circle` | `Lucide.circle` | `ic_circle.xml` | `kCircleIcon` |
| 38 | `circle.fill` | `circle` | `Circle` | `Lucide.circle` | `ic_circle.xml` | `kCircleIcon` |
| 39 | `circle.lefthalf.filled` | `contrast` | `Contrast` | `Lucide.contrast` | `ic_contrast.xml` | `kContrastIcon` |
| 40 | `clock` | `clock` | `Clock` | `Lucide.clock` | `ic_clock.xml` | `kClockIcon` |
| 41 | `clock.arrow.circlepath` | `history` | `History` | `Lucide.history` | `ic_history.xml` | `kHistoryIcon` |
| 42 | `creditcard` | `credit-card` | `CreditCard` | `Lucide.creditCard` | `ic_credit_card.xml` | `kCreditCardIcon` |
| 43 | `desktopcomputer` | `monitor` | `Monitor` | `Lucide.monitor` | `ic_monitor.xml` | `kMonitorIcon` |
| 44 | `doc.badge.plus` | `file-plus` | `FilePlus` | `Lucide.filePlus` | `ic_file_plus.xml` | `kFilePlusIcon` |
| 45 | `doc.on.doc` | `copy` | `Copy` | `Lucide.copy` | `ic_copy.xml` | `kCopyIcon` |
| 46 | `doc.text` | `file-text` | `FileText` | `Lucide.fileText` | `ic_file_text.xml` | `kFileTextIcon` |
| 47 | `doc.text.magnifyingglass` | `file-search` | `FileSearch` | `Lucide.fileSearch` | `ic_file_search.xml` | `kFileSearchIcon` |
| 48 | `ellipsis` | `ellipsis` | `Ellipsis` | `Lucide.ellipsis` | `ic_ellipsis.xml` | `kEllipsisIcon` |
| 49 | `ellipsis.circle` | `circle-ellipsis` | `CircleEllipsis` | `Lucide.circleEllipsis` | `ic_circle_ellipsis.xml` | `kCircleEllipsisIcon` |
| 50 | `envelope` | `mail` | `Mail` | `Lucide.mail` | `ic_mail.xml` | `kMailIcon` |
| 51 | `exclamationmark.shield` | `shield-alert` | `ShieldAlert` | `Lucide.shieldAlert` | `ic_shield_alert.xml` | `kShieldAlertIcon` |
| 52 | `exclamationmark.triangle.fill` | `triangle-alert` | `TriangleAlert` | `Lucide.triangleAlert` | `ic_triangle_alert.xml` | `kTriangleAlertIcon` |
| 53 | `eye.slash` | `eye-off` | `EyeOff` | `Lucide.eyeOff` | `ic_eye_off.xml` | `kEyeOffIcon` |
| 54 | `eye.slash.fill` | `eye-off` | `EyeOff` | `Lucide.eyeOff` | `ic_eye_off.xml` | `kEyeOffIcon` |
| 55 | `eyeglasses` | `glasses` | `Glasses` | `Lucide.glasses` | `ic_glasses.xml` | `kGlassesIcon` |
| 56 | `folder` | `folder` | `Folder` | `Lucide.folder` | `ic_folder.xml` | `kFolderIcon` |
| 57 | `folder.badge.minus` | `folder-minus` | `FolderMinus` | `Lucide.folderMinus` | `ic_folder_minus.xml` | `kFolderMinusIcon` |
| 58 | `folder.fill` | `folder` | `Folder` | `Lucide.folder` | `ic_folder.xml` | `kFolderIcon` |
| 59 | `function` | `function-square` | `FunctionSquare` | `Lucide.functionSquare` | `ic_function_square.xml` | `kFunctionSquareIcon` |
| 60 | `gearshape` | `settings` | `Settings` | `Lucide.settings` | `ic_settings.xml` | `kSettingsIcon` |
| 61 | `gearshape.2` | `settings-2` | `Settings2` | `Lucide.settings2` | `ic_settings_2.xml` | `kSettings2Icon` |
| 62 | `globe` | `globe` | `Globe` | `Lucide.globe` | `ic_globe.xml` | `kGlobeIcon` |
| 63 | `hand.thumbsdown` | `thumbs-down` | `ThumbsDown` | `Lucide.thumbsDown` | `ic_thumbs_down.xml` | `kThumbsDownIcon` |
| 64 | `hand.thumbsup` | `thumbs-up` | `ThumbsUp` | `Lucide.thumbsUp` | `ic_thumbs_up.xml` | `kThumbsUpIcon` |
| 65 | `hourglass` | `hourglass` | `Hourglass` | `Lucide.hourglass` | `ic_hourglass.xml` | `kHourglassIcon` |
| 66 | `key` | `key-round` | `KeyRound` | `Lucide.keyRound` | `ic_key_round.xml` | `kKeyRoundIcon` |
| 67 | `line.3.horizontal` | `menu` | `Menu` | `Lucide.menu` | `ic_menu.xml` | `kMenuIcon` |
| 68 | `line.3.horizontal.decrease` | `list-filter` | `ListFilter` | `Lucide.listFilter` | `ic_list_filter.xml` | `kListFilterIcon` |
| 69 | `line.3.horizontal.decrease.circle` | `filter` | `Filter` | `Lucide.filter` | `ic_filter.xml` | `kFilterIcon` |
| 70 | `lines.measurement.horizontal` | `ruler` | `Ruler` | `Lucide.ruler` | `ic_ruler.xml` | `kRulerIcon` |
| 71 | `link` | `link` | `Link` | `Lucide.link` | `ic_link.xml` | `kLinkIcon` |
| 72 | `lock.fill` | `lock` | `Lock` | `Lucide.lock` | `ic_lock.xml` | `kLockIcon` |
| 73 | `lock.open` | `lock-open` | `LockOpen` | `Lucide.lockOpen` | `ic_lock_open.xml` | `kLockOpenIcon` |
| 74 | `magnifyingglass` | `search` | `Search` | `Lucide.search` | `ic_search.xml` | `kSearchIcon` |
| 75 | `mappin.and.ellipse` | `map-pin` | `MapPin` | `Lucide.mapPin` | `ic_map_pin.xml` | `kMapPinIcon` |
| 76 | `mic` | `mic` | `Mic` | `Lucide.mic` | `ic_mic.xml` | `kMicIcon` |
| 77 | `minus` | `minus` | `Minus` | `Lucide.minus` | `ic_minus.xml` | `kMinusIcon` |
| 78 | `minus.circle` | `circle-minus` | `CircleMinus` | `Lucide.circleMinus` | `ic_circle_minus.xml` | `kCircleMinusIcon` |
| 79 | `minus.magnifyingglass` | `zoom-out` | `ZoomOut` | `Lucide.zoomOut` | `ic_zoom_out.xml` | `kZoomOutIcon` |
| 80 | `moon.fill` | `moon` | `Moon` | `Lucide.moon` | `ic_moon.xml` | `kMoonIcon` |
| 81 | `music.note` | `music` | `Music` | `Lucide.music` | `ic_music.xml` | `kMusicIcon` |
| 82 | `note.text` | `notebook-text` | `NotebookText` | `Lucide.notebookText` | `ic_notebook_text.xml` | `kNotebookTextIcon` |
| 83 | `paintbrush` | `paintbrush` | `Paintbrush` | `Lucide.paintbrush` | `ic_paintbrush.xml` | `kPaintbrushIcon` |
| 84 | `paintbrush.pointed` | `palette` | `Palette` | `Lucide.palette` | `ic_palette.xml` | `kPaletteIcon` |
| 85 | `paperplane` | `send` | `Send` | `Lucide.send` | `ic_send.xml` | `kSendIcon` |
| 86 | `pause` | `pause` | `Pause` | `Lucide.pause` | `ic_pause.xml` | `kPauseIcon` |
| 87 | `pencil` | `pencil` | `Pencil` | `Lucide.pencil` | `ic_pencil.xml` | `kPencilIcon` |
| 88 | `pencil.and.scribble` | `pencil-line` | `PencilLine` | `Lucide.pencilLine` | `ic_pencil_line.xml` | `kPencilLineIcon` |
| 89 | `pencil.tip.crop.circle` | `pencil-line` | `PencilLine` | `Lucide.pencilLine` | `ic_pencil_line.xml` | `kPencilLineIcon` |
| 90 | `person.crop.circle` | `circle-user` | `CircleUser` | `Lucide.circleUser` | `ic_circle_user.xml` | `kCircleUserIcon` |
| 91 | `photo` | `image` | `Image` | `Lucide.image` | `ic_image.xml` | `kImageIcon` |
| 92 | `photo.on.rectangle` | `image` | `Image` | `Lucide.image` | `ic_image.xml` | `kImageIcon` |
| 93 | `photo.on.rectangle.angled` | `images` | `Images` | `Lucide.images` | `ic_images.xml` | `kImagesIcon` |
| 94 | `pin` | `pin` | `Pin` | `Lucide.pin` | `ic_pin.xml` | `kPinIcon` |
| 95 | `pin.fill` | `pin` | `Pin` | `Lucide.pin` | `ic_pin.xml` | `kPinIcon` |
| 96 | `pin.slash` | `pin-off` | `PinOff` | `Lucide.pinOff` | `ic_pin_off.xml` | `kPinOffIcon` |
| 97 | `pip.enter` | `picture-in-picture-2` | `PictureInPicture2` | `Lucide.pictureInPicture2` | `ic_picture_in_picture_2.xml` | `kPictureInPicture2Icon` |
| 98 | `play` | `play` | `Play` | `Lucide.play` | `ic_play.xml` | `kPlayIcon` |
| 99 | `plus` | `plus` | `Plus` | `Lucide.plus` | `ic_plus.xml` | `kPlusIcon` |
| 100 | `plus.magnifyingglass` | `zoom-in` | `ZoomIn` | `Lucide.zoomIn` | `ic_zoom_in.xml` | `kZoomInIcon` |
| 101 | `plus.square.on.square` | `copy-plus` | `CopyPlus` | `Lucide.copyPlus` | `ic_copy_plus.xml` | `kCopyPlusIcon` |
| 102 | `puzzlepiece.extension` | `puzzle` | `Puzzle` | `Lucide.puzzle` | `ic_puzzle.xml` | `kPuzzleIcon` |
| 103 | `questionmark.shield` | `shield-question` | `ShieldQuestion` | `Lucide.shieldQuestion` | `ic_shield_question.xml` | `kShieldQuestionIcon` |
| 104 | `rectangle.on.rectangle` | `copy` | `Copy` | `Lucide.copy` | `ic_copy.xml` | `kCopyIcon` |
| 105 | `rectangle.split.2x1` | `panel-left-close` | `PanelLeftClose` | `Lucide.panelLeftClose` | `ic_panel_left_close.xml` | `kPanelLeftCloseIcon` |
| 106 | `safari` | `compass` | `Compass` | `Lucide.compass` | `ic_compass.xml` | `kCompassIcon` |
| 107 | `shield` | `shield` | `Shield` | `Lucide.shield` | `ic_shield.xml` | `kShieldIcon` |
| 108 | `shield.lefthalf.filled` | `shield-half` | `ShieldHalf` | `Lucide.shieldHalf` | `ic_shield_half.xml` | `kShieldHalfIcon` |
| 109 | `sidebar.left` | `panel-left` | `PanelLeft` | `Lucide.panelLeft` | `ic_panel_left.xml` | `kPanelLeftIcon` |
| 110 | `sidebar.right` | `panel-right` | `PanelRight` | `Lucide.panelRight` | `ic_panel_right.xml` | `kPanelRightIcon` |
| 111 | `slider.horizontal.3` | `sliders-horizontal` | `SlidersHorizontal` | `Lucide.slidersHorizontal` | `ic_sliders_horizontal.xml` | `kSlidersHorizontalIcon` |
| 112 | `sparkles` | `sparkles` | `Sparkles` | `Lucide.sparkles` | `ic_sparkles.xml` | `kSparklesIcon` |
| 113 | `sparkles.rectangle.stack` | `sparkles` | `Sparkles` | `Lucide.sparkles` | `ic_sparkles.xml` | `kSparklesIcon` |
| 114 | `speaker.slash` | `volume-off` | `VolumeOff` | `Lucide.volumeOff` | `ic_volume_off.xml` | `kVolumeOffIcon` |
| 115 | `speaker.slash.fill` | `volume-off` | `VolumeOff` | `Lucide.volumeOff` | `ic_volume_off.xml` | `kVolumeOffIcon` |
| 116 | `speaker.wave.2` | `volume-2` | `Volume2` | `Lucide.volume2` | `ic_volume_2.xml` | `kVolume2Icon` |
| 117 | `speaker.wave.2.fill` | `volume-2` | `Volume2` | `Lucide.volume2` | `ic_volume_2.xml` | `kVolume2Icon` |
| 118 | `square.and.arrow.up` | `share` | `Share` | `Lucide.share` | `ic_share.xml` | `kShareIcon` |
| 119 | `square.and.pencil` | `square-pen` | `SquarePen` | `Lucide.squarePen` | `ic_square_pen.xml` | `kSquarePenIcon` |
| 120 | `square.grid.2x2` | `layout-grid` | `LayoutGrid` | `Lucide.layoutGrid` | `ic_layout_grid.xml` | `kLayoutGridIcon` |
| 121 | `square.on.square` | `copy` | `Copy` | `Lucide.copy` | `ic_copy.xml` | `kCopyIcon` |
| 122 | `star` | `star` | `Star` | `Lucide.star` | `ic_star.xml` | `kStarIcon` |
| 123 | `star.fill` | `star` | `Star` | `Lucide.star` | `ic_star.xml` | `kStarIcon` |
| 124 | `stop.fill` | `circle-stop` | `CircleStop` | `Lucide.circleStop` | `ic_circle_stop.xml` | `kCircleStopIcon` |
| 125 | `sun.max.fill` | `sun` | `Sun` | `Lucide.sun` | `ic_sun.xml` | `kSunIcon` |
| 126 | `text.alignleft` | `align-left` | `AlignLeft` | `Lucide.alignLeft` | `ic_align_left.xml` | `kAlignLeftIcon` |
| 127 | `trash` | `trash-2` | `Trash2` | `Lucide.trash2` | `ic_trash_2.xml` | `kTrash2Icon` |
| 128 | `tray.and.arrow.down` | `inbox` | `Inbox` | `Lucide.inbox` | `ic_inbox.xml` | `kInboxIcon` |
| 129 | `triangle` | `triangle` | `Triangle` | `Lucide.triangle` | `ic_triangle.xml` | `kTriangleIcon` |
| 130 | `video` | `video` | `Video` | `Lucide.video` | `ic_video.xml` | `kVideoIcon` |
| 131 | `wifi.slash` | `wifi-off` | `WifiOff` | `Lucide.wifiOff` | `ic_wifi_off.xml` | `kWifiOffIcon` |
| 132 | `wrench` | `wrench` | `Wrench` | `Lucide.wrench` | `ic_wrench.xml` | `kWrenchIcon` |
| 133 | `xmark` | `x` | `X` | `Lucide.x` | `ic_x.xml` | `kXIcon` |
| 134 | `xmark.circle` | `circle-x` | `CircleX` | `Lucide.circleX` | `ic_circle_x.xml` | `kCircleXIcon` |
| 135 | `xmark.circle.fill` | `circle-x` | `CircleX` | `Lucide.circleX` | `ic_circle_x.xml` | `kCircleXIcon` |
| 136 | `xmark.shield` | `shield-x` | `ShieldX` | `Lucide.shieldX` | `ic_shield_x.xml` | `kShieldXIcon` |
| 137 | `xmark.square` | `square-x` | `SquareX` | `Lucide.squareX` | `ic_square_x.xml` | `kSquareXIcon` |

**Notes**:
- Filled SF Symbols (`*.fill`) collapse to line variants in Lucide (single-weight library) — accepted divergence.
- Compositional SF Symbols (`sparkles.rectangle.stack`, `pencil.tip.crop.circle`) use closest non-composite Lucide match.
- `square.on.square` → `copy` and `rectangle.on.rectangle` → `copy` deliberately deduplicated (semantic intent identical).
- `arrow.up.forward` and `arrow.up.right` both → `arrow-up-right` (semantic equivalence).
- Total: 137 SF Symbol → ~95 unique Lucide names (some N→1 collapses).

---

## §B APPENDIX — Per-Platform Integration Recipes

### B.1 — Maho Mail (React 19 + Vite)

**Install:**
```bash
cd mail && bun add lucide-react
```

**Import pattern** (per-icon, tree-shakable):
```tsx
import { ArrowLeft, Settings, Search } from 'lucide-react';

function Toolbar() {
  return (
    <div>
      <ArrowLeft size={16} />
      <Settings className="text-foreground" />
      <Search size={20} strokeWidth={1.5} />
    </div>
  );
}
```

**Default props**: `size=24`, `color=currentColor`, `strokeWidth=2`.

### B.2 — Chromium WebUI (non-React)

**Install** (vendor SVGs into resource directory, do not bundle full `lucide-static`):
```bash
# At build time, copy needed SVGs from lucide-static to:
# maho-chromium/browser/resources/maho_<feature>/icons/
```

**SVG file naming**: `kebab-case.svg` (`arrow-right.svg`, `chevron-down.svg`).

**Default Lucide SVG attributes**:
```svg
<svg xmlns="http://www.w3.org/2000/svg"
     width="24" height="24" viewBox="0 0 24 24"
     fill="none" stroke="currentColor"
     stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <!-- paths -->
</svg>
```

**Inline injection for `currentColor` tinting**:
```typescript
const response = await fetch(`chrome://maho-feature/icons/${name}.svg`);
const svgText = await response.text();
container.innerHTML = svgText;
container.querySelector('svg')!.style.color = 'var(--maho-icon-color)';
```

When used as `<img src=...>`, `currentColor` falls back to black — inline injection required for theme tinting.

### B.3 — Swift (macOS + iOS)

**Install** (SwiftPM):
```swift
.package(url: "https://github.com/JakubMazur/lucide-icons-swift", from: "1.0.0"),
```

**Naming**: kebab-case → camelCase static property on `Lucide` struct.
- `arrow-right` → `Lucide.arrowRight`
- `alarm-clock-check` → `Lucide.alarmClockCheck`

**macOS (AppKit)**:
```swift
import AppKit
import LucideIcons

let icon: NSImage = Lucide.arrowRight
let icon2: NSImage? = NSImage.image(lucideId: "arrow-right")
```

**iOS (UIKit)**:
```swift
import UIKit
import LucideIcons

let icon: UIImage = Lucide.arrowRight
```

**SwiftUI universal**:
```swift
import SwiftUI
import LucideIcons

Image(uiImage: Lucide.arrowRight)
    .renderingMode(.template)
    .foregroundColor(.accentColor)
```

**Tinting**: PDFs in xcassets must use `.renderingMode(.template)` to enable `foregroundColor` tinting.

### B.4 — Android Compose

**No official package**. Convert SVG → AVD via Android Studio's Vector Asset Studio or `vd-tool` CLI:

```bash
# Extract from lucide-static
bun add lucide-static
ls node_modules/lucide-static/icons/

# Convert (Vector Asset Studio GUI: File → New → Vector Asset)
# Or batch via vd-tool:
$ANDROID_HOME/cmdline-tools/latest/bin/vd-tool -c \
    -in arrow-right.svg \
    -out app/src/main/res/drawable/ic_arrow_right.xml
```

**Naming**: `snake_case` with `ic_` prefix (`arrow-right.svg` → `ic_arrow_right.xml`).

**Compose usage**:
```kotlin
import androidx.compose.material3.Icon
import androidx.compose.ui.res.painterResource

Icon(
    painter = painterResource(id = R.drawable.ic_arrow_right),
    contentDescription = "Forward",
    tint = MaterialTheme.colorScheme.onSurface,
)
```

**AVD post-processing**: Replace hardcoded `android:strokeColor="#000000"` with `?attr/colorOnSurface` for theming.

### B.5 — C++ Chromium Native Views

See APPENDIX C below for full Skiafy + aggregator workflow.

**Usage in C++**:
```cpp
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"

ui::ImageModel icon = ui::ImageModel::FromVectorIcon(
    maho_lucide_icons::kArrowRightIcon,
    ui::kColorIcon,           // color from ui/color/
    /*icon_size=*/16);

image_view->SetImage(icon);
```

### B.6 — License Attribution

`LICENSES/lucide.txt`:
```
Lucide Icons
https://lucide.dev
License: ISC

Copyright (c) for portions of Lucide are held by Cole Bemis 2013-2022
as part of Feather (MIT). All other copyright (c) for Lucide are held
by Lucide Contributors 2022.

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

---

## §C APPENDIX — Skiafy + aggregate_vector_icons Workflow (C++)

### C.1 — Toolchain

**Skiafy (SVG→.icon)**: Web tool at `http://evanstade.github.io/skiafy/`. Manual paste SVG → outputs `.icon` text. No CLI alternative.

**aggregate_vector_icons.py**: Build-time aggregator at `chromium/src/components/vector_icons/aggregate_vector_icons.py`. Driven by GN via `vector_icons.gni`.

### C.2 — Step-by-step

**Step 1 — Pre-process SVG**:
Run Lucide SVG through [SVGOMG](https://jakearchibald.github.io/svgomg/) to flatten transforms and simplify paths. Remove any `<path d="M0 0h24v24H0z"/>` bounding-box artifacts.

**Step 2 — Convert via Skiafy**:
Open `http://evanstade.github.io/skiafy/`, paste the cleaned SVG, set canvas size to `24` (matching Lucide's `viewBox="0 0 24 24"`). Copy the `.icon` output.

**Step 3 — Save `.icon` file**:
```
maho-chromium/browser/ui/views/maho_lucide_icons/arrow_right.icon
```

Example content (Lucide `arrow-right`):
```
CANVAS_DIMENSIONS, 24,
STROKE, 2,
MOVE_TO, 5, 12,
H_LINE_TO, 19,
NEW_PATH,
MOVE_TO, 12, 5,
R_LINE_TO, 7, 7,
R_LINE_TO, -7, 7
```

Key elements:
- `CANVAS_DIMENSIONS, 24,` — canvas size matches Lucide's 24×24 viewBox
- `STROKE, 2,` — switches paint mode to stroke at 2dip width (matches Lucide default)
- Default cap is round — matches Lucide `stroke-linecap="round"`
- `NEW_PATH,` — separates independent SVG `<path>` elements

**Step 4 — Add C++ templates to icon directory**:
Copy from `chromium/src/components/vector_icons/`:
- `vector_icons.cc.template`
- `vector_icons.h.template`

**Step 5 — Create BUILD.gn**:
```gn
# maho-chromium/browser/ui/views/maho_lucide_icons/BUILD.gn
import("//components/vector_icons/vector_icons.gni")

aggregate_vector_icons("maho_lucide_icons") {
  icon_directory = "."
  sources = [
    "arrow_right.icon",
    "chevron_left.icon",
    # ... all icons
  ]
}

source_set("vector_icons") {
  sources = [
    "$target_gen_dir/vector_icons.cc",
    "$target_gen_dir/vector_icons.h",
  ]
  deps = [
    ":maho_lucide_icons",
    "//ui/gfx",
  ]
}
```

**Step 6 — Build and use**:
```bash
autoninja -C out/Default chrome
```

The aggregator generates:
- `$target_gen_dir/vector_icons.cc` containing path arrays
- `$target_gen_dir/vector_icons.h` declaring `kArrowRightIcon` constants

Use in C++:
```cpp
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"

ui::ImageModel::FromVectorIcon(
    maho_lucide_icons::kArrowRightIcon, color_id, /*size=*/16);
```

### C.3 — `.icon` DSL Reference

Full command set from `chromium/src/ui/gfx/vector_icon_types.h`:

| Command | Args | Description |
|---|---|---|
| `CANVAS_DIMENSIONS` | size | Canvas in dip; defaults to 48 if omitted |
| `MOVE_TO` | x, y | Absolute moveto |
| `R_MOVE_TO` | dx, dy | Relative moveto |
| `LINE_TO` | x, y | Absolute lineto |
| `R_LINE_TO` | dx, dy | Relative lineto |
| `H_LINE_TO` | x | Absolute horizontal line |
| `R_H_LINE_TO` | dx | Relative horizontal line |
| `V_LINE_TO` | y | Absolute vertical line |
| `R_V_LINE_TO` | dy | Relative vertical line |
| `CUBIC_TO` | x1, y1, x2, y2, x, y | Absolute cubic bezier |
| `R_CUBIC_TO` | dx1... | Relative cubic |
| `ARC_TO` | rx, ry, angle, large-arc, sweep, x, y | Absolute elliptical arc |
| `R_ARC_TO` | ... | Relative arc |
| `CIRCLE` | cx, cy, r | Filled circle primitive |
| `OVAL` | cx, cy, rx, ry | Ellipse primitive |
| `ROUND_RECT` | x, y, w, h, r | Rounded rectangle primitive |
| `STROKE` | width | Switch from fill to stroke; default cap is round |
| `CAP_SQUARE` | — | Square cap (override default round) |
| `NEW_PATH` | — | Separator between independent SVG `<path>` groups |
| `CLOSE` | — | Close subpath (Z command) |
| `PATH_COLOR_ARGB` | A, R, G, B | Per-path color override |
| `PATH_COLOR_ALPHA` | alpha | Per-path alpha (0-255) |
| `FLIPS_IN_RTL` | — | Mirror in RTL locales |

### C.4 — Lucide SVG Coverage

| Lucide feature | `.icon` mapping | Status |
|---|---|---|
| `<path d="M/L/H/V/C/S/Q/T/A/Z">` | Direct command mapping | ✅ Supported |
| `<circle cx cy r>` | `CIRCLE, cx, cy, r` | ✅ Native |
| `<rect x y w h rx>` | `ROUND_RECT, x, y, w, h, rx` | ✅ Native |
| `<line x1 y1 x2 y2>` | `MOVE_TO, x1, y1, LINE_TO, x2, y2` | ✅ Skiafy converts |
| `<polyline points>` | Sequence of `LINE_TO` | ✅ Skiafy converts |
| `stroke="currentColor"` | Render-time tint via `FromVectorIcon(icon, color, size)` | ✅ Automatic |
| `stroke-width="2"` | `STROKE, 2,` | ✅ Manual in Skiafy |
| `stroke-linecap="round"` | Default cap is round | ✅ Default |
| `stroke-linejoin="round"` | Skia default is miter; round joins approximate | ⚠️ Visual diff negligible at icon sizes |
| `fill="none"` | Use `STROKE,` to enter stroke mode | ✅ Manual |
| `viewBox="0 0 24 24"` | `CANVAS_DIMENSIONS, 24,` | ✅ Direct |
| Transforms (`translate`, `rotate`) | Not supported | ❌ Flatten in SVGOMG first |
| `<use>`, `<defs>`, `<symbol>` | Not supported | ❌ Expand inline first |

### C.5 — Inline Pattern Alternative (Maho's Existing Style)

For one-off icons tightly coupled to a single View file, Maho already uses inline `gfx::PathElement[]` arrays (bypassing the aggregator). Reference: `maho_sidebar_library_rail_view.cc:54-85` (`kArchiveboxPath`).

```cpp
static constexpr gfx::PathElement kMyIconPath[] = {
    gfx::CANVAS_DIMENSIONS, 24, gfx::STROKE, 2.0f,
    gfx::MOVE_TO, 5, 12, gfx::H_LINE_TO, 19,
    gfx::MOVE_TO, 12, 5, gfx::R_LINE_TO, 7, 7,
    gfx::R_LINE_TO, -7, 7,
};
constexpr char kMyIconId[] = "kMyFeatureMyIcon";
static constexpr gfx::VectorIconRep kMyIconRepList[] = {{kMyIconPath}};
constexpr gfx::VectorIcon kMyIcon(kMyIconRepList, 1, kMyIconId);
```

This is identical to the aggregator output, just hand-written. Use for narrow scopes; use the aggregator for shared icon libraries.

---

**End of plan.**
