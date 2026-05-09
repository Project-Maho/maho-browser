// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>

#include "base/test/run_until.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/widget/widget.h"

namespace {

class UtilityPanelHostView : public views::View,
                             public views::WidgetObserver {
  METADATA_HEADER(UtilityPanelHostView, views::View)

 public:
  UtilityPanelHostView() {
    SetPreferredSize(gfx::Size(200, 120));

    maho::MahoLocationBarUtilityPanelAction quick_action;
    quick_action.title = u"Screenshot";
    quick_action.subtitle = u"Open utility panel";
    quick_action.accessible_name = u"Screenshot — Open utility panel";
    quick_action.enabled = true;
    quick_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kLive;
    model_.quick_actions.push_back(quick_action);

    model_.footer.title = u"Security state available";
    model_.footer.subtitle = u"Page security";
    model_.footer.primary_action_label = u"Close";
    model_.footer.close_after_primary_action = true;

    icon_ = AddChildView(std::make_unique<maho::MahoLocationBarUtilityIconView>(
        base::BindRepeating(&UtilityPanelHostView::ShowPanel,
                            base::Unretained(this))));
    icon_->SetBounds(90, 50, 20, 20);
    icon_->SetHoverVisible(false);
  }

  UtilityPanelHostView(const UtilityPanelHostView&) = delete;
  UtilityPanelHostView& operator=(const UtilityPanelHostView&) = delete;

  ~UtilityPanelHostView() override = default;

  void OnMouseEntered(const ui::MouseEvent& event) override {
    icon_->SetHoverVisible(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    icon_->SetHoverVisible(false);
  }

  void OnWidgetDestroyed(views::Widget* widget) override {
    if (widget == panel_widget_) {
      panel_observation_.Reset();
      panel_widget_ = nullptr;
    }
  }

  maho::MahoLocationBarUtilityIconView* icon_for_testing() const { return icon_; }
  views::Widget* panel_widget_for_testing() const { return panel_widget_; }

 private:
  views::Widget* ShowPanel() {
    panel_widget_ = maho::MahoLocationBarUtilityPanelView::Show(icon_, model_);
    if (panel_widget_) {
      panel_observation_.Observe(panel_widget_);
    }
    return panel_widget_;
  }

  raw_ptr<maho::MahoLocationBarUtilityIconView> icon_ = nullptr;
  raw_ptr<views::Widget> panel_widget_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver> panel_observation_{this};
  maho::MahoLocationBarUtilityPanelModel model_;
};

BEGIN_METADATA(UtilityPanelHostView)
END_METADATA

class LocationBarUtilityPanelInteractiveUiTest : public InProcessBrowserTest {
 public:
  LocationBarUtilityPanelInteractiveUiTest() = default;
  LocationBarUtilityPanelInteractiveUiTest(
      const LocationBarUtilityPanelInteractiveUiTest&) = delete;
  LocationBarUtilityPanelInteractiveUiTest& operator=(
      const LocationBarUtilityPanelInteractiveUiTest&) = delete;
  ~LocationBarUtilityPanelInteractiveUiTest() override = default;

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();

    browser()->profile()->GetPrefs()->SetBoolean(
        maho::sidebar_prefs::kSidebarLayoutEnabled, false);
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    ASSERT_TRUE(browser_view);
    browser_view->InvalidateLayout();
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
  }

 protected:
  LocationBarView* GetLocationBarView() {
    return BrowserView::GetBrowserViewForBrowser(browser())->GetLocationBarView();
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(LocationBarUtilityPanelInteractiveUiTest,
                       HoverRevealClickAndOutsideCloseUtilityPanel) {
  auto host_widget = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.context = browser()->window()->GetNativeWindow();
  params.bounds = gfx::Rect(100, 200, 200, 120);
  host_widget->Init(std::move(params));
  host_widget->SetContentsView(std::make_unique<UtilityPanelHostView>());
  host_widget->Show();
  host_widget->Activate();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return host_widget->IsActive();
  }));

  auto* host_view = static_cast<UtilityPanelHostView*>(host_widget->GetContentsView());
  ASSERT_TRUE(host_view);

  EXPECT_FALSE(host_view->icon_for_testing()->GetVisible());
  EXPECT_FALSE(host_view->icon_for_testing()->IsHoverVisibleForTesting());
  EXPECT_FALSE(host_view->icon_for_testing()->IsPanelOpenForTesting());

  ui::MouseEvent enter_event(ui::EventType::kMouseEntered,
                             gfx::PointF(host_view->GetBoundsInScreen().CenterPoint()),
                             gfx::PointF(host_view->GetBoundsInScreen().CenterPoint()),
                             ui::EventTimeForNow(), 0, 0);
  host_view->OnMouseEntered(enter_event);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return host_view->icon_for_testing()->GetVisible();
  }));
  EXPECT_TRUE(host_view->icon_for_testing()->IsHoverVisibleForTesting());
  EXPECT_TRUE(host_view->icon_for_testing()->GetVisible());

  scoped_refptr<content::MessageLoopRunner> open_runner =
      new content::MessageLoopRunner;
  ui_test_utils::MoveMouseToCenterAndClick(
      host_view->icon_for_testing(), ui_controls::LEFT,
      ui_controls::DOWN | ui_controls::UP, open_runner->QuitClosure());
  open_runner->Run();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return host_view->panel_widget_for_testing() != nullptr &&
           host_view->panel_widget_for_testing()->IsActive();
  }));
  ASSERT_TRUE(host_view->panel_widget_for_testing());
  EXPECT_TRUE(host_view->panel_widget_for_testing()->IsActive());
  EXPECT_TRUE(host_view->icon_for_testing()->IsPanelOpenForTesting());
  EXPECT_TRUE(host_view->icon_for_testing()->GetVisible());

  ui::MouseEvent exit_event(ui::EventType::kMouseExited,
                            gfx::PointF(host_widget->GetWindowBoundsInScreen().origin()),
                            gfx::PointF(host_widget->GetWindowBoundsInScreen().origin()),
                            ui::EventTimeForNow(), 0, 0);
  host_view->OnMouseExited(exit_event);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !host_view->icon_for_testing()->IsHoverVisibleForTesting();
  }));
  EXPECT_TRUE(host_view->icon_for_testing()->GetVisible())
      << "Icon should stay visible while the panel is open";
  EXPECT_TRUE(host_view->icon_for_testing()->IsPanelOpenForTesting());
  ASSERT_TRUE(host_view->panel_widget_for_testing());
  EXPECT_TRUE(host_view->panel_widget_for_testing()->IsActive());

  host_widget->Activate();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return host_view->panel_widget_for_testing() == nullptr;
  }));
  EXPECT_FALSE(host_view->icon_for_testing()->IsPanelOpenForTesting());
  EXPECT_FALSE(host_view->icon_for_testing()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(LocationBarUtilityPanelInteractiveUiTest,
                       RealLocationBarViewUtilityIconVisibilityAndKeepsPageInfoBubbleClosed) {
  LocationBarView* location_bar_view = GetLocationBarView();
  ASSERT_TRUE(location_bar_view);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::MahoLocationBarUtilityIconView* utility_icon = nullptr;
  for (views::View* child : location_bar_view->children()) {
    utility_icon = views::AsViewClass<maho::MahoLocationBarUtilityIconView>(child);
    if (utility_icon) {
      break;
    }
  }
  ASSERT_TRUE(utility_icon);

  EXPECT_FALSE(utility_icon->IsHoverVisibleForTesting());

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  const gfx::Point location_bar_origin =
      location_bar_view->GetBoundsInScreen().origin();
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(location_bar_origin,
                                               browser()->window()->GetNativeWindow()));

  const gfx::Point omnibox_center =
      ui_test_utils::GetCenterInScreenCoordinates(location_bar_view->omnibox_view());
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
      omnibox_center, browser()->window()->GetNativeWindow()));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return utility_icon->GetVisible() &&
           utility_icon->IsHoverVisibleForTesting();
  }));

  scoped_refptr<content::MessageLoopRunner> runner =
      new content::MessageLoopRunner;
  ui_test_utils::MoveMouseToCenterAndClick(
      utility_icon, ui_controls::LEFT,
      ui_controls::DOWN | ui_controls::UP, runner->QuitClosure());
  runner->Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(PageInfoBubbleView::BUBBLE_NONE,
            PageInfoBubbleView::GetShownBubbleType());
  EXPECT_TRUE(utility_icon->GetVisible());
}
