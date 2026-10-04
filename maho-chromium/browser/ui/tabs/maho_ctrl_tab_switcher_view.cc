// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_view.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/generated_resources.h"
#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_card_view.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"

namespace maho {

namespace {

constexpr int kOverlayPadding = 24;
constexpr int kCardSpacing = 12;
constexpr int kOverlayBorderRadius = 16;
constexpr int kMoreAffordanceWidth = 120;
constexpr int kMoreAffordanceBorderRadius = 10;
constexpr int kMoreAffordanceBorderThickness = 2;

constexpr SkColor kMoreAffordanceSelectedBorder =
    SkColorSetARGB(0xFF, 0x6E, 0xA9, 0xFF);
constexpr SkColor kMoreAffordanceUnselectedBorder =
    SkColorSetARGB(0x40, 0xFF, 0xFF, 0xFF);

}  // namespace

MahoCtrlTabSwitcherView::MahoCtrlTabSwitcherView(
    std::vector<content::WebContents*> tabs,
    size_t initial_selected_index,
    size_t max_visible)
    : tabs_(std::move(tabs)),
      selected_index_(initial_selected_index) {
  auto* box = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets(kOverlayPadding), kCardSpacing));
  box->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetBackground(views::CreateRoundedRectBackground(
      SkColorSetARGB(0xF0, 0x18, 0x18, 0x1B), kOverlayBorderRadius));
  SetBorder(views::CreateRoundedRectBorder(
      1, kOverlayBorderRadius,
      SkColorSetARGB(0x40, 0xFF, 0xFF, 0xFF)));

  const size_t visible_tabs =
      std::min(tabs_.size(), std::max<size_t>(max_visible, 1u));
  has_more_affordance_ = tabs_.size() > visible_tabs;

  for (size_t i = 0; i < visible_tabs; ++i) {
    auto* card = AddChildView(std::make_unique<MahoCtrlTabSwitcherCardView>(
        tabs_[i], i == selected_index_));
    card_views_.push_back(card);
  }

  if (has_more_affordance_) {
    const size_t remaining = tabs_.size() - visible_tabs;
    std::u16string more_text = l10n_util::GetPluralStringFUTF16(
        IDS_MAHO_CTRL_TAB_MORE_AFFORDANCE_COUNT,
        static_cast<int>(remaining));
    auto more_container = std::make_unique<views::View>();
    more_container->SetPreferredSize(
        gfx::Size(kMoreAffordanceWidth,
                  MahoCtrlTabSwitcherCardView::kCardHeight));
    more_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(6), 4));
    static_cast<views::BoxLayout*>(more_container->GetLayoutManager())
        ->set_main_axis_alignment(
            views::BoxLayout::MainAxisAlignment::kCenter);
    static_cast<views::BoxLayout*>(more_container->GetLayoutManager())
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kCenter);

    auto label = std::make_unique<views::Label>(more_text);
    label->SetEnabledColor(SkColorSetRGB(0xCF, 0xCF, 0xD2));
    label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    more_container->AddChildView(std::move(label));

    auto hint = std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_MAHO_CTRL_TAB_MORE_AFFORDANCE_HINT));
    hint->SetEnabledColor(SkColorSetRGB(0x88, 0x88, 0x8C));
    hint->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    more_container->AddChildView(std::move(hint));

    more_affordance_view_ = AddChildView(std::move(more_container));
    more_affordance_view_->SetBackground(nullptr);
    more_affordance_view_->SetBorder(views::CreateRoundedRectBorder(
        kMoreAffordanceBorderThickness,
        kMoreAffordanceBorderRadius, kMoreAffordanceUnselectedBorder));
  }

  if (card_views_.empty()) {
    selected_index_ = 0;
  } else {
    const size_t max_index =
        has_more_affordance_ ? card_views_.size() : card_views_.size() - 1;
    if (selected_index_ > max_index) {
      selected_index_ = max_index;
    }
  }

  // Route hover events to this container so OnMouseMoved can hit-test the slot
  // under the cursor. The individual cards and the "+N more" affordance are
  // passive. (Clicks are hit-tested by the controller's mouse monitor, since
  // the non-activatable overlay window does not receive mouseDown on macOS.)
  for (views::View* card : card_views_) {
    card->SetCanProcessEventsWithinSubtree(false);
  }
  if (more_affordance_view_) {
    more_affordance_view_->SetCanProcessEventsWithinSubtree(false);
  }
}

MahoCtrlTabSwitcherView::~MahoCtrlTabSwitcherView() = default;

void MahoCtrlTabSwitcherView::SetSelectedIndex(size_t index) {
  if (card_views_.empty() && !has_more_affordance_) {
    return;
  }
  const size_t max_index =
      has_more_affordance_
          ? card_views_.size()
          : (card_views_.empty() ? 0 : card_views_.size() - 1);
  if (index > max_index || index == selected_index_) {
    return;
  }

  if (selected_index_ < card_views_.size()) {
    card_views_[selected_index_]->SetSelected(false);
  } else if (more_affordance_view_) {
    more_affordance_view_->SetBorder(views::CreateRoundedRectBorder(
        kMoreAffordanceBorderThickness, kMoreAffordanceBorderRadius,
        kMoreAffordanceUnselectedBorder));
  }

  if (index < card_views_.size()) {
    card_views_[index]->SetSelected(true);
  } else if (more_affordance_view_) {
    more_affordance_view_->SetBorder(views::CreateRoundedRectBorder(
        kMoreAffordanceBorderThickness, kMoreAffordanceBorderRadius,
        kMoreAffordanceSelectedBorder));
  }

  selected_index_ = index;
}

content::WebContents* MahoCtrlTabSwitcherView::GetSelectedWebContents() const {
  return GetWebContentsAt(selected_index_);
}

content::WebContents* MahoCtrlTabSwitcherView::GetWebContentsAt(
    size_t index) const {
  if (index >= card_views_.size() || index >= tabs_.size()) {
    return nullptr;
  }
  return tabs_[index];
}

void MahoCtrlTabSwitcherView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateThemeColors();
}

void MahoCtrlTabSwitcherView::SetHoverCallback(SlotCallback callback) {
  hover_callback_ = std::move(callback);
}

std::optional<size_t> MahoCtrlTabSwitcherView::SlotIndexAt(
    const gfx::Point& location) const {
  for (size_t i = 0; i < card_views_.size(); ++i) {
    if (card_views_[i] && card_views_[i]->bounds().Contains(location)) {
      return i;
    }
  }
  if (has_more_affordance_ && more_affordance_view_ &&
      more_affordance_view_->bounds().Contains(location)) {
    return card_views_.size();
  }
  return std::nullopt;
}

std::optional<size_t> MahoCtrlTabSwitcherView::SlotIndexAtScreen(
    const gfx::Point& screen_point) const {
  gfx::Point local = screen_point;
  views::View::ConvertPointFromScreen(this, &local);
  return SlotIndexAt(local);
}

void MahoCtrlTabSwitcherView::OnMouseMoved(const ui::MouseEvent& event) {
  const std::optional<size_t> slot = SlotIndexAt(event.location());
  if (!slot.has_value() || *slot == selected_index_) {
    return;
  }
  if (hover_callback_) {
    hover_callback_.Run(*slot);
  }
}

ui::Cursor MahoCtrlTabSwitcherView::GetCursor(const ui::MouseEvent& event) {
  return SlotIndexAt(event.location()).has_value()
             ? ui::Cursor(ui::mojom::CursorType::kHand)
             : ui::Cursor();
}

void MahoCtrlTabSwitcherView::UpdateThemeColors() {
  // Hardcoded translucent gray matching the command palette's visual
  // language (kMahoColorCommandBarBackground) with alpha for a floating
  // overlay feel — deliberately theme-independent so the switcher stays
  // consistent across space themes.
  SetBackground(views::CreateRoundedRectBackground(
      SkColorSetARGB(0xB0, 0x17, 0x19, 0x20), kOverlayBorderRadius));
  SetBorder(views::CreateRoundedRectBorder(
      1, kOverlayBorderRadius,
      SkColorSetARGB(0x22, 0xFF, 0xFF, 0xFF)));
}

BEGIN_METADATA(MahoCtrlTabSwitcherView)
END_METADATA

}  // namespace maho
