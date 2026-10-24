// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CONTROLLER_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CONTROLLER_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_slot_model.h"
#include "ui/events/event_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class TabStripModel;

namespace content {
class WebContents;
}

namespace views {
class EventMonitor;
class Widget;
}  // namespace views

namespace maho {

class MahoCtrlTabSwitcherView;
class MahoMruTabTracker;

// Owns the lifecycle of the Ctrl+Tab MRU switcher overlay.
//
// State machine (see Zen/Firefox `ctrlTab` for reference):
//
//   kIdle
//     └─ HandleAdvance()
//          ├─ mru.size() < 2 → return false (falls through to positional)
//          ├─ mru.size() == 2 → instant flip, no overlay, stays kIdle
//          └─ mru.size() >= 3 → snapshot MRU, cursor=1 forward / MRU.back()
//                               backward (clamped to the visible cards when
//                               the overlay opens), start 200ms timer,
//                               install EventMonitor, state=kPendingShow
//
//   kPendingShow (< 200 ms after first Ctrl+Tab)
//     ├─ Ctrl release       → commit MRU[cursor], reset, state=kIdle
//     ├─ Tab keydown        → cursor advance, open overlay immediately,
//     │                        state=kShowing
//     ├─ Escape / outside click → cancel (no commit), state=kIdle
//     └─ 200ms timer fires  → open overlay, state=kShowing
//
//   kShowing (overlay visible)
//     ├─ Tab keydown        → advance cursor
//     ├─ Ctrl release       → commit MRU[cursor], close overlay,
//     │                        state=kIdle
//     ├─ Click on a card    → commit that card
//     ├─ Click outside      → cancel (no commit), close overlay, state=kIdle
//     └─ Escape             → cancel (no commit), close overlay, state=kIdle
//
// Tabs closed while a session is active are pruned from the snapshot and the
// overlay's cards are rebuilt. The visible card count and the per-card width
// are fitted to the browser window width so the overlay never overflows the
// window.
//
// Backward direction (Ctrl+Shift+Tab): a quick backward press commits the last
// snapshot entry (MRU.back(), the least-recently-used tab), the way Alt+Tab's
// Shift direction wraps to the end of the list. That is the MRU meaning of
// "backward" and does not depend on how many cards happen to fit the window.
// The commit target is tracked as an explicit pending state
// (pending_mru_index_), not inferred from the index, because the index can
// coincide with the "+N more" slot index - backward when exactly one tab is
// hidden, forward whenever only one card is visible (index 1 IS the more slot
// then) - and would otherwise open the command overlay instead of activating
// a tab. If the overlay opens, the selection enters the slot ring at the last
// visible card (that press is the one that brings it into view) and the
// commit follows the highlighted card from then on.
//
// (History: an earlier revision moved the backward start to the last visible
// card, which silently turned a quick backward press into a commit of MRU[6]
// instead of MRU.back(). That regression is fixed by the wrap-to-end commit
// plus the explicit pending state described above.)
//
// The controller does not own the tracker or the browser view; it holds
// non-owning pointers whose lifetime is guaranteed by BrowserView's
// initialization order (tracker created first, controller after; both live
// as long as BrowserView).
class MahoCtrlTabSwitcherController : public views::WidgetObserver,
                                       public ui::EventObserver,
                                       public TabStripModelObserver {
 public:
  MahoCtrlTabSwitcherController(BrowserView* browser_view,
                                MahoMruTabTracker* tracker);
  MahoCtrlTabSwitcherController(const MahoCtrlTabSwitcherController&) = delete;
  MahoCtrlTabSwitcherController& operator=(
      const MahoCtrlTabSwitcherController&) = delete;
  ~MahoCtrlTabSwitcherController() override;

  bool HandleAdvance(bool forward);

  base::TimeDelta show_delay_for_testing() const { return show_delay_; }
  void SetShowDelayForTesting(base::TimeDelta delay) { show_delay_ = delay; }
  bool IsOverlayVisibleForTesting() const;
  size_t cursor_for_testing() const { return cursor_; }
  size_t snapshot_size_for_testing() const { return snapshot_.size(); }

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  enum class State { kIdle, kPendingShow, kShowing };

  void StartPendingShow(bool forward);
  void OnShowTimerFired();
  void OpenOverlay();
  void AdvanceCursor(bool forward);
  // Brings a cursor that does not yet name the user's selection onto the
  // last visible card: a pending backward wrap target past the slot ring, or
  // the forward initial cursor sitting on the "+N more" slot when only one
  // card is visible. A cursor deliberately stepped or hovered onto the
  // "+N more" slot is left alone (releasing Ctrl there intentionally opens
  // the command palette).
  void NormalizeCursorToSlots();
  void CommitCurrent();
  void CancelWithoutCommit();

  // Moves the highlighted selection to the hovered slot (keeping the keyboard
  // cursor in sync). Wired as the overlay view's hover callback.
  void HandleSlotHovered(size_t index);

  // Commits the slot under |screen_point| if the point hits a card or the
  // "+N more" affordance. Called from OnEvent for the application-scoped mouse
  // monitor: on macOS the non-activatable inactive overlay window never
  // receives mouseDown through the normal responder path, so clicks are
  // captured process-wide and hit-tested here instead.
  void HandleMousePressAt(const gfx::Point& screen_point);

  // Builds a fresh overlay view from the live snapshot and current cursor.
  std::unique_ptr<MahoCtrlTabSwitcherView> CreateOverlayView() const;
  // Replaces the overlay's contents view after the snapshot or the fitted
  // card count changed, then re-centers the overlay.
  void RebuildOverlayView();
  // The max-visible pref reduced to a card count and per-card width that fit
  // the current browser window width.
  void FitCardsToWindow();
  // Number of tab cards shown, and whether a "+N more" slot follows them.
  size_t VisibleCardCount() const;
  bool HasMoreSlot() const;

  // Clamps the cursor after the fitted card count changed.  A cursor that sat
  // on the "+N more" slot stays there when the slot still exists, and a
  // pending backward commit stays on the last snapshot entry; otherwise the
  // cursor clamps to the last visible card, never onto a freshly created more
  // slot -- releasing Ctrl there would open the command overlay instead of a
  // tab.
  size_t ClampCursorAfterRefit(bool was_on_more_slot) const;
  // The slot model implied by the current snapshot and fitted card count.
  ctrl_tab_slots::SlotModel CurrentSlotModel() const;

  void CloseOverlay();
  void Reset();
  void InstallEventMonitor();
  void RemoveEventMonitor();
  void UpdateOverlayBounds();

  size_t EffectiveSlotCount() const;
  void ObserveParentWidget();
  void StopObservingParentWidget();

  raw_ptr<BrowserView> browser_view_;
  raw_ptr<MahoMruTabTracker> tracker_;

  State state_ = State::kIdle;

  // Weak references so a tab closing during the overlay/pending-show window
  // does not create a use-after-free. Dead entries are skipped when
  // committing.
  std::vector<base::WeakPtr<content::WebContents>> snapshot_;
  size_t cursor_ = 0;
  // Set on the first Ctrl+Tab press of a session: the snapshot index a quick
  // Ctrl release commits (forward -> 1, backward -> the last entry, the MRU
  // wrap-to-end convention). Tracked explicitly instead of inferred from the
  // cursor index because that index can coincide with the "+N more" slot - at
  // a single visible card the forward target (1) IS the more slot index, and
  // committing it through the slot ring would open the command palette
  // instead of switching tabs. Cleared as soon as the cursor becomes a slot
  // selection (overlay open, Tab step, hover, click); the ring cursor is
  // display-only while this is set.
  std::optional<size_t> pending_mru_index_;
  bool forward_ = true;
  base::TimeDelta show_delay_ = base::Milliseconds(200);
  size_t max_visible_ = 0;
  // Per-card width fitted to the window width; 0 means the natural width.
  int card_width_ = 0;

  base::OneShotTimer show_timer_;
  std::unique_ptr<views::Widget> overlay_widget_;
  raw_ptr<MahoCtrlTabSwitcherView> overlay_view_ = nullptr;
  raw_ptr<views::Widget> observed_parent_widget_ = nullptr;
  std::unique_ptr<views::EventMonitor> event_monitor_;
  std::unique_ptr<views::EventMonitor> mouse_event_monitor_;

  base::WeakPtrFactory<MahoCtrlTabSwitcherController> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CONTROLLER_H_
