// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_bubble_view.h"

#include <memory>

#include "base/check.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost_ui.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace maho {

namespace {
constexpr int kBubbleWidth = 420;
constexpr int kBubbleHeight = 520;
}  // namespace

MahoBoostBubbleView::MahoBoostBubbleView(
    views::View* anchor_view,
    content::WebContents* web_contents,
    Browser* browser,
    content::WebContents* target_web_contents)
    : LocationBarBubbleDelegateView(anchor_view, web_contents) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_margins(gfx::Insets());
  SetLayoutManager(std::make_unique<views::FillLayout>());
  InitWebUI(browser, target_web_contents);
}

MahoBoostBubbleView::~MahoBoostBubbleView() {
  // Detach the WebView from the WebContents *before* destroying the wrapper so
  // that the views::WebView does not hold a dangling pointer to a WebContents
  // that has already been torn down.  This mirrors the pattern in
  // WebUIBubbleDialogView::~WebUIBubbleDialogView().
  if (web_view_ && contents_wrapper_) {
    web_view_->SetWebContents(nullptr);
  }
  if (contents_wrapper_) {
    contents_wrapper_->SetHost(nullptr);
  }
}

gfx::Size MahoBoostBubbleView::CalculatePreferredSize(
    const views::SizeBounds& /*available_size*/) const {
  return gfx::Size(kBubbleWidth, kBubbleHeight);
}

void MahoBoostBubbleView::ShowUI() {
  if (GetWidget()) {
    GetWidget()->Show();
    if (web_view_) {
      web_view_->RequestFocus();
    }
  }
}

void MahoBoostBubbleView::CloseUI() {
  // Esc key dismissal is handled by WebUIContentsWrapperT (esc_closes_ui=true
  // in InitWebUI), which calls CloseUI() on the host.  Widget::Close() is
  // asynchronous; the coordinator's OnWidgetDestroying() clears bubble_ state.
  if (GetWidget()) {
    GetWidget()->Close();
  }
}

void MahoBoostBubbleView::InitWebUI(Browser* browser,
                                    content::WebContents* target_web_contents) {
  (void)target_web_contents;

  Profile* profile = browser->profile();
  contents_wrapper_ = std::make_unique<WebUIContentsWrapperT<MahoBoostUI>>(
      GURL(maho::kMahoBoostURL), profile, /*task_manager_string_id=*/0,
      /*esc_closes_ui=*/true,
      /*supports_draggable_regions=*/false);

  contents_wrapper_->SetHost(weak_factory_.GetWeakPtr());

  web_view_ = AddChildView(
      std::make_unique<views::WebView>(profile));
  web_view_->SetWebContents(contents_wrapper_->web_contents());
}

BEGIN_METADATA(MahoBoostBubbleView)
END_METADATA

}  // namespace maho
