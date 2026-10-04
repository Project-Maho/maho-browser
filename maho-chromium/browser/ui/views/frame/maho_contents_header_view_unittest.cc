// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/frame/maho_contents_header_view.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h"
#include "maho/browser/ui/views/sidebar/maho_translate_popover_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_drawing_test_utils.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

#if !BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#endif

namespace maho {
namespace {

void Click(views::Button* button) {
  views::test::ButtonTestApi(button).NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, 0));
}

MahoSidebarPalette MakeLightPalette();

// Highest alpha across a rasterized button image: how strongly its glyph
// draws.
int MaxAlpha(const gfx::ImageSkia& image) {
  const SkBitmap* bitmap = image.bitmap();
  int max_alpha = 0;
  if (!bitmap) {
    return max_alpha;
  }
  for (int y = 0; y < bitmap->height(); ++y) {
    for (int x = 0; x < bitmap->width(); ++x) {
      max_alpha =
          std::max(max_alpha, static_cast<int>(SkColorGetA(bitmap->getColor(x, y))));
    }
  }
  return max_alpha;
}

class MahoContentsHeaderViewTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    widget_ =
        CreateTestWidget(views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
    auto* root = widget_->SetContentsView(std::make_unique<views::View>());
    root->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    web_view_ = root->AddChildView(std::make_unique<ContentsWebView>(&profile_));
    header_ = root->AddChildView(std::make_unique<MahoContentsHeaderView>(
        /*browser_view=*/nullptr, web_view_));
    web_contents_ =
        content::WebContentsTester::CreateTestWebContents(&profile_, nullptr);
    web_view_->SetWebContents(web_contents_.get());
  }

  void TearDown() override {
    header_ = nullptr;
    web_view_ = nullptr;
    widget_.reset();
    other_web_contents_.reset();
    web_contents_.reset();
    ChromeViewsTestBase::TearDown();
  }

 protected:
  void NavigateAndCommit(content::WebContents* contents, const char* url) {
    content::WebContentsTester::For(contents)->NavigateAndCommit(GURL(url));
  }

  content::RenderViewHostTestEnabler rvh_test_enabler_;
  TestingProfile profile_;
  std::unique_ptr<content::WebContents> web_contents_;
  std::unique_ptr<content::WebContents> other_web_contents_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<ContentsWebView> web_view_ = nullptr;
  raw_ptr<MahoContentsHeaderView> header_ = nullptr;
};

TEST(MahoContentsHeaderViewStaticTest, BuildHostTextStripsWww) {
  EXPECT_EQ(MahoContentsHeaderView::BuildHostText(
                GURL("https://www.example.com/a")),
            u"example.com");
  EXPECT_EQ(
      MahoContentsHeaderView::BuildHostText(GURL("https://docs.example.com/")),
      u"docs.example.com");
  EXPECT_EQ(MahoContentsHeaderView::BuildHostText(GURL()),
            u"Search or Enter URL...");
}

TEST(MahoContentsHeaderViewStaticTest, BuildTranslateTextStates) {
  EXPECT_EQ(MahoContentsHeaderView::BuildTranslateText(false, false), u"");
  EXPECT_EQ(MahoContentsHeaderView::BuildTranslateText(false, true),
            u"Translation Available");
  EXPECT_EQ(MahoContentsHeaderView::BuildTranslateText(true, true),
            u"Translated");
  EXPECT_EQ(MahoContentsHeaderView::BuildTranslateText(true, false),
            u"Translated");
}

TEST(MahoContentsHeaderViewStaticTest, InternalPagesHaveNoSecurityIcon) {
  EXPECT_FALSE(MahoContentsHeaderView::ShouldShowSecurityIcon(
      GURL("chrome://settings")));
  EXPECT_FALSE(
      MahoContentsHeaderView::ShouldShowSecurityIcon(GURL("about:blank")));
  EXPECT_TRUE(MahoContentsHeaderView::ShouldShowSecurityIcon(
      GURL("https://example.com")));
  EXPECT_TRUE(MahoContentsHeaderView::ShouldShowSecurityIcon(
      GURL("http://example.com")));
}

TEST_F(MahoContentsHeaderViewTest, PreferredHeightIsHeaderConstant) {
  EXPECT_EQ(header_->GetPreferredSize().height(), kMahoContentsHeaderHeightDp);
}

TEST_F(MahoContentsHeaderViewTest, BackDisabledOnFreshContents) {
  EXPECT_FALSE(header_->back_button_for_testing()->GetEnabled());
  EXPECT_FALSE(header_->forward_button_for_testing()->GetEnabled());
  EXPECT_FALSE(header_->security_icon_for_testing()->GetVisible());

  // F3 D3: a disabled nav glyph must draw visibly dimmer than an enabled one,
  // both with the sidebar palette and with the native (no palette) colours.
  for (bool with_palette : {false, true}) {
    if (with_palette) {
      header_->SetPalette(MakeLightPalette());
    }
    views::ImageButton* back = header_->back_button_for_testing();
    const int enabled_alpha =
        MaxAlpha(back->GetImage(views::Button::STATE_NORMAL));
    const int disabled_alpha =
        MaxAlpha(back->GetImage(views::Button::STATE_DISABLED));
    ASSERT_GT(enabled_alpha, 0) << "palette=" << with_palette;
    EXPECT_LT(disabled_alpha, enabled_alpha) << "palette=" << with_palette;
    if (with_palette) {
      EXPECT_LE(disabled_alpha * 2, enabled_alpha)
          << "disabled glyph must be clearly dimmed";
    }
  }
}

// F3 D1: the security glyph is a button that opens page info for this pane's
// WebContents, anchored to the glyph itself.
TEST_F(MahoContentsHeaderViewTest, SecurityIconPressOpensPageInfoForPane) {
  content::WebContents* opened_for = nullptr;
  views::View* opened_anchor = nullptr;
  int open_count = 0;
  header_->set_page_info_opener_for_testing(base::BindLambdaForTesting(
      [&](content::WebContents* contents, views::View* anchor) {
        opened_for = contents;
        opened_anchor = anchor;
        ++open_count;
      }));
  NavigateAndCommit(web_contents_.get(), "http://example.com/");
  views::ImageButton* security = header_->security_icon_for_testing();
  ASSERT_TRUE(security->GetVisible());
  EXPECT_EQ(security->GetViewAccessibility().GetCachedName(), u"Not secure");

  Click(security);

  EXPECT_EQ(open_count, 1);
  EXPECT_EQ(opened_for, web_contents_.get());
  EXPECT_EQ(opened_anchor, security);
  EXPECT_EQ(opened_anchor, header_->security_anchor());
}

TEST_F(MahoContentsHeaderViewTest, InternalPageHidesSecurityButton) {
  NavigateAndCommit(web_contents_.get(), "data:text/html,internal");
  EXPECT_FALSE(header_->security_icon_for_testing()->GetVisible());
}

// F3 D2: the translate popover is wider than its anchor, so an anchor in the
// trailing half of the window must grow it toward the leading edge.
TEST_F(MahoContentsHeaderViewTest, TranslatePopoverArrowFollowsAnchorSide) {
  widget_->SetBounds(gfx::Rect(0, 0, 400, 200));
  views::test::RunScheduledLayout(widget_.get());
  EXPECT_EQ(MahoTranslatePopoverView::ArrowForAnchorForTesting(
                header_->copy_url_button_for_testing()),
            views::BubbleBorder::TOP_RIGHT);
  EXPECT_EQ(MahoTranslatePopoverView::ArrowForAnchorForTesting(
                header_->back_button_for_testing()),
            views::BubbleBorder::TOP_LEFT);
}

TEST_F(MahoContentsHeaderViewTest, BackEnabledAfterTwoNavigations) {
  NavigateAndCommit(web_contents_.get(), "https://www.example.com/one");
  NavigateAndCommit(web_contents_.get(), "https://www.example.com/two");

  EXPECT_TRUE(header_->back_button_for_testing()->GetEnabled());
  EXPECT_FALSE(header_->forward_button_for_testing()->GetEnabled());
  EXPECT_EQ(header_->host_button_for_testing()->GetText(), u"example.com");
}

TEST_F(MahoContentsHeaderViewTest, ReturnKeyOnHostButtonActivatesIt) {
  // views::Button only clicks on Return when
  // PlatformStyle::kReturnClicksFocusedControl is true, which is false on
  // macOS (ui/views/style/platform_style.h). host_button_ handles
  // VKEY_RETURN itself (see HeaderHostButton::OnKeyPressed in the .cc), so
  // this must pass on every platform, including Mac.
  views::LabelButton* host_button = header_->host_button_for_testing();
  ASSERT_TRUE(host_button->GetWidget());
  host_button->RequestFocus();

  ui::KeyEvent return_press(ui::EventType::kKeyPressed, ui::VKEY_RETURN,
                            ui::EF_NONE);
  EXPECT_TRUE(host_button->OnKeyPressed(return_press));
}

TEST_F(MahoContentsHeaderViewTest, GoBackActsOnPaneWebContents) {
  other_web_contents_ =
      content::WebContentsTester::CreateTestWebContents(&profile_, nullptr);
  NavigateAndCommit(web_contents_.get(), "https://a.example/1");
  NavigateAndCommit(web_contents_.get(), "https://a.example/2");
  NavigateAndCommit(other_web_contents_.get(), "https://b.example/1");
  NavigateAndCommit(other_web_contents_.get(), "https://b.example/2");

  Click(header_->back_button_for_testing());

  EXPECT_EQ(web_contents_->GetController().GetPendingEntryIndex(), 0);
  EXPECT_EQ(other_web_contents_->GetController().GetPendingEntryIndex(), -1);
  EXPECT_EQ(other_web_contents_->GetController().GetLastCommittedEntryIndex(),
            1);
}

TEST_F(MahoContentsHeaderViewTest, InsecurePageShowsSecurityIcon) {
  NavigateAndCommit(web_contents_.get(), "http://example.com/");

  EXPECT_TRUE(header_->security_icon_for_testing()->GetVisible());
  EXPECT_EQ(header_->security_icon_for_testing()->GetTooltipText(),
            u"Not secure");
}

TEST_F(MahoContentsHeaderViewTest, WebPageOffersTranslation) {
  NavigateAndCommit(web_contents_.get(), "https://example.com/");

  EXPECT_TRUE(header_->translate_label_for_testing()->GetVisible());
  EXPECT_EQ(header_->translate_label_for_testing()->GetText(),
            u"Translation Available");
}

TEST_F(MahoContentsHeaderViewTest, OnlyActiveHeaderCarriesTranslateElementId) {
  views::LabelButton* translate = header_->translate_label_for_testing();
  ASSERT_TRUE(header_->is_active_for_testing());
  EXPECT_EQ(translate->GetProperty(views::kElementIdentifierKey),
            kTranslatePageActionElementId);

  header_->SetActive(false);
  EXPECT_EQ(translate->GetProperty(views::kElementIdentifierKey),
            ui::ElementIdentifier());

  header_->SetActive(true);
  EXPECT_EQ(translate->GetProperty(views::kElementIdentifierKey),
            kTranslatePageActionElementId);
}

// A light single-stop palette shaped like ResolveMahoSidebarPalette() output
// for a pastel space (opaque stop, light-family text roles and row tints).
// Secondary text meets 4.5:1 on both the plain and the selected-row surface,
// as the resolver guarantees via selected_contrast_surfaces.
MahoSidebarPalette MakeLightPalette() {
  MahoSidebarPalette palette;
  const SkColor surface = SkColorSetRGB(236, 214, 232);
  palette.surface_stops = {surface};
  palette.opaque_contrast_stops = {surface};
  palette.content_surface_stops = {surface};
  palette.row_selected = SkColorSetARGB(25, 0, 0, 0);
  palette.primary_text = SkColorSetRGB(45, 51, 74);
  palette.secondary_text = SkColorSetRGB(72, 78, 104);
  palette.tertiary_text = SkColorSetRGB(72, 78, 104);
  palette.disabled_text = SkColorSetRGB(72, 78, 104);
  palette.neutral_glyph = SkColorSetRGB(72, 78, 104);
  return palette;
}

// Samples the header's own background in the leading inset, where no child
// paints.
SkColor HeaderBackgroundPixel(MahoContentsHeaderView* header) {
  const SkBitmap bitmap = views::test::PaintViewToBitmap(header);
  return bitmap.getColor(1, bitmap.height() / 2);
}

TEST_F(MahoContentsHeaderViewTest, PaintsOwningSidebarPaletteSurface) {
  widget_->SetBounds(gfx::Rect(0, 0, 400, 200));
  views::test::RunScheduledLayout(widget_.get());
  const MahoSidebarPalette palette = MakeLightPalette();
  header_->SetPalette(palette);

  const SkColor drawn = HeaderBackgroundPixel(header_);
  EXPECT_EQ(drawn, palette.opaque_contrast_stops.front());
  EXPECT_GE(color_utils::GetContrastRatio(
                header_->host_button_for_testing()->GetCurrentTextColor(),
                drawn),
            color_utils::kMinimumReadableContrastRatio);
}

TEST_F(MahoContentsHeaderViewTest, InactiveHeaderIsVisiblyDistinct) {
  widget_->SetBounds(gfx::Rect(0, 0, 400, 200));
  views::test::RunScheduledLayout(widget_.get());
  const MahoSidebarPalette palette = MakeLightPalette();
  header_->SetPalette(palette);
  NavigateAndCommit(web_contents_.get(), "https://example.com/");

  const SkColor active_background = HeaderBackgroundPixel(header_);
  const SkColor active_text =
      header_->host_button_for_testing()->GetCurrentTextColor();

  header_->SetActive(false);
  const SkColor inactive_background = HeaderBackgroundPixel(header_);
  const SkColor inactive_text =
      header_->host_button_for_testing()->GetCurrentTextColor();

  EXPECT_NE(active_background, inactive_background);
  // Skia and color_utils round the blend independently; allow 1 per channel.
  const SkColor recessed = color_utils::GetResultingPaintColor(
      palette.row_selected, palette.opaque_contrast_stops.front());
  EXPECT_NEAR(SkColorGetR(inactive_background), SkColorGetR(recessed), 1);
  EXPECT_NEAR(SkColorGetG(inactive_background), SkColorGetG(recessed), 1);
  EXPECT_NEAR(SkColorGetB(inactive_background), SkColorGetB(recessed), 1);
  EXPECT_EQ(active_text, palette.primary_text);
  EXPECT_EQ(inactive_text, palette.secondary_text);
  // The inactive pane must still be readable.
  EXPECT_GE(color_utils::GetContrastRatio(inactive_text, inactive_background),
            color_utils::kMinimumReadableContrastRatio);

  header_->SetActive(true);
  EXPECT_EQ(HeaderBackgroundPixel(header_), active_background);
}

// F2 N26: forced colors drop the inactive tint and the text swap, so the
// active split pane must still differ from the inactive one in drawn pixels,
// through a cue that is not a tint.
TEST_F(MahoContentsHeaderViewTest, ForcedColorsActiveSplitPaneHasCue) {
  widget_->SetBounds(gfx::Rect(0, 0, 400, 200));
  views::test::RunScheduledLayout(widget_.get());
  MahoSidebarPalette palette;
  const SkColor surface = SK_ColorBLACK;
  palette.forced_colors = true;
  palette.surface_stops = {surface};
  palette.opaque_contrast_stops = {surface};
  palette.content_surface_stops = {surface};
  palette.row_selected = SK_ColorTRANSPARENT;
  palette.primary_text = SK_ColorWHITE;
  palette.secondary_text = SK_ColorWHITE;
  palette.disabled_text = SK_ColorGRAY;
  palette.neutral_glyph = SK_ColorWHITE;
  palette.focus_ring = SK_ColorYELLOW;
  header_->SetPalette(palette);
  header_->set_in_split_for_testing(true);

  auto bottom_pixel = [this] {
    const SkBitmap bitmap = views::test::PaintViewToBitmap(header_);
    return bitmap.getColor(1, bitmap.height() - 1);
  };
  header_->SetActive(true);
  const SkColor active = bottom_pixel();
  header_->SetActive(false);
  const SkColor inactive = bottom_pixel();

  EXPECT_EQ(active, palette.focus_ring);
  EXPECT_EQ(inactive, surface);
  EXPECT_NE(active, inactive);

  // Outside a split the single pane carries no cue.
  header_->set_in_split_for_testing(false);
  header_->SetActive(true);
  EXPECT_EQ(bottom_pixel(), surface);
}

class MahoContentsHeaderWindowControlsTest : public MahoContentsHeaderViewTest {
 public:
  void SetUp() override {
    MahoContentsHeaderViewTest::SetUp();
    widget_->SetBounds(gfx::Rect(0, 0, 400, 200));
  }

  void TearDown() override {
    ClearWindowControlsRectForTesting();
    MahoContentsHeaderViewTest::TearDown();
  }

 protected:
  void Relayout() {
    header_->InvalidateLayout();
    views::test::RunScheduledLayout(widget_.get());
  }
};

TEST_F(MahoContentsHeaderWindowControlsTest, LeadingControlsPushChildrenPast) {
  SetWindowControlsRectForTesting(gfx::Rect(0, 0, 80, 24));
  Relayout();

  ASSERT_EQ(header_->width(), 400);
  EXPECT_GE(header_->back_button_for_testing()->x(), 80);
  for (views::View* child : header_->children()) {
    if (child->GetVisible()) {
      EXPECT_GE(child->x(), 80) << child->GetClassName();
    }
  }
}

TEST_F(MahoContentsHeaderWindowControlsTest, TrailingControlsKeepChildrenBefore) {
  SetWindowControlsRectForTesting(gfx::Rect(320, 0, 80, 24));
  Relayout();

  ASSERT_EQ(header_->width(), 400);
  EXPECT_LE(header_->copy_url_button_for_testing()->bounds().right(), 320);
  EXPECT_LT(header_->back_button_for_testing()->x(), 80);
}

TEST_F(MahoContentsHeaderWindowControlsTest, NulloptOverrideLeavesZeroInset) {
  SetWindowControlsRectForTesting(gfx::Rect(0, 0, 80, 24));
  Relayout();
  const int inset_x = header_->back_button_for_testing()->x();
  ASSERT_GE(inset_x, 80);

  SetWindowControlsRectForTesting(std::nullopt);
  Relayout();
  EXPECT_LT(header_->back_button_for_testing()->x(), 80);
  EXPECT_LT(header_->back_button_for_testing()->x(), inset_x);
}

#if !BUILDFLAG(IS_MAC)
// Only Linux/Windows window managers can place caption-button clusters on
// both edges at once; SetWindowControlsRectsForTesting() on Mac unions its
// inputs into the single leading-edge cluster the real Mac measurement path
// can ever produce, so this scenario cannot be exercised on Mac.
TEST_F(MahoContentsHeaderWindowControlsTest, SplitControlsInsetBothEdges) {
  // Linux window managers can put caption buttons on both edges.
  SetWindowControlsRectsForTesting(
      {gfx::Rect(0, 0, 80, 24), gfx::Rect(320, 0, 80, 24)});
  Relayout();

  ASSERT_EQ(header_->width(), 400);
  for (views::View* child : header_->children()) {
    if (child->GetVisible()) {
      EXPECT_GE(child->x(), 80) << child->GetClassName();
      EXPECT_LE(child->bounds().right(), 320) << child->GetClassName();
    }
  }
}
#endif  // !BUILDFLAG(IS_MAC)

#if !BUILDFLAG(IS_MAC)
TEST(MahoWindowControlsLayoutParamsTest, MapsBothExclusionsToFrameRects) {
  BrowserLayoutParams params;
  params.visual_client_area = gfx::Rect(10, 5, 400, 300);
  params.leading_exclusion.content = gfx::SizeF(60, 20);
  params.leading_exclusion.horizontal_padding = 4;
  params.trailing_exclusion.content = gfx::SizeF(90, 20);
  params.trailing_exclusion.horizontal_padding = 4;
  params.trailing_exclusion.vertical_padding = 2;

  EXPECT_EQ(GetWindowControlsRectsFromLayoutParams(params),
            (std::vector<gfx::Rect>{gfx::Rect(10, 5, 64, 20),
                                    gfx::Rect(316, 5, 94, 22)}));
}

TEST(MahoWindowControlsLayoutParamsTest, OmitsEmptyExclusions) {
  BrowserLayoutParams params;
  params.visual_client_area = gfx::Rect(0, 0, 400, 300);
  EXPECT_TRUE(GetWindowControlsRectsFromLayoutParams(params).empty());

  params.trailing_exclusion.content = gfx::SizeF(90, 20);
  EXPECT_EQ(GetWindowControlsRectsFromLayoutParams(params),
            (std::vector<gfx::Rect>{gfx::Rect(310, 0, 90, 20)}));
}

// A plain test widget has no BrowserFrameView, so measurement must yield no
// controls instead of reaching any frame-specific caption-button API.
TEST_F(MahoContentsHeaderViewTest, NonBrowserFrameYieldsNoControls) {
  ClearWindowControlsRectForTesting();
  EXPECT_TRUE(GetWindowControlsRectsInView(header_).empty());
  EXPECT_FALSE(GetWindowControlsRectInView(header_).has_value());
}
#endif  // !BUILDFLAG(IS_MAC)

// Replaces MahoSidebarContractTest.BuildSearchModelNullBrowserHasNoActiveTab:
// with no pane WebContents the header shows the placeholder and disables the
// controls that need a page.
TEST_F(MahoContentsHeaderViewTest, DetachedContentsShowsPlaceholder) {
  web_view_->SetWebContents(nullptr);

  EXPECT_EQ(header_->host_button_for_testing()->GetText(),
            u"Search or Enter URL...");
  EXPECT_FALSE(header_->utility_icon_for_testing()->GetEnabled());
  EXPECT_FALSE(header_->copy_url_button_for_testing()->GetEnabled());
  EXPECT_FALSE(header_->back_button_for_testing()->GetEnabled());
  EXPECT_FALSE(header_->security_icon_for_testing()->GetVisible());
}

TEST_F(MahoContentsHeaderViewTest, AnchorsResolveToHeaderControls) {
  EXPECT_EQ(header_->utility_anchor(), header_->utility_icon_for_testing());
  EXPECT_EQ(header_->security_anchor(), header_->security_icon_for_testing());
  // Hidden translate label falls back to the utility icon.
  header_->translate_label_for_testing()->SetVisible(false);
  EXPECT_EQ(header_->translate_anchor(), header_->utility_icon_for_testing());
}

}  // namespace
}  // namespace maho
