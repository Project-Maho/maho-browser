// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer_tree_owner.h"

namespace maho {
namespace {

class MahoSidebarDndInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DragReordersFavoriteTile) {
  SeedProfile(2);
  tab_list_->set_accessibility_tree_enabled_for_testing(false);
  RunAllPendingTasks();
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_GE(state.favorites.items.size(), 2u);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    if (!favorites_ || !favorites_->GetWidget()) {
      return false;
    }
    favorites_->GetWidget()->LayoutRootViewIfNecessary();
    views::View* tile = favorites_->GetTileForTesting(1);
    return tile && tile->GetVisible() && tile->parent();
  }));
  views::View* target_tile = favorites_->GetTileForTesting(1);
  ASSERT_TRUE(target_tile);
  ASSERT_TRUE(target_tile->parent());
  gfx::Point drop_point = target_tile->bounds().CenterPoint();
  views::View::ConvertPointToTarget(target_tile->parent(), favorites_,
                                    &drop_point);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = state.favorites.items[0].tab_id;
  payload.origin = SidebarDragOrigin::kFavorites;
  payload.space_id = state.tab_list.active_space_id;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(data, gfx::PointF(drop_point),
                            gfx::PointF(drop_point),
                            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(favorites_->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  auto callback = favorites_->GetDropCallbackForTesting(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  favorites_->OnDragExited();

  EXPECT_TRUE(observer_.HasEvent("reorder_favorite"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       FavoriteDropThenSelectionFastPathKeepsReorderedOrder) {
  SeedProfile(2);
  ResolveViews();
  tab_list_->set_accessibility_tree_enabled_for_testing(false);
  RunAllPendingTasks();
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_GE(state.favorites.items.size(), 2u);
  const std::string moved_tab_id = state.favorites.items[0].tab_id;
  const std::string expected_first_tab_id = state.favorites.items[1].tab_id;
  ASSERT_FALSE(moved_tab_id.empty());
  ASSERT_FALSE(expected_first_tab_id.empty());
  ASSERT_GE(browser()->GetTabStripModel()->count(), 2);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    if (!favorites_ || !favorites_->GetWidget()) {
      return false;
    }
    favorites_->GetWidget()->LayoutRootViewIfNecessary();
    views::View* tile = favorites_->GetTileForTesting(1);
    return tile && tile->GetVisible() && tile->parent();
  }));
  views::View* target_tile = favorites_->GetTileForTesting(1);
  ASSERT_TRUE(target_tile);
  ASSERT_TRUE(target_tile->parent());
  gfx::Point drop_point = target_tile->bounds().CenterPoint();
  views::View::ConvertPointToTarget(target_tile->parent(), favorites_,
                                    &drop_point);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = moved_tab_id;
  payload.origin = SidebarDragOrigin::kFavorites;
  payload.space_id = state.tab_list.active_space_id;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(data, gfx::PointF(drop_point),
                            gfx::PointF(drop_point),
                            static_cast<int>(ui::mojom::DragOperation::kMove));
  favorites_->OnDragUpdated(event);
  auto callback = favorites_->GetDropCallbackForTesting(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_TRUE(observer_.HasEvent("reorder_favorite"));
  EXPECT_EQ(favorites_->tile_tab_id_for_testing(0), expected_first_tab_id);

  const int active_index = browser()->GetTabStripModel()->active_index();
  const int next_index = active_index == 0 ? 1 : 0;
  browser()->GetTabStripModel()->ActivateTabAt(next_index);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(favorites_->tile_tab_id_for_testing(0), expected_first_tab_id)
      << "A selection-only fast path must repaint from the refreshed favorite "
         "model, not from the pre-drop order cached in state_adapter_";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragTabToPinnedSection) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.normal_tree.empty());

  std::string tab_id;
  int tab_strip_index = -1;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && node.tab_strip_index >= 0) {
      tab_id = node.tab_id;
      tab_strip_index = node.tab_strip_index;
      break;
    }
  }
  ASSERT_FALSE(tab_id.empty());

  auto* pinned_target = tab_list_->FindSectionDropTargetForTesting(
      MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = tab_strip_index;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(data, gfx::PointF(5, 2), gfx::PointF(5, 2),
                            static_cast<int>(ui::mojom::DragOperation::kMove));
  pinned_target->OnDragUpdated(event);
  auto callback = pinned_target->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  pinned_target->OnDragExited();

  EXPECT_TRUE(observer_.HasEvent("pin_tab"));
  EXPECT_TRUE(observer_.HasEvent("reorder_root_item"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragTabIntoFolder) {
  SeedProfile(2);
  AddTestTabs(1);
  observer_.Clear();

  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoSidebarStateAdapter adapter;
  auto state = adapter.BuildViewStateModel(static_cast<Browser*>(browser()));

  std::string folder_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folder_id = node.folder_id;
      break;
    }
  }
  ASSERT_FALSE(folder_id.empty()) << "Seed 2 must have at least one folder";

  std::string tab_id;
  int tab_strip_index = -1;
  std::string active_tab_id;
  int active_tab_strip_index = -1;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind != SidebarNodeKind::kTab || node.is_pinned ||
        node.tab_id.empty() || node.tab_strip_index < 0 ||
        !node.parent_folder_id.empty()) {
      continue;
    }
    if (!node.is_active) {
      tab_id = node.tab_id;
      tab_strip_index = node.tab_strip_index;
      break;
    }
    if (active_tab_id.empty()) {
      active_tab_id = node.tab_id;
      active_tab_strip_index = node.tab_strip_index;
    }
  }
  if (tab_id.empty()) {
    tab_id = active_tab_id;
    tab_strip_index = active_tab_strip_index;
  }

  if (tab_id.empty()) {
    // All normal tree tabs were active or had no strip index; use the active one.
    for (const auto& node : state.tab_list.normal_tree) {
      if (node.kind != SidebarNodeKind::kTab || node.tab_id.empty() ||
          node.tab_strip_index < 0 || node.is_pinned ||
          !node.parent_folder_id.empty()) {
        continue;
      }
      tab_id = node.tab_id;
      tab_strip_index = node.tab_strip_index;
      break;
    }
  }

  ASSERT_FALSE(tab_id.empty()) << "Expected a live normal tab to drag";
  ASSERT_GE(tab_strip_index, 0) << "Could not resolve live tab-strip index";

  auto* folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
  ASSERT_TRUE(folder_row);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = tab_strip_index;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(data,
                            gfx::PointF(5, folder_row->height() * 0.5f),
                            gfx::PointF(5, folder_row->height() * 0.5f),
                            static_cast<int>(ui::mojom::DragOperation::kMove));
  folder_row->OnDragUpdated(event);
  auto callback = folder_row->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  folder_row->OnDragExited();

  EXPECT_TRUE(observer_.HasEvent("move_tab_to_folder"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DragTabBodyCreatesSplit) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string source_id;
  std::string target_id;
  int source_index = -1;
  int target_index = -1;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind != SidebarNodeKind::kTab || !node.parent_folder_id.empty() ||
        node.tab_strip_index < 0) {
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
    GTEST_SKIP() << "Need two eligible root tabs for split drag coverage";
  }

  auto* target_row = tab_list_->FindTabRowByIdForTesting(target_id);
  ASSERT_TRUE(target_row);
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
  content::WebContents* source_contents =
      tab_strip_model->GetWebContentsAt(source_index);
  content::WebContents* target_contents =
      tab_strip_model->GetWebContentsAt(target_index);
  ASSERT_TRUE(source_contents);
  ASSERT_TRUE(target_contents);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = source_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = source_index;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event = ui::DropTargetEvent(
      data, gfx::PointF(5, 20), gfx::PointF(5, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  target_row->OnDragUpdated(event);
  EXPECT_TRUE(target_row->is_split_preview_visible_for_testing());
  EXPECT_EQ(target_row->split_preview_side_for_testing(),
            MahoSplitDropSide::kLeft);
  EXPECT_TRUE(target_row->background());
  EXPECT_TRUE(target_row->GetBorder());
  EXPECT_FALSE(tab_strip_model->GetSplitForTab(source_index).has_value());
  EXPECT_FALSE(tab_strip_model->GetSplitForTab(target_index).has_value());

  auto callback = target_row->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  target_row->OnDragExited();

  const int final_source_index =
      tab_strip_model->GetIndexOfWebContents(source_contents);
  const int final_target_index =
      tab_strip_model->GetIndexOfWebContents(target_contents);
  ASSERT_GE(final_source_index, 0);
  ASSERT_GE(final_target_index, 0);
  ASSERT_TRUE(tab_strip_model->GetSplitForTab(final_source_index).has_value());
  ASSERT_TRUE(tab_strip_model->GetSplitForTab(final_target_index).has_value());
  EXPECT_EQ(*tab_strip_model->GetSplitForTab(final_source_index),
            *tab_strip_model->GetSplitForTab(final_target_index));
  auto* split_data = tab_strip_model->GetSplitData(
      *tab_strip_model->GetSplitForTab(final_source_index));
  ASSERT_TRUE(split_data);
  const auto panes = split_data->ListTabs();
  ASSERT_EQ(panes.size(), 2u);
  EXPECT_EQ(panes[0]->GetContents(), source_contents)
      << "The left-side preview must match the committed pane order";
  EXPECT_EQ(panes[1]->GetContents(), target_contents);
  EXPECT_TRUE(observer_.events().empty())
      << "Tab-on-tab body split should not dispatch a shell move event";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       MultiTabDragOnTabBodyDoesNotPreviewOrSplit) {
  SeedProfile(2);
  AddTestTabs(1);
  RunAllPendingTasks();
  ResolveViews();

  const auto state = GetViewState();
  std::vector<const SidebarTreeNode*> tabs;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty() && node.tab_strip_index >= 0) {
      tabs.push_back(&node);
      if (tabs.size() == 3u) {
        break;
      }
    }
  }
  ASSERT_EQ(tabs.size(), 3u);

  auto* target_row = tab_list_->FindTabRowByIdForTesting(tabs[1]->tab_id);
  ASSERT_TRUE(target_row);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  content::WebContents* source_contents =
      model->GetWebContentsAt(tabs[0]->tab_strip_index);
  content::WebContents* target_contents =
      model->GetWebContentsAt(tabs[1]->tab_strip_index);
  ASSERT_TRUE(source_contents);
  ASSERT_TRUE(target_contents);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tabs[0]->tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = tabs[0]->tab_strip_index;
  payload.selected_tab_ids = {tabs[0]->tab_id, tabs[2]->tab_id};

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(
      data, gfx::PointF(5, 20), gfx::PointF(5, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(target_row->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_FALSE(target_row->is_split_preview_visible_for_testing());

  auto callback = target_row->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kNone);
  EXPECT_FALSE(model->GetSplitForTab(
      model->GetIndexOfWebContents(source_contents)).has_value());
  EXPECT_FALSE(model->GetSplitForTab(
      model->GetIndexOfWebContents(target_contents)).has_value());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       AlreadySplitSourceDoesNotPreviewOrSplit) {
  SeedProfile(2);
  AddTestTabs(1);
  RunAllPendingTasks();
  ResolveViews();

  const auto state = GetViewState();
  std::vector<const SidebarTreeNode*> tabs;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty() && node.tab_strip_index >= 0) {
      tabs.push_back(&node);
      if (tabs.size() == 3u) {
        break;
      }
    }
  }
  ASSERT_EQ(tabs.size(), 3u);

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  content::WebContents* source_contents =
      model->GetWebContentsAt(tabs[0]->tab_strip_index);
  content::WebContents* target_contents =
      model->GetWebContentsAt(tabs[1]->tab_strip_index);
  content::WebContents* partner_contents =
      model->GetWebContentsAt(tabs[2]->tab_strip_index);
  ASSERT_TRUE(source_contents);
  ASSERT_TRUE(target_contents);
  ASSERT_TRUE(partner_contents);

  model->ActivateTabAt(tabs[2]->tab_strip_index);
  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kSideBySide, 0.5);
  model->AddToNewSplit({model->GetIndexOfWebContents(source_contents)},
                       visual_data,
                       split_tabs::SplitTabCreatedSource::kDragAndDropTab);
  const auto source_split =
      model->GetSplitForTab(model->GetIndexOfWebContents(source_contents));
  ASSERT_TRUE(source_split.has_value());

  RunAllPendingTasks();
  ResolveViews();
  auto* target_row =
      tab_list_->FindTabRowByIdForTesting(tabs[1]->tab_id);
  ASSERT_TRUE(target_row);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tabs[0]->tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = model->GetIndexOfWebContents(source_contents);

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(
      data, gfx::PointF(5, 20), gfx::PointF(5, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(target_row->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_FALSE(target_row->is_split_preview_visible_for_testing());

  auto callback = target_row->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kNone);
  EXPECT_EQ(model->GetSplitForTab(
                model->GetIndexOfWebContents(source_contents)),
            source_split);
  EXPECT_FALSE(model->GetSplitForTab(
      model->GetIndexOfWebContents(target_contents)).has_value());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       AlreadySplitTargetDoesNotPreviewOrSplit) {
  SeedProfile(2);
  AddTestTabs(1);
  RunAllPendingTasks();
  ResolveViews();

  const auto state = GetViewState();
  std::vector<const SidebarTreeNode*> tabs;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty() && node.tab_strip_index >= 0) {
      tabs.push_back(&node);
      if (tabs.size() == 3u) {
        break;
      }
    }
  }
  ASSERT_EQ(tabs.size(), 3u);

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  content::WebContents* source_contents =
      model->GetWebContentsAt(tabs[0]->tab_strip_index);
  content::WebContents* target_contents =
      model->GetWebContentsAt(tabs[1]->tab_strip_index);
  content::WebContents* partner_contents =
      model->GetWebContentsAt(tabs[2]->tab_strip_index);
  ASSERT_TRUE(source_contents);
  ASSERT_TRUE(target_contents);
  ASSERT_TRUE(partner_contents);

  model->ActivateTabAt(tabs[1]->tab_strip_index);
  split_tabs::SplitTabVisualData visual_data(
      split_tabs::SplitTabLayout::kSideBySide, 0.5);
  model->AddToNewSplit({model->GetIndexOfWebContents(partner_contents)},
                       visual_data,
                       split_tabs::SplitTabCreatedSource::kDragAndDropTab);
  const auto target_split =
      model->GetSplitForTab(model->GetIndexOfWebContents(target_contents));
  ASSERT_TRUE(target_split.has_value());

  RunAllPendingTasks();
  ResolveViews();
  auto* target_row =
      tab_list_->FindTabRowByIdForTesting(tabs[1]->tab_id);
  ASSERT_TRUE(target_row);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tabs[0]->tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = model->GetIndexOfWebContents(source_contents);

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(
      data, gfx::PointF(215, 20), gfx::PointF(215, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(target_row->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_FALSE(target_row->is_split_preview_visible_for_testing());

  auto callback = target_row->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kNone);
  EXPECT_FALSE(model->GetSplitForTab(
      model->GetIndexOfWebContents(source_contents)).has_value());
  EXPECT_EQ(model->GetSplitForTab(
                model->GetIndexOfWebContents(target_contents)),
            target_split);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       RightPreviewCommitsSourceOnRight) {
  SeedProfile(2);

  const auto state = GetViewState();
  std::vector<const SidebarTreeNode*> tabs;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty() && node.tab_strip_index >= 0) {
      tabs.push_back(&node);
      if (tabs.size() == 2u) {
        break;
      }
    }
  }
  ASSERT_EQ(tabs.size(), 2u);

  auto* target_row = tab_list_->FindTabRowByIdForTesting(tabs[1]->tab_id);
  ASSERT_TRUE(target_row);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_TRUE(model);
  content::WebContents* source_contents =
      model->GetWebContentsAt(tabs[0]->tab_strip_index);
  content::WebContents* target_contents =
      model->GetWebContentsAt(tabs[1]->tab_strip_index);
  ASSERT_TRUE(source_contents);
  ASSERT_TRUE(target_contents);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tabs[0]->tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = tabs[0]->tab_strip_index;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(
      data, gfx::PointF(215, 20), gfx::PointF(215, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  target_row->OnDragUpdated(event);
  EXPECT_EQ(target_row->split_preview_side_for_testing(),
            MahoSplitDropSide::kRight);

  auto callback = target_row->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  const auto split_id =
      model->GetSplitForTab(model->GetIndexOfWebContents(source_contents));
  ASSERT_TRUE(split_id.has_value());
  auto* split_data = model->GetSplitData(*split_id);
  ASSERT_TRUE(split_data);
  const auto panes = split_data->ListTabs();
  ASSERT_EQ(panes.size(), 2u);
  EXPECT_EQ(panes[0]->GetContents(), target_contents);
  EXPECT_EQ(panes[1]->GetContents(), source_contents)
      << "The right-side preview must match the committed pane order";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       OnDragDoneClearsSplitPreview) {
  SeedProfile(2);

  const auto state = GetViewState();
  std::vector<const SidebarTreeNode*> tabs;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty() && node.tab_strip_index >= 0) {
      tabs.push_back(&node);
      if (tabs.size() == 2u) {
        break;
      }
    }
  }
  ASSERT_EQ(tabs.size(), 2u);

  auto* target_row = tab_list_->FindTabRowByIdForTesting(tabs[1]->tab_id);
  ASSERT_TRUE(target_row);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tabs[0]->tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = tabs[0]->tab_strip_index;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(
      data, gfx::PointF(5, 20), gfx::PointF(5, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  target_row->OnDragUpdated(event);
  ASSERT_TRUE(target_row->is_split_preview_visible_for_testing());

  target_row->OnDragDone();

  EXPECT_FALSE(target_row->is_drop_indicator_visible_for_testing());
  EXPECT_FALSE(target_row->is_split_preview_visible_for_testing());
  EXPECT_EQ(target_row->drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kNone);
  EXPECT_FALSE(target_row->background());
  EXPECT_FALSE(target_row->GetBorder());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DragReordersRegularTabs) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::vector<std::string> root_tab_ids;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty()) {
      root_tab_ids.push_back(node.tab_id);
      if (root_tab_ids.size() == 2)
        break;
    }
  }
  ASSERT_EQ(2u, root_tab_ids.size());

  auto* first = tab_list_->FindTabRowByIdForTesting(root_tab_ids[0]);
  auto* first_lane =
      tab_list_->FindInsertionLaneBeforeTabByIdForTesting(root_tab_ids[0]);
  ASSERT_TRUE(first);
  ASSERT_TRUE(first_lane);

  DragViewToView(tab_list_->FindTabRowByIdForTesting(root_tab_ids[1]),
                 first_lane);

  EXPECT_TRUE(observer_.HasEvent("reorder_root_item"));

  auto post_state = GetViewState();
  std::vector<std::string> post_root_tab_ids;
  for (const auto& node : post_state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab &&
        node.parent_folder_id.empty()) {
      post_root_tab_ids.push_back(node.tab_id);
    }
  }
  ASSERT_GE(post_root_tab_ids.size(), 2u);
  EXPECT_EQ(post_root_tab_ids[0], root_tab_ids[1]);
  EXPECT_EQ(post_root_tab_ids[1], root_tab_ids[0]);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragReordersFolder) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::vector<std::string> folder_ids;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder &&
        node.parent_of_folder_id.empty()) {
      folder_ids.push_back(node.folder_id);
    }
  }
  ASSERT_GE(folder_ids.size(), 2u)
      << "Seed 2 must have at least two sibling root folders";

  auto* target_lane =
      tab_list_->FindInsertionLaneBeforeFolderByIdForTesting(folder_ids[1]);
  ASSERT_TRUE(target_lane);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kFolder;
  payload.node_id = folder_ids[0];
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  const gfx::Point point(5, 2);
  ui::DropTargetEvent event = ui::DropTargetEvent(
      data, gfx::PointF(point), gfx::PointF(point),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  target_lane->OnDragUpdated(event);
  auto callback = target_lane->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  target_lane->OnDragExited();

  EXPECT_TRUE(observer_.HasEvent("reorder_folder"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragTabOutOfFolder) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string child_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      for (const auto& child : node.children) {
        if (child.kind == SidebarNodeKind::kTab) {
          child_tab_id = child.tab_id;
          break;
        }
      }
      if (!child_tab_id.empty())
        break;
    }
  }
  ASSERT_FALSE(child_tab_id.empty())
      << "Seed 2 must have a tab inside a folder";

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(child_tab_id);
  ASSERT_TRUE(tab_row);

  views::View* normal_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_TRUE(normal_target);

  DragViewToPoint(tab_row, GetViewPoint(normal_target, 0.5, 0.05));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return observer_.HasEvent("move_tab_to_root");
  }));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest, DragUnpinTab) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.pinned_tree.empty());

  std::string pinned_tab_id;
  int tab_strip_index = -1;
  for (const auto& node : state.tab_list.pinned_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      pinned_tab_id = node.tab_id;
      tab_strip_index = node.tab_strip_index;
      break;
    }
  }
  ASSERT_FALSE(pinned_tab_id.empty());

  auto* target_row = tab_list_->FindSectionDropTargetForTesting(
      MahoSidebarTabSection::kNormal);
  ASSERT_TRUE(target_row);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = pinned_tab_id;
  payload.origin = SidebarDragOrigin::kPinnedSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = tab_strip_index;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  const gfx::Point point(5, 2);
  ui::DropTargetEvent event = ui::DropTargetEvent(
      data, gfx::PointF(point), gfx::PointF(point),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  target_row->OnDragUpdated(event);
  auto callback = target_row->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  EXPECT_TRUE(observer_.HasEvent("unpin_tab"));
  target_row->OnDragExited();
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DragCancelRestoresState) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string source_tab_id;
  std::string target_tab_id;
  int source_index = -1;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind != SidebarNodeKind::kTab || node.tab_strip_index < 0) {
      continue;
    }
    if (source_tab_id.empty()) {
      source_tab_id = node.tab_id;
      source_index = node.tab_strip_index;
    } else {
      target_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(source_tab_id.empty());
  ASSERT_FALSE(target_tab_id.empty());

  auto* target_row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
  ASSERT_TRUE(target_row);
  EXPECT_EQ(target_row->tab_id_for_testing(), target_tab_id);
  target_row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = source_tab_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = source_index;

  Browser* browser = static_cast<Browser*>(this->browser());
  ASSERT_TRUE(browser);
  TabStripModel* tab_strip_model = browser->GetTabStripModel();
  ASSERT_TRUE(tab_strip_model);
  const int target_index = target_row->live_tab_index_for_testing();
  ASSERT_GE(target_index, 0);
  ASSERT_FALSE(tab_strip_model->GetSplitForTab(source_index).has_value());
  ASSERT_FALSE(tab_strip_model->GetSplitForTab(target_index).has_value());

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent left_event = ui::DropTargetEvent(
      data, gfx::PointF(5, 20), gfx::PointF(5, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  target_row->OnDragUpdated(left_event);
  EXPECT_TRUE(target_row->is_drop_indicator_visible_for_testing());
  EXPECT_TRUE(target_row->is_split_preview_visible_for_testing());
  EXPECT_EQ(target_row->drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kSplit);
  EXPECT_EQ(target_row->split_preview_side_for_testing(),
            MahoSplitDropSide::kLeft);
  EXPECT_TRUE(target_row->background());
  EXPECT_TRUE(target_row->GetBorder());

  ui::DropTargetEvent right_event = ui::DropTargetEvent(
      data, gfx::PointF(215, 20), gfx::PointF(215, 20),
      static_cast<int>(ui::mojom::DragOperation::kMove));
  target_row->OnDragUpdated(right_event);
  EXPECT_EQ(target_row->split_preview_side_for_testing(),
            MahoSplitDropSide::kRight);
  EXPECT_FALSE(tab_strip_model->GetSplitForTab(source_index).has_value());
  EXPECT_FALSE(tab_strip_model->GetSplitForTab(target_index).has_value());

  target_row->OnDragExited();

  EXPECT_FALSE(observer_.HasEvent("reorder_tab"));
  EXPECT_FALSE(observer_.HasEvent("pin_tab"));
  EXPECT_FALSE(observer_.HasEvent("move_tab_to_root"));
  EXPECT_FALSE(target_row->is_drop_indicator_visible_for_testing());
  EXPECT_FALSE(target_row->is_split_preview_visible_for_testing());
  EXPECT_EQ(target_row->drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kNone);
  EXPECT_FALSE(target_row->background());
  EXPECT_FALSE(target_row->GetBorder());
  EXPECT_FALSE(tab_strip_model->GetSplitForTab(source_index).has_value());
  EXPECT_FALSE(tab_strip_model->GetSplitForTab(target_index).has_value());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       FavoriteTabDropOnPinnedSectionDispatchesPinTab) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_GE(state.favorites.items.size(), 1u);

  views::View* fav_tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(fav_tile);

  views::View* pinned_target =
      tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);

  DragViewToView(fav_tile, pinned_target);

  EXPECT_TRUE(observer_.HasEvent("change_tab_role"))
      << "Favorites->pinned drop must dispatch change_tab_role event";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarDndInteractiveTest,
                       DuplicatePinDefended) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.pinned_tree.empty());

  std::string already_pinned_id;
  int tab_strip_index = -1;
  for (const auto& node : state.tab_list.pinned_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      already_pinned_id = node.tab_id;
      tab_strip_index = node.tab_strip_index;
      break;
    }
  }
  ASSERT_FALSE(already_pinned_id.empty());

  auto* pinned_target = tab_list_->FindSectionDropTargetForTesting(
      MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);

  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = already_pinned_id;
  payload.origin = SidebarDragOrigin::kPinnedSection;
  payload.space_id = state.tab_list.active_space_id;
  payload.tab_strip_index = tab_strip_index;

  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  ui::DropTargetEvent event(data, gfx::PointF(5, 5), gfx::PointF(5, 5),
                            static_cast<int>(ui::mojom::DragOperation::kMove));
  pinned_target->OnDragUpdated(event);
  auto callback = pinned_target->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  pinned_target->OnDragExited();

  auto post_state = GetViewState();
  EXPECT_FALSE(observer_.HasEvent("pin_tab"));
  EXPECT_TRUE(observer_.HasEvent("reorder_root_item"));
  EXPECT_EQ(post_state.tab_list.pinned_tree.size(), state.tab_list.pinned_tree.size());
  EXPECT_EQ(post_state.tab_list.pinned_tree.size(), state.tab_list.pinned_tree.size());
}

}  // namespace
}  // namespace maho
