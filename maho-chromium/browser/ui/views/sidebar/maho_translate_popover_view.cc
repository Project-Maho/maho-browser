// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_translate_popover_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/task/sequenced_task_runner.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {
constexpr int kPopoverCornerRadiusDp = 14;
constexpr int kPopoverPaddingDp = 14;
constexpr int kPopoverRowSpacingDp = 12;
constexpr int kBadgeSizeDp = 34;

// Owns the popover Widget (CLIENT_OWNS_WIDGET) and destroys both once the
// native widget is gone. With the default NATIVE_WIDGET_OWNS_WIDGET a plain
// BubbleDialogDelegate is not owned by the Widget and leaks, and its
// BubbleUmaLogger then dereferences the already-freed contents view from the
// pending presentation-time callback. Destroying the delegate invalidates the
// logger's WeakPtr in the same task that frees the contents view.
class PopoverBubbleDelegate : public views::BubbleDialogDelegate {
 public:
  PopoverBubbleDelegate(views::View* anchor_view,
                        views::BubbleBorder::Arrow arrow)
      : views::BubbleDialogDelegate(anchor_view, arrow) {}

  void set_widget(std::unique_ptr<views::Widget> widget) {
    owned_widget_ = std::move(widget);
  }

  // views::WidgetDelegate:
  void WidgetIsZombie(views::Widget* widget) override {
    // Defer so DeleteDelegate() finishes first and clears this delegate's
    // Widget pointer. Destroying the delegate then destroys `owned_widget_`
    // (a derived member) before the BubbleDialogDelegate base destructor.
    // DeleteSoon (not a PostTask owning a unique_ptr) so a task queue
    // discarded at shutdown leaks the delegate instead of tearing the views
    // down after the views environment is gone.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                               this);
  }

 private:
  // Declared last-in-class so it is destroyed first.
  std::unique_ptr<views::Widget> owned_widget_;
};
// The popover is wider than its anchor. A TOP_LEFT arrow grows it toward the
// trailing edge, which clipped it at the window edge for the header's
// trailing translate label (F3 D2). The bubble's own mirror-if-offscreen pass
// did not catch that case, so the arrow is chosen from where the anchor sits:
// an anchor in the trailing half of its window grows toward the leading edge.
// Arrows are logical (BubbleBorder mirrors them in RTL), so the side is
// measured in logical terms too.
views::BubbleBorder::Arrow ArrowForAnchor(const views::View* anchor_view) {
  const views::Widget* widget = anchor_view->GetWidget();
  if (!widget) {
    return views::BubbleBorder::TOP_LEFT;
  }
  const gfx::Rect window = widget->GetWindowBoundsInScreen();
  const int anchor_center_x = anchor_view->GetBoundsInScreen().CenterPoint().x();
  const int window_center_x = window.CenterPoint().x();
  const bool on_trailing_half = base::i18n::IsRTL()
                                    ? anchor_center_x < window_center_x
                                    : anchor_center_x > window_center_x;
  return on_trailing_half ? views::BubbleBorder::TOP_RIGHT
                          : views::BubbleBorder::TOP_LEFT;
}
}  // namespace

// static
views::BubbleBorder::Arrow MahoTranslatePopoverView::ArrowForAnchorForTesting(
    const views::View* anchor_view) {
  return ArrowForAnchor(anchor_view);
}

MahoTranslatePopoverView::MahoTranslatePopoverView(
    const std::u16string& target_language_name,
    base::RepeatingClosure translate_callback,
    OptionsCallback options_callback)
    : translate_callback_(std::move(translate_callback)),
      options_callback_(std::move(options_callback)) {
  SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysBaseContainerElevated, kPopoverCornerRadiusDp));

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kPopoverPaddingDp), kPopoverRowSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* prompt_row = AddChildView(std::make_unique<views::View>());
  auto* prompt_layout = prompt_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kPopoverRowSpacingDp));
  prompt_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto badge = std::make_unique<views::Label>(u"Aa");
  badge->SetAutoColorReadabilityEnabled(false);
  badge->SetEnabledColor(ui::kColorSysOnSurface);
  badge->SetPreferredSize(gfx::Size(kBadgeSizeDp, kBadgeSizeDp));
  badge->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysStateHoverOnSubtle, kBadgeSizeDp / 2));
  prompt_row->AddChildView(std::move(badge));

  auto* prompt = prompt_row->AddChildView(std::make_unique<views::Label>(
      std::u16string(u"Translate this page to ") + target_language_name +
      u"?"));
  prompt->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  prompt->SetMultiLine(true);
  prompt->SetAutoColorReadabilityEnabled(false);
  prompt->SetEnabledColor(ui::kColorSysOnSurface);
  prompt->SetTextStyle(views::style::STYLE_BODY_3_MEDIUM);
  prompt_layout->SetFlexForView(prompt, 1);

  auto* button_row = AddChildView(std::make_unique<views::View>());
  auto* button_layout = button_row->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
          kPopoverRowSpacingDp));
  button_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kEnd);

  auto options = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoTranslatePopoverView::OnOptionsPressed,
                          weak_factory_.GetWeakPtr()),
      u"Options");
  options->SetStyle(ui::ButtonStyle::kTonal);
  options_button_ = button_row->AddChildView(std::move(options));

  auto translate = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoTranslatePopoverView::OnTranslatePressed,
                          weak_factory_.GetWeakPtr()),
      u"Translate");
  translate->SetStyle(ui::ButtonStyle::kProminent);
  button_row->AddChildView(std::move(translate));
}

MahoTranslatePopoverView::~MahoTranslatePopoverView() = default;

// static
views::Widget* MahoTranslatePopoverView::Show(
    views::View* anchor_view,
    const std::u16string& target_language_name,
    base::RepeatingClosure translate_callback,
    OptionsCallback options_callback) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<PopoverBubbleDelegate>(
      anchor_view, ArrowForAnchor(anchor_view));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->SetContentsView(std::make_unique<MahoTranslatePopoverView>(
      target_language_name, std::move(translate_callback),
      std::move(options_callback)));

  // Released to self-ownership: PopoverBubbleDelegate deletes itself (and the
  // Widget it owns) from WidgetIsZombie().
  PopoverBubbleDelegate* const owner = delegate.get();
  // CreateBubble always returns the new widget.
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(delegate), views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  owner->set_widget(base::WrapUnique(widget));
  widget->Show();
  return widget;
}

void MahoTranslatePopoverView::OnTranslatePressed() {
  if (translate_callback_) {
    translate_callback_.Run();
  }
  if (GetWidget()) {
    GetWidget()->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
  }
}

void MahoTranslatePopoverView::OnOptionsPressed() {
  if (options_callback_) {
    options_callback_.Run(
        options_button_,
        base::BindRepeating(&MahoTranslatePopoverView::ClosePopover,
                            weak_factory_.GetWeakPtr()));
  }
}

void MahoTranslatePopoverView::ClosePopover() {
  if (GetWidget()) {
    GetWidget()->CloseWithReason(
        views::Widget::ClosedReason::kAcceptButtonClicked);
  }
}

BEGIN_METADATA(MahoTranslatePopoverView)
END_METADATA

}  // namespace maho
