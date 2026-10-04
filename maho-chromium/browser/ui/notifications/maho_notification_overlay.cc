// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/notifications/maho_notification_overlay.h"

#include <algorithm>
#include <utility>

#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"  // nogncheck
#include "maho/browser/ui/notifications/maho_toast_vibrancy.h"
#include "maho/browser/ui/notifications/maho_toast_view.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/views/widget/widget.h"

namespace maho {

// Shows the upper-right "Link copied" toast after a clipboard-writing copy
// command (Cmd+Shift+C / command-palette copy_url / favorites Copy Link).
// The command-palette copy path previously wrote the clipboard but never
// showed the toast, so Cmd+Shift+C copied silently; every copy surface now
// routes through this one helper. Show() no-ops when the browser has no live
// BrowserView widget (e.g. headless), so this is safe to call unconditionally
// on the UI thread.
void ShowLinkCopiedToast(Browser* browser) {
  if (!browser) {
    return;
  }
  auto* overlay = MahoNotificationOverlay::GetOrCreateForBrowser(browser);
  if (overlay) {
    overlay->Show(l10n_util::GetStringUTF16(IDS_MAHO_TOAST_LINK_COPIED),
                  std::u16string(), base::Seconds(2));
  }
}

}  // namespace maho

MahoNotificationOverlay::MahoNotificationOverlay(Browser* browser)
    : BrowserUserData<MahoNotificationOverlay>(browser), browser_(browser) {
  if (browser_) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
    if (bv && bv->GetWidget()) {
      parent_widget_observed_ = bv->GetWidget();
      parent_widget_observed_->AddObserver(this);
    }
    if (auto* collection = GlobalBrowserCollection::GetInstance()) {
      browser_collection_observation_.Observe(collection);
    }
  }
}

MahoNotificationOverlay::~MahoNotificationOverlay() {
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_.reset();
  }
  if (parent_widget_observed_) {
    parent_widget_observed_->RemoveObserver(this);
    parent_widget_observed_ = nullptr;
  }
}

void MahoNotificationOverlay::Show(const std::u16string& title,
                                   const std::u16string& body,
                                   base::TimeDelta duration) {
  if (browser_) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
    if (!bv || !bv->GetWidget()) {
      return;
    }
  }

  if (widget_) {
    slide_animation_.Stop();
    dismiss_timer_.Stop();
    widget_->RemoveObserver(this);
    widget_->CloseNow();
    widget_.reset();
    toast_view_ = nullptr;
    is_hiding_ = false;
  }

  CreateWidget();
  toast_view_->SetTitleText(title);
  toast_view_->SetBodyText(body);
  toast_view_->SetOnClicked(base::BindRepeating(
      &MahoNotificationOverlay::Hide, weak_factory_.GetWeakPtr()));

  PrimeAnimationStartFrame();
  widget_->ShowInactive();
  StartShowAnimation();
  dismiss_timer_.Start(FROM_HERE, duration,
      base::BindOnce(&MahoNotificationOverlay::OnDismissTimerFired,
                     weak_factory_.GetWeakPtr()));
}

void MahoNotificationOverlay::Hide() {
  if (!widget_ || is_hiding_) return;
  dismiss_timer_.Stop();
  StartHideAnimation();
}

bool MahoNotificationOverlay::IsVisible() const {
  return widget_ && widget_->IsVisible() && !is_hiding_;
}

void MahoNotificationOverlay::OnWidgetDestroying(views::Widget* widget) {
  if (widget == widget_.get()) {
    weak_factory_.InvalidateWeakPtrs();
    dismiss_timer_.Stop();
    slide_animation_.Stop();
    is_hiding_ = false;
    toast_view_ = nullptr;
    views::Widget* released = widget_.release();
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                   released);
    PostRemovalIfNeeded();
    return;
  }
  if (widget == parent_widget_observed_) {
    parent_widget_observed_ = nullptr;
    PostRemovalIfNeeded();
  }
}

void MahoNotificationOverlay::PostRemovalIfNeeded() {
  if (removal_posted_) {
    return;
  }
  removal_posted_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoNotificationOverlay::RemoveFromBrowser, browser_));
}

void MahoNotificationOverlay::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (browser && browser == browser_) {
    PostRemovalIfNeeded();
  }
}

void MahoNotificationOverlay::AnimationProgressed(
    const gfx::Animation* animation) {
  if (!widget_ || animation != &slide_animation_) return;
  double effective = animation->GetCurrentValue();

  gfx::Rect target = ComputeTargetBounds();
  if (target.IsEmpty()) return;

  gfx::Rect current = target;
  int slide_distance = target.height() + 24;
  current.set_y(target.y() - static_cast<int>((1.0 - effective) * slide_distance));
  widget_->SetBounds(current);

  auto* root_view = widget_->GetRootView();
  if (root_view && root_view->layer()) {
    root_view->layer()->SetOpacity(effective);
  }
}

void MahoNotificationOverlay::AnimationEnded(const gfx::Animation* animation) {
  if (animation != &slide_animation_) return;
  if (is_hiding_ && widget_) {
    widget_->RemoveObserver(this);
    widget_->CloseNow();
    widget_.reset();
    toast_view_ = nullptr;
    is_hiding_ = false;
  }
}

void MahoNotificationOverlay::CreateWidget() {
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.accept_events = true;
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;

  if (browser_) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
    params.parent = bv->GetWidget()->GetNativeView();
  } else {
    params.context = context_for_testing_;
  }
  params.bounds = ComputeTargetBounds();

  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  widget_->AddObserver(this);

  auto toast = std::make_unique<MahoToastView>();
  toast_view_ = toast.get();
  widget_->SetContentsView(std::move(toast));

  auto* root_view = widget_->GetRootView();
  if (root_view) {
    root_view->SetPaintToLayer();
    root_view->layer()->SetFillsBoundsOpaquely(false);
  }
  maho::ApplyVibrancyToToast(widget_.get());
}

void MahoNotificationOverlay::PrimeAnimationStartFrame() {
  if (!widget_) return;
  gfx::Rect target = ComputeTargetBounds();
  if (!target.IsEmpty()) {
    gfx::Rect start = target;
    int slide_distance = target.height() + 24;
    start.set_y(target.y() - slide_distance);
    widget_->SetBounds(start);
  }
  auto* root_view = widget_->GetRootView();
  if (root_view && root_view->layer()) {
    root_view->layer()->SetOpacity(0.0f);
  }
}

void MahoNotificationOverlay::StartShowAnimation() {
  is_hiding_ = false;
  slide_animation_.SetSlideDuration(base::Milliseconds(300));
  slide_animation_.SetTweenType(gfx::Tween::EASE_OUT);
  slide_animation_.Show();
}

void MahoNotificationOverlay::StartHideAnimation() {
  is_hiding_ = true;
  slide_animation_.SetSlideDuration(base::Milliseconds(200));
  slide_animation_.SetTweenType(gfx::Tween::EASE_IN);
  slide_animation_.Hide();
}

gfx::Rect MahoNotificationOverlay::ComputeTargetBounds() const {
  BrowserView* bv = browser_ ? BrowserView::GetBrowserViewForBrowser(browser_) : nullptr;
  if (!bv || !bv->contents_container()) {
    gfx::Size toast_size = toast_view_ ? toast_view_->GetPreferredSize() : gfx::Size(320, 80);
    return gfx::Rect(gfx::Point(100, 100), toast_size);
  }

  gfx::Size toast_size = toast_view_ ? toast_view_->GetPreferredSize() : gfx::Size(320, 80);
  gfx::Rect content_bounds = bv->contents_container()->GetBoundsInScreen();
  constexpr int kMarginPx = 16;
  int x = content_bounds.right() - toast_size.width() - kMarginPx;
  int y = content_bounds.y() + kMarginPx;
  return gfx::Rect(gfx::Point(x, y), toast_size);
}

void MahoNotificationOverlay::OnDismissTimerFired() {
  StartHideAnimation();
}
