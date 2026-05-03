#include "maho_sidebar_action_popover_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kBubbleCornerRadius = 18;
constexpr int kBubbleRowSpacing = 6;
constexpr int kBubbleRowCornerRadius = 12;
constexpr int kBubbleRowMinWidth = 270;
constexpr int kBubbleRowMinHeight = 54;

const gfx::Insets kBubbleContentsInsets = gfx::Insets::TLBR(10, 10, 10, 10);

std::u16string ComposeRowText(const std::u16string& title,
                              const std::u16string& subtitle) {
  return title + u"\n" + subtitle;
}

}  // namespace

BEGIN_METADATA(MahoSidebarActionPopoverView)
END_METADATA

MahoSidebarActionPopoverView::MahoSidebarActionPopoverView(
    ActionCallback action_callback)
    : action_callback_(std::move(action_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kBubbleContentsInsets,
      kBubbleRowSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface2,
                                                   kBubbleCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kBubbleCornerRadius,
                                           ui::kColorSysNeutralOutline));

  AddActionButton(MahoSidebarActionPopoverAction::kNewSpace, u"New Space",
                  u"Create a fresh workspace");
  AddActionButton(MahoSidebarActionPopoverAction::kNewFolder, u"New Folder",
                  u"Organize tabs in the current space");
  AddActionButton(MahoSidebarActionPopoverAction::kNewTab, u"New Tab",
                  u"Open a page in the current space");
}

MahoSidebarActionPopoverView::~MahoSidebarActionPopoverView() = default;

// static
views::Widget* MahoSidebarActionPopoverView::Show(
    views::View* anchor_view,
    ActionCallback action_callback) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::BOTTOM_RIGHT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->SetContentsView(std::make_unique<MahoSidebarActionPopoverView>(
      std::move(action_callback)));

  views::Widget* widget = views::BubbleDialogDelegate::CreateBubble(
      std::move(delegate));
  if (widget) {
    widget->Show();
  }
  return widget;
}

void MahoSidebarActionPopoverView::AddActionButton(
    MahoSidebarActionPopoverAction action,
    const std::u16string& title,
    const std::u16string& subtitle) {
  auto button = std::make_unique<views::LabelButton>(
      base::BindRepeating(&MahoSidebarActionPopoverView::OnActionPressed,
                          base::Unretained(this), action),
      ComposeRowText(title, subtitle));
  button->SetRequestFocusOnPress(false);
  button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  button->SetMinSize(gfx::Size(kBubbleRowMinWidth, kBubbleRowMinHeight));
  button->SetLabelStyle(views::style::STYLE_BODY_4_MEDIUM);
  button->SetTextColor(views::Button::STATE_NORMAL, ui::kColorSysOnSurface);
  button->SetTextColor(views::Button::STATE_HOVERED, ui::kColorSysOnSurface);
  button->SetTextColor(views::Button::STATE_PRESSED, ui::kColorSysOnSurface);
  button->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface3, kBubbleRowCornerRadius));
  button->SetBorder(views::CreateRoundedRectBorder(
      1, kBubbleRowCornerRadius, ui::kColorSysNeutralOutline));
  button->SetImageLabelSpacing(0);
  button->SetTooltipText(title + u" — " + subtitle);
  button->SetAccessibleName(title + u". " + subtitle);
  AddChildView(std::move(button));
}

void MahoSidebarActionPopoverView::OnActionPressed(
    MahoSidebarActionPopoverAction action,
    const ui::Event& event) {
  (void)event;
  if (action_callback_) {
    action_callback_.Run(action);
  }
}

}  // namespace maho
