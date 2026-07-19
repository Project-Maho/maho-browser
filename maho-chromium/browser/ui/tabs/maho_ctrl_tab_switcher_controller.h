// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CONTROLLER_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CONTROLLER_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
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
//          └─ mru.size() >= 3 → snapshot MRU, cursor=1, start 200ms timer,
//                               install EventMonitor, state=kPendingShow
//
//   kPendingShow (< 200 ms after first Ctrl+Tab)
//     ├─ Ctrl release       → commit MRU[cursor], reset, state=kIdle
//     ├─ Tab keydown        → cursor advance, open overlay immediately,
//     │                        state=kShowing
//     └─ 200ms timer fires  → open overlay, state=kShowing
//
//   kShowing (overlay visible)
//     ├─ Tab keydown        → advance cursor
//     ├─ Ctrl release       → commit MRU[cursor], close overlay,
//     │                        state=kIdle
//     └─ Escape             → cancel (no commit), close overlay, state=kIdle
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
  bool forward_ = true;
  base::TimeDelta show_delay_ = base::Milliseconds(200);
  size_t max_visible_ = 0;

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
