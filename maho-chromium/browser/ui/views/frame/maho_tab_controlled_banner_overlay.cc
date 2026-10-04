#include "maho/browser/ui/views/frame/maho_tab_controlled_banner_overlay.h"

#include <utility>

#include "base/task/single_thread_task_runner.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/layout/box_layout.h"

namespace maho {

MahoTabControlledBannerOverlay::MahoTabControlledBannerOverlay(Browser* browser)
    : BrowserUserData<MahoTabControlledBannerOverlay>(browser),
      browser_(browser) {
  if (browser_) {
    if (browser_->GetTabStripModel()) {
      browser_->GetTabStripModel()->AddObserver(this);
    }
    if (browser_->GetProfile()) {
      if (auto* service =
              maho::ai::MahoControlActivityService::GetForProfile(browser_->GetProfile())) {
        control_observation_.Observe(service);
      }
    }
  }
}

MahoTabControlledBannerOverlay::~MahoTabControlledBannerOverlay() {
  if (widget_) {
    widget_->RemoveObserver(this);
    widget_->CloseNow();
    widget_.reset();
  }
  if (parent_widget_observed_) {
    parent_widget_observed_->RemoveObserver(this);
    parent_widget_observed_ = nullptr;
  }
}

void MahoTabControlledBannerOverlay::ShowBanner(
    int64_t tab_id,
    const std::u16string& controller_name) {
  LOG(INFO) << "[maho-control-activity] banner show tab=" << tab_id
            << " controller=" << controller_name;
  current_controlled_tab_id_ = tab_id;
  current_controller_name_ = controller_name;

  if (!widget_) {
    CreateWidget();
  }

  if (banner_view_) {
    banner_view_->SetControlledTarget(tab_id, controller_name);
  }

  UpdatePosition();
  if (widget_) {
    widget_->Show();
  }
}

void MahoTabControlledBannerOverlay::HideBanner() {
  current_controlled_tab_id_ = 0;
  current_controller_name_.clear();
  if (banner_view_) {
    banner_view_->ClearControlledTarget();
  }
  if (widget_ && widget_->IsVisible()) {
    widget_->Hide();
  }
}

bool MahoTabControlledBannerOverlay::IsVisible() const {
  return widget_ && widget_->IsVisible();
}

void MahoTabControlledBannerOverlay::CreateWidget() {
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.accept_events = true;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;

  if (browser_) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
    if (bv && bv->GetWidget()) {
      params.parent = bv->GetWidget()->GetNativeView();
      if (!parent_widget_observed_) {
        parent_widget_observed_ = bv->GetWidget();
        parent_widget_observed_->AddObserver(this);
      }
    }
  }

  params.bounds = ComputeTargetBounds();

  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  widget_->AddObserver(this);

  auto banner = std::make_unique<MahoTabControlledBannerView>(
      base::BindRepeating(&MahoTabControlledBannerOverlay::OnStopRequested,
                          weak_factory_.GetWeakPtr()));
  banner_view_ = banner.get();
  widget_->SetContentsView(std::move(banner));
}

void MahoTabControlledBannerOverlay::UpdatePosition() {
  if (!widget_) {
    return;
  }
  gfx::Rect target_bounds = ComputeTargetBounds();
  if (!target_bounds.IsEmpty()) {
    widget_->SetBounds(target_bounds);
  }
}

gfx::Rect MahoTabControlledBannerOverlay::ComputeTargetBounds() const {
  BrowserView* bv =
      browser_ ? BrowserView::GetBrowserViewForBrowser(browser_) : nullptr;
  if (!bv || !bv->contents_container()) {
    gfx::Size banner_size =
        banner_view_ ? banner_view_->GetPreferredSize() : gfx::Size(360, 36);
    return gfx::Rect(gfx::Point(100, 100), banner_size);
  }

  gfx::Size banner_size =
      banner_view_ ? banner_view_->GetPreferredSize() : gfx::Size(360, 36);
  gfx::Rect content_bounds = bv->contents_container()->GetBoundsInScreen();
  int x = content_bounds.x() + (content_bounds.width() - banner_size.width()) / 2;
  int y = content_bounds.y() + 16;
  return gfx::Rect(gfx::Point(x, y), banner_size);
}

void MahoTabControlledBannerOverlay::OnStopRequested(int64_t tab_id) {
  if (browser_ && browser_->GetProfile()) {
    if (auto* service =
            maho::ai::MahoControlActivityService::GetForProfile(browser_->GetProfile())) {
      service->RequestImmediateStop(tab_id);
    }
  }
  HideBanner();
}

void MahoTabControlledBannerOverlay::OnWidgetDestroying(views::Widget* widget) {
  if (widget == widget_.get()) {
    weak_factory_.InvalidateWeakPtrs();
    banner_view_ = nullptr;
    views::Widget* released = widget_.release();
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                   released);
    return;
  }
  if (widget == parent_widget_observed_) {
    parent_widget_observed_ = nullptr;
    HideBanner();
  }
}

void MahoTabControlledBannerOverlay::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget == parent_widget_observed_) {
    UpdatePosition();
  }
}

void MahoTabControlledBannerOverlay::OnControlActivityChanged(
    const maho::ai::ControlActivity& activity) {
  LOG(INFO) << "[maho-control-activity] overlay event id=" << activity.session_id
            << " plane=" << static_cast<int>(activity.control_plane)
            << " state=" << static_cast<int>(activity.state)
            << " has_target=" << activity.target.has_value()
            << " target_window="
            << (activity.target ? activity.target->window_id : 0)
            << " my_browser=" << (browser_ ? browser_->GetSessionID().id() : 0);
  if (!browser_) {
    return;
  }

  const bool is_controlling =
      activity.target.has_value() &&
      activity.target->window_id == browser_->GetSessionID().id() &&
      activity.state != maho::ai::ControlActivityState::kDisconnected &&
      activity.state != maho::ai::ControlActivityState::kFailed;

  if (!is_controlling) {
    HideBanner();
    return;
  }

  // Direct control (Maho CLI tools, repl, pipe, remote MCP clients) is not
  // agent delegation: it only surfaces through this banner and the sidebar
  // control indicator, never by opening the AI side panel. Only an explicit
  // hand-off to the built-in agent (maho_agent_delegate) opens the panel.
  int64_t target_tab_id = activity.target->tab_id;
  std::u16string controller_name =
      base::UTF8ToUTF16(activity.controller_display_name);

  TabStripModel* tab_strip = browser_->GetTabStripModel();
  content::WebContents* active_contents =
      tab_strip ? tab_strip->GetActiveWebContents() : nullptr;
  auto* helper =
      active_contents
          ? sessions::SessionTabHelper::FromWebContents(active_contents)
          : nullptr;

  if (helper && helper->session_id().id() == target_tab_id) {
    ShowBanner(target_tab_id, controller_name);
  } else {
    HideBanner();
  }
}

void MahoTabControlledBannerOverlay::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    if (browser_ && browser_->GetProfile()) {
      if (auto* service =
              maho::ai::MahoControlActivityService::GetForProfile(browser_->GetProfile())) {
        const auto activity =
            service->GetActivity(base::NumberToString(current_controlled_tab_id_));
        if (activity) {
          OnControlActivityChanged(*activity);
        } else {
          content::WebContents* active_contents =
              tab_strip_model ? tab_strip_model->GetActiveWebContents() : nullptr;
          auto* helper =
              active_contents
                  ? sessions::SessionTabHelper::FromWebContents(active_contents)
                  : nullptr;
          if (helper && helper->session_id().id() == current_controlled_tab_id_) {
            ShowBanner(current_controlled_tab_id_, current_controller_name_);
          } else {
            HideBanner();
          }
        }
      }
    }
  }
}

void MahoTabControlledBannerOverlay::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (browser && browser == browser_) {
    HideBanner();
  }
}

}
