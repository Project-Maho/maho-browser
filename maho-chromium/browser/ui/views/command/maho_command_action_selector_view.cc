// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_action_selector_view.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/size.h"

namespace maho {

namespace {

constexpr int kSelectorCornerRadiusDp = 8;
constexpr int kSegmentCornerRadiusDp = 6;
constexpr int kSegmentWidthDp = 64;
constexpr int kSegmentHeightDp = 26;

}

class SegmentButton : public views::LabelButton {
 public:
  METADATA_HEADER(SegmentButton, views::LabelButton)
 public:
  SegmentButton(PressedCallback callback, const std::u16string& text)
      : views::LabelButton(std::move(callback), text) {
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetElideBehavior(gfx::NO_ELIDE);
    SetPreferredSize(gfx::Size(kSegmentWidthDp, kSegmentHeightDp));
    SetMinSize(gfx::Size(kSegmentWidthDp, kSegmentHeightDp));
    SetFocusRingCornerRadius(kSegmentCornerRadiusDp);
    label()->SetSubpixelRenderingEnabled(false);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
  }

  void SetSelectedVisuals(bool selected, SkColor accent_color) {
    selected_ = selected;
    if (selected) {
      if (const auto* cp = GetColorProvider()) {
        SetBackground(views::CreateRoundedRectBackground(
            cp->GetColor(kMahoColorCommandBarInputFill),
            kSegmentCornerRadiusDp));
        SetBorder(views::CreateRoundedRectBorder(
            1, kSegmentCornerRadiusDp,
            cp->GetColor(kMahoColorCommandBarKeycapBorder)));
        SetEnabledTextColors(cp->GetColor(kMahoColorPrimaryText));
      }
      GetViewAccessibility().SetIsSelected(true);
    } else {
      SetBackground(nullptr);
      SetBorder(views::CreateSolidBorder(1, SkColorSetA(accent_color, 0x00)));
      if (GetColorProvider()) {
        SetEnabledTextColors(GetColorProvider()->GetColor(ui::kColorLabelForegroundSecondary));
      }
      GetViewAccessibility().SetIsSelected(false);
    }
  }

  void OnThemeChanged() override {
    views::LabelButton::OnThemeChanged();
    // Refresh text color when theme changes to ensure unselected gets the proper secondary text color
    if (!selected_ && GetColorProvider()) {
      SetEnabledTextColors(GetColorProvider()->GetColor(ui::kColorLabelForegroundSecondary));
    }
  }

 private:
  bool selected_ = false;
};

MahoCommandActionSelectorView::MahoCommandActionSelectorView(
    base::RepeatingCallback<void(PaletteAction)> on_change)
    : on_change_(std::move(on_change)) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kRadioGroup);
  GetViewAccessibility().SetName(u"Command Palette Action Selector");
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCommandBarShortcutBadge, kSelectorCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(
      1, kSelectorCornerRadiusDp, kMahoColorCommandBarKeycapBorder));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(3, 3), 2));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  search_button_ = AddChildView(std::make_unique<SegmentButton>(
      base::BindRepeating(&MahoCommandActionSelectorView::OnSegmentClicked,
                          base::Unretained(this), PaletteAction::kSearch),
      u"Search"));
  search_button_->GetViewAccessibility().SetName(u"Search Mode");

  ask_maho_button_ = AddChildView(std::make_unique<SegmentButton>(
      base::BindRepeating(&MahoCommandActionSelectorView::OnSegmentClicked,
                          base::Unretained(this), PaletteAction::kAskMaho),
      u"Ask Maho"));
  ask_maho_button_->GetViewAccessibility().SetName(u"Ask Maho Mode");

  hint_label_ = AddChildView(std::make_unique<views::Label>(
      u"Tab to switch", views::style::CONTEXT_LABEL,
      views::style::STYLE_SECONDARY));
  hint_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  hint_label_->SetSubpixelRenderingEnabled(false);
  hint_label_->SetVisible(false);

  UpdateVisuals();
}

MahoCommandActionSelectorView::~MahoCommandActionSelectorView() = default;

void MahoCommandActionSelectorView::SetSelected(PaletteAction action) {
  if (selected_ == action) {
    return;
  }
  selected_ = action;
  UpdateVisuals();
  GetViewAccessibility().NotifyEvent(ax::mojom::Event::kSelectedChildrenChanged, true);
  on_change_.Run(selected_);
}

void MahoCommandActionSelectorView::SetEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }
  enabled_ = enabled;
  search_button_->SetEnabled(enabled);
  ask_maho_button_->SetEnabled(enabled);
  UpdateVisuals();
}



void MahoCommandActionSelectorView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateVisuals();
}

void MahoCommandActionSelectorView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::View>(this);
}

void MahoCommandActionSelectorView::OnSegmentClicked(PaletteAction action) {
  if (!enabled_) {
    return;
  }
  SetSelected(action);
}

void MahoCommandActionSelectorView::UpdateVisuals() {
  if (!GetColorProvider()) {
    return;
  }

  SkColor accent_color;
  if (!enabled_) {
    accent_color = GetColorProvider()->GetColor(ui::kColorLabelForegroundDisabled);
  } else {
    accent_color = GetColorProvider()->GetColor(
        selected_ == PaletteAction::kAskMaho ? kMahoColorAccentBlueBright
                                             : kMahoColorAccentBlue);
  }

  static_cast<SegmentButton*>(search_button_)
      ->SetSelectedVisuals(selected_ == PaletteAction::kSearch, accent_color);
  static_cast<SegmentButton*>(ask_maho_button_)
      ->SetSelectedVisuals(selected_ == PaletteAction::kAskMaho, accent_color);

  // If disabled, apply disabled colors to the label as well
  if (!enabled_) {
    hint_label_->SetEnabledColor(GetColorProvider()->GetColor(ui::kColorLabelForegroundDisabled));
  } else {
    hint_label_->SetEnabledColor(GetColorProvider()->GetColor(ui::kColorLabelForegroundSecondary));
  }
}

BEGIN_METADATA(SegmentButton)
END_METADATA

BEGIN_METADATA(MahoCommandActionSelectorView)
END_METADATA

}  // namespace maho
