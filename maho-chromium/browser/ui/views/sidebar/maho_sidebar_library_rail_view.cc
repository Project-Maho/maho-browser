// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "build/buildflag.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_data.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"

namespace maho {

namespace {

constexpr float kBackButtonDisabledAlpha = 0.38f;
constexpr int kLibraryRailLabelLineHeightDp = 11;
constexpr int kLibraryRailLabelFontSizeDeltaDp = -1;
constexpr int kLibraryRailIconOpticalSizeDp = 22;
// Rail Downloads progress indicator: a 3dp rounded track/fill bar sized to the
// icon, painted in the gap between the icon and the label.
constexpr int kDownloadsIndicatorWidthDp = 24;
constexpr int kDownloadsIndicatorHeightDp = 3;
constexpr int kDownloadsIndicatorCornerRadiusDp = 2;
// Unknown total size has no honest fraction, so the indeterminate band is drawn
// muted instead of reading as a near-complete transfer.
constexpr uint8_t kDownloadsIndicatorIndeterminateAlpha = 0x66;

struct RailCategorySpec {
  MahoSidebarLibraryRailView::Category category;
  std::u16string_view title;
  std::u16string_view visible_label;
  raw_ptr<const gfx::VectorIcon> icon;
  std::string_view ascii_title;
  bool visible_in_first_entry_rail;
};

constexpr RailCategorySpec kRailCategories[] = {
    {MahoSidebarLibraryRailView::Category::kDownloads, u"Downloads",
     u"Downloads", &maho_lucide_icons::kArrowDownToLineIcon,
     "Downloads", true},
    {MahoSidebarLibraryRailView::Category::kArchivedTabs, u"Archived Tabs",
     u"Archived\nTabs", &maho_lucide_icons::kArchiveIcon,
     "Archived Tabs", true},
    {MahoSidebarLibraryRailView::Category::kSpaces, u"Spaces", u"Spaces",
     &vector_icons::kHomeIcon, "Spaces", true},
    {MahoSidebarLibraryRailView::Category::kMedia, u"Media", u"Media",
     &maho_lucide_icons::kImagesIcon, "Media",
     false},
};

void StyleIconButton(views::ImageButton* button) {
  button->SetBackground(nullptr);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);
}

SkColor GetRailItemForegroundColor(const MahoSidebarPalette& palette,
                                   bool selected) {
  return selected ? palette.primary_text : palette.secondary_text;
}

}  // namespace

// Aggregate Downloads progress painted under the rail's Downloads icon. Owned
// by the Downloads rail item; it holds no download state of its own beyond the
// snapshot the owning sidebar pushes in.
class MahoSidebarDownloadsIndicatorView : public views::View {
  METADATA_HEADER(MahoSidebarDownloadsIndicatorView, views::View)

 public:
  MahoSidebarDownloadsIndicatorView() {
    SetPreferredSize(
        gfx::Size(kDownloadsIndicatorWidthDp, kDownloadsIndicatorHeightDp));
    // Purely decorative: the rail item's own accessible description carries the
    // status, and pointer events must keep reaching the button underneath.
    GetViewAccessibility().SetIsIgnored(true);
    SetCanProcessEventsWithinSubtree(false);
    fill_view_ = AddChildView(std::make_unique<views::View>());
    fill_view_->GetViewAccessibility().SetIsIgnored(true);
    SetVisible(false);
  }

  MahoSidebarDownloadsIndicatorView(const MahoSidebarDownloadsIndicatorView&) =
      delete;
  MahoSidebarDownloadsIndicatorView& operator=(
      const MahoSidebarDownloadsIndicatorView&) = delete;
  ~MahoSidebarDownloadsIndicatorView() override = default;

  void SetState(const DownloadsIndicatorState& state,
                const MahoSidebarPalette& palette,
                bool enabled) {
    state_ = state;
    SkColor fill_color = enabled ? palette.focus_ring : palette.disabled_text;
    if (state_.indeterminate) {
      fill_color =
          SkColorSetA(fill_color, kDownloadsIndicatorIndeterminateAlpha);
    }
    SetBackground(views::CreateRoundedRectBackground(
        palette.row_selected, kDownloadsIndicatorCornerRadiusDp));
    fill_view_->SetBackground(views::CreateRoundedRectBackground(
        fill_color, kDownloadsIndicatorCornerRadiusDp));
    SetVisible(state_.visible);
    UpdateFillBounds();
  }

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override {
    views::View::OnBoundsChanged(previous_bounds);
    UpdateFillBounds();
  }

  int fill_width_for_testing() const { return fill_view_->width(); }

 private:
  int FillWidth() const {
    if (state_.indeterminate) {
      return width();
    }
    return std::clamp(
        static_cast<int>(std::lround(width() * state_.fraction)), 0, width());
  }

  void UpdateFillBounds() {
    fill_view_->SetBounds(0, 0, FillWidth(), height());
  }

  DownloadsIndicatorState state_;
  raw_ptr<views::View> fill_view_ = nullptr;
};

BEGIN_METADATA(MahoSidebarDownloadsIndicatorView)
END_METADATA

const gfx::VectorIcon& GetMahoSidebarArchiveboxIcon() {
  return maho_lucide_icons::kArchiveIcon;
}

class MahoSidebarLibraryRailItemButton : public views::Button {
  METADATA_HEADER(MahoSidebarLibraryRailItemButton, views::Button)

 public:
  MahoSidebarLibraryRailItemButton(MahoSidebarLibraryRailView::Category category,
                                   std::u16string title,
                                   std::u16string visible_label,
                                   const gfx::VectorIcon& icon,
                                   std::string accessibility_name,
                                   bool enabled,
                                   PressedCallback callback)
      : views::Button(std::move(callback)),
        category_(category),
        title_(std::move(title)),
        visible_label_(std::move(visible_label)),
        icon_(icon),
        accessibility_name_(std::move(accessibility_name)) {
    SetAccessibleName(title_);
    GetViewAccessibility().SetIsSelected(false);
    SetTooltipText(title_);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetRequestFocusOnPress(false);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(sidebar_layout::kLibraryRailItemVerticalInsetDp,
                          sidebar_layout::kLibraryRailItemHorizontalInsetDp,
                          sidebar_layout::kLibraryRailItemVerticalInsetDp,
                          sidebar_layout::kLibraryRailItemHorizontalInsetDp),
        sidebar_layout::kLibraryRailIconLabelSpacingDp));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    icon_view_ = AddChildView(std::make_unique<views::ImageView>());
    icon_view_->SetImageSize(
        gfx::Size(kLibraryRailIconOpticalSizeDp, kLibraryRailIconOpticalSizeDp));

    if (category == MahoSidebarLibraryRailView::Category::kDownloads) {
      auto indicator =
          std::make_unique<MahoSidebarDownloadsIndicatorView>();
      // Positioned by PositionDownloadsIndicator(), not by the BoxLayout, so
      // the icon/label stack keeps its exact pre-indicator geometry.
      indicator->SetProperty(views::kViewIgnoredByLayoutKey, true);
      downloads_indicator_ = AddChildView(std::move(indicator));
    }

    label_ = AddChildView(std::make_unique<views::Label>(visible_label_));
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label_->SetAutoColorReadabilityEnabled(false);
    const bool multiline =
        (category == MahoSidebarLibraryRailView::Category::kArchivedTabs);
    label_->SetMultiLine(multiline);
    if (multiline) {
      label_->SetMaximumWidth(sidebar_layout::kLibraryRailItemContentWidthDp);
    } else {
      label_->SetMaximumWidthSingleLine(
          sidebar_layout::kLibraryRailItemContentWidthDp);
    }
    label_->SetElideBehavior(gfx::NO_ELIDE);
    label_->SetLineHeight(kLibraryRailLabelLineHeightDp);
    base_label_font_list_ = label_->font_list().Derive(
        kLibraryRailLabelFontSizeDeltaDp, gfx::Font::NORMAL,
        gfx::Font::Weight::NORMAL);
    label_->SetFontList(base_label_font_list_);
    label_->SetSubpixelRenderingEnabled(false);

    views::InstallRoundRectHighlightPathGenerator(
        this, gfx::Insets(), sidebar_layout::kLibraryRailItemCornerRadiusDp);
    SetShowInkDropWhenHotTracked(false);
    SetHasInkDropActionOnClick(false);
    auto* ink_drop = views::InkDrop::Get(this);
    ink_drop->SetMode(views::InkDropHost::InkDropMode::OFF);
    SetEnabled(enabled);
    UpdateAppearance();
  }

  MahoSidebarLibraryRailItemButton(const MahoSidebarLibraryRailItemButton&) =
      delete;
  MahoSidebarLibraryRailItemButton& operator=(
      const MahoSidebarLibraryRailItemButton&) = delete;
  ~MahoSidebarLibraryRailItemButton() override = default;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    views::SizeBounds size_bounds(sidebar_layout::kLibraryRailItemWidthDp,
                                  available_size.height());
    gfx::Size size = views::Button::CalculatePreferredSize(size_bounds);
    // Pin to the uniform item width so all rail items share one button width.
    size.set_width(sidebar_layout::kLibraryRailItemWidthDp);
    return size;
  }

  MahoSidebarLibraryRailView::Category category() const { return category_; }

  void SetDownloadsIndicatorState(const DownloadsIndicatorState& state) {
    downloads_indicator_state_ = state;
    UpdateAppearance();
  }

  views::View* downloads_indicator_for_testing() {
    return downloads_indicator_;
  }

  int downloads_indicator_fill_width_for_testing() const {
    return downloads_indicator_ ? downloads_indicator_->fill_width_for_testing()
                                : 0;
  }

  void OnSidebarPaletteChanged(const MahoSidebarPalette& palette) {
    palette_ = palette;
    UpdateAppearance();
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    GetViewAccessibility().SetIsSelected(selected_);
    UpdateAppearance();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateAppearance();
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateAppearance();
  }

  void Layout(PassKey) override {
    LayoutSuperclass<views::Button>(this);
    PositionDownloadsIndicator();
  }

 private:
  // Sits the indicator in the icon/label gap, centered on the icon. The
  // indicator is ignored by the BoxLayout, so appearing progress never reflows
  // the rail stack or shifts the icon.
  void PositionDownloadsIndicator() {
    if (!downloads_indicator_ || !downloads_indicator_->GetVisible() ||
        !icon_view_ || !label_) {
      return;
    }
    const gfx::Rect icon_bounds = icon_view_->bounds();
    const gfx::Size indicator_size = downloads_indicator_->GetPreferredSize();
    const int gap_height =
        std::max(0, label_->bounds().y() - icon_bounds.bottom());
    downloads_indicator_->SetBounds(
        icon_bounds.CenterPoint().x() - indicator_size.width() / 2,
        icon_bounds.bottom() +
            std::max(0, gap_height - indicator_size.height()) / 2,
        indicator_size.width(), indicator_size.height());
  }

  void UpdateAppearance() {
    const bool enabled = GetEnabled();
    const bool hovered = enabled &&
                         (GetState() == STATE_HOVERED ||
                          GetState() == STATE_PRESSED);
    const SkColor foreground =
        !enabled ? palette_.disabled_text
        : selected_ || hovered
            ? palette_.primary_text
            : GetRailItemForegroundColor(palette_, selected_);
    if (enabled && selected_) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_selected,
          sidebar_layout::kLibraryRailItemCornerRadiusDp));
      SetBorder(nullptr);
    } else if (hovered) {
      SetBackground(views::CreateRoundedRectBackground(
          palette_.row_hover,
          sidebar_layout::kLibraryRailItemCornerRadiusDp));
      SetBorder(nullptr);
    } else {
      SetBackground(nullptr);
      SetBorder(nullptr);
    }

    icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        icon_.get(), foreground, kLibraryRailIconOpticalSizeDp));
    label_->SetEnabledColor(foreground);
    label_->SetFontList(base_label_font_list_.Derive(
        0, gfx::Font::NORMAL,
        selected_ ? gfx::Font::Weight::MEDIUM : gfx::Font::Weight::NORMAL));
    SetCanProcessEventsWithinSubtree(enabled);

    if (downloads_indicator_) {
      downloads_indicator_->SetState(downloads_indicator_state_, palette_,
                                    enabled);
      PositionDownloadsIndicator();
    }
    // At-a-glance status for screen readers, e.g. "Downloads, 2 downloads in
    // progress, 37% complete". Cleared with the indicator itself.
    GetViewAccessibility().SetDescription(
        downloads_indicator_state_.visible
            ? DownloadsIndicatorAccessibleDescription(
                  downloads_indicator_state_)
            : std::u16string());
  }

  const MahoSidebarLibraryRailView::Category category_;
  const std::u16string title_;
  const std::u16string visible_label_;
  const base::raw_ref<const gfx::VectorIcon> icon_;
  const std::string accessibility_name_;
  gfx::FontList base_label_font_list_;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<MahoSidebarDownloadsIndicatorView> downloads_indicator_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  bool selected_ = false;
  DownloadsIndicatorState downloads_indicator_state_;
  MahoSidebarPalette palette_;
};

BEGIN_METADATA(MahoSidebarLibraryRailItemButton)
END_METADATA

BEGIN_METADATA(MahoSidebarLibraryRailView)
END_METADATA

MahoSidebarLibraryRailView::MahoSidebarLibraryRailView() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(sidebar_layout::kLibraryRailTopInsetDp,
                        sidebar_layout::kLibraryRailSideInsetDp,
                        sidebar_layout::kLibraryRailBottomInsetDp,
                        sidebar_layout::kLibraryRailSideInsetDp),
      0));
  layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetPreferredSize(gfx::Size(sidebar_layout::kLibraryRailWidthDp, 0));
  // Transparent: the parent sidebar already paints the background surface.
  // Painting it again here double-composites the translucent (glass-frame)
  // color, darkening the rail relative to the content pane.
  SetBackground(nullptr);

  utility_host_ = AddChildView(std::make_unique<views::View>());
  utility_host_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  auto* utility_layout = utility_host_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  utility_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  utility_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  utility_host_->SetVisible(true);

  auto back_button = views::CreateVectorImageButton(base::BindRepeating(
      &MahoSidebarLibraryRailView::HandleBackPressed,
      weak_factory_.GetWeakPtr()));
  back_button->SetPreferredSize(gfx::Size(
      sidebar_layout::kLibraryRailUtilityButtonSizeDp,
      sidebar_layout::kLibraryRailUtilityButtonSizeDp));
  back_button->SetTooltipText(u"Back");
  back_button->SetAccessibleName(u"Back");
  back_button->SetPaintToLayer();
  back_button->layer()->SetFillsBoundsOpaquely(false);
  views::InstallCircleHighlightPathGenerator(back_button.get());
  StyleIconButton(back_button.get());
  back_button_ = utility_host_->AddChildView(std::move(back_button));

  item_container_ = AddChildView(std::make_unique<views::View>());
  item_container_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  auto* item_layout = item_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          sidebar_layout::kLibraryRailItemSpacingDp));
  item_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  for (const auto& spec : kRailCategories) {
    const bool enabled = true;
    auto button = std::make_unique<MahoSidebarLibraryRailItemButton>(
        spec.category, std::u16string(spec.title),
        std::u16string(spec.visible_label), *spec.icon,
        std::string(spec.ascii_title),
        enabled,
        base::BindRepeating(&MahoSidebarLibraryRailView::HandleCategoryPressed,
                            weak_factory_.GetWeakPtr(), spec.category));
    auto* button_ptr = item_container_->AddChildView(std::move(button));
    button_ptr->SetVisible(spec.visible_in_first_entry_rail);
    category_buttons_.push_back(button_ptr);
  }

  UpdateSelection();
  UpdateBackButtonAppearance();
}

MahoSidebarLibraryRailView::~MahoSidebarLibraryRailView() = default;

void MahoSidebarLibraryRailView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);

  if (!utility_host_) {
    return;
  }

  const gfx::Rect bounds = GetContentsBounds();
  const gfx::Size host_size = utility_host_->GetPreferredSize();
  utility_host_->SetBounds(
      bounds.x() +
          ((bounds.width() - sidebar_layout::kLibraryRailRightMarginDp) -
           host_size.width()) /
              2,
      bounds.bottom() - sidebar_layout::kLibraryRailBottomInsetDp -
          host_size.height(),
      host_size.width(), host_size.height());

  if (!item_container_) {
    return;
  }

  const gfx::Size item_size = item_container_->GetPreferredSize();
  const int centered_y =
      bounds.y() + (bounds.height() - item_size.height()) / 2;
  const int anchored_y = std::max(
      bounds.y() + sidebar_layout::kLibraryRailTopInsetDp,
      centered_y - sidebar_layout::kLibraryRailItemStackVisualLiftDp);
  item_container_->SetBounds(
      bounds.x() +
          ((bounds.width() - sidebar_layout::kLibraryRailRightMarginDp) -
           item_size.width()) /
              2,
      anchored_y, item_size.width(), item_size.height());
}

views::Button* MahoSidebarLibraryRailView::button_for_category_for_testing(
    Category category) {
  for (auto& button : category_buttons_) {
    if (button && button->category() == category) {
      return button;
    }
  }
  return nullptr;
}

std::vector<MahoSidebarLibraryRailView::Category>
MahoSidebarLibraryRailView::visible_categories_for_testing() const {
  std::vector<Category> visible_categories;
  visible_categories.reserve(category_buttons_.size());

  for (const auto& button : category_buttons_) {
    if (button && button->GetVisible()) {
      visible_categories.push_back(button->category());
    }
  }

  return visible_categories;
}

void MahoSidebarLibraryRailView::SetDownloadsIndicatorState(
    const DownloadsIndicatorState& state) {
  downloads_indicator_state_ = state;
  for (auto& button : category_buttons_) {
    if (button && button->category() == Category::kDownloads) {
      button->SetDownloadsIndicatorState(state);
      return;
    }
  }
}

views::View* MahoSidebarLibraryRailView::downloads_indicator_for_testing() {
  for (auto& button : category_buttons_) {
    if (button && button->category() == Category::kDownloads) {
      return button->downloads_indicator_for_testing();
    }
  }
  return nullptr;
}

int MahoSidebarLibraryRailView::downloads_indicator_fill_width_for_testing()
    const {
  for (const auto& button : category_buttons_) {
    if (button && button->category() == Category::kDownloads) {
      return button->downloads_indicator_fill_width_for_testing();
    }
  }
  return 0;
}

void MahoSidebarLibraryRailView::SetSelectedCategory(Category category) {
  if (selected_category_ == category) {
    return;
  }
  selected_category_ = category;
  UpdateSelection();
}

void MahoSidebarLibraryRailView::ClearSelection() {
  selected_category_ = static_cast<Category>(-1);
  UpdateSelection();
}

void MahoSidebarLibraryRailView::SetCategorySelectedCallback(
    CategorySelectedCallback callback) {
  category_selected_callback_ = std::move(callback);
}

void MahoSidebarLibraryRailView::SetBackCallback(
    base::RepeatingClosure callback) {
  back_callback_ = std::move(callback);
  UpdateBackButtonAppearance();
}

void MahoSidebarLibraryRailView::OnSidebarPaletteChanged(
    const MahoSidebarPalette& palette) {
  palette_ = palette;
  for (auto& button : category_buttons_) {
    if (button) {
      button->OnSidebarPaletteChanged(palette);
    }
  }
  UpdateBackButtonAppearance();
}

void MahoSidebarLibraryRailView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(nullptr);
  UpdateSelection();
  UpdateBackButtonAppearance();
}

bool MahoSidebarLibraryRailView::IsPositionInWindowCaption(
    const gfx::Point& point) const {
  for (const auto& button : category_buttons_) {
    if (button && button->GetVisible()) {
      gfx::Point point_in_button = point;
      views::View::ConvertPointToTarget(this, button, &point_in_button);
      if (button->HitTestPoint(point_in_button)) {
        return false;
      }
    }
  }

  if (back_button_ && back_button_->GetVisible()) {
    gfx::Point point_in_button = point;
    views::View::ConvertPointToTarget(this, back_button_, &point_in_button);
    if (back_button_->HitTestPoint(point_in_button)) {
      return false;
    }
  }

  return true;
}

void MahoSidebarLibraryRailView::HandleCategoryPressed(Category category,
                                                       const ui::Event& event) {
  if (selected_category_ != category) {
    selected_category_ = category;
    UpdateSelection();
  }
  if (category_selected_callback_) {
    category_selected_callback_.Run(category);
  }
}

void MahoSidebarLibraryRailView::HandleBackPressed(const ui::Event& event) {
  if (back_callback_) {
    back_callback_.Run();
  }
}

void MahoSidebarLibraryRailView::UpdateSelection() {
  for (auto& button : category_buttons_) {
    if (!button) {
      continue;
    }
    button->SetSelected(button->category() == selected_category_);
  }
}

void MahoSidebarLibraryRailView::UpdateBackButtonAppearance() {
  if (!back_button_) {
    return;
  }

  const bool enabled = !back_callback_.is_null();
  back_button_->SetEnabled(enabled);
  back_button_->SetBackground(nullptr);
  back_button_->SetBorder(nullptr);

  const SkColor back_color =
      enabled ? palette_.primary_text : palette_.disabled_text;
  back_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(maho_lucide_icons::kArrowLeftIcon,
                                     back_color, kLibraryRailIconOpticalSizeDp));
  back_button_->layer()->SetOpacity(enabled ? 1.0f : kBackButtonDisabledAlpha);
}

}  // namespace maho
