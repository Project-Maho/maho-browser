// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Label;
class ToggleButton;
class Widget;
}  // namespace views

namespace maho {

class MahoShieldBubbleView : public views::View {
  METADATA_HEADER(MahoShieldBubbleView, views::View)

 public:
  MahoShieldBubbleView(Browser* browser,
                       const std::string& origin,
                       uint32_t blocked_count,
                       bool is_excepted);
  MahoShieldBubbleView(const MahoShieldBubbleView&) = delete;
  MahoShieldBubbleView& operator=(const MahoShieldBubbleView&) = delete;

  static views::Widget* Show(views::View* anchor_view,
                             Browser* browser,
                             const std::string& origin,
                             uint32_t blocked_count,
                             bool is_excepted);

 private:
  void OnTogglePressed();
  void OnSettingsPressed(const ui::Event& event);

  raw_ptr<Browser> browser_;
  std::string origin_;
  bool is_excepted_;
  raw_ptr<views::ToggleButton> toggle_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_VIEW_H_
