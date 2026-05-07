// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

class Browser;

namespace ui {
class Event;
}  // namespace ui

namespace views {
class ImageButton;
class View;
}  // namespace views

namespace maho {

class MahoSidebarTopBarView : public views::View {
  METADATA_HEADER(MahoSidebarTopBarView, views::View)

 public:
  explicit MahoSidebarTopBarView(Browser* browser);
  MahoSidebarTopBarView(const MahoSidebarTopBarView&) = delete;
  MahoSidebarTopBarView& operator=(const MahoSidebarTopBarView&) = delete;
  ~MahoSidebarTopBarView() override;

  void Update(const MahoSidebarTopBarModel& model);
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  // Returns true if `point` (in this view's coordinates) is not over any
  // interactive control and should be treated as window-draggable caption area.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

 private:
  void ApplyChromeSpacing();
  void UpdateButtonImages();
  void UpdateButtonState(views::ImageButton* button,
                         bool enabled,
                         std::string_view symbol_name,
                         std::string_view accessibility_description,
                         const std::u16string& tooltip);
  void OnBackPressed(const ui::Event& event);
  void OnForwardPressed(const ui::Event& event);
  void OnReloadPressed(const ui::Event& event);
  void OnAiPressed(const ui::Event& event);
  void OnToggleSidebarPressed(const ui::Event& event);

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> traffic_light_spacer_ = nullptr;
  raw_ptr<views::View> leading_cluster_ = nullptr;
  raw_ptr<views::View> nav_cluster_ = nullptr;
  raw_ptr<views::View> flex_spacer_ = nullptr;
  raw_ptr<views::ImageButton> sidebar_toggle_button_ = nullptr;
  raw_ptr<views::ImageButton> back_button_ = nullptr;
  raw_ptr<views::ImageButton> forward_button_ = nullptr;
  raw_ptr<views::ImageButton> reload_button_ = nullptr;
  raw_ptr<views::ImageButton> ai_button_ = nullptr;
  MahoSidebarTopBarModel model_;
  base::WeakPtrFactory<MahoSidebarTopBarView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_VIEW_H_
