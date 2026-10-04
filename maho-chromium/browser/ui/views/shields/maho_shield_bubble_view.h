// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_VIEW_H_

#include <cstddef>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

namespace views {
class ImageView;
class LabelButton;
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
                       bool is_excepted,
                       content::WebContents* target_web_contents);
  ~MahoShieldBubbleView() override;
  MahoShieldBubbleView(const MahoShieldBubbleView&) = delete;
  MahoShieldBubbleView& operator=(const MahoShieldBubbleView&) = delete;

  static views::Widget* Show(views::View* anchor_view,
                             Browser* browser,
                             const std::string& origin,
                             uint32_t blocked_count,
                             bool is_excepted,
                             content::WebContents* target_web_contents);
  static views::Widget* Show(views::Widget* anchor_widget,
                             const gfx::Rect& anchor_rect,
                             Browser* browser,
                             const std::string& origin,
                             uint32_t blocked_count,
                             bool is_excepted,
                             content::WebContents* target_web_contents);

  static int GetContentBlockingModeForTesting();
  static bool SetContentBlockingModeForTesting(int mode);
  static std::string GetSiteExceptionsForTesting();

  views::ToggleButton* GetToggleForTesting() const { return toggle_; }
  views::Label* GetBlockedCountLabelForTesting() const {
    return blocked_count_label_;
  }
  views::Label* GetBlockedCountDescriptionLabelForTesting() const {
    return blocked_count_description_label_;
  }
  views::Label* GetSiteLabelForTesting() const { return site_label_; }
  views::Label* GetStatusLabelForTesting() const { return status_label_; }
  views::Label* GetToggleLabelForTesting() const { return toggle_label_; }
  views::Label* GetToggleSublabelForTesting() const {
    return toggle_sublabel_;
  }
  size_t GetNativeToggleRowCountForTesting() const {
    return native_toggle_row_count_;
  }
  size_t GetUnsupportedRowCountForTesting() const {
    return capability_gated_row_count_;
  }
  size_t GetCapabilityGatedRowCountForTesting() const {
    return capability_gated_row_count_;
  }
  views::LabelButton* GetSettingsButtonForTesting() const {
    return settings_button_;
  }
  void ToggleForTesting();
  void OpenSettingsForTesting();

 private:
  void OnTogglePressed();
  void OnSettingsPressed(const ui::Event& event);
  void OpenSettings();
  void UpdateStatusPresentation(int mode);

  raw_ptr<Browser> browser_;
  std::string origin_;
  bool is_excepted_;
  base::WeakPtr<content::WebContents> target_web_contents_;
  raw_ptr<views::ToggleButton> toggle_ = nullptr;
  raw_ptr<views::View> status_band_ = nullptr;
  raw_ptr<views::Label> site_label_ = nullptr;
  raw_ptr<views::Label> blocked_count_label_ = nullptr;
  raw_ptr<views::Label> blocked_count_description_label_ = nullptr;
  raw_ptr<views::ImageView> status_icon_ = nullptr;
  raw_ptr<views::View> status_indicator_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::Label> toggle_label_ = nullptr;
  raw_ptr<views::Label> toggle_sublabel_ = nullptr;
  raw_ptr<views::LabelButton> settings_button_ = nullptr;
  size_t native_toggle_row_count_ = 0;
  size_t capability_gated_row_count_ = 0;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SHIELDS_MAHO_SHIELD_BUBBLE_VIEW_H_
