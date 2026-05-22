// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_notes_view.h"

#include <memory>

#include "base/check.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "maho/browser/ui/webui/maho_notes/maho_notes_ui.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace maho {

MahoSidebarNotesView::MahoSidebarNotesView(Browser* browser)
    : browser_(browser) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
}

MahoSidebarNotesView::~MahoSidebarNotesView() {
  if (web_view_ && contents_wrapper_) {
    web_view_->SetWebContents(nullptr);
  }
  if (contents_wrapper_) {
    contents_wrapper_->SetHost(nullptr);
  }
}

void MahoSidebarNotesView::EnsureWebUILoaded() {
  if (!webui_initialized_) {
    InitWebUI();
  }
}

void MahoSidebarNotesView::ShowUI() {}

void MahoSidebarNotesView::CloseUI() {}

void MahoSidebarNotesView::InitWebUI() {
  CHECK(browser_);
  webui_initialized_ = true;

  Profile* profile = browser_->profile();
  contents_wrapper_ = std::make_unique<WebUIContentsWrapperT<MahoNotesUI>>(
      GURL(maho::kMahoNotesURL), profile, /*task_manager_string_id=*/0,
      /*esc_closes_ui=*/false,
      /*supports_draggable_regions=*/false);

  contents_wrapper_->SetHost(weak_factory_.GetWeakPtr());

  web_view_ = AddChildView(std::make_unique<views::WebView>(profile));
  web_view_->SetWebContents(contents_wrapper_->web_contents());
}

BEGIN_METADATA(MahoSidebarNotesView)
END_METADATA

}  // namespace maho
