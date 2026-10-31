// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_library_action_pane_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kPaneCornerRadius = 16;
constexpr int kPanePadding = 16;
constexpr int kPaneSpacing = 8;
constexpr int kButtonHeight = 36;

views::Label* AddPaneLabel(views::View* parent,
                           const std::u16string& text,
                           views::style::TextStyle text_style,
                           bool multiline = false) {
  auto label = std::make_unique<views::Label>(text);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetTextStyle(text_style);
  label->SetMultiLine(multiline);
  label->SetElideBehavior(multiline ? gfx::NO_ELIDE : gfx::ELIDE_TAIL);
  return parent->AddChildView(std::move(label));
}

}  // namespace

BEGIN_METADATA(MahoSidebarLibraryActionPaneView)
END_METADATA

MahoSidebarLibraryActionPaneView::MahoSidebarLibraryActionPaneView(
    std::u16string title,
    std::u16string description,
    std::vector<ActionSpec> actions,
    ActionCallback action_callback)
    : action_callback_(std::move(action_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kPanePadding, kPanePadding, kPanePadding, kPanePadding),
      kPaneSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  SetAccessibleName(title);

  title_label_ =
      AddPaneLabel(this, title, views::style::STYLE_BODY_3_MEDIUM);
  description_label_ =
      AddPaneLabel(this, description, views::style::STYLE_BODY_5, true);

  auto* actions_container = AddChildView(std::make_unique<views::View>());
  auto* actions_layout = actions_container->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 8));
  actions_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  for (size_t i = 0; i < actions.size(); ++i) {
    const ActionSpec& action = actions[i];
    auto* button = actions_container->AddChildView(
        std::make_unique<views::MdTextButton>(
            base::BindRepeating(
                &MahoSidebarLibraryActionPaneView::HandleActionPressed,
                base::Unretained(this), i),
            action.label));
    button->SetStyle(ui::ButtonStyle::kText);
    button->SetCornerRadius(18);
    button->SetMinSize(gfx::Size(0, kButtonHeight));
    button->SetMaxSize(gfx::Size(0, kButtonHeight));
    button->SetRequestFocusOnPress(false);
    button->SetAccessibleName(action.accessible_name.empty()
                                  ? action.label
                                  : action.accessible_name);
    button->SetEnabled(action.enabled);
    action_buttons_.push_back(button);
  }

  UpdateAppearance();
}

MahoSidebarLibraryActionPaneView::~MahoSidebarLibraryActionPaneView() = default;

void MahoSidebarLibraryActionPaneView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateAppearance();
}

void MahoSidebarLibraryActionPaneView::SetSidebarPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  UpdateAppearance();
}

void MahoSidebarLibraryActionPaneView::SetActionEnabled(size_t index,
                                                        bool enabled) {
  if (index >= action_buttons_.size() || !action_buttons_[index]) {
    return;
  }
  action_buttons_[index]->SetEnabled(enabled);
  UpdateAppearance();
}

views::Button* MahoSidebarLibraryActionPaneView::button_for_action_for_testing(
    size_t index) {
  if (index >= action_buttons_.size()) {
    return nullptr;
  }
  return action_buttons_[index];
}

void MahoSidebarLibraryActionPaneView::HandleActionPressed(size_t index) {
  if (!action_callback_ || index >= action_buttons_.size() ||
      !action_buttons_[index] || !action_buttons_[index]->GetEnabled()) {
    return;
  }
  action_callback_.Run(index);
}

void MahoSidebarLibraryActionPaneView::UpdateAppearance() {
  const SkColor surface = palette_.content_surface_stops.empty()
                              ? palette_.row_active
                              : palette_.content_surface_stops.back();
  SetBackground(views::CreateRoundedRectBackground(surface, kPaneCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kPaneCornerRadius,
                                            palette_.outline));
  if (title_label_) {
    title_label_->SetEnabledColor(palette_.primary_text);
  }
  if (description_label_) {
    description_label_->SetEnabledColor(palette_.secondary_text);
  }
  for (auto& button : action_buttons_) {
    if (button) {
      button->SetRequestFocusOnPress(false);
      // Every entry is an MdTextButton (see the constructor).
      auto* text_button = static_cast<views::MdTextButton*>(button.get());
      text_button->SetEnabledTextColors(palette_.primary_text);
      text_button->SetTextColor(views::Button::STATE_DISABLED,
                                palette_.disabled_text);
    }
  }
}

}  // namespace maho
