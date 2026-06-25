# Space Switcher Unicode Icon Remediation Plan

**Created:** 2026-05-24
**Status:** Draft, code-grounded against current tree
**Scope:** Native sidebar footer icon rendering for the space switcher (`MahoSidebarSpaceDotView`), plus the consistency surface in `MahoSidebarCreateSpaceView`.
**Relationship to other plans:** Complements, does not replace, `.sisyphus/plans/space-switcher-fix-plan-2026-05-23.md` and `.sisyphus/plans/space-switcher-revised-plan-2026-05-24.md`. Those plans cover bridge unification, dot sizing, and shortcut delivery. This plan is narrowly the "gray dot instead of glyph" defect.

---

## 1. Objective

Make the sidebar space switcher render a space's Unicode icon as the resting visual whenever an icon is set on that space. Today the user sees a small gray dot for every space, even on spaces where an icon was assigned. The icon only flashes in on hover/active. That is wrong. The dot is the fallback, not the primary state.

This plan does not touch backend default icon seeding. Spaces created without an icon stay icon-less for now and continue to fall back to the dot. Whether to seed a default glyph at creation time is treated as a deliberate follow-up decision after the rendering fix lands.

---

## 2. Code-Grounded Diagnosis

The defect spans Rust core, the C++ adapter, the footer view, and the dot view. The visible symptom comes from the dot view; the upstream layers are listed only to explain why a "fix" higher in the stack would not solve the rendering problem.

### 2.1 Core data path: icons stay `None` unless explicitly set

`maho/crates/maho-core/src/space_manager.rs` constructs new and default spaces with `icon: None` at every creation site (lines 52, 101, 519, 560, 598, 721, 969, 1638). The struct→view-model conversion at line 1419 propagates the optional through to the view-model `icon: space.icon.clone()`. The only place an icon becomes non-empty is the `set_space_icon` mutation path around line 1658. Implication: for a newly created space, the FFI payload will surface either an absent or empty `icon` field until the user picks one.

This is **not** the bug we are fixing here. It is the reason the symptom is universal for fresh installs and for spaces created via the popover. Discussed again in section 3.

### 2.2 Adapter: icon string is read verbatim into `space_icons`

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc` parses each space's `icon` field from the JSON payload returned by `maho_core_get_space_view_models()` and pushes it into `state.space_icons` (line 145), defaulting to an empty string when the field is missing. `BuildFooterModel` later assigns `model.space_icons = core_state.space_icons` (line 1086). No transformation, no fallback substitution. An empty string at this layer means the footer will receive an empty string.

### 2.3 Footer: passes the icon straight into the dot view

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc::RebuildDots` (lines 494-544) picks the per-index icon and calls `dot->Configure(sid, name, icon, dot_color, active)` (line 529). No coercion, no synthesis. The dot view is the last hop.

### 2.4 Dot view: icon hidden at rest, gray dot painted as the visible fallback

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc` is where the user-visible defect lives:

- `Configure(...)` sets `has_icon_ = !icon.empty()` and writes the glyph into `icon_label_` (lines 72-81).
- `UpdateAppearance()` defines `const bool show_icon = (is_hovered_ || is_active_) && has_icon_;` (line 231), then sets `dot_opacity_ = show_icon ? 0.0f : 1.0f;` and animates `icon_label_->layer()` opacity to either 1 or 0.
- `OnPaint(...)` paints the dot whenever `dot_opacity_ > 0`. The fill color is hardcoded:
  - active: `SkColorSetRGB(0xE8, 0xEF, 0xFB)` at alpha `dot_opacity_ * 212`,
  - inactive: `cp->GetColor(ui::kColorSysOnSurfaceSubtle)` (subtle gray) at alpha `dot_opacity_ * 72`.

So when the tab is at rest and not active, the icon layer is at opacity 0 and the fallback dot is drawn at full opacity in subtle gray. That is precisely the "gray dot" the user reports.

Even when the space *does* have an icon, the dot view treats hover/active as the gating condition for showing it. Identity information is hidden behind a hover state.

### 2.5 `icon_label_` font path

`icon_label_` is a plain `views::Label` configured for center alignment, painted to its own layer, subpixel rendering disabled (lines 36-47). There is no explicit font list assignment. On macOS the default font list includes Apple Color Emoji as a fallback, so most Unicode emoji render. For non-emoji symbol glyphs (e.g. miscellaneous symbols, math operators, dingbats outside the emoji block) the platform's symbol font fallback is what determines whether we get a glyph or a tofu box. We treat this as a known limitation, not the primary defect, and call it out under risks.

### 2.6 Create-space icon preview (consistency surface)

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc` renders the icon preview in `SpaceIconPickerButton` using a plain `views::Label` set to `STYLE_BODY_3_MEDIUM`, sized 24x24, with `+` as the placeholder (lines 555-586). Whatever font / sizing decisions we make for the footer renderer should match here so the icon a user picks in the picker is the icon they see in the footer.

---

## 3. Recommended UX Decision

**An icon, once assigned to a space, is the primary visual at rest.** Hover and active are emphasis states, not visibility gates.

Concretely:

- Space has an icon, inactive, not hovered: **icon shown** (no fallback dot).
- Space has an icon, inactive, hovered: icon shown, optionally with subtle emphasis (size / opacity).
- Space has an icon, active, not hovered: icon shown, with the existing active emphasis (halo, color tier).
- Space has an icon, active, hovered: same as above.
- Space has no icon (any state): keep the current dot fallback. No change in this plan.

This makes the dot purely a fallback affordance for icon-less spaces. It also keeps the active dot's identity visible without requiring the user to hover.

Backend default icon seeding (giving fresh spaces a glyph at creation time so the dot fallback is never the user's first impression) is a separate decision. It is intentionally deferred to a follow-up so this plan stays narrow.

---

## 4. Scope

### In scope

- Inverting the icon visibility predicate in `MahoSidebarSpaceDotView` so a present icon renders at rest.
- Suppressing the gray fallback dot when an icon is shown for that space.
- Verifying the create-space picker preview stays visually coherent with the new footer renderer (font / sizing only; no behavior change).
- Documenting the font-fallback caveat for non-emoji glyphs without changing the font path.

### Out of scope

- Backend default icon seeding in `space_manager.rs`. Treated as a deliberate follow-up.
- Dot size constant changes. Already covered by `space-switcher-revised-plan-2026-05-24.md` Phase B.
- Bridge / writer / reader convergence. Already landed; covered by the revised plan's section 3.
- Keyboard shortcut delivery for `next_space` / `prev_space` / `select_space_N`. Covered by the revised plan's Phase C / D.
- Theme color tweaks for the dot fill. Out of scope; file separately if contrast issues surface.
- Any WebUI surface. The space switcher is native Views.

---

## 5. Phased Implementation Plan

### Phase 1: Make icons render at rest in the footer dot view

Single-file change in `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc`.

1. Change the icon visibility predicate in `UpdateAppearance()` from `(is_hovered_ || is_active_) && has_icon_` to `has_icon_`. The icon is shown whenever the space has one. Hover and active no longer gate visibility.
2. Adjust `dot_opacity_` so that when `has_icon_` is true, the fallback dot is fully suppressed. When `has_icon_` is false, keep the current dot behavior unchanged. This is the change that removes the gray-dot symptom.
3. Leave the active halo (`SkColorSetARGB(0x12, 0xFF, 0xFF, 0xFF)` stroke at radius `+0.8f`) intact. Active emphasis stays; it just no longer depends on the dot being visible.
4. Leave hover handling for icon-less spaces untouched (the `kDotSizeHoveredNoIcon` branch in `GetCurrentDotSize`).

Done when, in code review:

- A dot configured with a non-empty icon paints the glyph at rest with no underlying fill dot.
- A dot configured with an empty icon paints exactly as it does today.
- No new state members on the view; no new animations.

### Phase 2: Verify the create-space preview stays coherent

Read-only verification, no code change unless a delta is found.

1. Confirm `SpaceIconPickerButton` in `maho_sidebar_create_space_view.cc` renders the picked glyph at the same effective size as the footer dot's `icon_label_`. Today the picker is 24x24 with `STYLE_BODY_3_MEDIUM`; the footer's `icon_label_` is bounded by `kIndicatorSize` inside `kViewSize`. If the visual size is materially different at 1x and 2x, file a follow-up; do not adjust as part of this plan.
2. Confirm both surfaces use a plain `views::Label` so font fallback behavior matches. They do today. No change required.

Done when: a one-paragraph note in the verification log confirms picker preview and footer glyph match in size and font fallback, or identifies a specific follow-up.

### Phase 3 (deliberately deferred, not part of this plan)

Default icon seeding in `space_manager.rs` at the seven `icon: None` creation sites. This is the natural follow-up. It is *not* part of this plan because:

- It changes user-visible defaults across the entire core, including migration of existing on-disk spaces that already have `None`.
- It needs a product decision on what the default glyph should be (one universal default? per-template default? skip and prompt at creation?).
- The rendering fix is correct on its own and unblocks the user-visible symptom for any space that does have an icon assigned, including all spaces created via the picker today.

Re-open this once Phase 1 lands and the team has decided on the default-glyph policy.

---

## 6. File-by-File Planned Changes

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc`

- `UpdateAppearance()` (around line 230):
  - Replace `const bool show_icon = (is_hovered_ || is_active_) && has_icon_;` with `const bool show_icon = has_icon_;`.
  - Keep `target_icon_opacity = show_icon ? 1.0f : 0.0f;`.
  - Keep `dot_opacity_ = show_icon ? 0.0f : 1.0f;`. With the new predicate, this naturally suppresses the fallback dot whenever an icon is present.
- No other functions need to change.
- No header changes; no new state, no new constants.

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h`

- No change expected. If the implementation cleanup wants to drop `is_hovered_` references that are no longer load-bearing for icon visibility, leave them in place; they still drive `GetCurrentDotSize` for icon-less spaces.

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`

- No change. `RebuildDots` already passes through the icon string verbatim.

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

- No change. The adapter already preserves the icon string and treats absent as empty.

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.cc`

- No change planned. Verified for visual consistency with the footer renderer in Phase 2.

### `maho/crates/maho-core/src/space_manager.rs`

- **No change in this plan.** Default icon seeding is the deliberate follow-up.

---

## 7. Verification Plan

This plan does not claim any builds or tests have been run. The following is what should be done when the implementation lands, not a record of what has happened.

### Static checks

- `lsp_diagnostics` clean on `maho_sidebar_space_dot_view.{cc,h}` after the predicate change.
- No new warnings in the surrounding files.

### Build

- `python3 maho-chromium/build/scripts/build_maho.py` from repo root succeeds.

### Manual QA

1. Create a space and assign a Unicode icon via the picker. Confirm the footer dot for that space shows the glyph at rest, not a gray dot.
2. Hover that space. The glyph stays visible. No gray fallback bleeds through.
3. Activate that space. The glyph stays visible. The active halo is present.
4. Create a second space and do not assign an icon. Confirm the gray dot fallback still appears for that space at rest, hover, and active. (Hover-on-icon-less should keep its current size behavior via `kDotSizeHoveredNoIcon`.)
5. Switch between an icon-having space and an icon-less space. Confirm the transition is visually clean: the glyph appears or disappears, the dot appears or disappears, no double-rendering.
6. Spot check at 1x and 2x.
7. Spot check across light, dark, and themed spaces. Glyph contrast is the user's responsibility for now via the picker; we do not change icon coloring.
8. Compare the picker preview glyph against the footer glyph for the same space. Sizes and shapes should read as the same character at the same weight.

### Regression coverage

- Re-run any existing interactive tests under `browser/ui/views/sidebar/maho_sidebar_*_test.cc` that exercise the footer or the dot view. Add a new test only if an existing test would have caught this regression and currently does not. A reasonable candidate is a unit test on `MahoSidebarSpaceDotView` that asserts the icon label opacity goes to 1 immediately after `Configure(...)` with a non-empty icon, with no hover or active flag set.

---

## 8. Risks and Open Questions

### R1. Glyph rendering for non-emoji code points

`icon_label_` uses the default `views::Label` font list. On macOS this generally resolves emoji via Apple Color Emoji and symbol glyphs via the platform's symbol fonts, but we have not enumerated which characters the picker actually permits. If the picker allows characters that no fallback font in the build resolves, the user will see tofu boxes instead of the glyph after this plan lands, where today they see a gray dot. That is arguably worse for those specific code points. Mitigation: keep the picker's allowed character set narrow (already the case in practice), and file a follow-up to give `icon_label_` an explicit emoji-capable font list if a character ever fails.

### R2. Inactive icon contrast against the sidebar surface

Today the gray dot at low alpha is intentionally low-contrast. A full-opacity glyph at rest may read louder than the dot used to, particularly for inactive spaces in busy footers. We do not change icon coloring in this plan. If contrast becomes a real complaint, file a separate plan to add an icon-color tier (subtle for inactive, regular for hovered/active) without re-introducing visibility gating.

### R3. Active emphasis becomes harder to see

With every icon-having space showing its glyph at rest, the active marker has to do more work. Today the active marker is a slightly larger, more opaque dot plus a halo, but the dot was the load-bearing affordance. After this fix, active emphasis comes from the halo plus the size tier from `GetCurrentDotSize`. That is fine in principle but should be eyeballed during Phase 1 QA. If active is unreadable, the right fix is in `space-switcher-revised-plan-2026-05-24.md` Phase B (size tier change), not here.

### R4. Spaces with no icon become visually inconsistent with spaces that do

A footer row mixing icon-having and icon-less spaces will show glyphs interleaved with gray dots. That is intended given the chosen UX rule, and it is also the strongest argument for the deferred default-icon-seeding follow-up. Flag for product if it lands as ugly in practice; the fix is upstream, not here.

### R5. The bug also affects WebUI surfaces

This plan is native-only. If any WebUI sidebar mock or developer test surface mirrors the same icon-vs-dot logic, that surface will continue to render a dot at rest. We have not audited WebUI surfaces in this plan. Out of scope; file separately if it matters.

### Open question: should Phase 3 default-icon seeding be unconditional or per-template?

Not answered here. Phase 1 lands without taking a position. Re-open after the rendering fix is in.

---

## 9. Notes

- This plan intentionally treats backend default icon seeding as a follow-up decision, not a prerequisite. The first required fix is to make already-present icons render correctly in the native footer.
- This plan does not edit GN, accelerator infrastructure, the bridge, or any WebUI surface.
- Tone of this plan is "narrow remediation, not redesign". Anything beyond the predicate flip and the consistency check belongs in a different plan.
