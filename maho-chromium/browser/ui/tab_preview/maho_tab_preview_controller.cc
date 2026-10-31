// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/tab_preview/maho_tab_preview_controller.h"

#include <memory>

#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/class_property.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {

namespace {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(views::BubbleDialogDelegate,
                                 kPreviewDelegateKey)

constexpr base::TimeDelta kTabPreviewShowDelay = base::Milliseconds(225);
constexpr int kPreviewCornerRadius = 12;
constexpr int kPreviewBorderThickness = 1;
constexpr int kPreviewMaxTextWidth = 280;
constexpr int kPreviewTitleLineLimit = 2;
constexpr int kPreviewInsetsHorizontal = 12;
constexpr int kPreviewInsetsVertical = 10;
constexpr int kPreviewLabelSpacing = 4;

std::unique_ptr<views::View> BuildBubbleContents(const std::u16string& title,
                                                 const std::u16string& url_text) {
  auto contents = std::make_unique<views::View>();
  auto* layout = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kPreviewInsetsVertical, kPreviewInsetsHorizontal),
      kPreviewLabelSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  contents->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface4, kPreviewCornerRadius));
  contents->SetBorder(views::CreateRoundedRectBorder(
      kPreviewBorderThickness, kPreviewCornerRadius,
      ui::kColorSysNeutralOutline));

  auto* title_label = contents->AddChildView(std::make_unique<views::Label>(
      title, views::style::CONTEXT_DIALOG_TITLE,
      views::style::STYLE_HEADLINE_5));
  title_label->SetAutoColorReadabilityEnabled(false);
  title_label->SetEnabledColor(ui::kColorSysOnSurface);
  title_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label->SetMultiLine(true);
  title_label->SetAllowCharacterBreak(true);
  title_label->SetMaxLines(kPreviewTitleLineLimit);
  title_label->SetMaximumWidth(kPreviewMaxTextWidth);

  auto* url_label = contents->AddChildView(std::make_unique<views::Label>(
      url_text, views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  url_label->SetAutoColorReadabilityEnabled(false);
  url_label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
  url_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  url_label->SetMultiLine(false);
  url_label->SetElideBehavior(gfx::ELIDE_TAIL);
  url_label->SetMaximumWidthSingleLine(kPreviewMaxTextWidth);

  contents->GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  contents->SetAccessibleName(title + u"\n" + url_text);
  return contents;
}

}  // namespace

MahoTabPreviewController::MahoTabPreviewController(Browser* browser)
    : browser_(browser),
      anchor_view_tracker_(std::make_unique<views::ViewTracker>()) {}

MahoTabPreviewController::~MahoTabPreviewController() {
  show_timer_.Stop();
  HidePreview();
  ResetAnchorView();
}

void MahoTabPreviewController::ShowPreview(views::View* anchor_view,
                                           int tab_index) {
  TabStripModel* tab_strip_model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!tab_strip_model || !anchor_view || !tab_strip_model->ContainsIndex(tab_index) ||
      tab_index == tab_strip_model->active_index()) {
    HidePreview();
    return;
  }

  pending_tab_index_ = tab_index;
  anchor_view_tracker_->SetView(anchor_view);

  if (bubble_widget_) {
    HidePreview();
    pending_tab_index_ = tab_index;
    anchor_view_tracker_->SetView(anchor_view);
  }

  show_timer_.Start(FROM_HERE,
                    show_delay_for_testing_.value_or(kTabPreviewShowDelay),
                    this, &MahoTabPreviewController::ShowPreviewNow);
}

void MahoTabPreviewController::HidePreview() {
  show_timer_.Stop();
  pending_tab_index_ = -1;
  preview_title_for_testing_.clear();
  preview_url_for_testing_.clear();
  ResetAnchorView();

  if (!bubble_widget_) {
    return;
  }

  bubble_widget_observation_.Reset();
  views::Widget* widget = bubble_widget_;
  bubble_widget_ = nullptr;
  if (widget && !widget->IsClosed()) {
    widget->CloseNow();
  }
}

bool MahoTabPreviewController::IsShowingForTesting() const {
  return bubble_widget_ != nullptr;
}

void MahoTabPreviewController::ShowPreviewNow() {
  views::View* anchor_view = anchor_view_tracker_ ? anchor_view_tracker_->view() : nullptr;
  TabStripModel* tab_strip_model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!anchor_view || !tab_strip_model ||
      !tab_strip_model->ContainsIndex(pending_tab_index_) ||
      pending_tab_index_ == tab_strip_model->active_index()) {
    HidePreview();
    return;
  }

  content::WebContents* contents =
      tab_strip_model->GetWebContentsAt(pending_tab_index_);
  if (!contents || !anchor_view->GetWidget()) {
    HidePreview();
    return;
  }

  const std::u16string title = contents->GetTitle().empty()
                                   ? u"Untitled tab"
                                   : contents->GetTitle();
  const std::u16string url_text = GetUrlTextForTab(pending_tab_index_);

  auto bubble = std::make_unique<views::BubbleDialogDelegate>(
      anchor_view, views::BubbleBorder::LEFT_TOP);
  bubble->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble->set_margins(gfx::Insets());
  bubble->SetCanActivate(false);
  bubble->set_close_on_deactivate(false);
  bubble->SetContentsView(BuildBubbleContents(title, url_text));

  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      bubble.get(), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (!widget) {
    HidePreview();
    return;
  }

  widget->SetProperty(kPreviewDelegateKey, std::move(bubble));
  bubble_widget_ = widget;
  bubble_widget_observation_.Observe(widget);
  preview_title_for_testing_ = title;
  preview_url_for_testing_ = url_text;
  widget->ShowInactive();
}

void MahoTabPreviewController::ResetAnchorView() {
  if (anchor_view_tracker_) {
    anchor_view_tracker_->SetView(nullptr);
  }
}

std::u16string MahoTabPreviewController::GetUrlTextForTab(int tab_index) const {
  TabStripModel* tab_strip_model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!tab_strip_model || !tab_strip_model->ContainsIndex(tab_index)) {
    return std::u16string();
  }

  content::WebContents* contents = tab_strip_model->GetWebContentsAt(tab_index);
  if (!contents) {
    return std::u16string();
  }

  const GURL& url = contents->GetVisibleURL();
  if (url.is_empty()) {
    return std::u16string();
  }

  return base::UTF8ToUTF16(url.spec());
}

void MahoTabPreviewController::OnWidgetDestroying(views::Widget* widget) {
  if (widget != bubble_widget_) {
    return;
  }
  bubble_widget_observation_.Reset();
  bubble_widget_ = nullptr;
  preview_title_for_testing_.clear();
  preview_url_for_testing_.clear();
}

}  // namespace maho
