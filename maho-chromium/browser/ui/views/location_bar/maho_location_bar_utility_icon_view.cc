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
constexpr int kUtilityIconSize = 14;      // SF Symbol render size; small to avoid visual weight.
constexpr int kUtilityButtonCornerRadius = 7;  // Matches copy button chip.

ui::ImageModel CreateUtilityButtonImage(ui::ColorVariant color) {
  return ui::ImageModel::FromVectorIcon(
      maho_lucide_icons::kSlidersHorizontalIcon, color, kUtilityIconSize);
}

void UpdateUtilityButtonImages(views::ToggleImageButton* button,
                               const MahoSidebarPalette& palette) {
  // The sidebar palette is the only color authority on the search pill; an
  // OS-theme ColorId fallback would flash the wrong family before the first
  // palette push and disagree with the Space-themed surface.
  const ui::ColorVariant normal_color(palette.neutral_glyph);
  const ui::ColorVariant active_color(palette.primary_text);
  const ui::ColorVariant disabled_color(palette.disabled_text);
  button->SetImageModel(views::Button::STATE_NORMAL,
                        CreateUtilityButtonImage(normal_color));
  button->SetImageModel(views::Button::STATE_HOVERED,
                        CreateUtilityButtonImage(active_color));
  button->SetImageModel(views::Button::STATE_PRESSED,
                        CreateUtilityButtonImage(active_color));
  button->SetImageModel(views::Button::STATE_DISABLED,
                        CreateUtilityButtonImage(disabled_color));
  button->SetToggledImageModel(views::Button::STATE_NORMAL,
                               CreateUtilityButtonImage(active_color));
  button->SetToggledImageModel(views::Button::STATE_HOVERED,
                               CreateUtilityButtonImage(active_color));
  button->SetToggledImageModel(views::Button::STATE_PRESSED,
                               CreateUtilityButtonImage(active_color));
  button->SetToggledImageModel(views::Button::STATE_DISABLED,
                               CreateUtilityButtonImage(disabled_color));
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
  SetInstallFocusRingOnFocus(true);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  UpdateUtilityButtonImages(this, palette_);
  UpdateButtonChrome();
}

MahoLocationBarUtilityIconView::~MahoLocationBarUtilityIconView() = default;

void MahoLocationBarUtilityIconView::StateChanged(ButtonState old_state) {
  views::ToggleImageButton::StateChanged(old_state);
  UpdateButtonChrome();
}

void MahoLocationBarUtilityIconView::OnThemeChanged() {
  views::ToggleImageButton::OnThemeChanged();
  UpdateUtilityButtonImages(this, palette_);
  UpdateButtonChrome();
}

void MahoLocationBarUtilityIconView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  UpdateUtilityButtonImages(this, palette_);
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

void MahoLocationBarUtilityIconView::SetPanelIsShowing(bool showing) {
  if (panel_is_showing_ == showing) {
    return;
  }
  panel_is_showing_ = showing;
  UpdateVisibilityState();
}

void MahoLocationBarUtilityIconView::TogglePanel() {
  if (!show_panel_callback_) {
    return;
  }
  show_panel_callback_.Run();
}

void MahoLocationBarUtilityIconView::UpdateButtonChrome() {
  const bool active = GetToggled() || hover_visible_ || GetState() == STATE_HOVERED ||
                      GetState() == STATE_PRESSED;
  SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  // Previously fell back to a ui::ColorId implicitly converted to SkColor
  // (an enum value, not a color).
  SetBackground(active ? views::CreateRoundedRectBackground(
                             palette_.row_active, kUtilityButtonCornerRadius)
                       : nullptr);
}

void MahoLocationBarUtilityIconView::UpdateVisibilityState() {
  const bool should_show = affordance_visible_ || hover_visible_ || panel_is_showing_;
  SetVisible(should_show);
  SetToggled(panel_is_showing_);
  UpdateButtonChrome();
}

}  // namespace maho
