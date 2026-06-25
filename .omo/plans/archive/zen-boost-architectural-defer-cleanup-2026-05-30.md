# Zen Boost — Architectural Defer Cleanup

**Date**: 2026-05-30
**Author**: Sisyphus
**Status**: Ready for delegation
**Predecessor**: `.sisyphus/plans/zen-boost-structural-port-plan-2026-05-29.md` and the bg agent audit reports (sessions `ses_189f388dbffebrqW9yVi2jsgNv`, `ses_189f3c6b1ffek1WIVu69m2csiJ`, `ses_189f31f97ffeyn63f6O7f963vP`).

## Goal

Resolve the 5 outstanding items left over from the comprehensive Zen reference audit:
- 4 "lazy defers" (A1, A3, B1, B3)
- 1 missed item (`magic_theme` handling in `compose_css`)

After this plan ships, Maho Boost matches Zen reference behavior on every behavioral axis except those that physically require Gecko engine patches (color compositor, smart-invert at engine level, full-zoom at engine level — already approximated via Oracle-recommended CSS strategy in the previous plan).

## Acceptance Criteria

- [ ] `cargo test --workspace` green
- [ ] `lsp_diagnostics` clean on every changed file
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` exits 0
- [ ] `Maho.app` produced and launchable
- [ ] All 5 items either implemented or explicitly recorded as deferred with documented reason in this file's Section 7

## Non-Goals

- Zen profile data migration tool (`zen-boosts.jsonlz4` → SQLite reader). Out of scope.
- Custom CSS file storage split (Zen `{profile}/zen-boosts/{id}.css`). Architectural change, separate plan.
- Workspace gradient integration for Zen `autoTheme=true` color derivation. Requires Maho workspace API. Documented in §7.
- Updating C++ JSON keys for the `magic_theme`/`autoTheme` rename. **This plan ships an alias-only change** (input compat). Full bidirectional rename is a follow-up.

## Constraints

- Workspace root `.build.lock` serializes Rust + Chromium build entrypoints. WU-4 (build) is the only step that takes the lock; WU-1, WU-2, WU-3 only run cargo check/test which can take it briefly.
- `maho-chromium/AGENTS.md`: GN file edits forbidden without explicit user permission. None expected here — all edits are inside source files referenced by existing GN targets.
- No suppression of type errors, no commits, no git operations (per session contract).

## Parallelism Topology

```
       ┌─────────┐  ┌─────────┐  ┌─────────┐
       │  WU-1   │  │  WU-2   │  │  WU-3   │
       │  Rust   │  │  Rust   │  │   TS    │
       │ types   │  │  core   │  │  app    │
       └────┬────┘  └────┬────┘  └────┬────┘
            │            │            │
            └────────────┼────────────┘
                         ▼
                    ┌────────┐
                    │  WU-4  │
                    │ Verify │
                    │ + Build│
                    └────────┘
```

WU-1, WU-2, WU-3 all run **in parallel**. They touch disjoint files (no merge conflicts). WU-4 is a synchronization gate that runs once all three complete.

## Work Units

---

### WU-1: Rust serde alias for `autoTheme` (item A1)

**Severity**: Trivial
**Owner**: `task(category="quick", load_skills=[], ...)`
**Estimated effort**: 5 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho/crates/maho-types/src/boost.rs` | Add 2 serde attributes |

#### Spec

The Rust struct field `magic_theme` currently serializes as `"magicTheme"` (camelCase from `rename_all = "camelCase"`). Zen exports use the key `"autoTheme"`. Add an `alias` so deserialization accepts both keys; serialization continues to emit `"magicTheme"` (we keep the internal name).

#### Exact changes

In `boost.rs`, two structs hold this field. Both must get the alias:

1. **`ColorBoost.magic_theme`** (around line 108):
   ```rust
   #[derive(Clone, Debug, Serialize, Deserialize)]
   #[serde(rename_all = "camelCase")]
   pub struct ColorBoost {
       // ...existing fields...
       #[serde(alias = "autoTheme")]
       pub magic_theme: bool,
       // ...rest...
   }
   ```

2. **`ColorBoostUpdate.magic_theme`** (around line 157):
   ```rust
   #[derive(Clone, Debug, Default, Serialize, Deserialize)]
   #[serde(rename_all = "camelCase")]
   pub struct ColorBoostUpdate {
       // ...existing fields...
       #[serde(skip_serializing_if = "Option::is_none", alias = "autoTheme")]
       pub magic_theme: Option<bool>,
       // ...rest...
   }
   ```

#### Verification

```bash
cd maho && cargo check --package maho-types
cd maho && cargo test --package maho-core --lib boost_manager::tests::test_export_import_roundtrip
```

Plus a new test in `maho-types/tests/` (or `boost_manager` tests):

```rust
#[test]
fn test_zen_autoTheme_alias_deserializes() {
    let json = r#"{"dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"autoTheme":true,"colorBoostEnabled":false,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5}"#;
    let cb: maho_types::boost::ColorBoost = serde_json::from_str(json).unwrap();
    assert!(cb.magic_theme, "autoTheme alias must populate magic_theme");
}
```

#### Risk

None. `serde(alias)` is additive — does not break existing deserialization paths.

---

### WU-2: Rust core — export envelope translation + `magic_theme` in `compose_css` (items A3 + missed)

**Severity**: Medium
**Owner**: `task(category="deep", load_skills=[], ...)` — needs planning + multi-file edit
**Estimated effort**: 45-60 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho/crates/maho-types/src/boost.rs` | Add `ZenCompatBoostData` flat-shape DTO struct |
| `maho/crates/maho-core/src/boost_manager.rs` | Modify `export_boost` to emit Zen-compat shape; modify `import_boost` (if exists) to accept Zen-compat shape; modify `compose_css` to acknowledge `magic_theme` path |
| `maho/crates/maho-core/src/maho_core.rs` | Update `*_persisted` wrappers if signature changes |

#### Spec — Part 1: Export/import shape

Zen's `exportBoost` (ZenBoostsManager.sys.mjs:686-739) does `JSON.stringify(boostData)` — the inner flat object. Zen's `importBoost` does `JSON.parse(fileContent)` and expects the same flat shape. Field names use Zen's vocabulary: `boostName`, `enableColorBoost`, `dotAngleDeg`, `dotPos: {x, y}`, `secondaryDotPos: {x, y}`, `secondaryDotAngleDegDelta`, `dotDistance`, `magicTheme`/`autoTheme`, `smartInvert`, `brightness`, `saturation`, `contrast`, `fontFamily`, `textCaseOverride`, `sizeOverride`, `zapSelectors`, `customCSS`, `changeWasMade`.

Our internal `Boost` is nested (`color: {...}`, `typography: {...}`) and uses `name` not `boostName`, `customCss` not `customCSS`, `caseMode` not `textCaseOverride`, `sizeMode` (enum) not `sizeOverride` (float), and includes Maho-only fields (`id`, `domain`, `created_at`, `updated_at`).

Translation contract (this is a **DTO layer**, not a schema rewrite):

1. Define a new struct `ZenCompatBoostData` in `boost.rs` with serde annotations matching Zen's flat shape exactly.
2. Implement `From<&Boost> for ZenCompatBoostData` (export) and `TryFrom<(ZenCompatBoostData, /*domain*/ String)> for Boost` (import).
3. `export_boost(id) -> Option<String>` serializes the DTO instead of the full `Boost`.
4. `import_boost(domain, json)` first tries DTO deserialization. If it succeeds → reconstruct `Boost` with new id + provided domain + current timestamps. If it fails → fall back to legacy full-`Boost` deserialization (so Maho-exported files still import).

Field mapping reference (Rust internal → Zen-compat JSON key):

| `Boost` field | DTO field | JSON key (camelCase) |
|---|---|---|
| `name` | `boost_name` | `boostName` |
| `color.dot_pos` | `dot_pos` | `dotPos` |
| `color.dot_distance` | `dot_distance` | `dotDistance` |
| `color.dot_angle_deg` | `dot_angle_deg` | `dotAngleDeg` |
| `color.secondary_dot_pos` | `secondary_dot_pos` | `secondaryDotPos` |
| `color.secondary_dot_angle_deg_delta` | `secondary_dot_angle_deg_delta` | `secondaryDotAngleDegDelta` |
| `color.magic_theme` | `magic_theme` (with `alias = "autoTheme"`) | `magicTheme` (accepts `autoTheme`) |
| `color.color_boost_enabled` | `enable_color_boost` (with `alias = "colorBoostEnabled"`) | `enableColorBoost` |
| `color.smart_invert` | `smart_invert` | `smartInvert` |
| `color.contrast` | `contrast` | `contrast` |
| `color.brightness` | `brightness` | `brightness` |
| `color.saturation` | `saturation` | `saturation` |
| `typography.font_family` | `font_family` | `fontFamily` |
| `typography.case_mode` | `text_case_override` (custom serde with mapping) | `textCaseOverride` (string `"none"`/`"uppercase"`/`"lowercase"`/`"capitalize"`) |
| `typography.size_mode` | `size_override` (f32) | `sizeOverride` (use `as_zoom()` for export, `from_zoom` for import) |
| `zap_selectors` | `zap_selectors` | `zapSelectors` |
| `custom_css` | `custom_css` (renamed) | `customCSS` |
| `change_was_made` | `change_was_made` | `changeWasMade` |

Maho-only fields (`id`, `domain`, `created_at`, `updated_at`) are NOT in the DTO. On import, these are regenerated:
- `id` ← new UUID
- `domain` ← caller-provided (existing FFI signature already accepts domain)
- `created_at` ← `DateTime::now()`
- `updated_at` ← `DateTime::now()`

#### Spec — Part 2: `magic_theme` in `compose_css`

Currently `compose_css` ignores `boost.color.magic_theme` entirely. Zen behavior (ZenBoostsChild.sys.mjs:370-393): when `autoTheme=true`, derive primary hue from the workspace gradient's primary color, then feed into `#buildBoostColor` with the same saturation/brightness/contrast.

We have no workspace gradient API exposed to `maho-core`. Options:

**Option A (recommended for this plan)**: Document `magic_theme` as a known gap. In `compose_css`, when `magic_theme && color_boost_enabled`, emit a CSS comment marker `/* Magic Theme: workspace integration pending */` so it shows up in DevTools. Behavior unchanged from `magic_theme=false`. The toggle visually updates the button state (already wired in `app.ts:updateButtonToggleVisuals`) but doesn't change page CSS yet.

**Option B (deferred)**: Add a `workspace_primary_hue: Option<f64>` parameter to `compose_css` (or store it on `Boost`). The Chromium C++ layer would populate this from `MahoCore`'s active workspace before calling `GetBoostInjectionCss`. Larger surface change — defer.

→ **Implement Option A in this plan.** Track Option B as TODO in `§7 Deferred items`.

#### Exact changes

1. In `boost.rs`, add a new struct after `Boost`:

   ```rust
   #[derive(Clone, Debug, Serialize, Deserialize)]
   #[serde(rename_all = "camelCase")]
   pub struct ZenCompatBoostData {
       pub boost_name: String,
       pub dot_pos: Point,
       pub dot_distance: f32,
       pub dot_angle_deg: f64,
       pub secondary_dot_pos: Point,
       pub secondary_dot_angle_deg_delta: f64,
       #[serde(alias = "autoTheme")]
       pub magic_theme: bool,
       #[serde(alias = "colorBoostEnabled")]
       pub enable_color_boost: bool,
       pub smart_invert: bool,
       pub contrast: f64,
       pub brightness: f64,
       pub saturation: f64,
       pub font_family: String,
       pub text_case_override: String,
       pub size_override: f32,
       pub zap_selectors: Vec<String>,
       #[serde(rename = "customCSS")]
       pub custom_css: String,
       pub change_was_made: bool,
   }
   ```

   Plus `From<&Boost>` and `TryFrom<(ZenCompatBoostData, String)>` impls in the same file.

2. In `boost_manager.rs`:
   - Replace `export_boost` body to use the DTO:
     ```rust
     pub fn export_boost(&self, id: &BoostId) -> Option<String> {
         let boost = self.boosts.get(id)?;
         let dto = ZenCompatBoostData::from(boost);
         serde_json::to_string(&dto).ok()
     }
     ```
   - Add (or update) `import_boost`:
     ```rust
     pub fn import_boost(&mut self, domain: String, json: &str) -> Option<Boost> {
         // Try Zen-compat shape first
         if let Ok(dto) = serde_json::from_str::<ZenCompatBoostData>(json) {
             if let Ok(boost) = Boost::try_from((dto, domain.clone())) {
                 self.boosts.insert(boost.id.clone(), boost.clone());
                 return Some(boost);
             }
         }
         // Fallback: full Boost shape (Maho-exported)
         if let Ok(mut boost) = serde_json::from_str::<Boost>(json) {
             boost.id = BoostId::new(uuid::Uuid::new_v4().to_string());
             boost.domain = domain;
             boost.created_at = DateTime::now();
             boost.updated_at = DateTime::now();
             self.boosts.insert(boost.id.clone(), boost.clone());
             return Some(boost);
         }
         None
     }
     ```
   - In `compose_css`, after the `color_on || invert_on` block:
     ```rust
     if boost.color.magic_theme && boost.color.color_boost_enabled {
         css.push_str("/* Magic Theme: workspace integration pending — see plan §7 */\n");
     }
     ```

3. In `maho_core.rs`, the `import_boost_persisted` (if exists) must call `boost_manager.import_boost` then persist. Audit existing code path and update accordingly.

4. Add or update tests:
   ```rust
   #[test]
   fn test_export_emits_zen_compat_shape() {
       let mut mgr = BoostManager::new();
       let boost = mgr.create_temp("example.com".to_string());
       let json = mgr.export_boost(&boost.id).unwrap();
       let value: serde_json::Value = serde_json::from_str(&json).unwrap();
       assert!(value.get("boostName").is_some(), "must use Zen 'boostName'");
       assert!(value.get("enableColorBoost").is_some(), "must use Zen 'enableColorBoost'");
       assert!(value.get("textCaseOverride").is_some(), "must use Zen 'textCaseOverride'");
       assert!(value.get("sizeOverride").is_some(), "must use Zen 'sizeOverride'");
       assert!(value.get("customCSS").is_some(), "must use Zen 'customCSS'");
       assert!(value.get("name").is_none(), "must NOT use Maho 'name'");
       assert!(value.get("domain").is_none(), "must NOT include Maho-only 'domain'");
   }

   #[test]
   fn test_import_accepts_zen_compat_with_autoTheme() {
       let mut mgr = BoostManager::new();
       let zen_json = r#"{"boostName":"X","dotPos":{"x":0,"y":0},"dotDistance":0,"dotAngleDeg":0,"secondaryDotPos":{"x":0,"y":0},"secondaryDotAngleDegDelta":0,"autoTheme":true,"enableColorBoost":true,"smartInvert":false,"contrast":0.75,"brightness":0.5,"saturation":0.5,"fontFamily":"","textCaseOverride":"none","sizeOverride":1,"zapSelectors":[],"customCSS":"","changeWasMade":true}"#;
       let boost = mgr.import_boost("example.com".to_string(), zen_json).unwrap();
       assert_eq!(boost.name, "X");
       assert!(boost.color.magic_theme);
       assert!(boost.color.color_boost_enabled);
   }
   ```

#### Verification

```bash
cd maho && cargo test --package maho-core --lib boost_manager::
```

Existing test `test_export_import_roundtrip` must still pass (Maho-exported → Maho-imported via fallback path).

#### Risk

- **Round-trip stability**: Maho exports drop `id`/`created_at`. Re-importing produces fresh values. This is correct (imported boost is a NEW boost, not the same one), but tests asserting `id` equality after roundtrip will need updating.
- **`text_case_override` string mapping**: Manual `From`/`TryFrom` impl needs the four-way map. Easy to typo. Cover with unit test.

---

### WU-3: TypeScript app.ts polish — `setDotPos` 15px throttle (B1) + font availability filter (B3)

**Severity**: Low-Medium
**Owner**: `task(category="visual-engineering", load_skills=["frontend-ui-ux"], ...)`
**Estimated effort**: 25-30 minutes

#### Files

| Path | Edit type |
|---|---|
| `maho-chromium/browser/resources/maho_boost/app.ts` | Two surgical edits in disjoint regions |

#### Spec — Part 1: B1 — `lastDotSetPos` 15px throttle

Zen `setDotPos` (ZenBoostsEditor.mjs:713-816) skips work if the drag delta from the last applied position is < 15px AND `animate=false`. This avoids recomputing arc geometry / firing updates on every mouse-move event during a long drag.

Implementation in `app.ts`:
1. Add a private field `lastDotSetPos: {x: number; y: number} | null = null;` near other fields (around line 396).
2. In `setDotPos(clientX, clientY, animate)` body, near the start (after the `!this.currentBoost` guard):
   ```ts
   if (!animate && this.lastDotSetPos && clientX !== null && clientY !== null) {
     const dx = clientX - this.lastDotSetPos.x;
     const dy = clientY - this.lastDotSetPos.y;
     if (Math.hypot(dx, dy) < 15) {
       return;
     }
   }
   ```
3. Update `lastDotSetPos` at the end of the function (only when not the null-coordinates reset path):
   ```ts
   if (clientX !== null && clientY !== null) {
     this.lastDotSetPos = {x: clientX, y: clientY};
   }
   ```

#### Spec — Part 2: B3 — font availability filter

In `initFonts` (around line 762), after `this.systemFonts = fonts.slice();`, replace the unconditional `for (const font of COMMON_FONTS)` loop with a filtered version:

```ts
const availableCommon = COMMON_FONTS.filter(f => this.systemFonts.includes(f));
for (const font of availableCommon) {
  // existing button creation code
}
```

If `availableCommon.length === 0` (rare — system has none of the common fonts), fall back to the unfiltered list (so the grid is never empty). This matches Zen's "show available, fall back to defaults" approach.

#### Verification

```bash
# After edits:
# 1. lsp_diagnostics on app.ts must report 0 errors
# 2. Visual sanity: build and confirm font grid has no Aa buttons for missing fonts
```

#### Risk

- **B1**: 15px is a CSS-pixel threshold; on high-DPI displays this corresponds to ~1.5x physical pixels of throttle. Should not impact perceived smoothness.
- **B3**: `this.systemFonts` is populated from `pageHandler.getSystemFonts()` Mojo call. If that call fails (returns empty array), falling back to unfiltered is necessary — covered above.

---

### WU-4: Verification + Build (gate task)

**Severity**: Mandatory
**Owner**: Direct (no delegation)
**Estimated effort**: 5-15 minutes (mostly build wall time)

#### Steps

1. Wait for WU-1, WU-2, WU-3 to all report success.
2. `cd maho && cargo check --workspace` — must finish cleanly.
3. `cd maho && cargo test --workspace` — all green.
4. `lsp_diagnostics` on:
   - `maho-chromium/browser/resources/maho_boost/app.ts`
   - `maho/crates/maho-types/src/boost.rs`
   - `maho/crates/maho-core/src/boost_manager.rs`
5. `python3 maho-chromium/build/scripts/build_maho.py` — exit 0, `Maho.app` produced.
6. Smoke-test launch (user-driven, not in this plan): user opens boost editor, confirms shuffle, color picker, advanced color popup, font grid, export work.

#### On failure

- WU-1 fail → fix `boost.rs` annotations; re-run cargo check
- WU-2 fail (compile) → most likely missed `From`/`TryFrom` impl; fix and re-test
- WU-2 fail (test) → field mapping typo; cross-check against table in §3.WU-2
- WU-3 fail (LSP) → field type mismatch; check `lastDotSetPos` declaration
- Build fail → see `chromium/src/out/Default/build.ninja.log`; common causes are stylelint (`#fff` vs `#ffffff`) and TS strict checks

---

## §6 Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| WU-2 `From`/`TryFrom` typo on size_override mapping | Medium | Medium | Round-trip test `test_export_emits_zen_compat_shape` plus manual import-Maho-exported test |
| Existing test `test_export_import_roundtrip` breaks due to id/timestamp regen on import | Medium | Low | Update assertions to compare semantic equivalence (exclude id/created_at), not deep-eq |
| `magic_theme` Option A leaves toggle visually present but functionally inert; user confusion | Low | Low | Document in §7; add tooltip/hint copy in follow-up |
| C++ side still emits `magicTheme` while Zen-compat export emits same; bidirectional asymmetry | Low | Low | Alias on read covers Zen→Maho. Maho→Zen export currently uses Maho format. Out of scope. |

## §7 Deferred Items (with reason)

These are explicitly NOT addressed by this plan:

1. **C++ JSON key rename** `magicTheme` → `autoTheme` everywhere (full bidirectional Zen compat). Touches `maho_boost_page_handler.cc::SerializeBoostUpdate` + `ParseColorBoost`. Not blocking; alias covers the import direction. **Track as follow-up.**

2. **`magic_theme` workspace integration**. Zen reads `boost.workspaceGradient` to derive hue when `autoTheme=true`. We have no workspace API exposed to `maho-core`. This plan ships Option A (no-op + comment). **Follow-up: expose workspace primary hue to `compose_css`.**

3. **Custom CSS file storage** (Zen `{profile}/zen-boosts/{id}.css`). Real architectural change in `maho-storage`. Separate plan needed.

4. **Zen profile data importer** (`zen-boosts.jsonlz4` → SQLite). User has not asked for this. Defer until requested.

5. **Skip-stylesheet-when-color-enabled rule** (Zen `ZenBoostsChild.sys.mjs:368-409`). Zen returns early without loading the CSS sheet when `enableColorBoost=true` because the engine handles color. **We intentionally diverge** — our environment lacks engine support, so the CSS must do everything. Not a defer; an intentional, documented behavioral divergence.

6. **`setSecondaryDotPos` falsy null check** (B2 from audit). Edge case at coord `(0, 0)`. Our strict null check is arguably more correct. Skip.

## §8 Delegation Recipe (for the orchestrator)

Run all three in parallel via `task(... run_in_background=true)`:

```typescript
// WU-1
task(
  category="quick",
  load_skills=[],
  description="Add serde alias for autoTheme",
  prompt="See .sisyphus/plans/zen-boost-architectural-defer-cleanup-2026-05-30.md §WU-1. Implement exactly. Run cargo check on maho-types when done.",
  run_in_background=true
)

// WU-2
task(
  category="deep",
  load_skills=[],
  description="Zen-compat export envelope + magic_theme comment",
  prompt="See .sisyphus/plans/zen-boost-architectural-defer-cleanup-2026-05-30.md §WU-2. Implement DTO + From/TryFrom + import_boost dual-shape support + compose_css magic_theme comment marker. Run cargo test --package maho-core --lib boost_manager:: when done.",
  run_in_background=true
)

// WU-3
task(
  category="visual-engineering",
  load_skills=["frontend-ui-ux"],
  description="setDotPos 15px throttle + font availability filter",
  prompt="See .sisyphus/plans/zen-boost-architectural-defer-cleanup-2026-05-30.md §WU-3. Two surgical edits in app.ts. Run lsp_diagnostics afterward.",
  run_in_background=true
)
```

Wait for all three system-reminder completion notifications. Collect outputs, then orchestrator runs WU-4 directly (cargo + build).

## §9 Out-of-Plan Notes

- `.build.lock` is held only during build (WU-4). WU-1/2/3 use `cargo check`/`cargo test` which take the lock briefly and serialize naturally; no special coordination required.
- No GN files are modified.
- No new files are introduced outside `boost.rs` DTO addition (which is in an existing file).
- No image/icon assets touched.

---

**End of plan.**
