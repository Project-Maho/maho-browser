# Sidebar Folder Hover Popup — Remediation Plan

**Date:** 2026-06-01
**Surface:** Native Views (C++)
**Scope:** Address all 4th-pass review findings (CRITICAL/HIGH/MEDIUM/LOW + REGRESSIONS + PLAN_DEVIATIONS) before merge.
**Predecessor plan:** `sidebar-folder-hover-popup-plan-2026-05-31.md`

---

## 1. Background

The hover popup feature shipped through 4 review passes. Build now compiles, but the 4th pass surfaced critical UX gaps and a regression that snuck back in during the 3rd→4th iteration. This plan is the minimum work required to merge.

**Current state of fixes (from 4th-pass Oracle review):**
- 12 prior issues resolved and still working
- 2 CRITICAL bugs introduced or surfaced
- 2 HIGH plan deviations (kSearchOnly dead, 4px gap reverted)
- 1 HIGH regression (search field auto-focus broken)
- 4 MEDIUM polish items
- 3 LOW hygiene items

---

## 2. Affected Files

```
maho-chromium/browser/ui/views/sidebar/
├── maho_sidebar_folder_hover_popup_view.h       (modify: weak_factory, sequence checker on ItemRow)
├── maho_sidebar_folder_hover_popup_view.cc      (modify: most of changes here)
├── maho_sidebar_folder_hover_controller.h       (modify: focus observer, constants)
├── maho_sidebar_folder_hover_controller.cc      (modify: cancel timer on press, polling cap, focus observer)
└── maho_sidebar_tab_list_view.{h,cc}            (modify: cancel timer on press, kSearchOnly decision)
```

No other files need changes. No Rust / FFI / Mojo / GN work.

---

## 3. Fixes by Priority

### 3.1 CRITICAL — must fix

#### C1. `set_close_on_deactivate(true)` is dead code with `SetCanActivate(false)`

**File:** `maho_sidebar_folder_hover_popup_view.cc:209-210`

**Problem:** Non-activatable widget cannot deactivate, so close-on-deactivate never fires. User Cmd+Tabbing away while popup is showing leaves popup orphaned on screen indefinitely (mouse never crosses the row boundary so `OnRowMouseExited` never fires).

**Fix:**
1. Remove the misleading line: `delegate->set_close_on_deactivate(true);`
2. Add explicit dismissal hooks in the controller:
   - Observe browser widget activation via `views::WidgetObserver` on the **browser's** widget (not the popup's). Hide on browser deactivation.
   - Observe the parent browser frame's focus change events.

**Implementation in `controller.h`:**
```cpp
class MahoSidebarFolderHoverController : public views::WidgetObserver,
                                         public TabStripModelObserver {
  ...
 private:
  void StartObservingBrowserWidget();
  void StopObservingBrowserWidget();
  
  raw_ptr<views::Widget> observed_browser_widget_ = nullptr;
};
```

**Implementation in `controller.cc::ShowNow`** — after `popup_widget_->AddObserver(this)`:
```cpp
// Observe browser widget for activation changes to dismiss popup when
// the browser loses focus (Cmd+Tab, click another window, etc).
if (anchor && anchor->GetWidget()) {
  observed_browser_widget_ = anchor->GetWidget()->GetTopLevelWidget();
  if (observed_browser_widget_ && observed_browser_widget_ != popup_widget_) {
    observed_browser_widget_->AddObserver(this);
  }
}
```

**Implementation in `OnWidgetDestroying` — extend to handle browser widget too:**
```cpp
void OnWidgetDestroying(views::Widget* widget) override {
  if (widget == observed_browser_widget_) {
    observed_browser_widget_ = nullptr;
  }
  if (widget == popup_widget_) {
    /* existing logic */
  }
}
```

**Add new override:**
```cpp
void OnWidgetActivationChanged(views::Widget* widget, bool active) override {
  if (widget == observed_browser_widget_ && !active) {
    HideImmediately();
  }
}
```

**`HideImmediately`** — also unregister browser observer:
```cpp
if (observed_browser_widget_) {
  observed_browser_widget_->RemoveObserver(this);
  observed_browser_widget_ = nullptr;
}
```

---

#### C2. Click during 225 ms show timer leaves orphan popup over toggled folder

**Files:** `maho_sidebar_tab_list_view.cc:1890`, `maho_sidebar_folder_hover_controller.cc:57`

**Problem:** Sequence:
1. User hovers folder → `OnMouseEntered` → `show_timer_` started (225 ms)
2. User clicks folder → `label_button_` fires `toggle_callback` → `is_expanded_` flips
3. Timer is **never cancelled** because `OnMousePressed` doesn't notify the controller
4. 225 ms later → `ShowNow` fires → popup appears over the now-expanded folder, duplicating inline children

**Fix:**

Override `OnMousePressed` on `SidebarFolderRowView` to cancel the popup show timer:

**`maho_sidebar_tab_list_view.h` — add to `SidebarFolderRowView`:**
```cpp
bool OnMousePressed(const ui::MouseEvent& event) override;
```

**`maho_sidebar_tab_list_view.cc` — add immediately after `OnMouseExited`:**
```cpp
bool SidebarFolderRowView::OnMousePressed(const ui::MouseEvent& event) {
  // Cancel any pending hover popup so it doesn't appear over the toggled folder.
  if (hide_popup_callback_) {
    hide_popup_callback_.Run();
  }
  return views::View::OnMousePressed(event);
}
```

`hide_popup_callback_` calls `OnRowMouseExited` which both stops `show_timer_` and arms the grace timer. The grace timer will fire `HideImmediately` if no popup ended up showing (no-op) or hide the popup if it raced ahead.

> **Note:** `OnMousePressed` returns `false` to NOT consume the event — the click must propagate to `label_button_` for toggle to fire. With `SetNotifyEnterExitOnChild(true)` on the row, mouse pressed on the child still bubbles to the row first.

---

### 3.2 HIGH — must fix

#### H1. `SetInitiallyFocusedView` does not work with `SetCanActivate(false)`

**File:** `maho_sidebar_folder_hover_popup_view.cc:222-224`

**Problem:** Initial focus is logically set on the search field, but the widget cannot activate, so the search field never receives actual keyboard focus. User has to click the search field to type — defeats the auto-focus purpose.

**Fix:** Drop the misleading line and document the intent. Users will click the search field if they want to type — this matches `MahoTabPreviewController` UX (hover preview is read-only by default).

```cpp
// Remove these lines from popup_view.cc::Show():
//   views::Textfield* search_ptr = contents->search_field();
//   delegate->SetInitiallyFocusedView(search_ptr);
```

If keyboard-immediate-search is required (post-merge enhancement), wire `RequestFocus()` on first `OnMouseEntered` of the popup view content area, NOT at show time.

---

#### H2. `kSearchOnly` mode is unreachable dead code — DECISION CONFIRMED

**Files:** `maho_sidebar_tab_list_view.cc:1893, 2791-2799`, `popup_view.h:33`, `popup_view.cc:437-470, 572-578`, `controller.{h,cc}`

**Problem:** The `!is_expanded_` guard at row entry skips popup for expanded folders. The mode logic still computes `kSearchOnly` for expanded folders, but that path is never reached because the popup never shows. Additionally, the original "Bug 3 = 검색 아이콘만 나옴" was misdiagnosed: the user actually wants the **list to appear**, not the search-only mode.

**Confirmed user intent (2026-06-01):**
- Folder with children (collapsed OR expanded) → show full list popup
- Empty folder → no popup at all
- `kSearchOnly` mode was the source of "search icon only" symptom and is unwanted

**Decision: Combo C — show popup for all non-empty folders with full list, delete `kSearchOnly`.**

This gives:
- Closed non-empty folder hover → full list popup ✓
- Expanded non-empty folder hover → full list popup ✓ (lets user search even when inline list is long)
- Empty folder hover → no popup ✓

**Changes:**

**`tab_list_view.cc:1893`** — drop `!is_expanded_`:
```cpp
if (show_popup_callback_ && !children_snapshot_.empty()) {
  show_popup_callback_.Run();
}
```

**`tab_list_view.cc:2791-2799`** — drop the mode ternary; ScheduleShow no longer takes a mode parameter:
```cpp
folder_row->SetHoverPopupCallbacks(
    base::BindRepeating(&MahoSidebarFolderHoverController::ScheduleShow,
                        folder_hover_controller_->AsWeakPtr(),
                        folder_row, node.folder_id, node.folder_name),
    base::BindRepeating(&MahoSidebarFolderHoverController::OnRowMouseExited,
                        folder_hover_controller_->AsWeakPtr()));
```

**`popup_view.h`** — delete the enum and `Mode` parameter:
```cpp
// REMOVE: enum class Mode { kFullList, kSearchOnly };

static views::Widget* Show(views::View* anchor_view,
                           Browser* browser,
                           const std::u16string& folder_name,
                           std::string folder_id,
                           std::vector<SidebarTreeNode> children,
                           ActivateTabCallback on_activate);

MahoSidebarFolderHoverPopupView(Browser* browser,
                                std::u16string folder_name,
                                std::string folder_id,
                                std::vector<SidebarTreeNode> children,
                                ActivateTabCallback on_activate);
```

Drop `Mode mode_;` member.

**`popup_view.cc`** — simplify:
- Remove `mode` parameters from constructor and `Show`.
- `ApplyFilter` empty-query branch becomes unconditional fill:
  ```cpp
  if (query.empty()) {
    for (size_t i = 0; i < children.size(); ++i) {
      filtered_indices_.push_back(i);
    }
  } else {
    /* substring match unchanged */
  }
  ```
- `ShouldRenderList()` becomes `return true;` — can be inlined / removed at call site.

**`controller.h`** — drop `MahoSidebarFolderHoverPopupView::Mode` references:
```cpp
void ScheduleShow(views::View* anchor,
                  std::string folder_id,
                  std::u16string folder_name);
// drop: MahoSidebarFolderHoverPopupView::Mode pending_mode_
```

**`controller.cc::ShowNow`** — pass no mode to `Show`.

**Affected lines summary:** ~50 lines deleted across 5 files. All mechanical.

---

#### H3. **REGRESSION** — 4 px anchor gap fix reverted (commented out)

**File:** `popup_view.cc:49, 213-216`

**Problem:**
```cpp
// constexpr int kPopupAnchorGapDp = 4;
...
// delegate->SetAnchorViewInsets(            ← typo: should be set_anchor_view_insets (snake_case)
//     base::i18n::IsRTL()
//         ? gfx::Insets::TLBR(0, 0, 0, -kPopupAnchorGapDp)
//         : gfx::Insets::TLBR(0, -kPopupAnchorGapDp, 0, 0));
```

The 3rd-pass R1 fix is commented out. Worse, the commented version uses `SetAnchorViewInsets` (CamelCase) which is not the actual API — the correct name is `set_anchor_view_insets` (snake_case). Even if uncommented as-is, it would not compile. Likely reason for the comment-out: build failure due to wrong API name.

**Fix:**
1. Uncomment `kPopupAnchorGapDp` constant on line 49.
2. Restore the call with the **correct** API name `set_anchor_view_insets`:

```cpp
constexpr int kPopupAnchorGapDp = 4;

// in Show():
delegate->set_anchor_view_insets(
    base::i18n::IsRTL()
        ? gfx::Insets::TLBR(0, 0, 0, -kPopupAnchorGapDp)
        : gfx::Insets::TLBR(0, -kPopupAnchorGapDp, 0, 0));
```

**Couples with M7 (must do together):** Restoring the gap means the cursor traversing from row to popup briefly leaves both widget bounds. Without M7, `IsCursorInsidePopup` returns false during traversal → grace timer fires → popup tears down mid-traversal. See M7 below.

---

### 3.3 MEDIUM

#### M1. Hide-grace polling timer never terminates while cursor inside popup

**File:** `controller.cc:150-159`

**Problem:** `OnHideGraceTimerFired` self-rearms every 150 ms forever while cursor is over the popup. 6.7 Hz wakeup. Battery impact + risk of stuck timer if popup goes orphan.

**Fix:** Cap the rearm count (defensive) AND add event-driven dismissal where possible.

**Add to `controller.h`:**
```cpp
private:
  static constexpr int kMaxGraceRearms = 10;  // 10 × 150ms = 1.5s cap
  int grace_rearm_count_ = 0;
```

**`OnHideGraceTimerFired`:**
```cpp
void OnHideGraceTimerFired() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsCursorInsidePopup() && grace_rearm_count_ < kMaxGraceRearms) {
    ++grace_rearm_count_;
    hide_grace_timer_.Start(FROM_HERE, base::Milliseconds(kHideGraceMs),
                            base::BindOnce(&MahoSidebarFolderHoverController::OnHideGraceTimerFired,
                                           weak_factory_.GetWeakPtr()));
    return;
  }
  HideImmediately();
}
```

**`OnRowMouseExited`** — reset the counter:
```cpp
void OnRowMouseExited() {
  ...
  grace_rearm_count_ = 0;
  hide_grace_timer_.Start(...);
}
```

Also reset on `ShowNow` and `HideImmediately`.

---

#### M3. Magic numbers escape constants

**Files:** `controller.cc:57, 65, 153`, `popup_view.cc:515, 61, 394`

**Fix:** Extract named constants in anonymous namespaces:

**`controller.cc` (top of file):**
```cpp
namespace {
constexpr base::TimeDelta kHoverShowDelay = base::Milliseconds(225);
constexpr base::TimeDelta kHideGraceMs = base::Milliseconds(150);
}
```
Use throughout.

**`popup_view.cc` (existing constexpr block):**
```cpp
constexpr int kMaxNavDepth = 10;
constexpr int kRowHorizontalInset = 8;  // matches gfx::Insets::VH(_, 8) on row layout
```
Replace `>= 10` and `kPopupWidth - 16` (which is `kPopupWidth - 2 * kRowHorizontalInset`).

---

#### M4. `OnRowClicked` uses `base::Unretained(this)` — fragile if Widget::Close becomes sync

**Files:** `popup_view.h`, `popup_view.cc:111-117, 422-425`

**Problem:** `ItemRow::click_cb_` captures `Unretained(this)` (popup view). The popup view is destroyed asynchronously via `Widget::Close()` after `activate_callback_.Run`. If anyone changes Close to sync (or ports to a platform where Close is sync), it becomes UAF.

**Fix:** Add a `WeakPtrFactory<MahoSidebarFolderHoverPopupView>` and use it for the row callbacks.

**`popup_view.h`** — add at end of class:
```cpp
private:
  ...
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MahoSidebarFolderHoverPopupView> weak_factory_{this};
```

**`popup_view.cc:422-425`** — replace `base::Unretained(this)` with `weak_factory_.GetWeakPtr()`:
```cpp
auto* row = rows_container_->AddChildView(std::make_unique<ItemRow>(
    icon, display_name, is_folder, static_cast<int>(i),
    base::BindRepeating(&MahoSidebarFolderHoverPopupView::OnRowHovered,
                        weak_factory_.GetWeakPtr()),
    base::BindRepeating(&MahoSidebarFolderHoverPopupView::OnRowClicked,
                        weak_factory_.GetWeakPtr())));
```

`OnRowHovered` and `OnRowClicked` will become no-op if `this` is gone before callback fires (correct behavior).

---

#### M7. `IsCursorInsidePopup` needs slack region for the 4 px gap (couples with H3)

**File:** `controller.cc:208-227`

**Problem:** When the 4 px gap is restored (H3), the cursor traversing from anchor row → popup briefly leaves both widget bounds. `IsCursorInsidePopup` returns false during traversal → next grace fire dismisses popup mid-cursor-traversal. Bad UX.

**Fix:** Treat anchor + popup as a single hit region with the gap inflated:

```cpp
bool MahoSidebarFolderHoverController::IsCursorInsidePopup() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!popup_widget_) {
    return false;
  }

  gfx::Point cursor_point = display::Screen::Get()->GetCursorScreenPoint();

  // Popup bounds inflated leftward (rightward in RTL) by gap to cover the
  // dead-space between anchor and popup.
  gfx::Rect popup_bounds = popup_widget_->GetWindowBoundsInScreen();
  if (base::i18n::IsRTL()) {
    popup_bounds.Inset(gfx::Insets::TLBR(0, 0, 0, -kPopupAnchorGapDp));
  } else {
    popup_bounds.Inset(gfx::Insets::TLBR(0, -kPopupAnchorGapDp, 0, 0));
  }
  if (popup_bounds.Contains(cursor_point)) {
    return true;
  }

  const views::View* anchor = anchor_tracker_.view();
  if (anchor && anchor->GetWidget()) {
    if (anchor->GetBoundsInScreen().Contains(cursor_point)) {
      return true;
    }
  }
  return false;
}
```

`kPopupAnchorGapDp` must be available here (move to a shared header or duplicate the constant in controller.cc).

---

### 3.4 LOW

#### L1. Hard-coded user-facing strings → l10n

**File:** `popup_view.cc:276, 306, 390-391, 416, 524, 540`

Strings: `"Back"`, `"Search …"` placeholder pattern, `"No tabs in this folder"`, `"No matching results"`, `"Untitled tab"`.

**Fix:** Add string IDs to `chrome/app/maho_strings.grdp` (or whichever .grdp Maho uses for sidebar strings — consult AGENTS.md). Resolve via `l10n_util::GetStringUTF16(IDS_MAHO_SIDEBAR_FOLDER_HOVER_BACK)` etc.

For the "Search <name>..." pattern with substitution:
```cpp
search_field->SetPlaceholderText(
    l10n_util::GetStringFUTF16(IDS_MAHO_SIDEBAR_FOLDER_HOVER_SEARCH_PLACEHOLDER,
                               nav_stack_.back().name));
```

Confirm i18n policy with project owner if l10n keys aren't set up yet — may be acceptable to leave as English literals if other Maho features do the same. If so, document and skip.

---

#### L2. Missing include for `content::WebContents`

**File:** `popup_view.cc:164`

`content::WebContents*` is used but not directly included. Currently works via transitive include from `tab_strip_model.h`, fragile.

**Fix:** Add to includes block:
```cpp
#include "content/public/browser/web_contents.h"
```

---

#### L3. `ItemRow` lacks `SEQUENCE_CHECKER`

**File:** `popup_view.cc:51-140`

For consistency with other classes in the file. Low priority.

**Fix:** Add to `ItemRow` private section:
```cpp
SEQUENCE_CHECKER(sequence_checker_);
```
And `DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_)` in public methods.

---

## 4. Execution Order

Apply in this order — earlier fixes have lower risk and unblock later ones:

| # | Item | Estimated time | Risk |
|---|---|---|---|
| 1 | **L2** missing include | 30 sec | None |
| 2 | **M3** magic number constants | 10 min | None |
| 3 | **H3 + M7** restore 4px gap + slack region (must do together) | 15 min | Build can fail if API name still wrong — verify with `set_anchor_view_insets` |
| 4 | **C2** cancel timer on press | 10 min | Need to verify `OnMousePressed` returning false propagates to `label_button_` |
| 5 | **H1** drop SetInitiallyFocusedView | 1 min | None |
| 6 | **H2** delete `kSearchOnly` + remove `!is_expanded_` guard (Combo C) | 30 min | 5 files mechanical deletion |
| 7 | **C1** focus-loss observer | 30 min | New WidgetObserver pattern; verify on macOS |
| 8 | **M1** polling cap | 10 min | None |
| 9 | **M4** WeakPtrFactory | 5 min | None |
| 10 | **L3** SEQUENCE_CHECKER on ItemRow | 5 min | None |
| 11 | **L1** i18n strings | depends on policy decision | None |

Total: ~2 hours sequential, less if some items skipped (L1, L3).

---

## 5. Verification

After each batch:
1. `python3 maho-chromium/build/scripts/build_maho.py` — must succeed
2. Manual QA scenarios (priority ordered):
   - **Cmd+Tab while popup is showing** — popup should hide (C1 verification)
   - **Hover folder, click within 225 ms** — folder toggles, no orphan popup (C2 verification)
   - **Hover closed non-empty folder** — popup appears with 4 px visible gap from row (H3 verification)
   - **Move cursor slowly from row through gap into popup** — popup does NOT briefly hide and reappear (M7 verification)
   - **Hover expanded folder with children** — full-list popup appears (Combo C)
   - **Hover empty folder (collapsed or expanded)** — no popup at all (Bug 4 verification)
   - **Click search field, type Korean** — characters appear (no longer requires auto-focus to work)
   - **Park cursor on popup for 30 seconds** — no excessive CPU wake; popup eventually self-dismisses on focus loss
   - **Click tab in popup** — tab activates, popup dismisses, no UAF / no crash
   - **Resize browser, then hover folder** — popup positions correctly relative to row
   - **RTL locale** — popup opens to the left of the row with the gap on the correct side

---

## 6. Plan Updates Required

The original plan `sidebar-folder-hover-popup-plan-2026-05-31.md` had `kSearchOnly` mode for expanded folders. Decision Combo C deprecates this:

- §1 User Story: remove "Expanded folder — search-only mode" paragraph. Both states show full list when non-empty.
- §5.1 show conditions: remove the `kFullList` vs `kSearchOnly` mode decision logic.
- §5.4 search behavior: remove the `kSearchOnly` mode bullet.
- §10 Decision #1: original answer was "펼쳐진 폴더는 검색 전용 모드". Updated answer: "펼쳐진/닫힌 무관, 비어있지 않으면 풀 리스트 popup; 비어있으면 popup 없음."

The C2 fix (cancel timer on press) is **especially critical with Combo C** because both expand and collapse clicks can race against the 225 ms timer and produce ghost popups.

---

## 7. Risks & Open Questions

| Risk | Mitigation |
|---|---|
| `OnMousePressed` returning false on `SidebarFolderRowView` may not propagate to `label_button_` if Views' event-routing differs from expected | If toggle still breaks after C2, override `OnMousePressed` to call hide callback then return `views::View::OnMousePressed(event)` (which delegates) |
| `WidgetObserver` on browser widget may double-register if controller is reused for multiple folders | Add idempotent register guard: `if (observed_browser_widget_ != target_widget) { ... }` |
| `set_anchor_view_insets` semantics for negative right inset on `BubbleBorder::RIGHT_TOP` may not work as expected on all Chromium milestones | Test in actual build; if popup doesn't shift right, fall back to manually adjusting popup widget bounds via `Widget::SetBounds` after Show |
| 1.5 s polling cap from M1 may be too short for users who want to dwell on popup | Tune via manual QA; can increase to 5 s if needed |

**Open question:** ~~Confirm H2 decision before starting (Option A delete vs Option B restore).~~ **CONFIRMED 2026-06-01: Combo C** — show popup for all non-empty folders (expanded or collapsed), full list mode only, delete `kSearchOnly`.

---

## 8. Out of Scope

- Auto-scroll-during-drag system, SidebarSpaceHeaderRow, ScrollView wrapping, cycle_space_cache, IsSidebarIneligibleUrl removal, loading_ field removal — all the piggyback changes noted in earlier reviews. Should be split into separate commits but not addressed by this plan.
- Adding tests (interactive_test for the hover popup) — follow-up.
- Animation polish (fade-in / slide-in for popup) — follow-up.
- Recursive search across nav levels — explicitly v2 per original plan §7.
