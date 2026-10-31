// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_spaces_overlay_column_view.h"

#include "chrome/browser/ui/browser.h"
#include "ui/gfx/canvas.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "maho/browser/ui/context_menu/maho_space_context_menu.h"
#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "cc/paint/paint_shader.h"

#include <memory>
#include <optional>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "maho_spaces_overlay_tab_row_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"

namespace maho {

namespace {

constexpr int kColumnMinHeightDp = 240;
constexpr int kColumnCornerRadiusDp = 16;
constexpr int kColumnInsetDp = 10;
constexpr int kColumnSpacingDp = 8;
constexpr int kTabListSpacingDp = 8;
constexpr int kTabSectionSpacingDp = 3;
constexpr int kTabSectionLabelTopInsetDp = 3;
constexpr int kTabSectionLabelBottomInsetDp = 1;
constexpr int kHeaderBadgeSizeDp = 10;
constexpr int kHeaderBadgeCornerRadiusDp = 5;
constexpr int kHeaderSpacingDp = 8;
constexpr int kHeaderTextSpacingDp = 3;
constexpr int kMetaSpacingDp = 4;

std::optional<SkColor> ParseSpaceThemeColor(const std::string& color_string) {
  std::string hex = color_string;
  if (!hex.empty() && hex.front() == '#') {
    hex.erase(hex.begin());
  }
  if (hex.size() != 6) {
    return std::nullopt;
  }

  uint32_t value = 0;
  if (!base::HexStringToUInt(hex, &value)) {
    return std::nullopt;
  }

  return SkColorSetRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF,
                       value & 0xFF);
}

std::optional<SkColor> SpaceThemeColor(const SpaceBoardColumn& column) {
  return ParseSpaceThemeColor(
      GetMahoSpaceDisplayPrimaryColor(column.theme, column.color));
}

std::u16string TabCountText(int count) {
  return base::UTF8ToUTF16(base::NumberToString(count)) +
         (count == 1 ? u" tab" : u" tabs");
}

struct TabSectionSpec {
  std::u16string title;
  std::vector<const SpaceBoardTabItem*> tabs;
};

std::vector<TabSectionSpec> BuildTabSections(const SpaceBoardColumn& column) {
  std::vector<const SpaceBoardTabItem*> pinned_tabs;
  std::vector<const SpaceBoardTabItem*> regular_tabs;

  for (const SpaceBoardTabItem& tab : column.tabs) {
    if (tab.is_pinned) {
      pinned_tabs.push_back(&tab);
      continue;
    }
    regular_tabs.push_back(&tab);
  }

  std::vector<TabSectionSpec> sections;
  if (!pinned_tabs.empty()) {
    sections.push_back(TabSectionSpec{u"PINNED", std::move(pinned_tabs)});
  }
  if (!regular_tabs.empty()) {
    sections.push_back(TabSectionSpec{u"TABS", std::move(regular_tabs)});
  }
  return sections;
}

std::u16string SectionAccessibleName(const std::u16string& title) {
  if (title == u"PINNED") {
    return u"Pinned tabs";
  }
  if (title == u"TABS") {
    return u"Tabs";
  }
  return title;
}


class ColumnThemeBackground : public views::Background {
 public:
  ColumnThemeBackground(SkColor base_fill_color,
                        SkColor theme_primary_color,
                        bool is_active)
      : base_fill_color_(base_fill_color),
        theme_primary_color_(theme_primary_color),
        is_active_(is_active) {}
  ~ColumnThemeBackground() override = default;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    gfx::Rect bounds = view->GetLocalBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(base_fill_color_);
    canvas->DrawRoundRect(gfx::RectF(bounds), kColumnCornerRadiusDp, flags);

    cc::PaintFlags gradient_flags;
    gradient_flags.setAntiAlias(true);
    gradient_flags.setStyle(cc::PaintFlags::kFill_Style);

    const SkAlpha top_alpha = is_active_ ? 0x40 : 0x20;
    const SkAlpha bottom_alpha = is_active_ ? 0x20 : 0x10;

    const SkColor4f colors[2] = {
        SkColor4f::FromColor(SkColorSetA(theme_primary_color_, top_alpha)),
        SkColor4f::FromColor(SkColorSetA(theme_primary_color_, bottom_alpha))};
    const SkScalar stops[2] = {0.0f, 1.0f};
    const SkPoint pts[2] = {SkPoint::Make(0.0f, 0.0f),
                            SkPoint::Make(0.0f, bounds.height())};
    gradient_flags.setShader(cc::PaintShader::MakeLinearGradient(
        pts, colors, stops, 2, SkTileMode::kClamp));

    canvas->DrawRoundRect(gfx::RectF(bounds), kColumnCornerRadiusDp,
                          gradient_flags);
  }

 private:
  const SkColor base_fill_color_;
  const SkColor theme_primary_color_;
  const bool is_active_;
};

class GrabImageView : public views::ImageView {
  METADATA_HEADER(GrabImageView, views::ImageView)
 public:
  GrabImageView() = default;
  ~GrabImageView() override = default;

  ui::Cursor GetCursor(const ui::MouseEvent& event) override {
    return ui::mojom::CursorType::kGrab;
  }
};

BEGIN_METADATA(GrabImageView)
END_METADATA

}  // namespace

BEGIN_METADATA(MahoSpacesOverlayColumnView)
END_METADATA

MahoSpacesOverlayColumnView::MahoSpacesOverlayColumnView(
    Browser* browser,
    SpacesBoardRenderMode render_mode,
    const SpaceBoardColumn& column,
    bool is_active,
    SpaceSelectedCallback callback,
    TabSelectedCallback tab_callback)
    : browser_(browser),
      render_mode_(render_mode),
      column_(column),
      is_active_(is_active),
      space_selected_callback_(std::move(callback)),
      tab_selected_callback_(std::move(tab_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kColumnInsetDp, kColumnInsetDp), kColumnSpacingDp));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  if (render_mode_ == SpacesBoardRenderMode::kEmbedded) {
    SetPreferredSize(gfx::Size(sidebar_layout::kDefaultRailWidthDp,
                               kColumnMinHeightDp));
  } else {
    SetPreferredSize(gfx::Size(sidebar_layout::kDefaultRailWidthDp, 0));
  }

  SetNotifyEnterExitOnChild(true);


  auto* header_row = AddChildView(std::make_unique<views::View>());
  auto* header_layout = header_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kHeaderSpacingDp));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStart);

  badge_container_ = header_row->AddChildView(std::make_unique<views::View>());
  badge_container_->SetPreferredSize(
      gfx::Size(kHeaderBadgeSizeDp, kHeaderBadgeSizeDp));
  badge_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  static_cast<views::BoxLayout*>(badge_container_->GetLayoutManager())
      ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  static_cast<views::BoxLayout*>(badge_container_->GetLayoutManager())
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  badge_label_ = badge_container_->AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  badge_label_->SetAutoColorReadabilityEnabled(false);
  badge_label_->SetSubpixelRenderingEnabled(false);
  badge_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  badge_label_->SetVisible(false);

  auto* heading_stack = header_row->AddChildView(std::make_unique<views::View>());
  auto* heading_layout = heading_stack->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kHeaderTextSpacingDp));
  heading_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  header_layout->SetFlexForView(heading_stack, 1);

  auto* title_row = heading_stack->AddChildView(std::make_unique<views::View>());
  auto* title_row_layout = title_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  title_row_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  title_label_ = title_row->AddChildView(std::make_unique<views::Label>(
      column_.name, views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_4_MEDIUM));
  title_label_->SetAutoColorReadabilityEnabled(false);
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_row_layout->SetFlexForView(title_label_, 1);

  tag_chip_ = title_row->AddChildView(std::make_unique<views::Label>(
      u"Default", views::style::CONTEXT_LABEL, views::style::STYLE_BODY_5));
  tag_chip_->SetAutoColorReadabilityEnabled(false);
  tag_chip_->SetSubpixelRenderingEnabled(false);
  tag_chip_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(1, 4)));

  edit_button_ = title_row->AddChildView(
      views::CreateVectorImageButton(base::BindRepeating(
          &MahoSpacesOverlayColumnView::HandleEditPressed,
          base::Unretained(this))));
  edit_button_->SetPreferredSize(gfx::Size(16, 16));
  edit_button_->SetTooltipText(u"Edit Space");
  edit_button_->SetAccessibleName(u"Edit Space");
  views::InstallCircleHighlightPathGenerator(edit_button_);

  auto* meta_row = heading_stack->AddChildView(std::make_unique<views::View>());
  auto* meta_layout = meta_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), kMetaSpacingDp));
  meta_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  subtitle_label_ = meta_row->AddChildView(std::make_unique<views::Label>(
      TabCountText(column_.tab_count), views::style::CONTEXT_LABEL,
      views::style::STYLE_BODY_5));
  subtitle_label_->SetAutoColorReadabilityEnabled(false);
  subtitle_label_->SetSubpixelRenderingEnabled(false);
  subtitle_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  tab_scroll_view_ = AddChildView(std::make_unique<views::ScrollView>());
  tab_scroll_view_->SetBackgroundColor(std::nullopt);
  tab_scroll_view_->SetDrawOverflowIndicator(false);
  tab_scroll_view_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  tab_scroll_view_->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  layout->SetFlexForView(tab_scroll_view_, 1);

  tab_list_ = tab_scroll_view_->SetContents(std::make_unique<views::View>());
  auto* tab_list_layout = tab_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      kTabListSpacingDp));
  tab_list_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  tab_list_->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
  tab_list_->GetViewAccessibility().SetName(column_.name + u" tabs");

  if (column_.tabs.empty()) {
    empty_state_label_ = tab_list_->AddChildView(std::make_unique<views::Label>(
        u"No tabs parked here yet.", views::style::CONTEXT_LABEL,
        views::style::STYLE_BODY_4));
    empty_state_label_->SetAutoColorReadabilityEnabled(false);
    empty_state_label_->SetSubpixelRenderingEnabled(false);
    empty_state_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    empty_state_label_->SetMultiLine(true);
    empty_state_label_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(10, 0)));
  } else {
    std::vector<TabSectionSpec> sections = BuildTabSections(column_);
    for (size_t i = 0; i < sections.size(); ++i) {
      TabSectionSpec& section = sections[i];
      auto* section_view = tab_list_->AddChildView(std::make_unique<views::View>());
      auto* section_layout =
          section_view->SetLayoutManager(std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kVertical, gfx::Insets(),
              kTabSectionSpacingDp));
      section_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kStretch);
      section_view->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
      section_view->GetViewAccessibility().SetName(
          SectionAccessibleName(section.title));

      if (i > 0) {
        auto* separator_row = section_view->AddChildView(std::make_unique<views::View>());
        auto* sep_layout = separator_row->SetLayoutManager(
            std::make_unique<views::BoxLayout>(
                views::BoxLayout::Orientation::kHorizontal,
                gfx::Insets::TLBR(kTabSectionLabelTopInsetDp, 0, kTabSectionLabelBottomInsetDp, 0),
                4));
        sep_layout->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kCenter);

        auto* spacer = separator_row->AddChildView(std::make_unique<views::View>());
        sep_layout->SetFlexForView(spacer, 1);

        clear_arrow_ = separator_row->AddChildView(std::make_unique<views::ImageView>());
        // Styled in UpdateColors()

        clear_label_ = separator_row->AddChildView(std::make_unique<views::Label>(
            u"Clear", views::style::CONTEXT_LABEL,
            views::style::STYLE_BODY_5_MEDIUM));
        clear_label_->SetAutoColorReadabilityEnabled(false);
        clear_label_->SetSubpixelRenderingEnabled(false);
        clear_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      }

      for (const SpaceBoardTabItem* tab : section.tabs) {
        auto tab_row = std::make_unique<MahoSpacesOverlayTabRowView>(
            browser_ ? browser_->GetProfile() : nullptr, *tab, column_.space_id,
            tab_selected_callback_);
        tab_row->SetPalette(palette_);
        tab_row_views_.push_back(section_view->AddChildView(std::move(tab_row)));
      }
    }
  }

  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  GetViewAccessibility().SetName(
      is_active_ ? column_.name + u" (current space)" : column_.name);

  // Add Footer (Grip + Ellipsis Menu)
  auto* footer_row = AddChildView(std::make_unique<views::View>());
  auto* footer_layout = footer_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Glyph color is bound to the sidebar palette in UpdateColors().
  drag_handle_ = footer_row->AddChildView(std::make_unique<GrabImageView>());

  auto* spacer = footer_row->AddChildView(std::make_unique<views::View>());
  footer_layout->SetFlexForView(spacer, 1);

  menu_button_ = footer_row->AddChildView(
      views::CreateVectorImageButton(base::BindRepeating(
          &MahoSpacesOverlayColumnView::HandleMenuPressed,
          base::Unretained(this))));
  menu_button_->SetTooltipText(u"Space Menu");
  menu_button_->SetAccessibleName(u"Space Menu");
  views::InstallCircleHighlightPathGenerator(menu_button_);

  UpdateColors();
}

MahoSpacesOverlayColumnView::~MahoSpacesOverlayColumnView() = default;

bool MahoSpacesOverlayColumnView::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsOnlyLeftMouseButton() && space_selected_callback_) {
    space_selected_callback_.Run(column_.space_id);
    return true;
  }
  return false;
}

ui::Cursor MahoSpacesOverlayColumnView::GetCursor(const ui::MouseEvent& event) {
  return ui::mojom::CursorType::kHand;
}

std::u16string MahoSpacesOverlayColumnView::GetTitleTextForTesting() const {
  return title_label_ ? std::u16string(title_label_->GetText()) : std::u16string();
}

std::vector<std::u16string>
MahoSpacesOverlayColumnView::GetSectionTitlesForTesting() const {
  std::vector<std::u16string> titles;
  if (clear_label_) {
    titles.push_back(u"Clear");
  }
  return titles;
}

void MahoSpacesOverlayColumnView::SetPalette(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  for (MahoSpacesOverlayTabRowView* tab_row : tab_row_views_) {
    tab_row->SetPalette(palette_);
  }
  UpdateColors();
}

void MahoSpacesOverlayColumnView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

gfx::Size MahoSpacesOverlayColumnView::GetMinimumSize() const {
  return gfx::Size(sidebar_layout::kDefaultRailWidthDp, kColumnMinHeightDp);
}

void MahoSpacesOverlayColumnView::UpdateColors() {
  const std::optional<SkColor> theme_color = SpaceThemeColor(column_);
  const SkColor column_border = SkColorSetA(palette_.outline, 0x60);
  const SkColor base_fill = palette_.content_surface_stops.empty()
                                ? palette_.row_active
                                : palette_.content_surface_stops.back();
  if (theme_color) {
    SetBackground(std::make_unique<ColumnThemeBackground>(
        base_fill, *theme_color, is_active_));
  } else {
    SetBackground(views::CreateRoundedRectBackground(
        base_fill, kColumnCornerRadiusDp));
  }
  SetBorder(views::CreateRoundedRectBorder(1, kColumnCornerRadiusDp,
                                            column_border));
  if (theme_color) {
    badge_container_->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetA(*theme_color, 0xD0), kHeaderBadgeCornerRadiusDp));
  } else {
    badge_container_->SetBackground(nullptr);
  }
  badge_container_->SetBorder(nullptr);
  title_label_->SetEnabledColor(palette_.primary_text);
  // Tab count is readable metadata: secondary role, never tertiary.
  subtitle_label_->SetEnabledColor(palette_.secondary_text);

  if (clear_label_) {
    clear_label_->SetEnabledColor(palette_.secondary_text);
  }

  if (clear_arrow_) {
    clear_arrow_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kArrowUpIcon, palette_.neutral_glyph, 12));
  }

  if (drag_handle_) {
    drag_handle_->SetImage(ui::ImageModel::FromVectorIcon(
        maho_lucide_icons::kMoveIcon, palette_.neutral_glyph, 16));
  }

  if (empty_state_label_) {
    empty_state_label_->SetEnabledColor(palette_.secondary_text);
  }

  if (tag_chip_) {
    tag_chip_->SetBackground(
        views::CreateRoundedRectBackground(palette_.row_hover, 4));
    tag_chip_->SetEnabledColor(palette_.secondary_text);
  }

  // Functional glyphs use the contrast-guarded neutral role in every state;
  // the raw Space color has no contrast guarantee on the column surface.
  if (edit_button_) {
    SetFunctionalGlyphImages(edit_button_, maho_lucide_icons::kPencilLineIcon);
  }

  if (menu_button_) {
    SetFunctionalGlyphImages(menu_button_, maho_lucide_icons::kEllipsisIcon);
  }
}

void MahoSpacesOverlayColumnView::SetFunctionalGlyphImages(
    views::ImageButton* button,
    const gfx::VectorIcon& icon) {
  const ui::ImageModel normal =
      ui::ImageModel::FromVectorIcon(icon, palette_.neutral_glyph, 16);
  button->SetImageModel(views::Button::STATE_NORMAL, normal);
  button->SetImageModel(views::Button::STATE_HOVERED, normal);
  button->SetImageModel(views::Button::STATE_PRESSED, normal);
  button->SetImageModel(
      views::Button::STATE_DISABLED,
      ui::ImageModel::FromVectorIcon(icon, palette_.disabled_text, 16));
}


void MahoSpacesOverlayColumnView::HandleEditPressed() {
  if (browser_) {
    MahoSpaceConfigDialog::Open(
        browser_->GetProfile(), GetWidget()->GetNativeWindow(),
        column_.space_id, maho_space_config::mojom::InitialFocus::kName);
  }
}

void MahoSpacesOverlayColumnView::HandleMenuPressed() {
  if (!browser_ || !menu_button_) {
    return;
  }
  // Guard against re-entry while the menu is already showing.
  if (context_menu_runner_ && context_menu_runner_->IsRunning()) {
    context_menu_runner_->Cancel();
    return;
  }
  active_space_context_menu_.reset();
  active_context_menu_model_.reset();
  context_menu_runner_.reset();

  active_space_context_menu_ =
      std::make_unique<MahoSpaceContextMenu>(browser_, column_.space_id);
  active_context_menu_model_ = active_space_context_menu_->BuildMenuModel();
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      active_context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);

  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr,
      menu_button_->GetBoundsInScreen(),
      views::MenuAnchorPosition::kBubbleTopRight,
      ui::mojom::MenuSourceType::kMouse);
}

}  // namespace maho
