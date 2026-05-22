// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_easels_view.h"

#include <memory>

#include "base/check.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "maho/browser/ui/webui/maho_easel/maho_easel_ui.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace maho {

MahoSidebarEaselsView::MahoSidebarEaselsView(Browser* browser)
    : browser_(browser) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
}

MahoSidebarEaselsView::~MahoSidebarEaselsView() {
  if (web_view_ && contents_wrapper_) {
    web_view_->SetWebContents(nullptr);
  }
  if (contents_wrapper_) {
    contents_wrapper_->SetHost(nullptr);
  }
}

void MahoSidebarEaselsView::EnsureWebUILoaded() {
  if (!webui_initialized_) {
    InitWebUI();
  }
}

void MahoSidebarEaselsView::ShowUI() {
  // No-op: the pane is shown/hidden by the sidebar view's library routing.
}

void MahoSidebarEaselsView::CloseUI() {
  // No-op: lifetime is managed by the sidebar view.
}

void MahoSidebarEaselsView::InitWebUI() {
  CHECK(browser_);
  webui_initialized_ = true;

  Profile* profile = browser_->profile();
  contents_wrapper_ = std::make_unique<WebUIContentsWrapperT<MahoEaselUI>>(
      GURL(maho::kMahoEaselURL), profile, /*task_manager_string_id=*/0,
      /*esc_closes_ui=*/false,
      /*supports_draggable_regions=*/false);

  contents_wrapper_->SetHost(weak_factory_.GetWeakPtr());

  web_view_ = AddChildView(std::make_unique<views::WebView>(profile));
  web_view_->SetWebContents(contents_wrapper_->web_contents());
}

BEGIN_METADATA(MahoSidebarEaselsView)
END_METADATA

}  // namespace maho
