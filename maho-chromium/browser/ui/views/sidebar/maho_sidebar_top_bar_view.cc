// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_top_bar_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"
#include "maho/components/constants/webui_url_constants.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/controls/label.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/window_show_state.mojom.h"

namespace maho {

namespace {

// Extra margin below the top-bar row (currently zero; the kTopBarInsets bottom
// value handles separation from the search pill).
constexpr int kTopBarBottomMarginDp = 0;
// Internal padding inside each button cluster (leading and nav).
constexpr int kTopBarClusterHorizontalInsetDp = 0;
constexpr int kTopBarClusterVerticalInsetDp = 0;
// Left margin on the sidebar-toggle button, shifting it inward from the
// traffic-light spacer edge for optical alignment with macOS window controls.
constexpr int kSidebarToggleLeftMarginDp = 6;

ui::ImageModel CreateTopBarButtonImage(
    const gfx::VectorIcon& icon,
    int icon_size,
    ui::ColorId color_id) {
  return ui::ImageModel::FromVectorIcon(icon, color_id, icon_size);
}

void ApplyButtonChrome(views::ImageButton* button) {
  button->SetBackground(nullptr);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 0)));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);
}

void StyleClusterView(views::View* cluster) {
  cluster->SetBackground(nullptr);
  cluster->SetBorder(nullptr);
}

gfx::Rect GetButtonIconHitRect(const views::ImageButton* button) {
  gfx::Rect icon_hit_rect(button->GetLocalBounds());
  const int icon_hit_width =
      std::min(icon_hit_rect.width(), sidebar_layout::kTopBarIconSizeDp);
  const int icon_hit_height =
      std::min(icon_hit_rect.height(), sidebar_layout::kTopBarIconSizeDp);
  const int icon_hit_x = icon_hit_rect.x() +
                         (icon_hit_rect.width() - icon_hit_width) / 2;
  const int icon_hit_y = icon_hit_rect.y() +
                         (icon_hit_rect.height() - icon_hit_height) / 2;
  return gfx::Rect(icon_hit_x, icon_hit_y, icon_hit_width, icon_hit_height);
}

ui::ColorId GetTopBarButtonColor(bool enabled) {
  return enabled ? ui::kColorSysOnSurface : ui::kColorSysOnSurfaceSubtle;
}

views::ImageButton* AddControlButton(views::View* parent,
                                     views::Button::PressedCallback callback,
                                     const gfx::VectorIcon& icon,
                                     const std::u16string& tooltip,
                                     bool enabled = true) {
  auto button = views::CreateVectorImageButton(std::move(callback));
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateTopBarButtonImage(icon,
                              sidebar_layout::kTopBarIconSizeDp,
                              GetTopBarButtonColor(enabled)));
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetPreferredSize(gfx::Size(sidebar_layout::kTopBarButtonSizeDp,
                                     sidebar_layout::kTopBarButtonSizeDp));
  button->SetEnabled(enabled);
  views::InstallCircleHighlightPathGenerator(button.get());
  ApplyButtonChrome(button.get());
  return parent->AddChildView(std::move(button));
}

}  // namespace

BEGIN_METADATA(MahoSidebarTopBarView)
END_METADATA

MahoSidebarTopBarView::MahoSidebarTopBarView(Browser* browser)
    : browser_(browser) {
  // Horizontal BoxLayout: [traffic-light spacer | leading cluster | flex | nav cluster]
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(sidebar_layout::kTopBarInsets.top(),
                        sidebar_layout::kTopBarInsets.left(),
                        sidebar_layout::kTopBarInsets.bottom(),
                        sidebar_layout::kTopBarInsets.right()),
      0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetProperty(views::kMarginsKey,
              gfx::Insets::TLBR(0, 0, kTopBarBottomMarginDp, 0));

  // Fixed-width spacer reserving room for macOS traffic-light buttons.
  traffic_light_spacer_ = AddChildView(std::make_unique<views::View>());
  traffic_light_spacer_->SetPreferredSize(gfx::Size(
      sidebar_layout::kTrafficLightAlignmentWidthDp,
      sidebar_layout::kTopBarTrafficLightAlignmentHeightDp));
#if BUILDFLAG(IS_WIN)
  auto* menu_layout = traffic_light_spacer_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  menu_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  ellipsis_button_ = AddControlButton(
      traffic_light_spacer_,
      base::BindRepeating(&MahoSidebarTopBarView::OnMenuPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kEllipsisIcon, u"Menu");
#endif

  // Leading cluster: sidebar-toggle (and any future leading actions).
  leading_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* leading_layout =
      leading_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          sidebar_layout::kTopBarButtonSpacingDp));
  leading_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  sidebar_toggle_button_ = AddControlButton(
      leading_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnToggleSidebarPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kPanelLeftIcon, u"Toggle Sidebar");
  sidebar_toggle_button_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, kSidebarToggleLeftMarginDp, 0, 0));
  // Flexible spacer pushes the nav cluster to the trailing edge.
  flex_spacer_ = AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(flex_spacer_, 1);

  nav_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* nav_layout =
      nav_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          sidebar_layout::kTopBarButtonSpacingDp));
  nav_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  back_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnBackPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kChevronLeftIcon, u"Back");
  forward_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnForwardPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kChevronRightIcon, u"Forward");
  reload_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnReloadPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kRotateCwIcon, u"Reload");
  mail_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnMailPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kInboxIcon, u"Mail");
  ai_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnAiPressed,
                          weak_factory_.GetWeakPtr()),
      maho_lucide_icons::kMessageSquareIcon, u"AI Panel");

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);

  ApplyChromeSpacing();
  UpdateButtonImages();
}

MahoSidebarTopBarView::~MahoSidebarTopBarView() = default;

void MahoSidebarTopBarView::SetNavClusterVisible(bool visible) {
  if (nav_cluster_ && nav_cluster_->GetVisible() != visible) {
    nav_cluster_->SetVisible(visible);
  }
}

void MahoSidebarTopBarView::Update(const MahoSidebarTopBarModel& model) {
  model_ = model;
  ApplyChromeSpacing();
  UpdateButtonImages();
}

void MahoSidebarTopBarView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  views::View::OnBoundsChanged(previous_bounds);
  MaybeUpdateDynamicTopInset();
  ApplyChromeSpacing();
}

void MahoSidebarTopBarView::AddedToWidget() {
  views::View::AddedToWidget();
  MaybeUpdateDynamicTopInset();
}

void MahoSidebarTopBarView::MaybeUpdateDynamicTopInset() {
  auto* layout = static_cast<views::BoxLayout*>(GetLayoutManager());
  if (!layout) {
    return;
  }
  std::optional<int> measured_center_y = GetTrafficLightCenterYInView(this);
  if (!measured_center_y.has_value()) {
    return;
  }
  const int desired_top_inset =
      std::max(0, *measured_center_y -
                      sidebar_layout::kTopBarTrafficLightAlignmentHeightDp / 2);
  const gfx::Insets current = layout->inside_border_insets();
  if (current.top() == desired_top_inset) {
    return;
  }
  layout->set_inside_border_insets(gfx::Insets::TLBR(
      desired_top_inset, current.left(), current.bottom(), current.right()));
  InvalidateLayout();
}

bool MahoSidebarTopBarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
#if BUILDFLAG(IS_WIN)
  if (ellipsis_button_ && ellipsis_button_->GetVisible()) {
    gfx::Point point_in_button = point;
    views::View::ConvertPointToTarget(this, ellipsis_button_,
                                      &point_in_button);
    if (GetButtonIconHitRect(ellipsis_button_).Contains(point_in_button)) {
      return false;
    }
  }
#endif
  for (const views::ImageButton* button :
       {sidebar_toggle_button_.get(), back_button_.get(),
         forward_button_.get(), reload_button_.get(), ai_button_.get(),
         mail_button_.get()}) {
    if (button && button->GetVisible()) {
      gfx::Point point_in_button = point;
      views::View::ConvertPointToTarget(this, button, &point_in_button);
      if (GetButtonIconHitRect(button).Contains(point_in_button)) {
        return false;
      }
    }
  }
  return true;
}

// Adjusts spacing based on whether in-sidebar navigation is active. The chrome
// stays visible even before the first navigable tab exists so startup/no-tab
// states preserve the same top-row scaffold and caption behavior; button
// enabled state is handled separately in UpdateButtonImages().
void MahoSidebarTopBarView::ApplyChromeSpacing() {
  auto* leading_layout =
      static_cast<views::BoxLayout*>(leading_cluster_->GetLayoutManager());
  auto* nav_layout =
      static_cast<views::BoxLayout*>(nav_cluster_->GetLayoutManager());

  const bool navigation_active = model_.navigation_enabled;

  leading_layout->set_between_child_spacing(
      navigation_active ? sidebar_layout::kTopBarCompactLeadingGapDp
                        : sidebar_layout::kTopBarButtonSpacingDp);
  nav_layout->set_between_child_spacing(sidebar_layout::kTopBarButtonSpacingDp);

  leading_cluster_->SetVisible(true);
  leading_cluster_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, 0, 0,
                        navigation_active
                            ? sidebar_layout::kTopBarCompactLeadingGapDp
                            : 0));
  flex_spacer_->SetVisible(true);
  nav_cluster_->SetVisible(true);

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);
  PreferredSizeChanged();
}

void MahoSidebarTopBarView::UpdateButtonImages() {
#if BUILDFLAG(IS_WIN)
  UpdateButtonState(ellipsis_button_, true, maho_lucide_icons::kEllipsisIcon,
                    u"Menu");
#endif
  UpdateButtonState(sidebar_toggle_button_, true,
                    maho_lucide_icons::kPanelLeftIcon, u"Toggle Sidebar");
  UpdateButtonState(back_button_, model_.navigation_enabled && model_.can_go_back,
                    maho_lucide_icons::kChevronLeftIcon, u"Back");
  UpdateButtonState(forward_button_,
                    model_.navigation_enabled && model_.can_go_forward,
                    maho_lucide_icons::kChevronRightIcon, u"Forward");
  UpdateButtonState(reload_button_,
                    model_.navigation_enabled && model_.can_reload,
                    model_.is_loading ? maho_lucide_icons::kXIcon
                                     : maho_lucide_icons::kRotateCwIcon,
                    model_.is_loading ? u"Stop" : u"Reload");
  UpdateButtonState(ai_button_, true,
                    maho_lucide_icons::kMessageSquareIcon, u"AI Panel");
  UpdateButtonState(mail_button_, true, maho_lucide_icons::kInboxIcon,
                    u"Mail");
}

void MahoSidebarTopBarView::UpdateButtonState(
    views::ImageButton* button,
    bool enabled,
    const gfx::VectorIcon& icon,
    const std::u16string& tooltip) {
  if (!button) {
    return;
  }

  button->SetEnabled(enabled);
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateTopBarButtonImage(icon,
                              sidebar_layout::kTopBarIconSizeDp,
                              GetTopBarButtonColor(enabled)));
}

void MahoSidebarTopBarView::OnBackPressed(const ui::Event& event) {
  if (browser_) {
    chrome::GoBack(browser_, WindowOpenDisposition::CURRENT_TAB);
  }
}

void MahoSidebarTopBarView::OnForwardPressed(const ui::Event& event) {
  if (browser_) {
    chrome::GoForward(browser_, WindowOpenDisposition::CURRENT_TAB);
  }
}

void MahoSidebarTopBarView::OnReloadPressed(const ui::Event& event) {
  if (browser_) {
    chrome::Reload(browser_, WindowOpenDisposition::CURRENT_TAB);
  }
}

void MahoSidebarTopBarView::OnAiPressed(const ui::Event& event) {
  if (!browser_) {
    return;
  }

  auto* side_panel_ui = browser_->GetFeatures().side_panel_ui();
  if (side_panel_ui->GetCurrentEntryId(SidePanelType::kContent) ==
      SidePanelEntryId::kMahoAiPanel) {
    side_panel_ui->Close(SidePanelType::kContent,
                         SidePanelEntryHideReason::kSidePanelClosed,
                         /*suppress_animations=*/true);
    return;
  }

  side_panel_ui->Show(SidePanelEntryKey(SidePanelEntryId::kMahoAiPanel),
                      SidePanelOpenTrigger::kToolbarButton);
}

void MahoSidebarTopBarView::OnMailPressed(const ui::Event& event) {
  if (!browser_) {
    return;
  }

  NavigateParams params(browser_, GURL(maho::kMahoMailURL),
                        ui::PAGE_TRANSITION_GENERATED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void MahoSidebarTopBarView::OnToggleSidebarPressed(const ui::Event& event) {
  if (browser_) {
    sidebar_prefs::ToggleSidebarPanelExpanded(browser_->profile()->GetPrefs());
  }
}

bool MahoSidebarTopBarView::ShowAppMenu() {
#if BUILDFLAG(IS_WIN)
  return ShowAppMenuWithRunFlags(views::MenuRunner::SHOULD_SHOW_MNEMONICS |
                                 views::MenuRunner::INVOKED_FROM_KEYBOARD);
#else
  return false;
#endif
}

#if BUILDFLAG(IS_WIN)
void MahoSidebarTopBarView::OnMenuPressed(const ui::Event& event) {
  ShowAppMenuWithRunFlags(event.IsKeyEvent()
                              ? (views::MenuRunner::SHOULD_SHOW_MNEMONICS |
                                 views::MenuRunner::INVOKED_FROM_KEYBOARD)
                              : views::MenuRunner::NO_FLAGS);
}

bool MahoSidebarTopBarView::ShowAppMenuWithRunFlags(int run_flags) {
  if (!browser_ || !ellipsis_button_ || !ellipsis_button_->GetVisible()) {
    return false;
  }
  if (app_menu_ && app_menu_->IsShowing()) {
    return true;
  }

  views::Widget* widget = GetWidget();
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  ToolbarView* toolbar = browser_view ? browser_view->toolbar() : nullptr;
  if (!widget || !toolbar) {
    return false;
  }

  app_menu_.reset();
  app_menu_model_ = std::make_unique<AppMenuModel>(
      toolbar, browser_, toolbar->app_menu_icon_controller());
  app_menu_model_->Init();
  const int generation = ++app_menu_generation_;
  app_menu_ = std::make_unique<AppMenu>(
      browser_, app_menu_model_.get(), run_flags,
      base::BindRepeating(&MahoSidebarTopBarView::OnAppMenuClosed,
                          weak_factory_.GetWeakPtr(), generation));
  app_menu_->RunMenu(widget, ellipsis_button_->GetBoundsInScreen());
  return true;
}

void MahoSidebarTopBarView::OnAppMenuClosed(int generation) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&MahoSidebarTopBarView::ResetAppMenu,
                                weak_factory_.GetWeakPtr(), generation));
}

void MahoSidebarTopBarView::ResetAppMenu(int generation) {
  if (generation != app_menu_generation_) {
    return;
  }
  app_menu_.reset();
  app_menu_model_.reset();
}
#endif

void MahoSidebarTopBarView::SetPrivateIdentityIcon(
    const ui::ImageModel& icon,
    ui::ColorId text_color_id) {
  // R-11: Presentation-only. No profile/pref reads.
  // Stores the identity icon; actual rendering is done in OnThemeChanged via
  // the private-window background set by SetPrivateAppearance on the sidebar.
  // No-op if the top bar has not been built yet (icon is just stored).
  (void)icon;
  (void)text_color_id;
}

}  // namespace maho
