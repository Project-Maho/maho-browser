# Sidebar Top Bar + Search Pill Zen Visual Parity — 2026-06-09

**Status:** Draft (awaiting approval)
**Owner:** Sisyphus
**Scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc` + `maho_sidebar_top_bar_view.{cc,h}` + tests
**Reference:** Zen Browser visual style — search pill is glassy/transparent (no contrast vs sidebar), no trailing utility icon visible, top bar has 3 nav buttons only (no AI sparkle)

---

## 0. Goal

Apply user-selected options A + B + C + E to bring Maho's top-bar + address-bar visual closer to Zen:

| Option | Change | Visible effect |
|---|---|---|
| **A** | Search pill background `kColorSysSurface1` → transparent | Pill blends into sidebar, no contrasting fill |
| **B** | Search pill 1dp border removed | No visible outline around pill |
| **C** | Trailing utility (sliders) icon hidden | Right side of pill empty — only label text |
| **E** | AI sparkle button removed from top bar | Top bar = back/forward/reload only (3 buttons) |

**Non-goals**:
- Lucide icon stroke-thickness adjustment (would require modifying SVG vector data; affects all surfaces)
- Removing `utility_icon_*` infrastructure entirely (other code paths reference it for popup management)
- Changing search pill height or text size (already set in prior parity work)
- Repositioning top bar buttons

**Definition of Done**:
1. Visual: search pill has no visible fill/border at rest; AI sparkle absent from top bar.
2. Code: utility_icon_ infrastructure intact (member field, popup hooks) — only `SetVisible(false)` to hide.
3. Tests: `ai_button_for_testing()` removed; any test that referenced it updated.
4. Build green: `python3 maho-chromium/build/scripts/build_maho.py --skip-rust` exits 0.
5. Tests pass: `out/Default/unit_tests --gtest_filter="SidebarDnDDispatchTest.*:MahoSidebarTopBarViewTest.*"` exit 0.
6. Visual QA: Maho.app launched, sidebar matches Zen reference for pill + top bar density.

---

## 1. Scope of Files Touched

| File | Phase | Lines |
|---|---|---|
| `maho_sidebar_search_view.cc` | A, B, C | ~654-668 (UpdateSearchChrome), ~235-252 (utility_icon_slot creation) |
| `maho_sidebar_top_bar_view.cc` | E | ~192-196, ~218-225, ~278-281, ~321-end (OnAiPressed) |
| `maho_sidebar_top_bar_view.h` | E | ~50, 66, 78 (ai_button_for_testing, OnAiPressed declaration, ai_button_ field) |
| Tests referencing `ai_button_` | E | grep across `maho_sidebar_*_test.cc` and `*_unittest.cc` |

---

## 2. Phase Breakdown

### Phase A — Search Pill Transparent Background

**Action**: In `maho_sidebar_search_view.cc:654-659` (inside `UpdateSearchChrome`):

```cpp
// BEFORE
idle_button_->SetBackground(views::CreateRoundedRectBackground(
    ui::kColorSysSurface1,
    sidebar_layout::kSearchPillCornerRadiusDp));

// AFTER
idle_button_->SetBackground(nullptr);
```

**Verification**:
```bash
grep -n "idle_button_->SetBackground" maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc
```
Expected: exactly 1 match showing `SetBackground(nullptr)` (UpdateSearchChrome callsite). No `CreateRoundedRectBackground(ui::kColorSysSurface1` for idle_button_ remaining.

**Effort**: 5 min.

---

### Phase B — Search Pill Border Removed

**Action**: In `maho_sidebar_search_view.cc:660-668` (same function, immediately after Phase A change):

```cpp
// BEFORE
idle_button_->SetBorder(views::CreatePaddedBorder(
    views::CreateRoundedRectBorder(
        1, sidebar_layout::kSearchPillCornerRadiusDp,
        show_placeholder ? ui::kColorSysSurfaceVariant
                         : ui::kColorSysNeutralOutline),
    gfx::Insets::TLBR(
        kSearchPillInnerVerticalInsetDp,
        kSearchCardLeadingInsetDp,
        kSearchPillInnerVerticalInsetDp,
        kSearchCardTrailingInsetDp)));

// AFTER (keep padding via EmptyBorder, drop the rounded-rect outline)
idle_button_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
    kSearchPillInnerVerticalInsetDp,
    kSearchCardLeadingInsetDp,
    kSearchPillInnerVerticalInsetDp,
    kSearchCardTrailingInsetDp)));
```

Padding (insets) kept the same — text positioning unchanged. Only the visible 1dp outline is removed.

The `show_placeholder` color branch is no longer relevant since there's no border to color.

**Verification**:
```bash
grep -n "CreateRoundedRectBorder.*kSearchPillCornerRadiusDp" maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc
```
Expected: 0 matches.

```bash
grep -nE "idle_button_->SetBorder.*EmptyBorder" maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc
```
Expected: 1 match.

**Effort**: 5 min.

---

### Phase C — Hide Trailing Utility (Sliders) Icon

**Strategy**: `utility_icon_slot_` visibility is dynamically toggled in `UpdateUtilityAffordanceVisibility()` (line 624) based on `CanShowUtilityAffordance()`. Adding `SetVisible(false)` in the constructor is **NOT sufficient** — it would be overridden on next update.

**Action**: Modify line 624 in `MahoSidebarSearchView::UpdateUtilityAffordanceVisibility()`:

```cpp
// BEFORE (line 624)
utility_icon_slot_->SetVisible(affordance);

// AFTER
utility_icon_slot_->SetVisible(false);
```

The `affordance` variable continues to drive `utility_icon_->SetAffordanceVisible(affordance)` (line 625) and hover state (line 627) — so internal state remains consistent. Only the slot visibility is forced false.

This is a **single-line change** — affects all paths that toggle utility visibility.

**Verification**:
```bash
# 1. Slot visibility forced to false in UpdateUtilityAffordanceVisibility
grep -n "utility_icon_slot_->SetVisible" maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc
```
Expected: 1 hit at line ~624 reading `utility_icon_slot_->SetVisible(false);`. The line should NOT contain `affordance`.

```bash
# 2. utility_icon_ and utility_icon_slot_ infrastructure intact
grep -cn "utility_icon_" maho-chromium/browser/ui/views/sidebar/maho_sidebar_search_view.cc
```
Expected: ~12 matches (popup management code unaffected).

**Risk**: None — `affordance` is still used for affordance and hover state; only the dimensional visibility of the slot is suppressed.

**Effort**: 5 min (single-line change).

---

### Phase E — Remove AI Sparkle Button from Top Bar

This is the largest change — touches header, .cc, and tests.

#### E.1 — Remove from class

**File**: `maho_sidebar_top_bar_view.h`

```cpp
// REMOVE line 50:
views::ImageButton* ai_button_for_testing() { return ai_button_; }

// REMOVE line 66:
void OnAiPressed(const ui::Event& event);

// REMOVE line 78:
raw_ptr<views::ImageButton> ai_button_ = nullptr;
```

#### E.2 — Remove construction

**File**: `maho_sidebar_top_bar_view.cc:192-196`

```cpp
// REMOVE THIS BLOCK (5 lines):
ai_button_ = AddControlButton(
    nav_cluster_,
    base::BindRepeating(&MahoSidebarTopBarView::OnAiPressed,
                        weak_factory_.GetWeakPtr()),
    maho_lucide_icons::kWandSparklesIcon, u"AI Panel");
```

#### E.3 — Remove from cluster iteration

**File**: `maho_sidebar_top_bar_view.cc:218-225` (around line 223):

```cpp
// BEFORE
for (auto* button : {back_button_.get(), forward_button_.get(),
                     reload_button_.get(), ai_button_.get()}) {

// AFTER
for (auto* button : {back_button_.get(), forward_button_.get(),
                     reload_button_.get()}) {
```

#### E.4 — Remove from UpdateButtonImages

**File**: `maho_sidebar_top_bar_view.cc:280-281`

```cpp
// REMOVE THIS BLOCK (2 lines):
UpdateButtonState(ai_button_, true,
                  maho_lucide_icons::kWandSparklesIcon, u"AI Panel");
```

#### E.5 — Remove OnAiPressed method body

**File**: `maho_sidebar_top_bar_view.cc:321+`

```cpp
// REMOVE the entire MahoSidebarTopBarView::OnAiPressed function definition
// (~5-15 lines depending on body content)
```

Read lines 321-340 first to determine exact extent:
```bash
sed -n '321,345p' maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc
```

#### E.6 — Update tests

Pre-flight audit confirmed only ONE test file is affected:

- `maho_sidebar_dnd_dispatch_unittest.cc:1116` — `EXPECT_TRUE(top_bar.ai_button_for_testing()->GetVisible());`
- `maho_sidebar_dnd_dispatch_unittest.cc:1135` — `EXPECT_TRUE(top_bar.ai_button_for_testing()->GetEnabled());`

Both lines must be **deleted** (the entire EXPECT_TRUE statement). Verify the surrounding test cases still make sense without these assertions; if a test was specifically about AI button presence/enabled state, the test itself may need to be removed. Read context:

```bash
sed -n '1108,1140p' maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc
```

Other files DO NOT need changes:
- `maho_location_bar_utility_icon_view_interactive_uitest.cc:87` uses `kWandSparklesIcon` for a separate `magic_action` in the location bar utility panel — unrelated to sidebar AI button.

**Verification (cumulative for E)**:
```bash
# 1. ai_button_ field removed from header
grep -c "ai_button_" maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h
```
Expected: 0.

```bash
# 2. ai_button_ construction removed from .cc
grep -c "ai_button_" maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc
```
Expected: 0.

```bash
# 3. kWandSparklesIcon usage in top_bar removed
grep -c "kWandSparklesIcon" maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc
```
Expected: 0.

```bash
# 4. OnAiPressed declaration + definition both removed
grep -rn "OnAiPressed" maho-chromium/browser/ui/views/sidebar/
```
Expected: 0 matches in sidebar/ directory (any matches outside indicate a remaining call site to clean up).

```bash
# 5. Build is clean (no unused-variable warnings, no missing-symbol link errors)
python3 maho-chromium/build/scripts/build_maho.py --skip-rust
```
Expected: exit 0.

```bash
# 6. Top bar tests still pass (the ones that didn't depend on ai_button)
out/Default/unit_tests --gtest_filter="MahoSidebarTopBarViewTest.*"
```
Expected: PASSED.

**Risks**:
- **R1**: Other code (outside sidebar/) might still call `OnAiPressed` or reference `ai_button_for_testing()`. The grep at E.6 catches sidebar/ but not workspace-wide. Run `grep -rn` on entire `maho-chromium/` and `maho/` directories.
- **R2**: Action handlers (e.g., command palette, keyboard shortcut) might trigger AI panel via the same callback chain. If `OnAiPressed` was the entry point, removing it breaks those triggers. Verify by grepping for `IDC_*AI*` or similar action IDs.
- **R3**: If the AI panel can be opened via other means (menu item, command palette), removing the toolbar button still leaves those entry points functional. Confirm with user that the goal is **only the toolbar button**, not removing AI panel entirely.

**Effort**: 60 min (incl. risk audits + test updates).

---

### Phase F — Build, Test, Visual QA

**Action**:
```bash
# 1. Build
python3 maho-chromium/build/scripts/build_maho.py --skip-rust

# 2. Run tests
out/Default/unit_tests --gtest_filter="SidebarDnDDispatchTest.*:MahoSidebarDragImageControllerTest.*:MahoSidebarTopBarViewTest.*"

# 3. Restart Maho
ps aux | grep "Maho.app/Contents/MacOS/Maho" | grep -v grep | awk '{print $2}' | xargs -I{} kill {} 2>/dev/null
sleep 2
open chromium/src/out/Default/Maho.app
```

**Verification matrix**:

| Phase | Verification command | Expected |
|---|---|---|
| A | `grep -c "kColorSysSurface1.*kSearchPillCornerRadiusDp" search_view.cc` | 0 |
| A | `grep -n "idle_button_->SetBackground(nullptr)" search_view.cc` | ≥ 1 hit |
| B | `grep -c "CreateRoundedRectBorder.*kSearchPillCornerRadiusDp" search_view.cc` | 0 |
| C | `grep -n "utility_icon_slot_->SetVisible(false)" search_view.cc` | ≥ 1 hit |
| C | `grep -n "utility_icon_slot_->SetVisible(true)" search_view.cc` | 0 |
| E | `grep -c "ai_button_" top_bar_view.{h,cc}` | 0 across both |
| E | `grep -rn "kWandSparklesIcon" maho-chromium/browser/ui/views/sidebar/` | 0 |
| E | `grep -rn "OnAiPressed" maho-chromium/browser/ui/views/sidebar/` | 0 |
| F | Build exit code | 0 |
| F | Test exit code | 0 |

Visual QA checklist (after launch):
- [ ] Top bar shows 3 nav buttons (back, forward, reload) — NOT 4
- [ ] No AI sparkle (✦) icon anywhere in top bar
- [ ] Search pill has no visible background fill
- [ ] Search pill has no visible 1dp border
- [ ] No sliders/filter icon visible at right end of search pill
- [ ] Pill text still readable (not colliding with edges due to lost border padding)
- [ ] Compare side-by-side with Zen reference screenshot — top bar + pill area should look near-identical

**Effort**: 30 min.

---

## 3. Sequencing

Strict serial: **A → B → C → E → F**.

A and B are in the same function — apply together in one edit. C is independent. E touches multiple files including header — do last (most impactful). F is verification only.

**Total effort**: 5 + 5 + 5 + 60 + 30 = **~1h 45m focused work**.

---

## 4. Risk Register (Pre-flight Audit Status)

| ID | Risk | Pre-flight Result | Status |
|---|---|---|---|
| R1 | Phase B's removal of border breaks placeholder vs active visual distinction | Text color still differentiates (`kColorSysOnSurfaceSubtle` vs `kColorSysOnSurface`) | RESOLVED — accept |
| R2 | Phase C hides utility icon globally; some feature (e.g., Arc layout) may have expected it visible | Audit found `arc_active` only sets `secondary_label_` element ID, doesn't touch utility_icon_slot_ | RESOLVED — single-line change at L624 is safe |
| R3 | Phase E breaks AI panel access if button was the only entry point | Q1 audit: AI panel accessible via `chrome://maho-ai/` URL, popup, multiple other paths | RESOLVED — only toolbar button removed |
| R4 | Phase E removes `ai_button_for_testing()` — tests fail | Only `dnd_dispatch_unittest.cc:1116, 1135` reference; will be removed in E.6 | RESOLVED — 2 lines |
| R5 | Phase E breaks workspace-wide if `OnAiPressed` is referenced elsewhere | Audit: `OnAiPressed` only in `top_bar_view.{cc,h}`, no external callers | RESOLVED — safe |
| R6 | Build cache may serve stale .o files after edits | Mitigated by `python3 build_maho.py --skip-rust` (re-runs ninja from current source) | Standard mitigation |

---

## 5. Open Questions (Pre-flight Resolved)

| # | Q | Resolution |
|---|---|---|
| Q1 | Phase E — also remove AI panel entry points outside the toolbar (menu, command palette)? | **NO** — `chrome://maho-ai/` URL, popup, and other paths confirmed working. Only toolbar button removed. |
| Q2 | Phase C — also remove `utility_icon_` field entirely (cleanup)? | **NO** — line 624 single-line change preserves all infrastructure. |
| Q3 | Phase B — keep some visual indicator for active vs placeholder mode (since border was the differentiator)? | **NO** — text color (`kColorSysOnSurfaceSubtle` vs `kColorSysOnSurface`) is sufficient differentiator. |
| Q4 | Phase A — should pill have a hover-state background reveal (so user knows it's clickable)? | **OUT OF SCOPE** — Current scope: rest state matches Zen. Hover polish deferred. |

---

## 6. Files Touched Summary

```
maho-chromium/browser/ui/views/sidebar/
├── maho_sidebar_search_view.cc      [A: -1 SetBackground branch, +1 SetBackground(nullptr)]
│                                     [B: -1 RoundedRectBorder, +1 EmptyBorder]
│                                     [C: +1 SetVisible(false) line]
├── maho_sidebar_top_bar_view.h      [E: -3 lines (field, method decl, testing accessor)]
└── maho_sidebar_top_bar_view.cc     [E: -5 line ai_button_ ctor, -1 cluster ref,
                                          -2 UpdateButtonImages line, -OnAiPressed body]
```

Plus possibly test files (E.6 grep determines).

---

## 7. References

- Zen Browser reference (visual): user screenshot from session 2026-06-09
- Maho prior parity work: `.sisyphus/plans/sidebar-typography-icon-zen-parity-plan-2026-06-07.md`
- Current sidebar tokens: `.sisyphus/plans/sidebar-parity-v2-recovery-and-newly-found-gaps-2026-06-08.md`

---

## 8. Approval Gate

All pre-flight audits completed. Status:
- ✅ R1, R2, R3, R4, R5: RESOLVED
- ✅ Q1-Q4: RESOLVED
- ✅ E.6 affected files: identified (2 lines in 1 file)

Plan is ready for execution. Approve → execute A → B → C → E → F serially with grep-verification at each phase.
