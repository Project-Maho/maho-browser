// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_BUBBLE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_BUBBLE_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/location_bar/location_bar_bubble_delegate_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "ui/gfx/geometry/size.h"

class Browser;
class MahoBoostUI;

namespace content {
class WebContents;
}  // namespace content

namespace views {
class WebView;
}  // namespace views

template <typename T>
class WebUIContentsWrapperT;

namespace maho {

// Native bubble view that hosts the chrome://maho-boost WebUI surface.
// Designed to be shown anchored to the location bar or shield icon, with
// a target WebContents passed through to the embedded MahoBoostUI.
class MahoBoostBubbleView : public LocationBarBubbleDelegateView,
                             public WebUIContentsWrapper::Host {
  METADATA_HEADER(MahoBoostBubbleView, LocationBarBubbleDelegateView)

 public:
  MahoBoostBubbleView(views::View* anchor_view,
                      content::WebContents* web_contents,
                      Browser* browser,
                      content::WebContents* target_web_contents);
  MahoBoostBubbleView(const MahoBoostBubbleView&) = delete;
  MahoBoostBubbleView& operator=(const MahoBoostBubbleView&) = delete;
  ~MahoBoostBubbleView() override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // WebUIContentsWrapper::Host:
  void ShowUI() override;
  void CloseUI() override;

 private:
  // Initialises the WebUIContentsWrapper and embeds it in a WebView child.
  // |browser| and |target_web_contents| are consumed here and not retained.
  void InitWebUI(Browser* browser, content::WebContents* target_web_contents);

  std::unique_ptr<WebUIContentsWrapperT<MahoBoostUI>> contents_wrapper_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  base::WeakPtrFactory<MahoBoostBubbleView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_BUBBLE_VIEW_H_
