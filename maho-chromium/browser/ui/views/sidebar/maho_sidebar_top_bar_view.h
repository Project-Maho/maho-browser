// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_VIEW_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/functional/callback.h"
#include "build/build_config.h"
#include "components/prefs/pref_change_registrar.h"
#include "maho/browser/ai/maho_control_activity_service.h"
#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/view.h"

class AppMenu;
class AppMenuModel;
class Browser;

namespace ui {
class Event;
}  // namespace ui

namespace views {
class ImageButton;
class Label;
class View;
}  // namespace views

namespace maho {

class MahoSidebarTopBarView
    : public views::View,
      public maho::ai::MahoControlActivityService::Observer {
  METADATA_HEADER(MahoSidebarTopBarView, views::View)

 public:
  explicit MahoSidebarTopBarView(Browser* browser);
  MahoSidebarTopBarView(const MahoSidebarTopBarView&) = delete;
  MahoSidebarTopBarView& operator=(const MahoSidebarTopBarView&) = delete;
  ~MahoSidebarTopBarView() override;

  void Update(const MahoSidebarTopBarModel& model);
  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette);

  // Pushes the resolved control-activity state into the hosted chip. The top
  // bar owns the chip's placement only; controller/session truth stays with
  // the caller, so an idle model simply hides the chip.
  void SetControlActivity(const MahoControlActivityIndicatorModel& model);
  void SetControlledTabChangedCallback(
      base::RepeatingCallback<void(std::string)> callback) {
    controlled_tab_changed_callback_ = std::move(callback);
  }

  // R-12: Applies the private-window top-bar treatment. Mail visibility is
  // recomputed from the profile's off-the-record state and enabled pref.
  void SetPrivateAppearance();
  bool ShowAppMenu();
  void SetNavClusterVisible(bool visible);
  // Shows/hides the leading action cluster (sidebar-toggle button). Hidden in
  // library mode so the collapse action is not exposed over the library rail.
  void SetLeadingActionsVisible(bool visible);
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void AddedToWidget() override;
  void OnControlActivityChanged(
      const maho::ai::ControlActivity& activity) override;

  views::View* leading_cluster_for_testing() { return leading_cluster_; }
  views::View* nav_cluster_for_testing() { return nav_cluster_; }
  views::View* flex_spacer_for_testing() { return flex_spacer_; }
  views::View* traffic_light_spacer_for_testing() {
    return traffic_light_spacer_;
  }
  views::ImageButton* sidebar_toggle_button_for_testing() {
    return sidebar_toggle_button_;
  }
  views::ImageButton* ai_button_for_testing() { return ai_button_; }
  views::ImageButton* mail_button_for_testing() { return mail_button_; }
  views::Label* mail_unread_badge_for_testing() { return mail_unread_badge_; }
  const MahoSidebarPalette& sidebar_palette_for_testing() const {
    return palette_;
  }

  // Returns true if `point` (in this view's coordinates) is not over any
  // interactive control and should be treated as window-draggable caption area.
  bool IsPositionInWindowCaption(const gfx::Point& point) const;

 private:
  void ApplyChromeSpacing();
  void UpdateButtonImages();
  // Rewrites BoxLayout `inside_border_insets` so the action-icon row's
  // cross-axis center matches the runtime traffic-light center from
  // GetTrafficLightCenterYInView(). No-op on non-macOS or when NSWindow is
  // unavailable (unit tests, popup/devtools windows). Idempotent: only
  // invalidates layout on inset change, so does not loop with
  // OnBoundsChanged.
  void MaybeUpdateDynamicTopInset();
  void UpdateButtonState(views::ImageButton* button,
                         bool enabled,
                         const gfx::VectorIcon& icon,
                         const std::u16string& tooltip);
  void OnAiPressed(const ui::Event& event);
  void OnMailPressed(const ui::Event& event);
  // Shows/hides the Mail entry point from the canonical enabled pref while
  // keeping it hidden for off-the-record profiles.
  void UpdateMailButtonVisibility();
  void UpdateMailUnreadBadge();
  void OnToggleSidebarPressed(const ui::Event& event);
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  void OnMenuPressed(const ui::Event& event);
  bool ShowAppMenuWithRunFlags(int run_flags);
  void OnAppMenuClosed(int generation);
  void ResetAppMenu(int generation);
#endif

  raw_ptr<Browser> browser_;
  bool leading_actions_visible_ = true;
  raw_ptr<views::View> traffic_light_spacer_ = nullptr;
  raw_ptr<views::View> leading_cluster_ = nullptr;
  raw_ptr<views::View> nav_cluster_ = nullptr;
  raw_ptr<views::View> flex_spacer_ = nullptr;
  raw_ptr<views::ImageButton> sidebar_toggle_button_ = nullptr;
  raw_ptr<views::ImageButton> ai_button_ = nullptr;
  raw_ptr<views::ImageButton> mail_button_ = nullptr;
  raw_ptr<views::Label> mail_unread_badge_ = nullptr;
  PrefChangeRegistrar mail_pref_registrar_;
  MahoSidebarPalette palette_;
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  raw_ptr<views::ImageButton> ellipsis_button_ = nullptr;
  std::unique_ptr<AppMenuModel> app_menu_model_;
  std::unique_ptr<AppMenu> app_menu_;
  int app_menu_generation_ = 0;
#endif
  MahoSidebarTopBarModel model_;
  raw_ptr<maho::ai::MahoControlActivityService> control_activity_service_ =
      nullptr;
  uint64_t control_activity_revision_ = 0;
  std::string control_activity_id_;
  base::RepeatingCallback<void(std::string)> controlled_tab_changed_callback_;
  base::WeakPtrFactory<MahoSidebarTopBarView> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_TOP_BAR_VIEW_H_
