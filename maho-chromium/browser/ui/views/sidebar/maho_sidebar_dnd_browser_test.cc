// Copyright 2026 Maho Browser. All rights reserved.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/split_view/maho_split_view_controller.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/page_transition_types.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

namespace maho {
namespace {

class RecordingShellEventObserver : public ShellEventObserver {
 public:
  void OnShellEventDispatched(const std::string& kind,
                              const std::string& event_json) override {
    if (on_event) {
      on_event.Run(kind);
    }
    events.push_back({kind, event_json});
  }

  struct EventRecord {
    std::string kind;
    std::string json;
  };

  std::vector<EventRecord> events;
  base::RepeatingCallback<void(const std::string&)> on_event;
  void Clear() { events.clear(); }
};

ui::DropTargetEvent MakeDropEvent(const ui::OSExchangeData& data,
                                  const gfx::Point& point = gfx::Point(5, 5)) {
  return ui::DropTargetEvent(
      data, gfx::PointF(point), gfx::PointF(point),
      static_cast<int>(ui::mojom::DragOperation::kMove));
}

std::vector<std::string> UnsplitStableTabIdsInStripOrder(TabStripModel* model) {
  std::vector<std::string> ids;
  for (int i = 0; i < model->count(); ++i) {
    if (model->GetSplitForTab(i).has_value()) {
      continue;
    }
    content::WebContents* wc = model->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
        helper && !helper->stable_tab_id().empty()) {
      ids.push_back(helper->stable_tab_id());
    }
  }
  return ids;
}

int ResolveIndexForStableId(TabStripModel* model, const std::string& id) {
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* wc = model->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    if (auto* helper = MahoTabIdHelper::FromWebContents(wc);
        helper && helper->stable_tab_id() == id) {
      return i;
    }
  }
  return -1;
}

class MahoSidebarDnDBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    SetShellEventObserverForTesting(&observer_);

    PrefService* prefs = browser()->GetProfile()->GetPrefs();
    prefs->SetBoolean(sidebar_prefs::kSidebarLayoutEnabled, true);
    prefs->SetBoolean(sidebar_prefs::kSidebarPanelExpanded, true);

    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL("chrome://maho-test/?seed=2")));

    content::WebContents* wc =
        browser()->GetTabStripModel()->GetActiveWebContents();
    ASSERT_TRUE(wc);
    ASSERT_TRUE(content::WaitForLoadStop(wc));

    base::RunLoop().RunUntilIdle();

    ResolveViews();
  }

  void TearDownOnMainThread() override {
    SetShellEventObserverForTesting(nullptr);
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  void ResolveViews() {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser());
    ASSERT_TRUE(bv);

    container_ = nullptr;
    for (views::View* child : bv->children()) {
      if (auto* c = views::AsViewClass<MahoSidebarContainerView>(child)) {
        container_ = c;
        break;
      }
    }
    ASSERT_TRUE(container_) << "MahoSidebarContainerView not found";
    ASSERT_TRUE(container_->GetVisible());

    sidebar_ = views::AsViewClass<MahoSidebarView>(container_->sidebar_view());
    ASSERT_TRUE(sidebar_);

    tab_list_ = sidebar_->tab_list_view_for_testing();
    ASSERT_TRUE(tab_list_);

    favorites_ = sidebar_->favorites_view_for_testing();
    ASSERT_TRUE(favorites_);
  }

  MahoSidebarViewStateModel GetState() {
    MahoSidebarStateAdapter adapter;
    return adapter.BuildViewStateModel(static_cast<Browser*>(browser()));
  }

  SidebarTabRowView* FindLiveTabRow(const std::string& tab_id) {
    return tab_list_->FindTabRowByIdForTesting(tab_id);
  }

  views::View* FindLiveTabInsertionLaneBeforeTab(const std::string& tab_id) {
    return tab_list_->FindInsertionLaneBeforeTabByIdForTesting(tab_id);
  }

  views::View* FindLiveTabInsertionLaneBeforeFolder(
      const std::string& folder_id) {
    return tab_list_->FindInsertionLaneBeforeFolderByIdForTesting(folder_id);
  }

  SidebarFolderRowView* FindLiveFolderRow(const std::string& folder_id) {
    return tab_list_->FindFolderRowByIdForTesting(folder_id);
  }

  views::View* FindLiveSectionTarget(MahoSidebarTabSection section) {
    return tab_list_->FindSectionDropTargetForTesting(section);
  }

  void PerformSyntheticDrop(views::View* target,
                            const SidebarDragPayload& payload,
                            const gfx::Point& point = gfx::Point(5, 5)) {
    ui::OSExchangeData data;
    WriteMahoDragData(payload, &data);
    ui::DropTargetEvent event = MakeDropEvent(data, point);
    target->OnDragUpdated(event);
    auto callback = target->GetDropCallback(event);
    ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
    if (callback) {
      std::move(callback).Run(event, output,
                              std::unique_ptr<ui::LayerTreeOwner>());
    }
    target->OnDragExited();
    last_drop_result_ = output;
    base::RunLoop().RunUntilIdle();
  }

  std::string ActiveSpaceId() {
    return GetState().tab_list.active_space_id;
  }

  SidebarDragPayload MakeTabPayload(const std::string& tab_id,
                                    SidebarDragOrigin origin) {
    SidebarDragPayload p;
    p.node_kind = SidebarNodeKind::kTab;
    p.node_id = tab_id;
    p.origin = origin;
    p.space_id = ActiveSpaceId();
    return p;
  }

  SidebarDragPayload MakeFolderChildTabPayload(const std::string& tab_id,
                                               const std::string& folder_id,
                                               int child_index) {
    auto p = MakeTabPayload(tab_id, SidebarDragOrigin::kNormalSection);
    p.source_parent_folder_id = folder_id;
    p.source_folder_child_index = child_index;
    return p;
  }

  SidebarDragPayload MakeFolderPayload(const std::string& folder_id) {
    SidebarDragPayload p;
    p.node_kind = SidebarNodeKind::kFolder;
    p.node_id = folder_id;
    p.origin = SidebarDragOrigin::kNormalSection;
    p.space_id = ActiveSpaceId();
    return p;
  }

  SidebarDragPayload MakeNestedFolderPayload(
      const std::string& folder_id,
      const std::string& parent_folder_id) {
    auto p = MakeFolderPayload(folder_id);
    p.source_parent_folder_id = parent_folder_id;
    return p;
  }

  SidebarDragPayload MakeFavoritePayload(const std::string& tab_id) {
    return MakeTabPayload(tab_id, SidebarDragOrigin::kFavorites);
  }

  struct RootTabPair {
    std::string source_id;
    std::string target_id;
  };

  std::optional<RootTabPair> FindTwoRootTabs(
      const std::vector<SidebarTreeNode>& tree) {
    RootTabPair result;
    for (const auto& n : tree) {
      if (n.kind != SidebarNodeKind::kTab) continue;
      if (result.source_id.empty()) {
        result.source_id = n.tab_id;
      } else {
        result.target_id = n.tab_id;
        return result;
      }
    }
    return std::nullopt;
  }

  struct FolderWithChild {
    std::string folder_id;
    std::string child_tab_id;
    int child_index = -1;
  };

  std::optional<FolderWithChild> FindFolderWithChildTab(
      const std::vector<SidebarTreeNode>& tree) {
    for (const auto& n : tree) {
      if (n.kind != SidebarNodeKind::kFolder) continue;
      for (const auto& child : n.children) {
        if (child.kind == SidebarNodeKind::kTab) {
          return FolderWithChild{n.folder_id, child.tab_id,
                                 child.folder_child_index};
        }
      }
    }
    return std::nullopt;
  }

  struct FolderWithTwoChildTabs {
    std::string folder_id;
    std::string tab1_id;
    int tab1_index;
    std::string tab2_id;
  };

  std::optional<FolderWithTwoChildTabs> FindFolderWithTwoChildTabs(
      const std::vector<SidebarTreeNode>& tree) {
    for (const auto& n : tree) {
      if (n.kind != SidebarNodeKind::kFolder) continue;
      FolderWithTwoChildTabs result;
      result.folder_id = n.folder_id;
      int found = 0;
      for (const auto& child : n.children) {
        if (child.kind != SidebarNodeKind::kTab) continue;
        if (found == 0) {
          result.tab1_id = child.tab_id;
          result.tab1_index = child.folder_child_index;
          found++;
        } else {
          result.tab2_id = child.tab_id;
          return result;
        }
      }
    }
    return std::nullopt;
  }

  RecordingShellEventObserver observer_;
  raw_ptr<MahoSidebarContainerView> container_ = nullptr;
  raw_ptr<MahoSidebarView> sidebar_ = nullptr;
  raw_ptr<MahoSidebarTabListView> tab_list_ = nullptr;
  raw_ptr<MahoSidebarFavoritesGridView> favorites_ = nullptr;
  ui::mojom::DragOperation last_drop_result_ = ui::mojom::DragOperation::kNone;
};

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                        LiveSidebarViewsAreReachable) {
  EXPECT_TRUE(container_->GetVisible());
  EXPECT_TRUE(sidebar_->GetVisible());
  EXPECT_NE(tab_list_, nullptr);
  EXPECT_NE(favorites_, nullptr);

  auto state = GetState();
  EXPECT_FALSE(state.tab_list.active_space_id.empty())
      << "Active space ID should be set after seeding";

  bool has_tabs = !state.tab_list.pinned_tree.empty() ||
                  !state.tab_list.normal_tree.empty();
  EXPECT_TRUE(has_tabs) << "Seeded state should have tabs";

  views::View* pinned_section = FindLiveSectionTarget(MahoSidebarTabSection::kPinned);
  views::View* normal_section = FindLiveSectionTarget(MahoSidebarTabSection::kNormal);
  bool has_section_targets = pinned_section || normal_section;
  EXPECT_TRUE(has_section_targets) << "At least one live section drop target should exist";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveRootTabReorder) {
  auto state = GetState();
  auto pair = FindTwoRootTabs(state.tab_list.normal_tree);
  if (!pair) {
    GTEST_SKIP() << "Need at least 2 root normal tabs";
  }

  views::View* target_lane = FindLiveTabInsertionLaneBeforeTab(pair->source_id);
  ASSERT_TRUE(target_lane)
      << "Live insertion lane for target tab " << pair->source_id
      << " not found";

  observer_.Clear();
  auto payload = MakeTabPayload(pair->target_id,
                                SidebarDragOrigin::kNormalSection);
  PerformSyntheticDrop(target_lane, payload);

  ASSERT_FALSE(observer_.events.empty()) << "Should dispatch a shell event";
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");

  auto post = GetState();
  std::vector<std::string> root_tabs;
  for (const auto& n : post.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kTab && n.parent_folder_id.empty()) {
      root_tabs.push_back(n.tab_id);
    }
  }
  ASSERT_GE(root_tabs.size(), 2u);
  EXPECT_EQ(root_tabs[0], pair->target_id);
  EXPECT_EQ(root_tabs[1], pair->source_id);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveTabToPinnedSection) {
  auto state = GetState();
  std::string source_tab_id;
  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kTab) {
      source_tab_id = n.tab_id;
      break;
    }
  }
  if (source_tab_id.empty()) {
    GTEST_SKIP() << "Need at least 1 normal tab";
  }

  views::View* pinned_section = FindLiveSectionTarget(
      MahoSidebarTabSection::kPinned);
  if (!pinned_section) {
    GTEST_SKIP() << "Live pinned section drop target not found in hierarchy";
  }

  observer_.Clear();
  auto payload = MakeTabPayload(source_tab_id,
                                SidebarDragOrigin::kNormalSection);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  const int source_index = ResolveIndexForStableId(model, source_tab_id);
  ASSERT_GE(source_index, 0);
  payload.tab_strip_index = source_index;
  bool native_pin_preceded_core_event = false;
  observer_.on_event = base::BindLambdaForTesting(
      [&](const std::string& kind) {
        if (kind != "pin_tab") {
          return;
        }
        const int live_index = ResolveIndexForStableId(model, source_tab_id);
        native_pin_preceded_core_event =
            live_index >= 0 && model->IsTabPinned(live_index);
      });
  PerformSyntheticDrop(pinned_section, payload);
  observer_.on_event.Reset();

  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_TRUE(native_pin_preceded_core_event)
      << "Native TabStripModel pinning must finish before the core event can "
         "synchronously refresh and invalidate the drag target/index";
  const int source_index_after = ResolveIndexForStableId(model, source_tab_id);
  ASSERT_GE(source_index_after, 0);
  EXPECT_TRUE(model->IsTabPinned(source_index_after));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                       LiveTabDropOnSpaceHeaderPinsNativeTab) {
  auto state = GetState();
  std::string source_tab_id;
  int source_index = -1;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && node.tab_strip_index >= 0) {
      source_tab_id = node.tab_id;
      source_index = node.tab_strip_index;
      break;
    }
  }
  if (source_tab_id.empty()) {
    GTEST_SKIP() << "Need at least one live normal tab";
  }

  views::View* header = sidebar_->space_header_view_for_testing();
  ASSERT_TRUE(header);
  ASSERT_TRUE(header->GetVisible());

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_TRUE(model->ContainsIndex(source_index));
  ASSERT_FALSE(model->IsTabPinned(source_index));

  observer_.Clear();
  auto payload = MakeTabPayload(source_tab_id,
                                SidebarDragOrigin::kNormalSection);
  payload.tab_strip_index = source_index;
  PerformSyntheticDrop(header, payload);

  const int source_index_after = ResolveIndexForStableId(model, source_tab_id);
  ASSERT_GE(source_index_after, 0);
  EXPECT_EQ(last_drop_result_, ui::mojom::DragOperation::kMove);
  EXPECT_TRUE(model->IsTabPinned(source_index_after))
      << "The visible Space header advertises a pinned-section drop and must "
         "complete the same native pin transition as that section";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveFavoriteReorder) {
  auto state = GetState();
  if (state.favorites.items.size() < 2) {
    GTEST_SKIP() << "Need at least 2 favorites";
  }

  const std::string second_tab_id = state.favorites.items[1].tab_id;
  const std::string first_tab_id = state.favorites.items[0].tab_id;

  observer_.Clear();
  auto payload = MakeFavoritePayload(second_tab_id);
  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  favorites_->OnDragUpdated(event);
  auto callback = favorites_->GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  if (callback) {
    std::move(callback).Run(event, output,
                            std::unique_ptr<ui::LayerTreeOwner>());
  }
  favorites_->OnDragExited();

  ASSERT_FALSE(observer_.events.empty());
  EXPECT_EQ(observer_.events[0].kind, "reorder_favorite");

  auto post = GetState();
  ASSERT_GE(post.favorites.items.size(), 2u);
  bool second_moved = post.favorites.items[0].tab_id == second_tab_id ||
                      post.favorites.items[1].tab_id == second_tab_id;
  EXPECT_TRUE(second_moved)
      << "Reordered favorite should still be present in favorites";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveRootFolderReorder) {
  auto state = GetState();
  std::vector<std::string> folder_ids;
  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kFolder) {
      folder_ids.push_back(n.folder_id);
    }
  }
  if (folder_ids.size() < 2) {
    GTEST_SKIP() << "Need at least 2 root folders";
  }

  views::View* target_lane = FindLiveTabInsertionLaneBeforeFolder(folder_ids[1]);
  ASSERT_TRUE(target_lane)
      << "Live insertion lane for folder " << folder_ids[1] << " not found";

  observer_.Clear();
  auto payload = MakeFolderPayload(folder_ids[0]);
  PerformSyntheticDrop(target_lane, payload);

  ASSERT_FALSE(observer_.events.empty());
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");

  auto post = GetState();
  bool source_folder_exists = false;
  bool target_folder_exists = false;
  for (const auto& n : post.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kFolder) {
      if (n.folder_id == folder_ids[0]) source_folder_exists = true;
      if (n.folder_id == folder_ids[1]) target_folder_exists = true;
    }
  }
  EXPECT_TRUE(source_folder_exists)
      << "Source folder should remain in normal tree";
  EXPECT_TRUE(target_folder_exists)
      << "Target folder should remain in normal tree";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveTabOutOfFolderToRoot) {
  auto state = GetState();
  auto folder_info = FindFolderWithChildTab(state.tab_list.normal_tree);
  if (!folder_info) {
    GTEST_SKIP() << "Need a folder with at least 1 child tab";
  }

  std::string root_tab_id;
  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kTab) {
      root_tab_id = n.tab_id;
      break;
    }
  }
  if (root_tab_id.empty()) {
    GTEST_SKIP() << "Need a root tab as drop target";
  }

  SidebarTabRowView* target_row = FindLiveTabRow(root_tab_id);
  ASSERT_TRUE(target_row)
      << "Live row for root tab " << root_tab_id << " not found";
  EXPECT_EQ(target_row->tab_id_for_testing(), root_tab_id);

  observer_.Clear();
  auto payload = MakeFolderChildTabPayload(
      folder_info->child_tab_id, folder_info->folder_id,
      folder_info->child_index);
  PerformSyntheticDrop(target_row, payload);

  ASSERT_FALSE(observer_.events.empty());
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                       LiveSameFolderTabReorder) {
  auto state = GetState();
  auto folder_info = FindFolderWithTwoChildTabs(state.tab_list.normal_tree);
  if (!folder_info) {
    GTEST_SKIP() << "Need a folder with at least 2 child tabs";
  }

  views::View* target_lane =
      FindLiveTabInsertionLaneBeforeTab(folder_info->tab2_id);
  if (!target_lane) {
    GTEST_SKIP() << "Live insertion lane for child tab "
                 << folder_info->tab2_id << " not found";
  }

  observer_.Clear();
  auto payload = MakeFolderChildTabPayload(
      folder_info->tab1_id, folder_info->folder_id, folder_info->tab1_index);
  PerformSyntheticDrop(target_lane, payload);

  ASSERT_FALSE(observer_.events.empty());
  EXPECT_EQ(observer_.events[0].kind, "reorder_tab_in_folder")
      << "Same-folder child tab drops should stay within the folder";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveTabBodySplitCreatesSplit) {
  auto state = GetState();
  std::string source_id;
  std::string target_id;
  int source_index = -1;
  int target_index = -1;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind != SidebarNodeKind::kTab ||
        !node.parent_folder_id.empty() || node.tab_strip_index < 0) {
      continue;
    }
    if (source_id.empty()) {
      source_id = node.tab_id;
      source_index = node.tab_strip_index;
    } else {
      target_id = node.tab_id;
      target_index = node.tab_strip_index;
      break;
    }
  }
  if (source_id.empty() || target_id.empty()) {
    GTEST_SKIP() << "Need 2 root normal tabs with live tab-strip indices";
  }

  SidebarTabRowView* target_row = FindLiveTabRow(target_id);
  ASSERT_TRUE(target_row) << "Live row for target tab " << target_id
                          << " not found";
  EXPECT_EQ(target_row->tab_id_for_testing(), target_id);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  Browser* browser = static_cast<Browser*>(this->browser());
  ASSERT_TRUE(browser);
  TabStripModel* tab_strip_model = browser->GetTabStripModel();
  ASSERT_TRUE(tab_strip_model);
  ASSERT_TRUE(tab_strip_model->ContainsIndex(source_index));
  ASSERT_TRUE(tab_strip_model->ContainsIndex(target_index));
  ASSERT_FALSE(tab_strip_model->GetSplitForTab(source_index).has_value());
  ASSERT_FALSE(tab_strip_model->GetSplitForTab(target_index).has_value());

  observer_.Clear();
  auto payload = MakeTabPayload(source_id, SidebarDragOrigin::kNormalSection);
  payload.tab_strip_index = source_index;
  PerformSyntheticDrop(target_row, payload, gfx::Point(5, 20));

  ASSERT_TRUE(tab_strip_model->GetSplitForTab(source_index).has_value());
  ASSERT_TRUE(tab_strip_model->GetSplitForTab(target_index).has_value());
  EXPECT_EQ(*tab_strip_model->GetSplitForTab(source_index),
            *tab_strip_model->GetSplitForTab(target_index))
      << "Tab-on-tab body drops should create a split";
  EXPECT_TRUE(observer_.events.empty())
      << "Tab-on-tab body split should not dispatch a shell move event";
}

IN_PROC_BROWSER_TEST_F(
    MahoSidebarDnDBrowserTest,
    LiveNormalTabDropOnPopulatedPinnedRowBodyPinsWithoutSplit) {
  auto state = GetState();
  std::string source_id;
  std::string target_id;
  int source_index = -1;
  int target_index = -1;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty() && node.tab_strip_index >= 0) {
      source_id = node.tab_id;
      source_index = node.tab_strip_index;
      break;
    }
  }
  for (const auto& node : state.tab_list.pinned_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty() && node.tab_strip_index >= 0) {
      target_id = node.tab_id;
      target_index = node.tab_strip_index;
      break;
    }
  }
  if (source_id.empty() || target_id.empty()) {
    GTEST_SKIP() << "Need live root tabs in both normal and pinned sections";
  }

  SidebarTabRowView* target_row = FindLiveTabRow(target_id);
  ASSERT_TRUE(target_row);
  ASSERT_EQ(target_row->section_for_testing(),
            MahoSidebarTabSection::kPinned);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_TRUE(model->ContainsIndex(source_index));
  ASSERT_TRUE(model->ContainsIndex(target_index));
  content::WebContents* source_contents =
      model->GetWebContentsAt(source_index);
  content::WebContents* target_contents =
      model->GetWebContentsAt(target_index);
  ASSERT_TRUE(source_contents);
  ASSERT_TRUE(target_contents);
  ASSERT_FALSE(model->GetSplitForTab(source_index).has_value());
  ASSERT_FALSE(model->GetSplitForTab(target_index).has_value());

  observer_.Clear();
  auto payload = MakeTabPayload(source_id, SidebarDragOrigin::kNormalSection);
  payload.tab_strip_index = source_index;
  PerformSyntheticDrop(target_row, payload, gfx::Point(5, 20));

  const int source_index_after = model->GetIndexOfWebContents(source_contents);
  const int target_index_after = model->GetIndexOfWebContents(target_contents);
  ASSERT_GE(source_index_after, 0);
  ASSERT_GE(target_index_after, 0);
  EXPECT_FALSE(model->GetSplitForTab(source_index_after).has_value());
  EXPECT_FALSE(model->GetSplitForTab(target_index_after).has_value());

  ASSERT_EQ(last_drop_result_, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveFolderBodyDropRejected) {
  auto state = GetState();
  std::string folder_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folder_id = node.folder_id;
      break;
    }
  }
  if (folder_id.empty()) {
    GTEST_SKIP() << "Need at least 1 root folder";
  }

  std::string tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && node.parent_folder_id.empty()) {
      tab_id = node.tab_id;
      break;
    }
  }
  if (tab_id.empty()) {
    GTEST_SKIP() << "Need at least 1 root tab as body target";
  }

  SidebarTabRowView* target_row = FindLiveTabRow(tab_id);
  ASSERT_TRUE(target_row) << "Live row for " << tab_id << " not found";
  EXPECT_EQ(target_row->tab_id_for_testing(), tab_id);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  observer_.Clear();
  auto payload = MakeFolderPayload(folder_id);
  PerformSyntheticDrop(target_row, payload, gfx::Point(5, 20));

  EXPECT_TRUE(observer_.events.empty());
  EXPECT_EQ(last_drop_result_, ui::mojom::DragOperation::kNone)
      << "Folder-on-tab body drops should be rejected";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                        LiveNestedFolderReorder) {
  auto state = GetState();
  std::string parent_folder_id;
  std::vector<std::string> nested_ids;
  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind != SidebarNodeKind::kFolder) continue;
    for (const auto& child : n.children) {
      if (child.kind == SidebarNodeKind::kFolder) {
        parent_folder_id = n.folder_id;
        nested_ids.push_back(child.folder_id);
      }
    }
    if (nested_ids.size() >= 2) break;
    nested_ids.clear();
    parent_folder_id.clear();
  }

  if (nested_ids.size() < 2) {
    GTEST_SKIP() << "Need a parent folder with at least 2 nested folders";
  }

  SidebarFolderRowView* target_row = FindLiveFolderRow(nested_ids[1]);
  if (!target_row) {
    GTEST_SKIP() << "Live folder row for " << nested_ids[1] << " not found";
  }
  EXPECT_EQ(target_row->folder_id_for_testing(), nested_ids[1]);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  observer_.Clear();
  auto payload = MakeNestedFolderPayload(nested_ids[0], parent_folder_id);
  PerformSyntheticDrop(target_row, payload, gfx::Point(5, 2));

  ASSERT_FALSE(observer_.events.empty());
  bool ok = observer_.events[0].kind == "reorder_folder" ||
            observer_.events[0].kind == "move_folder_into_folder";
  EXPECT_TRUE(ok) << "Expected folder reorder or move, got: "
                  << observer_.events[0].kind;

  auto post = GetState();
  bool source_exists = false;
  for (const auto& n : post.tab_list.normal_tree) {
    if (n.folder_id == nested_ids[0]) { source_exists = true; break; }
    for (const auto& child : n.children) {
      if (child.folder_id == nested_ids[0]) { source_exists = true; break; }
    }
    if (source_exists) break;
  }
  EXPECT_TRUE(source_exists) << "Nested folder should still exist in tree";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest, LiveSelfDropNoOp) {
  auto state = GetState();
  std::string tab_id;
  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kTab) {
      tab_id = n.tab_id;
      break;
    }
  }
  if (tab_id.empty()) {
    GTEST_SKIP() << "Need at least 1 normal tab";
  }

  SidebarTabRowView* target_row = FindLiveTabRow(tab_id);
  ASSERT_TRUE(target_row) << "Live row for " << tab_id << " not found";
  EXPECT_EQ(target_row->tab_id_for_testing(), tab_id);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  observer_.Clear();
  auto payload = MakeTabPayload(tab_id, SidebarDragOrigin::kNormalSection);
  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  int drag_result = target_row->OnDragUpdated(event);
  target_row->OnDragExited();

  EXPECT_TRUE(observer_.events.empty() ||
              drag_result == static_cast<int>(ui::mojom::DragOperation::kNone))
      << "Self-drop should be a no-op or rejected";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                        CrossFolderChildTabDoesNotRootReorder) {
  auto state = GetState();
  std::string folder1_id, folder2_id;
  std::string child_tab_id;
  int child_idx = -1;
  std::string target_child_id;

  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind != SidebarNodeKind::kFolder) continue;
    if (folder1_id.empty() && !n.children.empty()) {
      folder1_id = n.folder_id;
      for (const auto& c : n.children) {
        if (c.kind == SidebarNodeKind::kTab) {
          child_tab_id = c.tab_id;
          child_idx = c.folder_child_index;
          break;
        }
      }
    } else if (!folder1_id.empty() && folder2_id.empty()) {
      folder2_id = n.folder_id;
      for (const auto& c : n.children) {
        if (c.kind == SidebarNodeKind::kTab) {
          target_child_id = c.tab_id;
          break;
        }
      }
    }
  }

  if (child_tab_id.empty() || target_child_id.empty()) {
    GTEST_SKIP() << "Need 2 folders each with at least 1 child tab";
  }

  SidebarTabRowView* target_row = FindLiveTabRow(target_child_id);
  ASSERT_TRUE(target_row)
      << "Live row for " << target_child_id << " not found";
  EXPECT_EQ(target_row->tab_id_for_testing(), target_child_id);

  observer_.Clear();
  auto payload = MakeFolderChildTabPayload(child_tab_id, folder1_id,
                                           child_idx);
  PerformSyntheticDrop(target_row, payload);

  if (!observer_.events.empty()) {
    EXPECT_NE(observer_.events[0].kind, "reorder_tab")
        << "Cross-folder child tab should NOT dispatch root reorder";
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                        CrossParentFolderDoesNotReorderSameParent) {
  auto state = GetState();
  std::string source_folder_id, source_parent;
  std::string target_folder_id, target_parent;

  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind != SidebarNodeKind::kFolder) continue;
    for (const auto& child : n.children) {
      if (child.kind != SidebarNodeKind::kFolder) continue;
      if (source_folder_id.empty()) {
        source_folder_id = child.folder_id;
        source_parent = n.folder_id;
      } else if (target_folder_id.empty() && n.folder_id != source_parent) {
        target_folder_id = child.folder_id;
        target_parent = n.folder_id;
      }
    }
  }

  if (source_folder_id.empty() || target_folder_id.empty()) {
    GTEST_SKIP() << "Need nested folders under different parents";
  }

  SidebarFolderRowView* target_row = FindLiveFolderRow(target_folder_id);
  if (!target_row) {
    GTEST_SKIP() << "Live folder row for " << target_folder_id << " not found";
  }
  EXPECT_EQ(target_row->folder_id_for_testing(), target_folder_id);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  observer_.Clear();
  auto payload = MakeNestedFolderPayload(source_folder_id, source_parent);
  PerformSyntheticDrop(target_row, payload, gfx::Point(5, 2));

  for (const auto& ev : observer_.events) {
    if (ev.kind == "reorder_folder") {
      EXPECT_EQ(ev.json.find("\"parent_folder_id\":"), std::string::npos)
          << "Cross-parent folder drop should NOT dispatch same-parent reorder";
    }
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                        LiveSameFolderChildReorderFinalState) {
  auto state = GetState();
  auto folder_info = FindFolderWithTwoChildTabs(state.tab_list.normal_tree);
  if (!folder_info) {
    GTEST_SKIP() << "Need a folder with at least 2 child tabs";
  }

  int tab2_index = -1;
  for (const auto& n : state.tab_list.normal_tree) {
    if (n.kind != SidebarNodeKind::kFolder ||
        n.folder_id != folder_info->folder_id) {
      continue;
    }
    for (const auto& child : n.children) {
      if (child.kind == SidebarNodeKind::kTab &&
          child.tab_id == folder_info->tab2_id) {
        tab2_index = child.folder_child_index;
      }
    }
    break;
  }
  if (tab2_index < 0) {
    GTEST_SKIP() << "Could not resolve tab2 folder_child_index";
  }

  views::View* target_lane =
      FindLiveTabInsertionLaneBeforeTab(folder_info->tab1_id);
  if (!target_lane) {
    GTEST_SKIP() << "Live insertion lane for child tab "
                 << folder_info->tab1_id << " not found";
  }

  observer_.Clear();
  auto payload = MakeFolderChildTabPayload(
      folder_info->tab2_id, folder_info->folder_id, tab2_index);
  PerformSyntheticDrop(target_lane, payload);

  ASSERT_FALSE(observer_.events.empty())
      << "Same-folder reorder should dispatch a shell event";
  EXPECT_EQ(observer_.events[0].kind, "reorder_tab_in_folder");

  auto post = GetState();
  std::vector<std::string> post_order;
  for (const auto& n : post.tab_list.normal_tree) {
    if (n.kind != SidebarNodeKind::kFolder ||
        n.folder_id != folder_info->folder_id) {
      continue;
    }
    for (const auto& child : n.children) {
      if (child.kind == SidebarNodeKind::kTab) {
        post_order.push_back(child.tab_id);
      }
    }
    break;
  }

  ASSERT_GE(post_order.size(), 2u)
      << "Folder must still have at least 2 child tabs after reorder";

  bool tab1_present = false;
  bool tab2_present = false;
  for (const auto& id : post_order) {
    if (id == folder_info->tab1_id) tab1_present = true;
    if (id == folder_info->tab2_id) tab2_present = true;
  }
  EXPECT_TRUE(tab1_present)
      << "tab1 must survive same-folder reorder; post_order has "
      << post_order.size() << " entries";
  EXPECT_TRUE(tab2_present)
      << "tab2 must survive same-folder reorder; post_order has "
      << post_order.size() << " entries";

  size_t tab1_pos = post_order.size();
  size_t tab2_pos = post_order.size();
  for (size_t i = 0; i < post_order.size(); ++i) {
    if (post_order[i] == folder_info->tab1_id) tab1_pos = i;
    if (post_order[i] == folder_info->tab2_id) tab2_pos = i;
  }
  EXPECT_LT(tab2_pos, tab1_pos)
      << "After dropping tab2 before tab1, tab2 should precede tab1 in the "
         "final child order";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                        LiveSameIndexFavoriteNoOp) {
  auto state = GetState();
  if (state.favorites.items.empty()) {
    GTEST_SKIP() << "Need at least 1 favorite";
  }

  std::vector<std::string> pre_ids;
  pre_ids.reserve(state.favorites.items.size());
  for (const auto& item : state.favorites.items) {
    pre_ids.push_back(item.tab_id);
  }

  const std::string target_id = pre_ids[0];
  observer_.Clear();

  auto payload = MakeFavoritePayload(target_id);
  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  favorites_->OnDragUpdated(event);
  auto callback = favorites_->GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  if (callback) {
    std::move(callback).Run(event, output,
                            std::unique_ptr<ui::LayerTreeOwner>());
  }
  favorites_->OnDragExited();
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(observer_.events.size(), 1u)
      << "Favorites grid always dispatches reorder_favorite; no same-index "
         "suppression at the view layer";
  EXPECT_EQ(observer_.events[0].kind, "reorder_favorite");
  EXPECT_NE(observer_.events[0].json.find("\"new_index\":0"), std::string::npos)
      << "Same-index drop must record new_index:0";

  auto post = GetState();
  std::vector<std::string> post_ids;
  post_ids.reserve(post.favorites.items.size());
  for (const auto& item : post.favorites.items) {
    post_ids.push_back(item.tab_id);
  }

  ASSERT_EQ(post_ids.size(), pre_ids.size())
      << "Same-index favorite drop must not add or remove items";
  EXPECT_EQ(post_ids, pre_ids)
      << "Same-index reorder_favorite must leave order unchanged";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                       AddSplitCreatesTwoPaneSplitAndRetainsActiveTab) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  const int original_active_index = model->active_index();
  ASSERT_NE(original_active_index, TabStripModel::kNoTab);
  const int original_count = model->count();
  ASSERT_FALSE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));

  // Construct the controller directly; its only dependency is Browser*, the
  // narrowest seam for the split-creation contract.
  MahoSplitViewController controller(static_cast<Browser*>(browser()));
  controller.AddSplit();

  EXPECT_EQ(model->count(), original_count + 1);
  EXPECT_EQ(model->active_index(), original_active_index)
      << "Original active tab must remain active after AddSplit";
  EXPECT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()));
  EXPECT_TRUE(controller.IsSplitActive());

  const int partner_index = original_active_index + 1;
  ASSERT_TRUE(model->ContainsIndex(partner_index));
  std::optional<split_tabs::SplitTabId> active_split =
      model->GetSplitForTab(original_active_index);
  std::optional<split_tabs::SplitTabId> partner_split =
      model->GetSplitForTab(partner_index);
  ASSERT_TRUE(active_split.has_value());
  ASSERT_TRUE(partner_split.has_value());
  EXPECT_EQ(*active_split, *partner_split)
      << "Active tab and its partner must share one split ID";

  auto* split_data = model->GetSplitData(*active_split);
  ASSERT_TRUE(split_data);
  EXPECT_EQ(split_data->ListTabs().size(), 2u)
      << "AddToNewSplit must produce exactly a two-pane split";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                       AddSplitOnAlreadySplitActiveTabIsNoOp) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_NE(model->active_index(), TabStripModel::kNoTab);

  MahoSplitViewController controller(static_cast<Browser*>(browser()));
  controller.AddSplit();
  ASSERT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()))
      << "Precondition: first AddSplit must create a split";

  const int count_after_first = model->count();
  std::optional<split_tabs::SplitTabId> split_after_first =
      model->GetSplitForTab(model->active_index());
  ASSERT_TRUE(split_after_first.has_value());

  // Invoking again while the active tab is already split must be a no-op: no
  // new blank tab and no new split ID (AddSplitWithURL early-returns on
  // IsSplitActive()).
  controller.AddSplit();

  EXPECT_EQ(model->count(), count_after_first)
      << "Second AddSplit on an already-split active tab must not add a tab";
  std::optional<split_tabs::SplitTabId> split_after_second =
      model->GetSplitForTab(model->active_index());
  ASSERT_TRUE(split_after_second.has_value());
  EXPECT_EQ(*split_after_first, *split_after_second)
      << "Second AddSplit must not create a new split";
}

// Regression for the fallback split-grouping defect (plan Todo 4). Both
// degraded sidebar builders (BuildTabListModel's tab-strip else branch and
// BuildLiveStripFallbackTabListModel) now run the shared maho::GroupSplitTabs
// over their flat rows. This exercises that shared helper against a live split
// model exactly as both fallbacks feed it: a flat kTab row per strip tab with
// is_active set on the model active tab.
//
// RED baseline: maho::GroupSplitTabs was an anonymous-namespace symbol with no
// header declaration, so this cross-TU call did not compile/link at all, and
// the fallback branches never called it (ungrouped split rows). Exposing it in
// the header + wiring both fallbacks is what makes this test buildable and its
// assertions hold. Runtime GREEN is CI-deferred: the maho-login-wall gate
// SIGSEGVs every InProcessBrowserTest at startup (see plan Verification
// strategy), so this body is proven by build-clean + source-level RED here.
IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                       FallbackBuildersGroupSplitTabsWithDualActiveState) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_NE(model->active_index(), TabStripModel::kNoTab);

  MahoSplitViewController controller(static_cast<Browser*>(browser()));
  controller.AddSplit();
  ASSERT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()))
      << "Precondition: AddSplit must create a live two-pane split";

  const int split_a = model->active_index();
  const int split_b = split_a + 1;
  ASSERT_TRUE(model->ContainsIndex(split_b));
  std::optional<split_tabs::SplitTabId> sid_a = model->GetSplitForTab(split_a);
  std::optional<split_tabs::SplitTabId> sid_b = model->GetSplitForTab(split_b);
  ASSERT_TRUE(sid_a.has_value());
  ASSERT_TRUE(sid_b.has_value());
  ASSERT_EQ(*sid_a, *sid_b);

  std::vector<SidebarTreeNode> rows;
  for (int i = 0; i < model->count(); ++i) {
    SidebarTreeNode node;
    node.kind = SidebarNodeKind::kTab;
    node.tab_strip_index = i;
    node.is_active = model->active_index() == i;
    rows.push_back(std::move(node));
  }
  const size_t rows_before = rows.size();

  maho::GroupSplitTabs(rows, model);

  int split_group_count = 0;
  int lone_tab_count = 0;
  const SidebarTreeNode* split_group = nullptr;
  for (const auto& n : rows) {
    if (n.kind == SidebarNodeKind::kSplitGroup) {
      ++split_group_count;
      split_group = &n;
    } else if (n.kind == SidebarNodeKind::kTab) {
      ++lone_tab_count;
      EXPECT_FALSE(n.is_in_split)
          << "Non-split tabs must stay independent rows, not marked in-split";
    }
  }

  ASSERT_EQ(split_group_count, 1)
      << "Fallback grouping must emit exactly one kSplitGroup";
  ASSERT_TRUE(split_group);
  EXPECT_EQ(rows.size(), rows_before - 1)
      << "The two split members collapse into a single group row";
  EXPECT_EQ(lone_tab_count, model->count() - 2)
      << "Every non-split tab remains its own row";

  ASSERT_EQ(split_group->children.size(), 2u)
      << "A split group carries exactly its two ordered members";
  EXPECT_TRUE(split_group->children[0].is_in_split);
  EXPECT_TRUE(split_group->children[1].is_in_split);
  EXPECT_TRUE(split_group->children[0].is_active)
      << "Dual-active: both panes render active when either is the active tab";
  EXPECT_TRUE(split_group->children[1].is_active)
      << "Dual-active: both panes render active when either is the active tab";
  EXPECT_LT(split_group->children[0].tab_strip_index,
            split_group->children[1].tab_strip_index)
      << "Split children preserve strip order";
}

// Regression for the multi-select split eligibility gate (plan Todo 6). The
// context menu shows/enables "Open in Split View" only when >= 2 selected tabs
// are eligible (id resolves to a contained, unsplit strip index), and
// OpenSelectedInSplit collapses any larger selection to the first two tabs in
// strip order — Maho splits are strictly two-pane. With three eligible tabs,
// exactly the first two by strip index form one two-pane split and the third
// is left untouched, with no blank partner tab created.
//
// RED baseline: BuildMenuModel/IsCommandIdEnabled added and enabled the command
// unconditionally for any multi-selection (no eligibility count), so a
// three-tab selection surfaced the command with no proof that only two are
// split. CountEligibleSplitTabs() + the >= 2 gate is what makes the enable
// assertion meaningful, and the OpenSelectedInSplit contract is what leaves the
// third tab unsplit. Runtime GREEN is CI-deferred: the maho-login-wall gate
// SIGSEGVs every InProcessBrowserTest at startup (see plan Verification
// strategy), so this body is proven by build-clean + source-level RED here.
IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                       MultiSelectSplitSplitsFirstTwoEligibleLeavingThirdUnsplit) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);

  while (model->count() < 3) {
    ASSERT_TRUE(AddTabAtIndex(model->count(), GURL("chrome://maho-test/"),
                              ui::PAGE_TRANSITION_TYPED));
  }
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  std::vector<std::string> ids = UnsplitStableTabIdsInStripOrder(model);
  ASSERT_GE(ids.size(), 3u) << "Need three eligible unsplit tabs";
  const std::string first_id = ids[0];
  const std::string second_id = ids[1];
  const std::string third_id = ids[2];

  // Eligibility gate: three eligible tabs → command enabled.
  MahoTabContextMenu menu(static_cast<Browser*>(browser()), 0, nullptr, first_id,
                          {first_id, second_id, third_id});
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_OPEN_SPLIT))
      << "Three eligible tabs must enable the multi-select split command";

  tab_list_->ClearSelection();
  tab_list_->ToggleTabSelected(first_id);
  tab_list_->ToggleTabSelected(second_id);
  tab_list_->ToggleTabSelected(third_id);
  ASSERT_EQ(tab_list_->selected_tab_ids().size(), 3u);

  const int count_before = model->count();
  tab_list_->OpenSelectedInSplit();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(model->count(), count_before)
      << "Splitting existing tabs must not create a blank partner tab";

  const int first_idx = ResolveIndexForStableId(model, first_id);
  const int second_idx = ResolveIndexForStableId(model, second_id);
  const int third_idx = ResolveIndexForStableId(model, third_id);
  ASSERT_GE(first_idx, 0);
  ASSERT_GE(second_idx, 0);
  ASSERT_GE(third_idx, 0);

  std::optional<split_tabs::SplitTabId> first_split =
      model->GetSplitForTab(first_idx);
  std::optional<split_tabs::SplitTabId> second_split =
      model->GetSplitForTab(second_idx);
  ASSERT_TRUE(first_split.has_value());
  ASSERT_TRUE(second_split.has_value());
  EXPECT_EQ(*first_split, *second_split)
      << "The first two tabs by strip index must share one split ID";
  EXPECT_FALSE(model->GetSplitForTab(third_idx).has_value())
      << "The third selected tab must be left untouched (two-pane only)";

  auto* split_data = model->GetSplitData(*first_split);
  ASSERT_TRUE(split_data);
  EXPECT_EQ(split_data->ListTabs().size(), 2u)
      << "Exactly one two-pane split is created";
}

// Companion to the eligibility gate: two already-split tabs are NOT eligible
// (GetSplitForTab is set), so CountEligibleSplitTabs() < 2 and the multi-select
// split command is both disabled and absent from the model — never an enabled
// no-op. Same CI-deferred rationale as above (source-level RED + build-clean).
IN_PROC_BROWSER_TEST_F(MahoSidebarDnDBrowserTest,
                       MultiSelectSplitDisabledWhenSelectedTabsAlreadySplit) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  ASSERT_NE(model->active_index(), TabStripModel::kNoTab);

  MahoSplitViewController controller(static_cast<Browser*>(browser()));
  controller.AddSplit();
  ASSERT_TRUE((model->ContainsIndex(model->active_index()) &&
               model->GetSplitForTab(model->active_index()).has_value()))
      << "Precondition: AddSplit must create a two-pane split";

  const int split_a = model->active_index();
  const int split_b = split_a + 1;
  ASSERT_TRUE(model->ContainsIndex(split_b));
  ASSERT_TRUE(model->GetSplitForTab(split_a).has_value());
  ASSERT_TRUE(model->GetSplitForTab(split_b).has_value());

  auto stable_id_at = [&](int index) -> std::string {
    content::WebContents* wc = model->GetWebContentsAt(index);
    if (!wc) {
      return std::string();
    }
    auto* helper = MahoTabIdHelper::FromWebContents(wc);
    return helper ? helper->stable_tab_id() : std::string();
  };
  const std::string id_a = stable_id_at(split_a);
  const std::string id_b = stable_id_at(split_b);
  ASSERT_FALSE(id_a.empty());
  ASSERT_FALSE(id_b.empty());

  MahoTabContextMenu menu(static_cast<Browser*>(browser()), split_a, nullptr, id_a, {id_a, id_b});
  EXPECT_FALSE(menu.IsCommandIdEnabled(IDC_MAHO_TAB_OPEN_SPLIT))
      << "Two already-split tabs are ineligible → command disabled";

  std::unique_ptr<ui::SimpleMenuModel> built = menu.BuildMenuModel();
  ASSERT_TRUE(built);
  bool has_open_split = false;
  for (size_t i = 0; i < built->GetItemCount(); ++i) {
    if (built->GetCommandIdAt(i) == IDC_MAHO_TAB_OPEN_SPLIT) {
      has_open_split = true;
      break;
    }
  }
  EXPECT_FALSE(has_open_split)
      << "An ineligible multi-selection must omit the split command entirely";
}

}  // namespace
}  // namespace maho
