// Copyright 2026 Maho Browser. All rights reserved.

#include <utility>

#include <memory>

#include "base/command_line.h"
#include "base/test/run_until.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "maho/browser/ui/context_menu/maho_address_bar_context_menu.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "ui/views/test/button_test_api.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "content/public/test/test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h"
#include "maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_coordinator.h"
#include "maho/browser/ui/views/shields/maho_shield_bubble_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/widget/widget.h"

namespace {

views::Label* FindDescendantLabelWithText(views::View* root,
                                          const std::u16string& text) {
  if (!root) {
    return nullptr;
  }

  for (views::View* child : root->children()) {
    if (auto* label = views::AsViewClass<views::Label>(child);
        label && label->GetText() == text) {
      return label;
    }

    if (views::Label* descendant = FindDescendantLabelWithText(child, text)) {
      return descendant;
    }
  }

  return nullptr;
}

class UtilityPanelHostView : public views::View,
                             public views::WidgetObserver {
  METADATA_HEADER(UtilityPanelHostView, views::View)

 public:
  UtilityPanelHostView() {
    SetPreferredSize(gfx::Size(200, 120));

    maho::MahoUtilityPanelZone toolbar_zone;
    toolbar_zone.id = maho::MahoUtilityPanelZoneId::kToolbar;
    toolbar_zone.style = maho::MahoUtilityPanelZoneStyle::kToolbarRow;

    maho::MahoLocationBarUtilityPanelAction share_action;
    share_action.title = u"Share";
    share_action.subtitle = u"Share this page";
    share_action.accessible_name = u"Share — Share this page";
    share_action.icon = &maho_lucide_icons::kShareIcon;
    share_action.enabled = true;
    share_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kLive;
    toolbar_zone.items.push_back(share_action);

    maho::MahoLocationBarUtilityPanelAction magic_action;
    magic_action.title = u"Magic";
    magic_action.accessible_name = u"Magic";
    magic_action.icon = &maho_lucide_icons::kWandSparklesIcon;
    magic_action.enabled = false;
    magic_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kUnavailable;
    toolbar_zone.items.push_back(magic_action);

    maho::MahoLocationBarUtilityPanelAction camera_action;
    camera_action.title = u"Camera";
    camera_action.accessible_name = u"Camera";
    camera_action.icon = &maho_lucide_icons::kCameraIcon;
    camera_action.enabled = false;
    camera_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kUnavailable;
    toolbar_zone.items.push_back(camera_action);

    maho::MahoLocationBarUtilityPanelAction viewfinder_action;
    viewfinder_action.title = u"Viewfinder";
    viewfinder_action.accessible_name = u"Viewfinder";
    viewfinder_action.icon = &maho_lucide_icons::kScanIcon;
    viewfinder_action.enabled = false;
    viewfinder_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kUnavailable;
    toolbar_zone.items.push_back(viewfinder_action);

    model_.zones.push_back(toolbar_zone);

    maho::MahoUtilityPanelZone extensions_zone;
    extensions_zone.id = maho::MahoUtilityPanelZoneId::kExtensions;
    extensions_zone.style = maho::MahoUtilityPanelZoneStyle::kChipRow;
    extensions_zone.header_label = u"Extensions";

    maho::MahoLocationBarUtilityPanelAction extension_action;
    extension_action.title = u"Sample extension";
    extension_action.subtitle = u"Enabled";
    extension_action.accessible_name = u"Sample extension — Enabled";
    extension_action.icon = &maho_lucide_icons::kPuzzleIcon;
    extension_action.enabled = true;
    extension_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kLive;
    extensions_zone.items.push_back(extension_action);

    maho::MahoLocationBarUtilityPanelAction add_extension_action;
    add_extension_action.title = u"+";
    add_extension_action.accessible_name = u"Add extension";
    add_extension_action.icon = &maho_lucide_icons::kPlusIcon;
    add_extension_action.enabled = true;
    add_extension_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kLive;
    extensions_zone.items.push_back(add_extension_action);

    model_.zones.push_back(extensions_zone);

    maho::MahoUtilityPanelZone settings_zone;
    settings_zone.id = maho::MahoUtilityPanelZoneId::kSettings;
    settings_zone.style = maho::MahoUtilityPanelZoneStyle::kSettingsList;
    settings_zone.header_label = u"Settings";

    maho::MahoLocationBarUtilityPanelAction developer_mode_action;
    developer_mode_action.title = u"Developer mode";
    developer_mode_action.subtitle = u"Disabled";
    developer_mode_action.accessible_name = u"Developer mode — Disabled";
    developer_mode_action.icon = &maho_lucide_icons::kHammerIcon;
    developer_mode_action.enabled = false;
    developer_mode_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kLive;
    settings_zone.items.push_back(developer_mode_action);

    maho::MahoLocationBarUtilityPanelAction block_ads_action;
    block_ads_action.title = u"Block Ads & Trackers";
    block_ads_action.subtitle = u"Allowed on every visit";
    block_ads_action.accessible_name = u"Block Ads & Trackers — Allowed on every visit";
    block_ads_action.icon = &maho_lucide_icons::kShieldHalfIcon;
    block_ads_action.enabled = true;
    block_ads_action.data_state = maho::MahoLocationBarUtilityPanelDataState::kLive;
    settings_zone.items.push_back(block_ads_action);

    model_.zones.push_back(settings_zone);

    model_.footer.title = u"Security state available";
    model_.footer.subtitle = u"Page security";

    icon_ = AddChildView(std::make_unique<maho::MahoLocationBarUtilityIconView>(
        base::BindRepeating(base::IgnoreResult(&UtilityPanelHostView::ShowPanel),
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

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Maho suppresses the startup browser while its onboarding login-gate is
    // active (no relay session in tests), which would leave browser() null and
    // crash SetUpOnMainThread. Disable the gate so the standard
    // InProcessBrowserTest window exists.
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();

    browser()->GetProfile()->GetPrefs()->SetBoolean(
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

  void NotifyButtonClick(views::Button* button) {
    ASSERT_TRUE(button);

    ui::MouseEvent released_event(ui::EventType::kMouseReleased, gfx::PointF(),
                                  gfx::PointF(), ui::EventTimeForNow(),
                                  ui::EF_LEFT_MOUSE_BUTTON,
                                  ui::EF_LEFT_MOUSE_BUTTON);
    views::test::ButtonTestApi(button).NotifyClick(released_event);
    base::RunLoop().RunUntilIdle();
  }

  void ClickViewCenter(views::View* view) {
    ASSERT_TRUE(view);
    ASSERT_FALSE(view->GetBoundsInScreen().IsEmpty());

    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    ui_test_utils::MoveMouseToCenterAndClick(
        view, ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
        runner->QuitClosure());
    runner->Run();
    base::RunLoop().RunUntilIdle();
  }

  maho::MahoLocationBarUtilityPanelView* OpenUtilityPanelFromRealLocationBar(
      LocationBarView* location_bar_view) {
    if (!location_bar_view) {
      ADD_FAILURE() << "Location bar view was null";
      return nullptr;
    }

    auto* utility_icon = location_bar_view->GetUtilityIconViewForTesting();
    if (!utility_icon) {
      ADD_FAILURE() << "Utility icon view was null";
      return nullptr;
    }

    location_bar_view->SimulateOmniboxHoveredForTesting(true);
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    if (!browser_view) {
      ADD_FAILURE() << "Browser view was null";
      return nullptr;
    }
    if (!base::test::RunUntil([&]() {
      browser_view->GetWidget()->LayoutRootViewIfNecessary();
      return utility_icon->GetVisible() &&
             utility_icon->IsHoverVisibleForTesting() &&
             !utility_icon->GetBoundsInScreen().IsEmpty();
    })) {
      ADD_FAILURE() << "Utility icon did not become visible";
      return nullptr;
    }

    NotifyButtonClick(utility_icon);

    if (!base::test::RunUntil([&]() {
      return utility_icon->IsPanelOpenForTesting();
    })) {
      ADD_FAILURE() << "Panel did not open after clicking the utility icon";
      return nullptr;
    }

    views::Widget* panel_widget = location_bar_view->ShowUtilityPanel();
    if (!panel_widget) {
      ADD_FAILURE() << "Utility panel widget was null";
      return nullptr;
    }
    if (!base::test::RunUntil([&]() {
      return panel_widget->IsVisible() && panel_widget->IsActive();
    })) {
      ADD_FAILURE() << "Utility panel widget did not become active";
      return nullptr;
    }

    auto* panel_view = static_cast<maho::MahoLocationBarUtilityPanelView*>(
        panel_widget->widget_delegate());
    if (!panel_view) {
      ADD_FAILURE() << "Utility panel delegate was null";
      return nullptr;
    }
    return panel_view;
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(LocationBarUtilityPanelInteractiveUiTest,
                       HoverRevealClickAndOutsideCloseUtilityPanel) {
  auto host_widget = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.context = browser()->GetWindow()->GetNativeWindow();
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

  maho::MahoLocationBarUtilityIconView* utility_icon =
      location_bar_view->GetUtilityIconViewForTesting();
  ASSERT_TRUE(utility_icon);

  EXPECT_FALSE(utility_icon->IsHoverVisibleForTesting());

  location_bar_view->SimulateOmniboxHoveredForTesting(true);

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

IN_PROC_BROWSER_TEST_F(LocationBarUtilityPanelInteractiveUiTest,
                       CoordinatorDismissesBubbleOnTabSwitch) {
  LocationBarView* location_bar_view = GetLocationBarView();
  ASSERT_TRUE(location_bar_view);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  maho::MahoLocationBarUtilityIconView* utility_icon =
      location_bar_view->GetUtilityIconViewForTesting();
  ASSERT_TRUE(utility_icon);

  location_bar_view->SimulateOmniboxHoveredForTesting(true);

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    browser_view->GetWidget()->LayoutRootViewIfNecessary();
    return utility_icon->GetVisible() &&
           utility_icon->IsHoverVisibleForTesting() &&
           !utility_icon->GetBoundsInScreen().IsEmpty();
  }));

  ui::MouseEvent released_event(ui::EventType::kMouseReleased, gfx::PointF(),
                                 gfx::PointF(), ui::EventTimeForNow(),
                                 ui::EF_LEFT_MOUSE_BUTTON,
                                 ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(utility_icon).NotifyClick(released_event);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return utility_icon->IsPanelOpenForTesting();
  })) << "Panel did not open after clicking the utility icon";

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !utility_icon->IsPanelOpenForTesting();
  }));
  EXPECT_FALSE(utility_icon->IsPanelOpenForTesting());
}

IN_PROC_BROWSER_TEST_F(LocationBarUtilityPanelInteractiveUiTest,
                       UtilityPanelShieldsPrimaryActionShowsShieldBubbleAndClosesPanel) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  LocationBarView* location_bar_view = GetLocationBarView();
  ASSERT_TRUE(location_bar_view);
  auto* utility_icon = location_bar_view->GetUtilityIconViewForTesting();
  ASSERT_TRUE(utility_icon);

  maho::MahoShieldBubbleCoordinator& shield_coordinator =
      maho::MahoShieldBubbleCoordinator::GetForBrowser(static_cast<Browser*>(browser()));
  EXPECT_FALSE(shield_coordinator.IsShowing());

  maho::MahoLocationBarUtilityPanelView* panel_view =
      OpenUtilityPanelFromRealLocationBar(location_bar_view);
  ASSERT_TRUE(panel_view);

  views::View* shields_action =
      panel_view->GetActionViewForTesting(u"Block Ads & Trackers");
  ASSERT_TRUE(shields_action);
  const gfx::Rect shields_action_bounds = shields_action->GetBoundsInScreen();
  ASSERT_FALSE(shields_action_bounds.IsEmpty());

  ClickViewCenter(shields_action);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return shield_coordinator.IsShowing();
  }));
  EXPECT_TRUE(shield_coordinator.IsShowing());
  views::Widget* shield_widget =
      shield_coordinator.GetBubbleWidgetForTesting();
  ASSERT_TRUE(shield_widget);
  auto* shield_delegate =
      shield_widget->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(shield_delegate);
  EXPECT_EQ(shield_delegate->GetAnchorView(), nullptr);
  EXPECT_EQ(shield_delegate->GetAnchorRect(), shields_action_bounds);
  EXPECT_EQ(shield_delegate->anchor_widget(),
            BrowserView::GetBrowserViewForBrowser(browser())->GetWidget());

  auto* shield_view = static_cast<maho::MahoShieldBubbleView*>(
      shield_delegate->GetContentsView());
  ASSERT_TRUE(shield_view);
  ASSERT_TRUE(shield_view->GetSiteLabelForTesting());
  EXPECT_EQ(shield_view->GetSiteLabelForTesting()->GetText(),
            base::UTF8ToUTF16(
                embedded_test_server()->GetURL("/title1.html").host()));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !utility_icon->IsPanelOpenForTesting();
  }));
  EXPECT_FALSE(utility_icon->IsPanelOpenForTesting());

  shield_coordinator.Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !shield_coordinator.IsShowing();
  }));
}

IN_PROC_BROWSER_TEST_F(
    LocationBarUtilityPanelInteractiveUiTest,
    UtilityPanelShieldsPrimaryActionTitleLabelClickShowsShieldBubbleAndClosesPanel) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

  LocationBarView* location_bar_view = GetLocationBarView();
  ASSERT_TRUE(location_bar_view);
  auto* utility_icon = location_bar_view->GetUtilityIconViewForTesting();
  ASSERT_TRUE(utility_icon);

  maho::MahoShieldBubbleCoordinator& shield_coordinator =
      maho::MahoShieldBubbleCoordinator::GetForBrowser(static_cast<Browser*>(browser()));
  EXPECT_FALSE(shield_coordinator.IsShowing());

  maho::MahoLocationBarUtilityPanelView* panel_view =
      OpenUtilityPanelFromRealLocationBar(location_bar_view);
  ASSERT_TRUE(panel_view);

  views::View* shields_action =
      panel_view->GetActionViewForTesting(u"Block Ads & Trackers");
  ASSERT_TRUE(shields_action);

  views::Label* title_label =
      FindDescendantLabelWithText(shields_action, u"Block Ads & Trackers");
  ASSERT_TRUE(title_label);
  ASSERT_FALSE(title_label->GetBoundsInScreen().IsEmpty());

  ClickViewCenter(title_label);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return shield_coordinator.IsShowing();
  }));
  EXPECT_TRUE(shield_coordinator.IsShowing());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !utility_icon->IsPanelOpenForTesting();
  }));
  EXPECT_FALSE(utility_icon->IsPanelOpenForTesting());

  shield_coordinator.Hide();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !shield_coordinator.IsShowing();
  }));
}

// Verifies the address-bar copy-command enabled-state with a real Browser
// object (the gap identified in the Wave 1 audit: unit tests note they cannot
// safely exercise IsCommandIdEnabled for copy commands without a real Browser).
IN_PROC_BROWSER_TEST_F(LocationBarUtilityPanelInteractiveUiTest,
                       AddressBarCopyCommandsEnabledStateWithRealBrowser) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  base::RunLoop().RunUntilIdle();

  MahoAddressBarContextMenu menu(static_cast<Browser*>(browser()));

  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_ADDR_COPY_URL))
      << "IDC_MAHO_ADDR_COPY_URL should be enabled when a page is loaded";
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_ADDR_COPY_MARKDOWN))
      << "IDC_MAHO_ADDR_COPY_MARKDOWN should be enabled when a page is loaded";

  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_ADDR_EDIT))
      << "IDC_MAHO_ADDR_EDIT should be disabled without a callback";
}
