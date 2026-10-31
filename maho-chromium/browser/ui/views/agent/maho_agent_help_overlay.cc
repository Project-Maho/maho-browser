// Copyright 2026 The Maho Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "maho/browser/ui/views/agent/maho_agent_help_overlay.h"

#include <utility>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/browser/mcp/maho_mcp_agent_policy.h"

namespace maho {

MahoAgentHelpOverlay::MahoAgentHelpOverlay(Browser* browser)
    : BrowserUserData<MahoAgentHelpOverlay>(browser), browser_(browser) {}

MahoAgentHelpOverlay::~MahoAgentHelpOverlay() = default;

void MahoAgentHelpOverlay::ShowRequest(int64_t tab_id,
                                       const std::string& session_id,
                                       const std::string& request_id,
                                       const std::string& prompt) {
  EnsureWidget();
  tab_id_ = tab_id;
  session_id_ = session_id;
  help_bar_->ShowRequest(request_id, prompt);
  widget_->SetBounds(ComputeTargetBounds());
  widget_->ShowInactive();
}

void MahoAgentHelpOverlay::DismissRequest(const std::string& request_id) {
  if (help_bar_ && help_bar_->active_request_id() == request_id) {
    help_bar_->Dismiss();
    Hide();
  }
}

void MahoAgentHelpOverlay::OnHelpCompleted(const std::string& request_id) {
  MahoAgentTaskSpaceRegistry::Get().Takeover(tab_id_, session_id_);
  Hide();
}

void MahoAgentHelpOverlay::OnHelpCancelled(const std::string& request_id) {
  MahoAgentTaskSpaceRegistry::Get().Fail(tab_id_, session_id_);
  Hide();
}

void MahoAgentHelpOverlay::EnsureWidget() {
  if (widget_) {
    return;
  }
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.accept_events = true;
  params.shadow_type = views::Widget::InitParams::ShadowType::kDrop;
  if (BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
      bv && bv->GetWidget()) {
    params.parent = bv->GetWidget()->GetNativeView();
  }
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  auto bar = std::make_unique<MahoAgentHelpBar>(this);
  help_bar_ = bar.get();
  widget_->SetContentsView(std::move(bar));
}

gfx::Rect MahoAgentHelpOverlay::ComputeTargetBounds() const {
  gfx::Size size = help_bar_ ? help_bar_->GetPreferredSize() : gfx::Size();
  size.SetToMax(gfx::Size(420, 44));
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!bv || !bv->contents_container()) {
    return gfx::Rect(gfx::Point(100, 100), size);
  }
  const gfx::Rect content = bv->contents_container()->GetBoundsInScreen();
  return gfx::Rect(
      gfx::Point(content.x() + (content.width() - size.width()) / 2,
                 content.bottom() - size.height() - 24),
      size);
}

void MahoAgentHelpOverlay::Hide() {
  if (widget_) {
    widget_->Hide();
  }
}

}  // namespace maho
