// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/views/maho_mac_sf_symbol_helper.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kPanelCornerRadius = 18;
constexpr int kPanelWidth = 304;
constexpr int kPanelSectionSpacing = 4;
constexpr int kPanelOuterPadding = 12;
constexpr int kFooterTopPadding = 2;
constexpr int kFooterHeight = 18;
constexpr int kFooterHorizontalPadding = 0;
constexpr int kFooterIconSize = 10;
constexpr int kFooterLabelSpacing = 3;
constexpr int kZoneHeaderTopPadding = 8;
constexpr int kZoneHeaderBottomPadding = 4;
constexpr int kToolbarRowHeight = 36;
constexpr int kToolbarIconSize = 13;
constexpr int kToolbarIconSpacing = 8;
constexpr int kToolbarButtonCornerRadius = 10;
constexpr int kChipRowSpacing = 8;
constexpr int kChipHeight = 30;
constexpr int kChipCornerRadius = 9;
constexpr int kChipHorizontalPadding = 10;
constexpr int kAddChipWidth = 30;
constexpr int kSettingsRowHeight = 38;
constexpr int kSettingsRowHorizontalPadding = 10;
constexpr int kSettingsRowCornerRadius = 10;
constexpr int kSettingsIconSize = 12;
constexpr int kFooterEllipsisSize = 18;

bool FooterIsEmpty(const MahoLocationBarUtilityPanelFooter& footer) {
  return footer.title.empty() && footer.subtitle.empty();
}

std::u16string ActionTooltip(const MahoLocationBarUtilityPanelAction& action) {
  if (!action.accessible_name.empty()) {
    return action.accessible_name;
  }
  if (action.subtitle.empty()) {
    return action.title;
  }
  return action.title + u" · " + action.subtitle;
}

std::u16string BuildAccessibleName(
    const MahoLocationBarUtilityPanelAction& action) {
  if (!action.accessible_name.empty()) {
    return action.accessible_name;
  }
  if (action.subtitle.empty()) {
    return action.title;
  }
  return action.title + u" — " + action.subtitle;
}

bool IsDisabledAction(const MahoLocationBarUtilityPanelAction& action) {
  return !action.enabled;
}

class MahoLocationBarUtilityPanelActionView : public views::View {
  METADATA_HEADER(MahoLocationBarUtilityPanelActionView, views::View)

 public:
  enum class DisplayMode {
    kToolbarButton,
    kExtensionChip,
    kSettingsRow,
  };

  explicit MahoLocationBarUtilityPanelActionView(
      MahoLocationBarUtilityPanelAction action,
      DisplayMode display_mode)
      : action_(std::move(action)), display_mode_(display_mode) {
    const bool is_toolbar = display_mode_ == DisplayMode::kToolbarButton;
    const bool is_chip = display_mode_ == DisplayMode::kExtensionChip;
    const bool is_settings = display_mode_ == DisplayMode::kSettingsRow;
    const bool show_settings_subtitle = is_settings && !action_.subtitle.empty();
    SetFocusBehavior(action_.enabled ? FocusBehavior::ALWAYS
                                     : FocusBehavior::NEVER);
    SetLayoutManager(std::make_unique<views::FillLayout>());
    if (is_toolbar) {
      SetPreferredSize(gfx::Size(kToolbarRowHeight, kToolbarRowHeight));
    } else if (is_settings) {
      SetPreferredSize(gfx::Size(0, kSettingsRowHeight));
    } else if (is_chip && action_.title == u"+") {
      SetPreferredSize(gfx::Size(kAddChipWidth, kChipHeight));
    }

    const int corner_radius = is_toolbar   ? kToolbarButtonCornerRadius
                              : is_chip     ? kChipCornerRadius
                              : is_settings ? kSettingsRowCornerRadius
                                            : 0;
    SetBackground(views::CreateRoundedRectBackground(
        is_settings ? ui::kColorSysSurface1 : ui::kColorSysSurface1,
        corner_radius));

    if (IsDisabledAction(action_)) {
      SetPaintToLayer();
      layer()->SetOpacity(0.55f);
    }

    selection_background_ = AddChildView(std::make_unique<views::View>());
    selection_background_->SetVisible(false);
    selection_background_->SetCanProcessEventsWithinSubtree(false);
    selection_background_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurface2,
        corner_radius));

    auto* content = AddChildView(std::make_unique<views::View>());
    content->SetCanProcessEventsWithinSubtree(false);
    auto* layout = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, is_toolbar   ? 0
                           : is_chip     ? kChipHorizontalPadding
                           : is_settings ? kSettingsRowHorizontalPadding
                                         : 0),
        is_toolbar   ? 0
        : is_chip     ? kToolbarIconSpacing
        : is_settings ? kToolbarIconSpacing
                      : 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    views::View* icon_parent = content;
    const int icon_size = is_toolbar   ? kToolbarIconSize
                         : is_chip     ? kSettingsIconSize
                         : is_settings ? kSettingsIconSize
                                       : 0;

    auto* icon_image = icon_parent->AddChildView(std::make_unique<views::ImageView>());
    icon_image->SetImageSize(gfx::Size(icon_size, icon_size));
    icon_image->SetCanProcessEventsWithinSubtree(false);
    icon_image->SetImage(CreateMahoMacSFSymbol(
        action_.sf_symbol_name, base::UTF16ToUTF8(BuildAccessibleName(action_)),
        icon_size, ui::kColorSysOnSurfaceSubtle));

    if (!is_toolbar) {
      auto* text_column = content->AddChildView(std::make_unique<views::View>());
      auto* text_layout = text_column->SetLayoutManager(
          std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                             gfx::Insets(),
                                             0));
      text_layout->set_cross_axis_alignment(
           views::BoxLayout::CrossAxisAlignment::kStart);
      text_layout->set_main_axis_alignment(
           views::BoxLayout::MainAxisAlignment::kCenter);
      layout->SetFlexForView(text_column, 1);

        auto* title_label = text_column->AddChildView(std::make_unique<views::Label>(
            action_.title, views::style::CONTEXT_LABEL,
            is_chip || is_settings ? views::style::STYLE_BODY_4
                                   : views::style::STYLE_BODY_4));
      title_label->SetAutoColorReadabilityEnabled(false);
      title_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      title_label->SetMultiLine(false);
      title_label->SetElideBehavior(gfx::ELIDE_TAIL);

        if (show_settings_subtitle) {
          auto* subtitle_label = text_column->AddChildView(
              std::make_unique<views::Label>(action_.subtitle,
                                             views::style::CONTEXT_LABEL,
                                           views::style::STYLE_BODY_5));
        subtitle_label->SetAutoColorReadabilityEnabled(false);
        subtitle_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
        subtitle_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        subtitle_label->SetMultiLine(false);
         subtitle_label->SetElideBehavior(gfx::ELIDE_TAIL);
       }
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    GetViewAccessibility().SetName(BuildAccessibleName(action_));
    SetTooltipText(ActionTooltip(action_));
    SetEnabled(action_.enabled);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& size_bounds) const override {
    if (display_mode_ != DisplayMode::kExtensionChip || action_.title == u"+") {
      return views::View::CalculatePreferredSize(size_bounds);
    }

    gfx::Size preferred_size = views::View::CalculatePreferredSize(size_bounds);
    preferred_size.set_height(kChipHeight);
    return preferred_size;
  }

  void SetHighlighted(bool highlighted) { selection_background_->SetVisible(highlighted); }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    if (!action_.enabled) {
      return;
    }
    SetHighlighted(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    if (!action_.enabled) {
      return;
    }
    SetHighlighted(false);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!action_.enabled || !event.IsOnlyLeftMouseButton() ||
        action_.callback.is_null()) {
      return false;
    }

    action_.callback.Run();
    if (action_.close_after_activate) {
      if (views::Widget* widget = GetWidget(); widget && !widget->IsClosed()) {
        widget->CloseWithReason(
            views::Widget::ClosedReason::kAcceptButtonClicked);
      }
    }
    return true;
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (!action_.enabled || action_.callback.is_null()) {
      return false;
    }

    switch (event.key_code()) {
      case ui::VKEY_RETURN:
      case ui::VKEY_SPACE:
        action_.callback.Run();
        if (action_.close_after_activate) {
          if (views::Widget* widget = GetWidget(); widget &&
                                                  !widget->IsClosed()) {
            widget->CloseWithReason(
                views::Widget::ClosedReason::kAcceptButtonClicked);
          }
        }
        return true;
      default:
        return false;
    }
  }

 private:
  MahoLocationBarUtilityPanelAction action_;
  DisplayMode display_mode_;
  raw_ptr<views::View> selection_background_ = nullptr;
};

BEGIN_METADATA(MahoLocationBarUtilityPanelActionView)
END_METADATA

class MahoLocationBarUtilityPanelSecuritySummaryView : public views::View {
  METADATA_HEADER(MahoLocationBarUtilityPanelSecuritySummaryView, views::View)

 public:
  explicit MahoLocationBarUtilityPanelSecuritySummaryView(
      const MahoLocationBarUtilityPanelFooter& footer) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kFooterHorizontalPadding), kFooterLabelSpacing));
    SetPreferredSize(gfx::Size(0, kFooterHeight));
    auto* icon_image = AddChildView(std::make_unique<views::ImageView>());
    icon_image->SetImageSize(gfx::Size(kFooterIconSize, kFooterIconSize));
    icon_image->SetCanProcessEventsWithinSubtree(false);
    icon_image->SetImage(CreateMahoMacSFSymbol(
        footer.sf_symbol_name, base::UTF16ToUTF8(footer.title), kFooterIconSize,
        ui::kColorSysOnSurfaceSubtle));

    auto* title = AddChildView(std::make_unique<views::Label>(
        footer.title, views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_5));
    title->SetAutoColorReadabilityEnabled(false);
    title->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    if (!footer.subtitle.empty()) {
      auto* separator = AddChildView(std::make_unique<views::Label>(u"·"));
      separator->SetAutoColorReadabilityEnabled(false);
      separator->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);

      auto* subtitle = AddChildView(std::make_unique<views::Label>(
          footer.subtitle, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
      subtitle->SetAutoColorReadabilityEnabled(false);
      subtitle->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
    GetViewAccessibility().SetName(footer.subtitle.empty()
                                       ? footer.title
                                       : footer.title + u" — " + footer.subtitle);
  }
};

BEGIN_METADATA(MahoLocationBarUtilityPanelSecuritySummaryView)
END_METADATA

}  // namespace

MahoLocationBarUtilityPanelView::MahoLocationBarUtilityPanelView(
    views::View* anchor_view,
    MahoLocationBarUtilityPanelModel model)
    : views::BubbleDialogDelegate(anchor_view, views::BubbleBorder::TOP_RIGHT),
      model_(std::move(model)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_close_on_deactivate(true);
  set_margins(gfx::Insets());
  set_fixed_width(kPanelWidth);

  contents_view_ = SetContentsView(std::make_unique<views::View>());
  contents_view_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  contents_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  contents_view_->GetViewAccessibility().SetName(u"Location bar utility panel");
  contents_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kPanelOuterPadding), kPanelSectionSpacing));

  contents_view_->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface2, kPanelCornerRadius));
  contents_view_->SetBorder(views::CreateRoundedRectBorder(
      1, kPanelCornerRadius, ui::kColorSysSurfaceVariant));

  BuildContents();
}

MahoLocationBarUtilityPanelView::~MahoLocationBarUtilityPanelView() = default;

views::View* MahoLocationBarUtilityPanelView::GetActionViewForTesting(
    const std::u16string& title) const {
  auto it = action_views_for_testing_.find(title);
  return it == action_views_for_testing_.end() ? nullptr : it->second.get();
}

views::Widget* MahoLocationBarUtilityPanelView::Show(
    views::View* anchor_view,
    MahoLocationBarUtilityPanelModel model) {
  if (!anchor_view) {
    return nullptr;
  }

  auto bubble = std::make_unique<MahoLocationBarUtilityPanelView>(
      anchor_view, std::move(model));
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubble(
      std::move(bubble));
  if (widget) {
    widget->Show();
    widget->widget_delegate()->GetContentsView()->RequestFocus();
  }
  return widget;
}

void MahoLocationBarUtilityPanelView::BuildContents() {
  for (const auto& zone : model_.zones) {
    AddZone(zone);
  }

  if (!FooterIsEmpty(model_.footer)) {
    AddFooterSummary();
  }
}

void MahoLocationBarUtilityPanelView::AddZone(const MahoUtilityPanelZone& zone) {
  AddZoneHeader(zone.header_label);
  switch (zone.style) {
    case MahoUtilityPanelZoneStyle::kToolbarRow:
      AddToolbarRowZone(zone);
      return;
    case MahoUtilityPanelZoneStyle::kChipRow:
      AddChipRowZone(zone);
      return;
    case MahoUtilityPanelZoneStyle::kSettingsList:
      AddSettingsListZone(zone);
      return;
  }
}

void MahoLocationBarUtilityPanelView::AddZoneHeader(
    const std::u16string& header_label) {
  if (header_label.empty()) {
    return;
  }

  auto* header = contents_view_->AddChildView(std::make_unique<views::Label>(
      header_label, views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_4_MEDIUM));
  header->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kZoneHeaderTopPadding, 2, kZoneHeaderBottomPadding, 2)));
  header->SetAutoColorReadabilityEnabled(false);
  header->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
}

void MahoLocationBarUtilityPanelView::AddToolbarRowZone(
    const MahoUtilityPanelZone& zone) {
  if (zone.items.empty()) {
    return;
  }

  auto* section = contents_view_->AddChildView(std::make_unique<views::View>());
  auto* layout = section->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kToolbarIconSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  for (const auto& action : zone.items) {
    auto* action_view = section->AddChildView(
        std::make_unique<MahoLocationBarUtilityPanelActionView>(
            action, MahoLocationBarUtilityPanelActionView::DisplayMode::kToolbarButton));
    RegisterActionViewForTesting(action, action_view);
    layout->SetFlexForView(action_view, 1);
  }
}

void MahoLocationBarUtilityPanelView::AddChipRowZone(
    const MahoUtilityPanelZone& zone) {
  if (zone.items.empty()) {
    return;
  }

  auto* section = contents_view_->AddChildView(std::make_unique<views::View>());
  auto* layout = section->SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kStart);
  layout->SetMainAxisAlignment(views::LayoutAlignment::kStart);
  layout->SetInteriorMargin(gfx::Insets());
  layout->SetDefault(views::kMarginsKey,
                     gfx::Insets::VH(0, kChipRowSpacing / 2));

  for (const auto& action : zone.items) {
    auto* action_view = section->AddChildView(
        std::make_unique<MahoLocationBarUtilityPanelActionView>(
            action, MahoLocationBarUtilityPanelActionView::DisplayMode::kExtensionChip));
    RegisterActionViewForTesting(action, action_view);
  }
}

void MahoLocationBarUtilityPanelView::AddSettingsListZone(
    const MahoUtilityPanelZone& zone) {
  if (zone.items.empty()) {
    return;
  }

  auto* section = contents_view_->AddChildView(std::make_unique<views::View>());
  auto* layout = section->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  for (const auto& action : zone.items) {
    auto* action_view = section->AddChildView(
        std::make_unique<MahoLocationBarUtilityPanelActionView>(
            action, MahoLocationBarUtilityPanelActionView::DisplayMode::kSettingsRow));
    RegisterActionViewForTesting(action, action_view);
  }
}

void MahoLocationBarUtilityPanelView::AddFooterSummary() {
  auto* footer = contents_view_->AddChildView(std::make_unique<views::View>());
  footer->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(kFooterTopPadding, 0, 0, 0)));
  auto* footer_layout = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* security_summary = footer->AddChildView(
      std::make_unique<MahoLocationBarUtilityPanelSecuritySummaryView>(
          model_.footer));
  footer_layout->SetFlexForView(security_summary, 1);

  auto* ellipsis = footer->AddChildView(std::make_unique<views::Label>(u"…"));
  ellipsis->SetAutoColorReadabilityEnabled(false);
  ellipsis->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  ellipsis->SetTextStyle(views::style::STYLE_BODY_3);
  ellipsis->SetPreferredSize(gfx::Size(kFooterEllipsisSize, kFooterHeight));
  ellipsis->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  ellipsis->SetCanProcessEventsWithinSubtree(false);
  ellipsis->SetEnabled(false);
}

void MahoLocationBarUtilityPanelView::RegisterActionViewForTesting(
    const MahoLocationBarUtilityPanelAction& action,
    views::View* view) {
  if (!action.title.empty() && view) {
    action_views_for_testing_[action.title] = view;
  }
}

}  // namespace maho
