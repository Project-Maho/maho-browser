// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"

#include <memory>
#include <string>

#include "maho/browser/ui/theme/maho_theme_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace maho {
namespace {

using Category = MahoSidebarLibraryRailView::Category;

MahoSidebarPalette MakePalette() {
  MahoSidebarPalette palette;
  palette.primary_text = SkColorSetRGB(0x10, 0x20, 0x30);
  palette.secondary_text = SkColorSetRGB(0x40, 0x50, 0x60);
  palette.disabled_text = SkColorSetRGB(0x70, 0x80, 0x90);
  palette.row_selected = SkColorSetRGB(0xA0, 0xB0, 0xC0);
  palette.row_hover = SkColorSetRGB(0xD0, 0xE0, 0xF0);
  palette.focus_ring = SkColorSetRGB(0x0B, 0x57, 0xD0);
  return palette;
}

DownloadsIndicatorState MakeActiveState(int active_count,
                                         uint64_t received_bytes,
                                         uint64_t total_bytes) {
  DownloadsIndicatorState state;
  state.visible = true;
  state.active_count = active_count;
  state.received_bytes = received_bytes;
  state.total_bytes = total_bytes;
  state.fraction = static_cast<double>(received_bytes) /
                   static_cast<double>(total_bytes);
  return state;
}

ui::AXNodeData AccessibilityDataFor(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

std::u16string AccessibilityDescriptionFor(views::View* view) {
  return AccessibilityDataFor(view).GetString16Attribute(
      ax::mojom::StringAttribute::kDescription);
}

SkColor ResolvedBackgroundColor(views::View* view) {
  return view->background()->color().ResolveToSkColor(/*color_provider=*/nullptr);
}

class MahoSidebarLibraryRailViewTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params = CreateParams(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_POPUP);
    params.bounds = gfx::Rect(0, 0, 92, 600);
    widget_->Init(std::move(params));
    rail_ = widget_->SetContentsView(
        std::make_unique<MahoSidebarLibraryRailView>());
    rail_->SetBounds(0, 0, 92, 600);
    rail_->OnSidebarPaletteChanged(MakePalette());
    LayOutRail();
  }

  void TearDown() override {
    rail_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  void LayOutRail() {
    rail_->InvalidateLayout();
    rail_->DeprecatedLayoutImmediately();
  }

  views::Button* DownloadsButton() {
    return rail_->button_for_category_for_testing(Category::kDownloads);
  }

  views::View* DownloadsIndicator() {
    return rail_->downloads_indicator_for_testing();
  }

  views::View* DownloadsIndicatorFill() {
    views::View* indicator = DownloadsIndicator();
    return indicator && !indicator->children().empty()
               ? indicator->children()[0].get()
               : nullptr;
  }

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<MahoSidebarLibraryRailView> rail_ = nullptr;
};

TEST_F(MahoSidebarLibraryRailViewTest, IndicatorIsClearedWithoutActiveDownloads) {
  ASSERT_NE(nullptr, DownloadsButton());
  ASSERT_NE(nullptr, DownloadsIndicator());

  EXPECT_FALSE(DownloadsIndicator()->GetVisible());
  EXPECT_EQ(0, rail_->downloads_indicator_fill_width_for_testing());
  EXPECT_TRUE(AccessibilityDescriptionFor(DownloadsButton()).empty());
}

TEST_F(MahoSidebarLibraryRailViewTest, IndicatorShowsDeterminateProgress) {
  rail_->SetDownloadsIndicatorState(MakeActiveState(1, 250, 1000));
  LayOutRail();

  ASSERT_TRUE(DownloadsIndicator()->GetVisible());
  // 24dp indicator at 25% => a 6dp fill.
  EXPECT_EQ(6, rail_->downloads_indicator_fill_width_for_testing());
  EXPECT_EQ(MakePalette().row_selected,
            ResolvedBackgroundColor(DownloadsIndicator()));
  EXPECT_EQ(MakePalette().focus_ring,
            ResolvedBackgroundColor(DownloadsIndicatorFill()));
  EXPECT_NE(std::u16string::npos,
            AccessibilityDescriptionFor(DownloadsButton())
                .find(u"25% complete"));
}

TEST_F(MahoSidebarLibraryRailViewTest, IndicatorShowsIndeterminateBand) {
  DownloadsIndicatorState state;
  state.visible = true;
  state.indeterminate = true;
  state.active_count = 1;
  rail_->SetDownloadsIndicatorState(state);
  LayOutRail();

  ASSERT_TRUE(DownloadsIndicator()->GetVisible());
  // Unknown size: the band spans the whole track at reduced alpha.
  EXPECT_EQ(24, rail_->downloads_indicator_fill_width_for_testing());
  EXPECT_EQ(SkColorSetA(MakePalette().focus_ring, 0x66),
            ResolvedBackgroundColor(DownloadsIndicatorFill()));
  EXPECT_NE(std::u16string::npos,
            AccessibilityDescriptionFor(DownloadsButton())
                .find(u"size unknown"));
}

TEST_F(MahoSidebarLibraryRailViewTest, ClearingTheIndicatorHidesIt) {
  rail_->SetDownloadsIndicatorState(MakeActiveState(2, 500, 1000));
  LayOutRail();
  ASSERT_TRUE(DownloadsIndicator()->GetVisible());

  rail_->SetDownloadsIndicatorState(DownloadsIndicatorState());
  LayOutRail();

  EXPECT_FALSE(DownloadsIndicator()->GetVisible());
  EXPECT_EQ(0, rail_->downloads_indicator_fill_width_for_testing());
  EXPECT_TRUE(AccessibilityDescriptionFor(DownloadsButton()).empty());
  EXPECT_FALSE(rail_->downloads_indicator_state_for_testing().visible);
}

TEST_F(MahoSidebarLibraryRailViewTest, PaletteChangeRepaintsTheIndicator) {
  rail_->SetDownloadsIndicatorState(MakeActiveState(1, 250, 1000));
  LayOutRail();
  ASSERT_EQ(MakePalette().focus_ring,
            ResolvedBackgroundColor(DownloadsIndicatorFill()));

  MahoSidebarPalette updated = MakePalette();
  updated.focus_ring = SkColorSetRGB(0x11, 0x22, 0x33);
  updated.row_selected = SkColorSetRGB(0x44, 0x55, 0x66);
  rail_->OnSidebarPaletteChanged(updated);
  LayOutRail();

  EXPECT_EQ(updated.row_selected, ResolvedBackgroundColor(DownloadsIndicator()));
  EXPECT_EQ(updated.focus_ring,
            ResolvedBackgroundColor(DownloadsIndicatorFill()));
}

TEST_F(MahoSidebarLibraryRailViewTest,
       SelectedCategoryAppearanceSurvivesTheIndicator) {
  rail_->SetSelectedCategory(Category::kDownloads);
  rail_->SetDownloadsIndicatorState(MakeActiveState(1, 250, 1000));
  LayOutRail();

  ASSERT_TRUE(DownloadsIndicator()->GetVisible());
  EXPECT_EQ(MakePalette().row_selected, ResolvedBackgroundColor(DownloadsButton()));
  EXPECT_TRUE(AccessibilityDataFor(DownloadsButton())
                  .GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_NE(std::u16string::npos,
            AccessibilityDescriptionFor(DownloadsButton()).find(u"25%"));
}

TEST_F(MahoSidebarLibraryRailViewTest, IndicatorKeepsRailItemGeometryAndFocus) {
  views::Button* downloads_button = DownloadsButton();
  const gfx::Size cleared_size = downloads_button->GetPreferredSize();
  ASSERT_NE(nullptr,
            downloads_button->GetProperty(views::kHighlightPathGeneratorKey));

  rail_->SetDownloadsIndicatorState(MakeActiveState(1, 250, 1000));
  LayOutRail();

  ASSERT_TRUE(DownloadsIndicator()->GetVisible());
  // Showing progress must not reflow the rail stack or steal the focus ring.
  EXPECT_EQ(cleared_size, downloads_button->GetPreferredSize());
  EXPECT_EQ(80, downloads_button->GetPreferredSize().width());
  EXPECT_EQ(views::View::FocusBehavior::ALWAYS,
            downloads_button->GetFocusBehavior());
  EXPECT_NE(nullptr,
            downloads_button->GetProperty(views::kHighlightPathGeneratorKey));
}

TEST_F(MahoSidebarLibraryRailViewTest, IndicatorSitsBetweenIconAndLabel) {
  rail_->SetDownloadsIndicatorState(MakeActiveState(1, 250, 1000));
  LayOutRail();

  views::Button* downloads_button = DownloadsButton();
  views::View* indicator = DownloadsIndicator();
  ASSERT_TRUE(indicator->GetVisible());

  views::View* icon = nullptr;
  views::View* label = nullptr;
  for (const auto& child : downloads_button->children()) {
    if (child.get() == indicator) {
      continue;
    }
    if (views::IsViewClass<views::ImageView>(child.get())) {
      icon = child.get();
    } else if (views::IsViewClass<views::Label>(child.get())) {
      label = child.get();
    }
  }
  ASSERT_NE(nullptr, icon);
  ASSERT_NE(nullptr, label);

  EXPECT_GE(indicator->bounds().y(), icon->bounds().bottom());
  EXPECT_LE(indicator->bounds().bottom(), label->bounds().y());
  EXPECT_EQ(icon->bounds().CenterPoint().x(),
            indicator->bounds().CenterPoint().x());
}

TEST_F(MahoSidebarLibraryRailViewTest, OtherCategoriesHaveNoIndicator) {
  for (const Category category : {Category::kArchivedTabs, Category::kSpaces,
                                  Category::kMedia}) {
    views::Button* button = rail_->button_for_category_for_testing(category);
    ASSERT_NE(nullptr, button);
    // Icon + label only: the indicator is exclusive to Downloads.
    EXPECT_EQ(2u, button->children().size())
        << "category " << static_cast<int>(category);
  }
}

}  // namespace
}  // namespace maho
