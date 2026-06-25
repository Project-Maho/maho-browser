# Sidebar Parity v2 Recovery + Newly-Found Gaps Plan — 2026-06-08

**Status:** Draft (awaiting user approval, then Momus review)
**Owner:** Sisyphus
**Scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` (primary) + newly-found token gaps across header
**Supersedes for tab_list_view.cc parity:** `.sisyphus/plans/sidebar-typography-icon-zen-parity-plan-2026-06-07.md` (the v2 plan; was incorrectly reported as 100% implemented)
**Reference:** `zen-browser/desktop @ 965bb9391`

---

## 0. Context — Honest Postmortem

After an audit triggered by the user's question "진짜 zen parity 인거 맞아?" (is this really Zen parity?), git forensics revealed:

1. **Parity v2 changes for `tab_list_view.cc` were NEVER written to disk** in this repository:
   - `git log --all --reflog -S "kTabFaviconCornerRadiusDp"` → 0 hits
   - `git log --all --reflog -S "kWorkspaceIndicatorHeightDp"` → 0 hits
   - `git log --all --reflog -S "using sidebar_layout::kRowHeight"` → 0 hits
   - `git log --grep "parity"` → 0 hits
   - 10 dangling commits inspected → none have parity v2 markers
   - `stash@{0}` → unrelated AGENTS.md changes

2. **Parity v2 was applied to OTHER sidebar files** (top_bar_view, footer_view, search_view, favorites_grid_view, folder_hover_popup_view) and the `layout_tokens.h` header was updated with new constants.

3. **`tab_list_view.cc` working tree changes are 100% flicker-fix scope** (B/C/D items from `sidebar-pinned-drop-zone-flicker-2026-06-07.md`): `last_painted_*` caching, `is_pinned_drop_lane_already_revealed_callback_`, `SetDragActive(true/false)`. Zero parity v2 changes.

4. **Result**: The header has new tokens (40dp, 28dp, 20dp, 14dp, 22dp/1dp, 16dp, 4dp, etc.) but `tab_list_view.cc` has anonymous-namespace `constexpr int` declarations that **shadow** the header tokens. C++ name lookup picks the inner-scope local, so the header values are dead code.

5. **My earlier "100% Zen parity / ship-ready" code review was a hallucinated false positive.** I conflated the parity v2 plan document's "should change X to Y" prose with actual diff content. This plan starts from honest reality: tab_list_view.cc has NEVER had parity v2 applied.

This plan corrects course by:
- Writing exact code changes for `tab_list_view.cc`
- Including newly-found gaps not in the original v2 plan (`--tab-block-margin`, `--tab-icon-end-margin`, OS-conditional radius)
- **Mandating git-verified checks** in every verification step so a false-positive review cannot recur

---

## 1. Goal

Actually apply parity v2 to `tab_list_view.cc` and close all currently-known parity gaps. After this plan executes:

1. `tab_list_view.cc` references the header tokens (no anonymous-namespace shadows)
2. Tab title bumps to STYLE_BODY_3 baseline + macOS Derive(+5)
3. Active tab weight is NORMAL (no MEDIUM divergence)
4. Audio indicator uses `kAudioIndicatorSizeDp` (16dp)
5. Folder name uses SEMIBOLD (600)
6. Section separator uses 22dp/1dp (matching Zen)
7. SidebarSpaceHeaderRow renders at 44dp with proper icon view, STYLE_BODY_3 + SEMIBOLD name, actions_button, and `SetSubpixelRenderingEnabled(false)` on both labels
8. Tab favicon clips to 4dp radius
9. Inter-tab vertical gap is 2dp (`--tab-block-margin`)
10. Icon-to-text spacing is 8dp (closest integer to Zen 8.5px)
11. Tab/section corner radius is OS-conditional (4dp Win, 6dp default, 8dp macOS)

**Definition of Done**:
- Every changed code path verified by `grep` / `git diff` actually reflects the intended value (not just plan prose)
- Build green via `python3 maho-chromium/build/scripts/build_maho.py --skip-rust`
- All sidebar tests pass: `out/Default/unit_tests --gtest_filter="SidebarDnDDispatchTest.*:MahoSidebarDragImageControllerTest.*"`
- Visual QA on macOS Retina: tab row 40dp, folder row 44dp, workspace indicator 48dp, etc. measured with screen ruler

**Non-goals**:
- Re-litigating prior parity decisions (settled in v2 plan)
- Floating-mode search pill (Zen `1.5em`/21px) — separate decision; current Maho lacks floating mode
- Compact mode behavior
- Touch min-height variant
- Maho-unique surfaces (Library Rail, Action Popover, Archive empty state)

---

## 2. Scope of Files Touched

| File | Phase(s) | Why |
|---|---|---|
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | 1, 2, 3, 4, 5, 6 | Primary recovery target — every parity v2 change |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h` | 7 | Newly-found gaps: `kRowSpacingDp = 2`, `kRowIconTextSpacingDp = 8`, OS-conditional radius |

**Files NOT touched** (already parity-complete per audit):
- `maho_sidebar_top_bar_view.cc`
- `maho_sidebar_footer_view.cc`
- `maho_sidebar_search_view.cc` (post-recent-regression-fix)
- `maho_sidebar_favorites_grid_view.cc`
- `maho_sidebar_folder_hover_popup_view.cc`

---

## 3. Phase Breakdown

### Phase 0 — Header Token Pre-Additions (Foundation Prerequisite)

**Why this exists**: Phase 1 imports `sidebar_layout::kRowSpacingDp` and `sidebar_layout::kRowIconTextSpacingDp` via `using` declarations. Both tokens are currently **missing** from `layout_tokens.h`. Without Phase 0, Phase 1 will fail to compile with "no member named 'kRowSpacingDp' in namespace 'sidebar_layout'".

This phase adds **only the two missing tokens** to the header. The rest of Phase 7's header work (OS-conditional radius branches) stays in Phase 7 where they don't block earlier phases.

**Action**: Add two `inline constexpr int` declarations to `maho_sidebar_layout_tokens.h`:

```cpp
// Anywhere in the maho::sidebar_layout namespace, near the other tab tokens:
inline constexpr int kRowSpacingDp = 2;            // Zen: --tab-block-margin: 2px
inline constexpr int kRowIconTextSpacingDp = 8;    // Zen: --tab-icon-end-margin: 8.5px (rounded down to integer)
```

**Verification**:
```bash
grep -nE "inline constexpr int (kRowSpacingDp|kRowIconTextSpacingDp)" \
  maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h
```
Must return exactly **2 hits**.

```bash
# Sanity: full pre-flight check that Phase 1 will need
grep -E "inline constexpr int (kRowSpacingDp|kRowHeightDp|kRowCornerRadiusDp|kRowIconSizeDp|kCloseButtonSizeDp|kCloseIconSizeDp|kRowIconTextSpacingDp|kFolderIndentDp|kFolderRowHeightDp|kSectionSeparatorHeightDp|kSectionSeparatorLineHeightDp|kFolderGlyphSizeDp|kFolderGlyphContainerSizeDp|kChevronSizeDp|kTabRowLineHeightDp|kTabFaviconCornerRadiusDp|kAudioIndicatorSizeDp)" \
  maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h | wc -l
```
Must return **17**.

**Effort**: 5 min (two-line header edit + grep check).

---

### Phase 1 — Remove Local Shadow Constants (Foundation)

**Why first**: Every other phase depends on the header tokens being actually used. As long as the local `constexpr int` block exists, Phase 2-6 changes will be silently overridden.

**Action**: Delete the anonymous-namespace local constants (lines ~101-122 of `maho_sidebar_tab_list_view.cc`) and add `using sidebar_layout::` declarations.

**Before** (current state):
```cpp
constexpr int kRowSpacingDp = 0;
constexpr int kRowHeightDp = 32;
constexpr int kRowCornerRadiusDp = 6;
constexpr int kRowIconSizeDp = 16;
constexpr int kCloseButtonSizeDp = 24;
constexpr int kCloseIconSizeDp = 14;
constexpr int kRowIconTextSpacingDp = 6;
constexpr int kRowTrailingSpacingDp = 0;
constexpr int kSectionSpacingDp = 4;
constexpr int kFolderIndentDp = 13;
constexpr int kFolderRowHeightDp = 33;
constexpr int kSectionSeparatorHeightDp = 8;
constexpr int kSectionSeparatorLineHeightDp = 2;
constexpr int kFolderGlyphSizeDp = 13;
constexpr int kFolderGlyphContainerSizeDp = 16;
constexpr int kChevronSizeDp = 12;
```

**After**:
```cpp
using sidebar_layout::kRowSpacingDp;
using sidebar_layout::kRowHeightDp;
using sidebar_layout::kRowCornerRadiusDp;
using sidebar_layout::kRowIconSizeDp;
using sidebar_layout::kCloseButtonSizeDp;
using sidebar_layout::kCloseIconSizeDp;
using sidebar_layout::kRowIconTextSpacingDp;
using sidebar_layout::kFolderIndentDp;
using sidebar_layout::kFolderRowHeightDp;
using sidebar_layout::kSectionSeparatorHeightDp;
using sidebar_layout::kSectionSeparatorLineHeightDp;
using sidebar_layout::kFolderGlyphSizeDp;
using sidebar_layout::kFolderGlyphContainerSizeDp;
using sidebar_layout::kChevronSizeDp;
using sidebar_layout::kTabRowLineHeightDp;
using sidebar_layout::kTabFaviconCornerRadiusDp;
using sidebar_layout::kAudioIndicatorSizeDp;
constexpr int kRowTrailingSpacingDp = 0;  // Maho-unique, not in header
constexpr int kSectionSpacingDp = 4;       // Maho-unique, not in header
```

**Pre-flight check before Phase 1**:
```bash
# Verify header has every token we plan to import
grep -E "inline constexpr int (kRowSpacingDp|kRowHeightDp|kRowCornerRadiusDp|kRowIconSizeDp|kCloseButtonSizeDp|kCloseIconSizeDp|kRowIconTextSpacingDp|kFolderIndentDp|kFolderRowHeightDp|kSectionSeparatorHeightDp|kSectionSeparatorLineHeightDp|kFolderGlyphSizeDp|kFolderGlyphContainerSizeDp|kChevronSizeDp|kTabRowLineHeightDp|kTabFaviconCornerRadiusDp|kAudioIndicatorSizeDp)" maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h
```
Must return 17 hits. If any token is missing, add to header before proceeding.

**Verification (phase exit)**:
```bash
# Local constexpr declarations must be GONE for the imported names
grep -nE "^constexpr int (kRowSpacingDp|kRowHeightDp|kRowCornerRadiusDp|kRowIconSizeDp|kCloseButtonSizeDp|kCloseIconSizeDp|kRowIconTextSpacingDp|kFolderIndentDp|kFolderRowHeightDp|kSectionSeparatorHeightDp|kSectionSeparatorLineHeightDp|kFolderGlyphSizeDp|kFolderGlyphContainerSizeDp|kChevronSizeDp)" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 0 hits.

```bash
grep -nE "^using sidebar_layout::(kRowHeightDp|kFolderRowHeightDp|kAudioIndicatorSizeDp|kTabFaviconCornerRadiusDp)" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return ≥ 4 hits (one for each spot-checked token).

Build must compile (`python3 maho-chromium/build/scripts/build_maho.py --skip-rust` exit 0).

**Effort**: 30 min (delete + replace + compile verify).

---

### Phase 2 — Tab Title Font + Active Weight Removal

**Action 2A — Tab title font on macOS (`SidebarTabRowView::SidebarTabRowView()`, around line 1180-1204)**:

Currently `title_button_` is created via `AddLabelButton()` with no explicit font setup. On macOS this resolves to LabelButton's default (12pt). Need to add macOS Derive(+5) to reach 17pt (Zen `1.25rem ≈ 17.5px`).

After `title_button_ = static_cast<views::LabelButton*>(...AddLabelButton(...));`, add:
```cpp
#if BUILDFLAG(IS_MAC)
  title_button_->label()->SetFontList(title_button_->label()->font_list().Derive(
      5, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
  title_button_->label()->SetLineHeight(30);
#else
  title_button_->label()->SetTextStyle(views::style::STYLE_BODY_3);
  title_button_->label()->SetLineHeight(kTabRowLineHeightDp);
#endif
```

**Action 2B — SidebarInlineRow tab title (around line 793)**:

Currently uses `STYLE_BODY_4` baseline and `STYLE_BODY_4_MEDIUM` on active. Change baseline to BODY_3:

```cpp
// In SidebarInlineRow ctor:
#if BUILDFLAG(IS_MAC)
  label()->SetFontList(label()->font_list().Derive(
      5, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
  label()->SetLineHeight(30);
#else
  label()->SetTextStyle(views::style::STYLE_BODY_3);
  label()->SetLineHeight(kTabRowLineHeightDp);
#endif
```

**Action 2C — Remove STYLE_BODY_4_MEDIUM in `UpdateAppearance()`**:

Remove lines 806 and 816-817 that switch text style based on `active_` / `subtle_`:
```cpp
// DELETE these lines:
label()->SetTextStyle(views::style::STYLE_BODY_4_MEDIUM);
label()->SetTextStyle(subtle_ ? views::style::STYLE_BODY_4
                              : views::style::STYLE_BODY_4_MEDIUM);
```

The font is now set ONCE in the constructor and never changes. Active state is conveyed by background/border only.

**Verification**:
```bash
# Tab title macOS Derive(+5) MUST exist
grep -nE "Derive\(\s*5\s*,\s*gfx::Font::NORMAL" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 2+ hits (one for SidebarTabRowView title_button_, one for SidebarInlineRow label()).

```bash
# STYLE_BODY_4_MEDIUM must be removed from UpdateAppearance
grep -n "STYLE_BODY_4_MEDIUM" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 0 hits (or only inside SidebarSpaceHeaderRow, which Phase 5 will fix).

```bash
# STYLE_BODY_3 baseline applied
grep -nE "SetTextStyle\(views::style::STYLE_BODY_3\)" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 2+ hits (non-mac branches for both classes).

**Effort**: 45 min.

---

### Phase 3 — Audio Indicator Size

**Action**: Replace hardcoded `12` at lines 1218, 1221, 1225, 1228 with `kAudioIndicatorSizeDp` (which the header defines as 16).

Before:
```cpp
audio_indicator_view_->SetImage(ui::ImageModel::FromVectorIcon(
    vector_icons::kVolumeOffChromeRefreshIcon, ui::kColorSysOnSurfaceSubtle, 12));
audio_indicator_view_->SetImageSize(gfx::Size(12, 12));
```

After:
```cpp
audio_indicator_view_->SetImage(ui::ImageModel::FromVectorIcon(
    vector_icons::kVolumeOffChromeRefreshIcon, ui::kColorSysOnSurfaceSubtle,
    kAudioIndicatorSizeDp));
audio_indicator_view_->SetImageSize(gfx::Size(kAudioIndicatorSizeDp, kAudioIndicatorSizeDp));
```

**Verification**:
```bash
# Hardcoded "12" for audio indicator must be gone
grep -nE "audio_indicator_view_.*(12)|gfx::Size\(12,\s*12\)" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 0 hits.

```bash
# kAudioIndicatorSizeDp usage
grep -nE "kAudioIndicatorSizeDp" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 4+ hits (image + size for muted + audible).

**Effort**: 10 min.

---

### Phase 4 — Folder Name SEMIBOLD Weight

**Action**: After `label_button_->SetTextSubpixelRenderingEnabled(false);` in `SidebarFolderRowView::SidebarFolderRowView()` (around line 1853), add:

```cpp
label_button_->label()->SetFontList(
    label_button_->label()->font_list().Derive(
        0, gfx::Font::NORMAL, gfx::Font::Weight::SEMIBOLD));
```

**Verification**:
```bash
# Folder SEMIBOLD applied in SidebarFolderRowView (between line 1830 and 1900)
sed -n '1830,1900p' maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc | grep -E "SEMIBOLD|Weight::SEMIBOLD"
```
Must return at least one hit.

**Effort**: 15 min.

---

### Phase 5 — Workspace Indicator (`SidebarSpaceHeaderRow`) Restructure

This is the biggest change in scope. Current state of `SidebarSpaceHeaderRow` (around line 2284-2308):
- No `SetPreferredSize()` (height not enforced at 44dp)
- `icon_label_` uses raw `Derive(4, NORMAL, NORMAL)` (acceptable but not token-driven)
- `name_label_` uses `STYLE_BODY_4_MEDIUM` (wrong — should be BODY_3 + SEMIBOLD)
- No `SetSubpixelRenderingEnabled(false)` on either label (causes stencil rendering)
- No `actions_button_` (Zen has trailing actions button)

**Action**: Rewrite the constructor:

```cpp
SidebarSpaceHeaderRow() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(4, 12, 4, 12),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetPreferredSize(gfx::Size(0, sidebar_layout::kWorkspaceIndicatorHeightDp));

  icon_label_ = AddChildView(std::make_unique<views::Label>());
  icon_label_->SetFontList(
      views::Label::GetDefaultFontList().Derive(
          sidebar_layout::kWorkspaceIndicatorEmojiFontSizeDp -
              views::Label::GetDefaultFontList().GetFontSize(),
          gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
  icon_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  icon_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
  icon_label_->SetSubpixelRenderingEnabled(false);
  icon_label_->SetVisible(false);

  name_label_ = AddChildView(std::make_unique<views::Label>());
  name_label_->SetTextStyle(views::style::STYLE_BODY_3);
  name_label_->SetFontList(name_label_->font_list().Derive(
      0, gfx::Font::NORMAL, gfx::Font::Weight::SEMIBOLD));
  name_label_->SetSubpixelRenderingEnabled(false);
  name_label_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  name_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  name_label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);
  layout->SetFlexForView(name_label_, 1);

  actions_button_ = AddChildView(views::CreateVectorImageButtonWithNativeTheme(
      views::Button::PressedCallback(),
      maho_lucide_icons::kSlidersHorizontalIcon,
      14, ui::kColorSysOnSurfaceSubtle, ui::kColorSysOnSurface,
      ui::kColorSysOnSurface));
  actions_button_->SetBorder(nullptr);
  actions_button_->SetPreferredSize(gfx::Size(26, 26));
  actions_button_->SetTooltipText(u"Space actions");
  actions_button_->SetAccessibleName(u"Space actions");

  SetBackground(nullptr);
  SetBorder(nullptr);
}
```

Add field:
```cpp
private:
  raw_ptr<views::Label> icon_label_ = nullptr;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::Button> actions_button_ = nullptr;  // NEW
```

**Notes**:
- `actions_button_` callback is `PressedCallback()` (no-op) for now. Wiring the actual context menu is a P2 follow-up.
- `kWorkspaceIndicatorHeightDp` and `kWorkspaceIndicatorEmojiFontSizeDp` already exist in `layout_tokens.h`.
- The 44dp tall row + 26×26 actions button + 16dp icon all match Zen `--zen-workspace-indicator-height: 44px`.

**Verification**:
```bash
# Indicator height token applied
grep -n "SetPreferredSize.*kWorkspaceIndicatorHeightDp" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 1 hit.

```bash
# name_label uses BODY_3 + SEMIBOLD
sed -n '/class SidebarSpaceHeaderRow/,/^};/p' maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc | grep -E "STYLE_BODY_3|SEMIBOLD"
```
Must return 2 hits (BODY_3 + SEMIBOLD).

```bash
# Subpixel rendering disabled on both labels
sed -n '/class SidebarSpaceHeaderRow/,/^};/p' maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc | grep "SetSubpixelRenderingEnabled(false)"
```
Must return 2 hits.

```bash
# actions_button_ field added
sed -n '/class SidebarSpaceHeaderRow/,/^};/p' maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc | grep "actions_button_"
```
Must return ≥ 3 hits (declaration + AddChildView + SetPreferredSize).

**Effort**: 60 min.

---

### Phase 6 — Tab Favicon Corner Radius (4dp)

**Action**: After `favicon_view_->SetImageSize(...)` in `SidebarTabRowView::SidebarTabRowView()` (around line 1175), add a clip path:

```cpp
favicon_view_->SetClipPath(SkPath::RRect(
    SkRRect::MakeRectXY(SkRect::MakeWH(kRowIconSizeDp, kRowIconSizeDp),
                        kTabFaviconCornerRadiusDp,
                        kTabFaviconCornerRadiusDp)));
```

**Required includes** at top of `tab_list_view.cc`:
```cpp
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
```

**Verification**:
```bash
grep -n "SetClipPath" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 1 hit.

```bash
grep -n "kTabFaviconCornerRadiusDp" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 1 hit (the SetClipPath callsite).

```bash
grep -nE "include.*Sk(Path|RRect).h" maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 2 hits.

**Caveat**: `views::ImageView::SetClipPath` accepts an `SkPath`. The clip is applied at paint time. Note that on layer-backed views, `SetPaintToLayer()` may also be needed for correct clipping. If visual QA shows favicon corners NOT rounded, add `favicon_view_->SetPaintToLayer(); favicon_view_->layer()->SetFillsBoundsOpaquely(false);` before the SetClipPath call.

**Effort**: 30 min (incl. visual QA verify).

---

### Phase 7 — Newly-Found Gap Wiring + OS-Conditional Radius

> **Note**: The two header token *additions* (`kRowSpacingDp`, `kRowIconTextSpacingDp`) were moved to **Phase 0** because Phase 1 depends on them. This phase covers the remaining work — wiring `kRowSpacingDp` into `BoxLayout` and adding OS-conditional radius branches.

**Action 7A — Wire `kRowSpacingDp` into BoxLayout for tab list**:

In `tab_list_view.cc`, locate the `BoxLayout` instantiation in `MahoSidebarTabListView::Init()` (or wherever the tab list container is built). Pass `kRowSpacingDp` as the `between_child_spacing` argument:

```cpp
auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
    views::BoxLayout::Orientation::kVertical,
    gfx::Insets(),
    kRowSpacingDp));   // ← was 0 or unspecified before
```

**Action 7B — OS-conditional tab corner radius**:

Replace the single `kRowCornerRadiusDp = 6` definition in `layout_tokens.h` with a BUILDFLAG branch:

```cpp
#if BUILDFLAG(IS_WIN)
inline constexpr int kRowCornerRadiusDp = 4;   // Zen: --tab-border-radius: 4px (Win)
#elif BUILDFLAG(IS_MAC)
inline constexpr int kRowCornerRadiusDp = 8;   // Zen: --tab-border-radius: 8px (macOS)
#else
inline constexpr int kRowCornerRadiusDp = 6;   // Zen: --tab-border-radius: 6px (default)
#endif
```

**Action 7C — OS-conditional medium radius (folder popup, search pill section, etc.)**:

Replace single `kSectionCornerRadiusDp = 12` with:

```cpp
#if BUILDFLAG(IS_WIN)
inline constexpr int kSectionCornerRadiusDp = 6;   // Zen: --border-radius-medium: 6px (Win)
#elif BUILDFLAG(IS_MAC)
inline constexpr int kSectionCornerRadiusDp = 12;  // Zen: --border-radius-medium: 12px (macOS)
#else
inline constexpr int kSectionCornerRadiusDp = 10;  // Zen: --border-radius-medium: 10px (default)
#endif
```

**Verification**:
```bash
# 7A: BoxLayout uses kRowSpacingDp
grep -nE "BoxLayout.*kRowSpacingDp|kRowSpacingDp.*spacing" \
  maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return ≥ 1 hit.

```bash
# 7B/7C: Header has BUILDFLAG branches for both radius tokens
grep -B1 -A4 "kRowCornerRadiusDp = " \
  maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h
grep -B1 -A4 "kSectionCornerRadiusDp = " \
  maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h
```
Both must show `#if BUILDFLAG(IS_WIN)` / `#elif BUILDFLAG(IS_MAC)` / `#else` branches with values 4/8/6 and 6/12/10 respectively.

```bash
# Sanity: no hardcoded `6` for row corner radius in tab_list_view.cc
grep -nE "RoundedRectBackground\(.*,\s*6\)|RoundedRectBorder\(.*,\s*6," \
  maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc
```
Must return 0 hits (all callsites already use the token name per Momus audit; this is a safety check).

**Effort**: 30 min (smaller than original Phase 7 because token additions moved to Phase 0).

**Risk**: OS-conditional radius means existing visual snapshot tests (if any) may now produce per-OS variants. Phase 8 verification will catch this.

---

### Phase 8 — Build + Test + Visual QA

**Action**:
1. Build: `python3 maho-chromium/build/scripts/build_maho.py --skip-rust`
2. Build unit tests: `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests`
3. Run existing sidebar tests:
   ```bash
   chromium/src/out/Default/unit_tests --gtest_filter="SidebarDnDDispatchTest.*:MahoSidebarDragImageControllerTest.*"
   ```
4. Launch Maho.app and visually verify:
   - Tab row 36dp height (Linux/Win) or 40dp (macOS)
   - Tab title 13pt (Linux/Win) or 17pt (macOS) — measure cap-height with screen ruler
   - Active tab has same font weight as inactive (no bolder)
   - Folder row 40dp height (44dp macOS), 28dp glyph container, 20dp icon, SEMIBOLD name
   - Workspace indicator 44dp tall, icon + name + actions sliders icon visible
   - Audio indicator on muted/audible tabs is 16dp
   - Tab favicon has visible 4dp rounded corners
   - 2dp gap between tab rows
   - Tab corner radius 8dp on macOS, 4dp on Win, 6dp elsewhere

**Visual QA checklist file**: capture screenshots at default 288dp width, side-by-side with Zen reference. Save to `plans/assets/parity-recovery-2026-06-08/`.

**Effort**: 60 min.

---

## 4. Sequencing & Throughput

Strict serial execution: **Phase 0 → 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8**. 

- **Phase 0** is the new prerequisite that adds the two missing header tokens before Phase 1's `using` declarations need them.
- **Phase 1** is the foundation for all consumer-side changes; all subsequent phases depend on the header tokens actually winning over local shadows.

Parallelization not advisable due to single-file overlap risk in `tab_list_view.cc`.

**Total honest estimate**: 5 + 30 + 45 + 10 + 15 + 60 + 30 + 30 + 60 = **~5 hours focused work**.

---

## 5. Verification Matrix (with anti-hallucination guards)

**Every phase MUST pass these checks AT THE COMMAND LINE (not visual inspection alone):**

| Phase | Command | Expected output |
|---|---|---|
| 0 | `grep -cE "inline constexpr int (kRowSpacingDp\|kRowIconTextSpacingDp)" layout_tokens.h` | 2 |
| 1 | `grep -c "^constexpr int k.*Dp = " tab_list_view.cc` | ≤ 2 (only kRowTrailingSpacingDp + kSectionSpacingDp remain) |
| 1 | `grep -c "^using sidebar_layout::" tab_list_view.cc` | ≥ 14 |
| 2 | `grep -c "Derive(\s*5\s*," tab_list_view.cc` | ≥ 2 |
| 2 | `grep -c "STYLE_BODY_4_MEDIUM" tab_list_view.cc` | 0 |
| 3 | `grep -c "kAudioIndicatorSizeDp" tab_list_view.cc` | ≥ 4 |
| 4 | `sed -n '1830,1900p' tab_list_view.cc \| grep -c SEMIBOLD` | ≥ 1 |
| 5 | `sed -n '/class SidebarSpaceHeaderRow/,/^};/p' tab_list_view.cc \| grep -c SetSubpixelRenderingEnabled` | ≥ 2 |
| 5 | `grep -c "kWorkspaceIndicatorHeightDp" tab_list_view.cc` | ≥ 1 |
| 6 | `grep -c "SetClipPath" tab_list_view.cc` | ≥ 1 |
| 7 | `grep -cE "BoxLayout.*kRowSpacingDp\|kRowSpacingDp.*spacing" tab_list_view.cc` | ≥ 1 |
| 7 | `grep -B1 -A4 "kRowCornerRadiusDp = " layout_tokens.h \| grep -c BUILDFLAG` | ≥ 2 |
| 7 | `grep -B1 -A4 "kSectionCornerRadiusDp = " layout_tokens.h \| grep -c BUILDFLAG` | ≥ 2 |
| 8 | Build exit code | 0 |
| 8 | Test exit code | 0 |

**If any check fails, the phase is NOT complete.** No "I think it's done" hand-waves. The grep must return the expected count.

This is the explicit guard against the hallucinated-code-review failure mode that produced the original "100% parity" false positive.

---

## 6. Risk Register

| ID | Risk | Likelihood | Mitigation |
|---|---|---|---|
| R1 | `using sidebar_layout::` declarations conflict with another `using` block in the file | Low | Search for existing `using` blocks; merge into single anonymous namespace |
| R2 | OS-conditional radius (Phase 7C) breaks tests that hardcode 6dp expectations | Medium | After Phase 7, run all sidebar tests; update hardcoded test expectations to use token |
| R3 | Phase 5 SidebarSpaceHeaderRow rewrite breaks `Configure()` callers if signature changes | Low | Configure() signature unchanged; only constructor body changes. Test by running `unit_tests` filter |
| R4 | Tab title macOS Derive(+5) on `title_button_->label()` may return empty FontList if LabelButton hasn't initialized label | Medium | Add `DCHECK(title_button_->label()->font_list().GetFontSize() > 0)` for diagnostic; verify visually |
| R5 | Adding `SetClipPath` requires layer-backed view, may need `SetPaintToLayer()` | Medium | If favicon corners not visibly rounded after build, add layer setup; documented in Phase 6 |
| R6 | `kRowSpacingDp = 2` Phase 7A increases sidebar height by 2px × N tabs; may break window-height assumptions | Low | At 700px window with 10 tabs: extra 20dp; still fits comfortably |
| R7 | Phase 1's `using` declarations compile but old token name still resolves to local — silent shadow | Critical | Phase 1 verification REQUIRES `grep ^constexpr int` returning ≤ 2; explicit count check catches this |
| R8 | Visual QA can't distinguish 17pt vs 17.5px (0.5pt difference) | Acceptable | Document as approximation; ≤1px tolerance is design-acceptable |
| R9 | Phase 2C removes active tab MEDIUM weight — UX regression | Documented | Per parity v2 plan §7 Q3, this is the correct Zen-parity choice; if user complains, revisit as separate ticket |
| R10 | Search pill compounding bug recurrence — similar pattern lurking elsewhere | Already mitigated | Audit complete; no other compounding patterns found in non-constructor paths (per oracle audit `bg_42bceeab` Section 4) |

---

## 7. Out-of-Scope (explicitly deferred)

- **Floating-mode search pill (Zen `1.5em` = 21px)**: Maho lacks floating mode; no UI surface to apply it on
- **Compact mode** (`--zen-toolbar-height: 34px`): Separate behavior, not in current Maho
- **Touch min-height variant** (`41px`): Out of scope per AGENTS.md
- **`--zen-toolbox-padding` collapsed=6/expanded=5**: Maho's collapsed mode is at 40dp width vs Zen 60dp — different UX model
- **Pinned-tabs-container-separator label** (`10px font, 500 weight, 10px icon`): Add as separate plan if user wants a "Pinned" header label
- **Workspace actions_button menu wiring**: Phase 5 adds the button as UI-only stub; menu wiring tracked separately

---

## 8. References

### Audit data (proves the current gap)
- `bg_0ac0fdfd` (explore agent, Section 1: 13 of 20 claimed-fixed items broken)
- `bg_42bceeab` (oracle, Section A: tab title macOS is actually 12pt, not 17pt)
- Git forensics confirmed: 0 commits with parity v2 markers in `tab_list_view.cc`

### Predecessor plan
- `.sisyphus/plans/sidebar-typography-icon-zen-parity-plan-2026-06-07.md` — has the design intent; `tab_list_view.cc` portion was never executed

### Companion plans (already implemented for other files)
- `.sisyphus/plans/sidebar-pinned-drop-zone-flicker-2026-06-07.md` — flicker fix B/C/D items (these IS in tab_list_view.cc)
- `.sisyphus/plans/sidebar-scope-creep-followups-and-retroactive-2026-06-07.md` — scope-creep follow-ups
- `.sisyphus/plans/favorites-dominant-color-and-roving-focus-RETROACTIVE-2026-06-07.md` — orphan F docs
- `.sisyphus/plans/sidebar-search-pill-compact-variant-removal-RETROACTIVE-2026-06-07.md` — orphan G docs

### Zen reference
- `zen-browser/desktop @ 965bb9391`
  - `src/zen/tabs/zen-tabs/vertical-tabs.css`
  - `src/zen/folders/zen-folders.css`
  - `src/zen/spaces/zen-workspaces.css`
  - `src/zen/common/styles/zen-theme.css`

### Files touched by this plan
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` (primary)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h` (Phase 7 only)

---

## 9. Approval Gate

This plan should be reviewed by Momus with focus on:
- Whether the verification matrix (§5) is sufficient to prevent recurrence of the hallucinated review
- Whether OS-conditional radius (Phase 7C) callsite enumeration is complete
- Whether Phase 5 SidebarSpaceHeaderRow restructure is too large for a single phase

```
task(subagent_type="Momus - Plan Critic", prompt=".sisyphus/plans/sidebar-parity-v2-recovery-and-newly-found-gaps-2026-06-08.md", load_skills=[], run_in_background=false)
```

After Momus approval, execute Phase 0 → 8 strictly serially. Verify each phase with the grep checks BEFORE moving to the next. **Do not declare any phase complete based on plan prose alone — only on grep evidence.**

---

## 10. Key Behavioral Commitment

**No "100% parity" or "ship-ready" claim will be made** until:
1. Every grep check in §5 passes
2. Build is green
3. Tests pass
4. Visual QA confirms expected sizes

If any check fails or is not actually performed, the report will state the actual state with grep-cited evidence, not optimistic prose.

This is the explicit lesson learned from the hallucinated 2026-06-07 code review.
