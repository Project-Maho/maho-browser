// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"

#include "base/functional/bind.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

// Dimensions for the trailing utility icon inside the search pill's action area.
// The icon occupies a compact slot at the right edge of the pill, sized to feel
// proportional to the pill height without crowding adjacent controls.
constexpr int kUtilityButtonWidth = 24;   // Horizontal hit target within the pill slot.
constexpr int kUtilityButtonHeight = 20;  // Shorter than pill height; vertically centered.
constexpr int kUtilityIconSize = 12;      // SF Symbol render size; small to avoid visual weight.
constexpr int kUtilityButtonCornerRadius = 8;  // Matches the pill's inner rounding.

ui::ImageModel CreateUtilityButtonImage(ui::ColorId color_id) {
  return ui::ImageModel::FromVectorIcon(
      maho_lucide_icons::kSlidersHorizontalIcon, color_id, kUtilityIconSize);
}

void UpdateUtilityButtonImages(views::ToggleImageButton* button) {
  button->SetImageModel(views::Button::STATE_NORMAL,
                        CreateUtilityButtonImage(ui::kColorIcon));
  button->SetImageModel(views::Button::STATE_HOVERED,
                        CreateUtilityButtonImage(ui::kColorIcon));
  button->SetImageModel(views::Button::STATE_PRESSED,
                        CreateUtilityButtonImage(ui::kColorIcon));
  button->SetImageModel(views::Button::STATE_DISABLED,
                        CreateUtilityButtonImage(ui::kColorIconDisabled));
  button->SetToggledImageModel(views::Button::STATE_NORMAL,
                               CreateUtilityButtonImage(ui::kColorIcon));
  button->SetToggledImageModel(views::Button::STATE_HOVERED,
                               CreateUtilityButtonImage(ui::kColorIcon));
  button->SetToggledImageModel(views::Button::STATE_PRESSED,
                               CreateUtilityButtonImage(ui::kColorIcon));
  button->SetToggledImageModel(views::Button::STATE_DISABLED,
                               CreateUtilityButtonImage(ui::kColorIconDisabled));
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
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  UpdateUtilityButtonImages(this);
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
  UpdateUtilityButtonImages(this);
  UpdateButtonChrome();
}

void MahoLocationBarUtilityIconView::SetAffordanceVisible(
    bool affordance_visible) {
  affordance_visible_ = affordance_visible;
  UpdateVisibilityState();
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
  SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  SetBackground(active ? views::CreateRoundedRectBackground(
                             ui::kColorSysSurfaceVariant,
                             kUtilityButtonCornerRadius)
                       : nullptr);
}

void MahoLocationBarUtilityIconView::UpdateVisibilityState() {
  const bool should_show = affordance_visible_ || hover_visible_ || panel_widget_;
  SetVisible(should_show);
  SetToggled(panel_widget_ != nullptr);
  UpdateButtonChrome();
}

}  // namespace maho
