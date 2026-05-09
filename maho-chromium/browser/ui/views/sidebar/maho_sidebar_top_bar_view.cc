// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_top_bar_view.h"

#include <memory>
#include <string_view>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho-chromium/browser/ui/views/sidebar/maho_sidebar_top_bar_mac_sf_symbol_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/components/constants/webui_url_constants.h"
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

constexpr int kTopBarBottomMarginDp = 0;
constexpr int kTopBarClusterHorizontalInsetDp = 0;
constexpr int kTopBarClusterVerticalInsetDp = 0;
constexpr float kTopBarNavTintAlpha = 0.68f;
constexpr float kTopBarDisabledAlpha = 0.4f;

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

ui::ColorId GetTopBarButtonColor(bool enabled) {
  return enabled ? ui::kColorSysOnSurface : ui::kColorSysOnSurfaceSubtle;
}

views::ImageButton* AddControlButton(views::View* parent,
                                     views::Button::PressedCallback callback,
                                     std::string_view symbol_name,
                                     std::string_view accessibility_description,
                                     const std::u16string& tooltip,
                                     bool enabled = true) {
  auto button = views::CreateVectorImageButton(std::move(callback));
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateSidebarTopBarMacSFSymbol(symbol_name, accessibility_description,
                                     sidebar_layout::kTopBarIconSizeDp,
                                     GetTopBarButtonColor(enabled),
                                     enabled ? kTopBarNavTintAlpha
                                             : kTopBarDisabledAlpha));
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

  traffic_light_spacer_ = AddChildView(std::make_unique<views::View>());
  traffic_light_spacer_->SetPreferredSize(gfx::Size(
      sidebar_layout::kTrafficLightAlignmentWidthDp,
      sidebar_layout::kTopBarTrafficLightAlignmentHeightDp));

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
      "sidebar.left", "Toggle Sidebar", u"Toggle Sidebar");

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
      "chevron.left", "Back", u"Back");
  forward_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnForwardPressed,
                          weak_factory_.GetWeakPtr()),
      "chevron.right", "Forward", u"Forward");
  reload_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnReloadPressed,
                          weak_factory_.GetWeakPtr()),
      "arrow.clockwise", "Reload", u"Reload");
  ai_button_ = AddControlButton(
      nav_cluster_,
      base::BindRepeating(&MahoSidebarTopBarView::OnAiPressed,
                          weak_factory_.GetWeakPtr()),
      "sparkles", "AI Panel", u"AI Panel");

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);

  ApplyChromeSpacing();
}

MahoSidebarTopBarView::~MahoSidebarTopBarView() = default;

void MahoSidebarTopBarView::Update(const MahoSidebarTopBarModel& model) {
  model_ = model;
  ApplyChromeSpacing();
  UpdateButtonImages();
}

void MahoSidebarTopBarView::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  views::View::OnBoundsChanged(previous_bounds);
  ApplyChromeSpacing();
}

bool MahoSidebarTopBarView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  for (const views::ImageButton* button :
       {sidebar_toggle_button_.get(), back_button_.get(),
        forward_button_.get(), reload_button_.get(), ai_button_.get()}) {
    if (button && button->GetVisible()) {
      gfx::Point point_in_button = point;
      views::View::ConvertPointToTarget(this, button, &point_in_button);
      if (button->HitTestPoint(point_in_button)) {
        return false;
      }
    }
  }
  return true;
}

void MahoSidebarTopBarView::ApplyChromeSpacing() {
  auto* leading_layout =
      static_cast<views::BoxLayout*>(leading_cluster_->GetLayoutManager());
  auto* nav_layout =
      static_cast<views::BoxLayout*>(nav_cluster_->GetLayoutManager());

  const bool show_nav = model_.navigation_enabled;

  leading_layout->set_between_child_spacing(
      show_nav ? sidebar_layout::kTopBarCompactLeadingGapDp
               : sidebar_layout::kTopBarButtonSpacingDp);
  nav_layout->set_between_child_spacing(sidebar_layout::kTopBarButtonSpacingDp);

  leading_cluster_->SetVisible(show_nav);
  leading_cluster_->SetProperty(
      views::kMarginsKey,
      gfx::Insets::TLBR(0, 0, 0,
                         show_nav ? sidebar_layout::kTopBarCompactLeadingGapDp
                                  : 0));
  flex_spacer_->SetVisible(show_nav);
  nav_cluster_->SetVisible(show_nav);

  StyleClusterView(leading_cluster_);
  StyleClusterView(nav_cluster_);
  PreferredSizeChanged();
}

void MahoSidebarTopBarView::UpdateButtonImages() {
  UpdateButtonState(sidebar_toggle_button_, true, "sidebar.left",
                    "Toggle Sidebar", u"Toggle Sidebar");
  UpdateButtonState(back_button_, model_.navigation_enabled && model_.can_go_back,
                    "chevron.left", "Back", u"Back");
  UpdateButtonState(forward_button_,
                    model_.navigation_enabled && model_.can_go_forward,
                    "chevron.right", "Forward", u"Forward");
  UpdateButtonState(reload_button_,
                    model_.navigation_enabled && model_.can_reload,
                    model_.is_loading ? "xmark" : "arrow.clockwise",
                    model_.is_loading ? "Stop" : "Reload",
                    model_.is_loading ? u"Stop" : u"Reload");
  UpdateButtonState(ai_button_, true, "sparkles", "AI Panel", u"AI Panel");
}

void MahoSidebarTopBarView::UpdateButtonState(
    views::ImageButton* button,
    bool enabled,
    std::string_view symbol_name,
    std::string_view accessibility_description,
    const std::u16string& tooltip) {
  if (!button) {
    return;
  }

  button->SetEnabled(enabled);
  button->SetTooltipText(tooltip);
  button->SetAccessibleName(tooltip);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateSidebarTopBarMacSFSymbol(symbol_name, accessibility_description,
                                     sidebar_layout::kTopBarIconSizeDp,
                                     GetTopBarButtonColor(enabled),
                                     enabled ? kTopBarNavTintAlpha
                                             : kTopBarDisabledAlpha));
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

void MahoSidebarTopBarView::OnToggleSidebarPressed(const ui::Event& event) {
  if (browser_) {
    sidebar_prefs::ToggleSidebarPanelExpanded(browser_->profile()->GetPrefs());
  }
}

}  // namespace maho
