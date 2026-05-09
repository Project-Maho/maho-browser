// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"

#include <utility>

#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kUtilityButtonWidth = 24;
constexpr int kUtilityButtonHeight = 20;
constexpr int kUtilityIconSize = 12;
constexpr int kUtilityButtonCornerRadius = 8;

const gfx::VectorIcon& GetUtilitySlidersIcon() {
  static constexpr gfx::PathElement kSlidersPath[39] = {
      gfx::CANVAS_DIMENSIONS, 16,
      gfx::STROKE, 1.6f,
      gfx::MOVE_TO, 2.5f, 4,
      gfx::LINE_TO, 13.5f, 4,
      gfx::NEW_PATH,
      gfx::MOVE_TO, 2.5f, 8,
      gfx::LINE_TO, 13.5f, 8,
      gfx::NEW_PATH,
      gfx::MOVE_TO, 2.5f, 12,
      gfx::LINE_TO, 13.5f, 12,
      gfx::NEW_PATH,
      gfx::CIRCLE, 5, 4, 1.4f,
      gfx::NEW_PATH,
      gfx::CIRCLE, 11, 8, 1.4f,
      gfx::NEW_PATH,
      gfx::CIRCLE, 7, 12, 1.4f,
  };
  static constexpr char kSlidersIconId[] = "maho_utility_sliders";
  static constexpr gfx::VectorIconRep kSlidersRepList[] = {{kSlidersPath}};
  static constexpr gfx::VectorIcon kIcon(kSlidersRepList, 1, kSlidersIconId);
  return kIcon;
}

}  // namespace

BEGIN_METADATA(MahoLocationBarUtilityIconView)
END_METADATA

MahoLocationBarUtilityIconView::MahoLocationBarUtilityIconView(
    ShowPanelCallback show_panel_callback)
    : views::ToggleImageButton(base::BindRepeating(
          &MahoLocationBarUtilityIconView::TogglePanel,
          base::Unretained(this))),
      show_panel_callback_(std::move(show_panel_callback)) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetTooltipText(u"Show utility panel");
  SetToggledTooltipText(u"Hide utility panel");
  SetPreferredSize(gfx::Size(kUtilityButtonWidth, kUtilityButtonHeight));
  SetImageHorizontalAlignment(ALIGN_CENTER);
  SetImageVerticalAlignment(ALIGN_MIDDLE);
  SetInstallFocusRingOnFocus(false);
  views::SetImageFromVectorIconWithColor(
      this, GetUtilitySlidersIcon(), kUtilityIconSize,
      {ui::kColorIcon, ui::kColorIconDisabled});
  views::SetToggledImageFromVectorIconWithColor(
      this, GetUtilitySlidersIcon(), kUtilityIconSize,
      {ui::kColorIcon, ui::kColorIconDisabled});
  UpdateButtonChrome();
}

MahoLocationBarUtilityIconView::~MahoLocationBarUtilityIconView() {
  if (panel_widget_) {
    panel_widget_observation_.Reset();
    panel_widget_ = nullptr;
  }
}

void MahoLocationBarUtilityIconView::StateChanged(ButtonState old_state) {
  views::ToggleImageButton::StateChanged(old_state);
  UpdateButtonChrome();
}

void MahoLocationBarUtilityIconView::OnThemeChanged() {
  views::ToggleImageButton::OnThemeChanged();
  UpdateButtonChrome();
}

void MahoLocationBarUtilityIconView::SetHoverVisible(bool hover_visible) {
  hover_visible_ = hover_visible;
  UpdateVisibilityState();
}

void MahoLocationBarUtilityIconView::SetPanelWidget(views::Widget* panel_widget) {
  if (panel_widget_ == panel_widget) {
    UpdateVisibilityState();
    return;
  }

  if (panel_widget_) {
    panel_widget_observation_.Reset();
  }
  panel_widget_ = panel_widget;
  if (panel_widget_) {
    panel_widget_observation_.Observe(panel_widget_);
  }
  UpdateVisibilityState();
}

void MahoLocationBarUtilityIconView::OnWidgetDestroyed(views::Widget* widget) {
  if (widget != panel_widget_) {
    return;
  }

  panel_widget_observation_.Reset();
  panel_widget_ = nullptr;
  UpdateVisibilityState();
}

void MahoLocationBarUtilityIconView::TogglePanel() {
  if (panel_widget_) {
    panel_widget_->Close();
    return;
  }

  if (!show_panel_callback_) {
    return;
  }

  views::Widget* panel_widget = show_panel_callback_.Run();
  SetPanelWidget(panel_widget);
}

void MahoLocationBarUtilityIconView::UpdateButtonChrome() {
  const bool active = GetToggled() || GetState() == STATE_HOVERED ||
                      GetState() == STATE_PRESSED;
  SetBackground(nullptr);
  SetBorder(views::CreateRoundedRectBorder(
      1, kUtilityButtonCornerRadius,
      active ? ui::kColorSysNeutralOutline : ui::kColorSysSurfaceVariant));
}

void MahoLocationBarUtilityIconView::UpdateVisibilityState() {
  const bool should_show = hover_visible_ || panel_widget_;
  SetVisible(should_show);
  SetToggled(panel_widget_ != nullptr);
  UpdateButtonChrome();
}

}  // namespace maho
