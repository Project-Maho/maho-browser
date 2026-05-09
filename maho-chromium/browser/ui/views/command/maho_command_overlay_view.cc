// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/command/maho_command_overlay_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_sparkle_geometry.h"
#include "maho/browser/ui/views/command/maho_command_result_row_view.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "base/strings/escape.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kPanelCornerRadiusDp = 22;
constexpr int kPanelBorderWidthDp = 1;
constexpr int kPanelPaddingTopDp = 10;
constexpr int kPanelPaddingSideDp = 0;
constexpr int kFieldShellSpacingDp = 8;
constexpr int kCurrentTabShellPaddingHDp = 10;
constexpr int kCurrentTabShellPaddingVDp = 8;
constexpr int kCurrentTabContextSpacingDp = 8;
constexpr int kCurrentTabContextLabelHorizontalInsetDp = 8;
constexpr int kCurrentTabContextLabelVerticalInsetDp = 2;
constexpr int kTopbarHorizontalInsetDp = 10;
constexpr int kPanelBottomInsetDp = 18;
constexpr int kCurrentTabPanelBottomInsetDp = 38;
constexpr int kSearchRowHeightDp = 31;
constexpr int kCurrentTabSearchRowHeightDp = 31;
constexpr int kSearchRowCornerRadiusDp = 3;
constexpr int kSearchRowPaddingHDp = 7;
constexpr int kSearchTextTrailingGutterDp = 58;
constexpr int kSearchIconLaneWidthDp = 30;
constexpr int kCurrentTabShellIconLaneWidthDp = 18;
constexpr int kSparkToInputGapDp = 7;
constexpr int kResultsSpacingDp = 0;
constexpr int kDividerTopGapDp = 6;
constexpr int kDividerBottomGapDp = 5;
constexpr int kDividerHorizontalInsetDp = 12;
constexpr int kMaxResultsHeightDp = 420;
constexpr int kSearchIconSizeDp = 15;
constexpr int kModeLabelSpacingDp = 6;
constexpr int kModeChipCornerRadiusDp = 8;
constexpr int kModeChipHorizontalInsetDp = 8;
constexpr int kModeChipVerticalInsetDp = 2;
constexpr int kModeChipDotSizeDp = 5;
constexpr int kModeChipDotLabelSpacingDp = 5;
constexpr int kSearchTextfieldHeightDp = 24;
constexpr int kCurrentTabSearchTextfieldHeightDp = 24;
constexpr int kCurrentTabFontSizeDeltaDp = 4;
constexpr int kEmptyStateTopInsetDp = 16;
constexpr int kEmptyStateBottomInsetDp = 10;
constexpr int kNoResultsTopInsetDp = 8;
constexpr int kNoResultsBottomInsetDp = 6;
constexpr int kEmptyStateHorizontalInsetDp = 48;

constexpr SkColor kCurrentTabShellFill = SkColorSetARGB(0x0F, 0xFF, 0xFF, 0xFF);
constexpr SkColor kCurrentTabShellBorder = SkColorSetARGB(0x24, 0xFF, 0xFF, 0xFF);

constexpr SkColor kSparkleOuterColor = SkColorSetRGB(0x73, 0x7C, 0xFF);
constexpr SkColor kSparkleInnerColor = SkColorSetRGB(0xFF, 0xC7, 0x57);

gfx::Insets EmptyStateInsets(bool compact) {
  return gfx::Insets::TLBR(
      compact ? kNoResultsTopInsetDp : kEmptyStateTopInsetDp,
      kEmptyStateHorizontalInsetDp,
      compact ? kNoResultsBottomInsetDp : kEmptyStateBottomInsetDp,
      kEmptyStateHorizontalInsetDp);
}

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
    if (bounds.IsEmpty()) {
      return;
    }

    const gfx::RectF sparkle_rect = command_palette::MakeSparkleRect(bounds);
    const SkPath outer_path = command_palette::BuildSparklePath(
        sparkle_rect, command_palette::kSparkleOuterPoints);
    const SkPath inner_path = command_palette::BuildSparklePath(
        sparkle_rect, command_palette::kSparkleInnerPoints);

    cc::PaintFlags outer_flags;
    outer_flags.setAntiAlias(true);
    outer_flags.setStyle(cc::PaintFlags::kFill_Style);
    outer_flags.setColor(kSparkleOuterColor);
    canvas->DrawPath(outer_path, outer_flags);

    cc::PaintFlags inner_flags;
    inner_flags.setAntiAlias(true);
    inner_flags.setStyle(cc::PaintFlags::kFill_Style);
    inner_flags.setColor(kSparkleInnerColor);
    canvas->DrawPath(inner_path, inner_flags);
  }

  METADATA_HEADER(SearchIconView, views::View)
};

class CommandPaletteTextfield : public views::Textfield {
 public:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return event.key_code() == ui::VKEY_TAB;
  }

  METADATA_HEADER(CommandPaletteTextfield, views::Textfield)
};

BEGIN_METADATA(SearchIconView)
END_METADATA

BEGIN_METADATA(CommandPaletteTextfield)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoCommandOverlayView)
END_METADATA

MahoCommandOverlayView::MahoCommandOverlayView(
    Browser* browser,
    CommandOverlayMode mode,
    const std::string& initial_text,
    bool select_initial_text,
    DismissCallback dismiss_callback,
    ResizeCallback resize_callback)
    : browser_(browser),
      mode_(mode),
      model_(std::make_unique<MahoCommandModel>(browser)),
      dismiss_callback_(std::move(dismiss_callback)),
      resize_callback_(std::move(resize_callback)),
      select_initial_text_(select_initial_text) {
  DCHECK(browser_);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kPanelPaddingTopDp, kPanelPaddingSideDp,
                        mode_ == CommandOverlayMode::kCurrentTab
                            ? kCurrentTabPanelBottomInsetDp
                            : kPanelBottomInsetDp,
                        kPanelPaddingSideDp),
      kResultsSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCommandBarBackground, kPanelCornerRadiusDp));
  SetBorder(views::CreateRoundedRectBorder(
      kPanelBorderWidthDp, kPanelCornerRadiusDp,
      kMahoColorCommandBarPanelBorder));

  field_shell_ = AddChildView(std::make_unique<views::View>());
  auto* field_shell_layout = field_shell_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::VH(0, 0),
                                         kFieldShellSpacingDp));
  field_shell_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  current_tab_shell_ = field_shell_->AddChildView(std::make_unique<views::View>());
  auto* current_tab_shell_layout = current_tab_shell_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::VH(
                                             kCurrentTabShellPaddingVDp,
                                             kCurrentTabShellPaddingHDp),
                                         kCurrentTabContextSpacingDp));
  current_tab_shell_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  current_tab_shell_->SetBackground(views::CreateRoundedRectBackground(
      kCurrentTabShellFill, kPanelCornerRadiusDp - 2));
  current_tab_shell_->SetBorder(views::CreateRoundedRectBorder(
      1, kPanelCornerRadiusDp - 2, kCurrentTabShellBorder));

  current_tab_context_row_ = current_tab_shell_->AddChildView(
      std::make_unique<views::View>());
  auto* current_tab_context_layout =
      current_tab_context_row_->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal,
              gfx::Insets::VH(0, 0), kCurrentTabContextSpacingDp));
  current_tab_context_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  current_tab_context_row_->SetVisible(
      mode_ == CommandOverlayMode::kCurrentTab);

  current_tab_context_label_ = current_tab_context_row_->AddChildView(
      std::make_unique<views::Label>(u"Current tab", views::style::CONTEXT_LABEL,
                                     views::style::STYLE_BODY_5_MEDIUM));
  current_tab_context_label_->SetAutoColorReadabilityEnabled(false);
  current_tab_context_label_->SetBorder(views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(1, 8, kCurrentTabShellBorder),
      gfx::Insets::VH(kCurrentTabContextLabelVerticalInsetDp,
                      kCurrentTabContextLabelHorizontalInsetDp)));

  current_tab_url_label_ = current_tab_context_row_->AddChildView(
      std::make_unique<views::Label>(u"", views::style::CONTEXT_LABEL,
                                     views::style::STYLE_BODY_5));
  current_tab_url_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  current_tab_url_label_->SetAutoColorReadabilityEnabled(false);
  current_tab_url_label_->SetMultiLine(false);
  current_tab_url_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  current_tab_context_layout->SetFlexForView(current_tab_url_label_, 1);

  search_row_ = field_shell_->AddChildView(std::make_unique<views::View>());
  auto* topbar_layout =
      search_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kTopbarHorizontalInsetDp), kSparkToInputGapDp));
  topbar_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto* search_icon_lane = search_row_->AddChildView(std::make_unique<views::View>());
  auto* search_icon_layout =
      search_icon_lane->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  search_icon_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  search_icon_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  search_icon_lane_ = search_icon_lane;
  search_icon_lane->SetPreferredSize(gfx::Size(
      mode_ == CommandOverlayMode::kCurrentTab ? kCurrentTabShellIconLaneWidthDp
                                               : kSearchIconLaneWidthDp,
      0));
  search_icon_lane->AddChildView(std::make_unique<SearchIconView>());

  input_background_ = search_row_->AddChildView(std::make_unique<views::View>());
  input_background_->SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCommandBarInputFill, kSearchRowCornerRadiusDp));
  input_background_->SetBorder(views::CreateRoundedRectBorder(
      kPanelBorderWidthDp, kSearchRowCornerRadiusDp,
      kMahoColorCommandBarInputBorder));
  topbar_layout->SetFlexForView(input_background_, 1);
  auto* search_layout =
      input_background_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kSearchRowPaddingHDp), kModeLabelSpacingDp));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  textfield_ = input_background_->AddChildView(
      std::make_unique<CommandPaletteTextfield>());
  textfield_->set_controller(this);
  textfield_->SetBackgroundEnabled(false);
  textfield_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  UpdateLayoutForMode();
  UpdateTextfieldPresentation();
  search_layout->SetFlexForView(textfield_, 1);

  trailing_gutter_ = input_background_->AddChildView(std::make_unique<views::View>());
  trailing_gutter_->SetPreferredSize(gfx::Size(kSearchTextTrailingGutterDp, 0));
  trailing_gutter_->SetCanProcessEventsWithinSubtree(false);

  mode_chip_ = input_background_->AddChildView(std::make_unique<views::View>());
  auto* mode_chip_layout =
      mode_chip_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kModeChipVerticalInsetDp, kModeChipHorizontalInsetDp),
          kModeChipDotLabelSpacingDp));
  mode_chip_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  mode_chip_->SetVisible(false);

  mode_chip_dot_ = mode_chip_->AddChildView(std::make_unique<views::View>());
  mode_chip_dot_->SetPreferredSize(
      gfx::Size(kModeChipDotSizeDp, kModeChipDotSizeDp));

  mode_label_ = mode_chip_->AddChildView(std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5_MEDIUM));
  mode_label_->SetAutoColorReadabilityEnabled(false);

  if (!initial_text.empty()) {
    textfield_->SetText(base::UTF8ToUTF16(initial_text));
    if (select_initial_text_) {
      textfield_->SelectAll(mode_ == CommandOverlayMode::kCurrentTab);
    }
  }

  if (mode_ == CommandOverlayMode::kCurrentTab) {
    current_tab_url_text_ = initial_text;
    if (current_tab_url_text_.empty()) {
      current_tab_url_text_ = "Current tab";
    }
  }

  divider_ = AddChildView(std::make_unique<views::Separator>());
  divider_->SetColorId(kMahoColorCommandBarDivider);
  divider_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(kDividerTopGapDp, kDividerHorizontalInsetDp,
                        kDividerBottomGapDp, kDividerHorizontalInsetDp)));
  divider_->SetVisible(false);

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
          u"Type to search tabs, history, and URLs",
          views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  hint_label_->SetEnabledColor(kMahoColorCommandBarEmptyText);
  hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  hint_label_->SetAutoColorReadabilityEnabled(false);
  hint_label_->SetMultiLine(true);
  hint_label_->SetBorder(views::CreateEmptyBorder(EmptyStateInsets(false)));

  ApplyPlaceholderForMode();
  UpdateCurrentTabShell();

  if (!initial_text.empty()) {
    ContentsChanged(textfield_, std::u16string(textfield_->GetText()));
  }
}

MahoCommandOverlayView::~MahoCommandOverlayView() {
  model_->Cancel();
}

void MahoCommandOverlayView::RequestFocus() {
  if (textfield_ && GetFocusManager()) {
    GetFocusManager()->SetFocusedView(textfield_);
  } else if (textfield_) {
    textfield_->RequestFocus();
  } else {
    views::View::RequestFocus();
  }
}

void MahoCommandOverlayView::UpdateLayoutForMode() {
  const bool is_current_tab = mode_ == CommandOverlayMode::kCurrentTab;

  if (field_shell_) {
    field_shell_->SetVisible(true);
  }
  if (current_tab_shell_) {
    current_tab_shell_->SetVisible(is_current_tab);
  }
  if (current_tab_context_row_) {
    current_tab_context_row_->SetVisible(is_current_tab);
  }

  if (search_row_) {
    search_row_->SetPreferredSize(
        gfx::Size(0, is_current_tab ? kCurrentTabSearchRowHeightDp
                                    : kSearchRowHeightDp));
  }

  if (textfield_) {
    textfield_->SetPreferredSize(
        gfx::Size(0, is_current_tab ? kCurrentTabSearchTextfieldHeightDp
                                    : kSearchTextfieldHeightDp));
  }

  if (auto* layout = static_cast<views::BoxLayout*>(GetLayoutManager())) {
    layout->set_inside_border_insets(gfx::Insets::TLBR(
        kPanelPaddingTopDp, kPanelPaddingSideDp,
        is_current_tab ? kCurrentTabPanelBottomInsetDp : kPanelBottomInsetDp,
        kPanelPaddingSideDp));
  }

  PreferredSizeChanged();
}

void MahoCommandOverlayView::UpdateTextfieldPresentation() {
  if (!textfield_) {
    return;
  }

  if (mode_ == CommandOverlayMode::kCurrentTab) {
    const gfx::FontList current_tab_font =
        views::Textfield::GetDefaultFontList().Derive(
            kCurrentTabFontSizeDeltaDp, gfx::Font::FontStyle::NORMAL,
            gfx::Font::Weight::MEDIUM);
    textfield_->SetFontList(current_tab_font);
    textfield_->set_placeholder_font_list(current_tab_font);
    return;
  }

  textfield_->SetFontList(views::Textfield::GetDefaultFontList());
  textfield_->set_placeholder_font_list(views::Textfield::GetDefaultFontList());
}

void MahoCommandOverlayView::UpdateCurrentTabShell() {
  if (!current_tab_shell_ || !current_tab_context_label_ ||
      !current_tab_url_label_) {
    return;
  }

  const bool is_current_tab = mode_ == CommandOverlayMode::kCurrentTab;
  current_tab_shell_->SetVisible(is_current_tab);
  current_tab_context_label_->SetVisible(is_current_tab);
  current_tab_url_label_->SetVisible(is_current_tab);
  if (!is_current_tab) {
    return;
  }

  current_tab_url_label_->SetText(base::UTF8ToUTF16(
      current_tab_url_text_.empty() ? std::string("Current tab")
                                    : current_tab_url_text_));
}

void MahoCommandOverlayView::AddedToWidget() {
  views::View::AddedToWidget();
  if (observed_widget_) {
    observed_widget_->RemoveObserver(this);
  }
  observed_widget_ = GetWidget();
  if (observed_widget_) {
    observed_widget_->AddObserver(this);
  }
}

void MahoCommandOverlayView::RemovedFromWidget() {
  if (observed_widget_) {
    observed_widget_->RemoveObserver(this);
    observed_widget_ = nullptr;
  }
  views::View::RemovedFromWidget();
}

void MahoCommandOverlayView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const auto* cp = GetColorProvider();
  if (!cp)
    return;

  textfield_->SetTextColor(cp->GetColor(kMahoColorPrimaryText));
  textfield_->set_placeholder_text_color(
      SkColorSetA(cp->GetColor(kMahoColorPrimaryText), 0xA3));
  textfield_->SetSelectionBackgroundColor(
      cp->GetColor(kMahoColorCommandBarInputSelection));
  textfield_->SetSelectionTextColor(cp->GetColor(kMahoColorPrimaryText));
  if (input_background_) {
    const SkColor input_fill = cp->GetColor(kMahoColorCommandBarInputFill);
    const SkColor input_border = cp->GetColor(kMahoColorCommandBarInputBorder);
    const bool is_current_tab = mode_ == CommandOverlayMode::kCurrentTab;
    input_background_->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetA(input_fill, is_current_tab ? 0xFF : 0xFD),
        kSearchRowCornerRadiusDp));
    input_background_->SetBorder(views::CreateRoundedRectBorder(
        kPanelBorderWidthDp, kSearchRowCornerRadiusDp,
        SkColorSetA(input_border, is_current_tab ? 0x52 : 0x3A)));
  }
  UpdateTextfieldPresentation();
  UpdateModeChip();
}

void MahoCommandOverlayView::OnWidgetActivationChanged(views::Widget* widget,
                                                       bool active) {
  if (widget != observed_widget_ || !active || !textfield_) {
    return;
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayView> view) {
            if (!view || !view->textfield_) {
              return;
            }

            auto* widget = view->GetWidget();
            if (!widget || !widget->IsVisible() || !widget->IsActive()) {
              return;
            }

            view->RequestFocus();
          },
          weak_factory_.GetWeakPtr()));
}

void MahoCommandOverlayView::OnWidgetVisibilityChanged(views::Widget* widget,
                                                       bool visible) {
  if (widget != observed_widget_ || !visible) {
    return;
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoCommandOverlayView> view) {
            if (!view) {
              return;
            }
            auto* widget = view->GetWidget();
            if (!widget || !widget->IsVisible()) {
              return;
            }
            view->RequestFocus();
          },
          weak_factory_.GetWeakPtr()));
}

void MahoCommandOverlayView::OnWidgetDestroying(views::Widget* widget) {
  if (widget == observed_widget_) {
    observed_widget_ = nullptr;
  }
}

void MahoCommandOverlayView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  TriggerSearchForCurrentText();
}

void MahoCommandOverlayView::OnAfterUserAction(views::Textfield* sender) {
  TriggerSearchForCurrentText();
}

void MahoCommandOverlayView::TriggerSearchForCurrentText() {
  if (mode_ == CommandOverlayMode::kAI)
    return;

  std::string query = base::UTF16ToUTF8(textfield_->GetText());

  if (mode_ == CommandOverlayMode::kSiteSearch && !query.empty()) {
    query = "@" + site_search_prefix_ + " " + query;
  }

  model_->Search(
      query, mode_,
      base::BindRepeating(&MahoCommandOverlayView::OnSearchResults,
                          weak_factory_.GetWeakPtr()));
}

bool MahoCommandOverlayView::HandleKeyEvent(views::Textfield* sender,
                                              const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed)
    return false;

  if (textfield_->HasCompositionText() &&
      key_event.key_code() != ui::VKEY_ESCAPE) {
    return false;
  }

  switch (key_event.key_code()) {
    case ui::VKEY_ESCAPE:
      if (textfield_->HasCompositionText()) {
        return false;
      }
      if (mode_ == CommandOverlayMode::kAI) {
        ExitAIMode();
        return true;
      }
      if (mode_ == CommandOverlayMode::kSiteSearch) {
        ExitSiteSearchMode();
        return true;
      }
      Dismiss();
      return true;

    case ui::VKEY_RETURN: {
      if (mode_ == CommandOverlayMode::kAI) {
        std::string query = base::UTF16ToUTF8(textfield_->GetText());
        if (!query.empty()) {
          browser_->profile()->GetPrefs()->SetString(
              maho::ai_prefs::kPendingInitialQuery, query);
          Dismiss();
          browser_->GetFeatures().side_panel_ui()->Show(
              SidePanelEntryId::kMahoAiPanel);
        }
        return true;
      }
      if (key_event.IsShiftDown()) {
        ActivateFirstNavigation();
      } else {
        ActivateSelected();
      }
      return true;
    }

    case ui::VKEY_DOWN: {
      int count = static_cast<int>(model_->results().size());
      if (count > 0) {
        int next = std::min(model_->selected_index() + 1, count - 1);
        UpdateSelection(next);
      }
      return true;
    }
    case ui::VKEY_UP: {
      int count = static_cast<int>(model_->results().size());
      if (count > 0) {
        int prev = std::max(model_->selected_index() - 1, 0);
        UpdateSelection(prev);
      }
      return true;
    }

    case ui::VKEY_TAB: {
      std::string text = base::UTF16ToUTF8(textfield_->GetText());
      std::string trimmed = text;
      while (!trimmed.empty() && trimmed.back() == ' ')
        trimmed.pop_back();
      while (!trimmed.empty() && trimmed.front() == ' ')
        trimmed.erase(trimmed.begin());

      if (!trimmed.empty() &&
          (mode_ == CommandOverlayMode::kSearch ||
           mode_ == CommandOverlayMode::kNewTab ||
           mode_ == CommandOverlayMode::kCurrentTab)) {
        if (trimmed == "ai" || trimmed == "AI") {
          EnterAIMode();
          return true;
        }

        const auto* engine = model_->MatchSiteSearch("@" + trimmed);
        if (!engine) {
          std::string with_at = trimmed;
          if (!with_at.empty() && with_at[0] == '@')
            with_at = with_at.substr(1);
          engine = model_->MatchSiteSearch("@" + with_at);
        }
        if (engine) {
          EnterSiteSearchMode(engine->prefix, engine->name);
          return true;
        }
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
    const bool is_no_results_state = !model_->query().empty();

    if (model_->query().empty()) {
      auto recent = model_->GetRecentSearches();
      if (!recent.empty()) {
        model_->set_results(std::move(recent));
        model_->set_selected_index(0);
        const auto& stored = model_->results();
        for (size_t i = 0; i < stored.size(); ++i) {
          auto* row = results_container_->AddChildView(
              std::make_unique<MahoCommandResultRowView>(
                  stored[i], static_cast<int>(i),
                  base::BindRepeating(&MahoCommandOverlayView::UpdateSelection,
                                      weak_factory_.GetWeakPtr()),
                  base::BindRepeating(
                      &MahoCommandOverlayView::ActivateResultAtIndex,
                      weak_factory_.GetWeakPtr())));
          if (static_cast<int>(i) == 0) {
            row->SetSelected(true);
          }
        }
        if (divider_)
          divider_->SetVisible(true);
        InvalidateLayout();
        ResizeWidgetToContents();
        return;
      }
      hint_label_ = results_container_->AddChildView(
          std::make_unique<views::Label>(
              u"Type to search tabs, history, and URLs",
              views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
    } else {
      std::u16string no_results_text =
          u"No results for \u201C" +
          base::UTF8ToUTF16(model_->query()) + u"\u201D";
      hint_label_ = results_container_->AddChildView(
          std::make_unique<views::Label>(
              no_results_text, views::style::CONTEXT_LABEL,
              views::style::STYLE_BODY_5));
    }
    hint_label_->SetEnabledColor(kMahoColorCommandBarEmptyText);
    hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    hint_label_->SetAutoColorReadabilityEnabled(false);
    hint_label_->SetMultiLine(true);
    hint_label_->SetBorder(
        views::CreateEmptyBorder(EmptyStateInsets(is_no_results_state)));
    if (divider_)
      divider_->SetVisible(false);
    InvalidateLayout();
    ResizeWidgetToContents();
    return;
  }

  if (divider_)
    divider_->SetVisible(true);

  for (size_t i = 0; i < results.size(); ++i) {
    auto* row = results_container_->AddChildView(
        std::make_unique<MahoCommandResultRowView>(
            results[i], static_cast<int>(i),
            base::BindRepeating(&MahoCommandOverlayView::UpdateSelection,
                                weak_factory_.GetWeakPtr()),
            base::BindRepeating(&MahoCommandOverlayView::ActivateResultAtIndex,
                                weak_factory_.GetWeakPtr())));
    if (static_cast<int>(i) == model_->selected_index()) {
      row->SetSelected(true);
    }
  }
  InvalidateLayout();
  ResizeWidgetToContents();
}

void MahoCommandOverlayView::UpdateSelection(int new_index) {
  int old_index = model_->selected_index();
  if (new_index == old_index) {
    return;
  }
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

void MahoCommandOverlayView::ActivateResultAtIndex(int index) {
  UpdateSelection(index);
  ActivateSelected();
}

void MahoCommandOverlayView::ActivateSelected() {
  DCHECK(browser_);
  const auto& results = model_->results();
  int index = model_->selected_index();

  if (index >= 0 && index < static_cast<int>(results.size())) {
    CommandSuggestion suggestion = results[index];
    if (!model_->query().empty() && !browser_->profile()->IsOffTheRecord()) {
      model_->SaveRecentSearch(model_->query());
    }
    Dismiss();
    OpenCommandSuggestion(browser_, suggestion, DispositionForMode());
    return;
  }

  NavigateTypedText();
}

void MahoCommandOverlayView::ActivateFirstNavigation() {
  const auto& results = model_->results();
  for (const auto& s : results) {
    if (s.type == CommandSuggestionType::kNavigation) {
      if (!model_->query().empty() && !browser_->profile()->IsOffTheRecord()) {
        model_->SaveRecentSearch(model_->query());
      }
      Dismiss();
      OpenCommandSuggestion(browser_, s, DispositionForMode());
      return;
    }
  }
  ActivateSelected();
}

void MahoCommandOverlayView::NavigateTypedText() {
  DCHECK(browser_);
  std::string text = base::UTF16ToUTF8(textfield_->GetText());
  if (text.empty()) {
    return;
  }

  // Try the raw input as a URL first.
  GURL gurl(text);

  // If that didn't parse into a navigable scheme, try adding https:// — this
  // handles bare domains like "example.com" and "localhost:3000".
  if (!gurl.is_valid() || !gurl.SchemeIsHTTPOrHTTPS()) {
    GURL with_scheme("https://" + text);
    // Accept if the resulting host looks like a real domain (contains a dot)
    // or is localhost.
    if (with_scheme.is_valid() && with_scheme.SchemeIsHTTPOrHTTPS() &&
        (with_scheme.host().find('.') != std::string::npos ||
         with_scheme.host() == "localhost")) {
      gurl = with_scheme;
    }
  }

  // Allow chrome:// URLs through as-is.
  if (!gurl.is_valid() || (!gurl.SchemeIsHTTPOrHTTPS() && !gurl.SchemeIs("chrome"))) {
    // Fall back to the configured default search engine.
    MahoCore* core = maho::GetCore();
    if (!core) {
      return;
    }

    char* search_url = maho_core_get_search_url(
        core, text.c_str());
    if (!search_url) {
      return;
    }

    gurl = GURL(search_url);
    maho_string_free(search_url);
  }

  if (!model_->query().empty() && !browser_->profile()->IsOffTheRecord()) {
    model_->SaveRecentSearch(model_->query());
  }
  Dismiss();

  if (mode_ == CommandOverlayMode::kNewTab) {
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    const std::string space_id =
        bridge ? bridge->GetActiveSpaceId() : std::string();
    if (!space_id.empty()) {
      maho::DispatchShellEvent("create_tab", {
          {"space_id", space_id},
          {"url", gurl.spec()}});
    }
  }

  NavigateParams params(browser_, gurl, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = DispositionForMode();
  Navigate(&params);
}

WindowOpenDisposition MahoCommandOverlayView::DispositionForMode() const {
  switch (mode_) {
    case CommandOverlayMode::kNewTab:
      return WindowOpenDisposition::NEW_FOREGROUND_TAB;
    case CommandOverlayMode::kCurrentTab:
      return WindowOpenDisposition::CURRENT_TAB;
    default:
      return WindowOpenDisposition::CURRENT_TAB;
  }
}

void MahoCommandOverlayView::Dismiss() {
  if (dismiss_callback_)
    std::move(dismiss_callback_).Run();
}

void MahoCommandOverlayView::ResizeWidgetToContents() {
  gfx::Size preferred = GetPreferredSize();
  if (resize_callback_)
    resize_callback_.Run(preferred.height());
}

void MahoCommandOverlayView::EnterSiteSearchMode(
    const std::string& prefix,
    const std::string& engine_name) {
  mode_ = CommandOverlayMode::kSiteSearch;
  site_search_prefix_ = prefix;
  site_search_engine_name_ = engine_name;

  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();

  ApplyPlaceholderForMode();
  model_->Cancel();
  UpdateResultViews({});
}

void MahoCommandOverlayView::ExitSiteSearchMode() {
  mode_ = CommandOverlayMode::kSearch;
  site_search_prefix_.clear();
  site_search_engine_name_.clear();

  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();

  ApplyPlaceholderForMode();
  model_->Cancel();
  UpdateResultViews({});
}

void MahoCommandOverlayView::EnterAIMode() {
  mode_ = CommandOverlayMode::kAI;
  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();

  ApplyPlaceholderForMode();
  model_->Cancel();

  results_container_->RemoveAllChildViews();
  hint_label_ = nullptr;
  if (divider_)
    divider_->SetVisible(false);
  InvalidateLayout();
  ResizeWidgetToContents();
}

void MahoCommandOverlayView::ExitAIMode() {
  mode_ = CommandOverlayMode::kSearch;
  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();

  ApplyPlaceholderForMode();
  UpdateResultViews({});
}

void MahoCommandOverlayView::ApplyPlaceholderForMode() {
  switch (mode_) {
    case CommandOverlayMode::kSearch:
      textfield_->SetPlaceholderText(u"Search or Enter URL…");
      break;
    case CommandOverlayMode::kCurrentTab:
      textfield_->SetPlaceholderText(u"Search or Enter URL…");
      break;
    case CommandOverlayMode::kNewTab:
      textfield_->SetPlaceholderText(u"Search in new tab…");
      break;
    case CommandOverlayMode::kSiteSearch:
      textfield_->SetPlaceholderText(u"Search…");
      break;
    case CommandOverlayMode::kAI:
      textfield_->SetPlaceholderText(u"Ask anything…");
      break;
  }
}

void MahoCommandOverlayView::UpdateModeChip() {
  if (!mode_chip_ || !mode_chip_dot_ || !mode_label_) {
    return;
  }

  const auto* cp = GetColorProvider();
  if (!cp) {
    return;
  }

  const bool show_chip = mode_ == CommandOverlayMode::kSiteSearch ||
                         mode_ == CommandOverlayMode::kAI;
  mode_chip_->SetVisible(show_chip);
  if (trailing_gutter_) {
    trailing_gutter_->SetVisible(!show_chip);
  }
  if (!show_chip) {
    return;
  }

  const bool is_ai_mode = mode_ == CommandOverlayMode::kAI;
  const std::u16string label_text =
      is_ai_mode
          ? std::u16string(u"AI Mode")
          : base::UTF8ToUTF16(site_search_engine_name_.empty()
                                  ? "Site Search"
                                  : site_search_engine_name_);
  mode_label_->SetText(label_text);

  const SkColor accent_color = cp->GetColor(
      is_ai_mode ? kMahoColorAccentBlueBright : kMahoColorAccentBlue);
  const SkColor chip_fill = SkColorSetA(accent_color, 0x14);
  const SkColor chip_border = SkColorSetA(accent_color, 0x2A);
  const SkColor chip_dot = SkColorSetA(accent_color, 0xE6);

  mode_chip_->SetBackground(views::CreateRoundedRectBackground(
      chip_fill, kModeChipCornerRadiusDp));
  mode_chip_->SetBorder(views::CreateRoundedRectBorder(
      1, kModeChipCornerRadiusDp, chip_border));
  mode_chip_dot_->SetBackground(views::CreateRoundedRectBackground(
      chip_dot, kModeChipDotSizeDp / 2));
  mode_label_->SetEnabledColor(accent_color);
}

}  // namespace maho
