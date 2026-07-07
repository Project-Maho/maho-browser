// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "maho/browser/maho_features.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "ui/compositor/layer.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_command_model.h"
#include "maho/browser/ui/views/command/maho_command_result_row_view.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"
#include "maho/browser/ui/views/command/maho_command_search_engine_picker_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/animation/ink_drop.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "base/strings/escape.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/web_contents.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/background.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {

namespace {

constexpr int kPanelCornerRadiusDp = 24;
constexpr int kPanelBorderWidthDp = 1;
constexpr int kPanelPaddingTopDp = 10;
constexpr int kPanelPaddingSideDp = 8;
constexpr int kFieldShellSpacingDp = 6;
constexpr int kTopbarHorizontalInsetDp = 18;
constexpr int kPanelBottomInsetDp = 8;
constexpr int kSearchRowCornerRadiusDp = 14;
constexpr int kSearchRowPaddingHDp = 0;
constexpr int kTrailingAccessoryTrailingInsetDp = 6;
// constexpr int kSparkToInputGapDp = 8;
constexpr int kPanelSectionSpacingDp = 0;
constexpr int kResultRowSpacingDp = 2;
constexpr int kDividerTopGapDp = 4;
constexpr int kDividerBottomGapDp = 2;
constexpr int kDividerHorizontalInsetDp = 10;
constexpr int kMaxResultsHeightDp = 420;
constexpr int kTrailingAccessorySizeDp = 18;
constexpr int kTrailingAccessoryCornerRadiusDp = 9;
constexpr char16_t kTrailingAccessoryInfoGlyph[] = u"i";
constexpr int kModeLabelSpacingDp = 6;
constexpr int kModeChipCornerRadiusDp = 8;
constexpr int kModeChipHorizontalInsetDp = 8;
constexpr int kModeChipVerticalInsetDp = 2;
constexpr int kModeChipDotSizeDp = 5;
constexpr int kModeChipDotLabelSpacingDp = 5;
constexpr int kSearchTextfieldHeightDp = 24;
constexpr int kEmptyStateTopInsetDp = 8;
constexpr int kEmptyStateBottomInsetDp = 4;
constexpr int kNoResultsTopInsetDp = 6;
constexpr int kNoResultsBottomInsetDp = 3;
constexpr int kEmptyStateHorizontalInsetDp = 40;

gfx::Insets EmptyStateInsets(bool compact) {
  return gfx::Insets::TLBR(
      compact ? kNoResultsTopInsetDp : kEmptyStateTopInsetDp,
      kEmptyStateHorizontalInsetDp,
      compact ? kNoResultsBottomInsetDp : kEmptyStateBottomInsetDp,
      kEmptyStateHorizontalInsetDp);
}


class CommandPaletteTextfield : public views::Textfield {
 public:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    return event.key_code() == ui::VKEY_TAB;
  }

  METADATA_HEADER(CommandPaletteTextfield, views::Textfield)
};

BEGIN_METADATA(CommandPaletteTextfield)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoCommandOverlayView)
END_METADATA

MahoCommandOverlayView::MahoCommandOverlayView(
    ForTestingTag,
    CommandOverlayMode mode,
    const std::string& initial_text,
    bool select_initial_text,
    DismissCallback dismiss_callback,
    ResizeCallback resize_callback)
    : browser_(nullptr),
      mode_(mode),
      model_(nullptr),
      dismiss_callback_(std::move(dismiss_callback)),
      resize_callback_(std::move(resize_callback)),
      origin_mode_(mode),
      select_initial_text_(select_initial_text) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(u"Command Palette");
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kPanelPaddingTopDp, kPanelPaddingSideDp,
                          kPanelBottomInsetDp,
                          kPanelPaddingSideDp),
      kPanelSectionSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  field_shell_ = AddChildView(std::make_unique<views::View>());
  auto* field_shell_layout = field_shell_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets::VH(0, 0),
                                         kFieldShellSpacingDp));
  field_shell_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  search_row_ = field_shell_->AddChildView(std::make_unique<views::View>());
  search_row_->SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCommandBarInputFill, kSearchRowCornerRadiusDp));
  search_row_->SetBorder(views::CreateRoundedRectBorder(
      kPanelBorderWidthDp, kSearchRowCornerRadiusDp,
      kMahoColorCommandBarInputBorder));

  auto input_background = std::make_unique<views::View>();
  auto* search_layout =
      input_background->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kSearchRowPaddingHDp), kModeLabelSpacingDp));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  input_background->SetBackground(nullptr);
  input_background->SetBorder(nullptr);

  textfield_ = input_background->AddChildView(
      std::make_unique<CommandPaletteTextfield>());
  textfield_->set_controller(this);
  textfield_->SetBackgroundEnabled(false);
  textfield_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  textfield_->SetPaintToLayer();
  textfield_->layer()->SetFillsBoundsOpaquely(false);

  mode_chip_ = input_background->AddChildView(std::make_unique<views::View>());
  auto* mode_chip_layout =
      mode_chip_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kModeChipVerticalInsetDp, kModeChipHorizontalInsetDp),
          kModeChipDotLabelSpacingDp));
  mode_chip_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  mode_chip_dot_ = mode_chip_->AddChildView(std::make_unique<views::View>());
  mode_chip_dot_->SetPreferredSize(
      gfx::Size(kModeChipDotSizeDp, kModeChipDotSizeDp));

  mode_label_ = mode_chip_->AddChildView(std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5_MEDIUM));
  mode_label_->SetAutoColorReadabilityEnabled(false);

  auto elements = MahoCommandResultRowView::BuildRowLayout(
      search_row_, std::move(input_background),
      MahoCommandResultRowView::PresentationStyle::kDefault);
  search_icon_lane_ = elements.glyph_container;
  input_background_ = elements.text_container->children().front().get();

  UpdateLayoutForMode();
  UpdateTextfieldPresentation();
  search_layout->SetFlexForView(textfield_, 1);

  auto button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoCommandOverlayView::OnSearchEngineBadgePressed,
                          base::Unretained(this)),
      std::u16string());
  button->SetStyle(ui::ButtonStyle::kText);
  button->SetCornerRadius(kTrailingAccessoryCornerRadiusDp);
  button->SetMinSize(gfx::Size(kTrailingAccessorySizeDp, kTrailingAccessorySizeDp));
  button->SetMaxSize(gfx::Size(kTrailingAccessorySizeDp, kTrailingAccessorySizeDp));
  button->SetTooltipText(u"Change search engine");
  views::InkDrop::Get(button.get())->SetMode(views::InkDropHost::InkDropMode::OFF);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);

  trailing_accessory_ = elements.content_view->AddChildView(std::move(button));
  trailing_accessory_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, 0, 0, kTrailingAccessoryTrailingInsetDp)));
  trailing_accessory_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  auto* trailing_layout =
      static_cast<views::BoxLayout*>(trailing_accessory_->GetLayoutManager());
  trailing_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  trailing_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  trailing_accessory_label_ = trailing_accessory_->AddChildView(
      std::make_unique<views::Label>(kTrailingAccessoryInfoGlyph,
                                     views::style::CONTEXT_LABEL,
                                     views::style::STYLE_BODY_5_MEDIUM));
  trailing_accessory_label_->SetAutoColorReadabilityEnabled(false);
  trailing_accessory_->SetVisible(false);
  UpdateTrailingAccessoryPresentation();
  UpdateSearchRowChrome();

  UpdateModeChipVisibility();

  divider_ = AddChildView(std::make_unique<views::Separator>());
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
  results_container_->GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  results_container_->GetViewAccessibility().SetName(u"Search Results");
  textfield_->GetViewAccessibility().SetRole(
      ax::mojom::Role::kTextFieldWithComboBox);
  textfield_->GetViewAccessibility().SetName(u"Search or Enter URL");
  textfield_->GetViewAccessibility().SetIsCollapsed();
  textfield_->GetViewAccessibility().SetControlIds(
      {results_container_->GetViewAccessibility().GetUniqueId().value()});
  auto* results_layout = results_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kResultRowSpacingDp));
  results_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  hint_label_ = results_container_->AddChildView(
      std::make_unique<views::Label>(
          u"Type to search tabs, history, and URLs",
          views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  hint_label_->SetAutoColorReadabilityEnabled(false);
  hint_label_->SetMultiLine(true);
  hint_label_->SetBorder(views::CreateEmptyBorder(EmptyStateInsets(false)));

  ApplyPlaceholderForMode();

  if (!initial_text.empty() && mode_ != CommandOverlayMode::kCurrentTab) {
    textfield_->SetText(base::UTF8ToUTF16(initial_text));
    if (select_initial_text_) {
      textfield_->SelectAll(false);
    }
  }

  UpdateSearchLeadingAccessory();

  if (!initial_text.empty() && mode_ == CommandOverlayMode::kCurrentTab) {
    textfield_->SetText(base::UTF8ToUTF16(initial_text));
    if (select_initial_text_) {
      textfield_->SelectAll(true);
    }
  }
}

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
      origin_mode_(mode),
      select_initial_text_(select_initial_text) {
  DCHECK(browser_);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(u"Command Palette");
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kPanelPaddingTopDp, kPanelPaddingSideDp,
                         kPanelBottomInsetDp,
                         kPanelPaddingSideDp),
      kPanelSectionSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  bool use_vibrancy = false;
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
  use_vibrancy = base::FeatureList::IsEnabled(features::kMahoCommandPaletteVibrancy);
#endif

  if (!use_vibrancy) {
    SetBackground(views::CreateRoundedRectBackground(
        kMahoColorCommandBarBackground, kPanelCornerRadiusDp));
  } else {
    SetBackground(nullptr);
  }
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

  search_row_ = field_shell_->AddChildView(std::make_unique<views::View>());
  search_row_->SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCommandBarInputFill, kSearchRowCornerRadiusDp));
  search_row_->SetBorder(views::CreateRoundedRectBorder(
      kPanelBorderWidthDp, kSearchRowCornerRadiusDp,
      kMahoColorCommandBarInputBorder));

  auto input_background = std::make_unique<views::View>();
  auto* search_layout =
      input_background->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kSearchRowPaddingHDp), kModeLabelSpacingDp));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  input_background->SetBackground(nullptr);
  input_background->SetBorder(nullptr);

  textfield_ = input_background->AddChildView(
      std::make_unique<CommandPaletteTextfield>());
  textfield_->set_controller(this);
  textfield_->SetBackgroundEnabled(false);
  textfield_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  textfield_->SetPaintToLayer();
  textfield_->layer()->SetFillsBoundsOpaquely(false);

  mode_chip_ = input_background->AddChildView(std::make_unique<views::View>());
  auto* mode_chip_layout =
      mode_chip_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(kModeChipVerticalInsetDp, kModeChipHorizontalInsetDp),
          kModeChipDotLabelSpacingDp));
  mode_chip_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  mode_chip_dot_ = mode_chip_->AddChildView(std::make_unique<views::View>());
  mode_chip_dot_->SetPreferredSize(
      gfx::Size(kModeChipDotSizeDp, kModeChipDotSizeDp));

  mode_label_ = mode_chip_->AddChildView(std::make_unique<views::Label>(
      u"", views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5_MEDIUM));
  mode_label_->SetAutoColorReadabilityEnabled(false);

  auto elements = MahoCommandResultRowView::BuildRowLayout(
      search_row_, std::move(input_background),
      MahoCommandResultRowView::PresentationStyle::kDefault);
  search_icon_lane_ = elements.glyph_container;
  input_background_ = elements.text_container->children().front().get();

  UpdateLayoutForMode();
  UpdateTextfieldPresentation();
  search_layout->SetFlexForView(textfield_, 1);

  auto button = std::make_unique<views::MdTextButton>(
      base::BindRepeating(&MahoCommandOverlayView::OnSearchEngineBadgePressed,
                          base::Unretained(this)),
      std::u16string());
  button->SetStyle(ui::ButtonStyle::kText);
  button->SetCornerRadius(kTrailingAccessoryCornerRadiusDp);
  button->SetMinSize(gfx::Size(kTrailingAccessorySizeDp, kTrailingAccessorySizeDp));
  button->SetMaxSize(gfx::Size(kTrailingAccessorySizeDp, kTrailingAccessorySizeDp));
  button->SetTooltipText(u"Change search engine");
  views::InkDrop::Get(button.get())->SetMode(views::InkDropHost::InkDropMode::OFF);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);

  trailing_accessory_ = elements.content_view->AddChildView(std::move(button));
  trailing_accessory_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(0, 0, 0, kTrailingAccessoryTrailingInsetDp)));
  trailing_accessory_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  auto* trailing_layout =
      static_cast<views::BoxLayout*>(trailing_accessory_->GetLayoutManager());
  trailing_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  trailing_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  trailing_accessory_label_ = trailing_accessory_->AddChildView(
      std::make_unique<views::Label>(kTrailingAccessoryInfoGlyph,
                                     views::style::CONTEXT_LABEL,
                                     views::style::STYLE_BODY_5_MEDIUM));
  trailing_accessory_label_->SetAutoColorReadabilityEnabled(false);
  trailing_accessory_->SetVisible(false);
  UpdateTrailingAccessoryPresentation();
  UpdateSearchRowChrome();


  UpdateModeChipVisibility();

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
  results_container_->GetViewAccessibility().SetRole(
      ax::mojom::Role::kListBox);
  results_container_->GetViewAccessibility().SetName(u"Search Results");
  textfield_->GetViewAccessibility().SetRole(
      ax::mojom::Role::kTextFieldWithComboBox);
  textfield_->GetViewAccessibility().SetName(u"Search or Enter URL");
  textfield_->GetViewAccessibility().SetIsCollapsed();

  textfield_->GetViewAccessibility().SetControlIds(
      {results_container_->GetViewAccessibility().GetUniqueId().value()});

  auto* results_layout = results_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kResultRowSpacingDp));
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

  if (!initial_text.empty() && mode_ != CommandOverlayMode::kCurrentTab) {
    textfield_->SetText(base::UTF8ToUTF16(initial_text));
    if (select_initial_text_) {
      textfield_->SelectAll(false);
    }
  }

  UpdateSearchLeadingAccessory();

  if (!initial_text.empty() && mode_ == CommandOverlayMode::kCurrentTab) {
    textfield_->SetText(base::UTF8ToUTF16(initial_text));
    if (select_initial_text_) {
      textfield_->SelectAll(true);
    }
  }

  if (!initial_text.empty()) {
    initial_url_ = initial_text;
    ContentsChanged(textfield_, std::u16string(textfield_->GetText()));
  }

  ApplyVibrancyBackgroundTransparency(use_vibrancy);
}

MahoCommandOverlayView::~MahoCommandOverlayView() {
  if (model_)
    model_->Cancel();
}

size_t MahoCommandOverlayView::GetRenderedResultCountForTesting() const {
  if (!results_container_) {
    return 0u;
  }

  size_t rendered_result_count = 0u;
  for (const auto& child : results_container_->children()) {
    if (child.get() != hint_label_) {
      ++rendered_result_count;
    }
  }
  return rendered_result_count;
}

// static
int MahoCommandOverlayView::GetPanelCornerRadiusForTesting() {
  return kPanelCornerRadiusDp;
}

// static
int MahoCommandOverlayView::GetSearchRowCornerRadiusForTesting() {
  return kSearchRowCornerRadiusDp;
}

// static
int MahoCommandOverlayView::GetTopbarHorizontalInsetForTesting() {
  return kTopbarHorizontalInsetDp;
}

// static
gfx::Insets MahoCommandOverlayView::GetPanelInsetsForTesting() {
  return gfx::Insets::TLBR(kPanelPaddingTopDp, kPanelPaddingSideDp,
                           kPanelBottomInsetDp, kPanelPaddingSideDp);
}

// static
gfx::Insets MahoCommandOverlayView::GetEmptyStateInsetsForTesting(bool compact) {
  return EmptyStateInsets(compact);
}

// static
int MahoCommandOverlayView::GetPanelSectionSpacingForTesting() {
  return kPanelSectionSpacingDp;
}

// static
int MahoCommandOverlayView::GetResultRowSpacingForTesting() {
  return kResultRowSpacingDp;
}

// static
gfx::Insets MahoCommandOverlayView::GetDividerInsetsForTesting() {
  return gfx::Insets::TLBR(kDividerTopGapDp, kDividerHorizontalInsetDp,
                             kDividerBottomGapDp, kDividerHorizontalInsetDp);
}

bool MahoCommandOverlayView::TextfieldControlsResultsContainerForTesting() const {
  if (!textfield_ || !results_container_) {
    return false;
  }

  ui::AXNodeData tf_data;
  textfield_->GetViewAccessibility().GetAccessibleNodeData(&tf_data);
  if (!tf_data.HasIntListAttribute(ax::mojom::IntListAttribute::kControlsIds)) {
    return false;
  }

  const auto& controls =
      tf_data.GetIntListAttribute(ax::mojom::IntListAttribute::kControlsIds);
  const int32_t listbox_id =
      results_container_->GetViewAccessibility().GetUniqueId().value();
  return std::find(controls.begin(), controls.end(), listbox_id) !=
         controls.end();
}

bool MahoCommandOverlayView::TextfieldIsCollapsedForTesting() const {
  if (!textfield_) {
    return false;
  }
  ui::AXNodeData data;
  textfield_->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.HasState(ax::mojom::State::kCollapsed);
}

bool MahoCommandOverlayView::TextfieldIsExpandedForTesting() const {
  if (!textfield_) {
    return false;
  }
  ui::AXNodeData data;
  textfield_->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.HasState(ax::mojom::State::kExpanded);
}

bool MahoCommandOverlayView::IsDividerVisibleForTesting() const {
  return divider_ && divider_->GetVisible();
}

views::ViewAccessibility* MahoCommandOverlayView::GetActiveDescendantForTesting() const {
  return textfield_ ? textfield_->GetViewAccessibility().GetActiveDescendantView()
                    : nullptr;
}

void MahoCommandOverlayView::UpdateResultViewsForTesting(
    const std::vector<CommandSuggestion>& results) {
  UpdateResultViews(results);
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
  if (field_shell_) {
    field_shell_->SetVisible(true);
  }

  if (search_row_) {
    search_row_->SetPreferredSize(gfx::Size(0, MahoCommandResultRowView::kRowHeightDp));
  }

  if (textfield_) {
    textfield_->SetPreferredSize(gfx::Size(0, kSearchTextfieldHeightDp));
  }

  if (trailing_accessory_) {
    trailing_accessory_->SetVisible(false);
  }

  UpdateTrailingAccessoryPresentation();

  if (auto* layout = static_cast<views::BoxLayout*>(GetLayoutManager())) {
    layout->set_inside_border_insets(gfx::Insets::TLBR(
        kPanelPaddingTopDp, kPanelPaddingSideDp,
        kPanelBottomInsetDp,
        kPanelPaddingSideDp));
  }

  PreferredSizeChanged();
}

void MahoCommandOverlayView::UpdateTextfieldPresentation() {
  if (!textfield_) {
    return;
  }

  gfx::FontList base = views::Textfield::GetDefaultFontList();
  gfx::FontList bumped = base.DeriveWithSizeDelta(2).Derive(
      0, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM);
  textfield_->SetFontList(bumped);
  textfield_->set_placeholder_font_list(bumped);
}

void MahoCommandOverlayView::UpdateSearchLeadingAccessory() {
  if (!search_icon_lane_) {
    return;
  }

  std::optional<ImageData> icon_to_decode;
  bool use_globe_fallback = false;
  if (model_) {
    const std::string current_text = base::UTF16ToUTF8(textfield_->GetText());
    if (!current_text.empty()) {
      for (const auto& result : model_->results()) {
        if (result.execution_payload == current_text && result.icon.has_value()) {
          icon_to_decode = result.icon;
          break;
        }
      }
      if (!icon_to_decode.has_value()) {
        use_globe_fallback = true;
      }
    } else {
      int selected_idx = model_->selected_index();
      if (selected_idx >= 0 && selected_idx < static_cast<int>(model_->results().size())) {
        icon_to_decode = model_->results()[selected_idx].icon;
      }
    }
  }

  if (icon_to_decode.has_value()) {
    const gfx::Image context_icon = MahoCommandResultRowView::DecodeFaviconImage(*icon_to_decode);
    if (!context_icon.IsEmpty()) {
      if (!favicon_image_) {
        auto* glyph_layout = search_icon_lane_->SetLayoutManager(
            std::make_unique<views::BoxLayout>(
                views::BoxLayout::Orientation::kHorizontal));
        glyph_layout->set_main_axis_alignment(
            views::BoxLayout::MainAxisAlignment::kCenter);
        glyph_layout->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kCenter);
        favicon_image_ =
            search_icon_lane_->AddChildView(std::make_unique<views::ImageView>());
        favicon_image_->SetCanProcessEventsWithinSubtree(false);
        favicon_image_->SetPaintToLayer();
        favicon_image_->layer()->SetFillsBoundsOpaquely(false);
      }
      favicon_image_->SetImage(
          ui::ImageModel::FromImageSkia(context_icon.AsImageSkia()));
      const int favicon_size = MahoCommandResultRowView::PreferredFaviconSize(
          icon_to_decode, MahoCommandResultRowView::kGlyphIconSizeDp);
      favicon_image_->SetImageSize(gfx::Size(favicon_size, favicon_size));
      favicon_image_->SetPreferredSize(gfx::Size(favicon_size, favicon_size));
      favicon_image_->SetVisible(true);
      return;
    }
  }

  if (use_globe_fallback) {
    if (!favicon_image_) {
      auto* glyph_layout = search_icon_lane_->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal));
      glyph_layout->set_main_axis_alignment(
          views::BoxLayout::MainAxisAlignment::kCenter);
      glyph_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      favicon_image_ =
          search_icon_lane_->AddChildView(std::make_unique<views::ImageView>());
      favicon_image_->SetCanProcessEventsWithinSubtree(false);
      favicon_image_->SetPaintToLayer();
      favicon_image_->layer()->SetFillsBoundsOpaquely(false);
    }
    const int icon_size = MahoCommandResultRowView::kGlyphIconSizeDp;
    const gfx::ImageSkia globe_bitmap = gfx::CreateVectorIcon(
        vector_icons::kGlobeIcon, icon_size, gfx::kGoogleGrey700);
    favicon_image_->SetImage(ui::ImageModel::FromImageSkia(globe_bitmap));
    favicon_image_->SetImageSize(gfx::Size(icon_size, icon_size));
    favicon_image_->SetPreferredSize(gfx::Size(icon_size, icon_size));
    favicon_image_->SetVisible(true);
    return;
  }

  if (favicon_image_) {
    favicon_image_->SetVisible(false);
  }
}

void MahoCommandOverlayView::UpdateTrailingAccessoryPresentation() {
  if (!trailing_accessory_ || !trailing_accessory_label_) {
    return;
  }

  trailing_accessory_label_->SetText(
      std::u16string(kTrailingAccessoryInfoGlyph));
  trailing_accessory_->SetPreferredSize(
      gfx::Size(kTrailingAccessorySizeDp, kTrailingAccessorySizeDp));
  trailing_accessory_->SetBackground(views::CreateRoundedRectBackground(
      kMahoColorCommandBarShortcutBadge, kTrailingAccessoryCornerRadiusDp));
  trailing_accessory_->SetBorder(views::CreateRoundedRectBorder(
      1, kTrailingAccessoryCornerRadiusDp,
      kMahoColorCommandBarKeycapBorder));
  trailing_accessory_label_->SetEnabledColor(kMahoColorTertiaryText);
}

void MahoCommandOverlayView::UpdateSearchRowChrome() {
  if (!search_row_) {
    return;
  }
  search_row_->SetBackground(nullptr);
  search_row_->SetBorder(nullptr);
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
  UpdateSearchRowChrome();
  UpdateTrailingAccessoryPresentation();
  UpdateTextfieldPresentation();
  UpdateModeChip();
  UpdateSearchLeadingAccessory();
}

void MahoCommandOverlayView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  if (suppress_search_) {
    return;
  }

  if ((mode_ == CommandOverlayMode::kSearch ||
       mode_ == CommandOverlayMode::kCurrentTab ||
       mode_ == CommandOverlayMode::kNewTab) &&
      new_contents == u">") {
    EnterCommandsOnlyMode();
    return;
  }

  TriggerSearchForCurrentText();
}

void MahoCommandOverlayView::OnAfterUserAction(views::Textfield* sender) {}

void MahoCommandOverlayView::TriggerSearchForCurrentText() {
  if (mode_ == CommandOverlayMode::kAI)
    return;

  if (!model_)
    return;

  std::string query = base::UTF16ToUTF8(textfield_->GetText());

  if (mode_ == CommandOverlayMode::kCurrentTab && !initial_url_.empty() &&
      query == initial_url_) {
    query.clear();
  }
  LOG(WARNING) << "[maho-cmdl-diag] TriggerSearch mode=" << static_cast<int>(mode_)
               << " initial_url_.empty=" << initial_url_.empty()
               << " query='" << query << "'";

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
      if (mode_ == CommandOverlayMode::kCommandsOnly) {
        ExitCommandsOnlyMode();
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
      int count = model_ ? static_cast<int>(model_->results().size()) : 0;
      if (count > 0) {
        int next = std::min(model_->selected_index() + 1, count - 1);
        UpdateSelection(next);
      }
      return true;
    }
    case ui::VKEY_UP: {
      int count = model_ ? static_cast<int>(model_->results().size()) : 0;
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

        const auto* engine = model_ ? model_->MatchSiteSearch("@" + trimmed) : nullptr;
        if (!engine && model_) {
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

    case ui::VKEY_BACK: {
      if (mode_ == CommandOverlayMode::kCommandsOnly &&
          textfield_->GetText().empty()) {
        ExitCommandsOnlyMode();
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

void MahoCommandOverlayView::OnSearchResults(
    std::vector<CommandSuggestion> results) {
  LOG(WARNING) << "[maho-cmdl-diag] OnSearchResults count=" << results.size()
               << " mode=" << static_cast<int>(mode_)
               << " initial_url_='" << initial_url_ << "'";
  if (mode_ == CommandOverlayMode::kCurrentTab && !initial_url_.empty()) {
    for (size_t i = 0; i < results.size(); ++i) {
      if (results[i].type == CommandSuggestionType::kTab &&
          results[i].execution_payload == initial_url_) {
        if (i > 0) {
          std::rotate(results.begin(), results.begin() + i,
                      results.begin() + i + 1);
        }
        break;
      }
    }
  }
  // Arc-parity: sync model with rotated results and always select first row.
  if (model_) {
    model_->set_results(results);
    model_->set_selected_index(results.empty() ? -1 : 0);
  }
  UpdateResultViews(results);
  UpdateSearchLeadingAccessory();
}

namespace {

void DisableSubpixelRenderingOnLabelsRecursive(views::View* view) {
  if (!view) return;
  if (views::Label* label = views::AsViewClass<views::Label>(view)) {
    label->SetSkipSubpixelRenderingOpacityCheck(true);
  }
  for (views::View* child : view->children()) {
    DisableSubpixelRenderingOnLabelsRecursive(child);
  }
}

}

void MahoCommandOverlayView::UpdateResultViews(
    const std::vector<CommandSuggestion>& results) {
  results_container_->RemoveAllChildViews();
  hint_label_ = nullptr;

  if (results.empty()) {
    const bool is_no_results_state = model_ && !model_->query().empty();

    if (!model_ || model_->query().empty()) {
      auto recent = model_ ? model_->GetRecentSearches()
                           : std::vector<CommandSuggestion>{};
      if (!recent.empty()) {
        model_->set_results(std::move(recent));
        model_->set_selected_index(0);
        const auto& stored = model_->results();
        const int stored_size = static_cast<int>(stored.size());
        for (size_t i = 0; i < stored.size(); ++i) {
          auto* row = results_container_->AddChildView(
              std::make_unique<MahoCommandResultRowView>(
                  stored[i], static_cast<int>(i), stored_size,
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
        textfield_->GetViewAccessibility().SetIsExpanded();
        {
          auto children = results_container_->children();
          if (!children.empty()) {
            textfield_->GetViewAccessibility().SetActiveDescendant(
                *children.front());
          }
        }
        results_container_->GetViewAccessibility().NotifyEvent(
            ax::mojom::Event::kSelectedChildrenChanged, true);
        GetViewAccessibility().NotifyEvent(ax::mojom::Event::kValueChanged, true);
        return;
      }
      hint_label_ = results_container_->AddChildView(
          std::make_unique<views::Label>(
              u"Type to search tabs, history, and URLs",
              views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
    } else {
      std::u16string no_results_text =
          u"No results for \u201C" +
          base::UTF8ToUTF16(model_ ? model_->query() : std::string()) +
          u"\u201D";
      hint_label_ = results_container_->AddChildView(
          std::make_unique<views::Label>(
              no_results_text, views::style::CONTEXT_LABEL,
              views::style::STYLE_BODY_5));
    }
    hint_label_->SetEnabledColor(kMahoColorCommandBarEmptyText);
    hint_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    hint_label_->SetAutoColorReadabilityEnabled(false);
    hint_label_->SetMultiLine(true);
    hint_label_->SetSkipSubpixelRenderingOpacityCheck(true);
    hint_label_->SetBorder(
        views::CreateEmptyBorder(EmptyStateInsets(is_no_results_state)));
    if (divider_)
      divider_->SetVisible(false);
    InvalidateLayout();
    ResizeWidgetToContents();
    textfield_->GetViewAccessibility().SetIsCollapsed();
    textfield_->GetViewAccessibility().ClearActiveDescendant();
    GetViewAccessibility().NotifyEvent(ax::mojom::Event::kValueChanged, true);
    return;
  }

  if (divider_)
    divider_->SetVisible(true);

  const int results_size = static_cast<int>(results.size());
  for (size_t i = 0; i < results.size(); ++i) {
    auto* row = results_container_->AddChildView(
        std::make_unique<MahoCommandResultRowView>(
            results[i], static_cast<int>(i), results_size,
            base::BindRepeating(&MahoCommandOverlayView::UpdateSelection,
                                weak_factory_.GetWeakPtr()),
            base::BindRepeating(&MahoCommandOverlayView::ActivateResultAtIndex,
                                weak_factory_.GetWeakPtr())));
    if (static_cast<int>(i) == (model_ ? model_->selected_index() : 0)) {
      row->SetSelected(true);
    }
  }
  InvalidateLayout();
  ResizeWidgetToContents();
  textfield_->GetViewAccessibility().SetIsExpanded();
  {
    int sel = model_ ? model_->selected_index() : 0;
    auto children = results_container_->children();
    if (sel >= 0 && sel < static_cast<int>(children.size())) {
      textfield_->GetViewAccessibility().SetActiveDescendant(
          *children[sel]);
    }
  }
  results_container_->GetViewAccessibility().NotifyEvent(
      ax::mojom::Event::kSelectedChildrenChanged, true);
  GetViewAccessibility().NotifyEvent(ax::mojom::Event::kValueChanged, true);
  DisableSubpixelRenderingOnLabelsRecursive(this);
}

void MahoCommandOverlayView::UpdateSelection(int new_index) {
  if (!model_)
    return;
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
    textfield_->GetViewAccessibility().SetActiveDescendant(*row);
    if (new_index < static_cast<int>(model_->results().size())) {
      const auto& sel = model_->results()[new_index];
      const std::string& payload = sel.execution_payload;
      if (!payload.empty()) {
        suppress_search_ = true;
        const std::u16string new_text = base::UTF8ToUTF16(payload);
        const gfx::Range selection(new_text.length());
        textfield_->SetTextWithoutCaretBoundsChangeNotification(
            new_text, selection.end());
        constexpr size_t kPadTrailing = 30;
        constexpr size_t kPadLeading = 10;
        textfield_->Scroll({
            0,
            std::min(selection.end() + kPadTrailing, new_text.size()),
            selection.end() - std::min(kPadLeading, selection.end())
        });
        textfield_->SelectAll(true);
        suppress_search_ = false;
      }
    }
    UpdateSearchLeadingAccessory();
  }
  results_container_->GetViewAccessibility().NotifyEvent(
      ax::mojom::Event::kSelectedChildrenChanged, true);
}

void MahoCommandOverlayView::UpdateSelectionA11yForTesting(int old_index,
                                                            int new_index) {
  auto children = results_container_->children();
  if (old_index >= 0 && old_index < static_cast<int>(children.size())) {
    static_cast<MahoCommandResultRowView*>(children[old_index])
        ->SetSelected(false);
  }
  if (new_index >= 0 && new_index < static_cast<int>(children.size())) {
    auto* row = static_cast<MahoCommandResultRowView*>(children[new_index]);
    row->SetSelected(true);
    textfield_->GetViewAccessibility().SetActiveDescendant(*row);
  }
  results_container_->GetViewAccessibility().NotifyEvent(
      ax::mojom::Event::kSelectedChildrenChanged, true);
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
    VLOG(1) << "[maho-command-trace] ActivateSelected: index=" << index
            << " type=" << static_cast<int>(suggestion.type)
            << " payload=" << suggestion.execution_payload;
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
      VLOG(1) << "[maho-command-trace] ActivateFirstNavigation:"
              << " payload=" << s.execution_payload;
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

  VLOG(1) << "[maho-command-trace] NavigateTypedText:"
          << " url=" << gurl.spec()
          << " text=" << text;

  // Use DispositionForMode() as the single authoritative source for the
  // intended open disposition.  This already accounts for sub-modes
  // (kSiteSearch, kAI) that inherit their origin disposition via origin_mode_,
  // so a new-tab palette that transitioned into site-search will still produce
  // NEW_FOREGROUND_TAB here without needing a separate mode_ == kNewTab check.
  if (DispositionForMode() == WindowOpenDisposition::NEW_FOREGROUND_TAB) {
    NavigateToNewForegroundTab(browser_, gurl, ui::PAGE_TRANSITION_GENERATED);
    return;
  }

  NavigateParams params(browser_, gurl, ui::PAGE_TRANSITION_GENERATED);
  params.disposition = DispositionForMode();
  Navigate(&params);
}

WindowOpenDisposition MahoCommandOverlayView::DispositionForMode() const {
  // For transient sub-modes (kSiteSearch, kAI, kCommandsOnly) the user is still operating in
  // the context of the mode that was active before entering the sub-mode.
  // Use origin_mode_ so that a new-tab open which transitioned to site-search
  // still opens the result in a new foreground tab.
  const CommandOverlayMode effective_mode =
      (mode_ == CommandOverlayMode::kSiteSearch ||
       mode_ == CommandOverlayMode::kAI ||
       mode_ == CommandOverlayMode::kCommandsOnly)
          ? origin_mode_
          : mode_;
  switch (effective_mode) {
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
  if (mode_ != CommandOverlayMode::kSiteSearch) {
    origin_mode_ = mode_;
  }
  mode_ = CommandOverlayMode::kSiteSearch;
  site_search_prefix_ = prefix;
  site_search_engine_name_ = engine_name;

  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();
  UpdateSearchLeadingAccessory();

  ApplyPlaceholderForMode();
  if (model_) model_->Cancel();
  UpdateResultViews({});
}

void MahoCommandOverlayView::ExitSiteSearchMode() {
  mode_ = origin_mode_;
  site_search_prefix_.clear();
  site_search_engine_name_.clear();

  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();
  UpdateSearchLeadingAccessory();

  ApplyPlaceholderForMode();
  if (model_) model_->Cancel();
  UpdateResultViews({});
}

void MahoCommandOverlayView::EnterAIMode() {
  if (mode_ != CommandOverlayMode::kAI) {
    origin_mode_ = mode_;
  }
  mode_ = CommandOverlayMode::kAI;
  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();
  UpdateSearchLeadingAccessory();

  ApplyPlaceholderForMode();
  if (model_) model_->Cancel();

  results_container_->RemoveAllChildViews();
  hint_label_ = nullptr;
  if (divider_)
    divider_->SetVisible(false);
  InvalidateLayout();
  ResizeWidgetToContents();
}

void MahoCommandOverlayView::ExitAIMode() {
  mode_ = origin_mode_;
  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();
  UpdateSearchLeadingAccessory();

  ApplyPlaceholderForMode();
  UpdateResultViews({});
}

void MahoCommandOverlayView::EnterCommandsOnlyMode() {
  if (mode_ != CommandOverlayMode::kCommandsOnly) {
    origin_mode_ = mode_;
  }
  mode_ = CommandOverlayMode::kCommandsOnly;
  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();
  UpdateSearchLeadingAccessory();

  ApplyPlaceholderForMode();
  if (model_) model_->Cancel();
  UpdateResultViews({});
}

void MahoCommandOverlayView::ExitCommandsOnlyMode() {
  mode_ = origin_mode_;
  textfield_->SetText(std::u16string());
  UpdateLayoutForMode();
  UpdateModeChip();
  UpdateSearchLeadingAccessory();

  ApplyPlaceholderForMode();
  if (model_) model_->Cancel();
  UpdateResultViews({});
}

void MahoCommandOverlayView::ApplyPlaceholderForMode() {
  switch (mode_) {
    case CommandOverlayMode::kSearch:
    case CommandOverlayMode::kNewTab:
    case CommandOverlayMode::kCurrentTab:
      textfield_->SetPlaceholderText(u"Search or Enter URL\u2026");
      break;
    case CommandOverlayMode::kSiteSearch:
      textfield_->SetPlaceholderText(u"Search\u2026");
      break;
    case CommandOverlayMode::kAI:
      textfield_->SetPlaceholderText(u"Ask anything\u2026");
      break;
    case CommandOverlayMode::kCommandsOnly:
      textfield_->SetPlaceholderText(u"Search commands\u2026");
      break;
  }
}

void MahoCommandOverlayView::UpdateModeChipVisibility() {
  if (!mode_chip_) {
    return;
  }

  // The chip is an in-surface context indicator rendered inside the single
  // command overlay, not a separate surface. It appears only in sub-modes
  // (kSiteSearch, kAI, kCommandsOnly) where the input scope differs from the default.
  const bool show_chip = mode_ == CommandOverlayMode::kSiteSearch ||
                         mode_ == CommandOverlayMode::kAI ||
                         mode_ == CommandOverlayMode::kCommandsOnly;
  mode_chip_->SetVisible(show_chip);
}

void MahoCommandOverlayView::UpdateModeChip() {
  if (!mode_chip_ || !mode_chip_dot_ || !mode_label_) {
    return;
  }

  UpdateModeChipVisibility();

  const auto* cp = GetColorProvider();
  if (!cp) {
    return;
  }

  const bool show_chip = mode_ == CommandOverlayMode::kSiteSearch ||
                         mode_ == CommandOverlayMode::kAI ||
                         mode_ == CommandOverlayMode::kCommandsOnly;
  if (!show_chip) {
    return;
  }

  const bool is_ai_mode = mode_ == CommandOverlayMode::kAI;
  const bool is_commands_mode = mode_ == CommandOverlayMode::kCommandsOnly;
  std::u16string label_text;
  if (is_ai_mode) {
    label_text = std::u16string(u"AI Mode");
  } else if (is_commands_mode) {
    label_text = std::u16string(u"Commands");
  } else {
    label_text = base::UTF8ToUTF16(site_search_engine_name_.empty()
                                      ? "Site Search"
                                      : site_search_engine_name_);
  }
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

void MahoCommandOverlayView::OnSearchEngineBadgePressed() {
  if (picker_widget_) {
    picker_widget_->Close();
    return;
  }
  picker_widget_ = MahoCommandSearchEnginePickerView::Show(
      trailing_accessory_,
      base::BindRepeating(&MahoCommandOverlayView::OnSearchEngineSelected,
                          weak_factory_.GetWeakPtr()),
      model_.get());
  if (picker_widget_) {
    picker_observation_.Observe(picker_widget_);
  }
}

void MahoCommandOverlayView::OnSearchEngineSelected(const std::string& engine_id) {
  base::DictValue event;
  event.Set("type", "SetDefaultSearchEngine");
  event.Set("id", engine_id);
  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  char* result = maho_core_handle_event(maho::GetCore(), event_json.c_str());
  if (result) {
    maho_string_free(result);
  }

  // Refresh current results so the kSearch row reflects the new engine name.
  if (model_) {
    model_->RefetchCurrentQuery();
  }
}

void MahoCommandOverlayView::OnWidgetDestroying(views::Widget* widget) {
  if (widget == picker_widget_) {
    picker_observation_.Reset();
    picker_widget_ = nullptr;
  }
}

void MahoCommandOverlayView::ApplyVibrancyBackgroundTransparency(bool vibrancy_active) {
  if (!vibrancy_active) return;
  if (scroll_view_) {
    scroll_view_->SetBackgroundColor(std::nullopt);
    scroll_view_->SetDrawOverflowIndicator(false);
    if (scroll_view_->layer()) {
      scroll_view_->layer()->SetFillsBoundsOpaquely(true);
    }
  }
  if (results_container_) {
    results_container_->SetBackground(nullptr);
  }
  if (field_shell_) {
    field_shell_->SetBackground(nullptr);
  }
  if (divider_) {
    divider_->SetPreferredSize(gfx::Size(0, 1));
  }
  DisableSubpixelRenderingOnLabelsRecursive(this);
}

}  // namespace maho
