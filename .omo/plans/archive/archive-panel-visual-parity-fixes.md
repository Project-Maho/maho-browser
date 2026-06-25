# Archive Panel Visual Parity Fixes (Oracle-Revised)

## Context
After completing Phases 0-9 of the archive panel Swift parity work, a pixel-level audit revealed 12 remaining visual discrepancies between the C++ Views implementation and the Swift reference. All changes in the same two files. Build is currently green.

## Target Files
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.cc` (primary)
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_archive_view.h` (if header changes needed)

## Compile Check Command
```bash
PATH="chromium/src/third_party/depot_tools:$PATH" autoninja -C chromium/src/out/Default obj/chrome/browser/ui/ui/maho_sidebar_archive_view.o
```

## Fixes

### Group A: Constants & Spacing (trivial, no structural changes)

#### Fix 1: Panel horizontal inset
- **Current**: `kArchiveInsets = TLBR(0,0,0,0)`
- **Target**: `kArchiveInsets = TLBR(0,8,0,8)`
- **Swift ref**: `panelHorizontalInset: 8`

#### Fix 2: Header↔Body gap
- **Current**: Root layout spacing `8` in `BuildUi()`
- **Target**: Root layout spacing `1`
- **Swift ref**: `contentSpacing: 1`

#### Fix 3: Header bottom padding
- **Current**: header_surface_ padding `TLBR(8,8,8,8)` in `BuildUi()`
- **Target**: `TLBR(8,8,10,8)`
- **Swift ref**: `headerPadding = NSEdgeInsets(top: 8, left: 8, bottom: 10, right: 8)`

#### Fix 4: Scroll content inset
- **Current**: `kContentPadding = 8`
- **Target**: `kContentPadding = 6`
- **Swift ref**: `scrollContentInset: 6`

#### Fix 5: Section header vertical padding
- **Current**: `ArchiveSectionHeaderView` padding `TLBR(4,4,4,4)`
- **Target**: `TLBR(0,4,0,4)`
- **Swift ref**: `sectionHeaderVerticalPadding: 0`, `sectionHeaderLeadingInset: 4`, `sectionHeaderTrailingInset: 4`

#### Fix 6: Section header count opacity
- **Current**: count label `ui::kColorSysOnSurfaceSubtle` (same intensity as title)
- **Target**: Use `SkColorSetA` to apply ~0.32 alpha on `kColorSysOnSurfaceSubtle`
- **Implementation**: Override `OnThemeChanged` in `ArchiveSectionHeaderView`, resolve color with `GetColorProvider()`, apply `SkColorSetA(color, 82)` (82/255 ≈ 0.32)
- **Swift ref**: `secondaryText.withAlphaComponent(0.32)`

### Group B: Section header alignment

#### Fix 7: Section header baseline alignment
- **Current**: `CrossAxisAlignment::kCenter`
- **Target**: `CrossAxisAlignment::kStart` + add 2px top border to count label
- **Implementation**: `count_label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(2, 0, 0, 0)))` — approximates baseline alignment given font size delta (~11.5pt title vs ~9.5pt count)
- **Swift ref**: `.firstBaseline`

### Group C: Row divider (paint, no layout change)

#### Fix 8: Paint divider in ArchiveRowView::OnPaint
- **Current**: Separate View between rows in `RebuildList()`, height 1dp, border insets
- **Target** (Oracle-revised — paint instead of restructure):
  1. Save `text_container_` as member pointer (already created at line 171)
  2. Add `shows_divider_` bool member, default false
  3. Add `SetShowsDivider(bool)` method that sets flag + `SchedulePaint()`
  4. Override `OnPaint`: after `View::OnPaint(canvas)`, if `shows_divider_` is true, draw 1px line from `text_container_->x()` to `width() - kRowInsets.right()` at `height() - 1`
  5. Color: `SkColorSetA(GetColorProvider()->GetColor(ui::kColorSysSurfaceVariant), 0x6B)` (0x6B/255 ≈ 0.42 alpha)
  6. Guard with `GetColorProvider()` null check
  7. Remove divider View creation from `RebuildList()` section_container loop
  8. In `RebuildList()`, call `row->SetShowsDivider(tab_idx < section.tabs.size() - 1)` after creating each row
- **Why**: Zero layout changes — horizontal BoxLayout stays untouched. Divider is decorative, not structural.
- **Swift ref**: height 0.5dp, leading from textStackView, trailing from backgroundView - 12

### Group D: Row idle border

#### Fix 9: Real border with SkColorSetA in idle state
- **Current**: `CreateEmptyBorder(1)` — invisible spacer
- **Target**: In `UpdateAppearance()` idle branch:
  ```cpp
  auto* cp = GetColorProvider();
  if (cp) {
    SkColor border_color = SkColorSetA(
        cp->GetColor(ui::kColorSysSurfaceVariant), 10);  // 10/255 ≈ 0.04 alpha
    SetBorder(views::CreateRoundedRectBorder(1, kRowCornerRadius, border_color));
  } else {
    SetBorder(views::CreateEmptyBorder(1));
  }
  ```
- **Note**: `CreateRoundedRectBorder` has an `SkColor` overload. Hover/focus branches keep using `ColorId` overload as-is.
- **Swift ref**: `archiveControlBorder.withAlphaComponent(0.04)`

### Group E: Icons

#### Fix 10: Restore glyph icon
- **Current**: `vector_icons::kUndoIcon` (circular undo ↺)
- **Target**: `vector_icons::kArrowBackIcon` (left-pointing arrow ←, 16x16 canvas, clean geometry)
- **Rationale**: At 9x9 render size, visual difference between ← and ↖ is minimal. `kBackArrowIcon` is heavier (toolbar-style, 32x32) — too thick at 9px.
- **Swift ref**: `arrow.up.left` with `pointSize: 9, weight: .semibold`

#### Fix 11: Empty state icon — keep kFilterIcon (conscious divergence)
- **Current**: `vector_icons::kFilterIcon`
- **Target**: **Keep as-is**. No suitable archivebox equivalent exists in Chromium's vector icon set. Add `// TODO(maho): Replace with archivebox icon when custom .icon file is added` comment.
- **Swift ref**: `archivebox` with `pointSize: 13, weight: .medium`

### Group F: Empty state layout restructure

#### Fix 12: Empty state — full-width silhouette with stack inside
- **Current**: Fixed 120x100 card, icon inside card, title/subtitle outside below card
- **Target**:
  1. Remove outer BoxLayout that holds card + title + subtitle
  2. Card: change `SetPreferredSize(gfx::Size(120, 100))` → `SetPreferredSize(gfx::Size(0, 100))` — parent's `kStretch` handles width
  3. Add `gfx::Insets::VH(0, 16)` as insets on `ArchiveEmptyStateView` layout for 16px side margins
  4. Move `title_label_` and `subtitle_label_` INSIDE the card (add as children of card, not of root)
  5. Card internal layout: vertical BoxLayout, spacing 4, `kCenter` main axis, `kCenter` cross axis
  6. Subtitle: `SetMultiLine(true)`, `SizeToFit(220)` for max width with word wrap
  7. ArchiveEmptyStateView itself just contains the card, centered vertically
- **Swift ref**: silhouette stretches to fill width (16px margin each side), stack (icon + title + subtitle) is inside, subtitle max width 220

## Execution Order
1. Group A (Fixes 1-6): All constant/spacing tweaks — one pass
2. Group B (Fix 7): Section header alignment — quick
3. Group C (Fix 8): Divider painted in OnPaint — no layout risk
4. Group D (Fix 9): Idle border with SkColorSetA — simple
5. Group E (Fixes 10-11): Icon swap + TODO comment
6. Group F (Fix 12): Empty state restructure
7. Compile check after each group
8. Final full build (`autoninja -C chromium/src/out/Default chrome`)

## Constraints
- Do NOT create new files or modify BUILD.gn
- Do NOT use type-safety suppression
- All changes in maho_sidebar_archive_view.cc/h only
- Sub-pixel heights (0.5dp) not supported in Chromium Views — use 1px with alpha
- Guard all `SkColorSetA` calls with `GetColorProvider()` null check (null before widget attachment)
- Need `#include "third_party/skia/include/core/SkColor.h"` for SkColorSetA (verify if already transitively included)
- Need `#include "ui/gfx/canvas.h"` for OnPaint override (verify if already included)
- Need `#include "cc/paint/paint_flags.h"` for divider painting
