// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/security_state/core/security_state.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kPanelCornerRadius = 18;
constexpr int kPanelWidth = 360;
constexpr int kPanelSectionSpacing = 10;
constexpr int kPanelOuterPadding = 14;
constexpr int kPanelInnerSpacing = 8;
constexpr int kSectionHeaderTopSpacing = 4;
constexpr int kSectionHeaderBottomSpacing = 6;
constexpr int kQuickActionSpacing = 8;
constexpr int kQuickActionTileMinHeight = 96;
constexpr int kQuickActionTilePadding = 10;
constexpr int kQuickActionIconSize = 42;
constexpr int kQuickActionIconCornerRadius = 14;
constexpr int kGridCardHeight = 74;
constexpr int kGridCardPadding = 10;
constexpr int kGridCardIconSize = 34;
constexpr int kGridCardIconCornerRadius = 12;
constexpr int kListRowHeight = 56;
constexpr int kListRowLabelSpacing = 2;
constexpr int kListRowArrowWidth = 16;
constexpr int kFooterCornerRadius = 14;
constexpr int kFooterInnerPadding = 10;
constexpr int kFooterSpacing = 10;
constexpr int kFooterPillPadding = 10;
constexpr int kFooterPillIconSize = 28;
constexpr int kFooterPillCornerRadius = 12;

std::u16string ActionIconGlyph(const MahoLocationBarUtilityPanelAction& action) {
  const std::string title = base::UTF16ToUTF8(action.title);
  const std::string lower = base::ToLowerASCII(title);

  if (lower.find("extension") != std::string::npos) {
    return u"🧩";
  }
  if (lower.find("developer") != std::string::npos) {
    return u"⚙";
  }
  if (lower.find("picture-in-picture") != std::string::npos ||
      lower.find("pip") != std::string::npos) {
    return u"▣";
  }
  if (lower.find("security") != std::string::npos) {
    return u"🛡";
  }
  if (!action.title.empty()) {
    return action.title.substr(0, 1);
  }
  return u"•";
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

std::u16string BuildAccessibleName(const MahoLocationBarUtilityPanelAction& action) {
  if (!action.accessible_name.empty()) {
    return action.accessible_name;
  }
  if (action.subtitle.empty()) {
    return action.title;
  }
  return action.title + u" — " + action.subtitle;
}

std::u16string SecurityIconGlyph(int security_level) {
  switch (static_cast<security_state::SecurityLevel>(security_level)) {
    case security_state::SECURE:
      return u"🛡";
    case security_state::WARNING:
      return u"⚠";
    case security_state::DANGEROUS:
      return u"⛔";
    case security_state::NONE:
    case security_state::SECURITY_LEVEL_COUNT:
      break;
  }
  return u"?";
}

class MahoLocationBarUtilityPanelActionView : public views::View {
  METADATA_HEADER(MahoLocationBarUtilityPanelActionView, views::View)

 public:
  enum class DisplayMode {
    kQuickTile,
    kGridCard,
    kListRow,
  };

  explicit MahoLocationBarUtilityPanelActionView(
      MahoLocationBarUtilityPanelAction action,
      DisplayMode display_mode)
      : action_(std::move(action)), display_mode_(display_mode) {
    SetFocusBehavior(action_.enabled ? FocusBehavior::ALWAYS
                                     : FocusBehavior::NEVER);
    SetLayoutManager(std::make_unique<views::FillLayout>());

    const bool is_quick_tile = display_mode_ == DisplayMode::kQuickTile;
    const int preferred_width = is_quick_tile ? 0
        : (display_mode_ == DisplayMode::kGridCard
               ? (kPanelWidth - (kPanelOuterPadding * 2) - kQuickActionSpacing) / 2
               : kPanelWidth - (kPanelOuterPadding * 2));
    const int preferred_height = is_quick_tile
                                     ? kQuickActionTileMinHeight
                                     : (display_mode_ == DisplayMode::kGridCard
                                            ? kGridCardHeight
                                            : kListRowHeight);
    SetPreferredSize(gfx::Size(preferred_width, preferred_height));

    selection_background_ = AddChildView(std::make_unique<views::View>());
    selection_background_->SetVisible(false);
    selection_background_->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurfaceVariant,
        is_quick_tile ? kQuickActionIconCornerRadius : kGridCardIconCornerRadius));

    auto* content = AddChildView(std::make_unique<views::View>());
    auto* layout = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
        is_quick_tile ? views::BoxLayout::Orientation::kVertical
                      : views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets(is_quick_tile ? kQuickActionTilePadding : kGridCardPadding),
        is_quick_tile ? 6 : kPanelInnerSpacing));
    layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    if (is_quick_tile) {
      layout->set_main_axis_alignment(
          views::BoxLayout::MainAxisAlignment::kCenter);
    }

    auto* icon_view = content->AddChildView(std::make_unique<views::View>());
    icon_view->SetPreferredSize(gfx::Size(
        is_quick_tile ? kQuickActionIconSize : kGridCardIconSize,
        is_quick_tile ? kQuickActionIconSize : kGridCardIconSize));
    icon_view->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurface3,
        is_quick_tile ? kQuickActionIconCornerRadius : kGridCardIconCornerRadius));
    auto* icon_layout = icon_view->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
    icon_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    icon_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* icon_label = icon_view->AddChildView(
        std::make_unique<views::Label>(ActionIconGlyph(action_),
                                       views::style::CONTEXT_LABEL,
                                       views::style::STYLE_BODY_4_BOLD));
    icon_label->SetEnabledColor(ui::kColorSysOnSurface);
    icon_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);

    auto* text_column = content->AddChildView(std::make_unique<views::View>());
    auto* text_layout = text_column->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                           gfx::Insets(), kListRowLabelSpacing));
    text_layout->set_cross_axis_alignment(
        is_quick_tile ? views::BoxLayout::CrossAxisAlignment::kCenter
                      : views::BoxLayout::CrossAxisAlignment::kStart);
    text_layout->set_main_axis_alignment(
        is_quick_tile ? views::BoxLayout::MainAxisAlignment::kCenter
                      : views::BoxLayout::MainAxisAlignment::kStart);
    if (!is_quick_tile) {
      layout->SetFlexForView(text_column, 1);
    }

    auto* title_label = text_column->AddChildView(std::make_unique<views::Label>(
        action_.title, views::style::CONTEXT_LABEL,
        is_quick_tile ? views::style::STYLE_BODY_5_MEDIUM
                      : views::style::STYLE_BODY_4_MEDIUM));
    title_label->SetAutoColorReadabilityEnabled(false);
    title_label->SetEnabledColor(ui::kColorSysOnSurface);
    title_label->SetHorizontalAlignment(is_quick_tile ? gfx::ALIGN_CENTER
                                                      : gfx::ALIGN_LEFT);

    if (!action_.subtitle.empty()) {
      auto* subtitle_label = text_column->AddChildView(std::make_unique<views::Label>(
          action_.subtitle, views::style::CONTEXT_LABEL,
          is_quick_tile ? views::style::STYLE_BODY_5
                        : views::style::STYLE_BODY_5));
      subtitle_label->SetAutoColorReadabilityEnabled(false);
      subtitle_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      subtitle_label->SetHorizontalAlignment(is_quick_tile ? gfx::ALIGN_CENTER
                                                           : gfx::ALIGN_LEFT);
      subtitle_label->SetMultiLine(true);
      subtitle_label->SetAllowCharacterBreak(true);
    }

    if (display_mode_ == DisplayMode::kListRow && action_.enabled) {
      auto* arrow_label = content->AddChildView(std::make_unique<views::Label>(u"›"));
      arrow_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      arrow_label->SetTextStyle(views::style::STYLE_BODY_2_MEDIUM);
      arrow_label->SetPreferredSize(gfx::Size(kListRowArrowWidth, 0));
      arrow_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    GetViewAccessibility().SetName(BuildAccessibleName(action_));
    SetTooltipText(ActionTooltip(action_));
    SetEnabled(action_.enabled);
  }

  void SetHighlighted(bool highlighted) {
    selection_background_->SetVisible(highlighted);
  }

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

class MahoLocationBarUtilityPanelSecurityPillView : public views::View {
  METADATA_HEADER(MahoLocationBarUtilityPanelSecurityPillView, views::View)

 public:
  explicit MahoLocationBarUtilityPanelSecurityPillView(
      const MahoLocationBarUtilityPanelFooter& footer) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(8, kFooterPillPadding), 8));
    SetPreferredSize(gfx::Size(0, 36));
    SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurface3, kFooterPillCornerRadius));

    auto* icon = AddChildView(std::make_unique<views::View>());
    icon->SetPreferredSize(gfx::Size(kFooterPillIconSize, kFooterPillIconSize));
    icon->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysSurfaceVariant, kFooterPillCornerRadius));
    auto* icon_layout = icon->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
    icon_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    icon_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* icon_label = icon->AddChildView(std::make_unique<views::Label>(
        SecurityIconGlyph(footer.security_level), views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_5_MEDIUM));
    icon_label->SetEnabledColor(ui::kColorSysOnSurface);
    icon_label->SetHorizontalAlignment(gfx::ALIGN_CENTER);

    auto* text_column = AddChildView(std::make_unique<views::View>());
    auto* text_layout = text_column->SetLayoutManager(
        std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                           gfx::Insets(), 0));
    text_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    text_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);

    auto* title = text_column->AddChildView(std::make_unique<views::Label>(
        footer.title, views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_5_MEDIUM));
    title->SetAutoColorReadabilityEnabled(false);
    title->SetEnabledColor(ui::kColorSysOnSurface);
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    if (!footer.subtitle.empty()) {
      auto* subtitle = text_column->AddChildView(std::make_unique<views::Label>(
          footer.subtitle, views::style::CONTEXT_LABEL,
          views::style::STYLE_BODY_5));
      subtitle->SetAutoColorReadabilityEnabled(false);
      subtitle->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    }
  }
};

BEGIN_METADATA(MahoLocationBarUtilityPanelSecurityPillView)
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

  contents_view_ = SetContentsView(std::make_unique<views::View>());
  contents_view_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  contents_view_->GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  contents_view_->GetViewAccessibility().SetName(u"Location bar utility panel");
  contents_view_->SetPreferredSize(gfx::Size(kPanelWidth, 0));
  contents_view_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kPanelOuterPadding), kPanelSectionSpacing));

  contents_view_->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface2, kPanelCornerRadius));
  contents_view_->SetBorder(views::CreateRoundedRectBorder(
      1, kPanelCornerRadius, ui::kColorSysNeutralOutline));

  BuildContents();
}

MahoLocationBarUtilityPanelView::~MahoLocationBarUtilityPanelView() = default;

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
  AddQuickActionsSection();

  if (!model_.extensions.empty()) {
    contents_view_->AddChildView(std::make_unique<views::Separator>());
    AddExtensionsSection();
  }

  if (!model_.settings.empty()) {
    contents_view_->AddChildView(std::make_unique<views::Separator>());
    AddListSection(u"Settings", model_.settings);
  }

  if (model_.footer.title.empty() && model_.footer.subtitle.empty() &&
      model_.footer.primary_action_label.empty()) {
    return;
  }

  contents_view_->AddChildView(std::make_unique<views::Separator>());
  AddFooterSection();
}

void MahoLocationBarUtilityPanelView::AddQuickActionsSection() {
  if (model_.quick_actions.empty()) {
    return;
  }

  auto* row = contents_view_->AddChildView(std::make_unique<views::View>());
  auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      kQuickActionSpacing));
  row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  for (const auto& action : model_.quick_actions) {
    auto* tile = row->AddChildView(std::make_unique<MahoLocationBarUtilityPanelActionView>(
        action, MahoLocationBarUtilityPanelActionView::DisplayMode::kQuickTile));
    row_layout->SetFlexForView(tile, 1);
  }
}

void MahoLocationBarUtilityPanelView::AddExtensionsSection() {
  auto* header_label = contents_view_->AddChildView(std::make_unique<views::Label>(
      u"Extensions", views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5_MEDIUM));
  header_label->SetAutoColorReadabilityEnabled(false);
  header_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  header_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  header_label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kSectionHeaderTopSpacing, 0, kSectionHeaderBottomSpacing,
                        0)));

  auto* grid = contents_view_->AddChildView(std::make_unique<views::View>());
  auto* grid_layout = grid->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kQuickActionSpacing));
  grid_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  for (size_t i = 0; i < model_.extensions.size(); i += 2) {
    auto* row = grid->AddChildView(std::make_unique<views::View>());
    auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
        kQuickActionSpacing));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* first = row->AddChildView(std::make_unique<MahoLocationBarUtilityPanelActionView>(
        model_.extensions[i],
        MahoLocationBarUtilityPanelActionView::DisplayMode::kGridCard));
    row_layout->SetFlexForView(first, 1);

    if (i + 1 < model_.extensions.size()) {
      auto* second = row->AddChildView(
          std::make_unique<MahoLocationBarUtilityPanelActionView>(
              model_.extensions[i + 1],
              MahoLocationBarUtilityPanelActionView::DisplayMode::kGridCard));
      row_layout->SetFlexForView(second, 1);
    } else {
      auto* spacer = row->AddChildView(std::make_unique<views::View>());
      spacer->SetPreferredSize(gfx::Size(0, kGridCardHeight));
      row_layout->SetFlexForView(spacer, 1);
    }
  }
}

void MahoLocationBarUtilityPanelView::AddListSection(
    const std::u16string& header,
    const std::vector<MahoLocationBarUtilityPanelAction>& items) {
  auto* header_label = contents_view_->AddChildView(std::make_unique<views::Label>(
      header, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5_MEDIUM));
  header_label->SetAutoColorReadabilityEnabled(false);
  header_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  header_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  header_label->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kSectionHeaderTopSpacing, 0, kSectionHeaderBottomSpacing,
                        0)));

  for (const auto& item : items) {
    contents_view_->AddChildView(
        std::make_unique<MahoLocationBarUtilityPanelActionView>(
            item, MahoLocationBarUtilityPanelActionView::DisplayMode::kListRow));
  }
}

void MahoLocationBarUtilityPanelView::AddFooterSection() {
  auto* footer = contents_view_->AddChildView(std::make_unique<views::View>());
  footer->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface3, kFooterCornerRadius));
  footer->SetBorder(views::CreateRoundedRectBorder(
      1, kFooterCornerRadius, ui::kColorSysNeutralOutline));

  auto* footer_layout = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(kFooterInnerPadding, kFooterInnerPadding),
      kFooterSpacing));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* security_pill = footer->AddChildView(
      std::make_unique<MahoLocationBarUtilityPanelSecurityPillView>(model_.footer));
  footer_layout->SetFlexForView(security_pill, 1);

  std::u16string action_label = model_.footer.primary_action_label;
  if (action_label.empty()) {
    action_label = u"Close";
  }

  auto* action_button = footer->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoLocationBarUtilityPanelView::RunActionPressed,
                          base::Unretained(this), model_.footer.primary_action,
                          model_.footer.close_after_primary_action),
      action_label));
  action_button->SetStyle(ui::ButtonStyle::kTonal);
  action_button->SetCornerRadius(kFooterCornerRadius);
  action_button->SetMinSize(gfx::Size(0, 32));
  action_button->SetEnabled(model_.footer.primary_action_enabled);
}

void MahoLocationBarUtilityPanelView::RunActionPressed(
    const base::RepeatingClosure& callback,
    bool close_panel,
    const ui::Event& event) {
  (void)event;
  if (!callback.is_null()) {
    callback.Run();
  }

  if (close_panel) {
    if (views::Widget* widget = GetWidget()) {
      widget->CloseWithReason(views::Widget::ClosedReason::kCloseButtonClicked);
    }
  }
}

}  // namespace maho
