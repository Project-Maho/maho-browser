// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "build/buildflag.h"
#include "components/vector_icons/vector_icons.h"
#include "maho/browser/ui/views/maho_mac_sf_symbol_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/color/color_id.h"
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
constexpr int kLibraryRailLabelFontSizeDeltaDp = -2;
constexpr int kLibraryRailIconOpticalSizeDp = 22;

static constexpr gfx::PathElement kArchiveboxPath[] = {
    gfx::CANVAS_DIMENSIONS, 24,        gfx::STROKE,
    1.5f,                 gfx::MOVE_TO, 20.25f,
    7.5f,                 gfx::LINE_TO, 19.6246f,
    18.1321f,             gfx::CUBIC_TO, 19.5546f,
    19.3214f,             18.5698f,   20.25f,
    17.3785f,             20.25f,     gfx::H_LINE_TO,
    6.62154f,             gfx::CUBIC_TO, 5.43022f,
    20.25f,               4.44538f,   19.3214f,
    4.37542f,             18.1321f,   gfx::LINE_TO,
    3.75f,                7.5f,       gfx::MOVE_TO,
    9.99976f,             11.25f,     gfx::H_LINE_TO,
    13.9998f,             gfx::MOVE_TO, 3.375f,
    7.5f,                 gfx::H_LINE_TO, 20.625f,
    gfx::CUBIC_TO,        21.2463f,   7.5f,
    21.75f,               6.99632f,   21.75f,
    6.375f,               gfx::V_LINE_TO, 4.875f,
    gfx::CUBIC_TO,        21.75f,     4.25368f,
    21.2463f,             3.75f,      20.625f,
    3.75f,                gfx::H_LINE_TO, 3.375f,
    gfx::CUBIC_TO,        2.75368f,   3.75f,
    2.25f,                4.25368f,   2.25f,
    4.875f,               gfx::V_LINE_TO, 6.375f,
    gfx::CUBIC_TO,        2.25f,      6.99632f,
    2.75368f,             7.5f,       3.375f,
    7.5f,                 gfx::CLOSE,
};

constexpr char kArchiveboxIconId[] = "kSidebarLibraryRailArchiveboxIcon";
static constexpr gfx::VectorIconRep kArchiveboxRepList[] = {{kArchiveboxPath}};
constexpr gfx::VectorIcon kArchiveboxIcon(kArchiveboxRepList, 1,
                                          kArchiveboxIconId);

struct RailCategorySpec {
  MahoSidebarLibraryRailView::Category category;
  std::u16string_view title;
  std::u16string_view visible_label;
  raw_ptr<const gfx::VectorIcon> icon;
  // SF Symbol name used on macOS instead of the vector icon. Empty string
  // means "no SF Symbol defined; always use vector icon".
  std::string_view sf_symbol_name;
  std::string_view ascii_title;
  bool visible_in_first_entry_rail;
};

constexpr RailCategorySpec kRailCategories[] = {
    {MahoSidebarLibraryRailView::Category::kDownloads, u"Downloads",
     u"Downloads", &vector_icons::kFileDownloadOffChromeRefreshIcon,
     "arrow.down.to.line", "Downloads", true},
    {MahoSidebarLibraryRailView::Category::kArchivedTabs, u"Archived Tabs",
     u"Archived\nTabs", &kArchiveboxIcon,
     "archivebox", "Archived Tabs", true},
    {MahoSidebarLibraryRailView::Category::kSpaces, u"Spaces", u"Spaces",
     &vector_icons::kHomeIcon, "square.on.square", "Spaces", true},
    {MahoSidebarLibraryRailView::Category::kBoosts, u"Boosts", u"Boosts",
     &vector_icons::kScienceIcon, "bolt", "Boosts", true},
    {MahoSidebarLibraryRailView::Category::kMedia, u"Media", u"Media",
     &vector_icons::kVideoLibraryIcon, "photo.on.rectangle.angled", "Media",
     false},
    {MahoSidebarLibraryRailView::Category::kEasels, u"Easels", u"Easels",
     &vector_icons::kStickyNote2Icon, "pencil.and.scribble", "Easels", false},
    {MahoSidebarLibraryRailView::Category::kNotes, u"Notes", u"Notes",
     &vector_icons::kStickyNote2Icon, "note.text", "Notes", false},
};

void StyleIconButton(views::ImageButton* button) {
  button->SetBackground(nullptr);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);
}

ui::ColorId GetRailItemForegroundColor(bool selected) {
  return selected ? kMahoColorLibraryRailItemSelectedText
                  : kMahoColorLibraryRailItemText;
}

}  // namespace

const gfx::VectorIcon& GetMahoSidebarArchiveboxIcon() {
  return kArchiveboxIcon;
}

class MahoSidebarLibraryRailItemButton : public views::Button {
  METADATA_HEADER(MahoSidebarLibraryRailItemButton, views::Button)

 public:
  MahoSidebarLibraryRailItemButton(MahoSidebarLibraryRailView::Category category,
                                   std::u16string title,
                                   std::u16string visible_label,
                                   const gfx::VectorIcon& icon,
                                   std::string sf_symbol_name,
                                   std::string accessibility_name,
                                   bool enabled,
                                   PressedCallback callback)
      : views::Button(std::move(callback)),
        category_(category),
        title_(std::move(title)),
        visible_label_(std::move(visible_label)),
        icon_(icon),
        sf_symbol_name_(std::move(sf_symbol_name)),
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

    label_ = AddChildView(std::make_unique<views::Label>(visible_label_));
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetMultiLine(true);
    label_->SetMaximumWidth(sidebar_layout::kLibraryRailItemContentWidthDp);
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
    return views::Button::CalculatePreferredSize(size_bounds);
  }

  MahoSidebarLibraryRailView::Category category() const { return category_; }

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

 private:
  void UpdateAppearance() {
    const bool enabled = GetEnabled();
    const bool hovered = enabled &&
                         (GetState() == STATE_HOVERED ||
                          GetState() == STATE_PRESSED);
    const ui::ColorId foreground = selected_ || hovered
                                       ? kMahoColorLibraryRailItemSelectedText
                                       : GetRailItemForegroundColor(selected_);
    if (enabled && selected_) {
      SetBackground(views::CreateRoundedRectBackground(
          kMahoColorLibraryRailItemSelected,
          sidebar_layout::kLibraryRailItemCornerRadiusDp));
      SetBorder(nullptr);
    } else if (hovered) {
      SetBackground(views::CreateRoundedRectBackground(
          kMahoColorLibraryRailItemHover,
          sidebar_layout::kLibraryRailItemCornerRadiusDp));
      SetBorder(nullptr);
    } else {
      SetBackground(nullptr);
      SetBorder(nullptr);
    }

    icon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        icon_.get(), foreground, kLibraryRailIconOpticalSizeDp));
#if BUILDFLAG(IS_MAC)
    if (!sf_symbol_name_.empty()) {
      icon_view_->SetImage(CreateMahoMacSFSymbol(
          sf_symbol_name_, accessibility_name_, kLibraryRailIconOpticalSizeDp,
          foreground));
    }
#endif
    label_->SetEnabledColor(foreground);
    label_->SetFontList(base_label_font_list_.Derive(
        0, gfx::Font::NORMAL,
        selected_ ? gfx::Font::Weight::MEDIUM : gfx::Font::Weight::NORMAL));
    SetCanProcessEventsWithinSubtree(enabled);
  }

  const MahoSidebarLibraryRailView::Category category_;
  const std::u16string title_;
  const std::u16string visible_label_;
  const base::raw_ref<const gfx::VectorIcon> icon_;
  const std::string sf_symbol_name_;
  const std::string accessibility_name_;
  gfx::FontList base_label_font_list_;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  bool selected_ = false;
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
  SetBackground(views::CreateSolidBackground(kMahoColorLibraryRailBackground));

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
        std::string(spec.sf_symbol_name), std::string(spec.ascii_title),
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
      bounds.x() + sidebar_layout::kLibraryRailUtilitySpacingDp,
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
      bounds.x() + (bounds.width() - item_size.width()) / 2, anchored_y,
      item_size.width(), item_size.height());
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

void MahoSidebarLibraryRailView::SetSelectedCategory(Category category) {
  if (selected_category_ == category) {
    return;
  }
  selected_category_ = category;
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

void MahoSidebarLibraryRailView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateSolidBackground(kMahoColorLibraryRailBackground));
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
  if (enabled) {
    back_button_->SetBackground(views::CreateRoundedRectBackground(
        kMahoColorLibraryRailItemHover,
        sidebar_layout::kLibraryRailUtilityButtonCornerRadiusDp));
  } else {
    back_button_->SetBackground(nullptr);
  }
  back_button_->SetBorder(nullptr);

  const ui::ColorId back_color = enabled ? kMahoColorLibraryRailItemSelectedText
                                          : kMahoColorLibraryRailItemText;
#if BUILDFLAG(IS_MAC)
  back_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      CreateMahoMacSFSymbol("chevron.backward", "Back", 12, back_color));
#else
  back_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kBackArrowIcon,
                                     back_color, 12));
#endif
  back_button_->layer()->SetOpacity(enabled ? 1.0f : kBackButtonDisabledAlpha);
}

}  // namespace maho
