// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_ICON_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_ICON_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class View;
class Widget;
}  // namespace views

namespace maho {

class MahoLocationBarUtilityIconView : public views::ToggleImageButton {
  METADATA_HEADER(MahoLocationBarUtilityIconView, views::ToggleImageButton)

 public:
  using ShowPanelCallback = base::RepeatingClosure;

  explicit MahoLocationBarUtilityIconView(ShowPanelCallback show_panel_callback);
  MahoLocationBarUtilityIconView(const MahoLocationBarUtilityIconView&) = delete;
  MahoLocationBarUtilityIconView& operator=(
      const MahoLocationBarUtilityIconView&) = delete;
  ~MahoLocationBarUtilityIconView() override;

  // Show the icon because the pill's trailing action area is expanded (focus, etc.).
  void SetAffordanceVisible(bool affordance_visible);
  // Show the icon because the user is hovering the pill region.
  void SetHoverVisible(bool hover_visible);
  void SetPanelIsShowing(bool showing);
  void SetSidebarPalette(const MahoSidebarPalette& palette);

  bool IsPanelOpenForTesting() const { return panel_is_showing_; }
  bool IsHoverVisibleForTesting() const { return hover_visible_; }
  bool IsAffordanceVisibleForTesting() const { return affordance_visible_; }

  // views::Button:
  void StateChanged(ButtonState old_state) override;

  // views::View:
  void OnThemeChanged() override;

 private:
  void TogglePanel();
  void UpdateButtonChrome();
  void UpdateVisibilityState();

  ShowPanelCallback show_panel_callback_;
  bool panel_is_showing_ = false;
  bool affordance_visible_ = false;
  bool hover_visible_ = false;
  MahoSidebarPalette palette_;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_LOCATION_BAR_UTILITY_ICON_VIEW_H_
