// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_CONTROLLER_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class GURL;
class MahoPeekView;

namespace content {
class WebContents;
}

namespace views {
class FocusManager;
class View;
class Widget;
}  // namespace views

namespace maho {

// Peek is available only in ordinary tabbed windows backed by a regular
// profile or its primary Incognito profile. Guest, system, app, DevTools, and
// popup windows are deliberately excluded.
bool IsPeekEligible(Browser* browser);

// Returns true when every non-null WebContents belongs to the eligible
// browser's exact BrowserContext. A null source is valid for manual opens.
bool IsPeekContentsContextCompatible(Browser* browser,
                                     content::WebContents* source,
                                     content::WebContents* contents);

// Owns the single transient Peek slot for one browser window. The slot is
// reserved synchronously before WebContents creation or adoption, and remains
// occupied until all widget and WebContents teardown has completed.
class MahoPeekController : public views::WidgetObserver,
                           public ui::AcceleratorTarget {
 public:
  enum class State {
    Closed,
    Opening,
    Open,
    Closing,
  };

  MahoPeekController(Browser* browser,
                     views::Widget* parent_widget,
                     base::RepeatingClosure dismiss_other_overlays);
  MahoPeekController(const MahoPeekController&) = delete;
  MahoPeekController& operator=(const MahoPeekController&) = delete;
  ~MahoPeekController() override;

  content::WebContents* ShowUrl(content::WebContents* source, const GURL& url);
  bool TryAdoptContents(content::WebContents* source,
                        std::unique_ptr<content::WebContents>* contents);
  void Hide();
  void PromoteToTab();
  bool SlotBusy() { return state_ != State::Closed; }

  State state_for_testing() const { return state_; }
  views::Widget* widget_for_testing() const { return widget_.get(); }
  MahoPeekView* peek_view_for_testing() const { return peek_view_; }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

 private:
  class HostedContentsObserver;
  class SourceTabObserver;

  bool ReserveSlot(content::WebContents* source,
                   content::WebContents* contents_to_adopt);
  void ShowReserved(std::unique_ptr<MahoPeekView> peek_view,
                    content::WebContents* hosted_contents);
  void OnHostedContentsDestroyed();
  void OnSourceContentsDestroyed();
  void OnScrimPressed();
  void UpdateBounds();
  void RegisterParentEscAccelerator();
  void UnregisterParentEscAccelerator();
  void RestoreFocus();
  void FinishUnexpectedWidgetTeardown();

  const raw_ptr<Browser> browser_;
  raw_ptr<views::Widget> parent_widget_ = nullptr;
  base::RepeatingClosure dismiss_other_overlays_;
  State state_ = State::Closed;

  std::unique_ptr<views::Widget> widget_;
  // Retains a widget whose native teardown began outside Hide() until cleanup
  // can safely run after the observer callback unwinds.
  std::unique_ptr<views::Widget> closing_widget_;
  raw_ptr<MahoPeekView> peek_view_ = nullptr;
  std::unique_ptr<HostedContentsObserver> hosted_contents_observer_;
  std::unique_ptr<SourceTabObserver> source_tab_observer_;

  views::ViewTracker focus_restore_tracker_;
  raw_ptr<views::FocusManager> esc_target_focus_manager_ = nullptr;
  bool tearing_down_ = false;

  base::WeakPtrFactory<MahoPeekController> weak_factory_{this};
  base::WeakPtrFactory<MahoPeekController> teardown_weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_PEEK_MAHO_PEEK_CONTROLLER_H_
