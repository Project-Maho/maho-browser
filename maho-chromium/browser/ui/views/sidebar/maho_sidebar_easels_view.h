// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_EASELS_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_EASELS_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;
class MahoEaselUI;

namespace views {
class WebView;
}  // namespace views

template <typename T>
class WebUIContentsWrapperT;

namespace maho {

// Inline sidebar pane that embeds the chrome://maho-easel/ WebUI surface
// inside the library content host.  Lazily initialises the WebUI wrapper on
// first show.
class MahoSidebarEaselsView : public views::View,
                               public WebUIContentsWrapper::Host {
  METADATA_HEADER(MahoSidebarEaselsView, views::View)

 public:
  explicit MahoSidebarEaselsView(Browser* browser);
  MahoSidebarEaselsView(const MahoSidebarEaselsView&) = delete;
  MahoSidebarEaselsView& operator=(const MahoSidebarEaselsView&) = delete;
  ~MahoSidebarEaselsView() override;

  // Ensures the WebUI is loaded.  Called when the pane becomes visible.
  void EnsureWebUILoaded();

  // WebUIContentsWrapper::Host:
  void ShowUI() override;
  void CloseUI() override;

  views::WebView* web_view_for_testing() { return web_view_; }

 private:
  void InitWebUI();

  raw_ptr<Browser> browser_;
  std::unique_ptr<WebUIContentsWrapperT<MahoEaselUI>> contents_wrapper_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  bool webui_initialized_ = false;
  base::WeakPtrFactory<MahoSidebarEaselsView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_EASELS_VIEW_H_
