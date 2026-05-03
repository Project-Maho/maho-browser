// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_view.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_result_row_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kOverlayWidthDp = 600;
constexpr int kOverlayMaxHeightDp = 480;
constexpr int kOverlayCornerRadiusDp = 12;
constexpr int kOverlayBorderWidthDp = 1;
constexpr int kContentPaddingDp = 8;
constexpr int kTextfieldHeightDp = 44;
constexpr int kTextfieldCornerRadiusDp = 8;
constexpr int kTextfieldPaddingHDp = 12;
constexpr int kResultsSpacingDp = 2;

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
      gfx::Insets(kContentPaddingDp), kResultsSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(views::CreateRoundedRectBackground(ui::kColorSysSurface2,
                                                   kOverlayCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(kOverlayBorderWidthDp,
                                           kOverlayCornerRadiusDp,
                                           ui::kColorSysNeutralOutline));

  textfield_ = AddChildView(std::make_unique<views::Textfield>());
  textfield_->SetPlaceholderText(
      u"Search tabs, bookmarks, actions\u2026");
  textfield_->set_controller(this);
  textfield_->SetPreferredSize(gfx::Size(0, kTextfieldHeightDp));
  textfield_->SetBackgroundEnabled(false);
  textfield_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(0, kTextfieldPaddingHDp)));
  textfield_->SetFontList(
      views::style::GetFont(views::style::CONTEXT_TEXTFIELD,
                            views::style::STYLE_BODY_3));

  scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_view_->SetDrawOverflowIndicator(false);
  scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_view_->ClipHeightTo(0, kOverlayMaxHeightDp - kTextfieldHeightDp -
                                    kContentPaddingDp * 3);
  layout->SetFlexForView(scroll_view_, 1);

  results_container_ = scroll_view_->SetContents(
      std::make_unique<views::View>());
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
  hint_label_->SetEnabledColorId(ui::kColorSysOnSurfaceSubtle);
  hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  hint_label_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(20, 0, 20, 0)));

  SetPreferredSize(gfx::Size(kOverlayWidthDp, 0));
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
    hint_label_->SetEnabledColorId(ui::kColorSysOnSurfaceSubtle);
    hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    hint_label_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(20, 0, 20, 0)));
    InvalidateLayout();
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

}  // namespace maho
