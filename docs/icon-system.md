# Maho Icon System

**Status**: Canonical
**Last updated**: 2026-05-31
**License**: [ISC](../LICENSES/lucide.txt)

Maho uses **[Lucide](https://lucide.dev)** as the single source-of-truth icon library across all five rendering surfaces:

| Surface | Tech | Integration |
|---|---|---|
| Maho Mail | React 19 (Tauri) | `lucide-react` (npm) |
| Chromium WebUI (AI panel, Boost editor, Settings, etc.) | TypeScript / React | Vendored Lucide SVGs in `browser/resources/<feature>/icons/` |
| Chromium Native Views (C++) | Views + Skia | `aggregate_vector_icons` GN target → `gfx::VectorIcon` |
| macOS shell | AppKit / SwiftUI | `lucide-icons-swift` (SwiftPM) |
| iOS shell | UIKit / SwiftUI | `lucide-icons-swift` (SwiftPM) |
| Android shell | Jetpack Compose | Vector AVDs under `res/drawable/ic_*.xml` |

The unified mapping table — every legacy SF Symbol name plus its per-platform Lucide form — lives at [`maho/assets/icons/sf-to-lucide-mapping.json`](../maho/assets/icons/sf-to-lucide-mapping.json) (137 entries).

---

## Naming Conventions

| Form | Example | Used by |
|---|---|---|
| `kebab-case` (canonical) | `arrow-up-right` | All platforms reference this as the source name |
| `PascalCase` | `ArrowUpRight` | `lucide-react` JSX components |
| `camelCase` | `arrowUpRight` | `lucide-icons-swift` static properties on `Lucide` |
| `snake_case` | `arrow_up_right` | C++ `.icon` filenames, `vector_icon` symbol bases |
| `ic_snake_case` | `ic_arrow_up_right` | Android AVD `R.drawable.*` resource IDs |
| `kPascalCaseIcon` | `kArrowUpRightIcon` | C++ `gfx::VectorIcon` constants emitted by aggregator |

---

## Per-Platform Usage

### Maho Mail (React 19)

```tsx
import { ArrowLeft, Settings, Search } from 'lucide-react';

<ArrowLeft size={16} />
<Settings className="text-foreground" />
```

### Chromium WebUI (TypeScript / React)

Per-feature vendored SVGs served via `chrome://maho-<feature>/icons/<kebab>.svg`. The AI panel uses a typed `IconSymbol` union and `getIconUrl()`:

```typescript
import { getIconUrl, type IconSymbol } from './views/icons';

element.style.setProperty('--icon-url', `url("${getIconUrl('arrowUpRight')}")`);
```

For React components within the AI/Boost bundles, `lucide-react` is also available:

```tsx
import { Sparkles } from 'lucide-react';
```

### Chromium Native Views (C++)

```cpp
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"

ui::ImageModel icon = ui::ImageModel::FromVectorIcon(
    maho_lucide_icons::kArrowUpRightIcon,
    ui::kColorIcon,
    /*icon_size=*/16);
```

For the no-icon case (nullable optional fallback), use `gfx::VectorIcon::EmptyIcon()` — never `gfx::kNoneIcon` (does not exist).

### macOS / iOS (Swift)

```swift
import LucideIcons

let img: UIImage = Lucide.arrowUpRight
let img2: NSImage = Lucide.arrowUpRight
```

`MahoIcon` enum wraps platform-specific cases:

```swift
let image = MahoIcon.navBack.image          // NSImage? on macOS, UIImage? on iOS
let view  = MahoIcon.navBack.imageVector    // SwiftUI Image (iOS only)
```

For SwiftUI dynamic SF-symbol-name strings (legacy data sources, conditional expressions), use the compatibility lookup:

```swift
Image(lucide: MahoIcon.imageForSFSymbol("arrow.clockwise"))
```

The `Image(lucide:)` initializer applies `.alwaysTemplate` rendering mode automatically so `.tint(...)` / `.foregroundColor(...)` work as expected.

For `Label` in SwiftUI (iOS), use the trailing-closure form rather than `systemImage:`:

```swift
Label { Text("Delete") } 
  icon: { Image(lucide: MahoIcon.imageForSFSymbol("trash")) }

Label { Text(tab.isPinned ? "Unpin" : "Pin") } 
  icon: { Image(lucide: MahoIcon.imageForSFSymbol(tab.isPinned ? "pin.slash" : "pin")) }
```

### Android (Compose)

```kotlin
import dev.maho.browser.ui.icons.MahoIcon
import androidx.compose.ui.res.painterResource

Icon(
    painter = painterResource(id = MahoIcon.NavBack.drawableRes),
    contentDescription = "Back",
    tint = MaterialTheme.colorScheme.onSurface,
)
```

---

## Adding a New Icon

1. Browse [lucide.dev/icons](https://lucide.dev/icons) and pick the closest semantic match.
2. Append a row to `maho/assets/icons/sf-to-lucide-mapping.json` (use `null` for `sf_symbol` when there is no legacy SF analogue).
3. Per-platform vendoring:
   - **Mail / Chromium WebUI (React)**: `import { NewIcon } from 'lucide-react'` — already tree-shakable, no further action.
   - **Chromium WebUI (vanilla TS)**: download the SVG from `https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/<kebab>.svg` into `browser/resources/<feature>/icons/<kebab>.svg`. Verify `stroke="currentColor"`. Add the path to `sources` in that feature's `BUILD.gn`. Reference via `chrome://<feature>/icons/<kebab>.svg`.
   - **Chromium Native Views (C++)**: convert the SVG to Skiafy `.icon` syntax (see [Skiafy workflow](#skiafy-workflow-c)) and drop into `maho-chromium/browser/ui/views/maho_lucide_icons/`. Add to the `sources` list of `aggregate_vector_icons("maho_lucide_icons")` in that directory's `BUILD.gn`.
   - **Swift (macOS / iOS)**: `lucide-icons-swift` already ships the full set; just reference `Lucide.newIcon`. Wire the new case to `MahoIcon`'s internal `lucideImage` / `lucideUIImage` switch.
   - **Android**: convert the SVG to AVD via Vector Asset Studio or `vd-tool`. Place under `app/src/main/res/drawable/ic_<snake_case>.xml`. Use `@android:color/black` (or `?attr/colorOnSurface` if the host theme declares it) for `android:strokeColor` and `android:tint` — the Compose `Icon(tint=…)` parameter overrides at render time.
4. Wire to the platform enum (`MahoIcon.swift` on macOS/iOS, `MahoIcon.kt` on Android, `IconSymbol` type on AI WebUI) so callers reference an enum case rather than a raw string.

---

## SVG Defaults

All Lucide SVGs MUST conform to:

```svg
<svg xmlns="http://www.w3.org/2000/svg"
     width="24" height="24" viewBox="0 0 24 24"
     fill="none" stroke="currentColor"
     stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <!-- paths -->
</svg>
```

`currentColor` is mandatory — it lets the parent CSS / `tintColor` / `gfx::VectorIcon` color pipeline tint the icon at render time.

---

## Skiafy Workflow (C++)

Chromium Native Views consumes vector icons through the `aggregate_vector_icons.py` build step. This requires `.icon` files in Skia's vector icon DSL, not raw SVG.

### Conversion steps

1. **Pre-process** the Lucide SVG through [SVGOMG](https://jakearchibald.github.io/svgomg/) to flatten transforms and drop bounding-box artifacts.
2. **Convert** at [Skiafy](http://evanstade.github.io/skiafy/): paste the cleaned SVG, set canvas size to `24` (matches Lucide's `viewBox="0 0 24 24"`), and copy the output. Alternatively, hand-translate path data to the DSL using the reference table below — Lucide SVGs are highly regular and translate mechanically.
3. **Save** as `<snake_case>.icon` under `maho-chromium/browser/ui/views/maho_lucide_icons/`.
4. **Register** in `BUILD.gn` `sources` list.

### Example output

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

Key: `STROKE, 2,` matches Lucide's `stroke-width="2"`. The default Skia line cap is round (matches `stroke-linecap="round"`). `NEW_PATH,` separates independent SVG `<path>` elements.

### `.icon` DSL reference

| Command | Args | Description |
|---|---|---|
| `CANVAS_DIMENSIONS` | size | Canvas dimension (24 for Lucide) |
| `MOVE_TO` / `R_MOVE_TO` | x, y | Absolute / relative moveto |
| `LINE_TO` / `R_LINE_TO` | x, y | Absolute / relative lineto |
| `H_LINE_TO` / `R_H_LINE_TO` | x | Horizontal line |
| `V_LINE_TO` / `R_V_LINE_TO` | y | Vertical line |
| `CUBIC_TO` / `R_CUBIC_TO` | x1, y1, x2, y2, x, y | Cubic bezier |
| `ARC_TO` / `R_ARC_TO` | rx, ry, angle, large-arc, sweep, x, y | Elliptical arc |
| `CIRCLE` | cx, cy, r | Filled circle primitive |
| `ROUND_RECT` | x, y, w, h, r | Rounded rectangle |
| `STROKE` | width | Stroke mode at given width (default cap is round) |
| `CAP_SQUARE` | — | Square cap (override default round) |
| `NEW_PATH` | — | Boundary between independent paths |
| `CLOSE` | — | Close subpath |
| `FLIPS_IN_RTL` | — | Mirror in RTL locales |

For one-off icons coupled to a single View, an inline `gfx::PathElement[]` array is acceptable (see `maho_sidebar_library_rail_view.cc` for the existing pattern).

### GN target shape

`maho-chromium/browser/ui/views/maho_lucide_icons/BUILD.gn` declares two targets:

```gn
import("//components/vector_icons/vector_icons.gni")

aggregate_vector_icons("maho_lucide_icons") {
  icon_directory = "."
  sources = [ "archive.icon", "x.icon", /* … */ ]
}

source_set("maho_lucide_icon_library") {
  sources = [
    "$target_gen_dir/vector_icons.cc",
    "$target_gen_dir/vector_icons.h",
  ]
  deps = [ ":maho_lucide_icons", "//ui/gfx" ]
  public_deps = [ "//ui/gfx" ]
}
```

The two `.template` files (`vector_icons.cc.template`, `vector_icons.h.template`) are local copies of upstream's templates with the namespace replaced (`maho_lucide_icons`). Consumers depend on `:maho_lucide_icon_library` from their parent `BUILD.gn`.

### Build gotchas (learned during initial migration)

| Pitfall | Symptom | Fix |
|---|---|---|
| `CAP_ROUND` keyword in `.icon` files | `error: use of undeclared identifier 'CAP_ROUND'` | The DSL has `CAP_SQUARE` only — round is the **default**. Omit `CAP_ROUND` lines entirely. |
| Custom `#define VECTOR_ICON_ID_PREFIX "..."` in `vector_icons.cc.template` | `error: 'VECTOR_ICON_ID_PREFIX' macro redefined` | `cc_macros.h` already provides an `#ifndef`-guarded empty default. Either define BEFORE including `cc_macros.h`, or omit. |
| `deps = [ "//components/vector_icons:cc_macros" ]` | GN regen `Unresolved dependencies` | `cc_macros.h` is a header-only include, **not** a separate GN target. Drop the dep; the include path is reachable through `//ui/gfx`. |
| `gfx::kNoneIcon` for null fallback | `error: no member named 'kNoneIcon' in namespace 'gfx'` | Use `gfx::VectorIcon::EmptyIcon()` (static accessor returning `const VectorIcon&`). |
| `raw_ref<const gfx::VectorIcon>` field passed to `FromVectorIcon` | `error: no viable conversion from 'const raw_ref<…>' to 'const gfx::VectorIcon'` | Dereference: `FromVectorIcon(*icon_, …)`. |
| Stale dep on a deleted helper in `chromium/src/chrome/browser/ui/BUILD.gn` | GN regen `needs //maho/browser:maho_mac_sf_symbol_helper` | When deleting a `source_set` from the Maho overlay, also remove its consumer entries from any upstream BUILD.gn that maho-chromium injects deps into. |

---

## Anti-Patterns

- ❌ Direct SF Symbol references (`imageWithSystemSymbolName:`, `Image(systemName:)`, `Label(_, systemImage:)`) — these are the legacy form being migrated away from.
- ❌ Hand-drawn SVGs that diverge from Lucide's stroke characteristics. Always start from the Lucide source.
- ❌ Bundling `lucide-static` wholesale in WebUI. Per-feature vendoring keeps GRIT bundles small.
- ❌ Hardcoded `stroke="#000000"` or `android:strokeColor="#000000"` (other than the AVD fallback default that is overridden at runtime by Compose `tint`). Use `currentColor` / theme attributes / Compose `tint` so light/dark themes work.
- ❌ Adding new icons without updating `sf-to-lucide-mapping.json` — the mapping is the canonical registry.
- ❌ Inventing DSL commands (`CAP_ROUND`) or GN targets (`//components/vector_icons:cc_macros`) — only what's documented in `ui/gfx/vector_icon_types.h` and the upstream `BUILD.gn` actually exists.

---

## Permanent Exceptions

These remain on SF Symbol APIs by Apple framework constraint — Lucide cannot substitute:

| Site | API | Reason |
|---|---|---|
| `maho/ios-shell/Integration/ShortcutsManager.swift` | `AppShortcut(systemImageName: …)` | Apple AppIntents framework requires SF Symbol names. iOS system Shortcuts.app, Spotlight, and Siri suggestions render the icon outside our process — no Lucide injection point exists. |

---

## See Also

- `.sisyphus/plans/sf-symbol-to-lucide-migration-2026-05-30.md` — the migration plan that established this system
- `LICENSES/lucide.txt` — ISC attribution
- [Lucide upstream](https://github.com/lucide-icons/lucide)
- [`lucide-icons-swift`](https://github.com/JakubMazur/lucide-icons-swift)
