// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOTES_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOTES_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;
class MahoNotesUI;

namespace views {
class WebView;
}  // namespace views

template <typename T>
class WebUIContentsWrapperT;

namespace maho {

class MahoSidebarNotesView : public views::View,
                              public WebUIContentsWrapper::Host {
  METADATA_HEADER(MahoSidebarNotesView, views::View)

 public:
  explicit MahoSidebarNotesView(Browser* browser);
  MahoSidebarNotesView(const MahoSidebarNotesView&) = delete;
  MahoSidebarNotesView& operator=(const MahoSidebarNotesView&) = delete;
  ~MahoSidebarNotesView() override;

  void EnsureWebUILoaded();

  // WebUIContentsWrapper::Host:
  void ShowUI() override;
  void CloseUI() override;

  views::WebView* web_view_for_testing() { return web_view_; }

 private:
  void InitWebUI();

  raw_ptr<Browser> browser_;
  std::unique_ptr<WebUIContentsWrapperT<MahoNotesUI>> contents_wrapper_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  bool webui_initialized_ = false;
  base::WeakPtrFactory<MahoSidebarNotesView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_NOTES_VIEW_H_
