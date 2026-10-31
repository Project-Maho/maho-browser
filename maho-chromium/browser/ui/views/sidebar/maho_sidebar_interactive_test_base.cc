// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/json/string_escape.h"
#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "ui/views/test/widget_test.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/test/ui_controls.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/menu_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

#include "net/dns/mock_host_resolver.h"

#include "base/timer/timer.h"

#include "content/public/test/test_utils.h"
#include "maho/browser/ui/webui/maho_test/maho_test_ui.h"

namespace maho {

namespace {

views::View* FindViewAtScreenPoint(views::View* root, const gfx::Point& screen_point) {
  if (!root || !root->GetVisible()) {
    return nullptr;
  }
  if (!root->GetBoundsInScreen().Contains(screen_point)) {
    return nullptr;
  }
  for (views::View* child : root->children()) {
    views::View* found = FindViewAtScreenPoint(child, screen_point);
    if (found) {
      return found;
    }
  }
  return root;
}

void FindAllRowsRecursive(views::View* root, std::vector<views::View*>& rows) {
  if (!root) return;
  std::string_view name = root->GetClassName();
  if (name == "SidebarTabRowView" || name == "SidebarFolderRowView") {
    rows.push_back(root);
    return;
  }
  for (views::View* child : root->children()) {
    FindAllRowsRecursive(child, rows);
  }
}

views::View* FindFirstRowInSection(MahoSidebarTabSection section, MahoSidebarTabListView* tab_list) {
  LOG(ERROR) << "[MAHO_DND_TEST_LOG] FindFirstRowInSection checking children for section " << static_cast<int>(section);
  std::vector<views::View*> rows;
  FindAllRowsRecursive(tab_list->tab_rows_for_testing(), rows);
  for (views::View* row : rows) {
    std::string_view name = row->GetClassName();
    if (name == "SidebarTabRowView") {
      SidebarTabRowView* tab_row = static_cast<SidebarTabRowView*>(row);
      LOG(ERROR) << "[MAHO_DND_TEST_LOG]     SidebarTabRowView section: " << static_cast<int>(tab_row->section_for_testing());
      if (tab_row->section_for_testing() == section) {
        return tab_row;
      }
    } else if (name == "SidebarFolderRowView") {
      SidebarFolderRowView* folder_row = static_cast<SidebarFolderRowView*>(row);
      LOG(ERROR) << "[MAHO_DND_TEST_LOG]     SidebarFolderRowView section: " << static_cast<int>(folder_row->section());
      if (folder_row->section() == section) {
        return folder_row;
      }
    }
  }
  return nullptr;
}

} // namespace

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

void MahoSidebarInteractiveTestBase::SetUpCommandLine(
    base::CommandLine* command_line) {
  InteractiveBrowserTest::SetUpCommandLine(command_line);
  // Maho suppresses the startup browser while its onboarding login-gate is
  // active (no relay session in tests), which would leave browser() null and
  // crash ResolveViews(). Disable the gate so the standard window exists.
  command_line->AppendSwitch("maho-disable-login-gate");
  command_line->AppendSwitch("no-proxy-server");
  command_line->AppendSwitch("no-sandbox");
}

void MahoSidebarInteractiveTestBase::PreRunTestOnMainThread() {
  content::RunAllPendingInMessageLoop();
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  for (auto* window : GetAllBrowserWindowInterfaces()) {
    auto* view = BrowserView::GetBrowserViewForBrowser(window);
    auto* controller =
        view ? view->GetMahoCommandOverlayControllerForTesting() : nullptr;
    if (controller && controller->IsVisible()) {
      views::test::WidgetDestroyedWaiter closed(
          controller->GetOverlayViewForTesting()->GetWidget());
      controller->Hide();
      closed.Wait();
    }
  }
  InteractiveBrowserTest::PreRunTestOnMainThread();
}

void MahoSidebarInteractiveTestBase::SetUpOnMainThread() {
  InteractiveBrowserTest::SetUpOnMainThread();
  host_resolver()->AddRule("*", "127.0.0.1");
  SetShellEventObserverForTesting(&observer_);
  // Disable Mac menu closure animation so that ExecuteCommand runs within
  // RunUntilIdle() instead of after a 450ms real-time animation.
  views::test::DisableMenuClosureAnimations();

  // Seed the 2-state sidebar prefs used by the refactor.
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/true);

  ResolveViews();

  std::ignore = ui_test_utils::BringBrowserWindowToFront(browser());
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
  MahoSidebarViewStateModel state = adapter.BuildViewStateModel(static_cast<Browser*>(browser()));
  if (!state.tab_list.active_space_id.empty()) {
    const std::string active_space_id_json =
        base::GetQuotedJSONString(state.tab_list.active_space_id);
    state.favorites =
        MahoSidebarStateAdapter::BuildFavoritesModelForSpaceIdJson(
            active_space_id_json);
  }
  return state;
}

std::string MahoSidebarInteractiveTestBase::GetActiveSpaceId() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge) {
    // Prefer the browser-scoped active space so tests observe the same state
    // the product code (MahoSidebarView, MahoSidebarStateAdapter) uses when
    // switching spaces via SwitchToSpace(browser_, ...). Fall back to the
    // global bridge value only if the browser-scoped call returns empty.
    const std::string browser_space_id = bridge->GetActiveSpaceId(browser());
    if (!browser_space_id.empty()) {
      return browser_space_id;
    }
    return bridge->GetActiveSpaceId();
  }
  return GetViewState().tab_list.active_space_id;
}

void MahoSidebarInteractiveTestBase::AddTestTab(
    const GURL& url,
    const std::u16string& title) {
  auto* wc = chrome::AddAndReturnTabAt(browser(), url, -1, false);
  LOG(INFO) << "AddTestTab: url=" << url.spec() 
            << " wc=" << wc 
            << " count=" << browser()->GetTabStripModel()->count();
}

void MahoSidebarInteractiveTestBase::AddTestTabs(int count) {
  for (int i = 0; i < count; ++i) {
    GURL url(base::StringPrintf("data:text/html,page%d", i));
    std::u16string title =
        base::UTF8ToUTF16(base::StringPrintf("Page %d", i));
    AddTestTab(url, title);
  }
}

void MahoSidebarInteractiveTestBase::SeedProfile(int seed_number) {
  content::WebContents* wc = chrome::AddAndReturnTabAt(
      browser(), GURL("chrome://maho-test/"), -1, true);
  ASSERT_TRUE(wc);
  ResolveViews();

  if (seed_number == 0) {
    return;
  }

  SeedRuntimeVerificationState(browser()->GetProfile(), seed_number);

  ASSERT_TRUE(BlockAndPollUntil([this]() {
    ResolveViews();
    const MahoSidebarViewStateModel state = GetViewState();
    LOG(INFO) << "SeedProfile condition: pinned=" << state.tab_list.pinned_tree.size()
              << " normal=" << state.tab_list.normal_tree.size();
    return !state.tab_list.pinned_tree.empty() ||
           !state.tab_list.normal_tree.empty();
  }));

  ResolveViews();
}

void MahoSidebarInteractiveTestBase::SeedSidebarPrefs(bool layout_enabled,
                                                      bool panel_expanded) {
  auto* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(sidebar_prefs::kSidebarLayoutEnabled, layout_enabled);
  prefs->SetBoolean(sidebar_prefs::kSidebarPanelExpanded, panel_expanded);
}

void MahoSidebarInteractiveTestBase::ShowContextMenuOnView(views::View* view) {
  ASSERT_TRUE(view);
  const gfx::Point center = view->GetBoundsInScreen().CenterPoint();
  view->ShowContextMenu(center, ui::mojom::MenuSourceType::kMouse);
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

  SidebarDragPayload payload;
  payload.space_id = GetActiveSpaceId();
  bool handled = false;

  std::string_view class_name = source->GetClassName();
  if (class_name == "SidebarTabRowView") {
    SidebarTabRowView* tab_row = static_cast<SidebarTabRowView*>(source);
    payload.node_kind = SidebarNodeKind::kTab;
    payload.node_id = tab_row->tab_id();
    if (tab_row->section_for_testing() == MahoSidebarTabSection::kNormal) {
      payload.origin = SidebarDragOrigin::kNormalSection;
    } else {
      payload.origin = SidebarDragOrigin::kPinnedSection;
    }
    if (!tab_row->containing_folder_id().empty()) {
      payload.source_parent_folder_id = tab_row->containing_folder_id();
      payload.source_folder_child_index = tab_row->tab_index_for_testing();
    }
    handled = true;
  } else if (class_name == "SidebarFolderRowView") {
    SidebarFolderRowView* folder_row = static_cast<SidebarFolderRowView*>(source);
    payload.node_kind = SidebarNodeKind::kFolder;
    payload.node_id = folder_row->folder_id();
    payload.origin = SidebarDragOrigin::kNormalSection;
    if (!folder_row->parent_folder_id().empty()) {
      payload.source_parent_folder_id = folder_row->parent_folder_id();
    }
    handled = true;
  } else if (class_name == "FavoriteTileButton") {
    std::optional<size_t> index = favorites_->GetIndexOf(source);
    if (index.has_value()) {
      auto state = GetViewState();
      if (index.value() < state.favorites.items.size()) {
        payload.node_kind = SidebarNodeKind::kTab;
        payload.node_id = state.favorites.items[index.value()].tab_id;
        payload.origin = SidebarDragOrigin::kFavorites;
        handled = true;
      }
    }
  }

  if (!handled) {
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
    return;
  }

  LOG(ERROR) << "[MAHO_DND_TEST_LOG] DragViewToPoint starting. Source: "
             << source->GetClassName() << " screen_point: " << screen_point.ToString();

  views::View* target_view = FindViewAtScreenPoint(sidebar_, screen_point);
  if (!target_view) {
    LOG(ERROR) << "[MAHO_DND_TEST_LOG] FindViewAtScreenPoint returned null!";
    target_view = sidebar_;
  } else {
    LOG(ERROR) << "[MAHO_DND_TEST_LOG] FindViewAtScreenPoint returned: "
               << target_view->GetClassName() << " bounds in screen: "
               << target_view->GetBoundsInScreen().ToString();
  }

  // If the screen point is close to the top of a section container,
  // target the insertion lane before the first row of that section.
  bool section_top_resolved = false;
  for (MahoSidebarTabSection sec : {MahoSidebarTabSection::kPinned, MahoSidebarTabSection::kNormal}) {
    views::View* sec_target = tab_list_->FindSectionDropTargetForTesting(sec);
    if (sec_target) {
      LOG(ERROR) << "[MAHO_DND_TEST_LOG] sec_target for " << static_cast<int>(sec)
                 << " bounds in screen: " << sec_target->GetBoundsInScreen().ToString()
                 << " Contains point: " << sec_target->GetBoundsInScreen().Contains(screen_point);
    }
    if (sec_target && sec_target->GetBoundsInScreen().Contains(screen_point)) {
      int dist_from_top = screen_point.y() - sec_target->GetBoundsInScreen().y();
      LOG(ERROR) << "[MAHO_DND_TEST_LOG] dist_from_top: " << dist_from_top;
      if (dist_from_top < 30) {
        views::View* first_row = FindFirstRowInSection(sec, tab_list_);
        if (first_row) {
          views::View* lane = nullptr;
          if (first_row->GetClassName() == std::string_view("SidebarTabRowView")) {
            lane = tab_list_->FindInsertionLaneBeforeTabByIdForTesting(
                static_cast<SidebarTabRowView*>(first_row)->tab_id());
          } else if (first_row->GetClassName() == std::string_view("SidebarFolderRowView")) {
            lane = tab_list_->FindInsertionLaneBeforeFolderByIdForTesting(
                static_cast<SidebarFolderRowView*>(first_row)->folder_id());
          }
          if (lane) {
            target_view = lane;
            section_top_resolved = true;
            LOG(ERROR) << "[MAHO_DND_TEST_LOG] Resolved to first lane of section because point is close to top of section container.";
            break;
          }
        }
      }
    }
  }

  if (!section_top_resolved) {
    // Walk up to find row views if nested
    views::View* resolved_target = target_view;
    while (resolved_target) {
      std::string_view name = resolved_target->GetClassName();
      if (name == "SidebarTabRowView" || name == "SidebarFolderRowView") {
        target_view = resolved_target;
        LOG(ERROR) << "[MAHO_DND_TEST_LOG] Walked up to row view: " << name;
        break;
      }
      resolved_target = resolved_target->parent();
    }
  }

  if (target_view->GetClassName() == std::string_view("SidebarTabRowView")) {
    SidebarTabRowView* target_tab = static_cast<SidebarTabRowView*>(target_view);
    gfx::Point target_local = screen_point;
    views::View::ConvertPointFromScreen(target_tab, &target_local);

    double y_frac = static_cast<double>(target_local.y()) / target_tab->height();
    LOG(ERROR) << "[MAHO_DND_TEST_LOG] Target is tab row. y_frac: " << y_frac;
    if (y_frac < 0.3) {
      views::View* lane = tab_list_->FindInsertionLaneBeforeTabByIdForTesting(target_tab->tab_id());
      if (lane) {
        target_view = lane;
        LOG(ERROR) << "[MAHO_DND_TEST_LOG] Resolved to insertion lane before tab: " << target_tab->tab_id();
      }
    }
  } else if (target_view->GetClassName() == std::string_view("SidebarFolderRowView")) {
    SidebarFolderRowView* target_folder = static_cast<SidebarFolderRowView*>(target_view);
    gfx::Point target_local = screen_point;
    views::View::ConvertPointFromScreen(target_folder, &target_local);

    double y_frac = static_cast<double>(target_local.y()) / target_folder->height();
    LOG(ERROR) << "[MAHO_DND_TEST_LOG] Target is folder row. y_frac: " << y_frac;
    if (y_frac < 0.3) {
      views::View* lane = tab_list_->FindInsertionLaneBeforeFolderByIdForTesting(target_folder->folder_id());
      if (lane) {
        target_view = lane;
        LOG(ERROR) << "[MAHO_DND_TEST_LOG] Resolved to insertion lane before folder: " << target_folder->folder_id();
      }
    }
  } else if (target_view->GetClassName() == std::string_view("SidebarSectionDropTarget")) {
    MahoSidebarTabSection section = MahoSidebarTabSection::kNormal;
    if (target_view == tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned)) {
      section = MahoSidebarTabSection::kPinned;
    }
    views::View* first_row = FindFirstRowInSection(section, tab_list_);
    if (first_row) {
      LOG(ERROR) << "[MAHO_DND_TEST_LOG] Target is populated SidebarSectionDropTarget. Resolving to first row: "
                 << first_row->GetClassName();
      if (first_row->GetClassName() == std::string_view("SidebarTabRowView")) {
        SidebarTabRowView* tab_row = static_cast<SidebarTabRowView*>(first_row);
        views::View* lane = tab_list_->FindInsertionLaneBeforeTabByIdForTesting(tab_row->tab_id());
        if (lane) {
          target_view = lane;
          LOG(ERROR) << "[MAHO_DND_TEST_LOG] Resolved to insertion lane before first tab: " << tab_row->tab_id();
        }
      } else if (first_row->GetClassName() == std::string_view("SidebarFolderRowView")) {
        SidebarFolderRowView* folder_row = static_cast<SidebarFolderRowView*>(first_row);
        views::View* lane = tab_list_->FindInsertionLaneBeforeFolderByIdForTesting(folder_row->folder_id());
        if (lane) {
          target_view = lane;
          LOG(ERROR) << "[MAHO_DND_TEST_LOG] Resolved to insertion lane before first folder: " << folder_row->folder_id();
        }
      }
    }
  }

  LOG(ERROR) << "[MAHO_DND_TEST_LOG] Final resolved target_view name: " << target_view->GetClassName()
             << " bounds: " << target_view->GetBoundsInScreen().ToString();

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);

  views::View* dnd_target = target_view;
  views::View::DropCallback drop_callback;
  while (dnd_target) {
    gfx::Point target_local = screen_point;
    views::View::ConvertPointFromScreen(dnd_target, &target_local);
    ui::DropTargetEvent target_event(
        data, gfx::PointF(target_local), gfx::PointF(target_local),
        static_cast<int>(ui::mojom::DragOperation::kMove));

    drop_callback = dnd_target->GetDropCallback(target_event);
    if (drop_callback) {
      LOG(ERROR) << "[MAHO_DND_TEST_LOG] Found DropCallback on dnd_target: " << dnd_target->GetClassName();
      dnd_target->OnDragUpdated(target_event);
      ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
      std::move(drop_callback).Run(target_event, output, std::unique_ptr<ui::LayerTreeOwner>());
      LOG(ERROR) << "[MAHO_DND_TEST_LOG] Run drop_callback. output drag operation: " << static_cast<int>(output);
      dnd_target->OnDragExited();
      break;
    }
    dnd_target = dnd_target->parent();
  }

  base::RunLoop().RunUntilIdle();
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
            sidebar_prefs::IsSidebarLayoutEnabled(browser()->GetProfile()->GetPrefs()));
}

void MahoSidebarInteractiveTestBase::ExpectSidebarPanelExpanded(bool expanded) {
  EXPECT_EQ(expanded,
            sidebar_prefs::IsSidebarPanelExpanded(browser()->GetProfile()->GetPrefs()));
}

void MahoSidebarInteractiveTestBase::ExpectTabCount(int expected) {
  auto state = GetViewState();
  int count = static_cast<int>(state.tab_list.normal_tree.size()) +
              static_cast<int>(state.tab_list.pinned_tree.size());
  EXPECT_EQ(expected, count);
}

void MahoSidebarInteractiveTestBase::ExpectActiveTabIndex(int expected) {
  EXPECT_EQ(expected, browser()->GetTabStripModel()->active_index());
}

void MahoSidebarInteractiveTestBase::SeedArchivedTabs(
    std::vector<ArchivedTabItem> tabs) {
  ASSERT_TRUE(sidebar_);
  auto* archive_view = sidebar_->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();
}

void MahoSidebarInteractiveTestBase::StopSidebarObservations() {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  if (bridge && sidebar_) {
    bridge->RemoveObserver(sidebar_);
  }
  if (browser() && browser()->GetTabStripModel() && sidebar_) {
    browser()->GetTabStripModel()->RemoveObserver(sidebar_);
  }
}

void MahoSidebarInteractiveTestBase::ReloadArchivedTabsFromFFI() {
  ASSERT_TRUE(sidebar_);
  auto* archive_view = sidebar_->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  archive_view->ReloadArchivedTabs();
  base::RunLoop().RunUntilIdle();
}

void MahoSidebarInteractiveTestBase::RunAllPendingTasks() {
  content::RunAllTasksUntilIdle();
}

}  // namespace maho
