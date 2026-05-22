// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_bubble_coordinator.h"

#include <map>
#include <memory>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/location_bar/location_bar_bubble_delegate_view.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/views/boost/maho_boost_bubble_view.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

namespace maho {

namespace {

std::map<Browser*, std::unique_ptr<MahoBoostBubbleCoordinator>>&
GetCoordinatorMap() {
  static base::NoDestructor<
      std::map<Browser*, std::unique_ptr<MahoBoostBubbleCoordinator>>>
      map;
  return *map;
}

}  // namespace

MahoBoostBubbleCoordinator& MahoBoostBubbleCoordinator::GetForBrowser(
    Browser* browser) {
  DCHECK(browser);
  auto& map = GetCoordinatorMap();
  auto it = map.find(browser);
  if (it == map.end()) {
    auto coordinator = std::make_unique<MahoBoostBubbleCoordinator>();
    coordinator->browser_ = browser;
    TabStripModel* tab_strip = browser->tab_strip_model();
    DCHECK(tab_strip);
    if (tab_strip) {
      // Registration on TabStripModel is the authoritative lifetime mechanism:
      // OnTabStripModelDestroyed() fires during normal browser teardown and
      // self-destructs this coordinator by erasing it from the map.  The
      // defensive |if (tab_strip)| above cannot leak silently because DCHECK
      // would fire first in debug builds; in release builds a missing
      // tab_strip means the coordinator simply stays in the map until process
      // exit, which is harmless given the NoDestructor storage.
      tab_strip->AddObserver(coordinator.get());
    }
    it = map.emplace(browser, std::move(coordinator)).first;
  }
  return *it->second;
}

MahoBoostBubbleCoordinator::MahoBoostBubbleCoordinator() = default;

MahoBoostBubbleCoordinator::~MahoBoostBubbleCoordinator() {
  if (bubble_) {
    widget_observation_.Reset();
    views::Widget* widget = bubble_->GetWidget();
    bubble_ = nullptr;
    if (widget && !widget->IsClosed()) {
      widget->CloseNow();
    }
  }
}

void MahoBoostBubbleCoordinator::Show(Browser* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!browser) {
    return;
  }
  BrowserWindow* window = browser->window();
  if (!window) {
    return;
  }
  LocationBar* location_bar = window->GetLocationBar();
  if (!location_bar) {
    return;
  }
  ui::TrackedElement* tracked_element = location_bar->GetAnchorOrNull();
  if (!tracked_element) {
    return;
  }
  views::View* anchor_view =
      tracked_element->AsA<views::TrackedElementViews>()->view();
  if (!anchor_view) {
    return;
  }
  content::WebContents* active_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  Show(anchor_view, browser, active_contents);
}

void MahoBoostBubbleCoordinator::Show(
    views::View* anchor_view,
    Browser* browser,
    content::WebContents* target_web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsShowing()) {
    // A direct Show() call with an existing bubble is treated as a replace, not
    // a toggle.  We close the old bubble synchronously here without notifying
    // on_close_callbacks_: the caller is immediately creating a replacement, so
    // any subscriber would receive a spurious "close" event mid-replace.
    // Subscribers that need unconditional close notification must observe widget
    // destruction via RegisterOnCloseCallback() on the new bubble.
    widget_observation_.Reset();
    content::WebContentsObserver::Observe(nullptr);
    views::Widget* old_widget = bubble_->GetWidget();
    bubble_ = nullptr;
    anchor_view_tracker_.SetView(nullptr);
    if (old_widget && !old_widget->IsClosed()) {
      old_widget->Close();
    }
  }

  if (!anchor_view || !browser) {
    return;
  }

  anchor_view_tracker_.SetView(anchor_view);

  content::WebContents* bubble_contents = target_web_contents;
  if (!bubble_contents) {
    bubble_contents = browser->tab_strip_model()->GetActiveWebContents();
  }
  if (!bubble_contents) {
    return;
  }

  auto bubble = std::make_unique<MahoBoostBubbleView>(
      anchor_view, bubble_contents, browser, target_web_contents);
  // Same-origin main-frame navigations should not dismiss the bubble just
  // because focus returns to the page during navigation. Cross-origin teardown
  // remains coordinator-driven via DidFinishNavigation().
  bubble->set_close_on_deactivate(false);
  bubble_ = bubble.get();

  views::Widget* widget =
      views::BubbleDialogDelegateView::CreateBubble(std::move(bubble));
  widget_observation_.Observe(widget);

  if (target_web_contents) {
    content::WebContentsObserver::Observe(target_web_contents);
    observed_origin_ =
        url::Origin::Create(target_web_contents->GetLastCommittedURL());
  } else {
    observed_origin_ = url::Origin();
  }

  bubble_->ShowForReason(LocationBarBubbleDelegateView::USER_GESTURE);
}

void MahoBoostBubbleCoordinator::Hide() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsShowing()) {
    return;
  }
  views::Widget* widget = bubble_->GetWidget();
  if (widget && !widget->IsClosed()) {
    widget->Close();
  }
}

bool MahoBoostBubbleCoordinator::IsShowing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return bubble_ != nullptr;
}

views::Widget* MahoBoostBubbleCoordinator::GetBubbleWidgetForTesting() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return bubble_ ? bubble_->GetWidget() : nullptr;
}

base::CallbackListSubscription
MahoBoostBubbleCoordinator::RegisterOnCloseCallback(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return on_close_callbacks_.Add(std::move(callback));
}

void MahoBoostBubbleCoordinator::OnWidgetDestroying(views::Widget* widget) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  widget_observation_.Reset();
  content::WebContentsObserver::Observe(nullptr);
  observed_origin_ = url::Origin();
  bubble_ = nullptr;

  views::View* anchor = anchor_view_tracker_.view();
  anchor_view_tracker_.SetView(nullptr);
  if (anchor && anchor->GetWidget()) {
    // Defer focus restore: during synchronous widget destruction the browser
    // window layers may be temporarily invisible, making IsFocusable() return
    // false. Posting to the current thread runs after the destroy unwinds.
    auto tracker = std::make_shared<views::ViewTracker>(anchor);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::shared_ptr<views::ViewTracker> t) {
              views::View* v = t->view();
              if (!v || !v->GetWidget()) {
                return;
              }
              views::FocusManager* fm = v->GetWidget()->GetFocusManager();
              if (fm && fm->ContainsView(v)) {
                fm->SetFocusedView(v);
              }
            },
            std::move(tracker)));
  }

  on_close_callbacks_.Notify();
}

void MahoBoostBubbleCoordinator::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsShowing()) {
    return;
  }
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument()) {
    return;
  }
  url::Origin new_origin =
      url::Origin::Create(navigation_handle->GetURL());
  if (!new_origin.IsSameOriginWith(observed_origin_)) {
    Hide();
  }
}

void MahoBoostBubbleCoordinator::WebContentsDestroyed() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContentsObserver::Observe(nullptr);
  if (IsShowing()) {
    Hide();
  }
}

void MahoBoostBubbleCoordinator::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus /*status*/) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContentsObserver::Observe(nullptr);
  if (IsShowing()) {
    Hide();
  }
}

void MahoBoostBubbleCoordinator::OnTabStripModelChanged(
    TabStripModel* /*tab_strip_model*/,
    const TabStripModelChange& /*change*/,
    const TabStripSelectionChange& selection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsShowing()) {
    return;
  }
  if (selection.active_tab_changed()) {
    Hide();
  }
}

void MahoBoostBubbleCoordinator::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tab_strip_model->RemoveObserver(this);
  if (IsShowing()) {
    // Call Hide() to initiate an async widget close.  We intentionally do NOT
    // notify on_close_callbacks_ from here: the coordinator and its owning
    // browser are both being torn down, so any subscriber that cares about the
    // bubble going away as a user-visible event will never need to act on this
    // destruction-driven close.  Subscribers that require teardown notification
    // regardless must observe browser destruction separately.
    Hide();
  }
  // Self-destruct by transferring ownership out of the map via the stored
  // browser_ key (O(1)).  Hide() above posts an async widget close, so the
  // widget may still be alive here; the destructor calls CloseNow() to flush
  // it.  No re-entrant calls into this coordinator can occur after
  // RemoveObserver() above and after the map entry is erased.
  auto& map = GetCoordinatorMap();
  auto it = map.find(browser_);
  if (it != map.end()) {
    std::unique_ptr<MahoBoostBubbleCoordinator> self =
        std::move(it->second);
    map.erase(it);
  }
}

}  // namespace maho
