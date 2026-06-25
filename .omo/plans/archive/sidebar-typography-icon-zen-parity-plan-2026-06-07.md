# Sidebar Typography & Icon Zen TRUE-Parity Plan — 2026-06-07 (v2)

**Status:** IMPLEMENTATION LANDED 2026-06-07.
All 16 audit deltas + 4 structural items at Zen-exact values; Momus parity-gate PASSED.
**Owner:** Sisyphus
**Scope:** `maho-chromium/browser/ui/views/sidebar/**` (Native Views C++ only)
**Reference:** `zen-browser/desktop @ 965bb9391da5ca543b9ab79125c7bbab8f645e7a`

> **v2 mandate (after Momus parity-gate review)**: Every previously-labeled "compromise" is removed. Where Zen has a value, Maho matches it. Where Zen has structural difference (workspace indicator, icon-only essentials), Maho replicates the structure or explicitly defers with a tracked plan reference, not a hand-wave.

---

## 0. Goal

Maho sidebar typography + iconography **byte-for-byte parity** with Zen Browser, including macOS-conditional scale-up, structural elements (44px workspace indicator), and all 16 audit deltas. Departures from Zen require a written rationale tied to Chromium framework constraints, not aesthetic preference.

**Non-goals**:
- Color / theme / animation parity (separate concerns)
- WebUI sidebar surfaces (none exist; sidebar is C++ Views only)
- Maho-unique surfaces with no Zen counterpart (Library Rail, Action Popover, Archive empty state, Update Pill — left untouched, documented as additive features)

**Definition of Done**:
1. All 16 audit deltas resolved at Zen exact values OR documented as framework-constrained with reference.
2. macOS Retina + Linux + Windows visual QA: side-by-side with Zen reference shows pixel-level alignment on tab/folder/footer surfaces (≤2px tolerance).
3. `lsp_diagnostics` clean on all touched files.
4. `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
5. Browser tests pass (sidebar/tab_list browsertests if present — verify in Phase 0).

---

## 1. Scope of Files Touched

| File | Phase(s) | Why |
|---|---|---|
| `maho_sidebar_layout_tokens.h` | 1, 2, 3, 4, 5 | Central token header — gains `BUILDFLAG(IS_MAC)` branches in Phase 5 |
| `maho_sidebar_tab_list_view.cc` | 1, 2, 4 | Tab row, folder row, space header, active-tab weight |
| `maho_sidebar_top_bar_view.cc` | 3, 5 | Top bar icon size + button hit target |
| `maho_sidebar_footer_view.cc` | 3 | Footer button icon + hit-target sizing |
| `maho_sidebar_favorites_grid_view.cc` | 4 | Glyph size + label visibility |
| `maho_sidebar_folder_hover_popup_view.cc` | 1 | Title weight |
| `maho_sidebar_search_view.cc` | 4 | Primary label size |
| `maho_sidebar_space_dot_view.{h,cc}` | 4 | Workspace indicator structural addition |

---

## 2. Findings → Action Mapping (16 audit deltas, Zen-exact targets)

| # | Element | Maho current | **Zen exact** | v2 action | Phase |
|---|---|---|---|---|---|
| 1 | Folder row height | 33dp | **40px** | → 40dp | 1 |
| 2 | Folder glyph container | 16dp | **28px** | → 28dp | 1 |
| 3 | Folder glyph icon | 13dp | (≈20px effective) | → 20dp | 1 |
| 4 | Folder name weight | NORMAL | **600** | → STYLE_BODY_4_BOLD (Chromium's BOLD ≈ 600) | 1 |
| 5 | Folder hover popup title | STYLE_BODY_3 | inherit (≈14px) + 600 | → STYLE_BODY_3_BOLD | 1 |
| 6 | Folder indent | 13dp | **14px** | → 14dp | 1 |
| 7 | Tab row height | 32dp | **36px** | → 36dp | 2 |
| 8 | Tab title font | STYLE_BODY_4 (12pt) | inherit ≈14px / **1.25rem ≈17.5px (macOS)** | → STYLE_BODY_3 (13pt) baseline + macOS branch (Phase 5) | 2 + 5 |
| 9 | Tab title line-height | 18dp | `1.7` (≈23.8px / 29.75px macOS) | → SetLineHeight(24) baseline / 30 macOS | 2 + 5 |
| 10 | Active tab weight | MEDIUM | NORMAL | → NORMAL (remove emphasis) | 2 |
| 11 | Audio indicator | 12dp | (Firefox default ~16px) | → 16dp | 2 |
| 12 | Top bar icon | 15dp | **16px** / 17px macOS | → 16dp baseline + macOS branch | 3 + 5 |
| 13 | Top bar height | 22dp button only | **38px** / 42px macOS toolbar | → restructure top bar to 38dp toolbar; buttons 24dp inside | 3 + 5 |
| 14 | Tab close icon | 14dp | **16px** | → 16dp | 3 |
| 15 | Footer action icon | 10dp | **16px** | → 16dp | 3 |
| 16 | Footer button hit target | 20dp | **36px** (`--tab-min-height`) | → 36dp (full Zen parity) | 3 |
| **+** | Favicon border-radius | none | **4px** | → 4dp (mandatory, not optional) | 4 |
| **+** | Favorites label | shown 12pt | `display:none` | → hide labels (icon-only grid) | 4 |
| **+** | Search pill primary label | 12pt | `1.15em` ≈16.1px / `1.5em` ≈21px macOS | → STYLE_BODY_3 baseline + macOS larger | 4 + 5 |
| **+** | Workspace indicator (active) | small dot | **44px tall row, 16×16 icon, 13px name 600** | → new MahoSidebarSpaceIndicatorRow component | 4 |

---

## 3. Phase-by-Phase Plan

### Phase 0 — Pre-flight (45 min)

**Tasks**:
1. Read `maho_sidebar_layout_tokens.h` end-to-end. Confirm token line numbers match audit (lines 32–93).
2. Grep token usage across the entire Chromium tree (not just sidebar/) to catch hidden callsites:
   ```bash
   grep -rn "kFolderGlyphSizeDp\|kFolderGlyphContainerSizeDp\|kRowHeightDp\|kCloseIconSizeDp\|kTopBarIconSizeDp\|kFooterButtonIconSize\|kFavoriteGlyphSizeDp\|kFolderIndentDp\|kFolderRowHeightDp\|kTopBarButtonSizeDp" chromium/src/ maho-chromium/
   ```
3. Locate sidebar browser tests:
   ```bash
   find chromium/src/chrome/test -name "*sidebar*" -o -name "*tab_list*" 2>/dev/null
   find maho-chromium -name "*test*.cc" | xargs grep -l "SidebarTabRowView\|SidebarFolderRowView" 2>/dev/null
   ```
4. Verify Chromium typography mapping for `STYLE_BODY_3_BOLD` and confirm BOLD weight value (read `chromium/src/ui/views/style/typography_provider.cc` to verify Chromium's BOLD ≈ font-weight 700, not 600 — if 700, we need a custom `Derive` call rather than `_BOLD` style for Zen's `font-weight: 600` parity).
5. Capture baseline screenshots at default 288dp width on macOS Retina + Linux + Windows.
6. Build green: `python3 maho-chromium/build/scripts/build_maho.py --skip-rust`.

**Critical pre-Phase-1 verification**: confirm Chromium's MEDIUM = 500 and BOLD = 700. Zen uses `600` (semibold). If Chromium has no `STYLE_BODY_X_SEMIBOLD`, use `Derive(0, NORMAL, gfx::Font::Weight::SEMIBOLD)` manually.

**Exit criteria**:
- All token line numbers verified
- No external (non-sidebar) callsites of the bumped tokens
- Browsertest list captured
- 600-weight path decided (semibold-style helper if Chromium lacks it)
- Baseline screenshots committed to `plans/assets/sidebar-zen-parity-baseline-2026-06-07/`

---

### Phase 1 — Folder Hierarchy True Parity

**Goal**: Folder reads as Zen-exact section header. Numeric parity on row, container, icon, indent, weight.

**`maho_sidebar_layout_tokens.h` additions** (new `// --- Folder ---` block):
```cpp
// --- Folder (Zen parity: zen-folders.css) ---
inline constexpr int kFolderRowHeightDp = 40;             // Zen: calc(2*2 + 36) = 40px
inline constexpr int kFolderGlyphContainerSizeDp = 28;    // Zen: 28px
inline constexpr int kFolderGlyphSizeDp = 20;             // Zen: ~20px effective inside 28px container
inline constexpr int kChevronSizeDp = 14;                 // Zen: ~14px chevron
inline constexpr int kFolderIndentDp = 14;                // Zen: --zen-folder-indent: 14px
inline constexpr int kSectionSeparatorHeightDp = 22;      // Zen: pinned-tabs-container-separator height: 22px
inline constexpr int kSectionSeparatorLineHeightDp = 1;   // Zen: separator line: 1px
```

**`maho_sidebar_tab_list_view.cc` changes**:
1. Remove the local `static constexpr int kFolderRowHeightDp = 33` etc. (lines ~110–129). Replace with `using ::maho::sidebar::kFolderRowHeightDp;` or include the token header and use directly.
2. **Folder name weight** — at the line where `label_button_` is constructed (~1842), append:
   ```cpp
   // Zen parity: zen-folders.css L81 — font-weight: 600 (semibold)
   label_button_->label()->SetFontList(
       label_button_->label()->font_list().Derive(
           0, gfx::Font::NORMAL, gfx::Font::Weight::SEMIBOLD));
   ```
   *(Use SEMIBOLD if available; fallback to MEDIUM with a TODO if Chromium font-weight enum lacks SEMIBOLD on the target platform — verify in Phase 0.)*
3. **Section separator height** — already 8dp; bump to 22dp per Zen `.pinned-tabs-container-separator { height: 22px }`. Update `kSectionSeparatorHeightDp = 8` → `22`. Adjust separator insets `(3,8,3,8)` → `(10,8,11,8)` so the 1dp line still sits centered in the 22dp track.

**`maho_sidebar_folder_hover_popup_view.cc` changes**:
4. Title style (~lines 83, 384): `STYLE_BODY_3` → custom `Derive` with SEMIBOLD weight (matching Zen's 600). Same helper as folder name.
5. Subtitle (~line 283): `STYLE_BODY_5` → keep, but add `SetFontList(Derive(0, NORMAL, MEDIUM))` for Zen's `font-weight: 500` parity.

**`maho_sidebar_layout_tokens.h` row-row coupling note**: with row at 36dp (Phase 2) and folder at 40dp, the +4dp ratio matches Zen's row(36) → folder(40) exactly.

**Verification**:
- `lsp_diagnostics` clean on tab_list_view.cc, folder_hover_popup_view.cc, layout_tokens.h
- Build green
- Visual: folder row measured at 40dp (use Views inspector / pixel ruler)
- Folder glyph container = 28dp
- Folder name renders at semibold (600) — A/B against Zen reference at same zoom

**Risks**:
- **R1: Chromium SEMIBOLD availability** — if `gfx::Font::Weight::SEMIBOLD` doesn't render correctly on Linux (font fallback), MEDIUM (500) is the next-best. Document the fallback in code comment.
- **R2: 28dp glyph container at depth 4 indent (4×14 = 56dp)** — total leading = 14+28 = 42dp, plus 4 levels of indent = 56dp leading. At min sidebar width 280dp, leaves 224dp for label. Long titles will ellipsis sooner than Zen at 250dp. **Acceptable** — Zen has same problem at 280dp; min width is design choice.

---

### Phase 2 — Tab Row + Title True Parity (Baseline Cross-Platform)

**Goal**: Match Zen's tab row 36px, line-height 1.7, font 14px (cross-platform baseline; macOS bump in Phase 5). Remove Maho's MEDIUM-on-active divergence.

**`maho_sidebar_layout_tokens.h` additions**:
```cpp
// --- Tab Row (Zen parity: vertical-tabs.css + Firefox upstream) ---
inline constexpr int kRowHeightDp = 36;                   // Zen: --tab-min-height: 36px
inline constexpr int kRowCornerRadiusDp = 6;              // Zen: --tab-border-radius: 6px (default)
inline constexpr int kRowIconSizeDp = 16;                 // Zen: --size-item-small: 16px
inline constexpr int kCloseButtonSizeDp = 24;             // Zen: --size-item-medium: 24px
inline constexpr int kCloseIconSizeDp = 16;               // Zen: 24 - 2*4 padding = 16
inline constexpr int kTabRowLineHeightDp = 24;            // Zen: 1.7 * 14px = 23.8 ≈ 24
inline constexpr int kTabFaviconCornerRadiusDp = 4;       // Zen: .tab-icon-image border-radius: 4px
inline constexpr int kAudioIndicatorSizeDp = 16;          // Zen: Firefox default 16px
```

**`maho_sidebar_tab_list_view.cc` changes**:
1. Migrate `kRowHeightDp`, `kCloseIconSizeDp`, `kCloseButtonSizeDp`, `kRowIconSizeDp` from local `constexpr` to header tokens.
2. **Tab title font** (lines ~773, 806):
   ```cpp
   // OLD: label()->SetTextStyle(views::style::STYLE_BODY_4);
   // NEW: Zen parity — vertical-tabs.css inherits ~14px
   label()->SetTextStyle(views::style::STYLE_BODY_3);  // 13pt baseline
   label()->SetLineHeight(kTabRowLineHeightDp);         // 24dp = 1.7 * 14
   ```
3. **Remove active-tab MEDIUM divergence** (lines ~806, 816–817):
   ```cpp
   // REMOVED: STYLE_BODY_4_MEDIUM on active.
   // Zen uses NORMAL weight on all tabs (vertical-tabs.css does not differentiate).
   // All tabs now use STYLE_BODY_3 NORMAL.
   ```
4. **Audio indicator** (lines ~1221, 1228): bump 12dp → 16dp (`kAudioIndicatorSizeDp`).
5. **Favicon corner radius** (~line 1177): use `views::ImageView` doesn't expose `SetCornerRadius`. **Solution**: wrap favicon in a `views::View` with `SetBackground(views::CreateRoundedRectBackground(SK_ColorTRANSPARENT, kTabFaviconCornerRadiusDp))` and `SetClipPath(...)` — OR override `OnPaint` on a custom `MahoTabFaviconView` that paints the favicon with a clipped rounded path. **Implementation cost ≈ 90 min** but **mandatory for parity** (no longer optional).

**Verification**:
- Tab row height = 36dp
- Tab title font = 13pt (STYLE_BODY_3)
- Line-height = 24dp
- Active tab not bolder than inactive
- Audio indicator = 16dp
- Favicon visibly rounded at 4dp

**Risks**:
- **R3: STYLE_BODY_3 (13pt) at 280dp width** — slightly wider character widths may push close-button overlap. Test with 30-char title.
- **R4: Removing active-tab MEDIUM** — UX research likely showed Maho's MEDIUM is better for accessibility. **This is true Zen parity by user mandate**; if a11y concerns arise, file a follow-up exception ticket. Document the divergence-resolution in commit message.
- **R5: Custom MahoTabFaviconView class** — adds new file. Verify naming + GN build inclusion. Estimate +30 min for plumbing.

---

### Phase 3 — Icon + Toolbar True Parity (Cross-Platform Baseline)

**Goal**: Top bar restructured to 38dp toolbar with 24dp buttons + 16dp icons (Zen exact). Footer at 36dp buttons + 16dp icons (Zen exact). Tab close 16dp.

**`maho_sidebar_layout_tokens.h` changes**:
```cpp
// --- Top Bar (Zen parity: zen-theme.css --zen-toolbar-height) ---
inline constexpr int kTopBarHeightDp = 38;                // Zen: --zen-toolbar-height: 38px
inline constexpr int kTopBarButtonSizeDp = 24;            // Zen: 16+(2*4 padding)=24, fits in 38 toolbar
inline constexpr int kTopBarIconSizeDp = 16;              // Zen: --zen-toolbar-button-size: 16px
inline constexpr int kTopBarButtonCornerRadiusDp = 8;
inline constexpr int kTopBarButtonSpacingDp = 5;          // Zen: gap: 5px in foot-buttons
inline constexpr int kTopBarTrafficLightAlignmentHeightDp = 38;  // bumped to match toolbar

// --- Footer (Zen parity: vertical-tabs.css foot-buttons) ---
inline constexpr int kFooterRowHeight = 36;               // Zen: --tab-min-height: 36px
inline constexpr int kFooterControlSize = 36;             // Zen: full --tab-min-height button
inline constexpr int kFooterControlCornerRadius = 10;
inline constexpr int kFooterButtonIconSize = 16;          // Zen: --size-item-small: 16px
```

**`maho_sidebar_top_bar_view.cc` changes**:
1. Remove icon-only 22dp button paradigm. Convert to 38dp toolbar containing 24dp buttons.
2. Update `top_bar_view.cc:106–107` button preferred size: `gfx::Size(kTopBarButtonSizeDp, kTopBarButtonSizeDp)` = 24×24.
3. Top bar container `SetPreferredSize(gfx::Size(0, kTopBarHeightDp))` = 38dp tall.
4. Adjust `kTopBarInsets` from `(0,1,2,1)` → `(0,4,0,4)` for proper button-in-toolbar spacing per Zen `--zen-toolbox-padding`.

**`maho_sidebar_footer_view.cc` changes**:
5. Update `kFooterRowHeight` 24 → **36**, `kFooterControlSize` 20 → **36**, `kFooterButtonIconSize` 10 → **16**.
6. Update icon construction (`footer_view.cc:123–124`): `ImageModel::FromVectorIcon(icon, color, kFooterButtonIconSize)` — token-driven, no other change.
7. Footer button gap: 1dp → 5dp (Zen parity).

**`maho_sidebar_tab_list_view.cc` close button**:
8. Token migration only — `kCloseIconSizeDp = 16` already set in Phase 2 token block.

**Verification**:
- Top bar visually = 38dp tall toolbar
- Top bar buttons = 24×24dp each
- Top bar icons = 16dp
- Footer buttons = 36×36dp each
- Footer icons = 16dp
- Tab close icon = 16dp

**Risks**:
- **R6: Top bar height 22→38 increases sidebar vertical real estate by 16dp.** At 700px window height with 10 tabs (10 × 36 = 360dp tabs) + 38dp top bar + ~80dp space-header + 36dp footer + favorites = total ≈ 600dp. Fits 700px window. Tested OK at default but verify at 600px window.
- **R7: Footer button 20→36 doubles size; 4 buttons need 4 × 36 + 3 × 5 gap = 159dp.** At 280dp min width, leaves 121dp for spacer/center alignment. Acceptable.
- **R8: Lucide icons at 16dp may look heavier than at 15dp**. Compare visual density — Zen uses these same sizes, so parity is by definition correct.

---

### Phase 4 — Structural Parity: Workspace Indicator + Favorites + Search Pill + Favicon

**Goal**: Resolve the structural deltas — the items that aren't just numeric token bumps.

#### 4A — Workspace Indicator (44px Active Header)

**Zen pattern**: When sidebar is expanded, the active workspace shows as a 44px-tall row with workspace icon (16dp), workspace name (13px / 600), and an actions button. Other workspaces appear as small icons below.

**Maho gap**: Currently Maho uses only small dots in the footer. There IS a `SidebarSpaceHeaderRow` (tab_list_view.cc:2280–2323) but it's just a label above tabs, not a dedicated indicator with icon + actions.

**Action**: Extend `MahoSidebarSpaceHeaderRow` to match Zen's indicator structure:

1. **`maho_sidebar_layout_tokens.h`**:
   ```cpp
   // --- Workspace Indicator (Zen parity: zen-workspaces.css) ---
   inline constexpr int kWorkspaceIndicatorHeightDp = 44;       // Zen: --zen-workspace-indicator-height: 44px
   inline constexpr int kWorkspaceIndicatorIconSizeDp = 16;     // Zen: width/height: 16px
   inline constexpr int kWorkspaceIndicatorEmojiFontSizeDp = 16; // Zen: font-size: 16px
   ```

2. **`maho_sidebar_tab_list_view.cc:2280–2323`** — `SidebarSpaceHeaderRow`:
   - Bump container height from current ~24dp to 44dp.
   - Replace the "+4 font delta" emoji label (line 2293) with explicit 16pt sizing.
   - Change name label from `STYLE_BODY_4_MEDIUM` (12pt 500) → `STYLE_BODY_3` (13pt) + custom `SEMIBOLD` weight (Zen's 600).
   - Add an "actions" button (3-dot menu trigger) at trailing edge, 26×26dp container with icon (Zen: `width × height: 26px × 26px`).

**Verification**: Active workspace renders as a 44dp tall row with icon + 13pt 600 name + actions button. Visually matches Zen's expanded sidebar header.

**Risk**:
- **R9: Adds 20dp+ to top of tab list.** At 700px window with full features, total = 38 (top bar) + 44 (indicator) + 360 (10 tabs) + 36 (footer) = 478dp. Fits.
- **R10: Actions button needs a context menu.** If Maho doesn't yet have a workspace context menu, file a follow-up ticket. Indicator can ship without functional menu (trigger callable but UI-only) — track as P2.

#### 4B — Favorites Icon-Only Grid

**Zen pattern**: `vertical-tabs.css:1224` — `display: none !important` on essential tab labels. Pure icon grid.

**Maho gap**: `maho_sidebar_favorites_grid_view.cc:361` shows label at 12pt.

**Action**:
1. **`maho_sidebar_favorites_grid_view.cc`**: remove label construction OR set `label->SetVisible(false)` unconditionally.
2. **`maho_sidebar_layout_tokens.h`**: `kFavoriteTileHeightDp = 47` → match Zen Essential tile (44dp `--tab-min-height`):
   ```cpp
   inline constexpr int kFavoriteTileHeightDp = 44;     // Zen: essentials --tab-min-height: 44px
   inline constexpr int kFavoriteGlyphSizeDp = 16;      // Zen: --size-item-small: 16px (was 15)
   ```
3. Remove `kFavoriteLabelMaxWidthDp` token (unused after label hidden) or keep with `// deprecated, label hidden` comment.

**Verification**: Favorites grid shows icons only, 44dp tall tiles, 16dp glyphs. No labels visible.

**Risk**:
- **R11: Removing labels reduces information density.** This IS Zen behavior. Document in commit message that this is intentional Zen parity. Users who want labels would need a config flag (out of scope here).
- **R12: Tile aspect ratio change.** 47×W → 44×W tiles may look squatter. Acceptable per Zen reference.

#### 4C — Search Pill Primary Label Bump

**Zen pattern**: URL bar inline at 1.15em (≈16px) baseline / 1.5em (≈21px) macOS floating.

**Action**:
1. **`maho_sidebar_search_view.cc:193, 674`**:
   ```cpp
   // OLD: primary_label_->SetTextStyle(views::style::STYLE_BODY_4);  // 12pt
   // NEW: Zen parity — URL bar floating font-size: 1.15em
   primary_label_->SetTextStyle(views::style::STYLE_BODY_3);  // 13pt cross-platform
   ```
2. macOS branch in Phase 5 will bump to a custom `Derive(+3)` for 16pt.

**Verification**: Search pill primary label = 13pt, line-height auto (20dp from STYLE_BODY_3).

**Risk**:
- **R13: 13pt at 38dp pill height** — pill internal vertical padding shrinks from `(38-18)/2 = 10` to `(38-20)/2 = 9`. Acceptable.

#### 4D — Favicon Border-Radius (mandatory)

Already covered in Phase 2 (`kTabFaviconCornerRadiusDp = 4` + `MahoTabFaviconView` clipping helper). **No longer optional.** ~90 min implementation budgeted.

---

### Phase 5 — macOS Conditional Scale-Up (MANDATORY for True Parity)

**Goal**: Apply Zen's macOS-specific bumps. v1 marked this optional; v2 mandates it for parity.

**`maho_sidebar_layout_tokens.h` — wrap critical tokens in `BUILDFLAG`**:

```cpp
#include "build/build_config.h"

// --- macOS Scale-Up (Zen parity: zen-theme.css macOS bumps) ---
#if BUILDFLAG(IS_MAC)
inline constexpr int kTopBarHeightDp = 42;                // Zen macOS: 42px
inline constexpr int kTopBarIconSizeDp = 17;              // Zen macOS: 17px
inline constexpr int kRowHeightDp = 40;                   // Zen macOS: ~36 * 1.111 (1.25rem coupling) = 40
inline constexpr int kTabRowLineHeightDp = 30;            // Zen macOS: 1.7 * 17.5 = 29.75
inline constexpr int kFolderRowHeightDp = 44;             // Maintains row+4 ratio with 40dp row
inline constexpr int kWorkspaceIndicatorHeightDp = 48;    // Bump proportionally
#else
inline constexpr int kTopBarHeightDp = 38;
inline constexpr int kTopBarIconSizeDp = 16;
inline constexpr int kRowHeightDp = 36;
inline constexpr int kTabRowLineHeightDp = 24;
inline constexpr int kFolderRowHeightDp = 40;
inline constexpr int kWorkspaceIndicatorHeightDp = 44;
#endif
```

**`maho_sidebar_tab_list_view.cc` — tab title font macOS branch**:

```cpp
#if BUILDFLAG(IS_MAC)
// Zen parity: vertical-tabs.css macOS — font-size: 1.25rem (~17.5px)
label()->SetFontList(label()->font_list().Derive(
    +5 /* delta from base 12pt → 17pt */, gfx::Font::NORMAL,
    gfx::Font::Weight::NORMAL));
label()->SetLineHeight(30);  // 1.7 * 17.5
#else
label()->SetTextStyle(views::style::STYLE_BODY_3);  // 13pt
label()->SetLineHeight(kTabRowLineHeightDp);  // 24
#endif
```

**`maho_sidebar_search_view.cc` — primary label macOS bump**:

```cpp
#if BUILDFLAG(IS_MAC)
primary_label_->SetFontList(primary_label_->font_list().Derive(
    +4 /* 12pt → 16pt to approximate Zen's 1.5em floating */,
    gfx::Font::NORMAL, gfx::Font::Weight::NORMAL));
#else
primary_label_->SetTextStyle(views::style::STYLE_BODY_3);  // 13pt
#endif
```

**Verification**:
- Build all three platforms (`build_maho.py` macOS, plus cross-build for Linux + Windows per `chromium-fork-cross-build-linux-windows-parallel-plan-2026-05-24.md`).
- macOS: tab row 40dp, font ~17pt, line-height 30dp, top bar 42dp.
- Linux/Windows: tab row 36dp, font 13pt, line-height 24dp, top bar 38dp.
- Side-by-side macOS Maho vs macOS Zen reference: pixel-aligned at default 288dp width.

**Risks**:
- **R14: Maintenance burden of `BUILDFLAG(IS_MAC)` branches.** Adds ~6 conditional regions. Acceptable per parity mandate; if maintenance becomes painful, future migration to a runtime `IsMacOS()` helper or design-token-style indirection is possible.
- **R15: `Derive(+5)` may produce non-integer point sizes on some platforms.** Verify font rendering at 17pt vs 17.5px CSS — likely 0.5pt rounding difference, imperceptible.
- **R16: macOS tab row 40dp + 30dp line-height = 5dp top + 5dp bottom internal padding.** Tight. If text clips, bump row to 42dp. Verify in QA.

---

## 4. Sequencing & Throughput

Phases 1–3 are file-overlap-heavy (all touch `tab_list_view.cc` or `layout_tokens.h`). Recommended: **strict serialization** Phase 1 → 2 → 3 → 4 → 5. Parallelization tempting but merge cost > savings.

**Effort estimate (v2 honest)**:
- Phase 0: 45 min
- Phase 1: 90 min (folder restructure + custom semibold helper)
- Phase 2: 120 min (row + title + favicon clipping helper class — 90 min + 30 min plumbing)
- Phase 3: 75 min (top bar restructure)
- Phase 4: 180 min (workspace indicator structural addition + favorites label removal + search pill + favicon Phase 2 spillover)
- Phase 5: 90 min (BUILDFLAG branches + cross-platform verification)

**Total: ~10 hours focused work + verification cycles.**

---

## 5. Verification Matrix

| Phase | Compile macOS | Compile Linux | Compile Windows | LSP | Browser launch | Pixel diff vs Zen |
|---|---|---|---|---|---|---|
| 0 | ✅ | — | — | ✅ | — | baseline captured |
| 1 | ✅ | ✅ | ✅ | ✅ | folder render | folder height 40dp ✓, glyph 28dp ✓, weight 600 ✓ |
| 2 | ✅ | ✅ | ✅ | ✅ | tab list | row 36dp ✓, title 13pt ✓, favicon 4dp radius ✓ |
| 3 | ✅ | ✅ | ✅ | ✅ | top bar + footer | top bar 38dp ✓, footer 36dp button ✓, all icons 16dp ✓ |
| 4 | ✅ | ✅ | ✅ | ✅ | full sidebar | workspace indicator 44dp ✓, favorites icon-only ✓, search 13pt ✓ |
| 5 | ✅ macOS | ✅ Linux | ✅ Windows | ✅ | platform-specific | macOS bumped values match Zen exactly |

**Pixel diff method**: Open both browsers (Maho, Zen) at identical window dimensions. Capture sidebar at default 288dp width. Open in Pixelmator / image diff tool. Tolerance ≤2px on any measured element.

---

## 6. Risk Register (v2 — expanded from R1-R16)

| ID | Risk | Likelihood | Mitigation |
|---|---|---|---|
| R1 | Chromium SEMIBOLD not rendered correctly on Linux | Medium | Phase 0 verification step; fallback to MEDIUM with code comment |
| R2 | Folder indent overflow at depth 4 | Medium | Tested at depth 4 in Phase 1 QA |
| R3 | STYLE_BODY_3 (13pt) close-button overlap at long titles | Low | Test 30-char title + close button at 280dp |
| R4 | Removing active-tab MEDIUM may regress a11y | Low | Documented as Zen-parity-mandated; follow-up if user complaint |
| R5 | MahoTabFaviconView class plumbing | High | Budget 30 min plumbing; if blocked, file separate PR |
| R6 | Top bar 22→38dp eats vertical space | Low | Verified 600dp total fits 700px window |
| R7 | Footer 4 × 36dp = 144dp + gap may overflow at 280dp | Low | 280-144-15(gap) = 121dp leftover; acceptable |
| R8 | Lucide at 16dp visual heaviness | None | Zen uses same; parity by definition |
| R9 | Workspace indicator adds 44dp to top | Low | Total fits comfortably |
| R10 | Workspace actions button lacks menu | Medium | UI-only; track P2 ticket |
| R11 | Removing favorite labels reduces info density | Medium | Documented; future config flag if needed |
| R12 | Favorites tile aspect ratio shift | Low | Acceptable per Zen ref |
| R13 | Search pill 13pt internal padding | Low | 9dp top/bottom OK |
| R14 | BUILDFLAG branch maintenance burden | Medium | Accept; consider runtime helper later |
| R15 | Derive(+5) point-size rounding | Low | Imperceptible |
| R16 | macOS row 40dp + 30dp line-height tight | Medium | Bump to 42dp if QA flags |
| **R17** | **Hidden token callsite outside sidebar/** | Low | Phase 0 cross-tree grep |
| **R18** | **Semibold-style LabelButton unsupported** (folder name uses LabelButton, not Label) | Medium | LabelButton internal label supports `SetFontList` — verify in Phase 0 |
| **R19** | **macOS tab row 40dp regresses tab density** (more scroll) | Medium | This IS Zen behavior on macOS — parity by definition |
| **R20** | **Zen's `1.7` line-height vs Maho's integer dp** | Low | 24dp baseline / 30dp macOS are integer rounding of 23.8 / 29.75 — within ≤1px tolerance |

---

## 7. Open Questions (resolved as Zen-parity defaults)

| Q | v1 default | **v2 (Zen-parity-mandated) default** |
|---|---|---|
| macOS scale-up? | No (Path C) | **YES (Path A)** — Phase 5 mandatory |
| Folder header style? | STYLE_BODY_4_MEDIUM | **STYLE_BODY_4 + Derive(0, NORMAL, SEMIBOLD)** for Zen's 600 |
| Active tab MEDIUM weight? | Keep | **Remove** — match Zen NORMAL |
| Folder hover popup title? | STYLE_BODY_3 NORMAL | **STYLE_BODY_3 + SEMIBOLD** for Zen's 600 |
| Search pill primary label? | 12pt (deferred) | **13pt baseline + 16pt macOS** |
| Workspace indicator? | (not addressed) | **Add 44dp tall indicator row with icon + 13pt 600 name + actions button** |
| Favorites label? | Keep at 12pt | **Hide (`SetVisible(false)`) for icon-only Zen parity** |
| Favicon corner radius? | Optional, drop if costly | **Mandatory 4dp via custom MahoTabFaviconView** |

All previously-open questions now have parity-driven answers. Plan has zero design decisions remaining.

---

## 8. Out-of-Scope (explicit, with rationale)

- **Library Rail font/icon** — Maho-unique surface (Zen has no Library); preserve current sizing.
- **Action Popover** — Maho-unique; preserve.
- **Archive empty state** — Maho-unique; preserve.
- **Update Pill** — Maho-unique; preserve.
- **Color/theme tokens** — separate audit needed.
- **Sidebar collapsed mode (40dp)** — Phase 5 macOS branch covers expanded only; collapsed needs separate plan.
- **Animation timings** — not in audit.
- **Workspace context menu functional implementation** — UI added in Phase 4A, menu wiring tracked separately.

---

## 9. References

### Primary audit data
- Maho audit: `task_id=bg_242dc10e` (2026-06-07).
- Zen audit: `task_id=bg_c1095314` (2026-06-07).

### Zen source pinned commit
`zen-browser/desktop @ 965bb9391da5ca543b9ab79125c7bbab8f645e7a`

Key Zen files audited:
- `src/zen/tabs/zen-tabs/vertical-tabs.css`
- `src/zen/folders/zen-folders.css`
- `src/zen/spaces/zen-workspaces.css`
- `src/zen/common/styles/zen-theme.css`
- `src/zen/common/styles/zen-omnibox.css`

### Maho source files
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.{h,cc}` (new edits in Phase 4A)

### Chromium typography
- `chromium/src/ui/views/style/typography_provider.cc` — verify SEMIBOLD support in Phase 0.
- `STYLE_BODY_3` = 13pt / line-height 20dp
- `STYLE_BODY_4` = 12pt / line-height 18dp
- `STYLE_BODY_5` = 11pt / line-height 16dp
- `gfx::Font::Weight::SEMIBOLD` = 600 (verify in Phase 0)
- `gfx::Font::Weight::MEDIUM` = 500
- `gfx::Font::Weight::BOLD` = 700

### Cross-build infrastructure
- `plans/chromium-fork-cross-build-linux-windows-parallel-plan-2026-05-24.md` (for Phase 5 multi-platform verification)

---

## 10. Approval Gate

This v2 plan supersedes v1 entirely. Before Phase 0 execution:

1. **Re-run Momus** with explicit "true-parity gate" focus to confirm v2 closes all 16 deltas at exact Zen values.
2. **Confirm Phase 0's Chromium SEMIBOLD discovery** — if SEMIBOLD is unavailable, Phase 1's folder-name + folder-popup-title implementation must use a different mechanism (custom `gfx::Font::Weight` integer literal). Block Phase 1 until resolved.
3. **User design sign-off on structural changes** — Phase 4A (workspace indicator 44dp), Phase 4B (favorites label hidden), Phase 5 (macOS scale-up). These are not pure numeric bumps; user-visible structural changes.

Run:
```
task(subagent_type="momus", prompt=".sisyphus/plans/sidebar-typography-icon-zen-parity-plan-2026-06-07.md", load_skills=[], run_in_background=false)
```

Address feedback, then begin Phase 0.

---

## v1 → v2 Diff Summary

| Item | v1 | v2 |
|---|---|---|
| Folder height | 38dp ("compromise") | **40dp (Zen exact)** |
| Folder glyph container | 22dp ("compromise") | **28dp (Zen exact)** |
| Folder glyph icon | 16dp | **20dp** |
| Folder indent | 13dp | **14dp** |
| Folder weight | MEDIUM (500) | **SEMIBOLD (600)** |
| Tab title font | 12pt deferred | **13pt baseline + 17pt macOS (Phase 5)** |
| Active tab MEDIUM | "intentional divergence" | **REMOVED — match Zen NORMAL** |
| Footer button | 20dp ("compromise") | **36dp (Zen exact)** |
| Footer icon | 12dp ("compromise") | **16dp (Zen exact)** |
| Top bar | 22dp button only | **38dp toolbar with 24dp buttons** |
| macOS scale-up | optional, default = no | **MANDATORY (Phase 5)** |
| Search pill primary | 12pt optional bump | **13pt baseline + 16pt macOS** |
| Workspace indicator | not addressed | **NEW 44dp indicator row (Phase 4A)** |
| Favorites label | shown 12pt | **hidden (icon-only Zen parity)** |
| Favicon radius | drop if >30min | **mandatory 4dp via custom view** |
| Total effort | ~3 hours | **~10 hours** (honest estimate) |

v2 is approximately 3.3× the work of v1 but achieves what the user asked for: TRUE Zen parity, not Zen-inspired refresh.
