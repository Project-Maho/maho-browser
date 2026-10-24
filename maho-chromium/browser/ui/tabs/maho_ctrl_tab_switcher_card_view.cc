// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_card_view.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"

namespace maho {

namespace {

constexpr int kContentInset = 6;
constexpr int kThumbnailHeight = 120;
// Thumbnail height for a card |card_width| wide. The snapshot is a page viewport,
// so its box keeps the aspect it has at the natural card width: a narrower card
// gets a proportionally shorter thumbnail. A constant height would keep the box
// the same while its width followed the fitted card width, and ImageView scales
// the snapshot into the box without preserving aspect, so the preview would be
// squashed by exactly the amount the card was narrowed.
int ThumbnailHeightForCardWidth(int card_width) {
  const int content = std::max(card_width - 2 * kContentInset, 1);
  const int natural_content = MahoCtrlTabSwitcherCardView::kCardWidth - 2 * kContentInset;
  return std::max(1, content * kThumbnailHeight / natural_content);
}
constexpr int kBorderRadius = 10;
constexpr int kBorderThickness = 2;
constexpr int kFooterHeight = 36;
constexpr int kFaviconSize = 16;
constexpr int kFallbackFaviconSize = 48;

SkColor CardBackgroundColor(bool selected) {
  return selected ? SkColorSetARGB(0x33, 0xFF, 0xFF, 0xFF)
                  : SK_ColorTRANSPARENT;
}

SkColor CardBorderColor(bool selected) {
  return selected ? SkColorSetARGB(0xFF, 0x6E, 0xA9, 0xFF)
                  : SkColorSetARGB(0x00, 0x00, 0x00, 0x00);
}

gfx::Image GetFaviconFromWebContents(content::WebContents* wc) {
  if (!wc) {
    return gfx::Image();
  }
  content::NavigationEntry* entry =
      wc->GetController().GetLastCommittedEntry();
  if (!entry) {
    return gfx::Image();
  }
  const content::FaviconStatus& favicon = entry->GetFavicon();
  if (!favicon.valid || favicon.image.IsEmpty()) {
    return gfx::Image();
  }
  return favicon.image;
}

}  // namespace

// static
int MahoCtrlTabSwitcherCardView::CardHeightForWidth(int card_width) {
  const int thumbnail = ThumbnailHeightForCardWidth(card_width);
  // content insets (top+bottom) + thumbnail + box spacing + footer.
  return 2 * kContentInset + thumbnail + 4 + kFooterHeight;
}

MahoCtrlTabSwitcherCardView::MahoCtrlTabSwitcherCardView(
    content::WebContents* web_contents,
    bool selected,
    int card_width)
    : web_contents_(web_contents ? web_contents->GetWeakPtr()
                                  : base::WeakPtr<content::WebContents>()),
      selected_(selected),
      card_width_(std::max(card_width, kMinCardWidth)) {
  SetPreferredSize(gfx::Size(card_width_, CardHeightForWidth(card_width_)));
  BuildContents();
  UpdateBorder();
  AttachThumbnail();
}

MahoCtrlTabSwitcherCardView::~MahoCtrlTabSwitcherCardView() = default;

int MahoCtrlTabSwitcherCardView::content_width() const {
  return std::max(card_width_ - 2 * kContentInset, 1);
}

void MahoCtrlTabSwitcherCardView::SetSelected(bool selected) {
  if (selected == selected_) {
    return;
  }
  selected_ = selected;
  UpdateBorder();
  SchedulePaint();
}

void MahoCtrlTabSwitcherCardView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateBorder();
}

void MahoCtrlTabSwitcherCardView::BuildContents() {
  auto* box = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kContentInset), 4));
  box->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  thumbnail_view_ = AddChildView(std::make_unique<views::ImageView>());
  const int thumb_height = ThumbnailHeightForCardWidth(card_width_);
  thumbnail_view_->SetPreferredSize(
      gfx::Size(content_width(), thumb_height));
  thumbnail_view_->SetImageSize(
      gfx::Size(content_width(), thumb_height));
  thumbnail_view_->SetHorizontalAlignment(
      views::ImageView::Alignment::kCenter);
  thumbnail_view_->SetVerticalAlignment(views::ImageView::Alignment::kCenter);
  thumbnail_view_->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetARGB(0x24, 0xFF, 0xFF, 0xFF), kBorderRadius - 2));

  auto* footer = AddChildView(std::make_unique<views::BoxLayoutView>());
  footer->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  footer->SetInsideBorderInsets(gfx::Insets::TLBR(0, 4, 0, 4));
  footer->SetBetweenChildSpacing(6);
  footer->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  footer->SetPreferredSize(gfx::Size(content_width(), kFooterHeight));

  favicon_view_ = footer->AddChildView(std::make_unique<views::ImageView>());
  favicon_view_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));

  content::WebContents* wc = web_contents_.get();
  gfx::Image favicon_image = GetFaviconFromWebContents(wc);
  if (!favicon_image.IsEmpty()) {
    // Render favicon centered inside the fixed-size thumbnail slot; the
    // thumbnail_view_ preferred size stays at content_width() x the per-card
    // thumbnail height
    // so the card layout does not shift when the real page thumbnail
    // arrives later.
    thumbnail_view_->SetImageSize(
        gfx::Size(kFallbackFaviconSize, kFallbackFaviconSize));
    thumbnail_view_->SetImage(ui::ImageModel::FromImage(favicon_image));
    favicon_view_->SetImage(ui::ImageModel::FromImage(favicon_image));
  }

  std::u16string title;
  if (wc) {
    title = wc->GetTitle();
    if (title.empty()) {
      title = base::UTF8ToUTF16(wc->GetLastCommittedURL().spec());
    }
  }

  title_label_ = footer->AddChildView(
      std::make_unique<views::Label>(title, views::style::CONTEXT_LABEL,
                                     views::style::STYLE_PRIMARY));
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetEnabledColor(SkColorSetRGB(0xEA, 0xEA, 0xEA));
  footer->SetFlexForView(title_label_, 1);
}

void MahoCtrlTabSwitcherCardView::AttachThumbnail() {
  content::WebContents* wc = web_contents_.get();
  if (!wc) {
    return;
  }
  auto* helper = ThumbnailTabHelper::From(tabs::TabInterface::GetFromContents(wc));
  if (!helper) {
    return;
  }
  thumbnail_source_ = helper->thumbnail();
  if (!thumbnail_source_) {
    return;
  }
  thumbnail_subscription_ = thumbnail_source_->Subscribe();
  thumbnail_subscription_->SetUncompressedImageCallback(base::BindRepeating(
      &MahoCtrlTabSwitcherCardView::OnThumbnailReady,
      weak_factory_.GetWeakPtr()));
  // Request cached data delivery. If a compressed JPEG is cached this fires
  // the callback with a decoded image on a background thread and posts back
  // to the UI thread — no fresh renderer capture required.
  if (thumbnail_source_->has_data()) {
    thumbnail_source_->RequestThumbnailImage();
  }
}

void MahoCtrlTabSwitcherCardView::OnThumbnailReady(gfx::ImageSkia image) {
  if (!thumbnail_view_ || image.isNull()) {
    return;
  }
  thumbnail_view_->SetImageSize(
      gfx::Size(content_width(), ThumbnailHeightForCardWidth(card_width_)));
  thumbnail_view_->SetImage(ui::ImageModel::FromImageSkia(image));
}

void MahoCtrlTabSwitcherCardView::UpdateBorder() {
  SetBackground(views::CreateRoundedRectBackground(
      CardBackgroundColor(selected_), kBorderRadius));
  SetBorder(views::CreateRoundedRectBorder(
      kBorderThickness, kBorderRadius, CardBorderColor(selected_)));
}

BEGIN_METADATA(MahoCtrlTabSwitcherCardView)
END_METADATA

}  // namespace maho
