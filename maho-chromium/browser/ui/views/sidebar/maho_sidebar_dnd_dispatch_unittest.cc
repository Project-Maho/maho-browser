// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_dnd_events.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_section_policy.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/context_menu/maho_tab_context_menu.h"
#include "maho/browser/ui/context_menu/maho_context_menu_ids.h"


#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "base/values.h"
#include "components/favicon_base/favicon_types.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/drop_helper.h"
#include "ui/views/widget/widget.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "components/prefs/pref_service.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/box_layout.h"
#include "content/public/test/browser_task_environment.h"

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

std::string CreateSpaceForCoreTesting(::MahoCore* core,
                                      std::string_view name,
                                      std::string_view profile_id = "default") {
  constexpr char kColorJson[] =
      R"({"hue":16.0,"saturation":0.8,"brightness":1.0,"grain":0.0})";
  const std::string name_string(name);
  const std::string profile_id_string(profile_id);
  char* raw = maho_core_create_space(core, name_string.c_str(), kColorJson,
                                     profile_id_string.c_str());
  if (!raw) {
    return std::string();
  }
  const std::string json(raw);
  maho_string_free(raw);
  std::optional<base::Value> parsed = ParseJson(json);
  if (!parsed || !parsed->is_dict()) {
    return std::string();
  }
  const std::string* id = parsed->GetDict().FindString("id");
  return id ? *id : std::string();
}

std::string ActiveSpaceIdForCore(::MahoCore* core) {
  char* raw = maho_core_get_active_space_id(core);
  if (!raw) {
    return std::string();
  }
  std::string json(raw);
  maho_string_free(raw);
  auto parsed = ParseJson(json);
  return parsed && parsed->is_string() ? parsed->GetString() : std::string();
}

void SetNewTabPosition(::MahoCore* core, std::string_view position) {
  const std::string settings =
      base::StringPrintf(R"({"general":{"newTabPosition":"%s"}})",
                         std::string(position).c_str());
  maho_core_update_settings(core, settings.c_str());
}

void DispatchCreateTabToCore(::MahoCore* core,
                             const std::string& space_id,
                             const std::string& tab_id,
                             bool is_private = false) {
  base::DictValue event;
  event.Set("kind", "create_tab");
  event.Set("space_id", space_id);
  event.Set("tab_id", tab_id);
  event.Set("url", "https://example.com/" + tab_id);
  event.Set("is_private", is_private);
  DispatchTabRegistryShellEventForTesting(core, "create_tab", std::move(event));
}

void DispatchJsonToCore(::MahoCore* core, base::DictValue event) {
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(event, &json));
  char* updates = maho_core_handle_event(core, json.c_str());
  ASSERT_TRUE(updates);
  maho_string_free(updates);
}

base::ListValue SidebarTreeForCore(::MahoCore* core,
                                   const std::string& space_id) {
  const std::string quoted_space_id = base::GetQuotedJSONString(space_id);
  char* raw = maho_core_get_sidebar_state_v2(core, quoted_space_id.c_str());
  if (!raw) {
    return base::ListValue();
  }
  std::string json(raw);
  maho_string_free(raw);
  auto parsed = ParseJson(json);
  if (!parsed || !parsed->is_dict()) {
    return base::ListValue();
  }
  const base::ListValue* tree = parsed->GetDict().FindList("tree");
  return tree ? tree->Clone() : base::ListValue();
}

std::vector<std::string> RootNodeIds(const base::ListValue& tree) {
  std::vector<std::string> ids;
  for (const auto& value : tree) {
    const base::DictValue* node = value.GetIfDict();
    if (!node) {
      continue;
    }
    if (const std::string* id = node->FindString("id")) {
      ids.push_back(*id);
    }
  }
  return ids;
}

views::Button::PressedCallback NoOpPressedCallback() {
  return base::BindRepeating([](const ui::Event&) {});
}

favicon_base::FaviconImageResult MakeFaviconResult(SkColor color) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(8, 8);
  bitmap.eraseColor(color);

  favicon_base::FaviconImageResult result;
  result.image = gfx::Image::CreateFrom1xBitmap(bitmap);
  return result;
}

MahoSidebarFavoritesModel MakeFavoritesModelForTesting(size_t count) {
  MahoSidebarFavoritesModel model;
  for (size_t index = 0; index < count; ++index) {
    MahoSidebarFavoriteItemModel item;
    item.tab_id = "fav-" + std::to_string(index + 1);
    item.url = GURL("https://favorite" + std::to_string(index + 1) +
                    ".example");
    item.title = u"Favorite";
    model.items.push_back(item);
  }
  return model;
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


class MahoNewTabPositionCoreTest : public testing::Test {
 public:
  void SetUp() override {
    core_ = maho_core_new();
    ASSERT_TRUE(core_);
    space_id_ = ActiveSpaceIdForCore(core_);
    ASSERT_FALSE(space_id_.empty());
  }

  void TearDown() override {
    maho_core_free(core_);
    core_ = nullptr;
  }

 protected:
  static constexpr char kTabOne[] =
      "00000000-0000-4000-8000-000000000001";
  static constexpr char kTabTwo[] =
      "00000000-0000-4000-8000-000000000002";
  static constexpr char kTabThree[] =
      "00000000-0000-4000-8000-000000000003";

  ::MahoCore* core_ = nullptr;
  std::string space_id_;
};

TEST_F(MahoNewTabPositionCoreTest, TopBottomAndEmptyInsertion) {
  DispatchCreateTabToCore(core_, space_id_, kTabOne);
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, space_id_)),
            std::vector<std::string>({kTabOne}));

  DispatchCreateTabToCore(core_, space_id_, kTabTwo);
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, space_id_)),
            std::vector<std::string>({kTabTwo, kTabOne}));

  SetNewTabPosition(core_, "bottom");
  DispatchCreateTabToCore(core_, space_id_, kTabThree);
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, space_id_)),
            std::vector<std::string>({kTabTwo, kTabOne, kTabThree}));
}

using MahoSpaceIconCoreTest = MahoNewTabPositionCoreTest;

std::string SpaceIconForCore(::MahoCore* core, const std::string& space_id) {
  char* raw = maho_core_get_space_view_models(core);
  if (!raw) {
    return std::string();
  }
  const std::string json(raw);
  maho_string_free(raw);
  std::optional<base::Value> parsed = ParseJson(json);
  if (!parsed || !parsed->is_list()) {
    return std::string();
  }
  for (const base::Value& space : parsed->GetList()) {
    if (!space.is_dict()) {
      continue;
    }
    const std::string* id = space.GetDict().FindString("id");
    if (id && *id == space_id) {
      const std::string* icon = space.GetDict().FindString("icon");
      return icon ? *icon : std::string();
    }
  }
  return std::string();
}

// The context menu's "Change Icon" used to send `set_space_icon`, a kind the
// core has no handler for, so the pick was silently dropped. The icon seam is
// `update_space_config`, and the core canonicalizes importer-style icon names
// ("bulb") to the emoji every shell renders as text.
TEST_F(MahoSpaceIconCoreTest, UpdateSpaceConfigIconIsAppliedAndCanonicalized) {
  auto dispatch_icon = [&](const std::string& icon) {
    base::DictValue changes;
    changes.Set("spaceId", space_id_);
    changes.Set("icon", icon);
    base::DictValue event;
    event.Set("kind", "update_space_config");
    event.Set("changes", std::move(changes));
    std::string event_json;
    ASSERT_TRUE(base::JSONWriter::Write(event, &event_json));
    DispatchSpaceContextMenuCoreEventForTesting(core_, event_json);
  };

  const std::string kRocket = "\xF0\x9F\x9A\x80";  // U+1F680
  const std::string kBulb = "\xF0\x9F\x92\xA1";    // U+1F4A1
  dispatch_icon(kRocket);
  EXPECT_EQ(SpaceIconForCore(core_, space_id_), kRocket);
  dispatch_icon("bulb");
  EXPECT_EQ(SpaceIconForCore(core_, space_id_), kBulb)
      << "icon names must be canonicalized to emoji, never stored as text";
}

TEST_F(MahoNewTabPositionCoreTest, InvalidValueFallsBackToTop) {
  SetNewTabPosition(core_, "bottom");
  SetNewTabPosition(core_, "diagonal");
  DispatchCreateTabToCore(core_, space_id_, kTabOne);
  DispatchCreateTabToCore(core_, space_id_, kTabTwo);
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, space_id_)),
            std::vector<std::string>({kTabTwo, kTabOne}));
}

TEST_F(MahoNewTabPositionCoreTest, ReannouncementPreservesRestoredOrder) {
  SetNewTabPosition(core_, "bottom");
  DispatchCreateTabToCore(core_, space_id_, kTabOne);
  DispatchCreateTabToCore(core_, space_id_, kTabTwo);
  DispatchCreateTabToCore(core_, space_id_, kTabOne);
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, space_id_)),
            std::vector<std::string>({kTabOne, kTabTwo}));
}

TEST_F(MahoNewTabPositionCoreTest, PrivateAndPinnedBoundariesPreserveChoice) {
  SetNewTabPosition(core_, "bottom");
  DispatchCreateTabToCore(core_, space_id_, kTabOne, true);

  base::DictValue pin;
  pin.Set("kind", "pin_tab");
  pin.Set("tab_id", kTabOne);
  DispatchJsonToCore(core_, std::move(pin));

  DispatchCreateTabToCore(core_, space_id_, kTabTwo, true);
  SetNewTabPosition(core_, "top");
  DispatchCreateTabToCore(core_, space_id_, kTabThree);

  const base::ListValue tree = SidebarTreeForCore(core_, space_id_);
  EXPECT_EQ(RootNodeIds(tree),
            std::vector<std::string>({kTabThree, kTabOne, kTabTwo}));
  ASSERT_EQ(tree.size(), 3u);
  EXPECT_FALSE(tree[0].GetDict().FindBool("isPrivate").value_or(false));
  EXPECT_TRUE(tree[1].GetDict().FindBool("isPinned").value_or(false));
  EXPECT_TRUE(tree[2].GetDict().FindBool("isPrivate").value_or(false));
}

TEST_F(MahoNewTabPositionCoreTest, FolderAndExplicitReorderRemainAuthoritative) {
  SetNewTabPosition(core_, "bottom");
  DispatchCreateTabToCore(core_, space_id_, kTabOne);

  base::DictValue create_folder;
  create_folder.Set("kind", "create_folder_with_tabs");
  create_folder.Set("space_id", space_id_);
  create_folder.Set("name", "Existing group");
  base::ListValue tab_ids;
  tab_ids.Append(kTabOne);
  create_folder.Set("tab_ids", std::move(tab_ids));
  DispatchJsonToCore(core_, std::move(create_folder));

  DispatchCreateTabToCore(core_, space_id_, kTabTwo);
  base::ListValue tree = SidebarTreeForCore(core_, space_id_);
  ASSERT_EQ(tree.size(), 2u);
  const std::string* first_kind = tree[0].GetDict().FindString("kind");
  ASSERT_TRUE(first_kind);
  EXPECT_EQ(*first_kind, "folder");
  const std::string* second_id = tree[1].GetDict().FindString("id");
  ASSERT_TRUE(second_id);
  EXPECT_EQ(*second_id, kTabTwo);

  DispatchCreateTabToCore(core_, space_id_, kTabThree);
  base::DictValue reorder;
  reorder.Set("kind", "reorder_root_item");
  reorder.Set("space_id", space_id_);
  base::DictValue item;
  item.Set("kind", "tab");
  item.Set("id", kTabThree);
  reorder.Set("item", std::move(item));
  base::DictValue target;
  target.Set("kind", "tab");
  target.Set("id", kTabTwo);
  base::DictValue insertion_point;
  insertion_point.Set("kind", "before");
  insertion_point.Set("target", std::move(target));
  reorder.Set("insertion_point", std::move(insertion_point));
  DispatchJsonToCore(core_, std::move(reorder));

  tree = SidebarTreeForCore(core_, space_id_);
  ASSERT_EQ(tree.size(), 3u);
  const std::string* second_reordered_id =
      tree[1].GetDict().FindString("id");
  ASSERT_TRUE(second_reordered_id);
  EXPECT_EQ(*second_reordered_id, kTabThree);
  const std::string* third_reordered_id =
      tree[2].GetDict().FindString("id");
  ASSERT_TRUE(third_reordered_id);
  EXPECT_EQ(*third_reordered_id, kTabTwo);
}

class MahoNativeTabStripNewTabPositionTest
    : public BrowserWithTestWindowTest {
 public:
  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    core_ = maho_core_new();
    ASSERT_TRUE(core_);
    saved_core_ = maho::GetCore();
    maho::SetCore(core_);
    space_id_ = ActiveSpaceIdForCore(core_);
    ASSERT_FALSE(space_id_.empty());
    MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
    bridge->RegisterSpace(space_id_, profile()->GetPath().BaseName());
    bridge->SetActiveSpaceId(browser(), space_id_);
    MahoTabRegistry::Get()->OnBrowserCreated(browser());
  }

  void TearDown() override {
    MahoTabRegistry::Get()->OnBrowserClosed(browser());
    MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
    bridge->ClearBrowserActiveSpace(browser());
    bridge->UnregisterSpace(space_id_);
    maho::SetCore(saved_core_);
    maho_core_free(core_);
    core_ = nullptr;
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  ::MahoCore* core_ = nullptr;
  ::MahoCore* saved_core_ = nullptr;
  std::string space_id_;
};

TEST_F(MahoNativeTabStripNewTabPositionTest,
       NativeTabStripInsertDispatchesBottomChoiceToCore) {
  SetNewTabPosition(core_, "bottom");
  AddTab(browser(), GURL("https://first.example/"));
  AddTab(browser(), GURL("https://second.example/"));

  const base::ListValue tree = SidebarTreeForCore(core_, space_id_);
  ASSERT_EQ(tree.size(), 2u);
  const std::string* first_url = tree[0].GetDict().FindString("url");
  ASSERT_TRUE(first_url);
  EXPECT_EQ(*first_url, "https://first.example/");
  const std::string* second_url = tree[1].GetDict().FindString("url");
  ASSERT_TRUE(second_url);
  EXPECT_EQ(*second_url, "https://second.example/");
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       ReannouncesTabInsertedBeforeObserverRegistration) {
  MahoTabRegistry* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);
  base::ScopedClosureRunner clear_observer(base::BindOnce([] {
    SetTabRegistryShellEventObserverForTesting(nullptr);
  }));
  registry->OnBrowserClosed(browser());

  AddTab(browser(), GURL("https://reannounce.example/"));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  MahoTabIdHelper::CreateForWebContents(contents);
  MahoTabIdHelper* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->has_been_announced());

  MahoSpaceProfileBridge::GetInstance()->SetActiveSpaceId(browser(),
                                                          space_id_);

  EXPECT_TRUE(helper->has_been_announced());
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, space_id_)),
            std::vector<std::string>({helper->stable_tab_id()}));

  size_t create_tab_events = 0;
  for (const auto& event : observer.events) {
    if (event.kind == "create_tab") {
      ++create_tab_events;
    }
  }
  EXPECT_EQ(create_tab_events, 1u);

  registry->ReannounceUnannouncedTabs();
  registry->ReannounceUnannouncedTabs();
  EXPECT_EQ(observer.events.size(), create_tab_events);
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       HydrationReconcilesMatchingBrowserAndReannouncesOnlyOnce) {
  const std::string hydrated_space_id =
      CreateSpaceForCoreTesting(core_, "Hydrated Default");
  ASSERT_FALSE(hydrated_space_id.empty());
  const std::string spaces_json =
      R"([{"id":)" + base::GetQuotedJSONString(hydrated_space_id) +
      R"(,"profileId":"default"}])";
  const std::string active_space_id_json =
      base::GetQuotedJSONString(hydrated_space_id);
  TestingProfile* matching_profile = CreateProfile("Default");
  ASSERT_TRUE(matching_profile);
  BrowserWindowCreateParams params(matching_profile, true);
  params.window = new TestBrowserWindow();
  std::unique_ptr<Browser> matching_browser =
      DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
  ASSERT_TRUE(matching_browser);

  MahoTabRegistry* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  registry->OnBrowserClosed(matching_browser.get());
  AddTab(matching_browser.get(), GURL("https://hydration.example/"));
  content::WebContents* contents =
      matching_browser->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  MahoTabIdHelper::CreateForWebContents(contents);
  MahoTabIdHelper* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->has_been_announced());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);
  base::ScopedClosureRunner clear_observer(base::BindOnce([] {
    SetTabRegistryShellEventObserverForTesting(nullptr);
  }));
  auto count_matching_create_events = [&] {
    size_t count = 0;
    for (const auto& event : observer.events) {
      if (event.kind == "create_tab" &&
          event.json.find(helper->stable_tab_id()) != std::string::npos) {
        ++count;
      }
    }
    return count;
  };

  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  ASSERT_TRUE(bridge->HydrateFromSerializedStateForTesting(
      spaces_json, active_space_id_json));
  EXPECT_EQ(bridge->GetActiveSpaceId(matching_browser.get()),
            hydrated_space_id);
  EXPECT_TRUE(helper->has_been_announced());
  EXPECT_EQ(count_matching_create_events(), 1u);
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, hydrated_space_id)),
            std::vector<std::string>({helper->stable_tab_id()}));

  ASSERT_TRUE(bridge->HydrateFromSerializedStateForTesting(
      spaces_json, active_space_id_json));
  EXPECT_EQ(count_matching_create_events(), 1u);

  registry->OnBrowserClosed(matching_browser.get());
  bridge->ClearBrowserActiveSpace(matching_browser.get());
  bridge->UnregisterSpace(hydrated_space_id);
  matching_browser.reset();
  bridge->SetActiveSpaceId(browser(), space_id_);
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       ForeignProfileSpaceDoesNotReannounceUnmappedTab) {
  const std::string foreign_space =
      CreateSpaceForCoreTesting(core_, "Foreign Profile");
  ASSERT_FALSE(foreign_space.empty());

  TestingProfile foreign_profile(
      base::FilePath(FILE_PATH_LITERAL("foreign_profile_dir")));
  BrowserWindowCreateParams params(&foreign_profile, true);
  params.window = new TestBrowserWindow();
  std::unique_ptr<Browser> foreign_browser =
      DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
  ASSERT_TRUE(foreign_browser);

  MahoTabRegistry* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  registry->OnBrowserClosed(foreign_browser.get());
  AddTab(foreign_browser.get(), GURL("https://foreign.example/"));
  content::WebContents* contents =
      foreign_browser->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  MahoTabIdHelper::CreateForWebContents(contents);
  MahoTabIdHelper* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->has_been_announced());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);
  base::ScopedClosureRunner clear_observer(base::BindOnce([] {
    SetTabRegistryShellEventObserverForTesting(nullptr);
  }));
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->RegisterSpace(foreign_space, profile()->GetPath().BaseName());
  bridge->SetActiveSpaceId(foreign_browser.get(), foreign_space);

  EXPECT_TRUE(bridge->GetActiveSpaceId(foreign_browser.get()).empty());
  EXPECT_FALSE(helper->has_been_announced());
  EXPECT_TRUE(observer.events.empty());

  registry->OnBrowserClosed(foreign_browser.get());
  bridge->ClearBrowserActiveSpace(foreign_browser.get());
  bridge->UnregisterSpace(foreign_space);
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       CoreRecoveryReannouncesUnannouncedTabs) {
  MahoTabRegistry* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  registry->OnBrowserClosed(browser());
  AddTab(browser(), GURL("https://core-unavailable.example/"));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  MahoTabIdHelper::CreateForWebContents(contents);
  MahoTabIdHelper* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->has_been_announced());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);
  base::ScopedClosureRunner clear_observer(base::BindOnce([] {
    SetTabRegistryShellEventObserverForTesting(nullptr);
  }));
  maho::SetCore(nullptr);
  registry->ReannounceUnannouncedTabs();

  EXPECT_FALSE(helper->has_been_announced());
  EXPECT_TRUE(observer.events.empty());

  maho::SetCore(core_);

  EXPECT_TRUE(helper->has_been_announced());
  ASSERT_EQ(observer.events.size(), 1u);
  EXPECT_EQ(observer.events[0].kind, "create_tab");
  EXPECT_EQ(RootNodeIds(SidebarTreeForCore(core_, space_id_)),
            std::vector<std::string>({helper->stable_tab_id()}));
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       BrowserCloseDropsDispatchCacheEntriesForItsTabs) {
  AddTab(browser(), GURL("https://close-cache.example/"));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->stable_tab_id().empty());

  MahoTabRegistry* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  registry->GetOrCreateDispatchCache(helper->stable_tab_id()).url =
      "https://cached.example/";

  registry->OnBrowserClosed(browser());

  EXPECT_TRUE(
      registry->GetOrCreateDispatchCache(helper->stable_tab_id()).url.empty());
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       UnregisterSpaceClearsBrowserMappingBeforeReannouncement) {
  MahoTabRegistry* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  registry->OnBrowserClosed(browser());
  AddTab(browser(), GURL("https://unregistered-space.example/"));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  MahoTabIdHelper::CreateForWebContents(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->has_been_announced());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);
  base::ScopedClosureRunner clear_observer(base::BindOnce([] {
    SetTabRegistryShellEventObserverForTesting(nullptr);
  }));
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->RegisterSpace(space_id_, profile()->GetPath().BaseName());

  bridge->UnregisterSpace(space_id_);
  registry->ReannounceUnannouncedTabs();

  EXPECT_TRUE(bridge->GetActiveSpaceId(browser()).empty());
  EXPECT_FALSE(helper->has_been_announced());
  EXPECT_TRUE(observer.events.empty());
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       UnknownSpaceCannotBeAssignedOrReannounced) {
  MahoTabRegistry* registry = MahoTabRegistry::Get();
  ASSERT_TRUE(registry);
  registry->OnBrowserClosed(browser());
  AddTab(browser(), GURL("https://unknown-space.example/"));
  content::WebContents* contents =
      browser()->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  MahoTabIdHelper::CreateForWebContents(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->has_been_announced());

  RecordingShellEventObserver observer;
  SetTabRegistryShellEventObserverForTesting(&observer);
  base::ScopedClosureRunner clear_observer(base::BindOnce([] {
    SetTabRegistryShellEventObserverForTesting(nullptr);
  }));
  MahoSpaceProfileBridge* bridge = MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);
  bridge->SetActiveSpaceId(browser(), "unknown-space");
  registry->ReannounceUnannouncedTabs();

  EXPECT_TRUE(bridge->GetActiveSpaceId(browser()).empty());
  EXPECT_FALSE(helper->has_been_announced());
  EXPECT_TRUE(observer.events.empty());
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       PrimaryOtrSidebarRefreshDoesNotTouchCoreCache) {
  Profile* otr_profile = profile()->GetPrimaryOTRProfile(true);
  ASSERT_TRUE(otr_profile);
  TestBrowserWindow* window = new TestBrowserWindow();
  BrowserWindowCreateParams params(otr_profile, true);
  params.window = window;
  auto otr_browser_owner =
      DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
  Browser* otr_browser = otr_browser_owner.get();
  ASSERT_TRUE(otr_browser);

  ResetSidebarCoreCacheForTesting();
  MahoSidebarView sidebar(otr_browser);
  sidebar.RefreshAllSynchronously();

  const auto counters = GetSidebarCoreCacheCountersForTesting();
  EXPECT_EQ(counters.footer_hits + counters.footer_misses, 0u);
  EXPECT_EQ(counters.tree_bundle_hits + counters.tree_bundle_misses, 0u);
  EXPECT_EQ(counters.favorites_hits + counters.favorites_misses, 0u);
  ResetSidebarCoreCacheForTesting();
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       BuildTabListModelCrossSpaceIsolationAndAdapterFallback) {
  AddTab(browser(), GURL("https://space1-tab.example/"));
  ASSERT_EQ(browser()->GetTabStripModel()->count(), 1);

  content::WebContents* wc = browser()->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(wc);
  auto* helper = MahoTabIdHelper::FromWebContents(wc);
  ASSERT_TRUE(helper);
  const std::string space_1_tab_id = helper->stable_tab_id();
  ASSERT_FALSE(space_1_tab_id.empty());

  const std::string space_2 = CreateSpaceForCoreTesting(core_, "Space Two");
  ASSERT_FALSE(space_2.empty());

  const std::string space_2_tab_id = "55555555-5555-4000-8000-000000000001";
  DispatchCreateTabToCore(core_, space_2, space_2_tab_id);

  // 1. In Space 1: BuildTabListModel must ONLY render space 1 tabs, never space 2 tabs.
  MahoSidebarStateAdapter adapter;
  MahoSidebarTabListModel model_space_1 = adapter.BuildTabListModel(browser());
  EXPECT_EQ(model_space_1.active_space_id, space_id_);

  bool found_tab1 = false;
  bool found_tab2 = false;
  for (const auto& node : model_space_1.normal_tree) {
    if (node.tab_id == space_1_tab_id) found_tab1 = true;
    if (node.tab_id == space_2_tab_id) found_tab2 = true;
  }
  EXPECT_TRUE(found_tab1);
  EXPECT_FALSE(found_tab2);

  // 2. Switch browser to Space 2: BuildTabListModel must ONLY render space 2 tabs.
  MahoSpaceProfileBridge::GetInstance()->SetActiveSpaceId(browser(), space_2);
  MahoSidebarTabListModel model_space_2 = adapter.BuildTabListModel(browser());
  EXPECT_EQ(model_space_2.active_space_id, space_2);

  found_tab1 = false;
  found_tab2 = false;
  for (const auto& node : model_space_2.normal_tree) {
    if (node.tab_id == space_1_tab_id) found_tab1 = true;
    if (node.tab_id == space_2_tab_id) found_tab2 = true;
  }
  EXPECT_FALSE(found_tab1);
  EXPECT_TRUE(found_tab2);

  // 3. Adapter Fallback: When safe mode / no core, BuildTabListModel falls back to tab strip.
  maho::SetCore(nullptr);
  MahoSidebarTabListModel fallback_model = adapter.BuildTabListModel(browser());
  EXPECT_FALSE(fallback_model.normal_tree.empty());
  EXPECT_EQ(fallback_model.normal_tree[0].tab_id, space_1_tab_id);
  maho::SetCore(core_);
}

TEST_F(MahoNativeTabStripNewTabPositionTest,
       BrowserProfileSpaceMatchingAndForeignProfileIsolation) {
  auto* bridge = MahoSpaceProfileBridge::GetInstance();
  const std::string original_active_space = bridge->GetActiveSpaceId();
  bridge->ClearBrowserActiveSpacesForTesting();

  const std::string registered_space = "space-registered";
  const std::string same_profile_secondary = "space-secondary";
  const base::FilePath matching_basename = profile()->GetPath().BaseName();
  bridge->RegisterSpace(registered_space, matching_basename);
  bridge->RegisterSpace(same_profile_secondary, matching_basename);
  bridge->SetActiveSpaceId(registered_space);
  bridge->ClearBrowserActiveSpacesForTesting();

  bridge->ReconcileExistingBrowsersActiveSpace();
  EXPECT_EQ(bridge->GetActiveSpaceId(browser()), registered_space);

  TestingProfile foreign_profile(base::FilePath(FILE_PATH_LITERAL("foreign_profile_dir")));
  BrowserWindowCreateParams foreign_params(&foreign_profile, true);
  foreign_params.window = new TestBrowserWindow();
  std::unique_ptr<Browser> foreign_browser =
      DeprecatedCreateOwnedBrowserWindowForTesting(std::move(foreign_params));

  EXPECT_TRUE(bridge->GetActiveSpaceId(foreign_browser.get()).empty());

  bridge->SetActiveSpaceId(foreign_browser.get(), registered_space);
  bridge->ReconcileExistingBrowsersActiveSpace();
  EXPECT_TRUE(bridge->GetActiveSpaceId(foreign_browser.get()).empty());

  foreign_browser.reset();
  bridge->UnregisterSpace(same_profile_secondary);
  bridge->UnregisterSpace(registered_space);
  bridge->SetActiveSpaceId(original_active_space);
  bridge->SetActiveSpaceId(browser(), original_active_space);
}

class MahoSidebarCoreCacheTest : public testing::Test {
 public:
  void SetUp() override {
    ResetSidebarCoreCacheForTesting();
    core_ = Core(1);
    InstallFetchers();
  }

  void TearDown() override { ResetSidebarCoreCacheForTesting(); }

 protected:
  static ::MahoCore* Core(uintptr_t identity) {
    return reinterpret_cast<::MahoCore*>(identity);
  }

  void InstallFetchers() {
    SidebarCoreCacheFetchersForTesting fetchers;
    fetchers.footer = base::BindRepeating(
        [](MahoSidebarCoreCacheTest* self, ::MahoCore*) {
          ++self->footer_fetches_;
          if (self->footer_fetch_started_) {
            self->footer_fetch_started_->Signal();
            self->footer_fetch_continue_->Wait();
          }
          FooterSpaceState state;
          state.has_core_state = true;
          state.space_count = 1;
          return state;
        },
        base::Unretained(this));
    fetchers.tree_bundle = base::BindRepeating(
        [](MahoSidebarCoreCacheTest* self, ::MahoCore*, const std::string&) {
          ++self->tree_fetches_;
          SidebarStateBackgroundResult result;
          result.parsed_tree = base::Value(base::ListValue());
          return result;
        },
        base::Unretained(this));
    fetchers.favorites = base::BindRepeating(
        [](MahoSidebarCoreCacheTest* self, ::MahoCore*, const std::string&) {
          ++self->favorites_fetches_;
          return MakeFavoritesModelForTesting(1);
        },
        base::Unretained(this));
    SetSidebarCoreCacheFetchersForTesting(std::move(fetchers));
  }

  SidebarStateBackgroundResult Build(const std::string& space_id,
                                     bool bypass = false) {
    return BuildSidebarStateOnBackgroundForTesting(
        core_, base::GetQuotedJSONString(space_id), bypass);
  }

  ::MahoCore* core_ = nullptr;
  int footer_fetches_ = 0;
  int tree_fetches_ = 0;
  int favorites_fetches_ = 0;
  base::WaitableEvent* footer_fetch_started_ = nullptr;
  base::WaitableEvent* footer_fetch_continue_ = nullptr;
};

TEST_F(MahoSidebarCoreCacheTest, InvalidationMaskTableCoversCoreVariants) {
  struct Case {
    const char* json;
    SidebarCoreFragment fragments;
    size_t space_count;
  };
  const Case cases[] = {
      {R"({"kind":"full_state"})", SidebarCoreFragment::kAll, 0},
      {R"({"kind":"tab_created","tab":{"space_id":"s"}})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 1},
      {R"({"kind":"tab_updated"})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 0},
      {R"({"kind":"tab_closed"})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 0},
      {R"({"kind":"tab_order_changed","space_id":"s"})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 1},
      {R"({"kind":"navigation_state_changed"})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 0},
      {R"({"kind":"tab_favicon_changed"})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 0},
      {R"({"kind":"tab_lifecycle_changed"})", SidebarCoreFragment::kTreeBundle,
       0},
      {R"({"kind":"tab_role_changed"})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 0},
      {R"({"kind":"tabs_migrated","from_space_id":"a","to_space_id":"b"})",
       SidebarCoreFragment::kTreeBundle | SidebarCoreFragment::kFavorites, 2},
      {R"({"kind":"folder_created"})", SidebarCoreFragment::kTreeBundle, 0},
      {R"({"kind":"folder_updated"})", SidebarCoreFragment::kTreeBundle, 0},
      {R"({"kind":"folder_deleted"})", SidebarCoreFragment::kTreeBundle, 0},
      {R"({"kind":"folder_order_changed","space_id":"s"})",
       SidebarCoreFragment::kTreeBundle, 1},
      {R"({"kind":"space_created"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"space_updated"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"space_deleted","space_id":"s"})", SidebarCoreFragment::kAll,
       1},
      {R"({"kind":"space_order_changed"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"active_space_changed"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"profile_created"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"profile_updated"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"profile_deleted"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"active_profile_changed"})", SidebarCoreFragment::kFooter,
       0},
      {R"({"kind":"space_config_updated","space_id":"s"})",
       SidebarCoreFragment::kAll, 1},
      {R"({"kind":"settings_changed"})", SidebarCoreFragment::kFooter, 0},
      {R"({"kind":"tab_audio_state_changed"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"tab_preview_updated"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"download_started"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"download_progress"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"download_completed"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"download_renamed"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"sync_state_changed"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"split_view_changed"})", SidebarCoreFragment::kNone, 0},
      {R"({"kind":"favorite_limit_reached"})", SidebarCoreFragment::kNone,
       0},
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.json);
    auto invalidation =
        GetSidebarCacheInvalidationForUpdateJsonForTesting(test_case.json);
    EXPECT_EQ(invalidation.fragments, test_case.fragments);
    EXPECT_EQ(invalidation.space_ids.size(), test_case.space_count);
  }
}

TEST_F(MahoSidebarCoreCacheTest, ConsecutiveBuildsFetchEachFragmentOnce) {
  Build("a");
  Build("a");
  EXPECT_EQ(footer_fetches_, 1);
  EXPECT_EQ(tree_fetches_, 1);
  EXPECT_EQ(favorites_fetches_, 1);
  const auto counters = GetSidebarCoreCacheCountersForTesting();
  EXPECT_EQ(counters.footer_misses, 1u);
  EXPECT_EQ(counters.footer_hits, 1u);
  EXPECT_EQ(counters.tree_bundle_misses, 1u);
  EXPECT_EQ(counters.tree_bundle_hits, 1u);
  EXPECT_EQ(counters.favorites_misses, 1u);
  EXPECT_EQ(counters.favorites_hits, 1u);
}

// A tree fetch that yields NO tree means the fetch could not produce one (core
// handle swapped, import gate raised, unparsable space id) — not that the space
// is empty. Publishing that as a valid cache entry would serve the empty tree
// back from cache on every later build, turning a momentary failure into a
// persistently empty sidebar. The entry must stay invalid so the next build
// re-fetches.
TEST_F(MahoSidebarCoreCacheTest, FailedTreeFetchIsNotCachedAsValid) {
  // A local counter: the fetchers run outside this TEST_F's own class, so
  // they cannot reach the fixture's protected counters through a base pointer.
  int tree_fetches = 0;
  SidebarCoreCacheFetchersForTesting fetchers;
  fetchers.footer = base::BindRepeating([](::MahoCore*) {
    FooterSpaceState state;
    state.has_core_state = true;
    state.space_count = 1;
    return state;
  });
  // Fails (no parsed_tree) on the first call, succeeds afterwards.
  fetchers.tree_bundle = base::BindRepeating(
      [](int* fetches, ::MahoCore*, const std::string&) {
        ++*fetches;
        SidebarStateBackgroundResult result;
        if (*fetches > 1) {
          base::ListValue tree;
          tree.Append(base::Value(base::DictValue()));
          result.parsed_tree = base::Value(std::move(tree));
        }
        return result;
      },
      base::Unretained(&tree_fetches));
  fetchers.favorites =
      base::BindRepeating([](::MahoCore*, const std::string&) {
        return MakeFavoritesModelForTesting(1);
      });
  SetSidebarCoreCacheFetchersForTesting(std::move(fetchers));

  SidebarStateBackgroundResult first = Build("a");
  EXPECT_EQ(tree_fetches, 1);
  EXPECT_FALSE(first.parsed_tree.has_value())
      << "the failing fetch must surface as an absent tree, not an empty one";

  SidebarStateBackgroundResult second = Build("a");
  EXPECT_EQ(tree_fetches, 2)
      << "a failed tree fetch must NOT be published as a valid cache entry; "
         "the next build has to re-fetch instead of serving an empty tree";
  ASSERT_TRUE(second.parsed_tree.has_value());
  EXPECT_EQ(second.parsed_tree->GetList().size(), 1u);

  SidebarStateBackgroundResult third = Build("a");
  EXPECT_EQ(tree_fetches, 2)
      << "a successful tree must still be cached exactly once";
  ASSERT_TRUE(third.parsed_tree.has_value());
  EXPECT_EQ(third.parsed_tree->GetList().size(), 1u);
}

TEST_F(MahoSidebarCoreCacheTest, FooterInvalidationIsFragmentIsolated) {
  Build("a");
  InvalidateSidebarCoreCacheForUpdatesJson(R"([{"kind":"space_updated"}])");
  Build("a");
  EXPECT_EQ(footer_fetches_, 2);
  EXPECT_EQ(tree_fetches_, 1);
  EXPECT_EQ(favorites_fetches_, 1);
}

TEST_F(MahoSidebarCoreCacheTest,
       FooterSpaceReorderInvalidatesCachedFooterFragment) {
  ::MahoCore* real_core = maho_core_new();
  ASSERT_TRUE(real_core);
  core_ = real_core;
  InstallFetchers();
  Build("a");
  EXPECT_EQ(footer_fetches_, 1);

  MahoSidebarFooterView::ReorderSpaceForTesting(real_core, "space-a", 0,
                                                 1);

  Build("a");
  EXPECT_EQ(footer_fetches_, 2);
  maho_core_free(real_core);
  core_ = Core(1);
}

TEST_F(MahoSidebarCoreCacheTest, FolderUpdateInvalidatesOnlyTree) {
  Build("a");
  InvalidateSidebarCoreCacheForUpdatesJson(R"([{"kind":"folder_updated"}])");
  Build("a");
  EXPECT_EQ(footer_fetches_, 1);
  EXPECT_EQ(tree_fetches_, 2);
  EXPECT_EQ(favorites_fetches_, 1);
}

TEST_F(MahoSidebarCoreCacheTest, SpaceDeleteEvictsOnlyDeletedSpaceAndFooter) {
  Build("a");
  Build("b");
  InvalidateSidebarCoreCacheForUpdatesJson(
      R"([{"kind":"space_deleted","space_id":"a"}])");
  Build("b");
  Build("a");
  EXPECT_EQ(footer_fetches_, 2);
  EXPECT_EQ(tree_fetches_, 3);
  EXPECT_EQ(favorites_fetches_, 3);
}

TEST_F(MahoSidebarCoreCacheTest, InFlightInvalidationDiscardsStalePublish) {
  base::WaitableEvent fetch_started;
  base::WaitableEvent fetch_continue;
  footer_fetch_started_ = &fetch_started;
  footer_fetch_continue_ = &fetch_continue;
  std::thread thread([this] { Build("a"); });
  fetch_started.Wait();
  InvalidateSidebarCoreCacheForUpdatesJson(R"([{"kind":"space_updated"}])");
  fetch_continue.Signal();
  thread.join();
  footer_fetch_started_ = nullptr;
  footer_fetch_continue_ = nullptr;
  Build("a");
  EXPECT_EQ(footer_fetches_, 2);
  EXPECT_EQ(
      GetSidebarCoreCacheCountersForTesting().stale_publications_discarded, 1u);
}

TEST_F(MahoSidebarCoreCacheTest, PerSpaceEntriesRemainIndependent) {
  Build("a");
  Build("b");
  Build("a");
  Build("b");
  EXPECT_EQ(footer_fetches_, 1);
  EXPECT_EQ(tree_fetches_, 2);
  EXPECT_EQ(favorites_fetches_, 2);
}

TEST_F(MahoSidebarCoreCacheTest, CorePointerReplacementClearsAllEntries) {
  Build("a");
  core_ = Core(2);
  Build("a");
  EXPECT_EQ(footer_fetches_, 2);
  EXPECT_EQ(tree_fetches_, 2);
  EXPECT_EQ(favorites_fetches_, 2);
}

TEST_F(MahoSidebarCoreCacheTest, ExplicitSafeModeBypassNeverPopulatesCache) {
  Build("a", true);
  Build("a", true);
  EXPECT_EQ(footer_fetches_, 0);
  EXPECT_EQ(tree_fetches_, 0);
  EXPECT_EQ(favorites_fetches_, 0);
  const auto counters = GetSidebarCoreCacheCountersForTesting();
  EXPECT_EQ(counters.footer_hits + counters.footer_misses, 0u);
  EXPECT_EQ(counters.tree_bundle_hits + counters.tree_bundle_misses, 0u);
  EXPECT_EQ(counters.favorites_hits + counters.favorites_misses, 0u);
}

TEST_F(MahoSidebarCoreCacheTest,
       RawContextMenuCreateFolderInvalidatesCachedTree) {
  ::MahoCore* real_core = maho_core_new();
  ASSERT_TRUE(real_core);
  core_ = real_core;
  InstallFetchers();
  char* active_space_json = maho_core_get_active_space_id(real_core);
  ASSERT_TRUE(active_space_json);
  auto active_space =
      base::JSONReader::Read(active_space_json, base::JSON_PARSE_RFC);
  maho_string_free(active_space_json);
  ASSERT_TRUE(active_space && active_space->is_string());
  const std::string space_id = active_space->GetString();
  Build(space_id);
  EXPECT_EQ(tree_fetches_, 1);

  base::DictValue create_folder;
  create_folder.Set("kind", "create_folder");
  create_folder.Set("space_id", space_id);
  create_folder.Set("name", "Context menu folder");
  std::string event_json;
  ASSERT_TRUE(base::JSONWriter::Write(create_folder, &event_json));
  EXPECT_FALSE(
      DispatchSpaceContextMenuCoreEventForTesting(real_core, event_json)
          .empty());

  Build(space_id);
  EXPECT_EQ(tree_fetches_, 2);
  maho_core_free(real_core);
  core_ = Core(1);
}

TEST_F(MahoSidebarCoreCacheTest,
       TypedContextMenuRecolorInvalidatesCachedFooter) {
  ::MahoCore* real_core = maho_core_new();
  ASSERT_TRUE(real_core);
  core_ = real_core;
  InstallFetchers();
  char* active_space_json = maho_core_get_active_space_id(real_core);
  ASSERT_TRUE(active_space_json);
  auto active_space =
      base::JSONReader::Read(active_space_json, base::JSON_PARSE_RFC);
  maho_string_free(active_space_json);
  ASSERT_TRUE(active_space && active_space->is_string());
  const std::string space_id = active_space->GetString();
  Build(space_id);
  EXPECT_EQ(footer_fetches_, 1);

  RecolorSpaceContextMenuForTesting(real_core, space_id, "#336699");

  Build(space_id);
  EXPECT_EQ(footer_fetches_, 2);
  maho_core_free(real_core);
  core_ = Core(1);
}

TEST_F(MahoSidebarCoreCacheTest, CreateTabCoreDispatchInvalidatesCachedTree) {
  ::MahoCore* real_core = maho_core_new();
  ASSERT_TRUE(real_core);
  core_ = real_core;
  InstallFetchers();
  char* active_space_json = maho_core_get_active_space_id(real_core);
  ASSERT_TRUE(active_space_json);
  auto active_space =
      base::JSONReader::Read(active_space_json, base::JSON_PARSE_RFC);
  maho_string_free(active_space_json);
  ASSERT_TRUE(active_space && active_space->is_string());
  const std::string space_id = active_space->GetString();
  Build(space_id);
  EXPECT_EQ(tree_fetches_, 1);

  base::DictValue create_tab;
  create_tab.Set("kind", "create_tab");
  create_tab.Set("space_id", space_id);
  create_tab.Set("tab_id", "tab-1");
  create_tab.Set("url", "https://example.com/");
  DispatchTabRegistryShellEventForTesting(real_core, "create_tab",
                                          std::move(create_tab));

  Build(space_id);
  EXPECT_EQ(tree_fetches_, 2);
  maho_core_free(real_core);
  core_ = Core(1);
}

TEST_F(MahoSidebarCoreCacheTest,
       SynchronousFavoritesRefreshPublishesFreshCacheEntry) {
  Build("a");
  EXPECT_EQ(favorites_fetches_, 1);
  auto refreshed = RefreshSidebarFavoritesForTesting(
      core_, base::GetQuotedJSONString("a"));
  EXPECT_EQ(refreshed.items.size(), 1u);
  EXPECT_EQ(favorites_fetches_, 2);
  Build("a");
  EXPECT_EQ(favorites_fetches_, 2);
}

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
       InsertionLanePreferredHeightRemainsTwoDpDuringHover) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);
  EXPECT_EQ(lane->GetPreferredSize().height(), 2);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(lane->GetPreferredSize().height(), 2);

  lane->OnDragExited();
  EXPECT_EQ(lane->GetPreferredSize().height(), 2);
}

TEST_F(SidebarDnDDispatchTest,
       FolderChildTabDropOnRootTabDispatchesMoveTabToRootBeforeTarget) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
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
  // The folder child escapes to root first, then a root reorder places it
  // before the target (60a07f00 split the old combined before_tab_id form so
  // later pin/unpin transitions cannot clobber the position).
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"space_id\":\"space-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"source-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[0].json.find("\"before_tab_id\":"),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"target-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       RootTabDropOnFolderChildRowDispatchesReorderRootItem) {
  SidebarTreeNode target_node =
      MakeFolderChildTabNode("target-tab", 1, "folder-2", 0);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(
    SidebarDnDDispatchTest,
    RootTabDropBeforeFolderChildRowBodyDispatchesReorderRootItemNotMoveTabToRoot) {
  SidebarTreeNode target_node =
      MakeFolderChildTabNode("target-tab", 1, "folder-2", 0);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"target-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[0].json.find("move_tab_to_root"),
            std::string::npos)
      << "row-body before drop for a root tab must not dispatch move_tab_to_root";
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
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderChildTabPayload("same-tab", "folder-1", 1), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));
  auto callback = row.GetDropCallbackForTesting(event);

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kNone);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kNone);
  EXPECT_TRUE(observer_.events.empty());
}

TEST_F(SidebarDnDDispatchTest,
       CrossFolderChildTabDropDoesNotDispatchIncorrectRootReorder) {
  SidebarTreeNode target_node =
      MakeFolderChildTabNode("target-tab", 1, "folder-2", 0);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
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
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos)
      << "Cross-folder child-tab drops must preserve the dragged tab's source "
         "folder id rather than the target row's folder context";
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
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"source-tab\""),
            std::string::npos);

  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("tab_id") != nullptr);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);
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
  EXPECT_EQ(observer_.events[0].json.find("\"before_tab_id\":"),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "pin_tab");
  for (const auto& event_record : observer_.events) {
    EXPECT_NE(event_record.kind, "reorder_root_item");
  }
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
  EXPECT_NE(observer_.events[0].json.find("\"new_index\":0"),
            std::string::npos);

  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("tab_id") != nullptr);
  ASSERT_TRUE(dict.Find("new_index") != nullptr);
}

TEST_F(SidebarDnDDispatchTest, PrivateSidebarDragDoesNotRevealFavorites) {
  MahoSidebarView sidebar(/*browser=*/nullptr);
  sidebar.SetPrivateFlagsForTesting(/*is_private=*/true, /*is_otr=*/true);
  sidebar.SetBoundsRect(gfx::Rect(0, 0, 240, 300));

  MahoSidebarFavoritesGridView* favorites = sidebar.favorites_view_for_testing();
  ASSERT_TRUE(favorites);
  favorites->SetVisible(false);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(10, 10));

  EXPECT_EQ(sidebar.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_FALSE(favorites->GetVisible());
  EXPECT_FALSE(favorites->is_drag_reveal_active_for_testing());
}

TEST_F(SidebarDnDDispatchTest,
       SidebarRootClaimsTabDragOnlyToRevealHiddenFavorites) {
  MahoSidebarView sidebar(/*browser=*/nullptr);
  sidebar.SetPrivateFlagsForTesting(false, false);
  MahoSidebarFavoritesGridView* favorites = sidebar.favorites_view_for_testing();
  ASSERT_TRUE(favorites);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);

  favorites->SetVisible(true);
  EXPECT_FALSE(sidebar.CanDrop(data))
      << "A visible Favorites child must own its hit area; the sidebar root "
         "must not swallow tab drags elsewhere";

  favorites->SetVisible(false);
  EXPECT_TRUE(sidebar.CanDrop(data))
      << "The root may claim one drag update only while it must reveal the "
         "hidden Favorites target";
}

TEST_F(SidebarDnDDispatchTest,
       TabListNegotiatesMoveForInternalDragAndCopyForUrl) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData internal_data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &internal_data);
  EXPECT_EQ(list.OnDragUpdated(MakeDropEvent(internal_data)),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  list.OnDragExited();

  ui::OSExchangeData url_data;
  url_data.SetURL(GURL("https://example.com"), u"Example");
  EXPECT_EQ(list.OnDragUpdated(MakeDropEvent(url_data)),
            static_cast<int>(ui::mojom::DragOperation::kCopy));
  list.OnDragExited();
}

TEST_F(SidebarDnDDispatchTest,
       SidebarOutsideFavoritesDropCallbackReturnsNone) {
  MahoSidebarView sidebar(/*browser=*/nullptr);
  sidebar.SetPrivateFlagsForTesting(false, false);
  sidebar.SetBoundsRect(gfx::Rect(0, 0, 240, 300));

  MahoSidebarFavoritesGridView* favorites = sidebar.favorites_view_for_testing();
  ASSERT_TRUE(favorites);
  favorites->SetVisible(true);
  favorites->SetBoundsRect(gfx::Rect(0, 0, 220, 80));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent inside_event = MakeDropEvent(data, gfx::Point(5, 5));
  EXPECT_EQ(favorites->OnDragUpdated(inside_event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(favorites->insertion_preview_visible_for_testing());

  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(500, 500));
  auto callback = sidebar.GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kNone);
  EXPECT_TRUE(observer_.events.empty());
  EXPECT_FALSE(favorites->insertion_preview_visible_for_testing());
  EXPECT_FALSE(favorites->is_drop_indicator_visible_for_testing());
}

TEST_F(SidebarDnDDispatchTest, FavoritesDragCancelRestoresExpandedHeight) {
  MahoSidebarFavoritesGridView favorites(nullptr);
  MahoSidebarFavoritesModel model = MakeFavoritesModelForTesting(3);
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 120));
  favorites.Update(model);
  favorites.DeprecatedLayoutImmediately();
  const int original_height = favorites.GetPreferredSize().height();

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-new", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  EXPECT_EQ(favorites.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_GT(favorites.GetPreferredSize().height(), original_height);
  EXPECT_TRUE(favorites.insertion_preview_visible_for_testing());
  EXPECT_TRUE(favorites.is_drop_indicator_visible_for_testing());
  EXPECT_EQ(favorites.active_drag_tab_id_for_testing(), "tab-new");

  favorites.OnDragEnded();

  EXPECT_EQ(favorites.GetPreferredSize().height(), original_height);
  EXPECT_FALSE(favorites.insertion_preview_visible_for_testing());
  EXPECT_FALSE(favorites.is_drop_indicator_visible_for_testing());
  EXPECT_TRUE(favorites.active_drag_tab_id_for_testing().empty());
  EXPECT_TRUE(favorites.drag_accepted_tab_id_for_testing().empty());
  EXPECT_TRUE(favorites.pending_external_drag_tab_id_for_testing().empty());
  for (size_t index = 0; index < model.items.size(); ++index) {
    views::Button* tile = favorites.GetTileForTesting(index);
    ASSERT_TRUE(tile);
    ASSERT_TRUE(tile->layer());
    EXPECT_EQ(tile->layer()->GetTargetOpacity(), 1.0f);
    EXPECT_TRUE(tile->layer()->GetTargetTransform().IsIdentity());
  }
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteOriginDragSourceRemainsHiddenAfterZoneExit) {
  MahoSidebarFavoritesGridView favorites(nullptr);
  MahoSidebarFavoritesModel model = MakeFavoritesModelForTesting(1);
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  favorites.Update(model);

  views::Button* tile = favorites.GetTileForTesting(0);
  ASSERT_TRUE(tile);
  ASSERT_TRUE(tile->layer());

  favorites.OnDragStarted("fav-1");
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 0.f);

  favorites.OnFavoritesZoneExited();
  favorites.Update(model);

  EXPECT_EQ(favorites.active_drag_tab_id_for_testing(), "fav-1");
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 0.f);

  favorites.OnDragEnded();
  EXPECT_TRUE(favorites.active_drag_tab_id_for_testing().empty());
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 1.f);
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTileIndexDoesNotAppendFromYCoordinateAlone) {
  MahoSidebarFavoritesGridView favorites(nullptr);
  MahoSidebarFavoritesModel model = MakeFavoritesModelForTesting(2);
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  favorites.Update(model);
  favorites.DeprecatedLayoutImmediately();

  views::Button* last_tile = favorites.GetTileForTesting(1);
  ASSERT_TRUE(last_tile);

  gfx::Point point = last_tile->bounds().CenterPoint();
  views::View::ConvertPointToTarget(last_tile->parent(), &favorites, &point);
  point.Offset(-1, last_tile->height() + sidebar_layout::kFavoriteTileSpacingDp);

  EXPECT_EQ(favorites.TileIndexForPointForTesting(point, 2), 1);
}

TEST_F(SidebarDnDDispatchTest, FolderRowDropIndicatorResetsOnDragExit) {
  SidebarTreeNode folder_node = MakeFolderNode("folder-1");

  SidebarFolderRowView folder_row(
      folder_node, MahoSidebarTabSection::kNormal, "space-1",
      /*browser=*/nullptr, base::DoNothing(), base::DoNothing());

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

TEST_F(SidebarDnDDispatchTest, FolderRowBodyClickInvokesToggleCallback) {
  int toggle_count = 0;
  auto folder_row = std::make_unique<SidebarFolderRowView>(
      MakeFolderNode("folder-1"), MahoSidebarTabSection::kNormal, "space-1",
      nullptr,
      base::BindRepeating([](int* count) { ++(*count); }, &toggle_count),
      base::DoNothing());
  views::Widget::InitParams params = CreateParams(
      views::Widget::InitParams::Ownership::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  auto* folder_row_ptr = widget->SetContentsView(std::move(folder_row));
  widget->SetBounds(gfx::Rect(0, 0, 220, 40));
  widget->LayoutRootViewIfNecessary();

  views::LabelButton* label = folder_row_ptr->label_button_for_testing();
  ASSERT_TRUE(label);
  const gfx::Rect label_bounds = label->bounds();
  ASSERT_GT(label_bounds.x(), 0);

  const gfx::Point body_point(label_bounds.x() - 1,
                              folder_row_ptr->bounds().CenterPoint().y());
  EXPECT_FALSE(label_bounds.Contains(body_point));

  ui::MouseEvent press(ui::EventType::kMousePressed, body_point, body_point,
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, body_point, body_point,
                         base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);

  EXPECT_TRUE(folder_row_ptr->OnMousePressed(press));
  folder_row_ptr->OnMouseReleased(release);

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(toggle_count, 1);
}

TEST_F(SidebarDnDDispatchTest,
       FolderRowLabelRegionReleaseInvokesSingleToggleCallback) {
  int toggle_count = 0;
  auto folder_row = std::make_unique<SidebarFolderRowView>(
      MakeFolderNode("folder-1"), MahoSidebarTabSection::kNormal, "space-1",
      nullptr,
      base::BindRepeating([](int* count) { ++(*count); }, &toggle_count),
      base::DoNothing());
  views::Widget::InitParams params = CreateParams(
      views::Widget::InitParams::Ownership::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  auto* folder_row_ptr = widget->SetContentsView(std::move(folder_row));
  widget->SetBounds(gfx::Rect(0, 0, 220, 40));
  widget->LayoutRootViewIfNecessary();

  views::LabelButton* label = folder_row_ptr->label_button_for_testing();
  ASSERT_TRUE(label);
  const gfx::Point label_point = label->bounds().CenterPoint();

  ui::MouseEvent press(ui::EventType::kMousePressed, label_point, label_point,
                       base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, label_point,
                         label_point, base::TimeTicks::Now(),
                         ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);

  EXPECT_TRUE(folder_row_ptr->OnMousePressed(press));
  folder_row_ptr->OnMouseReleased(release);

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(toggle_count, 1);
}

TEST_F(SidebarDnDDispatchTest, FolderRowLabelButtonClickInvokesToggleOnce) {
  int toggle_count = 0;
  auto folder_row = std::make_unique<SidebarFolderRowView>(
      MakeFolderNode("folder-1"), MahoSidebarTabSection::kNormal, "space-1",
      nullptr,
      base::BindRepeating([](int* count) { ++(*count); }, &toggle_count),
      base::DoNothing());
  views::Widget::InitParams params = CreateParams(
      views::Widget::InitParams::Ownership::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_POPUP);
  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  auto* folder_row_ptr = widget->SetContentsView(std::move(folder_row));
  widget->SetBounds(gfx::Rect(0, 0, 220, 40));
  widget->LayoutRootViewIfNecessary();

  views::LabelButton* label = folder_row_ptr->label_button_for_testing();
  ASSERT_TRUE(label);

  views::test::ButtonTestApi(label).NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      base::TimeTicks::Now(), 0, 0));

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(toggle_count, 1);
}

TEST_F(SidebarDnDDispatchTest,
       FolderRowTopZoneDispatchesReorderBeforeTargetFolder) {
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
                                  nullptr, base::DoNothing(),
                                  base::DoNothing(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kInto);

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

TEST_F(SidebarDnDDispatchTest, RootFolderDropTopZoneDispatchesReorderBefore) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kBefore);

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
       RootFolderDropBottomZoneDispatchesReorderBeforeNextSibling) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 39));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kAfter);

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
       RootFolderDropBottomZoneWithoutNextSiblingDispatchesAppendReorder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 39));

  auto callback = folder_row.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[0].json.find("\"before_folder_id\":"),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, FolderDropOntoSelfReturnsNoOp) {
  SidebarFolderRowView folder_row(MakeFolderNode("same-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("same-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kNone);

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
       CrossParentNestedFolderTopZoneDispatchesReorderIntoTargetParent) {
  SidebarFolderRowView folder_row(
      MakeNestedFolderNode("target-folder", "target-parent"),
      MahoSidebarTabSection::kNormal, "space-1", nullptr, base::DoNothing(),
      base::DoNothing(), "next-folder", "target-parent");
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
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  EXPECT_NE(observer_.events[0].json.find("\"parent_folder_id\":\"target-parent\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"before_folder_id\":\"target-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       CrossParentNestedFolderBottomZoneDispatchesReorderBeforeNextSibling) {
  SidebarFolderRowView folder_row(
      MakeNestedFolderNode("target-folder", "target-parent"),
      MahoSidebarTabSection::kNormal, "space-1", nullptr, base::DoNothing(),
      base::DoNothing(), "next-folder", "target-parent");
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
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  EXPECT_NE(observer_.events[0].json.find("\"parent_folder_id\":\"target-parent\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"before_folder_id\":\"next-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       TabDropOnFolderMiddleZoneDispatchesMoveTabToFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
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
       TabDropOnFolderTopZoneDispatchesReorderBeforeFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kBefore);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"target-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       TabDropOnFolderBottomZoneWithoutNextSiblingDispatchesAppendReorder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 39));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kAfter);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"append\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropOnBottomZoneWhenDraggedFolderIsNextSiblingReturnsNoOp) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing(), "next-folder");
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("next-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 39));

  auto callback = folder_row.GetDropCallback(event);
  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kNone);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kMove;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kNone);
  EXPECT_TRUE(observer_.events.empty());
}

TEST_F(SidebarDnDDispatchTest,
       FolderChildTabDropOnFolderMiddleZoneDispatchesMoveTabToFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
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
       NestedFolderDragPayloadContainsSourceParentFolderId) {
  SidebarTreeNode nested_folder = MakeNestedFolderNode("child-f", "parent-f");
  SidebarFolderRowView folder_row(
      nested_folder, MahoSidebarTabSection::kNormal, "space-1",
      nullptr, base::DoNothing(), base::DoNothing(), "", "parent-f");

  ui::OSExchangeData data;
  folder_row.WriteDragDataForView(&folder_row, gfx::Point(5, 5), &data);

  SidebarDragPayload read_payload;
  ASSERT_TRUE(ReadMahoDragData(data, read_payload));
  EXPECT_EQ(read_payload.node_kind, SidebarNodeKind::kFolder);
  EXPECT_EQ(read_payload.node_id, "child-f");
  EXPECT_EQ(read_payload.source_parent_folder_id, "parent-f");
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneIdleHidesTargetButKeepsNormalModeSeparator) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_FALSE(pinned_target->GetVisible())
      << "Empty pinned section target must not reserve idle startup space";
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);
  EXPECT_TRUE(pinned_separator->GetVisible())
      << "Normal mode keeps the pinned/normal divider visible even when the "
         "pinned drop target is idle";

  auto* rows = list.tab_rows_for_testing();
  ASSERT_TRUE(rows);
  EXPECT_EQ(rows->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       TopBarHasNoNavigationControlsAndStaysVisibleWithoutBrowser) {
  MahoSidebarTopBarView top_bar(/*browser=*/nullptr);

  EXPECT_TRUE(top_bar.leading_cluster_for_testing()->GetVisible());
  EXPECT_TRUE(top_bar.nav_cluster_for_testing()->GetVisible());
  EXPECT_TRUE(top_bar.flex_spacer_for_testing()->GetVisible());
  EXPECT_TRUE(top_bar.sidebar_toggle_button_for_testing()->GetVisible());
  EXPECT_TRUE(top_bar.ai_button_for_testing()->GetVisible());

  // Back/forward/reload moved to MahoContentsHeaderView; the sidebar top bar
  // no longer hosts navigation controls.
  EXPECT_EQ(top_bar.nav_cluster_for_testing()->children().size(), 2u);

  top_bar.Update(MahoSidebarTopBarModel());

  EXPECT_TRUE(top_bar.leading_cluster_for_testing()->GetVisible());
  EXPECT_TRUE(top_bar.nav_cluster_for_testing()->GetVisible());
  EXPECT_TRUE(top_bar.flex_spacer_for_testing()->GetVisible());
  EXPECT_TRUE(top_bar.sidebar_toggle_button_for_testing()->GetEnabled());
  EXPECT_TRUE(top_bar.ai_button_for_testing()->GetEnabled());
}
TEST_F(SidebarDnDDispatchTest, TopBarMailVisibilityTracksEnabledPrefLive) {
  TestingProfile profile;
  TestBrowserWindow* window = new TestBrowserWindow();
  BrowserWindowCreateParams params(&profile, true);
  params.window = window;
  auto browser_owner =
      DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
  Browser* browser = browser_owner.get();

  PrefService* prefs = profile.GetPrefs();
  prefs->SetBoolean(sidebar_prefs::kMahoMailEnabled, false);
  MahoSidebarTopBarView top_bar(browser);
  ASSERT_TRUE(top_bar.mail_button_for_testing());
  EXPECT_FALSE(top_bar.mail_button_for_testing()->GetVisible());

  prefs->SetBoolean(sidebar_prefs::kMahoMailEnabled, true);
  EXPECT_TRUE(top_bar.mail_button_for_testing()->GetVisible());

  prefs->SetBoolean(sidebar_prefs::kMahoMailEnabled, false);
  EXPECT_FALSE(top_bar.mail_button_for_testing()->GetVisible());
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
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);
  EXPECT_TRUE(pinned_separator->GetVisible())
      << "Empty pinned separator may appear while the drop lane is revealed";

  auto* rows = list.tab_rows_for_testing();
  ASSERT_TRUE(rows);
  EXPECT_EQ(rows->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       DropHelperRoutesNormalTabDropToRevealedEmptyPinnedLane) {
  views::Widget widget;
  views::Widget::InitParams init_params(
      CreateParams(views::Widget::InitParams::TYPE_WINDOW_FRAMELESS));
  init_params.ownership = views::Widget::InitParams::CLIENT_OWNS_WIDGET;
  widget.Init(std::move(init_params));
  widget.SetBounds(gfx::Rect(0, 0, 240, 240));

  auto list_view = std::make_unique<MahoSidebarTabListView>(/*browser=*/nullptr);
  MahoSidebarTabListView* list = list_view.get();
  widget.SetContentsView(std::move(list_view));

  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("source-tab", 0));
  model.normal_tree.push_back(MakeTabNode("hover-tab", 1));
  list->Update(model, /*browser=*/nullptr);
  widget.Show();
  widget.LayoutRootViewIfNecessary();

  SidebarTabRowView* source_row =
      list->FindTabRowByIdForTesting("source-tab");
  SidebarTabRowView* hover_row =
      list->FindTabRowByIdForTesting("hover-tab");
  views::View* pinned_target =
      list->FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(source_row);
  ASSERT_TRUE(hover_row);
  ASSERT_TRUE(pinned_target);
  ASSERT_FALSE(pinned_target->GetVisible());

  auto center_in_root = [&widget](views::View* view) {
    gfx::Point point = view->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToTarget(view, widget.GetRootView(), &point);
    return point;
  };

  auto data = std::make_unique<ui::OSExchangeData>();
  source_row->WriteDragDataForView(source_row, gfx::Point(), data.get());
  const int move = static_cast<int>(ui::mojom::DragOperation::kMove);
  views::DropHelper drop_helper(widget.GetRootView());

  EXPECT_EQ(drop_helper.OnDragOver(*data, center_in_root(hover_row), move),
            move);
  ASSERT_TRUE(list->is_pinned_drop_lane_revealed_for_testing());
  widget.LayoutRootViewIfNecessary();
  ASSERT_TRUE(pinned_target->GetVisible());
  ASSERT_GT(pinned_target->height(), 0);

  // DropHelper reports exit on the parent while transferring ownership to the
  // newly revealed pinned child. That is not drag completion: collapsing here
  // would move the child out from under the cursor and restart the cycle.
  list->OnDragExited();
  EXPECT_TRUE(list->is_pinned_drop_lane_revealed_for_testing());
  EXPECT_TRUE(pinned_target->GetVisible());

  const gfx::Point pinned_point = center_in_root(pinned_target);
  const int selected_operation =
      drop_helper.OnDragOver(*data, pinned_point, move);
  EXPECT_EQ(selected_operation, move);
  ASSERT_EQ(drop_helper.target_view(), pinned_target)
      << "The widget-level hit test must select the pinned section itself; "
         "falling back to the list applies normal-section semantics";

  auto callback =
      drop_helper.GetDropCallback(*data, pinned_point, selected_operation);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(std::move(data), output,
                          std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"source-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"source-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);

  source_row->OnDragDone();
  EXPECT_FALSE(list->is_pinned_drop_lane_revealed_for_testing());
  EXPECT_FALSE(pinned_target->GetVisible());
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
  for (const auto& event_record : observer_.events) {
    EXPECT_NE(event_record.kind, "reorder_root_item");
  }
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
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);
  EXPECT_TRUE(pinned_separator->GetVisible())
      << "Drag exit hides only the transient target, not the normal-mode "
         "section divider";
  EXPECT_EQ(list.tab_rows_for_testing()->children().size(), 4u);
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneRevealedForFavoriteDragToAllowFavoritesToPinnedDrop) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-1", SidebarDragOrigin::kFavorites), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_TRUE(list.is_pinned_drop_lane_revealed_for_testing())
      << "Empty pinned section must be revealed for favorites-origin drags "
         "to allow the favorites->pinned drop target to be reachable";
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_TRUE(pinned_target->GetVisible())
      << "Pinned section drop target must be visible during a favorites drag";
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneRevealedWhenInternalDragHoversNormalTabRow) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 0));
  list.Update(model, /*browser=*/nullptr);

  auto* row = list.FindTabRowByIdForTesting("target-tab");
  ASSERT_TRUE(row);
  row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(
      MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
  EXPECT_EQ(row->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_TRUE(list.is_pinned_drop_lane_revealed_for_testing())
      << "Dragging over a child tab row must reveal the empty pinned lane so "
         "the pinned drop target stays reachable during internal drags";

  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_TRUE(pinned_target->GetVisible());
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneHidesWhenTabDragSourceCompletes) {
  MahoSidebarTabListView list(/*browser=*/nullptr);

  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  SidebarTabRowView* row = list.FindTabRowByIdForTesting("tab-1");
  ASSERT_TRUE(row);
  row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  EXPECT_EQ(list.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));

  EXPECT_TRUE(list.is_pinned_drop_lane_revealed_for_testing());
  views::View* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_TRUE(pinned_target->GetVisible());
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);
  EXPECT_TRUE(pinned_separator->GetVisible());

  row->OnDragDone();

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
  EXPECT_FALSE(pinned_target->GetVisible());
  EXPECT_TRUE(pinned_separator->GetVisible());
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneContributesHeightOnlyWhileRevealed) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  views::View* rows = list.tab_rows_for_testing();
  ASSERT_TRUE(rows);
  views::View* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);

  const int idle_height = rows->GetPreferredSize().height();
  const int reveal_height = pinned_target->GetPreferredSize().height();
  ASSERT_GT(reveal_height, 0);
  EXPECT_FALSE(pinned_target->GetVisible());
  EXPECT_TRUE(pinned_separator->GetVisible());

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_EQ(rows->GetPreferredSize().height(), idle_height + reveal_height)
      << "Revealing an empty pinned lane must add exactly its tab-height "
         "target; the divider is already present at idle";

  list.OnDragExited();

  EXPECT_EQ(rows->GetPreferredSize().height(), idle_height)
      << "Hiding the transient target must return the list to its exact idle "
         "height without removing the divider";
  EXPECT_FALSE(pinned_target->GetVisible());
  EXPECT_TRUE(pinned_separator->GetVisible());
}

TEST_F(SidebarDnDDispatchTest,
       PrivateModeNeverRevealsEmptyPinnedLaneOrChangesHeight) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  list.SetPrivateMode(true);
  MahoSidebarTabListModel model;
  model.active_space_id = "private";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  views::View* rows = list.tab_rows_for_testing();
  ASSERT_TRUE(rows);
  views::View* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);
  const int idle_height = rows->GetPreferredSize().height();
  EXPECT_FALSE(pinned_target->GetVisible());
  EXPECT_FALSE(pinned_separator->GetVisible());

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());
  EXPECT_FALSE(pinned_target->GetVisible());
  EXPECT_FALSE(pinned_separator->GetVisible());
  EXPECT_EQ(rows->GetPreferredSize().height(), idle_height);
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

  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_NE(pinned_target, nullptr);
  EXPECT_TRUE(pinned_target->GetVisible())
      << "Non-empty pinned sections must remain visible at idle";
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);
  EXPECT_TRUE(pinned_separator->GetVisible())
      << "Non-empty pinned sections keep their separator visible";
  EXPECT_EQ(list.tab_rows_for_testing()->children().size(), 4u);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);
  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing());

  list.OnDragExited();
  pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_NE(pinned_target, nullptr);
  EXPECT_TRUE(pinned_target->GetVisible());
  pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator);
  EXPECT_TRUE(pinned_separator->GetVisible());
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
  views::View* pinned_separator = list.pinned_separator_for_testing();
  ASSERT_TRUE(pinned_separator)
      << "Empty pinned separator view should still exist for future reveal";
  EXPECT_TRUE(pinned_separator->GetVisible())
      << "Update() clears only the transient target; normal mode retains the "
         "section divider";
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
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 30));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 15));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kNone));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kNone);
  EXPECT_FALSE(row.is_drop_indicator_visible_for_testing());
}

TEST_F(SidebarDnDDispatchTest,
       FolderOnTabTopZoneDispatchesBeforeTargetTab) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 30));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);
  EXPECT_TRUE(row.is_drop_indicator_visible_for_testing());

  auto callback = row.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropOnTabRowTopThirdResolvesAndDispatchesBeforeTargetBehavior) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 30));

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);
  EXPECT_TRUE(row.is_drop_indicator_visible_for_testing());

  auto callback = row.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       NestedFolderOnTabTopZoneDispatchesRootExtractionThenPositionedReorder) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 30));

  ui::OSExchangeData data;
  WriteDropData(MakeNestedFolderPayload("source-folder", "parent-folder"),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "move_folder_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"space_id\":\"space-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(
    SidebarDnDDispatchTest,
    NestedFolderDropBeforeTabRowBodyDispatchesMoveFolderToRootThenReorder) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target tab",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 30));

  ui::OSExchangeData data;
  WriteDropData(MakeNestedFolderPayload("source-folder", "parent-folder"),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "move_folder_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"space_id\":\"space-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"target-tab\""),
            std::string::npos);
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
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(folder_row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(folder_row.drop_zone_for_testing(),
            SidebarFolderRowView::SidebarFolderDropZone::kInto);

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
  ASSERT_EQ(observer_.events.size(), 1u);
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
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events.back().kind, "reorder_root_item");
  EXPECT_NE(observer_.events.back().json.find("\"kind\":\"folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events.back().json.find("\"id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events.back().json.find("\"kind\":\"append\""),
            std::string::npos);
}

// ---------------------------------------------------------------------------
// Favorites-origin drop acceptance callback tests.
// ---------------------------------------------------------------------------

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnTabRowInvokesAcceptedCallback) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  int callback_count = 0;
  std::string last_accepted_id;
  row.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, std::string* last,
                             const std::string& tab_id) {
        ++(*count);
        *last = tab_id;
      }, &callback_count, &last_accepted_id));

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(callback_count, 1)
      << "Callback must fire exactly once for favorites-origin tab row drop";
  EXPECT_EQ(last_accepted_id, "fav-tab");
  ASSERT_FALSE(observer_.events.empty());
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"normal\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       NonFavoriteTabDropOnTabRowDoesNotInvokeAcceptedCallback) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return -1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  int callback_count = 0;
  row.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, const std::string&) {
        ++(*count);
      }, &callback_count));

  SidebarDragPayload payload =
      MakeTabPayload("normal-tab", SidebarDragOrigin::kNormalSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(callback_count, 0)
      << "Callback must NOT fire for non-favorites-origin drops";
  for (const auto& ev : observer_.events) {
    EXPECT_NE(ev.kind, "change_tab_role");
  }
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnFolderRowInvokesAcceptedCallback) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  int callback_count = 0;
  std::string last_accepted_id;
  folder_row.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, std::string* last,
                             const std::string& tab_id) {
        ++(*count);
        *last = tab_id;
      }, &callback_count, &last_accepted_id));

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));
  auto callback = folder_row.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(callback_count, 1)
      << "Callback must fire exactly once for favorites-origin folder row drop";
  EXPECT_EQ(last_accepted_id, "fav-tab");
  bool found_unfavorite = false;
  for (const auto& ev : observer_.events) {
    if (ev.kind == "change_tab_role") {
      found_unfavorite = true;
      EXPECT_NE(ev.json.find("\"tab_id\":\"fav-tab\""), std::string::npos);
    }
  }
  EXPECT_TRUE(found_unfavorite);
}

TEST_F(SidebarDnDDispatchTest,
       NonFavoriteTabDropOnFolderRowDoesNotInvokeAcceptedCallback) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  int callback_count = 0;
  folder_row.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, const std::string&) {
        ++(*count);
      }, &callback_count));

  SidebarDragPayload payload =
      MakeTabPayload("normal-tab", SidebarDragOrigin::kNormalSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));
  auto callback = folder_row.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(callback_count, 0)
      << "Callback must NOT fire for non-favorites folder row drops";
  for (const auto& ev : observer_.events) {
    EXPECT_NE(ev.kind, "change_tab_role");
  }
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnListViewAppendPathInvokesAcceptedCallback) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  int callback_count = 0;
  std::string last_accepted_id;
  list.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, std::string* last,
                             const std::string& tab_id) {
        ++(*count);
        *last = tab_id;
      }, &callback_count, &last_accepted_id));

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 200));
  auto callback = list.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(callback_count, 1)
      << "Callback must fire exactly once for favorites-origin list-view append drop";
  EXPECT_EQ(last_accepted_id, "fav-tab");
  bool found_unfavorite = false;
  for (const auto& ev : observer_.events) {
    if (ev.kind == "change_tab_role") {
      found_unfavorite = true;
      EXPECT_NE(ev.json.find("\"tab_id\":\"fav-tab\""), std::string::npos);
    }
  }
  EXPECT_TRUE(found_unfavorite);
}

TEST_F(SidebarDnDDispatchTest,
       NonFavoriteTabDropOnListViewAppendPathDoesNotInvokeAcceptedCallback) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  int callback_count = 0;
  list.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, const std::string&) {
        ++(*count);
      }, &callback_count));

  SidebarDragPayload payload =
      MakeTabPayload("normal-tab", SidebarDragOrigin::kNormalSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 200));
  auto callback = list.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(callback_count, 0)
      << "Callback must NOT fire for non-favorites list-view append drops";
  for (const auto& ev : observer_.events) {
    EXPECT_NE(ev.kind, "change_tab_role");
  }
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnTabRowViaListViewInvokesAcceptedCallback) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  int callback_count = 0;
  std::string last_accepted_id;
  list.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, std::string* last,
                             const std::string& tab_id) {
        ++(*count);
        *last = tab_id;
      }, &callback_count, &last_accepted_id));

  list.Update(model, /*browser=*/nullptr);

  SidebarTabRowView* row = list.FindTabRowByIdForTesting("target-tab");
  ASSERT_TRUE(row);
  row->SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = row->GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(callback_count, 1)
      << "Callback wired from list must fire on tab-row favorites-origin drop";
  EXPECT_EQ(last_accepted_id, "fav-tab");
}

TEST_F(SidebarDnDDispatchTest,
       PinnedTabDropOnNormalSectionLaneDispatchesUnpinThenReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload = MakeTabPayload(
      "pinned-tab", SidebarDragOrigin::kPinnedSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "unpin_tab");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
}

TEST_F(SidebarDnDDispatchTest,
       PinnedTabDropOnNormalListRootDispatchesUnpinThenReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  SidebarDragPayload payload = MakeTabPayload(
      "pinned-tab", SidebarDragOrigin::kPinnedSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 200));

  auto callback = list.GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "unpin_tab");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       PinnedRootTabOnInsertionLaneBeforeNormalRootDispatches_UnpinThenReorderWithBeforePoint) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("normal-target", 1));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeTabByIdForTesting("normal-target");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload =
      MakeTabPayload("pinned-tab", SidebarDragOrigin::kPinnedSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u)
      << "Exactly two events expected: unpin_tab then reorder_root_item";
  EXPECT_EQ(observer_.events[0].kind, "unpin_tab");
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  SCOPED_TRACE(observer_.events[1].json);
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"tab\""),
            std::string::npos)
      << "reorder_root_item item.kind must be \"tab\"";
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"pinned-tab\""),
            std::string::npos)
      << "reorder_root_item item.id must be \"pinned-tab\"";
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"before\""),
            std::string::npos)
      << "insertion_point.kind must be \"before\" for a lane drop";
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"normal-target\""),
            std::string::npos)
      << "insertion_point target id must be \"normal-target\"";

  auto parsed = ParseJson(observer_.events[1].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("item") != nullptr);
  ASSERT_TRUE(dict.Find("insertion_point") != nullptr);
}

TEST_F(SidebarDnDDispatchTest,
       PinnedRootTabOnListRootAppendPathDispatches_UnpinThenReorderWithAppendPoint) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  SidebarDragPayload payload =
      MakeTabPayload("pinned-tab", SidebarDragOrigin::kPinnedSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 200));

  auto callback = list.GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u)
      << "Exactly two events expected: unpin_tab then reorder_root_item";
  EXPECT_EQ(observer_.events[0].kind, "unpin_tab");
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  SCOPED_TRACE(observer_.events[1].json);
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"tab\""),
            std::string::npos)
      << "reorder_root_item item.kind must be \"tab\"";
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"pinned-tab\""),
            std::string::npos)
      << "reorder_root_item item.id must be \"pinned-tab\"";
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos)
      << "insertion_point.kind must be \"append\" for a list-root append drop";

  auto parsed = ParseJson(observer_.events[1].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("item") != nullptr);
  ASSERT_TRUE(dict.Find("insertion_point") != nullptr);
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnNormalSectionLaneDispatchesUnfavoriteThenReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload = MakeTabPayload(
      "fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"normal\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnFolderLaneDispatchesUnfavoriteThenReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeFolderNode("target-folder"));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload = MakeTabPayload(
      "fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role");
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"normal\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
}

TEST_F(SidebarDnDDispatchTest,
       PinnedTabDropOnNormalFolderRowDispatchesUnpinThenMoveIntoFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload = MakeTabPayload(
      "pinned-tab", SidebarDragOrigin::kPinnedSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  auto callback = folder_row.GetDropCallback(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "unpin_tab");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "move_tab_to_folder");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
}


TEST_F(SidebarDnDDispatchTest,
       PinnedFolderChildTabOnInsertionLaneDispatchesMoveToRootUnpinThenReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane = list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload = MakeTabPayload(
      "pinned-child", SidebarDragOrigin::kPinnedSection);
  payload.source_parent_folder_id = "pinned-folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 3u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"pinned-folder-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-child\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "unpin_tab");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"pinned-child\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[2].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[2].json.find("\"kind\":\"tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabOnFolderRowDispatchesUnfavoriteThenMoveToFolder) {
  SidebarFolderRowView folder_row(MakeFolderNode("target-folder"),
                                  MahoSidebarTabSection::kNormal, "space-1",
                                  nullptr, base::DoNothing(),
                                  base::DoNothing());
  folder_row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));
  auto callback = folder_row.GetDropCallback(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"normal\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "move_tab_to_folder");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"fav-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"folder_id\":\"target-folder\""),
            std::string::npos);
}

// ---------------------------------------------------------------------------
// Wave 0 baseline: state-model contract invariants (pure unit tests, no FFI).
//
// These tests protect the structural contracts that the identity/refresh rewrite
// depends on in later waves.  They run without a real Browser* or maho-core
// instance and therefore execute fast as pure ViewsTestBase unit tests.
// ---------------------------------------------------------------------------

// SidebarStateModelContractTest — a separate fixture so the Wave 0 contract
// tests stay clearly delineated from the DnD dispatch tests above.
// Some contract cases build a TestingProfile + Browser, whose keyed services
// (extensions::StateStore) post to the UI thread; that requires a
// BrowserTaskEnvironment rather than the plain ViewsTestBase task environment.
class SidebarStateModelContractTest : public views::ViewsTestBase {
 public:
  SidebarStateModelContractTest()
      : views::ViewsTestBase(
            std::unique_ptr<base::test::TaskEnvironment>(
                std::make_unique<content::BrowserTaskEnvironment>())) {}
};

// --- SidebarTreeNode field-kind invariants -----------------------------------

// A tab node built from scratch must have kind==kTab and must not populate
// folder-specific fields.
TEST_F(SidebarStateModelContractTest,
       TabNodeKindExcludesFolderFields) {
  SidebarTreeNode node = MakeTabNode("tab-abc", 3);
  EXPECT_EQ(node.kind, SidebarNodeKind::kTab);
  EXPECT_EQ(node.tab_id, "tab-abc");
  EXPECT_EQ(node.tab_strip_index, 3);
  // Folder fields must be empty / default for a pure tab node.
  EXPECT_TRUE(node.folder_id.empty())
      << "Tab node must not carry a folder_id";
  EXPECT_TRUE(node.folder_name.empty())
      << "Tab node must not carry a folder_name";
  EXPECT_FALSE(node.folder_is_pinned)
      << "Tab node must not be marked folder_is_pinned";
}

// A folder node built from scratch must have kind==kFolder and must not
// populate tab-specific identity fields.
TEST_F(SidebarStateModelContractTest,
       FolderNodeKindExcludesTabFields) {
  SidebarTreeNode node = MakeFolderNode("folder-xyz");
  EXPECT_EQ(node.kind, SidebarNodeKind::kFolder);
  EXPECT_EQ(node.folder_id, "folder-xyz");
  // Tab fields must be empty / default for a pure folder node.
  EXPECT_TRUE(node.tab_id.empty())
      << "Folder node must not carry a tab_id";
  EXPECT_EQ(node.tab_strip_index, -1)
      << "Folder node must default tab_strip_index to -1";
  EXPECT_FALSE(node.is_pinned)
      << "Folder node must not be marked is_pinned (use folder_is_pinned)";
}

// A folder node with children must have those children visible in the
// children vector, preserving order and kind.
TEST_F(SidebarStateModelContractTest,
       FolderNodeChildrenPreserveOrderAndKind) {
  SidebarTreeNode folder = MakeFolderNode("parent");
  folder.children.push_back(MakeFolderChildTabNode("child-0", 0, "parent", 0));
  folder.children.push_back(MakeFolderChildTabNode("child-1", 1, "parent", 1));
  folder.children.push_back(MakeFolderChildTabNode("child-2", 2, "parent", 2));

  ASSERT_EQ(folder.children.size(), 3u);
  for (size_t i = 0; i < folder.children.size(); ++i) {
    EXPECT_EQ(folder.children[i].kind, SidebarNodeKind::kTab);
    EXPECT_EQ(folder.children[i].parent_folder_id, "parent");
    EXPECT_EQ(folder.children[i].folder_child_index, static_cast<int>(i));
  }
}

// --- MahoSidebarTabListModel section invariants -----------------------------

// An empty MahoSidebarTabListModel must default-construct cleanly: no
// active_space_id, empty trees, empty flat lists.
TEST_F(SidebarStateModelContractTest,
       TabListModelDefaultConstructedIsEmpty) {
  MahoSidebarTabListModel model;
  EXPECT_TRUE(model.active_space_id.empty())
      << "Default model must have no active_space_id";
  EXPECT_TRUE(model.pinned_tree.empty())
      << "Default model must have no pinned_tree nodes";
  EXPECT_TRUE(model.normal_tree.empty())
      << "Default model must have no normal_tree nodes";
  EXPECT_EQ(model.active_tab.index, -1)
      << "Default active_tab.index must be -1";
}

// active_space_id survives a copy-assign round-trip (used by ScheduleRefreshAll
// path to hand the captured space_id into the background task).
TEST_F(SidebarStateModelContractTest,
       TabListModelActiveSpaceIdRoundTripsViaCopy) {
  MahoSidebarTabListModel original;
  original.active_space_id = "space-wave0";
  original.normal_tree.push_back(MakeTabNode("t1", 0));

  MahoSidebarTabListModel copy = original;
  EXPECT_EQ(copy.active_space_id, "space-wave0")
      << "active_space_id must survive MahoSidebarTabListModel copy-assign";
  ASSERT_EQ(copy.normal_tree.size(), 1u);
  EXPECT_EQ(copy.normal_tree[0].tab_id, "t1");
}

// Pinned and normal trees must be independently mutable: clearing one must
// not affect the other.
TEST_F(SidebarStateModelContractTest,
       TabListModelPinnedAndNormalTreesAreIndependent) {
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode pinned = MakeTabNode("pinned-1", 0);
  pinned.is_pinned = true;
  model.pinned_tree.push_back(pinned);
  model.normal_tree.push_back(MakeTabNode("normal-1", 1));

  model.pinned_tree.clear();
  EXPECT_TRUE(model.pinned_tree.empty())
      << "Clearing pinned_tree must leave it empty";
  ASSERT_EQ(model.normal_tree.size(), 1u)
      << "Clearing pinned_tree must not affect normal_tree";
  EXPECT_EQ(model.normal_tree[0].tab_id, "normal-1");
}

// --- MahoSidebarFavoritesModel slot-count cap invariant ---------------------

// The slot count must equal the layout constant after default construction
// and must survive a move.
TEST_F(SidebarStateModelContractTest,
       FavoritesModelSlotCountEqualsLayoutConstant) {
  MahoSidebarFavoritesModel model;
  EXPECT_EQ(model.slot_count,
            sidebar_layout::kMahoSidebarFavoriteSlotCount)
      << "slot_count must equal kMahoSidebarFavoriteSlotCount at construction";
  EXPECT_TRUE(model.items.empty())
      << "Default favorites model must have no items";
}

// Adding items up to slot_count and beyond: the model itself does not enforce
// the cap (that is the adapter's responsibility), but the slot_count field
// must remain stable and readable after the items vector grows.
TEST_F(SidebarStateModelContractTest,
       FavoritesModelSlotCountRemainsStableAfterItemsAdded) {
  MahoSidebarFavoritesModel model;
  const size_t cap = model.slot_count;

  MahoSidebarFavoriteItemModel item;
  item.title = u"Test";
  item.url = GURL("https://example.com");
  item.tab_id = "fav-t";
  model.items.push_back(item);

  EXPECT_EQ(model.slot_count, cap)
      << "slot_count must not change when items are added";
  EXPECT_EQ(model.items.size(), 1u);
}

// --- MahoSidebarViewStateModel structural shape ----------------------------

// BuildViewStateModel with a null browser must not crash and must return a
// model with all four sub-model fields default-initialized.  This documents
// the defensive null-browser contract that Wave 0 stabilizes.
TEST_F(SidebarStateModelContractTest,
       BuildViewStateModelWithNullBrowserReturnsDefaultShape) {
  MahoSidebarStateAdapter adapter;
  // Calling with nullptr is the documented null-browser defensive path.
  // It must not crash, and the returned model must be structurally valid.
  MahoSidebarViewStateModel model = adapter.BuildViewStateModel(nullptr);

  // top_bar: navigation controls (back/forward/reload) moved to
  // MahoContentsHeaderView; the sidebar top_bar sub-model no longer carries
  // navigation state, so there is nothing left to assert here.

  // favorites: slot_count must be the layout constant.
  EXPECT_EQ(model.favorites.slot_count,
            sidebar_layout::kMahoSidebarFavoriteSlotCount)
      << "favorites.slot_count must equal layout constant";

  // tab_list: active_space_id may be empty (no bridge in unit test context),
  // but the model must be default-valid (no uninitialized state).
  EXPECT_EQ(model.tab_list.active_tab.index, -1)
      << "active_tab.index must be -1 for null browser";
  EXPECT_TRUE(model.tab_list.pinned_tree.empty())
      << "pinned_tree must be empty for null browser";
  EXPECT_TRUE(model.tab_list.normal_tree.empty())
      << "normal_tree must be empty for null browser";

  // footer: can_create_space must be false without a browser.
  EXPECT_FALSE(model.footer.can_create_space)
      << "footer.can_create_space must be false for null browser";
}

// BuildViewStateModel called twice on the same adapter with null browser
// must return equal results (deterministic / no hidden mutation).
TEST_F(SidebarStateModelContractTest,
       BuildViewStateModelIsIdempotentForNullBrowser) {
  MahoSidebarStateAdapter adapter;
  MahoSidebarViewStateModel first  = adapter.BuildViewStateModel(nullptr);
  MahoSidebarViewStateModel second = adapter.BuildViewStateModel(nullptr);

  EXPECT_EQ(first.favorites.slot_count, second.favorites.slot_count);
  EXPECT_EQ(first.tab_list.active_tab.index, second.tab_list.active_tab.index);
  EXPECT_EQ(first.footer.can_create_space, second.footer.can_create_space);
}

// SetFavoritesModel followed by BuildViewStateModel must propagate the stored
// favorites into the returned model (the Wave 0 "set-then-build" pipeline).
TEST_F(SidebarStateModelContractTest,
       SetFavoritesModelPropagatesIntoViewStateModel) {
  MahoSidebarStateAdapter adapter;

  MahoSidebarFavoritesModel favs;
  MahoSidebarFavoriteItemModel item;
  item.title = u"Wave0 Fav";
  item.url = GURL("https://wave0.example");
  item.tab_id = "wave0-fav";
  favs.items.push_back(item);
  adapter.SetFavoritesModel(favs);

  MahoSidebarViewStateModel model = adapter.BuildViewStateModel(nullptr);
  ASSERT_EQ(model.favorites.items.size(), 1u)
      << "favorites must reflect what was set via SetFavoritesModel";
  EXPECT_EQ(model.favorites.items[0].tab_id, "wave0-fav");
  EXPECT_EQ(model.favorites.items[0].url, GURL("https://wave0.example"));
}

// --- Drag payload serialization round-trip (tab_id stability) ---------------

// The tab_id carried in SidebarDragPayload must survive a write/read
// round-trip via WriteMahoDragData / ReadMahoDragData.  This is the
// "stable identity across serialization" baseline that later phases extend.
TEST_F(SidebarStateModelContractTest,
       DragPayloadTabIdSurvivesSerializationRoundTrip) {
  const std::string kTabId = "wave0-stable-tab-id";
  SidebarDragPayload written = MakeTabPayload(kTabId,
                                              SidebarDragOrigin::kNormalSection);
  ui::OSExchangeData data;
  WriteMahoDragData(written, &data);

  SidebarDragPayload read;
  ASSERT_TRUE(ReadMahoDragData(data, read))
      << "ReadMahoDragData must succeed for a validly written payload";
  EXPECT_EQ(read.node_id, kTabId)
      << "node_id (tab_id) must survive serialize/deserialize unchanged";
  EXPECT_EQ(read.node_kind, SidebarNodeKind::kTab);
  EXPECT_EQ(read.origin, SidebarDragOrigin::kNormalSection);
}

// Folder payloads must also round-trip without loss of folder_id.
TEST_F(SidebarStateModelContractTest,
       DragPayloadFolderIdSurvivesSerializationRoundTrip) {
  const std::string kFolderId = "wave0-stable-folder-id";
  SidebarDragPayload written = MakeFolderPayload(kFolderId);
  ui::OSExchangeData data;
  WriteMahoDragData(written, &data);

  SidebarDragPayload read;
  ASSERT_TRUE(ReadMahoDragData(data, read));
  EXPECT_EQ(read.node_id, kFolderId)
      << "node_id (folder_id) must survive serialize/deserialize unchanged";
  EXPECT_EQ(read.node_kind, SidebarNodeKind::kFolder);
}

// A folder-child tab payload must preserve the source_parent_folder_id
// through the round-trip (this is the atomic cross-folder context that the
// identity rewrite must not break).
TEST_F(SidebarStateModelContractTest,
       DragPayloadSourceParentFolderIdSurvivesRoundTrip) {
  SidebarDragPayload written =
      MakeFolderChildTabPayload("child-tab", "parent-folder-id", 2);
  ui::OSExchangeData data;
  WriteMahoDragData(written, &data);

  SidebarDragPayload read;
  ASSERT_TRUE(ReadMahoDragData(data, read));
  EXPECT_EQ(read.node_id, "child-tab");
  EXPECT_EQ(read.source_parent_folder_id, "parent-folder-id")
      << "source_parent_folder_id must survive round-trip";
  EXPECT_EQ(read.source_folder_child_index, 2)
      << "source_folder_child_index must survive round-trip";
}

// --- TabListView Update idempotency -----------------------------------------

// Calling Update() twice with identical models must leave the view in the
// same structural state as calling it once (idempotent application contract).
TEST_F(SidebarStateModelContractTest,
       TabListViewUpdateIsStructurallyIdempotent) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-wave0";
  model.normal_tree.push_back(MakeTabNode("tab-a", 0));
  model.normal_tree.push_back(MakeTabNode("tab-b", 1));

  list.Update(model, /*browser=*/nullptr);
  auto* rows_after_first = list.tab_rows_for_testing();
  ASSERT_TRUE(rows_after_first);
  const size_t count_after_first = rows_after_first->children().size();

  list.Update(model, /*browser=*/nullptr);
  auto* rows_after_second = list.tab_rows_for_testing();
  ASSERT_TRUE(rows_after_second);
  EXPECT_EQ(rows_after_second->children().size(), count_after_first)
      << "A second Update() with the same model must produce the same "
         "child count as the first";
}

// A transient empty core push must never clear the RENDERED rows. Update()'s
// adoption guard already refuses to store such a model in last_model_, but the
// rejected model used to be handed to RebuildRows() anyway, wiping every row —
// the "sidebar tab list vanishes" regression. The rows must survive until a
// real model arrives.
TEST_F(SidebarStateModelContractTest,
       TransientEmptyPushDoesNotClearRenderedRows) {
  // No Browser: the unavailable-tree guard runs before any tab-strip based
  // adoption logic, and a TestingProfile inside ViewsTestBase crashes in the
  // extensions StateStore on Linux.
  Browser* browser = nullptr;
  auto* list = new MahoSidebarTabListView(browser);
  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetContentsView(list);

  MahoSidebarTabListModel populated;
  populated.active_space_id = "space-wave0";
  populated.normal_tree.push_back(MakeTabNode("tab-a", 0));
  populated.normal_tree.push_back(MakeTabNode("tab-b", 1));
  list->Update(populated, browser);
  ASSERT_NE(list->FindTabRowByIdForTesting("tab-a"), nullptr);
  ASSERT_NE(list->FindTabRowByIdForTesting("tab-b"), nullptr);
  const int rebuilds_after_populated = list->rebuild_rows_count_for_testing();

  // No widget capture here: this is the plain push path, not the drag path.
  // The core could not produce a tree (null v2 payload) for the space on
  // screen: that is not data and must not be rendered.
  MahoSidebarTabListModel transient_empty;
  transient_empty.active_space_id = "space-wave0";
  transient_empty.tree_unavailable = true;
  list->Update(std::move(transient_empty), browser);

  EXPECT_NE(list->FindTabRowByIdForTesting("tab-a"), nullptr)
      << "a transient empty push must not clear already-rendered rows";
  EXPECT_NE(list->FindTabRowByIdForTesting("tab-b"), nullptr)
      << "a transient empty push must not clear already-rendered rows";
  EXPECT_EQ(list->rebuild_rows_count_for_testing(), rebuilds_after_populated)
      << "a rejected empty push must not trigger a rebuild at all";

  // A genuinely new model must still be applied.
  MahoSidebarTabListModel next;
  next.active_space_id = "space-wave0";
  next.normal_tree.push_back(MakeTabNode("tab-c", 0));
  list->Update(std::move(next), browser);
  EXPECT_NE(list->FindTabRowByIdForTesting("tab-c"), nullptr)
      << "rejecting empty pushes must not block later real updates";

  // Switching to a genuinely EMPTY space must still render empty: the guard is
  // scoped to the space already on screen, so it must not strand the previous
  // space's rows in the new space.
  MahoSidebarTabListModel empty_other_space;
  empty_other_space.active_space_id = "space-empty";
  list->Update(std::move(empty_other_space), browser);
  EXPECT_EQ(list->FindTabRowByIdForTesting("tab-c"), nullptr)
      << "switching to an empty space must clear the previous space's rows";

  // A genuinely EMPTY tree for the space on screen (the core answered with an
  // empty list, e.g. its last tab closed) is data and must clear the rows;
  // only an unavailable tree is ignored.
  MahoSidebarTabListModel populated_empty_space;
  populated_empty_space.active_space_id = "space-empty";
  populated_empty_space.normal_tree.push_back(MakeTabNode("tab-d", 0));
  list->Update(std::move(populated_empty_space), browser);
  ASSERT_NE(list->FindTabRowByIdForTesting("tab-d"), nullptr);
  MahoSidebarTabListModel genuinely_empty;
  genuinely_empty.active_space_id = "space-empty";
  list->Update(std::move(genuinely_empty), browser);
  EXPECT_EQ(list->FindTabRowByIdForTesting("tab-d"), nullptr)
      << "a genuinely empty tree for the rendered space must clear its rows";
}

TEST_F(SidebarStateModelContractTest,
       CaptureDeferredTransientEmptyModelPreservesRenderedRows) {
  TestingProfile profile;
  TestBrowserWindow* window = new TestBrowserWindow();
  BrowserWindowCreateParams params(&profile, true);
  params.window = window;
  auto browser_owner =
      DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
  Browser* browser = browser_owner.get();
  browser->GetTabStripModel()->AppendWebContents(
      content::WebContents::Create(
          content::WebContents::CreateParams(&profile)),
      /*foreground=*/true);

  auto* list = new MahoSidebarTabListView(browser);
  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetContentsView(list);

  MahoSidebarTabListModel populated;
  populated.active_space_id = "space-wave0";
  populated.normal_tree.push_back(MakeTabNode("tab-a", 0));
  list->Update(populated, browser);
  ASSERT_NE(list->FindTabRowByIdForTesting("tab-a"), nullptr);

  widget->SetCapture(list);
  ASSERT_TRUE(widget->HasCapture());
  MahoSidebarTabListModel transient_empty;
  transient_empty.active_space_id = "space-wave0";
  list->Update(std::move(transient_empty), browser);
  widget->ReleaseCapture();
  list->FlushDeferredRebuild();

  EXPECT_NE(list->FindTabRowByIdForTesting("tab-a"), nullptr)
      << "A transient empty state push rejected by Update's adoption guard "
         "must not be queued during drag capture and replayed after release";
}

// After Update() with a model containing only normal tabs, the pinned section
// must not be visible (empty-pinned contract referenced by Wave 0 layout
// stabilization).
TEST_F(SidebarStateModelContractTest,
       TabListViewNormalOnlyModelHidesEmptyPinnedSection) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-wave0";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target)
      << "Empty pinned section must still exist in the view tree";
  EXPECT_FALSE(pinned_target->GetVisible())
      << "Empty pinned section must be hidden when normal_tree has content "
         "but pinned_tree is empty";
}

// After Update() with a model that has pinned nodes, the pinned section must
// be present and visible.
TEST_F(SidebarStateModelContractTest,
       TabListViewPinnedNodesModelShowsPinnedSection) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-wave0";
  SidebarTreeNode pinned = MakeTabNode("pinned-t", 0);
  pinned.is_pinned = true;
  model.pinned_tree.push_back(pinned);
  model.normal_tree.push_back(MakeTabNode("normal-t", 1));
  list.Update(model, /*browser=*/nullptr);

  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target)
      << "Pinned section drop target must exist when pinned_tree is non-empty";
  EXPECT_TRUE(pinned_target->GetVisible())
      << "Pinned section must be visible when pinned_tree has content";
}

TEST_F(SidebarStateModelContractTest,
       TabListViewDirectlyOwnsRowsWithoutInternalScrollView) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-wave0";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));

  list.Update(model, /*browser=*/nullptr);

  views::View* rows = list.tab_rows_for_testing();
  ASSERT_TRUE(rows);
  EXPECT_EQ(rows->parent(), &list)
      << "MahoSidebarTabListView should directly own its content rows; "
         "tabs-mode vertical scrolling belongs to MahoSidebarView";
}

// --- Stable identity: drag payload no longer serializes tab_strip_index ------

TEST_F(SidebarStateModelContractTest,
       DragPayloadDoesNotSerializeTabStripIndex) {
  SidebarDragPayload written;
  written.node_kind = SidebarNodeKind::kTab;
  written.node_id = "tab-with-index";
  written.origin = SidebarDragOrigin::kNormalSection;
  written.space_id = "space-1";
  written.tab_strip_index = 42;
  written.source_parent_folder_id = "";
  written.source_folder_child_index = -1;

  std::string json = SerializeDragPayload(written);
  EXPECT_EQ(json.find("tab_strip_index"), std::string::npos)
      << "Serialized drag payload must not contain tab_strip_index; "
         "live index must be resolved at drop time from stable ID";

  ui::OSExchangeData data;
  WriteMahoDragData(written, &data);
  SidebarDragPayload read;
  ASSERT_TRUE(ReadMahoDragData(data, read));
  EXPECT_EQ(read.tab_strip_index, -1)
      << "Deserialized tab_strip_index must be default (-1) since it is "
         "no longer serialized";
  EXPECT_EQ(read.node_id, "tab-with-index");
}

// --- Stable identity: duplicate-URL correctness ------------------------------

TEST_F(SidebarStateModelContractTest,
       DuplicateUrlTabsGetDistinctIdentityInTree) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode tab_a;
  tab_a.kind = SidebarNodeKind::kTab;
  tab_a.tab_id = "dup-url-tab-a";
  tab_a.tab_strip_index = 0;
  tab_a.title = u"Page A";
  tab_a.host = u"example.com";

  SidebarTreeNode tab_b;
  tab_b.kind = SidebarNodeKind::kTab;
  tab_b.tab_id = "dup-url-tab-b";
  tab_b.tab_strip_index = 1;
  tab_b.title = u"Page B";
  tab_b.host = u"example.com";

  model.normal_tree.push_back(tab_a);
  model.normal_tree.push_back(tab_b);
  list.Update(model, /*browser=*/nullptr);

  auto* row_a = list.FindTabRowByIdForTesting("dup-url-tab-a");
  auto* row_b = list.FindTabRowByIdForTesting("dup-url-tab-b");
  ASSERT_TRUE(row_a) << "Tab A must be findable by its stable tab_id";
  ASSERT_TRUE(row_b) << "Tab B must be findable by its stable tab_id";
  EXPECT_NE(row_a, row_b)
      << "Two tabs with the same URL must produce distinct rows "
         "identified by tab_id, not URL";
}

TEST_F(SidebarStateModelContractTest,
       DuplicateUrlTabsDragPayloadsCarryDistinctIds) {
  SidebarDragPayload payload_a = MakeTabPayload(
      "dup-url-tab-a", SidebarDragOrigin::kNormalSection);
  SidebarDragPayload payload_b = MakeTabPayload(
      "dup-url-tab-b", SidebarDragOrigin::kNormalSection);

  ui::OSExchangeData data_a;
  WriteMahoDragData(payload_a, &data_a);
  ui::OSExchangeData data_b;
  WriteMahoDragData(payload_b, &data_b);

  SidebarDragPayload read_a, read_b;
  ASSERT_TRUE(ReadMahoDragData(data_a, read_a));
  ASSERT_TRUE(ReadMahoDragData(data_b, read_b));

  EXPECT_EQ(read_a.node_id, "dup-url-tab-a");
  EXPECT_EQ(read_b.node_id, "dup-url-tab-b");
  EXPECT_NE(read_a.node_id, read_b.node_id)
      << "Drag payloads for tabs with duplicate URLs must carry distinct "
         "stable IDs, not be conflated by URL";
}

// ---------------------------------------------------------------------------
// Favorites / state-model edge cases
// ---------------------------------------------------------------------------

// (a) Same-index favorite reorder dispatch contract.
//
// Dragging a favorite that resolves to its own current slot still dispatches
// reorder_favorite.  The grid does not suppress same-index drops; idempotency
// is the backend's responsibility.  This test verifies the payload carries the
// correct new_index value (0 for the first slot) and the kind is correct.
TEST_F(SidebarDnDDispatchTest,
       SameIndexFavoriteReorderStillDispatchesReorderFavorite) {
  MahoSidebarFavoritesGridView favorites(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;

  MahoSidebarFavoriteItemModel first;
  first.tab_id = "fav-a";
  first.url = GURL("https://alpha.example");
  first.title = u"Alpha";
  model.items.push_back(first);

  MahoSidebarFavoriteItemModel second;
  second.tab_id = "fav-b";
  second.url = GURL("https://beta.example");
  second.title = u"Beta";
  model.items.push_back(second);

  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 80));
  favorites.Update(model);

  // Drag fav-a (currently at index 0) and drop it back on the first slot
  // (point(5, 5) → top-left of grid → index 0).
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-a", SidebarDragOrigin::kFavorites), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  EXPECT_EQ(favorites.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));

  auto callback = favorites.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());
  favorites.OnDragExited();

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u)
      << "Same-index drop must still emit exactly one reorder_favorite event";
  EXPECT_EQ(observer_.events[0].kind, "reorder_favorite");
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-a\""),
            std::string::npos)
      << "reorder_favorite payload must carry the dragged tab_id";
  EXPECT_NE(observer_.events[0].json.find("\"new_index\":0"),
            std::string::npos)
      << "reorder_favorite new_index must be 0 for a drop on the first slot";

  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  ASSERT_TRUE(dict.Find("tab_id") != nullptr);
  ASSERT_TRUE(dict.Find("new_index") != nullptr);
}

// (b) SetFavoritesModel replacement / no-leak contract.
//
// Calling SetFavoritesModel a second time with a different model must replace
// the cached model entirely — old items must not appear in the next
// BuildViewStateModel call, and no new items must be silently appended.
TEST_F(SidebarStateModelContractTest,
       SetFavoritesModelReplacementErasesOldItems) {
  MahoSidebarStateAdapter adapter;

  MahoSidebarFavoritesModel first_model;
  MahoSidebarFavoriteItemModel item_x;
  item_x.title = u"X";
  item_x.url = GURL("https://x.example");
  item_x.tab_id = "fav-x";
  first_model.items.push_back(item_x);
  MahoSidebarFavoriteItemModel item_y;
  item_y.title = u"Y";
  item_y.url = GURL("https://y.example");
  item_y.tab_id = "fav-y";
  first_model.items.push_back(item_y);
  adapter.SetFavoritesModel(first_model);

  {
    MahoSidebarViewStateModel after_first = adapter.BuildViewStateModel(nullptr);
    ASSERT_EQ(after_first.favorites.items.size(), 2u)
        << "BuildViewStateModel must reflect the first SetFavoritesModel call";
    EXPECT_EQ(after_first.favorites.items[0].tab_id, "fav-x");
    EXPECT_EQ(after_first.favorites.items[1].tab_id, "fav-y");
  }

  MahoSidebarFavoritesModel second_model;
  MahoSidebarFavoriteItemModel item_z;
  item_z.title = u"Z";
  item_z.url = GURL("https://z.example");
  item_z.tab_id = "fav-z";
  second_model.items.push_back(item_z);
  adapter.SetFavoritesModel(second_model);

  MahoSidebarViewStateModel after_second = adapter.BuildViewStateModel(nullptr);
  ASSERT_EQ(after_second.favorites.items.size(), 1u)
      << "After replacing the favorites model, only the new item must appear "
         "(old items must not leak)";
  EXPECT_EQ(after_second.favorites.items[0].tab_id, "fav-z")
      << "The surviving item must be from the replacement model, not the old one";

  for (const auto& item : after_second.favorites.items) {
    EXPECT_NE(item.tab_id, "fav-x")
        << "fav-x from the old model must not survive a SetFavoritesModel replacement";
    EXPECT_NE(item.tab_id, "fav-y")
        << "fav-y from the old model must not survive a SetFavoritesModel replacement";
  }
}

// (b-bis) Replacing with an empty model clears all favorites.
TEST_F(SidebarStateModelContractTest,
       SetFavoritesModelWithEmptyModelClearsAllItems) {
  MahoSidebarStateAdapter adapter;

  MahoSidebarFavoritesModel populated;
  MahoSidebarFavoriteItemModel item;
  item.title = u"ToBeCleared";
  item.url = GURL("https://clear.example");
  item.tab_id = "fav-clear";
  populated.items.push_back(item);
  adapter.SetFavoritesModel(populated);

  ASSERT_EQ(adapter.BuildViewStateModel(nullptr).favorites.items.size(), 1u)
      << "Precondition: one item must be present before clearing";

  adapter.SetFavoritesModel(MahoSidebarFavoritesModel{});

  MahoSidebarViewStateModel cleared = adapter.BuildViewStateModel(nullptr);
  EXPECT_TRUE(cleared.favorites.items.empty())
      << "Setting an empty favorites model must clear all previously set items";
  EXPECT_EQ(cleared.favorites.slot_count,
            sidebar_layout::kMahoSidebarFavoriteSlotCount)
      << "slot_count must remain equal to the layout constant after clearing";
}

// (c) Unfavorite dispatch when a favorite drag completes outside the grid.
//
// NOTE ON BLOCKER: The MahoSidebarFavoritesGridView::OnTileDragDone path
// dispatches "unfavorite_tab" only when
//   !GetBoundsInScreen().Contains(display::Screen::Get()->GetCursorScreenPoint())
//   && !drag_drop_accepted_
// Both conditions depend on the real screen cursor position and the view's
// on-screen bounds, which are not reliably controllable in a ViewsTestBase
// unit-test harness.  Injecting a synthetic cursor position or calling
// SetBoundsInScreen is possible only with a full interactive-test environment
// (see maho_sidebar_edge_cases_interactive_test.cc for the live version).
//
// The reliably testable path for unfavorite dispatch is the SectionDropTarget
// callback: when a kFavorites-origin tab drag is dropped on the normal section
// target, "unfavorite_tab" is dispatched unconditionally before the section
// logic runs.  This path exercises the same DispatchShellEvent("unfavorite_tab")
// contract and is stable under all test configurations.
TEST_F(SidebarDnDDispatchTest,
       FavoriteDragDropOnNormalSectionDispatchesUnfavoriteTab) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-drop", SidebarDragOrigin::kFavorites),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kNormal, event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);

  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role")
      << "change_tab_role must be dispatched when a kFavorites drag completes "
         "on the normal section target";
  SCOPED_TRACE(observer_.events[0].json);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-drop\""),
            std::string::npos)
      << "change_tab_role payload must carry the tab_id of the dragged favorite";
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"normal\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);
}

// Verify the role-transition contract when a kFavorites drag drops on the
// pinned section — under ADR 11 the tab stays alive and its role transitions
// atomically from Favorite to Pinned via a single "change_tab_role" event
// (no unfavorite_tab + pin_tab sequence).
TEST_F(SidebarDnDDispatchTest,
       FavoriteDragDropOnPinnedSectionDispatchesChangeTabRoleToPinned) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-pin", SidebarDragOrigin::kFavorites), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kPinned, event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role")
      << "change_tab_role must be dispatched when a kFavorites drag completes "
         "on the pinned section target";
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-pin\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"pinned\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);
}

// ---------------------------------------------------------------------------
// Cross-surface DnD regression tests — cases (a)–(f)
//
// These lock the corrected dispatch behavior after routing section/favorites
// drop handlers through the canonical drop planner.
// ---------------------------------------------------------------------------

// (a) Pinned folder-child tab dropped on the normal section escapes once, then
//     unpins without a duplicate append event.
TEST_F(SidebarDnDDispatchTest,
       PinnedFolderChildDropToNormalSectionDispatchesMoveToRootThenUnpin) {
  SidebarDragPayload payload = MakeTabPayload(
      "pinned-child", SidebarDragOrigin::kPinnedSection);
  payload.source_parent_folder_id = "pinned-folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kNormal, event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"pinned-folder-1\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "unpin_tab");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"pinned-child\""),
            std::string::npos);
  for (const auto& event_record : observer_.events) {
    EXPECT_NE(event_record.kind, "reorder_root_item");
  }
}

// (b) Pinned root tab dropped on the favorites grid must produce
//     [favorite_tab, reorder_favorite] — not unpin_tab.
TEST_F(SidebarDnDDispatchTest,
       PinnedTabDropToFavoritesGridDispatchesFavoriteAndReorder) {
  MahoSidebarFavoritesGridView favorites(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 80));
  favorites.Update(model);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("pinned-tab", SidebarDragOrigin::kPinnedSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = favorites.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "favorite_tab");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_favorite");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"new_index\":0"),
            std::string::npos);
}

// (c) Normal folder-child tab dropped on the favorites grid must produce
//     [move_tab_to_root, favorite_tab, reorder_favorite].
TEST_F(SidebarDnDDispatchTest,
       NormalFolderChildDropToFavoritesGridDispatchesMoveToRootThenFavorite) {
  MahoSidebarFavoritesGridView favorites(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 80));
  favorites.Update(model);

  SidebarDragPayload payload = MakeFolderChildTabPayload(
      "child-tab", "folder-1", 0);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = favorites.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 3u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"folder-1\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "favorite_tab");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"child-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[2].kind, "reorder_favorite");
  EXPECT_NE(observer_.events[2].json.find("\"tab_id\":\"child-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[2].json.find("\"new_index\":0"),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       NormalRootTabDropToEmptyFavoritesGridDispatchesFavoriteThenPlacement) {
  MahoSidebarFavoritesGridView favorites(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 80));
  favorites.Update(model);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("root-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  EXPECT_EQ(favorites.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  auto callback = favorites.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "favorite_tab");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"root-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_favorite");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"root-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"new_index\":0"),
            std::string::npos)
      << "Empty favorites drops must still place the newly favorited tab at "
         "index 0";
}

TEST_F(SidebarDnDDispatchTest,
       NormalRootTabDropToFavoritesAppendDispatchesFavoriteThenAppendPlacement) {
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
  favorites.DeprecatedLayoutImmediately();

  views::Button* last_tile = favorites.GetTileForTesting(1);
  ASSERT_TRUE(last_tile);
  gfx::Point append_point = last_tile->bounds().CenterPoint();
  views::View::ConvertPointToTarget(last_tile->parent(), &favorites,
                                    &append_point);
  append_point.Offset(sidebar_layout::kFavoriteTileSpacingDp +
                          (last_tile->width() / 2),
                      0);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("root-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data, append_point);

  EXPECT_EQ(favorites.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  auto callback = favorites.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "favorite_tab");
  EXPECT_EQ(observer_.events[1].kind, "reorder_favorite");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"root-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"new_index\":2"),
            std::string::npos)
      << "Append drops must still emit reorder_favorite with the resolved "
         "append index";
}

// (d) Pinned folder-child tab dropped on the favorites grid must produce
//     [move_tab_to_root, favorite_tab, reorder_favorite] (exactly 3 steps).
TEST_F(SidebarDnDDispatchTest,
       PinnedFolderChildDropToFavoritesGridDispatchesThreeSteps) {
  MahoSidebarFavoritesGridView favorites(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;
  favorites.SetBoundsRect(gfx::Rect(0, 0, 220, 80));
  favorites.Update(model);

  SidebarDragPayload payload = MakeTabPayload(
      "pinned-child", SidebarDragOrigin::kPinnedSection);
  payload.source_parent_folder_id = "pinned-folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));
  auto callback = favorites.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 3u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"pinned-folder-1\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "favorite_tab");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"pinned-child\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[2].kind, "reorder_favorite");
  EXPECT_NE(observer_.events[2].json.find("\"tab_id\":\"pinned-child\""),
            std::string::npos);
}

// (e) Favorites root tab dropped on the normal section changes role, then
//     appends without a spurious unpin transition.
TEST_F(SidebarDnDDispatchTest,
       FavoritesRootTabDropToNormalSectionChangesRoleThenAppends) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kNormal, event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role");
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"normal\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[0].json.find("\"unpin_tab\""), std::string::npos)
      << "No spurious unpin_tab must be emitted for a favorites root tab drop";
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);
}

// (f) Normal root tab dropped on the normal section background appends without
//     a role transition.
TEST_F(SidebarDnDDispatchTest,
       NormalRootTabDropOnNormalSectionContainerAppends) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kNormal, event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[0].json.find("\"kind\":\"append\""),
            std::string::npos);
}

// ---------------------------------------------------------------------------
// Insertion-lane favorites accepted-callback tests (review blocker 1).
// ---------------------------------------------------------------------------

// A favorites-origin tab dropped on a tab-target insertion lane must invoke
// the accepted callback exactly once with the dragged tab's ID.
TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnInsertionLaneInvokesAcceptedCallback) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  int callback_count = 0;
  std::string last_accepted_id;
  list.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, std::string* last,
                             const std::string& tab_id) {
        ++(*count);
        *last = tab_id;
      }, &callback_count, &last_accepted_id));
  // Re-run Update so lanes pick up the callback.
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  EXPECT_EQ(callback_count, 1)
      << "Accepted callback must fire exactly once for a favorites-origin tab "
         "dropped on a tab-target insertion lane";
  EXPECT_EQ(last_accepted_id, "fav-tab");
  bool found_unfavorite = false;
  for (const auto& ev : observer_.events) {
    if (ev.kind == "change_tab_role") {
      found_unfavorite = true;
      EXPECT_NE(ev.json.find("\"tab_id\":\"fav-tab\""), std::string::npos);
    }
  }
  EXPECT_TRUE(found_unfavorite)
      << "change_tab_role must be dispatched for a favorites-origin tab lane drop";
}

// A favorites-origin tab dropped on a folder-target insertion lane must invoke
// the accepted callback exactly once.
TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnFolderInsertionLaneInvokesAcceptedCallback) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeFolderNode("target-folder"));
  list.Update(model, /*browser=*/nullptr);

  int callback_count = 0;
  std::string last_accepted_id;
  list.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, std::string* last,
                             const std::string& tab_id) {
        ++(*count);
        *last = tab_id;
      }, &callback_count, &last_accepted_id));
  // Re-run Update so lanes pick up the callback.
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  EXPECT_EQ(callback_count, 1)
      << "Accepted callback must fire exactly once for a favorites-origin tab "
         "dropped on a folder-target insertion lane";
  EXPECT_EQ(last_accepted_id, "fav-tab");
  bool found_unfavorite = false;
  for (const auto& ev : observer_.events) {
    if (ev.kind == "change_tab_role") {
      found_unfavorite = true;
    }
  }
  EXPECT_TRUE(found_unfavorite)
      << "change_tab_role must be dispatched for a favorites-origin folder lane drop";
}

// A non-favorites tab dropped on a tab-target insertion lane must NOT invoke
// the accepted callback.
TEST_F(SidebarDnDDispatchTest,
       NonFavoriteTabDropOnInsertionLaneDoesNotInvokeAcceptedCallback) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("target-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  int callback_count = 0;
  list.SetFavoritesDropAcceptedCallback(
      base::BindRepeating([](int* count, const std::string&) {
        ++(*count);
      }, &callback_count));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeTabByIdForTesting("target-tab");
  ASSERT_TRUE(lane);

  SidebarDragPayload payload =
      MakeTabPayload("normal-tab", SidebarDragOrigin::kNormalSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  EXPECT_EQ(callback_count, 0)
      << "Accepted callback must NOT fire for a non-favorites-origin tab "
         "dropped on an insertion lane";
  for (const auto& ev : observer_.events) {
    EXPECT_NE(ev.kind, "change_tab_role");
  }
}

// ---------------------------------------------------------------------------
// Cross-pin folder-row dispatch tests (review blocker 4).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Row-body section-routing regressions. Cross-section body drops are positional
// moves before the target row; same-section body drops remain split targets.
// ---------------------------------------------------------------------------

TEST_F(
    SidebarDnDDispatchTest,
    PinnedFolderChildDropBeforeNormalTabRowBodyDispatchesMoveToRootThenUnpin) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return 1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload = MakeTabPayload(
      "pinned-child", SidebarDragOrigin::kPinnedSection);
  payload.source_parent_folder_id = "pinned-folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 2));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 3u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"pinned-folder-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-child\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[0].json.find("\"before_tab_id\":"),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "unpin_tab");
  EXPECT_NE(observer_.events[1].json.find("\"tab_id\":\"pinned-child\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[2].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[2].json.find("\"id\":\"pinned-child\""),
            std::string::npos);
  EXPECT_NE(observer_.events[2].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[2].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(
    SidebarDnDDispatchTest,
    NormalFolderChildDropBeforePinnedTabRowBodyDispatchesEscapePinThenReorder) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 0);
  target_node.is_pinned = true;
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kPinned, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return 0; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload =
      MakeTabPayload("normal-child", SidebarDragOrigin::kNormalSection);
  payload.source_parent_folder_id = "normal-folder-1";
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 3u);
  EXPECT_EQ(observer_.events[0].kind, "move_tab_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"normal-folder-1\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[0].json.find("\"before_tab_id\":"),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "pin_tab");
  EXPECT_EQ(observer_.events[2].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[2].json.find("\"id\":\"normal-child\""),
            std::string::npos);
  EXPECT_NE(observer_.events[2].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[2].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       NormalTabDropOnPinnedRowBodyPinsThenReordersBeforeTarget) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 0);
  target_node.is_pinned = true;
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kPinned, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return 0; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(
      MakeTabPayload("normal-tab", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"normal-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"normal-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       PinnedTabDropOnNormalRowBodyUnpinsThenReordersBeforeTarget) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return 1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload =
      MakeTabPayload("pinned-tab", SidebarDragOrigin::kPinnedSection);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "unpin_tab")
      << "Pinned→normal row-body drop must dispatch unpin_tab";
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"pinned-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"before\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"id\":\"target-tab\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest, SameSectionTabRowBodyRemainsSplit) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return 1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  ui::OSExchangeData data;
  WriteDropData(
      MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kSplit);
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnNormalRowBodyChangesRoleThenReordersBeforeTarget) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 1);
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kNormal, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return 1; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role")
      << "Favorites→normal row-body drop must dispatch change_tab_role";
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"normal\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"before\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTabDropOnPinnedRowBodyChangesRoleThenReordersBeforeTarget) {
  SidebarTreeNode target_node = MakeTabNode("target-tab", 0);
  target_node.is_pinned = true;
  SidebarTabRowView row(
      target_node, ui::ImageModel(), /*browser=*/nullptr,
      MahoSidebarTabSection::kPinned, "space-1", u"Target",
      NoOpPressedCallback(), NoOpPressedCallback(), NoOpPressedCallback(),
      base::DoNothing(),
      base::BindRepeating([](const std::string&) { return 0; }),
      base::DoNothing(), base::DoNothing());
  row.SetBoundsRect(gfx::Rect(0, 0, 220, 40));

  SidebarDragPayload payload =
      MakeTabPayload("fav-tab", SidebarDragOrigin::kFavorites);
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 20));

  EXPECT_EQ(row.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(row.drop_zone_for_testing(),
            SidebarTabRowView::SidebarTabDropZone::kBefore);

  auto callback = row.GetDropCallbackForTesting(event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "change_tab_role");
  EXPECT_NE(observer_.events[0].json.find("\"tab_id\":\"fav-tab\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"type\":\"pinned\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"before\""),
            std::string::npos);
}

// (4) Empty pinned section must be revealed when a favorites-origin drag
//     enters the list view, so that a favorites→pinned drop target is reachable.
TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedLaneRevealedForFavoritesDrag) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("fav-1", SidebarDragOrigin::kFavorites), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  list.OnDragUpdated(event);

  EXPECT_TRUE(list.is_pinned_drop_lane_revealed_for_testing())
      << "Empty pinned section must be revealed for favorites-origin drags so "
         "the favorites→pinned transition target is reachable";
  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  EXPECT_TRUE(pinned_target->GetVisible())
      << "Pinned section drop target must be visible during a favorites drag";

  list.OnDragExited();

  EXPECT_FALSE(list.is_pinned_drop_lane_revealed_for_testing())
      << "Revealed pinned lane must hide again on drag exit";
  EXPECT_FALSE(pinned_target->GetVisible());
}

TEST_F(SidebarDnDDispatchTest,
       PopulatedPinnedSectionAcceptsWholeSectionDropIndicator) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("t1", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto state = GetSectionDropIndicatorStateForTesting(
      MahoSidebarTabSection::kPinned, event);

  EXPECT_TRUE(state.visible_after_update);
  EXPECT_FALSE(state.visible_after_exit);
}

TEST_F(SidebarDnDDispatchTest,
       PopulatedNormalSectionFallbackUnpinsThenAppends) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("t1", SidebarDragOrigin::kPinnedSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kNormal, event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "unpin_tab");
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       EmptyPinnedSectionAcceptsWholeSectionDrop) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("t1", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto state = GetSectionDropIndicatorStateForTesting(
      MahoSidebarTabSection::kPinned, event);
  EXPECT_TRUE(state.visible_after_update);
  EXPECT_FALSE(state.visible_after_exit);

  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kPinned, event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       MixedSectionsBothExposeSectionBackgroundFallback) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("normal-tab", 0));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("src", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  list.OnDragUpdated(event);

  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);

  auto* normal_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_TRUE(normal_target);

  EXPECT_EQ(pinned_target->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove))
      << "Empty pinned section must accept the drag";

  EXPECT_EQ(normal_target->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove))
      << "Populated normal section must remain a background fallback";

  auto normal_callback = normal_target->GetDropCallback(event);
  ASSERT_TRUE(normal_callback);
  ui::mojom::DragOperation normal_output = ui::mojom::DragOperation::kNone;
  std::move(normal_callback).Run(event, normal_output,
                                 std::unique_ptr<ui::LayerTreeOwner>());
  EXPECT_EQ(normal_output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_root_item");
}

TEST_F(SidebarDnDDispatchTest,
       MixedSectionsPopulatedPinnedSidePinsThenAppends) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode pinned = MakeTabNode("pinned-tab", 0);
  pinned.is_pinned = true;
  model.pinned_tree.push_back(pinned);
  model.normal_tree.push_back(MakeTabNode("normal-tab", 1));
  list.Update(model, /*browser=*/nullptr);

  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("src", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target)
      << "Populated pinned section must still expose a drop target";

  EXPECT_EQ(pinned_target->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove))
      << "Populated pinned section must accept background fallback drops";

  auto callback = pinned_target->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output,
                          std::unique_ptr<ui::LayerTreeOwner>());
  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
  EXPECT_NE(observer_.events[1].json.find("\"kind\":\"append\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       NestedFolderDropOnPopulatedPinnedSectionEscapesThenPinsOnce) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode pinned = MakeTabNode("pinned-tab", 0);
  pinned.is_pinned = true;
  model.pinned_tree.push_back(pinned);
  list.Update(model, /*browser=*/nullptr);

  SidebarDragPayload payload =
      MakeNestedFolderPayload("nested-folder", "parent-folder");
  ui::OSExchangeData data;
  WriteDropData(payload, &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  views::View* pinned_target =
      list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kPinned);
  ASSERT_TRUE(pinned_target);
  auto callback = pinned_target->GetDropCallback(event);
  ASSERT_TRUE(callback);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "move_folder_to_root");
  EXPECT_NE(observer_.events[0].json.find("\"space_id\":\"space-1\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"nested-folder\""),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "set_folder_pinned");
  EXPECT_NE(observer_.events[1].json.find("\"folder_id\":\"nested-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[1].json.find("\"is_pinned\":true"),
            std::string::npos);
  for (const auto& event_record : observer_.events) {
    EXPECT_NE(event_record.kind, "reorder_root_item");
  }
}

TEST_F(SidebarDnDDispatchTest, FavoriteStaleTransformsClearedOnUpdate) {
  MahoSidebarFavoritesGridView grid(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;
  
  MahoSidebarFavoriteItemModel item1;
  item1.tab_id = "tab-1";
  item1.url = GURL("https://example.com/1");
  item1.title = u"Title 1";
  model.items.push_back(item1);

  MahoSidebarFavoriteItemModel item2;
  item2.tab_id = "tab-2";
  item2.url = GURL("https://example.com/2");
  item2.title = u"Title 2";
  model.items.push_back(item2);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  views::Button* tile = grid.GetTileForTesting(0);
  ASSERT_TRUE(tile);
  ASSERT_TRUE(tile->layer());

  tile->layer()->SetTransform(gfx::Transform::MakeTranslation(15, 25));
  tile->layer()->SetOpacity(0.5f);

  grid.Update(model);

  EXPECT_TRUE(tile->layer()->GetTargetTransform().IsIdentity());
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 1.0f);
}

TEST_F(SidebarDnDDispatchTest, FavoriteDragPreviewRestoredOnAsyncUpdate) {
  MahoSidebarFavoritesGridView grid(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;

  MahoSidebarFavoriteItemModel item1;
  item1.tab_id = "tab-1";
  item1.url = GURL("https://example.com/1");
  item1.title = u"Title 1";
  model.items.push_back(item1);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  grid.OnDragStarted("tab-1");

  grid.Update(model);

  views::Button* tile = grid.GetTileForTesting(0);
  ASSERT_TRUE(tile);
  ASSERT_TRUE(tile->layer());
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 0.f);

  grid.OnDragEnded();
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 1.0f);
  EXPECT_TRUE(tile->layer()->GetTargetTransform().IsIdentity());
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteStaleFaviconCallbackDoesNotOverwriteReusedSlot) {
  constexpr char kSharedUrl[] = "https://example.com/shared";
  MahoSidebarFavoritesGridView grid(nullptr);
  MahoSidebarFavoritesModel model;

  MahoSidebarFavoriteItemModel item;
  item.tab_id = "tab-a";
  item.url = GURL(kSharedUrl);
  item.title = u"Tab A";
  model.items.push_back(item);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  const uint64_t tab_a_generation = grid.favicon_generation_for_testing(0);
  favicon_base::FaviconImageResult red_result = MakeFaviconResult(SK_ColorRED);
  ui::ImageModel red_icon = ui::ImageModel::FromImage(red_result.image);
  grid.RunFaviconLoadedForTesting(0, "tab-a", GURL(kSharedUrl),
                                  tab_a_generation, red_result);
  EXPECT_EQ(red_icon, grid.cached_favicon_for_tab_id_for_testing("tab-a"));
  EXPECT_TRUE(grid.cached_favicon_for_tab_id_for_testing("tab-b").IsEmpty());

  model.items[0].tab_id = "tab-b";
  model.items[0].title = u"Tab B";
  grid.Update(model);
  const ui::ImageModel tab_b_before = grid.tile_favicon_for_testing(0);

  favicon_base::FaviconImageResult blue_result = MakeFaviconResult(SK_ColorBLUE);
  grid.RunFaviconLoadedForTesting(0, "tab-a", GURL(kSharedUrl),
                                  tab_a_generation, blue_result);

  EXPECT_EQ(tab_b_before, grid.tile_favicon_for_testing(0));
  EXPECT_TRUE(grid.cached_favicon_for_tab_id_for_testing("tab-b").IsEmpty());
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteRepeatedUpdatePreservesCachedFavicon) {
  constexpr char kUrl[] = "https://example.com/favorite";
  MahoSidebarFavoritesGridView grid(nullptr);
  MahoSidebarFavoritesModel model;
  MahoSidebarFavoriteItemModel item;
  item.tab_id = "tab-a";
  item.url = GURL(kUrl);
  item.title = u"Tab A";
  model.items.push_back(item);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  const uint64_t generation = grid.favicon_generation_for_testing(0);
  favicon_base::FaviconImageResult red_result = MakeFaviconResult(SK_ColorRED);
  const ui::ImageModel red_icon = ui::ImageModel::FromImage(red_result.image);
  grid.RunFaviconLoadedForTesting(0, "tab-a", GURL(kUrl), generation,
                                  red_result);
  ASSERT_EQ(red_icon, grid.tile_favicon_for_testing(0));
  ASSERT_EQ(red_icon, grid.cached_favicon_for_tab_id_for_testing("tab-a"));

  grid.Update(model);

  EXPECT_EQ(red_icon, grid.tile_favicon_for_testing(0));
  EXPECT_EQ(red_icon, grid.cached_favicon_for_tab_id_for_testing("tab-a"));
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteUrlChangeSameTabIdKeepsCachedFaviconUntilFreshResult) {
  constexpr char kOldUrl[] = "https://example.com/old";
  constexpr char kNewUrl[] = "https://example.com/new";
  MahoSidebarFavoritesGridView grid(nullptr);
  MahoSidebarFavoritesModel model;
  MahoSidebarFavoriteItemModel item;
  item.tab_id = "tab-a";
  item.url = GURL(kOldUrl);
  item.title = u"Tab A";
  model.items.push_back(item);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  const uint64_t old_generation = grid.favicon_generation_for_testing(0);
  favicon_base::FaviconImageResult red_result = MakeFaviconResult(SK_ColorRED);
  const ui::ImageModel red_icon = ui::ImageModel::FromImage(red_result.image);
  grid.RunFaviconLoadedForTesting(0, "tab-a", GURL(kOldUrl), old_generation,
                                  red_result);
  ASSERT_EQ(red_icon, grid.tile_favicon_for_testing(0));

  model.items[0].url = GURL(kNewUrl);
  grid.Update(model);
  const uint64_t new_generation = grid.favicon_generation_for_testing(0);

  EXPECT_EQ(red_icon, grid.tile_favicon_for_testing(0));
  EXPECT_NE(old_generation, new_generation);

  favicon_base::FaviconImageResult stale_result =
      MakeFaviconResult(SK_ColorBLUE);
  grid.RunFaviconLoadedForTesting(0, "tab-a", GURL(kOldUrl), old_generation,
                                  stale_result);
  EXPECT_EQ(red_icon, grid.tile_favicon_for_testing(0));

  favicon_base::FaviconImageResult green_result =
      MakeFaviconResult(SK_ColorGREEN);
  const ui::ImageModel green_icon =
      ui::ImageModel::FromImage(green_result.image);
  grid.RunFaviconLoadedForTesting(0, "tab-a", GURL(kNewUrl), new_generation,
                                  green_result);

  EXPECT_EQ(green_icon, grid.tile_favicon_for_testing(0));
  EXPECT_EQ(green_icon, grid.cached_favicon_for_tab_id_for_testing("tab-a"));
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteSameUrlDifferentTabIdsKeepSeparateFaviconCaches) {
  constexpr char kSharedUrl[] = "https://example.com/shared";
  MahoSidebarFavoritesGridView grid(nullptr);
  MahoSidebarFavoritesModel model;

  MahoSidebarFavoriteItemModel tab_a;
  tab_a.tab_id = "tab-a";
  tab_a.url = GURL(kSharedUrl);
  tab_a.title = u"Tab A";
  model.items.push_back(tab_a);

  MahoSidebarFavoriteItemModel tab_b;
  tab_b.tab_id = "tab-b";
  tab_b.url = GURL(kSharedUrl);
  tab_b.title = u"Tab B";
  model.items.push_back(tab_b);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  const uint64_t tab_a_generation = grid.favicon_generation_for_testing(0);
  const uint64_t tab_b_generation = grid.favicon_generation_for_testing(1);
  favicon_base::FaviconImageResult red_result = MakeFaviconResult(SK_ColorRED);
  favicon_base::FaviconImageResult blue_result =
      MakeFaviconResult(SK_ColorBLUE);
  const ui::ImageModel red_icon = ui::ImageModel::FromImage(red_result.image);
  const ui::ImageModel blue_icon = ui::ImageModel::FromImage(blue_result.image);

  grid.RunFaviconLoadedForTesting(0, "tab-a", GURL(kSharedUrl),
                                  tab_a_generation, red_result);
  grid.RunFaviconLoadedForTesting(1, "tab-b", GURL(kSharedUrl),
                                  tab_b_generation, blue_result);
  ASSERT_EQ(red_icon, grid.cached_favicon_for_tab_id_for_testing("tab-a"));
  ASSERT_EQ(blue_icon, grid.cached_favicon_for_tab_id_for_testing("tab-b"));

  std::swap(model.items[0], model.items[1]);
  grid.Update(model);

  EXPECT_EQ(blue_icon, grid.tile_favicon_for_testing(0));
  EXPECT_EQ(red_icon, grid.tile_favicon_for_testing(1));
  EXPECT_EQ(red_icon, grid.cached_favicon_for_tab_id_for_testing("tab-a"));
  EXPECT_EQ(blue_icon, grid.cached_favicon_for_tab_id_for_testing("tab-b"));
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteTwoRowStableUpdateDoesNotAnimateReorder) {
  MahoSidebarFavoritesGridView grid(nullptr);
  MahoSidebarFavoritesModel model = MakeFavoritesModelForTesting(5);
  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 140));

  grid.Update(model);
  grid.DeprecatedLayoutImmediately();
  ASSERT_FALSE(grid.tile_bounds_in_grid_for_testing(4).IsEmpty())
      << "Test setup must produce a laid-out second-row tile";

  grid.Update(model);

  EXPECT_FALSE(grid.last_update_had_reorder_animation_for_testing())
      << "Unchanged two-row favorites must compare old and new bounds in the "
         "same grid-local coordinate space, not row-local vs grid-local";
}

TEST_F(SidebarDnDDispatchTest,
       FavoriteInsertionAnimationUsesPostLayoutBoundsAfterFourToFive) {
  MahoSidebarFavoritesGridView grid(nullptr);
  MahoSidebarFavoritesModel model = MakeFavoritesModelForTesting(4);
  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 140));
  grid.Update(model);
  grid.DeprecatedLayoutImmediately();

  model = MakeFavoritesModelForTesting(5);
  grid.Update(model);

  const gfx::Rect insertion_bounds =
      grid.last_insertion_animation_bounds_for_testing();
  EXPECT_FALSE(insertion_bounds.IsEmpty())
      << "Insertion animation after 4->5 must read post-layout tile bounds";
  EXPECT_GT(insertion_bounds.width(), 0);
  EXPECT_GT(insertion_bounds.height(), 0);
  EXPECT_EQ(insertion_bounds, grid.tile_bounds_in_grid_for_testing(4));
}

TEST_F(SidebarDnDDispatchTest,
       TabRowStaleFaviconGenerationDoesNotOverwriteCurrentRow) {
  MahoSidebarTabListView list(nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode tab = MakeTabNode("tab-a", 0);
  tab.url = "https://example.com/old";
  tab.host = u"example.com";
  model.normal_tree.push_back(tab);

  list.Update(model, nullptr);
  SidebarTabRowView* row = list.FindTabRowByIdForTesting("tab-a");
  ASSERT_TRUE(row);
  const uint64_t old_generation =
      list.row_favicon_generation_for_testing("tab-a");

  favicon_base::FaviconImageResult red_result = MakeFaviconResult(SK_ColorRED);
  ui::ImageModel red_icon = ui::ImageModel::FromImage(red_result.image);
  list.RunFaviconLoadedForTesting("tab-a", GURL("https://example.com/old"),
                                  old_generation, red_result);
  EXPECT_EQ(red_icon, row->favicon_for_testing());

  model.normal_tree[0].url = "https://example.com/new";
  list.Update(model, nullptr);
  row = list.FindTabRowByIdForTesting("tab-a");
  ASSERT_TRUE(row);
  EXPECT_NE(old_generation, list.row_favicon_generation_for_testing("tab-a"));
  const ui::ImageModel current_icon = row->favicon_for_testing();

  favicon_base::FaviconImageResult blue_result = MakeFaviconResult(SK_ColorBLUE);
  list.RunFaviconLoadedForTesting("tab-a", GURL("https://example.com/old"),
                                  old_generation, blue_result);

  EXPECT_EQ(current_icon, row->favicon_for_testing());
}

TEST_F(SidebarDnDDispatchTest,
       TabRowBackgroundFaviconCompletionReplacesGlobeWithoutActivation) {
  MahoSidebarTabListView list(nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode tab = MakeTabNode("tab-a", 0);
  tab.url = "https://example.com/background";
  tab.host = u"example.com";
  model.normal_tree.push_back(tab);

  list.Update(model, nullptr);
  SidebarTabRowView* row = list.FindTabRowByIdForTesting("tab-a");
  ASSERT_TRUE(row);
  const ui::ImageModel before = row->favicon_for_testing();
  const uint64_t generation =
      list.row_favicon_generation_for_testing("tab-a");

  favicon_base::FaviconImageResult red_result = MakeFaviconResult(SK_ColorRED);
  ui::ImageModel red_icon = ui::ImageModel::FromImage(red_result.image);
  list.RunFaviconLoadedForTesting("tab-a", GURL("https://example.com/background"),
                                  generation, red_result);

  EXPECT_EQ(red_icon, row->favicon_for_testing());
  EXPECT_FALSE(before == row->favicon_for_testing())
      << "Async favicon completion must replace the initial globe without tab activation";
}

TEST_F(SidebarDnDDispatchTest,
       TabRowFaviconCallbackRequiresTrackedRowLookup) {
  MahoSidebarTabListView list(nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  SidebarTreeNode tab = MakeTabNode("tab-a", 0);
  tab.url = "https://example.com/o1";
  tab.host = u"example.com";
  model.normal_tree.push_back(tab);

  list.Update(model, nullptr);
  SidebarTabRowView* row = list.FindTabRowByIdForTesting("tab-a");
  ASSERT_TRUE(row);
  const ui::ImageModel before = row->favicon_for_testing();
  const uint64_t generation =
      list.row_favicon_generation_for_testing("tab-a");

  list.ForgetTabRowForTesting("tab-a");
  favicon_base::FaviconImageResult red_result = MakeFaviconResult(SK_ColorRED);
  list.RunFaviconLoadedForTesting("tab-a", GURL("https://example.com/o1"),
                                  generation, red_result);

  EXPECT_EQ(before, row->favicon_for_testing())
      << "OnFaviconLoaded must use the O(1) tracked-row map, not DFS over tab_rows_";
}

TEST_F(SidebarDnDDispatchTest, FavoriteDragEndedCancelsAnimations) {
  MahoSidebarFavoritesGridView grid(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;

  MahoSidebarFavoriteItemModel item1;
  item1.tab_id = "tab-1";
  item1.url = GURL("https://example.com/1");
  item1.title = u"Title 1";
  model.items.push_back(item1);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  views::Button* tile = grid.GetTileForTesting(0);
  ASSERT_TRUE(tile);
  ASSERT_TRUE(tile->layer());

  tile->layer()->SetTransform(gfx::Transform::MakeTranslation(5, 10));
  tile->layer()->SetOpacity(0.8f);

  grid.OnDragEnded();

  EXPECT_TRUE(tile->layer()->GetTargetTransform().IsIdentity());
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 1.0f);
}

TEST_F(SidebarDnDDispatchTest, FavoriteExternalDragExitedRestoresOpacity) {
  MahoSidebarFavoritesGridView grid(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;

  MahoSidebarFavoriteItemModel item1;
  item1.tab_id = "tab-1";
  item1.url = GURL("https://example.com/1");
  item1.title = u"Title 1";
  model.items.push_back(item1);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  views::Button* tile = grid.GetTileForTesting(0);
  ASSERT_TRUE(tile);
  ASSERT_TRUE(tile->layer());

  // Simulate an external drag enter (active_drag_tab_id_ set, drag_over_drop_target_ true)
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  EXPECT_EQ(grid.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));

  // The matching tile is hidden via opacity 0.f when async update comes
  grid.Update(model);
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 0.f);

  // Drag exits the grid view
  grid.OnDragExited();

  // Opacity should be restored to 1.0f and transform to identity
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 1.0f);
  EXPECT_TRUE(tile->layer()->GetTargetTransform().IsIdentity());
}

TEST_F(SidebarDnDDispatchTest, FavoriteExternalDragEndedRestoresOpacity) {
  MahoSidebarFavoritesGridView grid(/*browser=*/nullptr);
  MahoSidebarFavoritesModel model;

  MahoSidebarFavoriteItemModel item1;
  item1.tab_id = "tab-1";
  item1.url = GURL("https://example.com/1");
  item1.title = u"Title 1";
  model.items.push_back(item1);

  grid.SetBoundsRect(gfx::Rect(0, 0, 220, 100));
  grid.Update(model);

  views::Button* tile = grid.GetTileForTesting(0);
  ASSERT_TRUE(tile);
  ASSERT_TRUE(tile->layer());

  // Simulate an external drag enter (active_drag_tab_id_ set, drag_over_drop_target_ true)
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection), &data);
  ui::DropTargetEvent event = MakeDropEvent(data, gfx::Point(5, 5));

  EXPECT_EQ(grid.OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));

  // The matching tile is hidden via opacity 0.f when async update comes
  grid.Update(model);
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 0.f);

  // Drag ends (session finished/cancelled) while still over the grid
  grid.OnDragEnded();

  // Opacity should be restored to 1.0f and transform to identity
  EXPECT_EQ(tile->layer()->GetTargetOpacity(), 1.0f);
  EXPECT_TRUE(tile->layer()->GetTargetTransform().IsIdentity());
}

// ---------------------------------------------------------------------------
// Pinned/normal separator visual structure regression test.
//
// The separator row between the pinned and normal sections was previously drawn
// via OnPaint().  The fix replaced manual painting with an internal child
// divider view that has a solid background.  This test locks the structural
// contract: after Update() with at least one pinned tab and one normal tab,
// the separator must be non-null, visible, and contain exactly one child view
// (the divider).  A future regression that removes the child or hides the
// separator would fail here before it reaches any paint/pixel path.
// ---------------------------------------------------------------------------

TEST_F(SidebarDnDDispatchTest,
       PinnedSeparatorExistsAndHasChildDividerAfterPopulatedUpdate) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode pinned = MakeTabNode("pinned-tab", 0);
  pinned.is_pinned = true;
  model.pinned_tree.push_back(pinned);
  model.normal_tree.push_back(MakeTabNode("normal-tab", 1));

  list.Update(model, /*browser=*/nullptr);

  views::View* separator = list.pinned_separator_for_testing();
  ASSERT_NE(separator, nullptr)
      << "pinned_separator must be non-null after Update() with both pinned "
         "and normal tabs";

  EXPECT_TRUE(separator->GetVisible())
      << "pinned_separator must be visible in a populated sidebar model";

  ASSERT_EQ(separator->children().size(), 1u)
      << "SidebarSeparatorRow must contain exactly one child divider view; "
         "removing that child (reverting to manual OnPaint) would break the "
         "separator rendering path introduced by the fix";

  views::View* divider = separator->children().front();
  EXPECT_EQ(divider->GetPreferredSize().height(), 1)
      << "Child divider must report a preferred height of 1dp; a zero or "
          "missing height would make the separator invisible without a failing "
          "pixel test";
  EXPECT_NE(divider->background(), nullptr)
      << "Child divider must have a non-null background; an empty/transparent "
          "background would silently revert the separator to invisible";
}

class SidebarSplitGroupRenderTest : public views::ViewsTestBase {
 public:
  SidebarSplitGroupRenderTest()
      : views::ViewsTestBase(
            std::unique_ptr<base::test::TaskEnvironment>(
                std::make_unique<content::BrowserTaskEnvironment>())) {}
};

views::BoxLayoutView* FindSplitGroupContainer(views::View* root) {
  if (root && (root->GetClassName() == "BoxLayoutView" ||
               root->GetClassName() == "SplitGroupContainerView")) {
    return static_cast<views::BoxLayoutView*>(root);
  }
  if (root) {
    for (views::View* child : root->children()) {
      if (auto* found = FindSplitGroupContainer(child)) {
        return found;
      }
    }
  }
  return nullptr;
}

TEST_F(SidebarSplitGroupRenderTest, SplitGroup_HorizontalOrientation_RendersRow) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode split_group;
  split_group.kind = SidebarNodeKind::kSplitGroup;
  split_group.split_id = "split-1";
  split_group.split_orientation = "vertical";
  split_group.children.push_back(MakeTabNode("tab-1", 0));
  split_group.children.push_back(MakeTabNode("tab-2", 1));
  model.normal_tree.push_back(split_group);

  list.Update(model, /*browser=*/nullptr);

  views::View* normal_section = list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_NE(normal_section, nullptr);

  views::BoxLayoutView* split_container = FindSplitGroupContainer(normal_section);
  ASSERT_NE(split_container, nullptr);

  // Vertical split in viewport -> horizontal layout orientation in sidebar
  EXPECT_EQ(split_container->GetOrientation(), views::LayoutOrientation::kHorizontal);
}

TEST_F(SidebarSplitGroupRenderTest, SplitGroup_HorizontalOrientation_WhenPanelCollapsed_RendersColumn) {
  TestingProfile profile;
  TestBrowserWindow* window = new TestBrowserWindow();
  BrowserWindowCreateParams params(&profile, true);
  params.window = window;
  auto browser_owner =
      DeprecatedCreateOwnedBrowserWindowForTesting(std::move(params));
  Browser* browser = browser_owner.get();

  // Set pref to collapsed (expanded = false)
  profile.GetPrefs()->SetBoolean(sidebar_prefs::kSidebarPanelExpanded, false);

  MahoSidebarTabListView list(browser);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode split_group;
  split_group.kind = SidebarNodeKind::kSplitGroup;
  split_group.split_id = "split-1";
  split_group.split_orientation = "vertical";
  split_group.children.push_back(MakeTabNode("tab-1", 0));
  split_group.children.push_back(MakeTabNode("tab-2", 1));
  model.normal_tree.push_back(split_group);

  list.Update(model, browser);

  views::View* normal_section = list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_NE(normal_section, nullptr);

  views::BoxLayoutView* split_container = FindSplitGroupContainer(normal_section);
  ASSERT_NE(split_container, nullptr);

  // Forced to kVertical because panel is collapsed (narrow mode)
  EXPECT_EQ(split_container->GetOrientation(), views::LayoutOrientation::kVertical);
}

TEST_F(SidebarSplitGroupRenderTest, SplitGroup_FlexFollowsSplitRatio) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode split_group;
  split_group.kind = SidebarNodeKind::kSplitGroup;
  split_group.split_id = "split-1";
  split_group.split_orientation = "vertical";
  split_group.split_ratio = 0.7;  // pane widths mirror the real split ratio
  split_group.children.push_back(MakeTabNode("tab-1", 0));
  split_group.children.push_back(MakeTabNode("tab-2", 1));
  model.normal_tree.push_back(split_group);

  list.Update(model, /*browser=*/nullptr);

  views::View* normal_section = list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_NE(normal_section, nullptr);

  views::BoxLayoutView* split_container = FindSplitGroupContainer(normal_section);
  ASSERT_NE(split_container, nullptr);

  // Trigger layout
  list.SetBounds(0, 0, 200, 400);
  list.DeprecatedLayoutImmediately();

  // Children layout: tab1 (lane, row, lane) -> divider -> tab2 (lane, row, lane)
  views::View* tab1 = split_container->children()[1];
  views::View* tab2 = split_container->children()[5];

  EXPECT_GT(tab2->bounds().width(), 0);
  EXPECT_GT(tab1->bounds().width(), tab2->bounds().width())
      << "The 0.7 leading pane must render wider than the 0.3 trailing pane";
}

TEST_F(SidebarSplitGroupRenderTest, SplitGroup_DividerInsertedBetweenChildren) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode split_group;
  split_group.kind = SidebarNodeKind::kSplitGroup;
  split_group.split_id = "split-1";
  split_group.split_orientation = "vertical";
  split_group.children.push_back(MakeTabNode("tab-1", 0));
  split_group.children.push_back(MakeTabNode("tab-2", 1));
  model.normal_tree.push_back(split_group);

  list.Update(model, /*browser=*/nullptr);

  views::View* normal_section = list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_NE(normal_section, nullptr);

  views::BoxLayoutView* split_container = FindSplitGroupContainer(normal_section);
  ASSERT_NE(split_container, nullptr);

  // Container must have 7 children: tab1 (3 views) -> divider (1 view) -> tab2 (3 views)
  ASSERT_EQ(split_container->children().size(), 7u);
  
  views::View* divider = split_container->children()[3];
  EXPECT_EQ(divider->GetClassName(), "View");
  EXPECT_EQ(divider->GetPreferredSize().width(), 1);
  EXPECT_EQ(divider->GetPreferredSize().height(), 28);
}

TEST_F(SidebarSplitGroupRenderTest, SplitGroup_ActiveChildHighlights) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode split_group;
  split_group.kind = SidebarNodeKind::kSplitGroup;
  split_group.split_id = "split-1";
  split_group.split_orientation = "vertical";
  
  SidebarTreeNode tab1 = MakeTabNode("tab-1", 0);
  tab1.is_active = true; // active tab
  split_group.children.push_back(tab1);
  split_group.children.push_back(MakeTabNode("tab-2", 1));
  model.normal_tree.push_back(split_group);

  list.Update(model, /*browser=*/nullptr);

  views::View* normal_section = list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_NE(normal_section, nullptr);

  views::BoxLayoutView* split_container = FindSplitGroupContainer(normal_section);
  ASSERT_NE(split_container, nullptr);

  // Active child -> background is non-null
  EXPECT_NE(split_container->background(), nullptr);
}

TEST_F(SidebarSplitGroupRenderTest, SplitGroup_TwoLayoutOrientationsHaveDifferentDividerAxis) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode split_group;
  split_group.kind = SidebarNodeKind::kSplitGroup;
  split_group.split_id = "split-1";
  split_group.split_orientation = "horizontal";
  split_group.children.push_back(MakeTabNode("tab-1", 0));
  split_group.children.push_back(MakeTabNode("tab-2", 1));
  model.normal_tree.push_back(split_group);

  list.Update(model, /*browser=*/nullptr);

  views::View* normal_section = list.FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
  ASSERT_NE(normal_section, nullptr);

  views::BoxLayoutView* split_container = FindSplitGroupContainer(normal_section);
  ASSERT_NE(split_container, nullptr);

  EXPECT_EQ(split_container->GetOrientation(), views::LayoutOrientation::kVertical);
  
  ASSERT_EQ(split_container->children().size(), 7u);
  views::View* divider = split_container->children()[3];
  EXPECT_EQ(divider->GetPreferredSize().width(), 28);
  EXPECT_EQ(divider->GetPreferredSize().height(), 1);
}

TEST_F(SidebarStateModelContractTest, FolderCollapsePreservesMultiSelection) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode folder = MakeFolderNode("folder-1");
  folder.is_expanded = true;
  folder.children.push_back(MakeFolderChildTabNode("tab-1", 0, "folder-1", 0));
  model.normal_tree.push_back(folder);

  list.Update(model, /*browser=*/nullptr);

  SidebarTabRowView* tab_row = list.FindTabRowByIdForTesting("tab-1");
  ASSERT_NE(tab_row, nullptr);

  list.ToggleTabSelected("tab-1");
  EXPECT_TRUE(list.IsTabSelected("tab-1"));
  EXPECT_TRUE(tab_row->is_selected());

  list.ToggleFolderExpandedForTesting("folder-1");

  EXPECT_TRUE(list.IsTabSelected("tab-1"));
  EXPECT_EQ(list.FindTabRowByIdForTesting("tab-1"), nullptr);

  // Re-expand: the freshly built row must reflect the preserved selection.
  list.ToggleFolderExpandedForTesting("folder-1");
  SidebarTabRowView* reexpanded = list.FindTabRowByIdForTesting("tab-1");
  ASSERT_NE(reexpanded, nullptr);
  EXPECT_TRUE(reexpanded->is_selected());

  // Collapse again then clear: ClearSelection must not dereference the freed
  // collapsed row (dangling-raw_ptr/UAF regression guard).
  list.ToggleFolderExpandedForTesting("folder-1");
  list.ClearSelection();
  EXPECT_FALSE(list.IsTabSelected("tab-1"));
}

TEST_F(SidebarStateModelContractTest,
       UpdateRvaluePreservesLastModelForFolderToggle) {
  MahoSidebarTabListView list(nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";

  SidebarTreeNode folder = MakeFolderNode("folder-1");
  folder.is_expanded = true;
  folder.children.push_back(MakeFolderChildTabNode("tab-1", 0, "folder-1", 0));
  model.normal_tree.push_back(folder);

  list.Update(std::move(model), nullptr);

  ASSERT_NE(list.FindTabRowByIdForTesting("tab-1"), nullptr);
  list.ToggleFolderExpandedForTesting("folder-1");
  EXPECT_EQ(list.FindTabRowByIdForTesting("tab-1"), nullptr);

  list.ToggleFolderExpandedForTesting("folder-1");
  EXPECT_NE(list.FindTabRowByIdForTesting("tab-1"), nullptr);
}

TEST_F(SidebarStateModelContractTest, MultiSelectDragAndKeyboardNavigation) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  model.normal_tree.push_back(MakeTabNode("tab-2", 1));
  list.Update(model, /*browser=*/nullptr);

  SidebarTabRowView* tab_row_1 = list.FindTabRowByIdForTesting("tab-1");
  SidebarTabRowView* tab_row_2 = list.FindTabRowByIdForTesting("tab-2");
  ASSERT_NE(tab_row_1, nullptr);
  ASSERT_NE(tab_row_2, nullptr);

  // 1. Verify multi-select selection state
  list.ToggleTabSelected("tab-1");
  list.ToggleTabSelected("tab-2");
  EXPECT_TRUE(list.IsTabSelected("tab-1"));
  EXPECT_TRUE(list.IsTabSelected("tab-2"));
  EXPECT_EQ(list.selected_tab_ids().size(), 2u);

  // 2. Verify Drag Payload Serialization
  ui::OSExchangeData data;
  tab_row_1->WriteDragDataForView(tab_row_1, gfx::Point(), &data);
  SidebarDragPayload payload;
  EXPECT_TRUE(ReadMahoDragData(data, payload));
  EXPECT_EQ(payload.selected_tab_ids.size(), 2u);
  EXPECT_TRUE(std::find(payload.selected_tab_ids.begin(), payload.selected_tab_ids.end(), "tab-1") != payload.selected_tab_ids.end());
  EXPECT_TRUE(std::find(payload.selected_tab_ids.begin(), payload.selected_tab_ids.end(), "tab-2") != payload.selected_tab_ids.end());

  // 3. Verify Shift+Arrow down range extension on key pressed
  // Focus the first row:
  tab_row_1->RequestFocus();
  ui::KeyEvent key_event(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_SHIFT_DOWN);
  list.OnKeyPressed(key_event);

  // Both should still be selected, and range extension is verified.
  EXPECT_TRUE(list.IsTabSelected("tab-1"));
  EXPECT_TRUE(list.IsTabSelected("tab-2"));
}

TEST_F(SidebarStateModelContractTest, ShowTabRowContextMenuMultiSelect) {
  auto* list = new MahoSidebarTabListView(/*browser=*/nullptr);

  // Set up a widget so that list->GetWidget() is valid.
  std::unique_ptr<views::Widget> widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetContentsView(list);

  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  model.normal_tree.push_back(MakeTabNode("tab-2", 1));
  list->Update(model, /*browser=*/nullptr);

  SidebarTabRowView* tab_row_1 = list->FindTabRowByIdForTesting("tab-1");
  ASSERT_NE(tab_row_1, nullptr);

  // Select both tabs
  list->ToggleTabSelected("tab-1");
  list->ToggleTabSelected("tab-2");
  EXPECT_TRUE(list->IsTabSelected("tab-1"));
  EXPECT_TRUE(list->IsTabSelected("tab-2"));

  // Trigger ShowTabRowContextMenu.
  list->ShowTabRowContextMenu(tab_row_1, 0, gfx::Point(), ui::mojom::MenuSourceType::kNone, nullptr);

  // Verify that BUG-2 is solved: because the row view is passed, the active context menu was created
  // with multi-select enabled.
  MahoTabContextMenu* menu = list->active_context_menu_for_testing();
  ASSERT_NE(menu, nullptr);

  // Check that the selected tab IDs passed to the context menu contain both selected tabs.
  const std::vector<std::string>& selected_ids = menu->selected_tab_ids_for_testing();
  EXPECT_EQ(selected_ids.size(), 2u);
  EXPECT_TRUE(std::find(selected_ids.begin(), selected_ids.end(), "tab-1") != selected_ids.end());
  EXPECT_TRUE(std::find(selected_ids.begin(), selected_ids.end(), "tab-2") != selected_ids.end());
}

TEST_F(SidebarStateModelContractTest, ShowTabRowContextMenuUnselectedClearsSelection) {
  auto* list = new MahoSidebarTabListView(/*browser=*/nullptr);

  std::unique_ptr<views::Widget> widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetContentsView(list);

  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeTabNode("tab-1", 0));
  model.normal_tree.push_back(MakeTabNode("tab-2", 1));
  model.normal_tree.push_back(MakeTabNode("tab-3", 2));
  list->Update(model, /*browser=*/nullptr);

  SidebarTabRowView* tab_row_1 = list->FindTabRowByIdForTesting("tab-1");
  SidebarTabRowView* tab_row_2 = list->FindTabRowByIdForTesting("tab-2");
  SidebarTabRowView* tab_row_3 = list->FindTabRowByIdForTesting("tab-3");
  ASSERT_NE(tab_row_1, nullptr);
  ASSERT_NE(tab_row_2, nullptr);
  ASSERT_NE(tab_row_3, nullptr);

  // Select tab-1 and tab-2
  list->ToggleTabSelected("tab-1");
  list->ToggleTabSelected("tab-2");
  EXPECT_TRUE(list->IsTabSelected("tab-1"));
  EXPECT_TRUE(list->IsTabSelected("tab-2"));
  EXPECT_FALSE(list->IsTabSelected("tab-3"));

  // Trigger ShowTabRowContextMenu on tab-3 (unselected).
  list->ShowTabRowContextMenu(tab_row_3, 2, gfx::Point(), ui::mojom::MenuSourceType::kNone, nullptr);

  // Selection must be cleared.
  EXPECT_FALSE(list->IsTabSelected("tab-1"));
  EXPECT_FALSE(list->IsTabSelected("tab-2"));

  // Active menu should be single-tab for tab-3.
  MahoTabContextMenu* menu = list->active_context_menu_for_testing();
  ASSERT_NE(menu, nullptr);
  EXPECT_EQ(menu->selected_tab_ids_for_testing().size(), 0u);
}

// ---------------------------------------------------------------------------
// Whole-folder Normal<->Pinned drag transitions.
//
// A folder that crosses the Pinned/Normal boundary flips its pin state and must
// emit a typed set_folder_pinned(bool) event before any positional reorder. A
// same-section folder drop is a pure reorder and must not emit a pin event.
// ---------------------------------------------------------------------------

SidebarDragPayload MakePinnedFolderPayload(const std::string& folder_id) {
  SidebarDragPayload payload = MakeFolderPayload(folder_id);
  payload.origin = SidebarDragOrigin::kPinnedSection;
  return payload;
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropToPinnedSectionPlanEmitsFolderPinTransition) {
  SidebarDropPlan plan =
      ResolveDropPlan(SectionKind::kPinned, MakeFolderPayload("folder-1"));
  EXPECT_TRUE(plan.is_valid);
  EXPECT_TRUE(plan.steps.empty());
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kPin);
  EXPECT_TRUE(plan.pin_transition.is_folder);
  EXPECT_EQ(plan.pin_transition.node_id, "folder-1");
  EXPECT_EQ(plan.pin_transition.space_id, "space-1");
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropToNormalSectionPlanEmitsFolderUnpinTransition) {
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal,
                                         MakePinnedFolderPayload("folder-p"));
  EXPECT_TRUE(plan.is_valid);
  EXPECT_TRUE(plan.steps.empty());
  EXPECT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kUnpin);
  EXPECT_TRUE(plan.pin_transition.is_folder);
  EXPECT_EQ(plan.pin_transition.node_id, "folder-p");
  EXPECT_EQ(plan.pin_transition.space_id, "space-1");
}

TEST_F(SidebarDnDDispatchTest,
       SameSectionFolderDropPlanEmitsNoPinTransition) {
  SidebarDropPlan normal_plan =
      ResolveDropPlan(SectionKind::kNormal, MakeFolderPayload("folder-n"));
  EXPECT_TRUE(normal_plan.is_valid);
  EXPECT_EQ(normal_plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
  EXPECT_FALSE(normal_plan.pin_transition.is_folder);

  SidebarDropPlan pinned_plan =
      ResolveDropPlan(SectionKind::kPinned, MakePinnedFolderPayload("folder-p"));
  EXPECT_TRUE(pinned_plan.is_valid);
  EXPECT_EQ(pinned_plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kNone);
  EXPECT_FALSE(pinned_plan.pin_transition.is_folder);
}

TEST_F(SidebarDnDDispatchTest, FolderDropToFavoritesRemainsInvalid) {
  EXPECT_FALSE(
      ResolveDropPlan(SectionKind::kFavorites, MakeFolderPayload("folder-n"))
          .is_valid);
  EXPECT_FALSE(ResolveDropPlan(SectionKind::kFavorites,
                               MakePinnedFolderPayload("folder-p"))
                   .is_valid);
}

TEST_F(SidebarDnDDispatchTest,
       HandlePostDropTransitionFolderPinDispatchesSetFolderPinnedTrue) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  SidebarDragPayload payload = MakeFolderPayload("folder-1");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  list.HandlePostDropTransition(payload, plan);

  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "set_folder_pinned");
  SCOPED_TRACE(observer_.events[0].json);
  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("space_id"), "space-1");
  EXPECT_EQ(*dict.FindString("folder_id"), "folder-1");
  std::optional<bool> is_pinned = dict.FindBool("is_pinned");
  ASSERT_TRUE(is_pinned.has_value());
  EXPECT_TRUE(*is_pinned);
}

TEST_F(SidebarDnDDispatchTest,
       HandlePostDropTransitionFolderUnpinDispatchesSetFolderPinnedFalse) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  SidebarDragPayload payload = MakePinnedFolderPayload("folder-p");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  list.HandlePostDropTransition(payload, plan);

  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "set_folder_pinned");
  SCOPED_TRACE(observer_.events[0].json);
  auto parsed = ParseJson(observer_.events[0].json);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(*dict.FindString("folder_id"), "folder-p");
  std::optional<bool> is_pinned = dict.FindBool("is_pinned");
  ASSERT_TRUE(is_pinned.has_value());
  EXPECT_FALSE(*is_pinned);
}

TEST_F(SidebarDnDDispatchTest,
       HandlePostDropTransitionSameSectionFolderDispatchesNoEvent) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  SidebarDragPayload payload = MakeFolderPayload("folder-n");
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kNormal, payload);
  list.HandlePostDropTransition(payload, plan);
  EXPECT_TRUE(observer_.events.empty());
}

TEST_F(SidebarDnDDispatchTest,
       HandlePostDropTransitionTabPinDoesNotDispatchSetFolderPinned) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  SidebarDragPayload payload =
      MakeTabPayload("tab-1", SidebarDragOrigin::kNormalSection);
  SidebarDropPlan plan = ResolveDropPlan(SectionKind::kPinned, payload);
  ASSERT_EQ(plan.pin_transition.kind,
            SidebarDropPlan::PinTransition::Kind::kPin);
  ASSERT_FALSE(plan.pin_transition.is_folder);
  list.HandlePostDropTransition(payload, plan);
  for (const auto& ev : observer_.events) {
    EXPECT_NE(ev.kind, "set_folder_pinned");
  }
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropToPinnedSectionLaneDispatchesSetFolderPinnedBeforeReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.pinned_tree.push_back(MakeFolderNode("target-folder"));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "set_folder_pinned");
  EXPECT_NE(observer_.events[0].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
  EXPECT_NE(observer_.events[0].json.find("\"is_pinned\":true"),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_folder");
  EXPECT_NE(observer_.events[1].json.find("\"folder_id\":\"source-folder\""),
            std::string::npos);
}

TEST_F(SidebarDnDDispatchTest,
       FolderDropToNormalSectionLaneDispatchesSetFolderPinnedFalseBeforeReorder) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeFolderNode("target-folder"));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeFolderByIdForTesting("target-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakePinnedFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "set_folder_pinned");
  EXPECT_NE(observer_.events[0].json.find("\"is_pinned\":false"),
            std::string::npos);
  EXPECT_EQ(observer_.events[1].kind, "reorder_folder");
}

TEST_F(SidebarDnDDispatchTest,
       SameSectionFolderReorderLaneEmitsReorderWithoutPinEvent) {
  MahoSidebarTabListView list(/*browser=*/nullptr);
  MahoSidebarTabListModel model;
  model.active_space_id = "space-1";
  model.normal_tree.push_back(MakeFolderNode("target-folder"));
  model.normal_tree.push_back(MakeFolderNode("next-folder"));
  list.Update(model, /*browser=*/nullptr);

  views::View* lane =
      list.FindInsertionLaneBeforeFolderByIdForTesting("next-folder");
  ASSERT_TRUE(lane);

  ui::OSExchangeData data;
  WriteDropData(MakeFolderPayload("source-folder"), &data);
  ui::DropTargetEvent event = MakeDropEvent(data);

  auto callback = lane->GetDropCallback(event);
  ASSERT_TRUE(callback);
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 1u);
  EXPECT_EQ(observer_.events[0].kind, "reorder_folder");
  for (const auto& ev : observer_.events) {
    EXPECT_NE(ev.kind, "set_folder_pinned");
  }
}

TEST_F(SidebarDnDDispatchTest,
       TabDropToPinnedSectionStillDispatchesPinTabAndNoFolderEvent) {
  ui::OSExchangeData data;
  WriteDropData(MakeTabPayload("source-tab", SidebarDragOrigin::kNormalSection),
                &data);
  ui::DropTargetEvent event = MakeDropEvent(data);
  auto callback = GetSectionDropCallbackForTesting(
      MahoSidebarTabSection::kPinned, event);

  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  ASSERT_EQ(output, ui::mojom::DragOperation::kMove);
  ASSERT_EQ(observer_.events.size(), 2u);
  EXPECT_EQ(observer_.events[0].kind, "pin_tab");
  EXPECT_NE(observer_.events[0].kind, "set_folder_pinned");
  EXPECT_EQ(observer_.events[1].kind, "reorder_root_item");
}

TEST_F(SidebarDnDDispatchTest, CrossSpaceIsolationInTabListModel) {
  ::MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  std::string space_1 = ActiveSpaceIdForCore(core);
  ASSERT_FALSE(space_1.empty());

  const std::string space_2 = CreateSpaceForCoreTesting(core, "Space Two");
  ASSERT_FALSE(space_2.empty());

  const std::string kTabSpace1 = "11111111-1111-4000-8000-000000000001";
  const std::string kTabSpace2 = "22222222-2222-4000-8000-000000000002";

  DispatchCreateTabToCore(core, space_1, kTabSpace1);
  DispatchCreateTabToCore(core, space_2, kTabSpace2);

  // Space 1 must only contain kTabSpace1 and never kTabSpace2.
  SidebarStateBackgroundResult res1 =
      BuildSidebarStateOnBackgroundForTesting(core, base::GetQuotedJSONString(space_1), true);
  ASSERT_TRUE(res1.parsed_tree.has_value());
  ASSERT_TRUE(res1.parsed_tree->is_list());
  std::vector<std::string> space_1_tabs;
  for (const auto& item : res1.parsed_tree->GetList()) {
    if (const auto* dict = item.GetIfDict()) {
      if (const std::string* id = dict->FindString("id")) {
        space_1_tabs.push_back(*id);
      }
    }
  }
  EXPECT_EQ(space_1_tabs, std::vector<std::string>({kTabSpace1}));

  // Space 2 must only contain kTabSpace2 and never kTabSpace1.
  SidebarStateBackgroundResult res2 =
      BuildSidebarStateOnBackgroundForTesting(core, base::GetQuotedJSONString(space_2), true);
  ASSERT_TRUE(res2.parsed_tree.has_value());
  ASSERT_TRUE(res2.parsed_tree->is_list());
  std::vector<std::string> space_2_tabs;
  for (const auto& item : res2.parsed_tree->GetList()) {
    if (const auto* dict = item.GetIfDict()) {
      if (const std::string* id = dict->FindString("id")) {
        space_2_tabs.push_back(*id);
      }
    }
  }
  EXPECT_EQ(space_2_tabs, std::vector<std::string>({kTabSpace2}));

  maho_core_free(core);
}

TEST_F(SidebarDnDDispatchTest, CrossSpacePinnedTabsPreservedWithoutLeak) {
  ::MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  std::string space_1 = ActiveSpaceIdForCore(core);
  ASSERT_FALSE(space_1.empty());

  const std::string space_2 = CreateSpaceForCoreTesting(core, "Space B");
  ASSERT_FALSE(space_2.empty());

  const std::string kPinnedTab1 = "33333333-3333-4000-8000-000000000001";
  const std::string kNormalTab1 = "33333333-3333-4000-8000-000000000002";
  const std::string kNormalTab2 = "44444444-4444-4000-8000-000000000001";

  DispatchCreateTabToCore(core, space_1, kPinnedTab1);
  DispatchCreateTabToCore(core, space_1, kNormalTab1);
  DispatchCreateTabToCore(core, space_2, kNormalTab2);
  base::DictValue pin_tab;
  pin_tab.Set("kind", "pin_tab");
  pin_tab.Set("tab_id", kPinnedTab1);
  DispatchJsonToCore(core, std::move(pin_tab));

  SidebarStateBackgroundResult res1 =
      BuildSidebarStateOnBackgroundForTesting(core, base::GetQuotedJSONString(space_1), true);
  ASSERT_TRUE(res1.parsed_tree.has_value());
  ASSERT_TRUE(res1.parsed_tree->is_list());

  std::vector<std::string> pinned_ids;
  std::vector<std::string> normal_ids;
  for (const auto& item : res1.parsed_tree->GetList()) {
    if (const auto* dict = item.GetIfDict()) {
      const std::string* id = dict->FindString("id");
      std::optional<bool> is_pinned = dict->FindBool("isPinned");
      if (id && is_pinned && *is_pinned) {
        pinned_ids.push_back(*id);
      } else if (id) {
        normal_ids.push_back(*id);
      }
    }
  }

  EXPECT_EQ(pinned_ids, std::vector<std::string>({kPinnedTab1}));
  EXPECT_EQ(normal_ids, std::vector<std::string>({kNormalTab1}));

  SidebarStateBackgroundResult res2 =
      BuildSidebarStateOnBackgroundForTesting(core, base::GetQuotedJSONString(space_2), true);
  ASSERT_TRUE(res2.parsed_tree.has_value());
  ASSERT_TRUE(res2.parsed_tree->is_list());

  std::vector<std::string> space_2_all;
  for (const auto& item : res2.parsed_tree->GetList()) {
    if (const auto* dict = item.GetIfDict()) {
      if (const std::string* id = dict->FindString("id")) {
        space_2_all.push_back(*id);
      }
    }
  }
  EXPECT_EQ(space_2_all, std::vector<std::string>({kNormalTab2}));

  maho_core_free(core);
}

}  // namespace
}  // namespace maho
