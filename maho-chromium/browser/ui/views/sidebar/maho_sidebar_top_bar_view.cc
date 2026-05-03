// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/components/constants/webui_url_constants.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/common/referrer.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
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
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace maho {

namespace {

constexpr int kControlClusterSpacingDp = 4;
constexpr int kTopRowTrailingInsetDp = 2;
constexpr int kTopBarBottomMarginDp = 0;
constexpr int kTrafficLightAlignmentHeightDp = 16;
constexpr int kTopBarClusterCornerRadiusDp = 11;
constexpr int kTopBarClusterHorizontalInsetDp = 3;
constexpr int kTopBarClusterVerticalInsetDp = 1;

void ApplyButtonChrome(views::ImageButton* button) {
  button->SetBackground(nullptr);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 0)));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
}

void StyleClusterView(views::View* cluster) {
  cluster->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface1, kTopBarClusterCornerRadiusDp));
  cluster->SetBorder(views::CreateRoundedRectBorder(
      1, kTopBarClusterCornerRadiusDp, ui::kColorSysSurfaceVariant));
}

views::ImageButton* AddControlButton(views::View* parent,
                                     views::Button::PressedCallback callback,
                                     const gfx::VectorIcon& icon,
                                     const std::u16string& tooltip) {
  auto button = views::CreateVectorImageButtonWithNativeTheme(
      std::move(callback), icon, sidebar_layout::kTopBarIconSizeDp,
      ui::kColorSysOnSurfaceSubtle, ui::kColorSysOnSurface,
      ui::kColorSysOnSurface);
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetPreferredSize(gfx::Size(sidebar_layout::kTopBarButtonSizeDp,
                                     sidebar_layout::kTopBarButtonSizeDp));
  views::InstallCircleHighlightPathGenerator(button.get());
  ApplyButtonChrome(button.get());
  return parent->AddChildView(std::move(button));
}

}  // namespace

BEGIN_METADATA(MahoSidebarTopBarView)
END_METADATA

MahoSidebarTopBarView::MahoSidebarTopBarView(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      sidebar_layout::kTopBarInsets, 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetProperty(views::kMarginsKey,
              gfx::Insets::TLBR(0, 0, kTopBarBottomMarginDp, 0));

  traffic_light_spacer_ = AddChildView(std::make_unique<views::View>());
  traffic_light_spacer_->SetPreferredSize(gfx::Size(
      sidebar_layout::kTrafficLightAlignmentWidthDp,
      kTrafficLightAlignmentHeightDp));

  leading_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* controls_layout =
      leading_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          kControlClusterSpacingDp));
  controls_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  leading_cluster_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, 0, 0, kTopRowTrailingInsetDp));

  sidebar_toggle_button_ = AddControlButton(
      leading_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnToggleSidebarPressed,
                          weak_factory_.GetWeakPtr()),
      kDockToLeftIcon, u"Toggle Sidebar");

  auto* left_spacer = AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(left_spacer, 1);

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
      vector_icons::kArrowBackChromeRefreshIcon, u"Back");
  forward_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnForwardPressed,
                          weak_factory_.GetWeakPtr()),
      vector_icons::kForwardArrowIcon, u"Forward");

  auto* right_spacer = AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(right_spacer, 1);

  trailing_cluster_ = AddChildView(std::make_unique<views::View>());
  auto* trailing_layout =
      trailing_cluster_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kTopBarClusterVerticalInsetDp,
                          kTopBarClusterHorizontalInsetDp),
          kControlClusterSpacingDp));
  trailing_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  reload_button_ = AddControlButton(
      trailing_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnReloadPressed,
                          weak_factory_.GetWeakPtr()),
      vector_icons::kReloadIcon, u"Reload");
  ai_button_ = AddControlButton(
      trailing_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnAiPressed,
                          weak_factory_.GetWeakPtr()),
      omnibox::kSparkIcon, u"AI Panel");

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);
  StyleClusterView(trailing_cluster_);

  ApplyChromeSpacing();
}

MahoSidebarTopBarView::~MahoSidebarTopBarView() = default;

void MahoSidebarTopBarView::Update(const MahoSidebarTopBarModel& model) {
  model_ = model;
  ApplyChromeSpacing();
  back_button_->SetEnabled(model.navigation_enabled && model.can_go_back);
  forward_button_->SetEnabled(model.navigation_enabled && model.can_go_forward);
  reload_button_->SetEnabled(model.navigation_enabled && model.can_reload);
  reload_button_->SetTooltipText(model.is_loading ? u"Stop" : u"Reload");
  reload_button_->SetAccessibleName(model.is_loading ? u"Stop" : u"Reload");
  reload_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          model.is_loading ? vector_icons::kCloseChromeRefreshIcon
                           : vector_icons::kReloadIcon,
          ui::kColorSysOnSurfaceSubtle,
          sidebar_layout::kTopBarIconSizeDp));
  sidebar_toggle_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kDockToLeftIcon,
                                     ui::kColorSysOnSurfaceSubtle,
                                     sidebar_layout::kTopBarIconSizeDp));
  back_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          vector_icons::kArrowBackChromeRefreshIcon,
          ui::kColorSysOnSurfaceSubtle, sidebar_layout::kTopBarIconSizeDp));
  forward_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kForwardArrowIcon,
                                     ui::kColorSysOnSurfaceSubtle,
                                     sidebar_layout::kTopBarIconSizeDp));
  ai_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(omnibox::kSparkIcon,
                                     ui::kColorSysOnSurfaceSubtle,
                                     sidebar_layout::kTopBarIconSizeDp));
}

void MahoSidebarTopBarView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  views::View::OnBoundsChanged(previous_bounds);
  ApplyChromeSpacing();
}

void MahoSidebarTopBarView::ApplyChromeSpacing() {
  auto* leading_layout =
      static_cast<views::BoxLayout*>(leading_cluster_->GetLayoutManager());
  auto* nav_layout =
      static_cast<views::BoxLayout*>(nav_cluster_->GetLayoutManager());
  auto* trailing_layout =
      static_cast<views::BoxLayout*>(trailing_cluster_->GetLayoutManager());

  leading_layout->set_between_child_spacing(kControlClusterSpacingDp);
  nav_layout->set_between_child_spacing(kControlClusterSpacingDp);
  trailing_layout->set_between_child_spacing(kControlClusterSpacingDp);
  leading_cluster_->SetProperty(
      views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, kTopRowTrailingInsetDp));
  const bool show_nav = model_.navigation_enabled;
  nav_cluster_->SetVisible(show_nav);
  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);
  StyleClusterView(trailing_cluster_);
  trailing_cluster_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, 0, 0,
                        model_.navigation_enabled ? 0
                                                  : sidebar_layout::kTopBarCompactTrailingInsetDp));
  PreferredSizeChanged();
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
  browser_->GetFeatures().side_panel_ui()->Show(SidePanelEntryId::kMahoAiPanel);
}

void MahoSidebarTopBarView::OnToggleSidebarPressed(const ui::Event& event) {
  if (browser_) {
    sidebar_prefs::ToggleSidebarVisibilityInShell(browser_->profile()->GetPrefs());
  }
}

}  // namespace maho
