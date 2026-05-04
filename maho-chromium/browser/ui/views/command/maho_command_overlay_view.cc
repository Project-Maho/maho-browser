// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_view.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_result_row_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"

namespace maho {

namespace {

constexpr int kPanelCornerRadiusDp = 16;
constexpr int kPanelBorderWidthDp = 1;
constexpr int kPanelPaddingDp = 12;
constexpr int kSearchRowHeightDp = 44;
constexpr int kSearchRowCornerRadiusDp = 12;
constexpr int kSearchRowPaddingHDp = 14;
constexpr int kResultsSpacingDp = 4;
constexpr int kMaxResultsHeightDp = 420;
constexpr int kSearchIconSizeDp = 16;

constexpr SkColor kPanelColor = SkColorSetRGB(0x1A, 0x1D, 0x26);
constexpr SkColor kPanelBorderColor = SkColorSetARGB(0x30, 0xFF, 0xFF, 0xFF);
constexpr SkColor kSearchRowColor = SkColorSetRGB(0x22, 0x26, 0x30);
constexpr SkColor kSearchRowBorderColor = SkColorSetARGB(0x28, 0xFF, 0xFF, 0xFF);
constexpr SkColor kPrimaryTextColor = SkColorSetRGB(0xF0, 0xF2, 0xF5);
constexpr SkColor kSecondaryTextColor = SkColorSetRGB(0x8A, 0x92, 0xA6);
constexpr SkColor kSearchIconColor = SkColorSetRGB(0x8A, 0x92, 0xA6);

class SearchIconView : public views::View {
 public:
  SearchIconView() {
    SetPreferredSize(gfx::Size(kSearchIconSizeDp, kSearchIconSizeDp));
  }

  SearchIconView(const SearchIconView&) = delete;
  SearchIconView& operator=(const SearchIconView&) = delete;
  ~SearchIconView() override = default;

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);

    const gfx::Rect bounds = GetContentsBounds();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.6f);
    flags.setColor(kSearchIconColor);

    float cx = bounds.CenterPoint().x() - 1.5f;
    float cy = bounds.CenterPoint().y() - 1.5f;
    canvas->DrawCircle(gfx::PointF(cx, cy), 4.5f, flags);
    canvas->DrawLine(
        gfx::PointF(cx + 3.5f, cy + 3.5f),
        gfx::PointF(bounds.right() - 1.0f, bounds.bottom() - 1.0f), flags);
  }

  METADATA_HEADER(SearchIconView, views::View)
};

BEGIN_METADATA(SearchIconView)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoCommandOverlayView)
END_METADATA

MahoCommandOverlayView::MahoCommandOverlayView(
    Browser* browser,
    DismissCallback dismiss_callback)
    : browser_(browser),
      model_(std::make_unique<MahoCommandModel>(browser)),
      dismiss_callback_(std::move(dismiss_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kPanelPaddingDp), kResultsSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(
      views::CreateRoundedRectBackground(kPanelColor, kPanelCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(
      kPanelBorderWidthDp, kPanelCornerRadiusDp, kPanelBorderColor));

  auto* search_row = AddChildView(std::make_unique<views::View>());
  auto* search_layout =
      search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kSearchRowPaddingHDp), 10));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  search_row->SetPreferredSize(gfx::Size(0, kSearchRowHeightDp));
  search_row->SetBackground(views::CreateRoundedRectBackground(
      kSearchRowColor, kSearchRowCornerRadiusDp));
  search_row->SetBorder(views::CreateRoundedRectBorder(
      1, kSearchRowCornerRadiusDp, kSearchRowBorderColor));

  search_row->AddChildView(std::make_unique<SearchIconView>());

  textfield_ = search_row->AddChildView(std::make_unique<views::Textfield>());
  textfield_->SetPlaceholderText(u"Search tabs, bookmarks, actions\u2026");
  textfield_->set_controller(this);
  textfield_->SetPreferredSize(gfx::Size(0, kSearchRowHeightDp - 8));
  textfield_->SetBackgroundEnabled(false);
  textfield_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  textfield_->SetTextColor(kPrimaryTextColor);
  textfield_->set_placeholder_text_color(kSecondaryTextColor);
  search_layout->SetFlexForView(textfield_, 1);

  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetDrawOverflowIndicator(false);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->ClipHeightTo(0, kMaxResultsHeightDp);
  layout->SetFlexForView(scroll_view_, 1);

  results_container_ =
      scroll_view_->SetContents(std::make_unique<views::View>());
  auto* results_layout = results_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kResultsSpacingDp));
  results_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  hint_label_ = results_container_->AddChildView(
      std::make_unique<views::Label>(
          u"Type to search tabs, bookmarks, actions\u2026",
          views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
  hint_label_->SetEnabledColor(kSecondaryTextColor);
  hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  hint_label_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(20, 0, 20, 0)));
}

MahoCommandOverlayView::~MahoCommandOverlayView() {
  model_->Cancel();
}

void MahoCommandOverlayView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  std::string query = base::UTF16ToUTF8(new_contents);
  model_->Search(
      query,
      base::BindRepeating(&MahoCommandOverlayView::OnSearchResults,
                          base::Unretained(this)));
}

bool MahoCommandOverlayView::HandleKeyEvent(views::Textfield* sender,
                                              const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed)
    return false;

  switch (key_event.key_code()) {
    case ui::VKEY_ESCAPE:
      Dismiss();
      return true;
    case ui::VKEY_RETURN:
      ActivateSelected();
      return true;
    case ui::VKEY_DOWN: {
      int count = static_cast<int>(model_->results().size());
      if (count > 0) {
        int next = (model_->selected_index() + 1) % count;
        UpdateSelection(next);
      }
      return true;
    }
    case ui::VKEY_UP: {
      int count = static_cast<int>(model_->results().size());
      if (count > 0) {
        int prev = model_->selected_index() - 1;
        if (prev < 0)
          prev = count - 1;
        UpdateSelection(prev);
      }
      return true;
    }
    default:
      return false;
  }
}

void MahoCommandOverlayView::OnSearchResults(
    std::vector<CommandSuggestion> results) {
  UpdateResultViews(results);
}

void MahoCommandOverlayView::UpdateResultViews(
    const std::vector<CommandSuggestion>& results) {
  results_container_->RemoveAllChildViews();
  hint_label_ = nullptr;

  if (results.empty()) {
    if (model_->query().empty()) {
      hint_label_ = results_container_->AddChildView(
          std::make_unique<views::Label>(
              u"Type to search tabs, bookmarks, actions\u2026",
              views::style::CONTEXT_LABEL, views::style::STYLE_BODY_4));
    } else {
      hint_label_ = results_container_->AddChildView(
          std::make_unique<views::Label>(
              u"No results", views::style::CONTEXT_LABEL,
              views::style::STYLE_BODY_4));
    }
    hint_label_->SetEnabledColor(kSecondaryTextColor);
    hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    hint_label_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(20, 0, 20, 0)));
    InvalidateLayout();
    ResizeWidgetToContents();
    return;
  }

  for (size_t i = 0; i < results.size(); ++i) {
    auto* row = results_container_->AddChildView(
        std::make_unique<MahoCommandResultRowView>(results[i]));
    if (static_cast<int>(i) == model_->selected_index()) {
      row->SetSelected(true);
    }
  }
  InvalidateLayout();
  ResizeWidgetToContents();
}

void MahoCommandOverlayView::UpdateSelection(int new_index) {
  int old_index = model_->selected_index();
  model_->set_selected_index(new_index);

  auto children = results_container_->children();
  if (old_index >= 0 && old_index < static_cast<int>(children.size())) {
    static_cast<MahoCommandResultRowView*>(children[old_index])
        ->SetSelected(false);
  }
  if (new_index >= 0 && new_index < static_cast<int>(children.size())) {
    auto* row = static_cast<MahoCommandResultRowView*>(children[new_index]);
    row->SetSelected(true);
    row->ScrollViewToVisible();
  }
}

void MahoCommandOverlayView::ActivateSelected() {
  const auto& results = model_->results();
  int index = model_->selected_index();
  if (index < 0 || index >= static_cast<int>(results.size()))
    return;

  const auto& suggestion = results[index];
  Dismiss();
  OpenCommandSuggestion(browser_, suggestion);
}

void MahoCommandOverlayView::Dismiss() {
  if (dismiss_callback_)
    std::move(dismiss_callback_).Run();
}

void MahoCommandOverlayView::ResizeWidgetToContents() {
  auto* widget = GetWidget();
  if (!widget)
    return;

  gfx::Size preferred = GetPreferredSize();
  gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  bounds.set_height(preferred.height());
  widget->SetBounds(bounds);
}

}  // namespace maho
