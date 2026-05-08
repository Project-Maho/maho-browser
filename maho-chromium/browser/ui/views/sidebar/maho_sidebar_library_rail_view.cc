// Copyright 2026 Maho Browser. All rights reserved.

#include "maho_sidebar_library_rail_view.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "third_party/skia/include/core/SkColor.h"

namespace maho {

namespace {

constexpr int kRailWidthDp = 64;
constexpr int kRailTopInsetDp = 12;
constexpr int kRailSideInsetDp = 3;
constexpr int kRailBottomInsetDp = 10;
constexpr int kRailItemSpacingDp = 2;
constexpr int kRailItemHeightDp = 50;
constexpr int kRailTileWidthDp = 48;
constexpr int kRailTileHeightDp = 30;
constexpr int kRailTileCornerRadiusDp = 7;
constexpr int kRailIconSizeDp = 18;
constexpr int kRailLabelTopSpacingDp = 1;
constexpr int kRailBackButtonSizeDp = 24;
constexpr int kRailBackButtonLeadingInsetDp = 8;
constexpr int kRailBackButtonCornerRadiusDp = 12;
constexpr float kBackButtonDisabledAlpha = 0.38f;

static constexpr gfx::PathElement kMediaPath[] = {
    gfx::CANVAS_DIMENSIONS, 24,        gfx::STROKE,   1.8f,
    gfx::MOVE_TO,           5.0f,      7.0f,          gfx::H_LINE_TO,
    19.0f,                  gfx::V_LINE_TO, 16.0f,    gfx::H_LINE_TO,
    5.0f,                   gfx::CLOSE, gfx::MOVE_TO, 10.0f,
    10.0f,                  gfx::LINE_TO, 14.5f,      12.0f,
    gfx::LINE_TO,           10.0f,     14.5f,         gfx::CLOSE,
};

static constexpr gfx::PathElement kDownloadsPath[] = {
    gfx::CANVAS_DIMENSIONS, 24,        gfx::STROKE,   1.8f,
    gfx::MOVE_TO,           12.0f,     4.5f,          gfx::V_LINE_TO,
    14.0f,                  gfx::MOVE_TO, 8.5f,       10.5f,
    gfx::LINE_TO,           12.0f,     14.0f,         gfx::LINE_TO,
    15.5f,                  10.5f,     gfx::MOVE_TO,  6.0f,
    18.0f,                  gfx::H_LINE_TO, 18.0f,
};

static constexpr gfx::PathElement kEaselPath[] = {
    gfx::CANVAS_DIMENSIONS, 24,        gfx::STROKE,   1.8f,
    gfx::MOVE_TO,           7.0f,      5.0f,          gfx::H_LINE_TO,
    17.0f,                  gfx::V_LINE_TO, 14.0f,    gfx::H_LINE_TO,
    7.0f,                   gfx::CLOSE, gfx::MOVE_TO, 10.0f,
    14.0f,                  gfx::LINE_TO, 8.0f,       19.0f,
    gfx::MOVE_TO,           14.0f,     14.0f,         gfx::LINE_TO,
    16.0f,                  19.0f,     gfx::MOVE_TO,  12.0f,
    14.0f,                  gfx::V_LINE_TO, 20.0f,
};

static constexpr gfx::PathElement kSpacesPath[] = {
    gfx::CANVAS_DIMENSIONS, 24,        gfx::STROKE,   1.8f,
    gfx::MOVE_TO,           5.0f,      5.0f,          gfx::H_LINE_TO,
    10.0f,                  gfx::V_LINE_TO, 10.0f,    gfx::H_LINE_TO,
    5.0f,                   gfx::CLOSE, gfx::MOVE_TO, 14.0f,
    5.0f,                   gfx::H_LINE_TO, 19.0f,    gfx::V_LINE_TO,
    10.0f,                  gfx::H_LINE_TO, 14.0f,    gfx::CLOSE,
    gfx::MOVE_TO,           5.0f,      14.0f,         gfx::H_LINE_TO,
    10.0f,                  gfx::V_LINE_TO, 19.0f,    gfx::H_LINE_TO,
    5.0f,                   gfx::CLOSE, gfx::MOVE_TO, 14.0f,
    14.0f,                  gfx::H_LINE_TO, 19.0f,    gfx::V_LINE_TO,
    19.0f,                  gfx::H_LINE_TO, 14.0f,    gfx::CLOSE,
};

static constexpr gfx::PathElement kBoostsPath[] = {
    gfx::CANVAS_DIMENSIONS, 24,        gfx::STROKE,   1.8f,
    gfx::MOVE_TO,           13.0f,     4.0f,          gfx::LINE_TO,
    7.0f,                   12.5f,     gfx::H_LINE_TO, 11.0f,
    gfx::LINE_TO,           9.5f,      20.0f,         gfx::LINE_TO,
    17.0f,                  10.0f,     gfx::H_LINE_TO, 12.8f,
    gfx::LINE_TO,           13.0f,     4.0f,
};

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

constexpr char kMediaIconId[] = "kSidebarLibraryRailMediaIcon";
static constexpr gfx::VectorIconRep kMediaRepList[] = {{kMediaPath}};
constexpr gfx::VectorIcon kMediaIcon(kMediaRepList, 1, kMediaIconId);

constexpr char kDownloadsIconId[] = "kSidebarLibraryRailDownloadsIcon";
static constexpr gfx::VectorIconRep kDownloadsRepList[] = {{kDownloadsPath}};
constexpr gfx::VectorIcon kDownloadsIcon(kDownloadsRepList, 1,
                                         kDownloadsIconId);

constexpr char kEaselIconId[] = "kSidebarLibraryRailEaselIcon";
static constexpr gfx::VectorIconRep kEaselRepList[] = {{kEaselPath}};
constexpr gfx::VectorIcon kEaselIcon(kEaselRepList, 1, kEaselIconId);

constexpr char kSpacesIconId[] = "kSidebarLibraryRailSpacesIcon";
static constexpr gfx::VectorIconRep kSpacesRepList[] = {{kSpacesPath}};
constexpr gfx::VectorIcon kSpacesIcon(kSpacesRepList, 1, kSpacesIconId);

constexpr char kBoostsIconId[] = "kSidebarLibraryRailBoostsIcon";
static constexpr gfx::VectorIconRep kBoostsRepList[] = {{kBoostsPath}};
constexpr gfx::VectorIcon kBoostsIcon(kBoostsRepList, 1, kBoostsIconId);

struct RailCategorySpec {
  MahoSidebarLibraryRailView::Category category;
  std::u16string_view title;
  raw_ptr<const gfx::VectorIcon> icon;
};

constexpr std::array<RailCategorySpec, 6> kRailCategories = {{
    {MahoSidebarLibraryRailView::Category::kMedia, u"Media", &kMediaIcon},
    {MahoSidebarLibraryRailView::Category::kDownloads, u"Downloads",
     &kDownloadsIcon},
    {MahoSidebarLibraryRailView::Category::kEasels, u"Easels", &kEaselIcon},
    {MahoSidebarLibraryRailView::Category::kSpaces, u"Spaces", &kSpacesIcon},
    {MahoSidebarLibraryRailView::Category::kBoosts, u"Boosts", &kBoostsIcon},
    {MahoSidebarLibraryRailView::Category::kArchivedTabs, u"Archived Tabs",
     &kArchiveboxIcon},
}};

void StyleIconButton(views::ImageButton* button) {
  button->SetBackground(nullptr);
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetInstallFocusRingOnFocus(false);
  button->SetRequestFocusOnPress(false);
}

ui::ColorId GetRailItemForegroundColor(bool selected) {
  return selected ? ui::kColorSysOnSurface : ui::kColorSysOnSurfaceSubtle;
}

}  // namespace

const gfx::VectorIcon& GetMahoSidebarArchiveboxIcon() {
  return kArchiveboxIcon;
}

class MahoSidebarLibraryRailItemButton : public views::Button {
 public:
  MahoSidebarLibraryRailItemButton(MahoSidebarLibraryRailView::Category category,
                                   std::u16string title,
                                   const gfx::VectorIcon& icon,
                                   bool enabled,
                                   PressedCallback callback)
      : views::Button(std::move(callback)),
        category_(category),
        title_(std::move(title)),
        icon_(icon) {
    SetAccessibleName(title_);
    GetViewAccessibility().SetIsSelected(false);
    SetTooltipText(title_);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetRequestFocusOnPress(false);
    SetPreferredSize(gfx::Size(0, kRailItemHeightDp));

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(2, 0, 2, 0), kRailLabelTopSpacingDp));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    tile_ = AddChildView(std::make_unique<views::View>());
    tile_->SetPreferredSize(gfx::Size(kRailTileWidthDp, kRailTileHeightDp));
    auto* tile_layout = tile_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
    tile_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);
    tile_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    tile_->SetBorder(nullptr);

    icon_view_ = tile_->AddChildView(std::make_unique<views::ImageView>());
    icon_view_->SetImageSize(gfx::Size(kRailIconSizeDp, kRailIconSizeDp));

    label_ = AddChildView(std::make_unique<views::Label>(title_));
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetTextStyle(views::style::STYLE_BODY_5);
    label_->SetMultiLine(false);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);

    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kRailTileCornerRadiusDp);
    SetEnabled(enabled);
    UpdateAppearance();
  }

  MahoSidebarLibraryRailItemButton(const MahoSidebarLibraryRailItemButton&) =
      delete;
  MahoSidebarLibraryRailItemButton& operator=(
      const MahoSidebarLibraryRailItemButton&) = delete;
  ~MahoSidebarLibraryRailItemButton() override = default;

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
                                       ? ui::kColorSysOnSurface
                                       : GetRailItemForegroundColor(selected_);
    if (enabled && selected_) {
      tile_->SetBackground(views::CreateRoundedRectBackground(
          hovered ? ui::kColorSysSurface3 : ui::kColorSysSurface2,
          kRailTileCornerRadiusDp));
      tile_->SetBorder(views::CreateRoundedRectBorder(
          1, kRailTileCornerRadiusDp, ui::kColorSysSurfaceVariant));
    } else if (hovered) {
      tile_->SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysSurface1, kRailTileCornerRadiusDp));
      tile_->SetBorder(views::CreateRoundedRectBorder(
          1, kRailTileCornerRadiusDp, ui::kColorSysSurfaceVariant));
    } else {
      tile_->SetBackground(nullptr);
      tile_->SetBorder(views::CreateEmptyBorder(1));
    }

    icon_view_->SetImage(ui::ImageModel::FromVectorIcon(icon_.get(), foreground,
                                                        kRailIconSizeDp));
    label_->SetEnabledColor(foreground);
    SetPaintToLayer();
    layer()->SetOpacity(enabled ? 1.0f : 0.6f);
    SetCanProcessEventsWithinSubtree(enabled);
  }

  const MahoSidebarLibraryRailView::Category category_;
  const std::u16string title_;
  const base::raw_ref<const gfx::VectorIcon> icon_;
  raw_ptr<views::View> tile_ = nullptr;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  bool selected_ = false;
};

BEGIN_METADATA(MahoSidebarLibraryRailView)
END_METADATA

MahoSidebarLibraryRailView::MahoSidebarLibraryRailView() {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(kRailTopInsetDp, kRailSideInsetDp, kRailBottomInsetDp,
                        kRailSideInsetDp),
      0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  SetPreferredSize(gfx::Size(kRailWidthDp, 0));
  SetBackground(views::CreateSolidBackground(ui::kColorSysSurface));

  item_container_ = AddChildView(std::make_unique<views::View>());
  auto* item_layout = item_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kRailItemSpacingDp));
  item_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  for (const auto& spec : kRailCategories) {
    auto button = std::make_unique<MahoSidebarLibraryRailItemButton>(
        spec.category, std::u16string(spec.title), *spec.icon, true,
        base::BindRepeating(&MahoSidebarLibraryRailView::HandleCategoryPressed,
                            weak_factory_.GetWeakPtr(), spec.category));
    auto* button_ptr = static_cast<MahoSidebarLibraryRailItemButton*>(
        item_container_->AddChildView(
            std::unique_ptr<views::View>(button.release())));
    category_buttons_.push_back(button_ptr);
  }

  auto* spacer = AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(spacer, 1);

  auto* back_button_host = AddChildView(std::make_unique<views::View>());
  auto* back_button_layout = back_button_host->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(0, kRailBackButtonLeadingInsetDp, 0, 0), 0));
  back_button_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  back_button_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  auto back_button = views::CreateVectorImageButton(base::BindRepeating(
      &MahoSidebarLibraryRailView::HandleBackPressed,
      weak_factory_.GetWeakPtr()));
  back_button->SetPreferredSize(
      gfx::Size(kRailBackButtonSizeDp, kRailBackButtonSizeDp));
  back_button->SetTooltipText(u"Back");
  back_button->SetAccessibleName(u"Back");
  views::InstallCircleHighlightPathGenerator(back_button.get());
  StyleIconButton(back_button.get());
  back_button_ = back_button_host->AddChildView(std::move(back_button));

  UpdateSelection();
  UpdateBackButtonAppearance();
}

MahoSidebarLibraryRailView::~MahoSidebarLibraryRailView() = default;

views::Button* MahoSidebarLibraryRailView::button_for_category_for_testing(
    Category category) {
  for (auto& button : category_buttons_) {
    if (button && button->category() == category) {
      return button;
    }
  }
  return nullptr;
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
  SetBackground(views::CreateSolidBackground(ui::kColorSysSurface));
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
  back_button_->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface1, kRailBackButtonCornerRadiusDp));
  back_button_->SetBorder(views::CreateRoundedRectBorder(
      1, kRailBackButtonCornerRadiusDp, ui::kColorSysSurfaceVariant));
  back_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          vector_icons::kBackArrowIcon,
          enabled ? ui::kColorSysOnSurface : ui::kColorSysOnSurfaceSubtle,
          14));
  back_button_->SetPaintToLayer();
  back_button_->layer()->SetOpacity(enabled ? 1.0f : kBackButtonDisabledAlpha);
}

}  // namespace maho
