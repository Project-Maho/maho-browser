# Icon System Cross-Platform Registry Migration Plan

**Created:** 2026-05-25
**Status:** Draft, code-grounded against current main (commit `e2e5fbf`)
**Intent:** Establish an approved cross-platform icon registry / semantic mapping layer for non-WebUI Maho surfaces, then progressively retire the platform-specific fallback icon systems (SF Symbols string literals on Apple shells, Compose `Icons.*` references on Android, ad-hoc inline `gfx::VectorIcon` declarations in Views) in favour of registry-resolved lookups.

**Primary scope (non-WebUI surfaces only):**
- `maho-chromium/browser/ui/views/**` — Chromium Views C++ surfaces
- `maho/macos-shell/**` — AppKit reference shell
- `maho/ios-shell/**` — UIKit/SwiftUI shell
- `maho/android-shell/**` — Android Compose shell

**Out of scope (explicitly excluded):**
- `maho-chromium/browser/resources/**` — WebUI (CSS/SVG icons in browser WebUIs are outside this plan)
- App icon / branding assets (`.icns`, `.png` launcher icons, `Assets.xcassets` AppIcon sets, splash screens)

---

## 1. Objective

Stand up a single approved cross-platform icon registry — a semantic mapping layer — that all non-WebUI Maho surfaces eventually consume, and progressively retire the platform-specific icon systems currently used as fallbacks. The end state is a codified registry (a shared source of truth for icon names, sizes, and per-platform rendering hints) such that:

1. Every UI icon used on a non-WebUI Maho surface has a single canonical name in the registry.
2. Platform implementations (Views/C++, AppKit, UIKit, Compose) resolve their icon through the registry rather than independently declaring or importing platform-native identifiers.
3. Adding a new icon, changing an icon's size contract, or auditing icon usage does not require searching four separate codebases for ad-hoc declarations.
4. Over time, the platform-native fallback icon systems (SF Symbols string literals, Compose `Icons.*` references, ad-hoc inline `gfx::VectorIcon` declarations) are removed in favour of registry-resolved lookups.

This plan does not propose building a cross-compiled icon rendering engine. Each platform continues to render icons in whatever native mechanism it uses; the registry is a coordination + naming layer, not a rendering layer. Removing the per-platform fallback systems is an outcome of progressive migration, not a Phase-1 deliverable.

---

## 2. Current State Audit

### 2.1 Chromium Views C++ (`maho-chromium/browser/ui/views/`)

Icons in Views C++ surfaces (sidebar, toolbar, command overlay, library rail, utility panel) are delivered today through two patterns. **There is no Maho-owned `vector_icons` directory or `vector_icons::kMaho*Icon` namespace today** — any path like `maho-chromium/browser/ui/vector_icons/` referenced later in this document is a *proposed* future location, not an existing one.

**Upstream `vector_icons::` consumers (dominant pattern):**
- The vast majority of Views icon usage is direct consumption of upstream Chromium icons declared in `components/vector_icons/*.icon` and exposed under the `vector_icons::` namespace (e.g. `vector_icons::kHomeIcon`, `vector_icons::kScienceIcon`, `vector_icons::kVideoLibraryIcon`, `vector_icons::kStickyNote2Icon`, `vector_icons::kFileDownloadOffChromeRefreshIcon`, `vector_icons::kBackArrowIcon`).
- Rendering: `ui::ImageModel::FromVectorIcon(vector_icons::kSomeIcon, color, size_dp)` or `gfx::CreateVectorIcon(...)` → `gfx::ImageSkia`.
- These are upstream-managed: their existence, naming, and shape are owned by Chromium. Maho is a consumer.

**Maho-defined custom `gfx::VectorIcon` (one known instance):**
- A single Maho-defined custom `gfx::VectorIcon` currently exists, declared inline in `maho-chromium/browser/ui/views/sidebar/maho_sidebar_library_rail_view.cc` as `kArchiveboxIcon` (file-local `static constexpr gfx::VectorIconRep kArchiveboxRepList[]` plus a `constexpr gfx::VectorIcon kArchiveboxIcon(...)` near the top of the TU). It is exposed via a file-local accessor `GetMahoSidebarArchiveboxIcon()`.
- This is the canonical example of the "ad-hoc inline custom icon" pattern the registry should eventually replace. Future custom Maho icons should land in a dedicated proposed location (see Phase 5) rather than as more inline declarations.

**`ui::ImageModel` / `gfx::Image` data-driven icons:**
- A small number of places (per-tab favicons, extension icons, web-content thumbnails) use `ui::ImageModel::FromImage()` or `gfx::Image` directly with content-derived bitmap data.
- These are intrinsically data-driven and are explicitly outside this plan's scope — the registry covers chrome glyphs, not per-site favicons.

**Existing inline cross-platform pairing (informative):**
- `maho_sidebar_library_rail_view.cc` already statically pairs Views vector icons with SF Symbols names in a struct (e.g. `{ &vector_icons::kHomeIcon, "square.on.square", "Spaces", true }`). This in-code pairing is a useful real-world signal that the team already thinks of icons as semantic concepts with per-platform identifiers — exactly the relation the proposed `registry.toml` would formalise.

**Current pain:** No registry file enumerates which icons Maho's Views surfaces actually use, what their intended size is, or how they correspond to icons used on the Apple / Android shells. The single existing custom Maho icon (`kArchiveboxIcon`) is buried inside an unrelated TU.

### 2.2 macOS Shell (`maho/macos-shell/`)

The AppKit shell (`macos-shell/`) uses **SF Symbols** (`NSImage(systemName:)`) for all UI icons. Icon names are hardcoded as string literals scattered across view controller and view files. There is no centralized list; the same conceptual icon (e.g. "back navigation") may use a different SF Symbols name on macOS than the corresponding icon in the Views shell.

This shell is noted in `AGENTS.md` as "reference; active work in Chromium fork." However its icon pattern influences what the approved registry must support.

### 2.3 iOS Shell (`maho/ios-shell/`)

iOS shell (`ios-shell/`) also uses **SF Symbols** via `UIImage(systemName:)` (UIKit) or `Image(systemName:)` (SwiftUI). Same issue as macOS: string-literal SF Symbols names are inline and not centrally catalogued.

### 2.4 Android Shell (`maho/android-shell/`)

The Android shell uses **Jetpack Compose Material icons** (`Icons.Default.*`, `Icons.Outlined.*`, `Icons.Rounded.*`) from the `androidx.compose.material.icons` library, plus occasional PNG drawables under `res/drawable/`. Compose Material icon names (`Icons.Outlined.ArrowBack`) are not semantically aligned with the SF Symbols names used on iOS/macOS or the upstream `vector_icons::k*Icon` names consumed in Views.

### 2.5 Summary of the Gap

| Platform | Current icon system | Registry exists? | Semantic alignment with other platforms |
|---|---|---|---|
| Views (Chromium C++) | Mostly upstream `vector_icons::k*Icon` consumers; one Maho-defined inline custom `gfx::VectorIcon` (`kArchiveboxIcon` in `maho_sidebar_library_rail_view.cc`) | No | Partial — `maho_sidebar_library_rail_view.cc` already inlines a Views↔SF Symbols pairing per row |
| macOS shell | SF Symbols string literals (`NSImage(systemName:)`) | No | No |
| iOS shell | SF Symbols string literals (`UIImage(systemName:)` / `Image(systemName:)`) | No | No |
| Android shell | Compose Material Icons (`Icons.Default.*`, `Icons.Outlined.*`, `Icons.AutoMirrored.*`) + occasional drawables | No | No |

No surface owns a registry. No two surfaces share a semantic icon name as a first-class artifact. The same UI concept is expressed differently on each platform with no enforced correspondence — except for the partial inline pairing inside one Views file.

---

## 3. Target Architecture

### 3.1 The Registry (proposed format — DRAFT)

A single file (or small set of files) defines the canonical icon set for Maho's non-WebUI surfaces. The format below is a **draft sketch** to anchor discussion in Phase 2; the exact schema is expected to evolve once the Phase 1 audit lands. The simplest viable form is a machine-readable manifest — a JSON or TOML file checked into the repo under a path like `maho/icons/registry.toml` — with one entry per logical icon:

```toml
# DRAFT — exact schema TBD in Phase 2
[[icon]]
name = "nav_back"
description = "Navigate back in history"
sizes = ["16", "20", "24"]

  [icon.platforms]
  views   = "vector_icons::kBackArrowIcon"               # upstream symbol; or future Maho-owned symbol
  apple   = "chevron.backward"                           # SF Symbols name (shared macOS + iOS)
  android = "Icons.AutoMirrored.Outlined.ArrowBack"      # Compose Material icon
```

Key properties:
- `name` is the canonical cross-platform identifier. Code that wants to look up an icon references this name.
- `platforms` block records the native identifier on each platform, maintaining a 1:1 mapping so the registry acts as a translation table rather than an additional rendering layer.
- The `views` value can be either an upstream `vector_icons::k*Icon` symbol (the dominant case today) or a Maho-owned symbol once the proposed Maho `vector_icons` location exists (see Phase 5).
- `sizes` records the intended display sizes (dp) so reviewers can catch mismatches.

This format is illustrative only. Phase 2 may decide on TOML vs. JSON, may add fields (e.g. accessibility label, deprecation flag), or may split single-platform icons into a separate file. The above is the *minimum* the registry should express.

### 3.2 How Platforms Consume the Registry

No code generation is required in the initial phase. The registry is authoritative documentation that:

1. Tells a Views developer which `vector_icons::` symbol corresponds to a given conceptual icon.
2. Tells an iOS/macOS developer which SF Symbols name to use.
3. Tells an Android developer which Compose icon to use.
4. Enables automated audit: a script can grep all four codebases for known platform identifiers and check that every usage maps to a registered entry.

In a later phase (Phase 4), platform-specific wrapper layers (a Swift `MahoIcon` enum, a Kotlin `MahoIcon` sealed class, a C++ `MahoIconId` enum) can be generated from the registry to enforce compile-time correctness. The Phase 1–3 work is compatible with this future upgrade.

### 3.3 Scope Boundary: UI Icons vs. Branding Assets

This plan covers only **UI chrome icons** — icons rendered inline as part of the browser shell UI (toolbar buttons, sidebar glyphs, navigation affordances, tab indicators, status indicators). Explicitly excluded:

- **App icons**: `macos-shell/Resources/AppIcon.icns`, `ios-shell/Assets.xcassets/AppIcon.appiconset/`, `android-shell/app/src/main/res/mipmap-*/ic_launcher.png`. These are branding assets, not UI icons.
- **Favicons and site thumbnails**: fetched dynamically from the web; not part of the shell chrome.
- **Extension icons**: loaded from the extension package; not shell chrome.
- **WebUI icons**: CSS/SVG assets under `maho-chromium/browser/resources/`. Separate icon pipeline managed by the WebUI build.
- **Splash screens and onboarding illustrations**: not UI icons.

---

## 4. Migration Phases

### Phase 1 — Audit and Inventory

**Goal:** Produce a complete, accurate list of every UI icon currently in use across all four surfaces.

**Work items:**

1. **Views audit:** grep `maho-chromium/browser/ui/views/` for all uses of `gfx::CreateVectorIcon`, `gfx::CreateVectorIconWithBadge`, `views::CreateVectorImageButton`, `ui::ImageModel::FromVectorIcon`, and `gfx::VectorIcon` (to catch inline custom declarations like `kArchiveboxIcon` in `maho_sidebar_library_rail_view.cc`). Record every distinct `vector_icons::k*Icon` symbol consumed and every Maho-defined custom icon. Note the calling TU, the size argument, and the color token. Also record any inline cross-platform pairings already present (the existing `{ &vector_icons::kHomeIcon, "square.on.square", ... }`-style structs in the library rail view) — these are pre-existing semantic mappings the registry should preserve.

2. **macOS shell audit:** grep `maho/macos-shell/` for `NSImage(systemName:` and `NSImage(named:` literals. Record every unique string.

3. **iOS shell audit:** grep `maho/ios-shell/` for `UIImage(systemName:`, `Image(systemName:`, and `UIImage(named:` literals. Record every unique string.

4. **Android shell audit:** grep `maho/android-shell/` for `Icons.Default.`, `Icons.Outlined.`, `Icons.Rounded.`, `Icons.AutoMirrored.`, `painterResource(R.drawable.`, `ImageVector.vectorResource(`. Record every unique reference.

5. **Cross-reference:** Cluster the raw names from all four platforms into logical groups (e.g. macOS `"chevron.backward"` + iOS `"chevron.backward"` + Android `Icons.AutoMirrored.Outlined.ArrowBack` + Views `vector_icons::kBackArrowIcon` → `nav_back`). Record unmatched entries as icons that exist on only one platform. Treat the existing inline pairings inside `maho_sidebar_library_rail_view.cc` as authoritative seed clusters (e.g. `kHomeIcon` + `square.on.square` → likely `spaces`).

6. **Output:** `maho/icons/audit-2026.md` — a markdown table with one row per logical group, listing the raw platform name on each surface (or `—` if absent). This becomes the input to Phase 2.

**Verification:**
- Review the audit against the visual UI on each platform. No icon should appear in the UI but not in the audit table.
- Confirm branding assets (app icon, splash) are explicitly not in the table.

**Risk:** Low. Read-only exploration. The audit file itself has no effect on build or runtime.

---

### Phase 2 — Draft the Registry

**Goal:** Convert the Phase 1 audit into the machine-readable `registry.toml`.

**Work items:**

1. Create `maho/icons/registry.toml` with one `[[icon]]` block per logical group discovered in Phase 1. Populate `name`, `description`, `sizes`, and the `[icon.platforms]` block for each platform that has an entry.

2. For icons that exist on one platform only (e.g. a glyph used only in the Android shell), create a registry entry with the `platforms` block partially filled. Leave missing platforms as `""` (empty string) to indicate "not yet implemented on this platform." This makes gaps visible without blocking the registry itself.

3. Create `maho/icons/README.md` documenting the registry format, the intent, and the workflow for adding a new icon (edit `registry.toml` first, then implement on each platform, then update the `platforms` mapping).

4. Do not change any platform code in this phase.

**Verification:**
- Parse `registry.toml` with a quick Python or `bun` script to confirm valid TOML and no duplicate `name` entries.
- Human review: each entry in the audit table from Phase 1 should correspond to a registry entry.

**Risk:** Very low. No code change.

---

### Phase 3 — Migrate macOS and iOS Shells to Named Constants

**Goal:** Eliminate inline SF Symbols string literals from the Apple shells. Replace with a Swift `MahoIcon` enum whose cases map 1:1 to `registry.toml` `name` fields, with the SF Symbols string expressed once per case.

**Work items:**

1. Create `maho/macos-shell/Sources/MahoIcons/MahoIcon.swift` (and mirror it in `maho/ios-shell/Sources/MahoIcons/MahoIcon.swift`, or share via a common Swift package if the shells already share a package target):
   ```swift
   public enum MahoIcon {
       case navBack
       case navForward
       case newTab
       // ... one case per registry entry with an `apple` mapping
   }

   public extension MahoIcon {
       var symbolName: String {
           switch self {
           case .navBack:    return "chevron.left"
           case .navForward: return "chevron.right"
           case .newTab:     return "plus"
           // ...
           }
       }

       var image: NSImage { NSImage(systemName: symbolName)! } // macOS
       // UIImage equivalent for iOS
   }
   ```

2. Grep each shell codebase for every `NSImage(systemName:` / `UIImage(systemName:` / `Image(systemName:` call from the Phase 1 audit. Replace each with `MahoIcon.<caseName>.image` (or equivalent SwiftUI `.init` helper).

3. Inline `UIImage(named:)` / `NSImage(named:)` calls for UI icons (not branding assets) should be migrated to registry entries; add corresponding vector drawables or PNG fallbacks as appropriate if SF Symbols is unavailable on older OS targets.

4. Do **not** touch `Assets.xcassets/AppIcon*` or any branding asset folder.

**Verification:**
- `swift build` / Xcode build clean on both shells with no warnings about unknown systemName strings.
- UI smoke test: launch the macOS shell, visually confirm all toolbar and sidebar icons render correctly.
- Grep confirms zero residual `NSImage(systemName:` or `UIImage(systemName:` literals in the shell source files after migration. Only `MahoIcon.*` usages remain.

**Risk:** Medium. Large-scale string replacement across potentially many call sites. String literals in `NSImage(systemName:)` are only caught at runtime (nil image) not compile time — the enum conversion makes mismatches a compile error going forward. Regression is "icon renders as missing/blank"; easy to spot in smoke test.

---

### Phase 4 — Migrate Android Shell to Named Constants

**Goal:** Eliminate inline `Icons.Default.*` / `Icons.Outlined.*` / `Icons.AutoMirrored.*` references. Replace with a Kotlin `MahoIcon` sealed class (or `enum class`) generated from or kept in sync with `registry.toml`.

**Work items:**

1. Create `maho/android-shell/app/src/main/kotlin/com/maho/browser/ui/icons/MahoIcon.kt`:
   ```kotlin
   import androidx.compose.material.icons.Icons
   import androidx.compose.material.icons.automirrored.outlined.ArrowBack
   // ...

   enum class MahoIcon(val imageVector: ImageVector) {
       NavBack(Icons.AutoMirrored.Outlined.ArrowBack),
       NavForward(Icons.AutoMirrored.Outlined.ArrowForward),
       NewTab(Icons.Outlined.Add),
       // ... one entry per registry entry with an `android` mapping
   }
   ```

2. For icons backed by PNG drawables (`painterResource(R.drawable.*)`), either:
   - Convert the drawable to an `ImageVector` and add it to the enum, or
   - Keep the `R.drawable` reference but wrap it in a `painterResource` helper accessed via the icon name (e.g. `MahoDrawableIcon.SomeSpecialIcon.painter(context)` as a parallel sealed class for drawable-backed icons).

   Do not attempt to unify the two approaches in this phase; keeping them separate is acceptable as a migration step.

3. Grep `maho/android-shell/` for all `Icons.*` references from the Phase 1 audit. Replace with `MahoIcon.<Name>.imageVector`.

4. Grep for `painterResource(R.drawable.` UI-icon usages and migrate to whichever wrapper approach was chosen in step 2.

5. Do **not** touch `R.mipmap.ic_launcher*` or any launcher icon resource.

**Verification:**
- `./gradlew assembleDebug` in `maho/android-shell/` succeeds.
- Compose Preview renders all migrated icons without `null` / placeholder fallback.
- Grep confirms zero residual `Icons.Default.`, `Icons.Outlined.`, `Icons.AutoMirrored.` bare references in app source after migration.

**Risk:** Medium. Similar in scope to Phase 3. Kotlin enum approach makes missing mappings a compile error.

---

### Phase 5 — Views C++ Consistency Audit and Registry Enforcement

**Goal:** Ensure every Views icon usage (upstream `vector_icons::k*Icon` consumption + the one known Maho-defined inline custom icon) is enumerated in the registry, and introduce a lightweight enforcement mechanism so new icons cannot be added to Views without a corresponding registry entry. Additionally, propose a future home for Maho-owned custom Views icons so they stop being declared inline in unrelated TUs.

**Work items:**

1. For each Views icon usage discovered in the Phase 1 audit, confirm its registry entry has `platforms.views` filled. The dominant case is upstream `vector_icons::k*Icon` symbols; the registry simply documents which ones Maho consumes and under what semantic name. Add any gaps found.

2. Record the one known Maho-defined custom `gfx::VectorIcon` (`kArchiveboxIcon` in `maho_sidebar_library_rail_view.cc`) explicitly in the registry, mapping it to its semantic name (e.g. `archivebox`). This makes the inline declaration discoverable through the registry rather than only through full-text search.

3. Create `maho/icons/audit_icons.py` (or a `bun` script): parses `registry.toml`, extracts all `platforms.views` values, and greps `maho-chromium/browser/ui/views/` for each. Reports icons in the registry but unused in Views, and (more importantly) icons used in Views but absent from the registry. Intended to run as a non-blocking CI check or a pre-commit advisory.

4. **Propose** a future home for Maho-owned custom Views icons: a new directory such as `maho-chromium/browser/ui/vector_icons/` containing `.icon` files plus a generated `vector_icons::kMaho*Icon` namespace, mirroring Brave's pattern. **This directory does not exist today.** Creating it requires `BUILD.gn` changes and is therefore deferred behind explicit user approval (per `AGENTS.md`). Phase 5's deliverable is the *proposal* — a short design note inside `maho/icons/README.md` describing the future migration of `kArchiveboxIcon` (and any new custom icons) into that directory. No GN edits in this phase.

5. Do not remove or rename any existing icon symbol or declaration. The registry is additive in this phase; symbol moves and renames are tracked as a separate, post-approval clean-up.

**Verification:**
- `python3 maho/icons/audit_icons.py` exits 0 (no unregistered Views icons).
- Views C++ build is unaffected (no code changes to Views source in this phase).
- `maho/icons/README.md` contains the future-home proposal section for Maho-owned Views icons, clearly marked as proposal-only.

**Risk:** Very low. Documentation + audit script only. No Views source edits, no GN edits.

---

## 5. Non-Goals

Explicitly deferred or excluded — do not implement in this migration:

1. **A cross-compiled icon rendering engine.** Each platform continues to render icons natively. The registry is coordination, not a shared renderer.
2. **Automatic code generation from `registry.toml`.** Phase 3 and Phase 4 enums are hand-authored in this plan. Code generation is a follow-up if and when the registry stabilises.
3. **WebUI icon unification.** `maho-chromium/browser/resources/` uses CSS/SVG icons. Those are a separate pipeline and out of scope.
4. **App icon / branding asset consolidation.** `AppIcon.icns`, launcher `.png` files, and splash screens are not UI icons. Branding asset management is its own domain.
5. **Icon color token standardisation.** This plan does not address the color tokens or theme tokens passed to `gfx::CreateVectorIcon`. Color mapping is a separate theming concern.
6. **Forking or shadowing upstream Chromium icons.** Icons pulled directly from Chromium's own icon set (e.g. `vector_icons::kBackArrowIcon`, `vector_icons::kHomeIcon`) remain upstream-managed. The registry documents which upstream symbols Maho consumes under which semantic name, but does not own them and does not duplicate them.
7. **Creation of `maho-chromium/browser/ui/vector_icons/` (or any new GN target for Maho-owned Views icons).** Phase 5 only *proposes* this future home; actually creating the directory and wiring its `BUILD.gn` requires explicit user approval and is out of scope for this plan.
8. **GN file modifications.** No `BUILD.gn` edits are part of this plan. If a Phase 3/4 Swift/Kotlin wrapper file requires a new build target, that is noted as requiring explicit user approval before execution.
9. **Retroactive size normalisation.** Icons that happen to be rendered at non-standard sizes in existing code are not corrected by this plan; the `sizes` field in the registry records intent, not a mandatory runtime constraint.
10. **Icon animation or Lottie assets.** Out of scope.

---

## 6. Open Questions

1. **Swift package sharing between macOS and iOS shells.** `maho/macos-shell/` and `maho/ios-shell/` may already share a Swift package or SPM workspace. If so, `MahoIcon.swift` can live in a shared module and avoid duplication. If not, maintaining two parallel files is acceptable in Phase 3 with a note to unify later. Needs investigation before Phase 3 begins.

2. **SF Symbols availability floor.** Some SF Symbols names (`chevron.left.2`) were introduced in iOS 14 / macOS 11. The iOS shell's deployment target needs to be confirmed before Phase 3. If the floor is below these targets, fallback image assets may be needed for some cases.

3. **`MahoIcon` enum generation vs. hand-authoring.** Hand-authoring is proposed for Phase 3/4 to avoid introducing a build-time code generator. If the registry grows beyond ~80 entries, the maintenance cost of keeping the enum in sync manually may justify a lightweight `bun` script to generate it. Decision deferred to after Phase 2 completes.

4. **Registry format: TOML vs. JSON.** TOML is human-writable and supports comments. JSON is easier to parse from both Python and `bun` scripts. Either works; the choice should be consistent with whatever format conventions already exist in `maho/` for configuration files.

5. **Handling Views icons sourced from upstream `vector_icons::k*`.** Most Views icon usage today is direct upstream consumption. If upstream renames or removes a symbol, the Maho codebase has a silent dependency. The registry audit script (Phase 5) can surface these but cannot do more without GN changes. Accepted risk for now.

6. **Android `painterResource(R.drawable.*)` vs. `ImageVector`:** Some drawable XML icons may not faithfully translate to `ImageVector` (e.g. they rely on `<clip-path>` features not supported by `ImageVector`). Identify problematic drawables during Phase 4 audit before committing to the `ImageVector` conversion path.

---

## 7. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| SF Symbols name unavailable on minimum deployment target | Medium | Medium | Confirm OS floor before Phase 3; add `UIImage(named:)` fallback assets for any symbol below the floor |
| `MahoIcon` enum drifts from `registry.toml` over time | Medium | Low | Phase 5 audit script catches drift; add to CI advisory checks post-Phase 5 |
| String-literal SF Symbols replacement introduces a typo (runtime nil) | Medium | Medium | The Swift enum wrapping is the mitigation; zero residual string literals = zero runtime nil risk post-migration |
| Android `ImageVector` conversion breaks a complex drawable | Low | Medium | Keep original drawable as a parallel `R.drawable` entry and migrate incrementally; do not force conversion in Phase 4 |
| Inventory in Phase 1 misses dynamically-constructed icon names | Low | Low | Dynamic construction is rare in native shells; grep sweep + manual UI walkthrough covers the common case |
| Registry TOML grows large and becomes a merge conflict hotspot | Low | Low | One row per icon; typical icon set for a browser shell is 30–80 entries; not a problematic file size |
| Phase 3/4 creates a large diff that is hard to review | Medium | Low | Split the Swift/Kotlin migration by shell area (toolbar icons, sidebar icons, tab strip icons) rather than one giant PR |
| New icon added to a shell after registry freeze without updating registry | Medium | Low | Phase 5 audit script + PR review reminder in `maho/icons/README.md` |

---

## 8. Recommended Execution Order

| Phase | Deliverable | Depends on | Risk | Effort estimate |
|---|---|---|---|---|
| Phase 1 — Audit | `maho/icons/audit-2026.md` | Nothing | Very low | 1–2 days |
| Phase 2 — Registry draft | `maho/icons/registry.toml` + `README.md` | Phase 1 | Very low | 1 day |
| Phase 3 — Apple shell migration | `MahoIcon.swift` + replaced literals | Phase 2 | Medium | 2–4 days |
| Phase 4 — Android shell migration | `MahoIcon.kt` + replaced literals | Phase 2 | Medium | 2–4 days |
| Phase 5 — Views audit + future-home proposal | audit script + README proposal section | Phase 2 | Very low | 1 day |

**Recommended overall order:** **Phase 1 → Phase 2 → (Phase 3 ∥ Phase 4 ∥ Phase 5).**

Phase 1 is a strict prerequisite for everything else: without an inventory, the registry is guesswork. Phase 2 is the gating artefact — the `registry.toml` must exist and be reviewed/approved before any platform migration begins, because Phases 3, 4, and 5 all reference its `name` fields. After Phase 2 is approved, Phases 3, 4, and 5 are mutually independent and can proceed in parallel by different contributors.

**Concrete day-1 actions when work starts:**

1. Run the Views grep sweep described in Phase 1 (Section 4, work item 1) and capture results into `maho/icons/audit-2026.md`.
2. In parallel, run the macOS / iOS / Android shell grep sweeps (Phase 1 work items 2–4) into the same audit file.
3. Cluster the raw names into logical groups (Phase 1 work item 5) and identify the "low-hanging" icons that already appear on all four platforms with obvious correspondences (e.g. back/forward, new tab, close).
4. Draft `registry.toml` starting from those high-confidence entries (Phase 2). Single-platform-only icons can be added later.
5. Open Phase 2 for review and approval **before** starting any platform migration. Do not begin Phases 3/4/5 work in earnest until the registry format is locked.

**Stop-and-confirm gates (do not silently proceed past these):**

- After Phase 1: human review of audit completeness.
- After Phase 2: explicit user approval of `registry.toml` schema + initial entries. This is the point at which the registry becomes the approved system referenced by `AGENTS.md` (per the no-new-icon-systems-without-approval rule).
- Before any GN-affecting work in Phase 5 (specifically, creating `maho-chromium/browser/ui/vector_icons/`): explicit user approval. Phase 5 as scoped here stops short of that and only produces a written proposal.

Each phase produces a standalone, reviewable artefact. No phase requires a prior phase to be fully merged before it can start — only Phase 2's `registry.toml` must exist before Phase 3/4/5 work can reference it.

---

## 9. Files Affected (by phase)

### Phase 1 — New files only
- `maho/icons/audit-2026.md` (new)

### Phase 2 — New files only
- `maho/icons/registry.toml` (new)
- `maho/icons/README.md` (new)

### Phase 3 — New + modified
- `maho/macos-shell/Sources/MahoIcons/MahoIcon.swift` (new)
- `maho/ios-shell/Sources/MahoIcons/MahoIcon.swift` (new, or shared module)
- All `.swift` files in `maho/macos-shell/` containing `NSImage(systemName:` literals (modified)
- All `.swift` files in `maho/ios-shell/` containing `UIImage(systemName:` or `Image(systemName:` literals (modified)

### Phase 4 — New + modified
- `maho/android-shell/app/src/main/kotlin/com/maho/browser/ui/icons/MahoIcon.kt` (new)
- All `.kt` files in `maho/android-shell/` containing `Icons.Default.`, `Icons.Outlined.`, `Icons.AutoMirrored.`, or `painterResource(R.drawable.` UI-icon usages (modified)

### Phase 5 — New + doc-only modifications
- `maho/icons/audit_icons.py` (new, non-blocking audit script)
- `maho/icons/README.md` (modified — add proposal section for future Maho-owned Views icon location, e.g. `maho-chromium/browser/ui/vector_icons/`; this directory is **not** created in this phase)

### Untouched (per scope / `AGENTS.md`)
- All `BUILD.gn` files (require explicit user approval for any change)
- `maho-chromium/browser/resources/**` (WebUI icons, out of scope)
- All `AppIcon.*`, `ic_launcher.*`, `Assets.xcassets/AppIcon*` (branding assets, out of scope)
- `maho-chromium/browser/ui/views/**` C++ source files (Phase 5 is documentation only; no Views logic changes; the inline `kArchiveboxIcon` in `maho_sidebar_library_rail_view.cc` is left in place and is only *registered*, not relocated)
- `maho-chromium/browser/ui/vector_icons/` — does not exist today and is **not** created by this plan
- `maho/crates/**` (no Rust crate changes in this plan)

---

## 10. Acceptance Checklist

- [ ] User reviews and approves this migration plan
- [ ] Phase 1 audit covers all four non-WebUI surfaces and confirms scope of branding asset exclusion
- [ ] Phase 2 `registry.toml` reviewed and agreed as the canonical format before Phase 3/4/5 begin
- [ ] Phase 3 Swift migration: zero residual `NSImage(systemName:` / `UIImage(systemName:` literals; macOS and iOS shells build clean
- [ ] Phase 4 Kotlin migration: zero residual `Icons.*` bare references; Android shell `assembleDebug` clean
- [ ] Phase 5 audit script exits 0 on main after Phases 3 and 4 land
- [ ] `maho/icons/README.md` documents the workflow for adding new icons going forward
- [ ] No GN edits made without explicit user approval
- [ ] No branding assets touched
- [ ] No WebUI icon pipeline touched
