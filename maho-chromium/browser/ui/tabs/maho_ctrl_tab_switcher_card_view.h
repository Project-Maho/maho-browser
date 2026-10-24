// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CARD_VIEW_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CARD_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_layout.h"
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
  static constexpr int kCardWidth = ctrl_tab_layout::kNaturalCardWidth;
  // Height of a card at the NATURAL width: 2*content inset + thumbnail + box
  // spacing + footer = 12 + 120 + 4 + 36. A card narrowed by the fit is shorter
  // instead - see CardHeightForWidth(). (This constant previously said 168,
  // which omitted the 4 px box spacing and so was 4 px short of what the
  // BoxLayout actually laid out.)
  static constexpr int kCardHeight = 172;
  // Narrowest card the switcher shrinks to when a window is too narrow for the
  // natural width; the thumbnail shrinks with the card so nothing is clipped.
  static constexpr int kMinCardWidth = ctrl_tab_layout::kMinCardWidth;

  // Height of a card |card_width| wide. The thumbnail is drawn at a fixed aspect
  // ratio of the page viewport, so a narrower card is also a SHORTER card. A
  // constant height would leave the thumbnail box unchanged while its width
  // followed the fitted card width, and ImageView scales the snapshot into the
  // box without preserving aspect - so every preview would be squashed into a
  // near-square by exactly the amount the card was narrowed.
  static int CardHeightForWidth(int card_width);

  MahoCtrlTabSwitcherCardView(content::WebContents* web_contents,
                              bool selected,
                              int card_width = kCardWidth);
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

  // Width available to the thumbnail and the title row inside the card: the
  // card width minus its content insets.
  int content_width() const;

  base::WeakPtr<content::WebContents> web_contents_;
  bool selected_;
  int card_width_ = kCardWidth;

  raw_ptr<views::ImageView> thumbnail_view_ = nullptr;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;

  scoped_refptr<ThumbnailImage> thumbnail_source_;
  std::unique_ptr<ThumbnailImage::Subscription> thumbnail_subscription_;

  base::WeakPtrFactory<MahoCtrlTabSwitcherCardView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SWITCHER_CARD_VIEW_H_
