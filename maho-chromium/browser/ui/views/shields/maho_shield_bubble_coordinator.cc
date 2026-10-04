// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/shields/maho_shield_bubble_coordinator.h"

#include <cstdint>
#include <map>
#include <memory>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/net/maho_ad_block_tab_helper.h"
#include "maho/browser/net/maho_shield_site_state.h"
#include "maho/browser/net/maho_shield_site_state_factory.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

namespace maho {

namespace {

std::map<Browser*, std::unique_ptr<MahoShieldBubbleCoordinator>>&
GetCoordinatorMap() {
  static base::NoDestructor<
      std::map<Browser*, std::unique_ptr<MahoShieldBubbleCoordinator>>>
      map;
  return *map;
}

content::WebContents* GetActiveWebContents(Browser* browser) {
  if (!browser) {
    return nullptr;
  }

  TabStripModel* tab_strip = browser->GetTabStripModel();
  return tab_strip ? tab_strip->GetActiveWebContents() : nullptr;
}

uint32_t GetBlockedCount(content::WebContents* web_contents) {
  if (!web_contents) {
    return 0;
  }

  auto* helper = MahoAdBlockTabHelper::FromWebContents(web_contents);
  return helper ? helper->blocked_count() : 0;
}

MahoShieldSiteState* GetShieldSiteState(content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }
  return MahoShieldSiteStateFactory::GetForProfile(
      Profile::FromBrowserContext(web_contents->GetBrowserContext()));
}

}  // namespace

MahoShieldBubbleCoordinator& MahoShieldBubbleCoordinator::GetForBrowser(
    Browser* browser) {
  DCHECK(browser);
  auto& map = GetCoordinatorMap();
  auto it = map.find(browser);
  if (it == map.end()) {
    auto coordinator = std::make_unique<MahoShieldBubbleCoordinator>();
    coordinator->browser_ = browser;
    TabStripModel* tab_strip = browser->GetTabStripModel();
    DCHECK(tab_strip);
    if (tab_strip) {
      coordinator->observed_tab_strip_ = tab_strip;
      tab_strip->AddObserver(coordinator.get());
    }
    it = map.emplace(browser, std::move(coordinator)).first;
  }
  return *it->second;
}

MahoShieldBubbleCoordinator::MahoShieldBubbleCoordinator() = default;

MahoShieldBubbleCoordinator::~MahoShieldBubbleCoordinator() {
  if (observed_tab_strip_) {
    observed_tab_strip_->RemoveObserver(this);
    observed_tab_strip_ = nullptr;
  }
  if (bubble_widget_) {
    widget_observation_.Reset();
    content::WebContentsObserver::Observe(nullptr);
    views::Widget* widget = bubble_widget_;
    bubble_widget_ = nullptr;
    if (widget && !widget->IsClosed()) {
      widget->CloseNow();
    }
  }
}

void MahoShieldBubbleCoordinator::Show(
    views::View* anchor_view,
    Browser* browser,
    content::WebContents* target_web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!anchor_view || !browser) {
    return;
  }

  content::WebContents* web_contents = target_web_contents;
  if (!web_contents) {
    web_contents = GetActiveWebContents(browser);
  }
  if (!web_contents) {
    return;
  }

  const std::string origin =
      url::Origin::Create(web_contents->GetLastCommittedURL()).Serialize();
  if (origin.empty()) {
    return;
  }

  MahoShieldSiteState* state = GetShieldSiteState(web_contents);
  if (!state) {
    return;
  }

  Show(anchor_view, origin, GetBlockedCount(web_contents),
       state->IsSiteExceptedForOrigin(origin), web_contents);
}

void MahoShieldBubbleCoordinator::Show(
    views::View* anchor_view,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    content::WebContents* target_web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!anchor_view || !browser_ || origin.empty() || !target_web_contents) {
    return;
  }

  if (IsShowing()) {
    widget_observation_.Reset();
    content::WebContentsObserver::Observe(nullptr);
    views::Widget* old_widget = bubble_widget_;
    bubble_widget_ = nullptr;
    anchor_view_tracker_.SetView(nullptr);
    if (old_widget && !old_widget->IsClosed()) {
      old_widget->Close();
    }
  }

  anchor_view_tracker_.SetView(anchor_view);

  views::Widget* widget =
      MahoShieldBubbleView::Show(anchor_view, browser_, origin, blocked_count,
                                 is_excepted, target_web_contents);
  if (!widget) {
    anchor_view_tracker_.SetView(nullptr);
    return;
  }

  bubble_widget_ = widget;
  widget_observation_.Observe(widget);
  content::WebContentsObserver::Observe(target_web_contents);
}

void MahoShieldBubbleCoordinator::Show(
    views::Widget* anchor_widget,
    const gfx::Rect& anchor_rect,
    const std::string& origin,
    uint32_t blocked_count,
    bool is_excepted,
    content::WebContents* target_web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!anchor_widget || anchor_rect.IsEmpty() || !browser_ || origin.empty() ||
      !target_web_contents) {
    return;
  }

  if (IsShowing()) {
    widget_observation_.Reset();
    content::WebContentsObserver::Observe(nullptr);
    views::Widget* old_widget = bubble_widget_;
    bubble_widget_ = nullptr;
    anchor_view_tracker_.SetView(nullptr);
    if (old_widget && !old_widget->IsClosed()) {
      old_widget->Close();
    }
  }

  views::Widget* widget = MahoShieldBubbleView::Show(
      anchor_widget, anchor_rect, browser_, origin, blocked_count, is_excepted,
      target_web_contents);
  if (!widget) {
    return;
  }

  bubble_widget_ = widget;
  widget_observation_.Observe(widget);
  content::WebContentsObserver::Observe(target_web_contents);
}

void MahoShieldBubbleCoordinator::Hide() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsShowing()) {
    return;
  }
  if (bubble_widget_ && !bubble_widget_->IsClosed()) {
    bubble_widget_->Close();
  }
}

bool MahoShieldBubbleCoordinator::IsShowing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return bubble_widget_ != nullptr;
}

views::Widget* MahoShieldBubbleCoordinator::GetBubbleWidgetForTesting() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return bubble_widget_;
}

void MahoShieldBubbleCoordinator::OnWidgetDestroying(views::Widget* widget) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (widget != bubble_widget_) {
    return;
  }
  widget_observation_.Reset();
  content::WebContentsObserver::Observe(nullptr);
  bubble_widget_ = nullptr;

  views::View* anchor = anchor_view_tracker_.view();
  anchor_view_tracker_.SetView(nullptr);
  if (anchor && anchor->GetWidget()) {
    auto tracker = std::make_shared<views::ViewTracker>(anchor);
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](std::shared_ptr<views::ViewTracker> t) {
                         views::View* v = t->view();
                         if (!v || !v->GetWidget()) {
                           return;
                         }
                         views::FocusManager* fm =
                             v->GetWidget()->GetFocusManager();
                         if (fm && fm->ContainsView(v)) {
                           fm->SetFocusedView(v);
                         }
                       },
                       std::move(tracker)));
  }
}

void MahoShieldBubbleCoordinator::WebContentsDestroyed() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContentsObserver::Observe(nullptr);
  if (IsShowing()) {
    Hide();
  }
}

void MahoShieldBubbleCoordinator::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus /*status*/) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContentsObserver::Observe(nullptr);
  if (IsShowing()) {
    Hide();
  }
}

void MahoShieldBubbleCoordinator::OnTabStripModelChanged(
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

void MahoShieldBubbleCoordinator::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsShowing()) {
    Hide();
  }
  if (tab_strip_model) {
    tab_strip_model->RemoveObserver(this);
  }
  observed_tab_strip_ = nullptr;

  auto& map = GetCoordinatorMap();
  auto it = map.find(browser_);
  if (it != map.end()) {
    [[maybe_unused]] std::unique_ptr<MahoShieldBubbleCoordinator> self =
        std::move(it->second);
    map.erase(it);
  }
}

}  // namespace maho
