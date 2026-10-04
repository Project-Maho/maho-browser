// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_TOAST_VIEW_H_
#define MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_TOAST_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace views {
class ImageView;
class Label;
}  // namespace views

// Compact rounded pill toast with a leading glyph chip, title, and optional
// body line; painted translucent so window vibrancy shows through.
class MahoToastView : public views::View {
  METADATA_HEADER(MahoToastView, views::View)
 public:
  MahoToastView();
  ~MahoToastView() override;

  void SetTitleText(const std::u16string& title);
  void SetBodyText(const std::u16string& body);
  void SetOnClicked(base::RepeatingClosure callback);

  views::Label* title_label_for_testing() const { return title_label_; }
  views::Label* body_label_for_testing() const { return body_label_; }
  views::ImageView* icon_view_for_testing() const { return icon_chip_; }

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnThemeChanged() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  raw_ptr<views::ImageView> icon_chip_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> body_label_ = nullptr;
  base::RepeatingClosure on_clicked_;
};

#endif  // MAHO_BROWSER_UI_NOTIFICATIONS_MAHO_TOAST_VIEW_H_
