// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/site_control/maho_page_info_ui.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kBubbleCornerRadius = 18;
constexpr int kRowSpacing = 8;
const gfx::Insets kBubbleContentsInsets = gfx::Insets::TLBR(16, 16, 16, 16);

}  // namespace

BEGIN_METADATA(MahoShieldBubbleView)
END_METADATA

MahoShieldBubbleView::MahoShieldBubbleView(Browser* browser,
                                           const std::string& origin,
                                           uint32_t blocked_count,
                                           bool is_excepted)
    : browser_(browser), origin_(origin), is_excepted_(is_excepted) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, kBubbleContentsInsets,
      kRowSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface2,
                                                   kBubbleCornerRadius));
  SetBorder(views::CreateRoundedRectBorder(1, kBubbleCornerRadius,
                                           ui::kColorSysNeutralOutline));

  auto* origin_label = AddChildView(std::make_unique<views::Label>(
      base::UTF8ToUTF16(origin), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_3_MEDIUM));
  origin_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  origin_label->SetEnabledColor(ui::kColorSysOnSurface);

  auto* toggle_row = AddChildView(std::make_unique<views::View>());
  auto* toggle_layout =
      toggle_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));

  auto* toggle_label = toggle_row->AddChildView(std::make_unique<views::Label>(
      u"Content Blocker", views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_4));
  toggle_label->SetEnabledColor(ui::kColorSysOnSurface);
  toggle_layout->SetFlexForView(toggle_label, 1);

  toggle_ = toggle_row->AddChildView(std::make_unique<views::ToggleButton>(
      base::BindRepeating(&MahoShieldBubbleView::OnTogglePressed,
                          base::Unretained(this))));
  toggle_->SetIsOn(!is_excepted_);
  toggle_->SetAccessibleName(u"Content Blocker");

  status_label_ = AddChildView(std::make_unique<views::Label>(
      base::UTF8ToUTF16(base::NumberToString(blocked_count) +
                        " requests blocked"),
      views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetEnabledColor(ui::kColorSysOnSurfaceVariant);

  AddChildView(std::make_unique<views::Separator>());

  auto* settings_button =
      AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&MahoShieldBubbleView::OnSettingsPressed,
                              base::Unretained(this)),
          u"Content Blocker Settings"));
  settings_button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  settings_button->SetLabelStyle(views::style::STYLE_BODY_4);
  settings_button->SetEnabledTextColors(ui::kColorSysOnSurfaceVariant);

}

// static
views::Widget* MahoShieldBubbleView::Show(views::View* anchor_view,
                                          Browser* browser,
                                          const std::string& origin,
                                          uint32_t blocked_count,
                                          bool is_excepted) {
  if (!anchor_view) {
    return nullptr;
  }

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::TOP_RIGHT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets());
  delegate->SetContentsView(std::make_unique<MahoShieldBubbleView>(
      browser, origin, blocked_count, is_excepted));

  views::Widget* widget =
      views::BubbleDialogDelegate::CreateBubble(std::move(delegate));
  if (widget) {
    widget->Show();
  }
  return widget;
}

void MahoShieldBubbleView::OnTogglePressed() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  MahoCore* core = maho::GetCore();
  if (!core) {
    return;
  }

  is_excepted_ = !is_excepted_;
  if (is_excepted_) {
    maho::core::AddSiteException(core, origin_.c_str());
  } else {
    maho::core::RemoveSiteException(core, origin_.c_str());
  }

  status_label_->SetText(base::UTF8ToUTF16(
      is_excepted_ ? "Content blocker paused for this site"
                   : "Content blocker active"));
}

void MahoShieldBubbleView::OnSettingsPressed(
    [[maybe_unused]] const ui::Event& event) {
  if (!browser_) {
    return;
  }
  maho::OpenMahoSettingsPane(browser_, "content-blocker");
}

}  // namespace maho
