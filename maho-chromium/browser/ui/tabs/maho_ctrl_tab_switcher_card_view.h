// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CARD_VIEW_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CARD_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace maho {

// A single tab preview card inside the Ctrl+Tab MRU switcher overlay.
//
// Layout (top-to-bottom):
//   - Thumbnail (page snapshot; async loaded via ThumbnailTabHelper)
//   - Title row: favicon (16px) + title label (truncated end)
//
// The selected card is drawn with an accent border to distinguish it from
// unselected cards.
class MahoCtrlTabSwitcherCardView : public views::View {
  METADATA_HEADER(MahoCtrlTabSwitcherCardView, views::View)

 public:
  static constexpr int kCardWidth = 220;
  static constexpr int kCardHeight = 168;

  MahoCtrlTabSwitcherCardView(content::WebContents* web_contents,
                              bool selected);
  MahoCtrlTabSwitcherCardView(const MahoCtrlTabSwitcherCardView&) = delete;
  MahoCtrlTabSwitcherCardView& operator=(const MahoCtrlTabSwitcherCardView&) =
      delete;
  ~MahoCtrlTabSwitcherCardView() override;

  void SetSelected(bool selected);
  bool selected() const { return selected_; }

  content::WebContents* web_contents() const { return web_contents_.get(); }

  // views::View:
  void OnThemeChanged() override;

 private:
  void BuildContents();
  void AttachThumbnail();
  void OnThumbnailReady(gfx::ImageSkia image);
  void UpdateBorder();

  base::WeakPtr<content::WebContents> web_contents_;
  bool selected_;

  raw_ptr<views::ImageView> thumbnail_view_ = nullptr;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;

  scoped_refptr<ThumbnailImage> thumbnail_source_;
  std::unique_ptr<ThumbnailImage::Subscription> thumbnail_subscription_;

  base::WeakPtrFactory<MahoCtrlTabSwitcherCardView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CARD_VIEW_H_
