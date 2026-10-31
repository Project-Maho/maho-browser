// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/class_property.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/compositor/layer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace maho {

MahoDomScreenshotActionRow::MahoDomScreenshotActionRow() = default;
MahoDomScreenshotActionRow::MahoDomScreenshotActionRow(
    std::u16string label,
    raw_ptr<const gfx::VectorIcon> icon,
    bool enabled,
    bool destructive,
    base::OnceClosure activate)
    : label(std::move(label)),
      icon(icon),
      enabled(enabled),
      destructive(destructive),
      activate(std::move(activate)) {}
MahoDomScreenshotActionRow::MahoDomScreenshotActionRow(MahoDomScreenshotActionRow&&) = default;
MahoDomScreenshotActionRow& MahoDomScreenshotActionRow::operator=(MahoDomScreenshotActionRow&&) = default;
MahoDomScreenshotActionRow::~MahoDomScreenshotActionRow() = default;

MahoDomScreenshotActionMenu::MahoDomScreenshotActionMenu() = default;
MahoDomScreenshotActionMenu::MahoDomScreenshotActionMenu(MahoDomScreenshotActionMenu&&) = default;
MahoDomScreenshotActionMenu& MahoDomScreenshotActionMenu::operator=(MahoDomScreenshotActionMenu&&) = default;
MahoDomScreenshotActionMenu::~MahoDomScreenshotActionMenu() = default;

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(views::BubbleDialogDelegate,
                                 kScreenshotDialogDelegateKey)

constexpr int kDialogWidth = 248;
constexpr int kDialogCornerRadius = 16;
constexpr int kDialogSpacing = 4;
constexpr int kSelectionGap = 8;
constexpr int kRowCornerRadius = 10;
constexpr int kHeaderRowHeight = 36;
constexpr int kMenuRowHeight = 34;
constexpr int kLeadingIconSize = 12;
constexpr int kChevronIconSize = 10;
constexpr int kSeparatorThickness = 1;
constexpr gfx::Insets kDialogInsets = gfx::Insets::TLBR(10, 10, 10, 10);
constexpr gfx::Insets kRowInsets = gfx::Insets::TLBR(0, 10, 0, 10);
constexpr float kDisabledRowOpacity = 0.55f;

using maho_lucide_icons::kShareIcon;
using maho_lucide_icons::kCopyIcon;
using maho_lucide_icons::kArrowDownToLineIcon;
using maho_lucide_icons::kRotateCwIcon;
using maho_lucide_icons::kPencilLineIcon;
using maho_lucide_icons::kMessageSquareIcon;
using maho_lucide_icons::kChevronRightIcon;

class MenuSeparator : public views::View {
  METADATA_HEADER(MenuSeparator, views::View)
 public:
  MenuSeparator() {
    SetCanProcessEventsWithinSubtree(false);
    SetPreferredSize(gfx::Size(0, kSeparatorThickness));
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateSolidBackground(kMahoColorCommandBarDivider));
  }
};
BEGIN_METADATA(MenuSeparator)
END_METADATA

class ActionMenuButton : public views::Button {
  METADATA_HEADER(ActionMenuButton, views::Button)
 public:
  ActionMenuButton(std::u16string label,
                   const gfx::VectorIcon& icon,
                   PressedCallback callback,
                   bool enabled,
                   bool is_header,
                   bool show_chevron)
      : views::Button(std::move(callback)),
        label_text_(std::move(label)),
        icon_(icon),
        is_header_(is_header),
        show_chevron_(show_chevron) {
    SetFocusBehavior(enabled ? FocusBehavior::ALWAYS : FocusBehavior::NEVER);
    SetEnabled(enabled);
    SetInstallFocusRingOnFocus(true);
    SetLayoutManager(std::make_unique<views::FillLayout>());
    SetPreferredSize(
        gfx::Size(0, is_header_ ? kHeaderRowHeight : kMenuRowHeight));

    background_container_ = AddChildView(std::make_unique<views::View>());
    background_container_->SetCanProcessEventsWithinSubtree(false);
    auto* layout = background_container_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, kRowInsets, 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_view_ = background_container_->AddChildView(
        std::make_unique<views::ImageView>());
    icon_view_->SetCanProcessEventsWithinSubtree(false);
    icon_view_->SetImageSize(gfx::Size(kLeadingIconSize, kLeadingIconSize));

    label_view_ = background_container_->AddChildView(
        std::make_unique<views::Label>(
            label_text_, views::style::CONTEXT_LABEL,
            is_header_ ? views::style::STYLE_BODY_4_MEDIUM
                       : views::style::STYLE_BODY_4));
    label_view_->SetAutoColorReadabilityEnabled(false);
    label_view_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_view_->SetMultiLine(false);
    label_view_->SetElideBehavior(gfx::ELIDE_TAIL);
    layout->SetFlexForView(label_view_, 1);

    if (show_chevron_) {
      chevron_view_ = background_container_->AddChildView(
          std::make_unique<views::ImageView>());
      chevron_view_->SetCanProcessEventsWithinSubtree(false);
      chevron_view_->SetImageSize(gfx::Size(kChevronIconSize, kChevronIconSize));
    }

    if (!enabled) {
      SetPaintToLayer();
      layer()->SetOpacity(kDisabledRowOpacity);
    }

    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
    GetViewAccessibility().SetName(label_text_);
    UpdateColors();
  }

  const std::u16string& label_text() const { return label_text_; }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateColors();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateColors();
  }

 private:
  void UpdateColors() {
    // Child views are created after SetEnabled() in the constructor, so guard
    // against the StateChanged()->UpdateColors() call that fires before they
    // exist.
    if (!background_container_) {
      return;
    }
    const bool highlighted = GetState() == ButtonState::STATE_HOVERED ||
                             GetState() == ButtonState::STATE_PRESSED ||
                             HasFocus();
    background_container_->SetBackground(
        views::CreateRoundedRectBackground(
            highlighted ? kMahoColorCommandBarInputFill
                        : kMahoColorCommandBarBackground,
            kRowCornerRadius));

    const ui::ColorId text_color =
        is_header_
            ? static_cast<ui::ColorId>(kMahoColorPrimaryText)
            : static_cast<ui::ColorId>(ui::kColorSysOnSurfaceSubtle);
    label_view_->SetEnabledColor(text_color);

    icon_view_->SetImage(
        ui::ImageModel::FromVectorIcon(*icon_, text_color, kLeadingIconSize));
    if (chevron_view_) {
      chevron_view_->SetImage(
          ui::ImageModel::FromVectorIcon(
              kChevronRightIcon, ui::kColorSysOnSurfaceSubtle,
              kChevronIconSize));
    }
  }

  const std::u16string label_text_;
  const raw_ref<const gfx::VectorIcon> icon_;
  const bool is_header_;
  const bool show_chevron_;
  raw_ptr<views::View> background_container_ = nullptr;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> label_view_ = nullptr;
  raw_ptr<views::ImageView> chevron_view_ = nullptr;
};
BEGIN_METADATA(ActionMenuButton)
END_METADATA

const gfx::VectorIcon& IconForLabel(const std::u16string& label) {
  if (label == u"Copy") {
    return kCopyIcon;
  }
  if (label == u"Save...") {
    return kArrowDownToLineIcon;
  }
  if (label == u"Retake") {
    return kRotateCwIcon;
  }
  if (label == u"Edit") {
    return kPencilLineIcon;
  }
  if (label == u"Send via iMessage...") {
    return kMessageSquareIcon;
  }
  if (label == u"Save to Library") {
    return kArrowDownToLineIcon;
  }
  return gfx::VectorIcon::EmptyIcon();
}

MahoDomScreenshotActionMenu BuildLegacyMenu(base::OnceClosure copy_callback,
                                            base::OnceClosure download_callback,
                                            base::OnceClosure dismiss_callback) {
  MahoDomScreenshotActionMenu menu;
  menu.header_label = u"Send To...";
  menu.header_icon = &kShareIcon;
  menu.rows.push_back(
      {u"Copy", &kCopyIcon, true, false, std::move(copy_callback)});
  menu.rows.push_back(
      {u"Save...", &kArrowDownToLineIcon, true, false, std::move(download_callback)});
  menu.rows.push_back({u"Retake", &kRotateCwIcon, false, false,
                       base::OnceClosure()});
  menu.rows.push_back(
      {u"Edit", &kPencilLineIcon, false, false, base::OnceClosure()});
  menu.rows.push_back({u"Send via iMessage...", &kMessageSquareIcon, false,
                       false, base::OnceClosure()});
  menu.rows.push_back({u"Save to Library", &kArrowDownToLineIcon, false,
                       false, base::OnceClosure()});
  menu.dismiss_callback = std::move(dismiss_callback);
  return menu;
}

}  // namespace

MahoDomScreenshotActionDialogView::MahoDomScreenshotActionDialogView(
    MahoDomScreenshotActionMenu menu)
    : menu_(std::move(menu)) {
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  AddAccelerator(ui::Accelerator(ui::VKEY_UP, ui::EF_NONE));
  AddAccelerator(ui::Accelerator(ui::VKEY_DOWN, ui::EF_NONE));
  SetFocusBehavior(FocusBehavior::ALWAYS);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kDialogInsets, kDialogSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(views::CreateRoundedRectBackground(kMahoColorCommandBarBackground,
                                                   kDialogCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kDialogCornerRadius,
                                           kMahoColorCommandBarPanelBorder));

  if (!menu_.header_label.empty()) {
    const bool header_enabled = !menu_.header_activate.is_null();
    auto* header_button = AddChildView(std::make_unique<ActionMenuButton>(
        menu_.header_label,
        menu_.header_icon ? *menu_.header_icon : gfx::VectorIcon::EmptyIcon(),
        base::BindRepeating(&MahoDomScreenshotActionDialogView::ActivateHeader,
                            base::Unretained(this)),
        header_enabled, true, true));
    header_button_ = header_button;
  }

  if (!menu_.rows.empty()) {
    AddChildView(std::make_unique<MenuSeparator>());
  }

  for (size_t i = 0; i < menu_.rows.size(); ++i) {
    const MahoDomScreenshotActionRow& row = menu_.rows[i];
    if (i == menu_.rows.size() - 1 && row.label == u"Save to Library") {
      AddChildView(std::make_unique<MenuSeparator>());
    }

    auto* row_button = AddChildView(std::make_unique<ActionMenuButton>(
        row.label,
        row.icon ? *row.icon : IconForLabel(row.label),
        base::BindRepeating(&MahoDomScreenshotActionDialogView::ActivateRow,
                            base::Unretained(this), i),
        row.enabled, false, false));
    row_buttons_.push_back(row_button);
  }

  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(u"Selected area screenshot result actions");
  SetPreferredSize(gfx::Size(kDialogWidth, GetPreferredSize().height()));
}

MahoDomScreenshotActionDialogView::~MahoDomScreenshotActionDialogView() = default;

// static
views::Widget* MahoDomScreenshotActionDialogView::Show(
    gfx::NativeView parent_window,
    const gfx::Rect& selection_rect_screen,
    MahoDomScreenshotActionMenu menu) {
  if (!parent_window) {
    return nullptr;
  }

  auto bubble =
      std::make_unique<MahoDomScreenshotActionDialogView>(std::move(menu));
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      nullptr, GetArrowForSelection(parent_window, selection_rect_screen));
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->SetShowCloseButton(false);
  delegate->set_close_on_deactivate(true);
  delegate->set_parent_window(parent_window);
  delegate->SetAnchorRect(
      GetAnchorRectForSelection(parent_window, selection_rect_screen));
  delegate->set_margins(gfx::Insets());
  delegate->set_fixed_width(kDialogWidth);
  delegate->SetContentsView(std::move(bubble));
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      delegate.get(), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (widget) {
    // The owned property releases the delegate only after Widget teardown.
    widget->SetProperty(kScreenshotDialogDelegateKey, std::move(delegate));
    widget->Show();
    widget->widget_delegate()->GetContentsView()->RequestFocus();
  }
  return widget;
}

// static
views::Widget* MahoDomScreenshotActionDialogView::Show(
    gfx::NativeView parent_window,
    const gfx::Rect& selection_rect_screen,
    base::OnceClosure copy_callback,
    base::OnceClosure download_callback,
    base::OnceClosure dismiss_callback) {
  return Show(parent_window, selection_rect_screen,
              BuildLegacyMenu(std::move(copy_callback),
                              std::move(download_callback),
                              std::move(dismiss_callback)));
}

void MahoDomScreenshotActionDialogView::RequestFocus() {
  if (header_button_ && header_button_->GetEnabled()) {
    header_button_->RequestFocus();
    return;
  }
  for (views::Button* button : row_buttons_) {
    if (button && button->GetEnabled()) {
      button->RequestFocus();
      return;
    }
  }
  views::View::RequestFocus();
}

bool MahoDomScreenshotActionDialogView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  switch (accelerator.key_code()) {
    case ui::VKEY_ESCAPE:
      if (views::Widget* widget = GetWidget(); widget && !widget->IsClosed()) {
        widget->Close();
      } else {
        RunDismissCallback();
      }
      return true;
    case ui::VKEY_UP:
      return MoveFocusBy(-1);
    case ui::VKEY_DOWN:
      return MoveFocusBy(1);
    default:
      return views::View::AcceleratorPressed(accelerator);
  }
}

void MahoDomScreenshotActionDialogView::AddedToWidget() {
  views::View::AddedToWidget();
  if (views::Widget* widget = GetWidget()) {
    widget_observation_.Observe(widget);
  }
}

void MahoDomScreenshotActionDialogView::RemovedFromWidget() {
  widget_observation_.Reset();
  views::View::RemovedFromWidget();
}

// static
gfx::Rect MahoDomScreenshotActionDialogView::GetAnchorRectForSelection(
    gfx::NativeView parent_window,
    const gfx::Rect& selection_rect_screen) {
  if (GetArrowForSelection(parent_window, selection_rect_screen) ==
      views::BubbleBorder::RIGHT_TOP) {
    return gfx::Rect(selection_rect_screen.x() - kSelectionGap,
                     selection_rect_screen.y(), 1, 1);
  }

  return gfx::Rect(selection_rect_screen.right() + kSelectionGap,
                   selection_rect_screen.y(), 1, 1);
}

// static
views::BubbleBorder::Arrow
MahoDomScreenshotActionDialogView::GetArrowForSelection(
    gfx::NativeView parent_window,
    const gfx::Rect& selection_rect_screen) {
  if (!parent_window || !display::Screen::Get()) {
    return views::BubbleBorder::LEFT_TOP;
  }

  const gfx::Rect anchor_rect =
      gfx::Rect(selection_rect_screen.right() + kSelectionGap,
                selection_rect_screen.y(), 1, 1);
  const display::Display display =
      display::Screen::Get()->GetDisplayNearestView(parent_window);
  const int right_space = display.work_area().right() - anchor_rect.x();
  if (right_space >= kDialogWidth) {
    return views::BubbleBorder::LEFT_TOP;
  }
  return views::BubbleBorder::RIGHT_TOP;
}

std::u16string MahoDomScreenshotActionDialogView::header_label_for_testing() const {
  return menu_.header_label;
}

std::u16string MahoDomScreenshotActionDialogView::row_label_for_testing(
    size_t index) const {
  return index < menu_.rows.size() ? menu_.rows[index].label : std::u16string();
}

bool MahoDomScreenshotActionDialogView::row_enabled_for_testing(
    size_t index) const {
  return index < menu_.rows.size() && menu_.rows[index].enabled;
}

views::Button* MahoDomScreenshotActionDialogView::row_button_for_testing(
    size_t index) const {
  return index < row_buttons_.size() ? row_buttons_[index].get() : nullptr;
}

void MahoDomScreenshotActionDialogView::PressHeaderForTesting() {
  ActivateHeader();
}

void MahoDomScreenshotActionDialogView::PressRowForTesting(size_t index) {
  ActivateRow(index);
}

void MahoDomScreenshotActionDialogView::OnWidgetDestroying(views::Widget* widget) {
  if (widget != GetWidget()) {
    return;
  }

  widget_observation_.Reset();
  RunDismissCallback();
}

void MahoDomScreenshotActionDialogView::ActivateHeader() {
  if (menu_.header_activate.is_null()) {
    return;
  }
  base::WeakPtr<MahoDomScreenshotActionDialogView> weak_this =
      weak_factory_.GetWeakPtr();
  base::OnceClosure activate = std::move(menu_.header_activate);
  std::move(activate).Run();
  if (!weak_this) {
    return;
  }
  DismissForAction();
}

void MahoDomScreenshotActionDialogView::ActivateRow(size_t index) {
  if (index >= menu_.rows.size()) {
    return;
  }

  MahoDomScreenshotActionRow& row = menu_.rows[index];
  if (!row.enabled || row.activate.is_null()) {
    return;
  }

  base::WeakPtr<MahoDomScreenshotActionDialogView> weak_this =
      weak_factory_.GetWeakPtr();
  base::OnceClosure activate = std::move(row.activate);
  std::move(activate).Run();
  if (!weak_this) {
    return;
  }
  DismissForAction();
}

void MahoDomScreenshotActionDialogView::DismissForAction() {
  if (views::Widget* widget = GetWidget(); widget && !widget->IsClosed()) {
    widget->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
    return;
  }
  RunDismissCallback();
}

bool MahoDomScreenshotActionDialogView::MoveFocusBy(int delta) {
  std::vector<views::Button*> buttons;
  if (header_button_ && header_button_->GetEnabled()) {
    buttons.push_back(header_button_);
  }
  for (views::Button* button : row_buttons_) {
    if (button && button->GetEnabled()) {
      buttons.push_back(button);
    }
  }
  if (buttons.empty()) {
    return false;
  }

  views::View* focused = GetFocusManager() ? GetFocusManager()->GetFocusedView()
                                           : nullptr;
  size_t current = 0;
  for (size_t i = 0; i < buttons.size(); ++i) {
    if (buttons[i] == focused) {
      current = i;
      break;
    }
  }

  const size_t next = delta < 0 ? (current == 0 ? buttons.size() - 1 : current - 1)
                                : (current + 1) % buttons.size();
  buttons[next]->RequestFocus();
  return true;
}

void MahoDomScreenshotActionDialogView::RunDismissCallback() {
  if (action_invoked_) {
    return;
  }
  action_invoked_ = true;
  if (menu_.dismiss_callback) {
    std::move(menu_.dismiss_callback).Run();
  }
}

}  // namespace maho
