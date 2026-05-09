// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"

#include <memory>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/views/test/views_test_base.h"

namespace maho {
namespace {

class RecordingShellEventObserver : public ShellEventObserver {
 public:
  void OnShellEventDispatched(const std::string& kind,
                              const std::string& event_json) override {
    events.push_back({kind, event_json});
  }

  struct EventRecord {
    std::string kind;
    std::string json;
  };

  std::vector<EventRecord> events;
};

SidebarTreeNode MakeTabNode(const std::string& tab_id, int tab_index) {
  SidebarTreeNode node;
  node.kind = SidebarNodeKind::kTab;
  node.tab_id = tab_id;
  node.tab_strip_index = tab_index;
  node.title = u"Tab";
  node.host = u"example.com";
  return node;
}

SidebarTreeNode MakeFolderChildTabNode(const std::string& tab_id,
                                       int tab_index,
                                       const std::string& folder_id,
                                       int folder_child_index) {
  SidebarTreeNode node = MakeTabNode(tab_id, tab_index);
  node.parent_folder_id = folder_id;
  node.folder_child_index = folder_child_index;
  node.depth = 1;
  return node;
}

SidebarTreeNode MakeFolderNode(const std::string& folder_id, int depth = 0) {
  SidebarTreeNode node;
  node.kind = SidebarNodeKind::kFolder;
  node.folder_id = folder_id;
  node.folder_name = u"Folder";
  node.folder_is_pinned = false;
  node.depth = depth;
  return node;
}

SidebarTreeNode MakeNestedFolderNode(const std::string& folder_id,
                                     const std::string& parent_folder_id,
                                     int depth = 1) {
  SidebarTreeNode node = MakeFolderNode(folder_id, depth);
  node.parent_of_folder_id = parent_folder_id;
  return node;
}

SidebarDragPayload MakeTabPayload(const std::string& tab_id,
                                  SidebarDragOrigin origin) {
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = tab_id;
  payload.origin = origin;
  payload.space_id = "space-1";
  return payload;
}

SidebarDragPayload MakeFolderChildTabPayload(const std::string& tab_id,
                                             const std::string& folder_id,
                                             int folder_child_index) {
  SidebarDragPayload payload = MakeTabPayload(
      tab_id, SidebarDragOrigin::kNormalSection);
  payload.source_parent_folder_id = folder_id;
  payload.source_folder_child_index = folder_child_index;
  return payload;
}

SidebarDragPayload MakeFolderPayload(const std::string& folder_id) {
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kFolder;
  payload.node_id = folder_id;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = "space-1";
  return payload;
}

SidebarDragPayload MakeNestedFolderPayload(const std::string& folder_id,
                                           const std::string& parent_folder_id) {
  SidebarDragPayload payload = MakeFolderPayload(folder_id);
  payload.source_parent_folder_id = parent_folder_id;
  return payload;
}

void WriteDropData(const SidebarDragPayload& payload,
                   ui::OSExchangeData* data) {
  WriteMahoDragData(payload, data);
}

ui::DropTargetEvent MakeDropEvent(const ui::OSExchangeData& data,
                                  const gfx::Point& point = gfx::Point(1, 1)) {
  return ui::DropTargetEvent(
      data, gfx::PointF(point), gfx::PointF(point),
      static_cast<int>(ui::mojom::DragOperation::kMove));
}

std::optional<base::Value> ParseJson(const std::string& json) {
  return base::JSONReader::Read(json, base::JSON_PARSE_RFC);
}

views::Button::PressedCallback NoOpPressedCallback() {
  return base::BindRepeating([](const ui::Event&) {});
}

class SidebarDnDDispatchTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    SetShellEventObserverForTesting(&observer_);
  }

  void TearDown() override {
    SetShellEventObserverForTesting(nullptr);
    views::ViewsTestBase::TearDown();
  }

 protected:
  RecordingShellEventObserver observer_;
};

TEST_F(SidebarDnDDispatchTest, RootTabInsertionLaneDispatchesReorderRootItem) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  lane->OnDragExited();

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"target\""), std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"target-tab\""),
            std::string::npos);

  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("item") != nullptr);
  ASSERT_TRUE(dict.Find("insertion_point") != nullptr);
}

TEST_F(SidebarDnDDispatchTest,
       FolderChildTabDropOnRootTabDispatchesMoveTabToRootBeforeTarget) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload = MakeTabPayload(
      "source-tab", SidebarDragOrigin::kNormalSection);
  payload.source_parent_folder_id = "folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"space_id\":\"space-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"source-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"before_tab_id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       SameFolderTabInsertionLaneBeforeTargetDispatchesReorderTabInFolder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode folder = MakeFolderNode("folder-1");
  folder.is_expanded = true;
  folder.children.push_back(MakeFolderChildTabNode("source-tab", 2, "folder-1", 0));
  folder.children.push_back(MakeFolderChildTabNode("target-tab", 1, "folder-1", 1));
  model.normal_tree.push_back(folder);
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderChildTabPayload("source-tab", "folder-1", 2),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);

  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_tab_in_folder");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"to\":1"),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       SameFolderTabInsertionLaneBeforeNextSiblingDispatchesReorderTabInFolder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode folder = MakeFolderNode("folder-1");
  folder.is_expanded = true;
  folder.children.push_back(MakeFolderChildTabNode("source-tab", 0, "folder-1", 0));
  folder.children.push_back(MakeFolderChildTabNode("target-tab", 1, "folder-1", 1));
  model.normal_tree.push_back(folder);
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderChildTabPayload("source-tab", "folder-1", 0), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);

  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_tab_in_folder");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"to\":1"),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, SameFolderTabSelfDropReturnsNoOp) {
  SidebarTreeNode target_node =
      MakeFolderChildTabNode("same-tab", 1, "folder-1", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderChildTabPayload("same-tab", "folder-1", 1), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));
  auto callback = row.GetDropCallbackForTesting(event);

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::DropZoneForTesting::kNone);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kMove);
  EXPECT_TRUE(observer_.events.empty());
}

TEST_F(SidebarDnDDispatchTest,
       CrossFolderChildTabDropDoesNotDispatchIncorrectRootReorder) {
  SidebarTreeNode target_node =
      MakeFolderChildTabNode("target-tab", 1, "folder-2", 0);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderChildTabPayload("source-tab", "folder-1", 0), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_NE(observer_.events[0].kind, "reorder_tab");
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
}

TEST_F(SidebarDnDDispatchTest, TabDropToPinnedSectionDispatchesPinTab) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  const auto state = GetSectionDropIndicatorStateForTesting(
      MahoSidebarTabSection::kPinned, event);
  EXPECT_TRUE(state.visible_after_update);
  EXPECT_FALSE(state.visible_after_exit);
  auto callback = GetSectionDropCallbackForTesting(MahoSidebarTabSection::kPinned,
                                                   event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"source-tab\""),
            std::string::npos);

  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("tab_id") != nullptr);
}

TEST_F(SidebarDnDDispatchTest,
       FolderChildTabDropToPinnedSectionDispatchesAtomicRootReturnFirst) {
  SidebarDragPayload payload = MakeTabPayload(
      "source-tab", SidebarDragOrigin::kNormalSection);
  payload.source_parent_folder_id = "folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(MahoSidebarTabSection::kPinned,
                                                   event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "pin_tab");
}

TEST_F(SidebarDnDDispatchTest,
       FolderChildTabDropToNormalSectionDispatchesMoveTabToRootOnly) {
  SidebarDragPayload payload = MakeTabPayload(
      "source-tab", SidebarDragOrigin::kNormalSection);
  payload.source_parent_folder_id = "folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(MahoSidebarTabSection::kNormal,
                                                   event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[0].json.find("\"before_tab_id\":"),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, FavoriteReorderDispatchesReorderFavorite) {
  MahoSidebarFavoritesGridView favorites(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;
  MahoSidebarFavoriteItemModel first;
  first.tab_id = "fav-1";
  first.url = GURL("https://one.example");
  first.title = u"One";
  model.items.push_back(first);
  MahoSidebarFavoriteItemModel second;
  second.tab_id = "fav-2";
  second.url = GURL("https://two.example");
  second.title = u"Two";
  model.items.push_back(second);
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 80));
  favorites.Update(model);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-2", SidebarDragOrigin::kFavorites),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = favorites.GetDropCallbackForTesting(event);
  EXPECT_FALSE(favorites.is_drop_indicator_visible_for_testing());
  EXPECT_EQ(favorites.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(favorites.is_drop_indicator_visible_for_testing());
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  favorites.OnDragExited();
  EXPECT_FALSE(favorites.is_drop_indicator_visible_for_testing());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_favorite");
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-2\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"new_index\":1"),
            std::string::npos);

  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("tab_id") != nullptr);
  ASSERT_TRUE(dict.Find("new_index") != nullptr);
}

TEST_F(SidebarDnDDispatchTest, FolderRowDropIndicatorResetsOnDragExit) {
  SidebarTreeNode folder_node = MakeFolderNode("folder-1");

  SidebarFolderRowView folder_row(
      folder_node, MahoSidebarTabSection::kNormal, "space-1",
      NoOpPressedCallback());

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  EXPECT_FALSE(folder_row.is_drop_indicator_visible_for_testing());
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(folder_row.is_drop_indicator_visible_for_testing());
  folder_row.OnDragExited();
  EXPECT_FALSE(folder_row.is_drop_indicator_visible_for_testing());
}

TEST_F(SidebarDnDDispatchTest, RootFolderDropTopZoneDispatchesReorderBefore) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeFolderNode("target-folder"));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = lane->GetDropCallback(event);
  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"before_folder_id\":\"target-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       RootFolderInsertionLaneBeforeNextSiblingDispatchesReorderFolder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeFolderNode("target-folder"));
  model.normal_tree.push_back(MakeFolderNode("next-folder"));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeFolderByIdForTesting("next-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = lane->GetDropCallback(event);
  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"before_folder_id\":\"next-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       RootFolderDropMiddleZoneDispatchesMoveFolderIntoFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  NoOpPressedCallback(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::DropZoneForTesting::kInto);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_folder_into_folder");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"target_folder_id\":\"target-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, FolderDropOntoSelfReturnsNoOp) {
  SidebarFolderRowView folder_row(MakeFolderNode("same-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  NoOpPressedCallback(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("same-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::DropZoneForTesting::kNone);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kNone);
  EXPECT_TRUE(observer_.events.empty());
}

TEST_F(SidebarDnDDispatchTest,
       NestedSameParentFolderInsertionLaneBeforeTargetDispatchesReorderFolder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode parent = MakeFolderNode("parent-folder");
  parent.is_expanded = true;
  parent.children.push_back(MakeNestedFolderNode("source-folder", "parent-folder"));
  parent.children.push_back(MakeNestedFolderNode("target-folder", "parent-folder"));
  model.normal_tree.push_back(parent);
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeNestedFolderPayload("source-folder", "parent-folder"),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = lane->GetDropCallback(event);
  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  EXPECT_NE(observer_.events[0].json.find("\"parent_folder_id\":\"parent-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"before_folder_id\":\"target-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       NestedSameParentFolderInsertionLaneBeforeNextSiblingDispatchesReorderFolder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode parent = MakeFolderNode("parent-folder");
  parent.is_expanded = true;
  parent.children.push_back(MakeNestedFolderNode("source-folder", "parent-folder"));
  parent.children.push_back(MakeNestedFolderNode("target-folder", "parent-folder"));
  model.normal_tree.push_back(parent);
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeNestedFolderPayload("source-folder", "parent-folder"),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = lane->GetDropCallback(event);
  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  EXPECT_NE(observer_.events[0].json.find("\"space_id\":\"space-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"before_folder_id\":\"target-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       CrossParentNestedFolderTopZoneDoesNotDispatchIncorrectReorder) {
  SidebarFolderRowView folder_row(
      MakeNestedFolderNode("target-folder", "target-parent"),
      MahoSidebarTabSection::kNormal, "space-1", NoOpPressedCallback(),
      "next-folder", "target-parent");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeNestedFolderPayload("source-folder", "source-parent"),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  auto callback = folder_row.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_folder_into_folder");
  EXPECT_EQ(observer_.events[0].json.find("\"parent_folder_id\":"),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       CrossParentNestedFolderBottomZoneDoesNotDispatchIncorrectReorder) {
  SidebarFolderRowView folder_row(
      MakeNestedFolderNode("target-folder", "target-parent"),
      MahoSidebarTabSection::kNormal, "space-1", NoOpPressedCallback(),
      "next-folder", "target-parent");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeNestedFolderPayload("source-folder", "source-parent"),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 39));

  auto callback = folder_row.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_folder_into_folder");
  EXPECT_EQ(observer_.events[0].json.find("\"parent_folder_id\":"),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       TabDropOnFolderMiddleZoneDispatchesMoveTabToFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  NoOpPressedCallback());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_folder");
  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("space_id"), "space-1");
  EXPECT_EQ(*dict.FindString("folder_id"), "target-folder");
  EXPECT_EQ(*dict.FindString("tab_id"), "source-tab");
}

TEST_F(SidebarDnDDispatchTest,
       FolderChildTabDropOnFolderMiddleZoneDispatchesMoveTabToFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  NoOpPressedCallback());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderChildTabPayload("source-tab", "old-folder", 0),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  auto callback = folder_row.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_folder");
  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("folder_id"), "target-folder");
  EXPECT_EQ(*dict.FindString("tab_id"), "source-tab");
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropToPinnedSectionDispatchesPinFolder) {
  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(MahoSidebarTabSection::kPinned,
                                                   event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_GE(observer_.events.size(), 1u);
  bool found_pin = false;
  for (const auto& ev : observer_.events) {
    if (ev.kind == "pin_folder") {
      found_pin = true;
      EXPECT_NE(ev.json.find("\"folder_id\":\"source-folder\""),
                std::string::npos);
    }
  }
  EXPECT_TRUE(found_pin);
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropToNormalSectionDispatchesUnpinFolder) {
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kFolder;
  payload.node_id = "source-folder";
  payload.origin = SidebarDragOrigin::kPinnedSection;
  payload.space_id = "space-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(MahoSidebarTabSection::kNormal,
                                                   event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_GE(observer_.events.size(), 1u);
  bool found_unpin = false;
  for (const auto& ev : observer_.events) {
    if (ev.kind == "unpin_folder") {
      found_unpin = true;
      EXPECT_NE(ev.json.find("\"folder_id\":\"source-folder\""),
                std::string::npos);
    }
  }
  EXPECT_TRUE(found_unpin);
}

TEST_F(SidebarDnDDispatchTest,
       NestedFolderDragPayloadContainsSourceParentFolderId) {
  SidebarTreeNode nested_folder = MakeNestedFolderNode("child-f", "parent-f");
  SidebarFolderRowView folder_row(
      nested_folder, MahoSidebarTabSection::kNormal, "space-1",
      NoOpPressedCallback(), "", "parent-f");

  ui::OSExchangeData data;
  folder_row.WriteDragDataForView(&folder_row, gfx::Point(5, 5), &data);

  SidebarDragPayload read_payload;
  ASSERT_TRUE(ReadMahoDragData(data, read_payload));
  EXPECT_EQ(read_payload.node_kind, SidebarNodeKind::kFolder);
  EXPECT_EQ(read_payload.node_id, "child-f");
  EXPECT_EQ(read_payload.source_parent_folder_id, "parent-f");
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneIdleHiddenNoSectionTarget) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_FALSE(pinned_target->GetVisible());

  auto* rows = list.tab_rows_for_testing();
  ASSERT_TRUE(rows);
  EXPECT_EQ(rows->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneRevealedDuringValidInternalDrag) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_TRUE(list.is_pinned_drop_lane_revealed_for_testing());
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_TRUE(pinned_target->GetVisible());

  auto* rows = list.tab_rows_for_testing();
  ASSERT_TRUE(rows);
  EXPECT_EQ(rows->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneRevealedTargetSurvivesAndDispatchesPinAfterReveal) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  SidebarDragPayload payload = MakeTabPayload(
      "source-tab", SidebarDragOrigin::kNormalSection);
  payload.source_parent_folder_id = "folder-1";
  payload.source_folder_child_index = 0;
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_TRUE(list.is_pinned_drop_lane_revealed_for_testing());
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_TRUE(pinned_target->GetVisible());

  auto callback = GetSectionDropCallbackForTesting(MahoSidebarTabSection::kPinned,
                                                   event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "pin_tab");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"source-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneHidesAfterDragExit) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);
  ASSERT_TRUE(list.is_pinned_drop_lane_revealed_for_testing());

  list.OnDragExited();

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_FALSE(pinned_target->GetVisible());
  EXPECT_EQ(list.tab_rows_for_testing()->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneNotRevealedForFavoriteDrag) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-1", SidebarDragOrigin::kFavorites), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_FALSE(pinned_target->GetVisible());
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneNotRevealedForExternalUrlDrag) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  data.SetURL(GURL("https://example.com"), u"Example");
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
}

TEST_F(SidebarDnDDispatchTest,
       NonEmptyPinnedNotAffectedByRevealLogic) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode pinned = MakeTabNode("pinned-1", 0);
  pinned.is_pinned = true;
  model.pinned_tree.push_back(pinned);
  model.normal_tree.push_back(MakeTabNode("tab-1", 1));
  list.Update(model, /*browser=*/nullptr);

  EXPECT_NE(list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned),
            nullptr);
  EXPECT_EQ(list.tab_rows_for_testing()->children().size(), 4u);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);
  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());

  list.OnDragExited();
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_NE(pinned_target, nullptr);
  EXPECT_TRUE(pinned_target->GetVisible());
  EXPECT_EQ(list.tab_rows_for_testing()->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       RevealedEmptyPinnedSectionHasNonZeroHeight) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_NE(pinned_target, nullptr);
  EXPECT_GT(pinned_target->GetPreferredSize().height(), 0)
      << "Revealed empty pinned section must have non-zero height for drop targeting";
}

TEST_F(SidebarDnDDispatchTest,
       UpdateResetsRevealedPinnedDropLane) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);
  ASSERT_TRUE(list.is_pinned_drop_lane_revealed_for_testing());

  list.Update(model, /*browser=*/nullptr);

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing())
      << "Update() must clear the transient reveal flag";
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target)
      << "Empty pinned section must still exist after Update()";
  EXPECT_FALSE(pinned_target->GetVisible())
      << "Empty pinned section must be hidden at idle after Update()";
  EXPECT_EQ(list.tab_rows_for_testing()->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       UpdateWithNewPinnedContentClearsRevealFlag) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);
  ASSERT_TRUE(list.is_pinned_drop_lane_revealed_for_testing());

  SidebarTreeNode pinned = MakeTabNode("pinned-1", 0);
  pinned.is_pinned = true;
  model.pinned_tree.push_back(pinned);
  list.Update(model, /*browser=*/nullptr);

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing())
      << "Reveal flag must be cleared even when new pinned content arrives";
  EXPECT_NE(list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned),
             nullptr)
      << "Pinned section should be visible because it now has content";
  EXPECT_EQ(list.tab_rows_for_testing()->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest, FolderOnTabBodyIsNoOp) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 30));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 15));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::DropZoneForTesting::kNone);
  EXPECT_FALSE(row.is_drop_indicator_visible_for_testing());
}

TEST_F(SidebarDnDDispatchTest, FolderOnTabLaneDispatchesReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, TabOnFolderLaneDispatchesReorderNotMoveInto) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeFolderNode("target-folder"));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, TabOnFolderBodyDispatchesMoveIntoFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  NoOpPressedCallback());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::DropZoneForTesting::kInto);

  auto callback = folder_row.GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_folder");
}

TEST_F(SidebarDnDDispatchTest, LastItemAfterLaneAppendDispatchesReorderTab) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 200));

  auto callback = list.GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_GE(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events.back().kind, "reorder_root_item");
  EXPECT_NE(observer_.events.back().json.find("\"kind\":\"tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events.back().json.find("\"id\":\"tab-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events.back().json.find("\"kind\":\"append\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, LastItemAfterLaneAppendDispatchesReorderFolder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 200));

  auto callback = list.GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_GE(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events.back().kind, "reorder_root_item");
  EXPECT_NE(observer_.events.back().json.find("\"kind\":\"folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events.back().json.find("\"id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events.back().json.find("\"kind\":\"append\""),
            std::string::npos);
}

}  // namespace
}  // namespace maho
