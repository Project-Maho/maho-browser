// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/json/string_escape.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view_utils.h"

namespace maho {

RecordingShellEventObserver::RecordingShellEventObserver() = default;
RecordingShellEventObserver::~RecordingShellEventObserver() = default;

void RecordingShellEventObserver::OnShellEventDispatched(
    const std::string& kind,
    const std::string& event_json) {
  events_.push_back({kind, event_json});
}

bool RecordingShellEventObserver::HasEvent(const std::string& kind) const {
  for (const auto& e : events_) {
    if (e.kind == kind)
      return true;
  }
  return false;
}

MahoSidebarInteractiveTestBase::MahoSidebarInteractiveTestBase() = default;
MahoSidebarInteractiveTestBase::~MahoSidebarInteractiveTestBase() = default;

void MahoSidebarInteractiveTestBase::SetUpOnMainThread() {
  InteractiveBrowserTest::SetUpOnMainThread();
  SetShellEventObserverForTesting(&observer_);

  // Seed the 2-state sidebar prefs used by the refactor.
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/true);

  ResolveViews();
}

void MahoSidebarInteractiveTestBase::TearDownOnMainThread() {
  SetShellEventObserverForTesting(nullptr);
  InteractiveBrowserTest::TearDownOnMainThread();
}

void MahoSidebarInteractiveTestBase::ResolveViews() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);

  for (views::View* child : browser_view->children()) {
    auto* candidate = views::AsViewClass<MahoSidebarContainerView>(child);
    if (candidate) {
      container_ = candidate;
      break;
    }
  }
  ASSERT_TRUE(container_);

  sidebar_ = views::AsViewClass<MahoSidebarView>(container_->sidebar_view());
  ASSERT_TRUE(sidebar_);

  tab_list_ = sidebar_->tab_list_view_for_testing();
  favorites_ = sidebar_->favorites_view_for_testing();
}

MahoSidebarView* MahoSidebarInteractiveTestBase::GetSidebarView() {
  return sidebar_;
}

MahoSidebarTabListView* MahoSidebarInteractiveTestBase::GetTabListView() {
  return tab_list_;
}

MahoSidebarFavoritesGridView*
MahoSidebarInteractiveTestBase::GetFavoritesView() {
  return favorites_;
}

MahoSidebarFooterView* MahoSidebarInteractiveTestBase::GetFooterView() {
  return sidebar_->footer_view_for_testing();
}

MahoSidebarContainerView* MahoSidebarInteractiveTestBase::GetContainerView() {
  return container_;
}

MahoSidebarViewStateModel MahoSidebarInteractiveTestBase::GetViewState() {
  MahoSidebarStateAdapter adapter;
  MahoSidebarViewStateModel state = adapter.BuildViewStateModel(browser());
  if (!state.tab_list.active_space_id.empty()) {
    const std::string active_space_id_json =
        base::GetQuotedJSONString(state.tab_list.active_space_id);
    state.favorites =
        MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
            active_space_id_json);
    if (state.favorites.items.empty()) {
      (void)base::test::RunUntil([&]() {
        state.favorites =
            MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
                active_space_id_json);
        return !state.favorites.items.empty();
      });
    }
  }
  return state;
}

std::string MahoSidebarInteractiveTestBase::GetActiveSpaceId() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    return bridge->GetActiveSpaceId();
  }
  return GetViewState().tab_list.active_space_id;
}

void MahoSidebarInteractiveTestBase::AddTestTab(
    const GURL& url,
    const std::u16string& title) {
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
}

void MahoSidebarInteractiveTestBase::AddTestTabs(int count) {
  for (int i = 0; i < count; ++i) {
    GURL url(base::StringPrintf("https://example.com/page%d", i));
    std::u16string title =
        base::UTF8ToUTF16(base::StringPrintf("Page %d", i));
    AddTestTab(url, title);
  }
}

void MahoSidebarInteractiveTestBase::SeedProfile(int seed_number) {
  GURL seed_url(
      base::StringPrintf("chrome://maho-test/?seed=%d", seed_number));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), seed_url));
  ResolveViews();

  if (seed_number == 0) {
    return;
  }

  ASSERT_TRUE(base::test::RunUntil([this]() {
    ResolveViews();
    const MahoSidebarViewStateModel state = GetViewState();
    return !state.tab_list.pinned_tree.empty() ||
           !state.tab_list.normal_tree.empty() ||
           !state.tab_list.pinned_tabs.empty() || !state.tab_list.tabs.empty();
  }));

  ResolveViews();
}

void MahoSidebarInteractiveTestBase::SeedSidebarPrefs(bool layout_enabled,
                                                      bool panel_expanded) {
  auto* prefs = browser()->profile()->GetPrefs();
  prefs->SetBoolean(sidebar_prefs::kSidebarLayoutEnabled, layout_enabled);
  prefs->SetBoolean(sidebar_prefs::kSidebarPanelExpanded, panel_expanded);
}

void MahoSidebarInteractiveTestBase::DragViewToView(views::View* source,
                                                     views::View* target) {
  ASSERT_TRUE(source);
  ASSERT_TRUE(target);

  const gfx::Point source_center =
      source->GetBoundsInScreen().CenterPoint();
  const gfx::Point target_center =
      target->GetBoundsInScreen().CenterPoint();

  // Move to source center and press.
  ASSERT_TRUE(ui_controls::SendMouseMove(source_center.x(),
                                          source_center.y()));
  base::RunLoop press_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN, press_loop.QuitClosure()));
  press_loop.Run();

  // Move to target center.
  ASSERT_TRUE(ui_controls::SendMouseMove(target_center.x(),
                                          target_center.y()));

  // Release.
  base::RunLoop release_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::UP, release_loop.QuitClosure()));
  release_loop.Run();
}

void MahoSidebarInteractiveTestBase::DragViewToPoint(
    views::View* source,
    const gfx::Point& screen_point) {
  ASSERT_TRUE(source);

  const gfx::Point source_center =
      source->GetBoundsInScreen().CenterPoint();

  ASSERT_TRUE(ui_controls::SendMouseMove(source_center.x(),
                                          source_center.y()));
  base::RunLoop press_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN, press_loop.QuitClosure()));
  press_loop.Run();

  ASSERT_TRUE(ui_controls::SendMouseMove(screen_point.x(),
                                          screen_point.y()));

  base::RunLoop release_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::UP, release_loop.QuitClosure()));
  release_loop.Run();
}

gfx::Point MahoSidebarInteractiveTestBase::GetViewPoint(
    views::View* view,
    double x_fraction,
    double y_fraction) {
  CHECK(view);
  CHECK_GE(x_fraction, 0.0);
  CHECK_LE(x_fraction, 1.0);
  CHECK_GE(y_fraction, 0.0);
  CHECK_LE(y_fraction, 1.0);

  const gfx::Rect bounds = view->GetBoundsInScreen();
  return gfx::Point(bounds.x() + static_cast<int>(bounds.width() * x_fraction),
                    bounds.y() + static_cast<int>(bounds.height() * y_fraction));
}

void MahoSidebarInteractiveTestBase::ExpectSidebarVisible(bool visible) {
  ASSERT_TRUE(container_);
  EXPECT_EQ(visible, container_->GetVisible());
}

void MahoSidebarInteractiveTestBase::ExpectSidebarLayoutEnabled(bool enabled) {
  EXPECT_EQ(enabled,
            sidebar_prefs::IsSidebarLayoutEnabled(browser()->profile()->GetPrefs()));
}

void MahoSidebarInteractiveTestBase::ExpectSidebarPanelExpanded(bool expanded) {
  EXPECT_EQ(expanded,
            sidebar_prefs::IsSidebarPanelExpanded(browser()->profile()->GetPrefs()));
}

void MahoSidebarInteractiveTestBase::ExpectTabCount(int expected) {
  auto state = GetViewState();
  int count = static_cast<int>(state.tab_list.normal_tree.size()) +
              static_cast<int>(state.tab_list.pinned_tree.size());
  EXPECT_EQ(expected, count);
}

void MahoSidebarInteractiveTestBase::ExpectActiveTabIndex(int expected) {
  EXPECT_EQ(expected, browser()->tab_strip_model()->active_index());
}

}  // namespace maho
