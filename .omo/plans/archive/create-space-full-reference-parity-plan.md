# Implementation Plan: Native Create Space Sidebar — Full Reference Parity

## 1. Goal

Make the native Chromium Views surface implemented in
`maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.{cc,h}`
visually match the canonical reference screenshot at
`maho/reference_screenshots/create-space.jpeg` to the extent achievable in
native Views, **including the hero illustration, all icons, row treatment,
input treatment, spacing, and pagination dots — not only constant tuning**.

Behavior, FFI flow, and existing test contract must remain intact.

## 2. Source of Truth

| Layer | Authority |
|---|---|
| Pixel-level visual target | `maho/reference_screenshots/create-space.jpeg` |
| Behavior + test contract | `maho_sidebar_spaces_interactive_test.cc` (`MahoSidebarSpacesInteractiveTest.*`) |
| Reusable native style tokens | `maho_sidebar_layout_tokens.h`, `maho_sidebar_search_view.cc`, `maho_sidebar_action_popover_view.cc`, `maho_sidebar_library_action_pane_view.cc`, `maho_sidebar_footer_view.cc` |
| Icons | macOS: `MahoMacSFSymbolHelper` (`browser/ui/views/maho_mac_sf_symbol_helper.{h,mm}`). Cross-platform fallback: `components/vector_icons/vector_icons.h` |

If the screenshot conflicts with prior plans or comments, **the screenshot wins**.

## 3. Complete Delta Inventory (current vs. reference)

This list intentionally includes everything visual, not just constants.

### 3.1 Hero illustration
- **Reference:** Stack of three slightly fanned tab/cards depicting a laptop with green plant accents; warm, illustrative.
- **Current:** `CreateSpaceHeroIllustrationView` custom-paints three abstract overlapping rounded rectangles with a small primary-colored dot and two muted lines. Visually generic.
- **Delta:** The current paint is structurally similar (3 stacked cards) but lacks the laptop body and plant accents that give the reference its character.
- **Decision:** Keep `CreateSpaceHeroIllustrationView` as the host class (preserves layout integration). Replace its `OnPaint()` body with a higher-fidelity vector composition: laptop silhouette in the front card, simple foliage stroke accents on the back cards. Pure custom paint, no new asset. No FFI/asset pipeline change required.

### 3.2 Title + subtitle
- Already correct: `Create a Space` / `Separate your tabs for life, work, projects, and more.`
- Center alignment already applied. **No change.**

### 3.3 Name row
- **Reference:** Visually balanced row with a dashed `+` icon on the left and a `Space name...` textfield on the right. The textfield looks like a darker card-tone pill, internal blue caret visible.
- **Current:** Dashed placeholder + textfield with a 1dp `kColorSysSurfaceVariant` border, `kInputCornerRadius=12`, `kInputHeight=44`. Background painted via `OnThemeChanged()` with `kColorSysSurface3`.
- **Deltas:**
  1. Border treatment uses raw `SetBorder(CreateRoundedRectBorder(...))` rather than `CreatePaddedBorder(CreateRoundedRectBorder(...), insets)`. Padding can fight pixel-perfect parity.
  2. Dashed placeholder feels slightly small relative to the textfield in the current layout.
- **Decision:** Adopt the `maho_sidebar_search_view.cc` input idiom (`CreatePaddedBorder` wrapping a rounded border, transparent background on the textfield, surface set on parent). Tune dashed-placeholder weight (stroke width / dash length) only after the textfield treatment is correct.

### 3.4 Profile row
- **Reference:** Card-tone row with: leading profile glyph tile, `Profile` label, flex spacer, `Default ▾` pill with chevron, trailing `ⓘ` info glyph. Compact, balanced.
- **Current:** Card row implemented with `kColorSysSurface1` + `kColorSysSurfaceVariant` border, leading icon tile with custom-painted `CreateSpaceGlyphView::kProfile`, label, spacer, `profile_pill_` rounded view, custom-painted `kInfo` glyph.
- **Deltas:**
  1. Profile glyph is currently fully custom-painted. macOS SF Symbol equivalent (`person.crop.circle.fill` or `person.fill`) would match reference visual weight better and stay consistent with the rest of the sidebar (which already uses SF Symbols on macOS).
  2. Pill text is `Default` only; reference shows `Default ▾` (with a chevron). Currently no chevron in the pill.
  3. Info glyph stroke weight may need to align to SF Symbol `info.circle` for consistency.
  4. Row height/padding/pill height may read slightly heavy.
- **Decision:**
  - macOS: render the leading tile glyph and trailing info glyph via `CreateMahoMacSFSymbol(...)` inside an `views::ImageView`.
  - Non-macOS: fall back to `vector_icons` (`kAccountCircleIcon` / `kInfoIcon` or closest) via `ui::ImageModel::FromVectorIcon`.
  - Add a chevron glyph (`chevron.down` SF Symbol or `kArrowDropDownIcon` vector icon) inside `profile_pill_`, after the `Default` label.
  - Final row dimensions tuned in §3.7 constants pass.

### 3.5 Theme row
- **Reference:** Single-line card row: brush glyph tile + `Choose a Theme`. No visible preview dot, no chevron in the closed state.
- **Current:** Card row with brush glyph tile, label, flex spacer, `theme_preview_dot_` (visible), trailing `›` chevron, plus a hidden `theme_summary_label_`.
- **Deltas:**
  1. Reference closed state is much simpler — preview dot and chevron are not visible.
  2. After theme selection, summary text appears (`Custom theme`) per existing tests; this still applies, but should remain hidden until a theme is chosen.
- **Decision:**
  - Hide `theme_preview_dot_` in the baseline state, show it after a theme is applied (in `ApplyThemeJson` / `ApplyThemeSelection`).
  - Hide trailing chevron in the baseline state, or remove it. Keep the icon-tile + label only by default.
  - Continue to hide `theme_summary_label_` until theme selection sets non-default text. Pointer must remain non-null (tests assert non-null repeatedly).
  - macOS theme icon: `paintbrush.fill` SF Symbol; fallback: `vector_icons::kPaletteIcon` (or closest available).

### 3.6 Bottom region (CTA stack)
- **Reference:** Bottom-anchored: full-width `Create Space` button (disabled until name is non-empty), centered `Cancel` text button, then 10-dot pagination strip with the rightmost dot active.
- **Current:** Already restructured: `bottom_spacer` flex View pushes `bottom_region` to the bottom. `Create Space` is `kProminent`, `kCornerRadius=16`, `min_size=46`. Cancel is centered `kText` button. Dots: 10 total, last active, sizes `kBottomDotSize=5` / `kBottomActiveDotSize=11`.
- **Deltas (visual rhythm only):**
  1. Vertical spacing between Create button → Cancel → dots may be slightly off; will be tuned in §3.7.
  2. Inactive dot color/size weight should be re-checked against the reference (currently `kColorSysSurfaceVariant`, may need to be more muted, e.g. `kColorSysOnSurfaceSubtle` at lower alpha).

### 3.7 Constants tuning (last-pass numbers)
After §3.1 – §3.6 structure is correct, tune these to match reference proportions:

| Constant | Current | Direction |
|---|---|---|
| `kFormCardHeight` | 56 | Likely keep; verify against reference |
| `kFormCardCornerRadius` | 16 | Verify; reference looks ~12–16 |
| `kFormCardIconTileSize` | 28 | Likely correct; verify glyph alignment |
| `kFormCardIconTileCornerRadius` | 10 | Verify |
| `kFormCardHorizontalPadding` | 14 | Verify |
| `kFormCardVerticalPadding` | 6 | Verify |
| `kFormCardContentSpacing` | 12 | Verify |
| `kProfilePillCornerRadius` | 16 | Likely keep |
| `kProfilePillHeight` | 32 | Verify (reference reads ~28–30) |
| `kProfilePillInsets` | `VH(0,14)` | Re-check after chevron is added |
| `kInputHeight` | 44 | Verify against reference name row |
| `kInputCornerRadius` | 12 | Likely keep |
| `kBottomDotSize` | 5 | Verify |
| `kBottomActiveDotSize` | 11 | Verify (reference active dot reads ~6–8 with brighter color, not necessarily ~2× larger) |
| `kBottomDotCount` | 10 | Reference has ~10; keep |
| `kContentInsets` | `TLBR(24,24,20,24)` | Verify side padding matches reference |
| `kBottomRegionInsets` | `TLBR(0,0,0,0)` (post-spacer fix) | Verify CTA-to-cancel and cancel-to-dots gaps |
| Bottom layout `between_child_spacing` | 8 | Verify |

This constants pass is **not** the whole change; it is the final tuning step after the structural deltas above are addressed.

### 3.8 Theming / `OnThemeChanged()`
- All new icon `ImageView`s and additional glyphs must use `kColorSys*` color IDs so `OnThemeChanged()` re-applies them automatically. No SK colors cached.
- For SF Symbols on macOS: `CreateMahoMacSFSymbol` already takes a color ID-equivalent; use `kColorSysOnSurfaceSubtle` for tile glyphs and `kColorSysOnSurface` for trailing info/chevron — match the rest of the sidebar.

### 3.9 Things the reference does NOT show (avoid feature creep)
- No avatar image in the profile pill (only `Default` label + chevron).
- No theme color dot in the closed state.
- No emoji/icon picker behavior on the dashed `+` placeholder (still non-functional).
- No tooltip/popover overlays in the baseline screenshot.
- These remain out of scope.

## 4. File-by-File Change List

### `maho_sidebar_create_space_view.cc` (only file with structural changes)
- Replace `CreateSpaceHeroIllustrationView::OnPaint` with a higher-fidelity laptop+plants vector paint. Keep class name + size + integration unchanged.
- Replace custom-painted `CreateSpaceGlyphView` usages for profile / theme / info with platform-appropriate `views::ImageView`s:
  - macOS: `CreateMahoMacSFSymbol(...)` returning `ui::ImageModel`
  - Other: `ui::ImageModel::FromVectorIcon(...)`
- Add a chevron glyph inside `profile_pill_` after `Default`.
- Hide `theme_preview_dot_` and trailing theme chevron in baseline state; show preview dot after theme selection.
- Adopt `views::CreatePaddedBorder` idiom for the name field, mirroring `maho_sidebar_search_view.cc`.
- Tune §3.7 constants only after the above are merged.

### `maho_sidebar_create_space_view.h`
- Possibly add nothing; if a chevron view inside the pill needs lifecycle tracking (theming), add `raw_ptr<views::ImageView> profile_pill_chevron_ = nullptr;`.
- Existing `_for_testing()` accessors must remain stable.

### `maho_sidebar_layout_tokens.h`
- Optional: lift `kFormCard*` and `kBottomDot*` into shared tokens **only if** they get reused outside this file. Otherwise keep them local.

### Test files
- **No structural assertions about title/subtitle text or theme summary text are expected to break**, since the reference text already matches and `theme_summary_label_` remains in the tree.
- If any test reads `theme_preview_dot_` visibility (it currently does not, per grep), update accordingly.

### `BUILD.gn`
- No new files. `CreateMahoMacSFSymbol` requires `//maho/browser:maho_mac_sf_symbol_helper` to already be in deps; verify and add if missing in `maho_sidebar_library` (it is currently there per `BUILD.gn`).

### Branding / assets
- No new image assets. All visuals remain custom-paint or SF-symbol/vector-icon sourced.

## 5. Ordered Execution Phases

Each phase ends with a build + a focused interactive test run.

### Phase 0 — Audit gates (read-only)
- 0A. Confirm theme picker test coverage. Re-run the prior audit summary in §6.
- 0B. Confirm `MahoSpaceCreateDialog` host status (still dead/no production callsite).
- 0C. Catalog every test accessor used and which assertions touch text vs. visibility vs. bounds.
- 0D. Spike: try the laptop+plants paint in a scratch repaint. If pure paint cannot reach reference fidelity, escalate to an SVG/PNG asset under `branding/` — but only after the spike fails.

### Phase 1 — Icon swap (no layout change)
- Replace profile / theme / info glyph tiles with SF-symbol/vector-icon `ImageView`s.
- Add the pill chevron after `Default`.
- Theme preview dot + trailing theme chevron set hidden in baseline.
- Build, run `MahoSidebarSpacesInteractiveTest.*`. **Gate G1:** all green; visual: row content reads cleaner.

### Phase 2 — Hero illustration replacement
- Replace `CreateSpaceHeroIllustrationView::OnPaint` body. Keep class name + integration.
- Build, run `MahoSidebarSpacesInteractiveTest.*` and `MahoSidebarContractTest.*`.
- **Gate G2:** tests green; visual: hero recognizable as laptop+plants.

### Phase 3 — Input treatment polish
- Switch name field to `CreatePaddedBorder` idiom. Verify placeholder color, caret color, focus ring on macOS.
- Build + tests. **Gate G3:** green.

### Phase 4 — Bottom rhythm + constants
- Tune §3.7 constants only. No structural changes.
- Build + tests. **Gate G4:** green.

### Phase 5 — Capture + diff
- Build the app: `python3 maho-chromium/build/scripts/build_maho.py --skip-rust`.
- Launch, navigate to `chrome://maho-test/?seed=2`, open Create Space.
- Capture screenshot, save under `.sisyphus/artifacts/create-space-parity/<date>.png`.
- Compare side-by-side with `maho/reference_screenshots/create-space.jpeg`.
- **Gate G5 (FINAL):** every element in §3 inventory is visually present and reads correctly.

## 6. Verification Gates

| Gate | Phase | What | How | Blocking? |
|------|-------|------|-----|-----------|
| G0-AUDIT | 0 | Test/runtime audit reconciled | Re-run §0A–0C | YES |
| G1-ICONS | 1 | Profile/theme/info glyphs use platform-native rendering | Build + `MahoSidebarSpacesInteractiveTest.*` | YES |
| G2-HERO | 2 | Hero illustration matches reference subject (laptop+plants) | Build + tests + visual | YES |
| G3-INPUT | 3 | Name field treatment matches reference | Build + tests + visual | YES |
| G4-RHYTHM | 4 | Bottom stack and constants tuned | Build + tests + visual | YES |
| G5-PARITY | 5 | Side-by-side capture matches reference | Manual capture + screenshot diff stored under `.sisyphus/artifacts/` | FINAL |

## 7. Constraints (non-negotiable)

- All five `_for_testing()` accessors on `MahoSidebarCreateSpaceView` remain valid and non-null after construction.
- `theme_summary_label_` remains in the view tree even when hidden; tests read its text.
- `theme_button_` continues to open `MahoSpaceConfigDialog::OpenForCreate()`. The dead in-file picker code is not modified.
- Existing footer → popover → New Space → inline surface flow remains the live entry path. No host-mode changes.
- `OnThemeChanged()` re-applies all new backgrounds/borders via color IDs, never cached SkColors.
- No WebUI changes. No GN cycle changes. No build-system surgery.

## 8. Risks

| Risk | Mitigation |
|---|---|
| Custom hero paint cannot reach reference fidelity | Phase 0D spike checks early; fallback path is to add an SVG/PNG asset under `branding/`. Treated as escalation, not default. |
| SF Symbols look different from vector_icons across platforms | Accept platform-native variance; macOS is the primary target since the reference is macOS. |
| Tuning constants introduce regressions in interactive tests that read bounds | Constants pass is last so it can be reverted independently. |
| Rebuild blocked by the unrelated GN cycle previously documented | Treat as separate task. If still blocking at G5, capture is deferred but plan is otherwise complete. |

## 9. Commit Strategy

Atomic, ordered for safe bisectability:

1. `feat(sidebar): swap create-space row glyphs to SF Symbols / vector icons` — Phase 1
2. `feat(sidebar): redraw create-space hero illustration toward reference` — Phase 2
3. `refactor(sidebar): align create-space name field with search input idiom` — Phase 3
4. `chore(sidebar): tune create-space form/dot constants to reference rhythm` — Phase 4
5. `docs(sidebar): record create-space parity capture artifact` — Phase 5

No commits without an explicit user request to commit; commits described here are the sequencing, not an instruction to push.

## 10. Out of Scope

- Real profile dropdown behavior (FFI work; tracked separately).
- Real icon picker for the dashed `+` placeholder (FFI signature change).
- WebUI variants of Create Space.
- Theme picker dialog parity (separate task; uses `MahoSpaceConfigDialog` already).
- Resolving the unrelated upstream GN dependency cycle.
