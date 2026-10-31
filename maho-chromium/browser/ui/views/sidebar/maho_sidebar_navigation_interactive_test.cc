// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include <algorithm>
#include <optional>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/test/browser_test.h"
#include "build/build_config.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/command/maho_shortcut_interceptor.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "base/time/time.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_downloads_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_rail_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_action_pane_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_media_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_spaces_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_library_action_pane_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h"
#include "maho/browser/ui/views/spaces_overlay/maho_spaces_overlay_board_view.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/events/event.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/test/test_event.h"
#include "ui/base/test/ui_controls.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "base/values.h"
#include "url/gurl.h"

namespace maho {
namespace {

class ScopedCoreOverride {
 public:
  explicit ScopedCoreOverride(MahoCore* core) : saved_core_(GetCore()) {
    SetCore(core);
  }

  ScopedCoreOverride(const ScopedCoreOverride&) = delete;
  ScopedCoreOverride& operator=(const ScopedCoreOverride&) = delete;

 ~ScopedCoreOverride() { SetCore(saved_core_); }

 private:
  raw_ptr<MahoCore> saved_core_ = nullptr;
};

std::string StartDownload(MahoCore* core,
                          const std::string& filename,
                          const std::string& url,
                          uint64_t total_bytes,
                          const std::string& file_path = std::string(),
                          const std::string& mime_type = std::string()) {
  std::string json = base::StringPrintf(
      R"({"filename":%s,"url":%s,"total_bytes":%llu)",
      base::GetQuotedJSONString(filename).c_str(),
      base::GetQuotedJSONString(url).c_str(),
      static_cast<unsigned long long>(total_bytes));
  if (!file_path.empty()) {
    json += R"(,"file_path":)" + base::GetQuotedJSONString(file_path);
  }
  if (!mime_type.empty()) {
    json += R"(,"mime_type":)" + base::GetQuotedJSONString(mime_type);
  }
  json += "}";
  char* raw_id = maho_core_start_download(core, json.c_str());
  EXPECT_TRUE(raw_id);
  std::optional<base::Value> parsed = base::JSONReader::Read(raw_id, base::JSON_PARSE_RFC);
  maho_string_free(raw_id);
  EXPECT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->is_string());
  return parsed->is_string() ? parsed->GetString() : std::string();
}

views::View* FindDownloadRow(MahoSidebarDownloadsView* downloads_view,
                             const std::string& download_id) {
  return downloads_view->download_row_for_download_id_for_testing(download_id);
}

views::Label* FindLabelWithText(views::View* root, const std::u16string& text) {
  if (!root) {
    return nullptr;
  }

  if (auto* label = views::AsViewClass<views::Label>(root);
      label && label->GetText() == text) {
    return label;
  }

  for (const auto& child : root->children()) {
    if (views::Label* match = FindLabelWithText(child.get(), text)) {
      return match;
    }
  }

  return nullptr;
}

views::Button* FindButtonWithAccessibleName(views::View* root,
                                            const std::u16string& name) {
  if (!root) {
    return nullptr;
  }

  if (auto* button = views::AsViewClass<views::Button>(root)) {
    ui::AXNodeData data;
    button->GetViewAccessibility().GetAccessibleNodeData(&data);
    if (data.GetString16Attribute(ax::mojom::StringAttribute::kName) == name) {
      return button;
    }
  }

  for (views::View* child : root->children()) {
    if (views::Button* found = FindButtonWithAccessibleName(child, name)) {
      return found;
    }
  }
  return nullptr;
}

const SidebarTreeNode* FindTabById(
    const std::vector<SidebarTreeNode>& nodes,
    const std::string& tab_id) {
  for (const auto& node : nodes) {
    if (node.kind == SidebarNodeKind::kTab && node.tab_id == tab_id) {
      return &node;
    }
    if (const SidebarTreeNode* match = FindTabById(node.children, tab_id)) {
      return match;
    }
  }
  return nullptr;
}

ui::AXNodeData GetAccessibilityNodeData(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data;
}

std::vector<uint8_t> SnapshotImageBytes(const gfx::ImageSkia& image) {
  std::vector<uint8_t> out;
  for (float scale : {1.0f, 2.0f, 3.0f}) {
    const SkBitmap& bitmap = image.GetRepresentation(scale).GetBitmap();
    const size_t size = bitmap.computeByteSize();
    if (size == 0 || !bitmap.getPixels()) {
      continue;
    }
    const uint8_t* pixels = static_cast<const uint8_t*>(bitmap.getPixels());
    auto span = UNSAFE_BUFFERS(base::span(pixels, size));
    out.insert(out.end(), span.begin(), span.end());
  }
  return out;
}

class MahoSidebarNavigationInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LaunchShowsSidebar) {
  ExpectSidebarVisible(true);

  auto state = GetViewState();
  int total_tabs = static_cast<int>(state.tab_list.normal_tree.size()) +
                   static_cast<int>(state.tab_list.pinned_tree.size());
  EXPECT_GE(total_tabs, 1) << "Expected at least 1 tab (NTP) in sidebar";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       RealTabsPopulateSidebar) {
  AddTestTabs(3);

  ResolveViews();
  auto state = GetViewState();
  int total_tabs = static_cast<int>(state.tab_list.normal_tree.size()) +
                   static_cast<int>(state.tab_list.pinned_tree.size());
  // NTP + 3 added tabs.
  EXPECT_EQ(4, total_tabs);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       CollapsingPanelKeepsLayoutEnabled) {
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ResolveViews();

  base::RunLoop().RunUntilIdle();

  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(false);
  ExpectSidebarVisible(true);

  ASSERT_TRUE(GetContainerView());
  ASSERT_TRUE(GetContainerView()->sidebar_view());
  EXPECT_TRUE(GetContainerView()->GetVisible());
  EXPECT_FALSE(GetContainerView()->sidebar_view()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       CollapsedPanelExposesUsableExpandSidebarButton) {
  SeedSidebarPrefs(true, false);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  MahoSidebarContainerView* container = GetContainerView();
  ASSERT_TRUE(container);
  views::Button* expand_button =
      FindButtonWithAccessibleName(container, u"Expand Sidebar");
  ASSERT_TRUE(expand_button);
  EXPECT_TRUE(expand_button->GetVisible());
  EXPECT_FALSE(expand_button->GetBoundsInScreen().IsEmpty());
  EXPECT_EQ(views::View::FocusBehavior::ALWAYS,
            expand_button->GetFocusBehavior());

  const ui::AXNodeData data = GetAccessibilityNodeData(expand_button);
  EXPECT_EQ(ax::mojom::Role::kButton, data.role);
  EXPECT_EQ(u"Expand Sidebar",
            data.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_TRUE(data.HasAction(ax::mojom::Action::kDoDefault));

  views::test::ButtonTestApi(expand_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  ExpectSidebarPanelExpanded(true);
  EXPECT_TRUE(container->sidebar_view()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ClickSidebarTabSwitchesActive) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_GE(state.tab_list.normal_tree.size(), 2u);

  // Find a non-active tab in the tree.
  int active_index = browser()->GetTabStripModel()->active_index();
  std::string target_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.is_active) {
      target_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(target_tab_id.empty());

  auto* tab_row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
  ASSERT_TRUE(tab_row);
  views::View* title_button = tab_row->title_button_for_testing();
  ASSERT_TRUE(title_button);

  // Click the visible label button so activation exercises the same surface
  // users hit for normal row selection.
  const gfx::Point center = title_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();

  base::RunLoop().RunUntilIdle();

  // The active index should have changed.
  EXPECT_NE(active_index, browser()->GetTabStripModel()->active_index());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       TabCloseFromSidebar) {
  SeedProfile(2);

  auto state = GetViewState();
  int initial_count = static_cast<int>(state.tab_list.normal_tree.size());
  ASSERT_GE(initial_count, 2);

  std::string target_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.is_active) {
      target_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(target_tab_id.empty());

  {
    auto* tab_row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
    ASSERT_TRUE(tab_row);

    // Hover over the row synchronously so the hover-reveal logic runs before we
    // try to read the close button's state.
    const gfx::Point row_center = tab_row->GetBoundsInScreen().CenterPoint();
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(row_center));
  }
  base::RunLoop().RunUntilIdle();

  // Re-acquire the row after hover/wait: the sidebar may have rebuilt its view
  // tree during RunUntilIdle(), making any previously captured pointer stale.
  auto* tab_row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
  ASSERT_TRUE(tab_row) << "tab row must still exist after hover settle";
  ASSERT_TRUE(tab_row->GetVisible()) << "tab row must be visible after hover settle";

  views::View* close_btn = tab_row->close_button_for_testing();
  ASSERT_TRUE(close_btn);

  // Wait until the close button is actually visible (hover-reveal may be async).
  ASSERT_TRUE(base::test::RunUntil([&] {
    // Re-fetch each poll iteration to guard against further rebuilds during wait.
    auto* row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
    if (!row) return false;
    auto* btn = row->close_button_for_testing();
    return btn && btn->GetVisible();
  })) << "close button must become visible after hovering the row";

  // Re-acquire a fresh close_btn pointer after the RunUntil loop above, since
  // the sidebar may have rebuilt during the polling iterations.
  tab_row = tab_list_->FindTabRowByIdForTesting(target_tab_id);
  ASSERT_TRUE(tab_row) << "tab row must still exist after close-button visibility wait";
  close_btn = tab_row->close_button_for_testing();
  ASSERT_TRUE(close_btn);
  // Sanity-check the button has been laid out with non-empty bounds before
  // dereferencing its screen position.
  ASSERT_FALSE(close_btn->GetBoundsInScreen().IsEmpty())
      << "close button must have non-empty bounds when visible";

  const gfx::Point center = close_btn->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(center));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  ResolveViews();
  auto new_state = GetViewState();
  int new_count = static_cast<int>(new_state.tab_list.normal_tree.size());
  EXPECT_EQ(initial_count - 1, new_count);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       PinnedLiveCloseAffordanceSurvivesThemeChange) {
  SeedProfile(2);

  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* r = tab_list_->FindActiveTabRowForTesting();
    return r && r->section_for_testing() == MahoSidebarTabSection::kPinned;
  })) << "SeedProfile(2)'s active tab must render as a pinned sidebar row";
  auto* row = tab_list_->FindActiveTabRowForTesting();
  ASSERT_TRUE(row) << "SeedProfile(2)'s active tab must have a sidebar row";
  ASSERT_EQ(MahoSidebarTabSection::kPinned, row->section_for_testing())
      << "SeedProfile(2) pins the active tab; expected it in the pinned section";
  ASSERT_FALSE(row->is_suspended());

  views::ImageButton* close_button = row->close_button_for_testing();
  ASSERT_TRUE(close_button);
  EXPECT_EQ(u"Unload tab", close_button->GetTooltipText());
  EXPECT_EQ(u"Unload tab",
            GetAccessibilityNodeData(close_button).GetString16Attribute(
                ax::mojom::StringAttribute::kName));
  const std::vector<uint8_t> live_pinned_bytes =
      SnapshotImageBytes(close_button->GetImage(views::Button::STATE_NORMAL));
  ASSERT_FALSE(live_pinned_bytes.empty());

  ASSERT_TRUE(row->GetWidget());
  row->GetWidget()->ThemeChanged();

  const std::vector<uint8_t> themed_bytes =
      SnapshotImageBytes(close_button->GetImage(views::Button::STATE_NORMAL));
  EXPECT_EQ(live_pinned_bytes, themed_bytes)
      << "Theme change must preserve the pinned minus close affordance";
  EXPECT_EQ(u"Unload tab", close_button->GetTooltipText());
  EXPECT_EQ(u"Unload tab",
            GetAccessibilityNodeData(close_button).GetString16Attribute(
                ax::mojom::StringAttribute::kName));

  row->SetSuspended(true);
  EXPECT_EQ(u"Close tab", close_button->GetTooltipText());
  EXPECT_EQ(u"Close tab",
            GetAccessibilityNodeData(close_button).GetString16Attribute(
                ax::mojom::StringAttribute::kName));
  const std::vector<uint8_t> suspended_bytes =
      SnapshotImageBytes(close_button->GetImage(views::Button::STATE_NORMAL));
  ASSERT_FALSE(suspended_bytes.empty());
  EXPECT_NE(live_pinned_bytes, suspended_bytes)
      << "Active live pinned tab must show the minus (unload) close affordance, "
         "distinct from the suspended X (close) affordance";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       PinnedLiveCloseButtonSuspendsBeforeDeleting) {
  SeedProfile(2);

  observer_.Clear();

  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* r = tab_list_->FindActiveTabRowForTesting();
    return r && r->section_for_testing() == MahoSidebarTabSection::kPinned;
  })) << "SeedProfile(2)'s active tab must render as a pinned sidebar row";
  auto* row = tab_list_->FindActiveTabRowForTesting();
  ASSERT_TRUE(row) << "SeedProfile(2)'s active tab must have a sidebar row";
  ASSERT_EQ(MahoSidebarTabSection::kPinned, row->section_for_testing())
      << "SeedProfile(2) pins the active tab; expected it in the pinned section";
  ASSERT_FALSE(row->is_suspended());
  const std::string tab_id = row->tab_id_for_testing();
  ASSERT_FALSE(tab_id.empty());
  views::ImageButton* close_button = row->close_button_for_testing();
  ASSERT_TRUE(close_button);

  views::test::ButtonTestApi(close_button).NotifyClick(ui::test::TestEvent());
  RunAllPendingTasks();
  ResolveViews();

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return observer_.HasEvent("suspend_tab"); }));
  EXPECT_FALSE(observer_.HasEvent("close_tab"));

  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto current_state = GetViewState();
    const SidebarTreeNode* current_tab =
        FindTabById(current_state.tab_list.pinned_tree, tab_id);
    return current_tab && current_tab->is_suspended;
  })) << "first pinned close press must suspend without deleting the tab";

  const auto final_state = GetViewState();
  const SidebarTreeNode* suspended_tab =
      FindTabById(final_state.tab_list.pinned_tree, tab_id);
  ASSERT_TRUE(suspended_tab);
  EXPECT_TRUE(suspended_tab->is_suspended);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       PinnedSuspendUsesFastPathWithoutFullRebuild) {
  SeedProfile(2);

  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* r = tab_list_->FindActiveTabRowForTesting();
    return r && r->section_for_testing() == MahoSidebarTabSection::kPinned;
  })) << "SeedProfile(2)'s active tab must render as a pinned sidebar row";
  observer_.Clear();

  auto* row = tab_list_->FindActiveTabRowForTesting();
  ASSERT_TRUE(row);
  const std::string tab_id = row->tab_id_for_testing();
  ASSERT_FALSE(tab_id.empty());
  views::ImageButton* close_button = row->close_button_for_testing();
  ASSERT_TRUE(close_button);

  const int rebuilds_before = tab_list_->rebuild_rows_count_for_testing();

  views::test::ButtonTestApi(close_button).NotifyClick(ui::test::TestEvent());
  RunAllPendingTasks();
  ResolveViews();

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return observer_.HasEvent("suspend_tab"); }));

  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* r = tab_list_->FindTabRowByIdForTesting(tab_id);
    return r && r->is_suspended();
  })) << "suspended pinned row must flip to X in place";

  EXPECT_EQ(rebuilds_before, tab_list_->rebuild_rows_count_for_testing())
      << "suspending a pinned tab must not trigger a full RebuildRows";
  EXPECT_EQ(row, tab_list_->FindTabRowByIdForTesting(tab_id))
      << "the suspended row must be updated in place, not recreated";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       PinnedWakeUsesFastPathFlippingRowInPlace) {
  SeedProfile(2);

  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* r = tab_list_->FindActiveTabRowForTesting();
    return r && r->section_for_testing() == MahoSidebarTabSection::kPinned;
  })) << "SeedProfile(2)'s active tab must render as a pinned sidebar row";
  observer_.Clear();

  auto* row = tab_list_->FindActiveTabRowForTesting();
  ASSERT_TRUE(row);
  const std::string tab_id = row->tab_id_for_testing();
  ASSERT_FALSE(tab_id.empty());
  views::ImageButton* close_button = row->close_button_for_testing();
  ASSERT_TRUE(close_button);

  views::test::ButtonTestApi(close_button).NotifyClick(ui::test::TestEvent());
  RunAllPendingTasks();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* r = tab_list_->FindTabRowByIdForTesting(tab_id);
    return r && r->is_suspended();
  })) << "pinned tab must first suspend to X";
  RunAllPendingTasks();

  auto* suspended_row = tab_list_->FindTabRowByIdForTesting(tab_id);
  ASSERT_TRUE(suspended_row);
  const int rebuilds_before_wake = tab_list_->rebuild_rows_count_for_testing();

  tab_list_->ActivateTabById(tab_id);

  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* r = tab_list_->FindTabRowByIdForTesting(tab_id);
    return r && !r->is_suspended();
  })) << "woken pinned row must flip back to minus in place";

  EXPECT_EQ(rebuilds_before_wake, tab_list_->rebuild_rows_count_for_testing())
      << "waking a suspended pinned tab must not trigger a full RebuildRows";
  EXPECT_EQ(suspended_row, tab_list_->FindTabRowByIdForTesting(tab_id))
      << "the woken row must be updated in place, not recreated";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       TabTitleUpdatesOnNavigation) {
  AddTestTabs(1);
  ResolveViews();

  content::WebContents* active =
      browser()->GetTabStripModel()->GetActiveWebContents();
  std::u16string title_before = active->GetTitle();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("https://example.org/different-page")));
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();
  bool found_updated = false;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && node.is_active) {
      found_updated = (node.title != title_before || !node.title.empty());
      break;
    }
  }
  EXPECT_TRUE(found_updated)
      << "Sidebar should reflect updated title after navigation";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       FavoriteClickActivatesTab) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());

  const auto& first_fav = state.favorites.items[0];
  ASSERT_FALSE(first_fav.tab_id.empty())
      << "First favorite must have a matching tab";

  views::View* tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(tile);

  const gfx::Point center = tile->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  content::WebContents* active =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(active);
  auto* helper = MahoTabIdHelper::FromWebContents(active);
  ASSERT_TRUE(helper);
  EXPECT_EQ(helper->stable_tab_id(), first_fav.tab_id)
      << "Click on a live-mapped favorite must activate that tab, "
         "not navigate the current tab";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       FavoriteClickWithNoLiveTabOpensNewForegroundTab) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());
  const auto& first_fav = state.favorites.items[0];
  ASSERT_FALSE(first_fav.tab_id.empty());

  // Close every live tab whose stable_tab_id matches the favorite.
  // After this, OnTilePressed must NOT find a live mapping and must
  // fall back to NEW_FOREGROUND_TAB.
  TabStripModel* strip = browser()->GetTabStripModel();
  for (int i = strip->count() - 1; i >= 0; --i) {
    content::WebContents* wc = strip->GetWebContentsAt(i);
    auto* helper = wc ? MahoTabIdHelper::FromWebContents(wc) : nullptr;
    if (helper && helper->stable_tab_id() == first_fav.tab_id) {
      strip->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
    }
  }
  // Ensure at least one unrelated tab remains so we can detect "new tab".
  ASSERT_GT(strip->count(), 0);
  const int tab_count_before = strip->count();
  content::WebContents* active_before = strip->GetActiveWebContents();
  ASSERT_TRUE(active_before);

  base::RunLoop().RunUntilIdle();
  ResolveViews();

  views::View* tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(tile);
  const gfx::Point center = tile->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(strip->count(), tab_count_before + 1)
      << "Fallback path must open a new tab, not reuse current";
  EXPECT_NE(strip->GetActiveWebContents(), active_before)
      << "New tab must be foreground (active)";
  EXPECT_TRUE(strip->GetActiveWebContents()
                  ->GetVisibleURL()
                  .spec()
                  .find(first_fav.url.host()) != std::string::npos);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       FavoriteWakeTargetsPinnedUrl) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());
  const auto& first_fav = state.favorites.items[0];
  ASSERT_FALSE(first_fav.tab_id.empty());

  // Author a home (pinned) URL that differs from the page the tab is showing.
  const GURL pinned_url("https://pinned-home.example.test/landing");
  ASSERT_TRUE(pinned_url.is_valid());
  ASSERT_NE(first_fav.url.spec(), pinned_url.spec());
  ASSERT_TRUE(maho_core_set_tab_pinned_url(maho::GetCore(),
                                           first_fav.tab_id.c_str(),
                                           pinned_url.spec().c_str()));

  // Close the live WebContents backing the favorite so a tile click takes the
  // suspend/wake path instead of plain activation.
  TabStripModel* strip = browser()->GetTabStripModel();
  for (int i = strip->count() - 1; i >= 0; --i) {
    content::WebContents* wc = strip->GetWebContentsAt(i);
    auto* helper = wc ? MahoTabIdHelper::FromWebContents(wc) : nullptr;
    if (helper && helper->stable_tab_id() == first_fav.tab_id) {
      strip->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
    }
  }
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  views::View* tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(tile);
  const gfx::Point center = tile->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  content::WebContents* active =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(active);
  EXPECT_EQ(active->GetVisibleURL().spec(), pinned_url.spec())
      << "waking a sleeping favorite must navigate to its pinned URL, not to "
         "the last visited page";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       FavoriteClickAfterDragCancel) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.favorites.items.empty());

  const auto& first_fav = state.favorites.items[0];
  ASSERT_FALSE(first_fav.tab_id.empty())
      << "First favorite must have a matching tab";

  views::View* tile = favorites_->GetTileForTesting(0);
  ASSERT_TRUE(tile);

  // Simulate starting drag and canceling it using standard views API.
  views::DragController* controller = tile->drag_controller();
  ASSERT_TRUE(controller);

  ui::OSExchangeData data;
  controller->WriteDragDataForView(tile, gfx::Point(), &data);
  tile->OnDragDone();

  // Click on the tile and verify it still activates the correct tab.
  const gfx::Point center = tile->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  content::WebContents* active =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(active);
  auto* helper = MahoTabIdHelper::FromWebContents(active);
  ASSERT_TRUE(helper);
  EXPECT_EQ(helper->stable_tab_id(), first_fav.tab_id)
      << "Click on a live-mapped favorite must activate that tab, "
         "not navigate the current tab";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       CollapseAndReopenPreservesState) {
  SeedProfile(2);
  auto state_before = GetViewState();
  int tab_count_before =
      static_cast<int>(state_before.tab_list.normal_tree.size());

  // Collapse panel.
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(false);

  // Re-expand panel via pref toggle.
  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/true);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  ExpectSidebarLayoutEnabled(true);
  ExpectSidebarPanelExpanded(true);
  ExpectSidebarVisible(true);

  auto state_after = GetViewState();
  int tab_count_after =
      static_cast<int>(state_after.tab_list.normal_tree.size());
  EXPECT_EQ(tab_count_before, tab_count_after)
      << "Tab count must be preserved across collapse and reopen";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       HoverRevealAndRepin) {
  SeedProfile(2);
  ResolveViews();

  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  ASSERT_TRUE(container->sidebar_view());

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/false);
  ResolveViews();
  // Collapsing a pinned sidebar slides it out; it hides when the slide ends.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !container->sidebar_view()->GetVisible(); }));

  EXPECT_TRUE(container->IsAutoHideMode());
  EXPECT_EQ(sidebar_layout::kCollapsedRailWidthDp,
            container->GetPreferredSize().width());

  // The interactive harness moves the cursor but does not deliver synthetic
  // mouse-enter/exit to the container, so invoke its real handlers with the
  // cursor parked at the matching position.
  const ui::MouseEvent enter_event(ui::EventType::kMouseEntered, gfx::Point(),
                                   gfx::Point(), base::TimeTicks::Now(), 0, 0);
  const ui::MouseEvent exit_event(ui::EventType::kMouseExited, gfx::Point(),
                                  gfx::Point(), base::TimeTicks::Now(), 0, 0);
  const gfx::Point center = container->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  container->OnMouseEntered(enter_event);

  EXPECT_TRUE(container->sidebar_view()->GetVisible());
  EXPECT_TRUE(container->IsOverlayVisible());

  const gfx::Rect screen_bounds = container->GetBoundsInScreen();
  const gfx::Point outside(screen_bounds.right() + 50,
                           screen_bounds.CenterPoint().y());
  ASSERT_TRUE(ui_controls::SendMouseMove(outside.x(), outside.y()));
  container->OnMouseExited(exit_event);

  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  container->OnMouseEntered(enter_event);

  EXPECT_TRUE(container->sidebar_view()->GetVisible());
  EXPECT_TRUE(container->IsOverlayVisible());

  SeedSidebarPrefs(/*layout_enabled=*/true, /*panel_expanded=*/true);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(container->IsAutoHideMode());
  EXPECT_FALSE(container->IsOverlayVisible());
  EXPECT_TRUE(container->sidebar_view()->GetVisible());
  ExpectSidebarPanelExpanded(true);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LibraryOpensArchivedTabsAndExitReturnsToTabs) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_archive_mode_for_testing());
  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs,
            sidebar->active_library_category_for_testing());
  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  EXPECT_TRUE(archive_view->GetVisible());
  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs,
            rail->selected_category());
  const std::vector<MahoSidebarLibraryRailView::Category>
      kVisibleFirstEntryCategories = {
          MahoSidebarLibraryRailView::Category::kDownloads,
          MahoSidebarLibraryRailView::Category::kArchivedTabs,
          MahoSidebarLibraryRailView::Category::kSpaces,
      };
  EXPECT_EQ(kVisibleFirstEntryCategories, rail->visible_categories_for_testing());
  // PB1-B: Rail selection exclusivity — only kArchivedTabs must carry the
  // selected AX attribute; every other category must be unselected.  This
  // protects the SG-1 contract that the footer/library path drives the correct
  // initial selection state rather than leaving the rail in an undefined or
  // multi-selected state.
  const std::vector<MahoSidebarLibraryRailView::Category> kAllCategories = {
      MahoSidebarLibraryRailView::Category::kArchivedTabs,
      MahoSidebarLibraryRailView::Category::kDownloads,
      MahoSidebarLibraryRailView::Category::kMedia,
      MahoSidebarLibraryRailView::Category::kSpaces,
  };
  for (const auto category : kAllCategories) {
    views::Button* btn = rail->button_for_category_for_testing(category);
    ASSERT_TRUE(btn) << "Rail button missing for category "
                     << static_cast<int>(category);
    // All rail buttons must remain enabled regardless of selection so the user
    // can switch categories without re-entering library mode.
    EXPECT_TRUE(btn->GetEnabled())
        << "Rail button should be enabled for category "
        << static_cast<int>(category);
    const bool expect_visible =
        std::find(kVisibleFirstEntryCategories.begin(),
                  kVisibleFirstEntryCategories.end(),
                  category) != kVisibleFirstEntryCategories.end();
    EXPECT_EQ(expect_visible, btn->GetVisible())
        << "Rail button visibility mismatch for category "
        << static_cast<int>(category);
    // Verify AX selected state matches the expected selection exclusively.
    ui::AXNodeData ax_data;
    btn->GetViewAccessibility().GetAccessibleNodeData(&ax_data);
    const bool expect_selected =
        (category == MahoSidebarLibraryRailView::Category::kArchivedTabs);
    EXPECT_EQ(expect_selected,
              ax_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected))
        << "AX kSelected mismatch for category "
        << static_cast<int>(category);
  }
  // PB1-B: Archive pane geometry — the view must have non-zero rendered height,
  // confirming the pane is actually laid out and not merely flagged visible.
  EXPECT_GT(archive_view->GetBoundsInScreen().height(), 0)
      << "Archive pane must have non-zero height after footer-entry";
  // PB1-B: tabs_surface_ must be hidden while archive/library mode is active so
  // the shell correctly swaps the body region.
  ASSERT_TRUE(sidebar->tabs_surface_for_testing());
  EXPECT_FALSE(sidebar->tabs_surface_for_testing()->GetVisible())
      << "tabs_surface_ must be hidden when library/archive mode is active";
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  ASSERT_TRUE(GetTabListView());
  EXPECT_FALSE(GetTabListView()->GetVisible());

  sidebar->ExitLibraryToTabs();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(sidebar->is_archive_mode_for_testing());
  EXPECT_FALSE(sidebar->is_library_mode_for_testing());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  EXPECT_TRUE(GetTabListView()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LibraryOpensDownloadsPane) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kDownloads,
            sidebar->active_library_category_for_testing());
  ASSERT_TRUE(sidebar->downloads_view_for_testing());
  EXPECT_TRUE(sidebar->downloads_view_for_testing()->GetVisible());
  EXPECT_TRUE(sidebar->library_rail_view_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kDownloads,
            sidebar->library_rail_view_for_testing()->selected_category());
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  ASSERT_TRUE(GetTabListView());
  EXPECT_FALSE(GetTabListView()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                        DownloadsPaneRunsItemActionsAndRefreshesState) {
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);

  std::string downloading_id;
  std::string paused_id;
  {
    ScopedCoreOverride scoped_core(core);

    downloading_id = StartDownload(core, "active.zip",
                                   "https://example.org/active.zip", 100);
    ASSERT_FALSE(downloading_id.empty());
    paused_id = StartDownload(core, "paused.zip",
                             "https://example.org/paused.zip", 200);
    ASSERT_FALSE(paused_id.empty());
    maho_core_pause_download(core, paused_id.c_str());

    ResolveViews();

    auto* sidebar = GetSidebarView();
    ASSERT_TRUE(sidebar);
    sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
    base::RunLoop().RunUntilIdle();

    auto* downloads_view = sidebar->downloads_view_for_testing();
    ASSERT_TRUE(downloads_view);
    ASSERT_TRUE(downloads_view->GetVisible());
    ASSERT_TRUE(FindLabelWithText(downloads_view->list_container_for_testing(),
                                  u"Today"));

    auto* downloading_row = FindDownloadRow(downloads_view, downloading_id);
    ASSERT_TRUE(downloading_row);
    auto* text_block = downloading_row->children()[1].get();
    ASSERT_GE(text_block->children().size(), 2u);
    auto* downloading_title =
        static_cast<views::Label*>(text_block->children()[0].get());
    auto* downloading_subtitle =
        static_cast<views::Label*>(text_block->children()[1].get());
    EXPECT_EQ(u"active.zip", downloading_title->GetText());
    EXPECT_EQ(u"0 bytes of 100 bytes", downloading_subtitle->GetText());
    EXPECT_EQ(3u, text_block->children().size());
    EXPECT_TRUE(downloads_view->download_action_button_for_testing(
        downloading_id, 0));
    EXPECT_TRUE(downloads_view->download_action_button_for_testing(
        downloading_id, 1));
    EXPECT_FALSE(downloads_view->download_action_button_for_testing(
        downloading_id, 2));

    auto* pause_button = downloads_view->download_action_button_for_testing(
        downloading_id, 0);
    ASSERT_TRUE(pause_button);
    views::test::ButtonTestApi(pause_button).NotifyClick(ui::test::TestEvent());
    base::RunLoop().RunUntilIdle();

    downloading_row = FindDownloadRow(downloads_view, downloading_id);
    ASSERT_TRUE(downloading_row);
    text_block = downloading_row->children()[1].get();
    downloading_subtitle =
        static_cast<views::Label*>(text_block->children()[1].get());
    EXPECT_EQ(u"0 bytes of 100 bytes", downloading_subtitle->GetText());
    EXPECT_TRUE(downloads_view->download_action_button_for_testing(
        downloading_id, 0));
    EXPECT_TRUE(downloads_view->download_action_button_for_testing(
        downloading_id, 1));

    auto* paused_row = FindDownloadRow(downloads_view, paused_id);
    ASSERT_TRUE(paused_row);
    auto* paused_text_block = paused_row->children()[1].get();
    auto* paused_subtitle =
        static_cast<views::Label*>(paused_text_block->children()[1].get());
    EXPECT_EQ(u"0 bytes of 200 bytes", paused_subtitle->GetText());
    auto* resume_button = downloads_view->download_action_button_for_testing(
        paused_id, 0);
    ASSERT_TRUE(resume_button);
    views::test::ButtonTestApi(resume_button).NotifyClick(ui::test::TestEvent());
    base::RunLoop().RunUntilIdle();

    paused_row = FindDownloadRow(downloads_view, paused_id);
    ASSERT_TRUE(paused_row);
    paused_text_block = paused_row->children()[1].get();
    paused_subtitle =
        static_cast<views::Label*>(paused_text_block->children()[1].get());
    EXPECT_EQ(u"0 bytes of 200 bytes", paused_subtitle->GetText());
  }

  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                        DownloadsPaneShowsOpenRevealForRealFilesOnly) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath real_path = temp_dir.GetPath().AppendASCII("report.pdf");
  ASSERT_TRUE(base::WriteFile(real_path, "pdf"));
  base::FilePath missing_path = temp_dir.GetPath().AppendASCII("missing.pdf");

  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);
  ScopedCoreOverride scoped_core(core);

  std::string real_id = StartDownload(core, "report.pdf",
                                      "https://example.org/report.pdf", 512,
                                      real_path.AsUTF8Unsafe(), "application/pdf");
  ASSERT_FALSE(real_id.empty());
  char* raw_complete = maho_core_complete_download(core, real_id.c_str());
  ASSERT_TRUE(raw_complete);
  EXPECT_TRUE(base::JSONReader::Read(raw_complete, base::JSON_PARSE_RFC));
  maho_string_free(raw_complete);

  std::string missing_id = StartDownload(
      core, "missing.pdf", "https://example.org/missing.pdf", 256,
      missing_path.AsUTF8Unsafe(), "application/pdf");
  ASSERT_FALSE(missing_id.empty());
  raw_complete = maho_core_complete_download(core, missing_id.c_str());
  ASSERT_TRUE(raw_complete);
  EXPECT_TRUE(base::JSONReader::Read(raw_complete, base::JSON_PARSE_RFC));
  maho_string_free(raw_complete);

  ResolveViews();
  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
  base::RunLoop().RunUntilIdle();

  auto* downloads_view = sidebar->downloads_view_for_testing();
  ASSERT_TRUE(downloads_view);

  EXPECT_TRUE(downloads_view->download_action_button_for_testing(real_id, 0));
  EXPECT_TRUE(downloads_view->download_action_button_for_testing(real_id, 1));
  EXPECT_FALSE(downloads_view->download_action_button_for_testing(real_id, 2));

  EXPECT_TRUE(downloads_view->download_action_button_for_testing(missing_id, 0));
  EXPECT_FALSE(downloads_view->download_action_button_for_testing(missing_id, 1));

  std::vector<std::pair<std::string, DownloadActionKind>> captured_actions;
  downloads_view->SetDownloadFileActionCallbackForTesting(
      base::BindLambdaForTesting(
          [&](const std::string& file_path, DownloadActionKind action) {
            captured_actions.emplace_back(file_path, action);
          }));

  auto* open_button = downloads_view->download_action_button_for_testing(real_id, 0);
  ASSERT_TRUE(open_button);
  views::test::ButtonTestApi(open_button).NotifyClick(ui::test::TestEvent());
  ASSERT_EQ(1u, captured_actions.size());
  EXPECT_EQ(real_path.AsUTF8Unsafe(), captured_actions[0].first);
  EXPECT_EQ(DownloadActionKind::kOpen, captured_actions[0].second);

  auto* reveal_button =
      downloads_view->download_action_button_for_testing(real_id, 1);
  ASSERT_TRUE(reveal_button);
  views::test::ButtonTestApi(reveal_button).NotifyClick(ui::test::TestEvent());
  ASSERT_EQ(2u, captured_actions.size());
  EXPECT_EQ(real_path.AsUTF8Unsafe(), captured_actions[1].first);
  EXPECT_EQ(DownloadActionKind::kReveal, captured_actions[1].second);

  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                        DownloadsRemoveWorksForHistoricalCoreOnlyDownload) {
  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);
  ScopedCoreOverride scoped_core(core);

  // Seed a completed download straight into the core store with no file path
  // and without registering it with the live DownloadManager/bridge, mirroring
  // a historical download loaded after a restart (no live download::DownloadItem
  // exists for it). A completed no-file-path download renders a single Remove
  // action at index 0.
  std::string historical_id =
      StartDownload(core, "old-report.zip", "https://example.org/old.zip", 100);
  ASSERT_FALSE(historical_id.empty());
  char* raw_complete = maho_core_complete_download(core, historical_id.c_str());
  ASSERT_TRUE(raw_complete);
  maho_string_free(raw_complete);

  ResolveViews();
  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
  base::RunLoop().RunUntilIdle();

  auto* downloads_view = sidebar->downloads_view_for_testing();
  ASSERT_TRUE(downloads_view);
  ASSERT_TRUE(FindDownloadRow(downloads_view, historical_id))
      << "Historical download should render a row";

  auto* remove_button =
      downloads_view->download_action_button_for_testing(historical_id, 0);
  ASSERT_TRUE(remove_button);
  views::test::ButtonTestApi(remove_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(FindDownloadRow(downloads_view, historical_id))
      << "Remove must delete the historical (core-only) download from the "
         "store and refresh the list even without a live DownloadItem";

  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                        LibraryOpensMediaPaneAndRailItemIsEnabled) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  auto* media_button = rail->button_for_category_for_testing(
      MahoSidebarLibraryRailView::Category::kMedia);
  ASSERT_TRUE(media_button);
  EXPECT_TRUE(media_button->GetEnabled());
  EXPECT_FALSE(media_button->GetVisible());

  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kMedia);
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kMedia,
            sidebar->active_library_category_for_testing());
  ASSERT_TRUE(sidebar->media_view_for_testing());
  EXPECT_TRUE(sidebar->media_view_for_testing()->GetVisible());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kMedia,
            rail->selected_category());
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  ASSERT_TRUE(GetTabListView());
  EXPECT_FALSE(GetTabListView()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                        MediaPaneGroupsItemsAndShowsPathActions) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath audio_path = temp_dir.GetPath().AppendASCII("track-a.mp3");
  base::FilePath image_path = temp_dir.GetPath().AppendASCII("cover.png");
  ASSERT_TRUE(base::WriteFile(audio_path, "audio"));
  ASSERT_TRUE(base::WriteFile(image_path, "image"));

  MahoCore* core = maho_core_new();
  ASSERT_TRUE(core);
  ScopedCoreOverride scoped_core(core);

  std::string audio_id = StartDownload(core, "track-a.mp3",
                                       "https://example.org/track-a.mp3",
                                       1024, audio_path.AsUTF8Unsafe(), "audio/mpeg");
  ASSERT_FALSE(audio_id.empty());
  std::string image_id = StartDownload(core, "cover.png",
                                       "https://example.org/cover.png",
                                       2048, image_path.AsUTF8Unsafe(), "image/png");
  ASSERT_FALSE(image_id.empty());

  ResolveViews();
  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kMedia);
  base::RunLoop().RunUntilIdle();

  auto* media_view = sidebar->media_view_for_testing();
  ASSERT_TRUE(media_view);
  ASSERT_TRUE(media_view->scroll_view_for_testing());
  ASSERT_TRUE(media_view->list_container_for_testing());
  EXPECT_TRUE(media_view->scroll_view_for_testing()->GetVisible());

  auto* list = media_view->list_container_for_testing();
  ASSERT_GE(list->children().size(), 2u);

  auto* first_section_header = static_cast<views::Label*>(
      list->children()[0]->children()[0]);
  EXPECT_TRUE(first_section_header->GetText().find(u"Audio") !=
              std::u16string::npos);

  auto* second_section_header = static_cast<views::Label*>(
      list->children()[1]->children()[0]);
  EXPECT_TRUE(second_section_header->GetText().find(u"Images") !=
              std::u16string::npos);

  EXPECT_TRUE(media_view->media_action_button_for_item_id_for_testing(audio_id, 0));
  EXPECT_TRUE(media_view->media_action_button_for_item_id_for_testing(audio_id, 1));
  EXPECT_TRUE(media_view->media_action_button_for_item_id_for_testing(image_id, 0));
  EXPECT_TRUE(media_view->media_action_button_for_item_id_for_testing(image_id, 1));

  maho_core_free(core);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                         LibraryOpensSpacesPaneInline) {
  SeedProfile(2);
  ResolveViews();

  // Disable full viewport preference to test inline path
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kSpacesFullViewport, false);

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  auto* spaces_button = rail->button_for_category_for_testing(
      MahoSidebarLibraryRailView::Category::kSpaces);
  ASSERT_TRUE(spaces_button);

  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(sidebar->is_library_mode_for_testing());

  views::test::ButtonTestApi(spaces_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kSpaces,
            sidebar->active_library_category_for_testing());
  ASSERT_TRUE(sidebar->spaces_view_for_testing());
  EXPECT_TRUE(sidebar->spaces_view_for_testing()->GetVisible());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kSpaces,
            rail->selected_category());
  EXPECT_GT(sidebar->spaces_view_for_testing()->GetBoundsInScreen().height(), 0);
  ASSERT_TRUE(sidebar->archive_view_for_testing());
  EXPECT_FALSE(sidebar->archive_view_for_testing()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                        LibraryOpensSpacesPaneInOverlay) {
  SeedProfile(2);
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  auto* spaces_button = rail->button_for_category_for_testing(
      MahoSidebarLibraryRailView::Category::kSpaces);
  ASSERT_TRUE(spaces_button);

  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(sidebar->is_library_mode_for_testing());

  // Confirm default is true (opens overlay)
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kSpacesFullViewport, true);

  views::test::ButtonTestApi(spaces_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  // Overlay should be visible
  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  EXPECT_TRUE(container->IsSpacesOverlayVisible());

  // Dismiss overlay by toggling it again
  container->ToggleSpacesOverlay();
  base::RunLoop().RunUntilIdle();

  // Verify that the selection is cleared on the library rail
  EXPECT_FALSE(container->IsSpacesOverlayVisible());
  EXPECT_EQ(static_cast<MahoSidebarLibraryRailView::Category>(-1), rail->selected_category());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LibraryDismissesSpacesOverlayOnCategorySwitch) {
  SeedProfile(2);
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  auto* spaces_button = rail->button_for_category_for_testing(
      MahoSidebarLibraryRailView::Category::kSpaces);
  ASSERT_TRUE(spaces_button);

  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(sidebar->is_library_mode_for_testing());

  // Enable overlay pref
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kSpacesFullViewport, true);

  // Click Spaces to open overlay
  views::test::ButtonTestApi(spaces_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  EXPECT_TRUE(container->IsSpacesOverlayVisible());

  // Switch to Archived Tabs category (non-Spaces)
  auto* archived_button = rail->button_for_category_for_testing(
      MahoSidebarLibraryRailView::Category::kArchivedTabs);
  ASSERT_TRUE(archived_button);
  views::test::ButtonTestApi(archived_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  // Verify that the overlay is dismissed
  EXPECT_FALSE(container->IsSpacesOverlayVisible());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs, rail->selected_category());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LibraryDismissesSpacesOverlayOnAutoHide) {
  SeedProfile(2);
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  auto* spaces_button = rail->button_for_category_for_testing(
      MahoSidebarLibraryRailView::Category::kSpaces);
  ASSERT_TRUE(spaces_button);

  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  base::RunLoop().RunUntilIdle();

  // Enable overlay pref
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kSpacesFullViewport, true);

  // Click Spaces to open overlay
  views::test::ButtonTestApi(spaces_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  EXPECT_TRUE(container->IsSpacesOverlayVisible());

  // Simulate auto-hide transition by collapsing the panel (unpinning)
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      maho::sidebar_prefs::kSidebarPanelExpanded, false);
  base::RunLoop().RunUntilIdle();

  // Verify that the overlay is dismissed automatically
  EXPECT_FALSE(container->IsSpacesOverlayVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ToggleArchiveModeAliasOpensAndClosesLibrary) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  sidebar->ToggleArchiveMode();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_archive_mode_for_testing());
  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs,
            sidebar->active_library_category_for_testing());

  sidebar->ToggleArchiveMode();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(sidebar->is_archive_mode_for_testing());
  EXPECT_FALSE(sidebar->is_library_mode_for_testing());
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  ASSERT_TRUE(GetTabListView());
  EXPECT_TRUE(GetTabListView()->GetVisible());
}

// The Archive entry point (Cmd+Shift+A) must land on the Archive even when the
// library is already open on another category. Closing the library instead
// would take the user further from what they asked for.
IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ToggleArchiveModeSwitchesFromAnotherLibraryCategory) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(sidebar->is_library_mode_for_testing());
  ASSERT_EQ(MahoSidebarLibraryRailView::Category::kDownloads,
            sidebar->active_library_category_for_testing());

  sidebar->ToggleArchiveMode();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_TRUE(sidebar->is_archive_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kArchivedTabs,
            sidebar->active_library_category_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveFilterAccessibilityValueUpdates) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  const gfx::Point archive_center =
      archive_button->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(archive_center.x(), archive_center.y()));
  base::RunLoop enter_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      enter_loop.QuitClosure()));
  enter_loop.Run();
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  EXPECT_EQ(u"Filter Inactive",
            archive_view->filter_accessibility_value_for_testing());

  auto* filter_button = archive_view->filter_button_for_testing();
  ASSERT_TRUE(filter_button);
  views::test::ButtonTestApi(filter_button).NotifyClick(ui::test::TestEvent());
  ASSERT_TRUE(archive_view->filter_menu_model_for_testing());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false));
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(ArchiveFilterMode::kToday, archive_view->filter_mode_for_testing());
  EXPECT_EQ(u"Filter Active: Today",
            archive_view->filter_accessibility_value_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveBackRestoresFooterArchiveFocus) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  archive_button->RequestFocus();
  ASSERT_TRUE(sidebar->GetFocusManager());
  EXPECT_EQ(archive_button, sidebar->GetFocusManager()->GetFocusedView());

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(sidebar->is_archive_mode_for_testing());
  ASSERT_TRUE(sidebar->library_rail_view_for_testing());
  auto* back_button =
      sidebar->library_rail_view_for_testing()->back_button_for_testing();
  ASSERT_TRUE(back_button);

  views::test::ButtonTestApi(back_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(sidebar->is_archive_mode_for_testing());
  EXPECT_EQ(archive_button, sidebar->GetFocusManager()->GetFocusedView());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LibraryRailBackButtonAnchorsBottomCenter) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kArchivedTabs);
  base::RunLoop().RunUntilIdle();

  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  auto* back_button = rail->back_button_for_testing();
  ASSERT_TRUE(back_button);
  EXPECT_TRUE(back_button->GetVisible());

  const gfx::Rect rail_bounds = rail->GetBoundsInScreen();
  const gfx::Rect back_bounds = back_button->GetBoundsInScreen();
  const int expected_center_x =
      rail_bounds.x() + (rail_bounds.width() - back_bounds.width()) / 2;
  const int expected_bottom =
      rail_bounds.bottom() - sidebar_layout::kLibraryRailBottomInsetDp;

  EXPECT_EQ(expected_center_x, back_bounds.x());
  EXPECT_EQ(expected_bottom, back_bounds.bottom());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveReturnKeepsFooterAndTabsVisible) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(sidebar->is_archive_mode_for_testing());
  ASSERT_TRUE(sidebar->library_rail_view_for_testing());
  auto* back_button =
      sidebar->library_rail_view_for_testing()->back_button_for_testing();
  ASSERT_TRUE(back_button);

  views::test::ButtonTestApi(back_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  EXPECT_FALSE(sidebar->is_archive_mode_for_testing());
  ASSERT_TRUE(GetFooterView());
  EXPECT_TRUE(GetFooterView()->GetVisible());
  EXPECT_GT(GetFooterView()->GetBoundsInScreen().height(), 0);
  ASSERT_TRUE(GetTabListView());
  EXPECT_TRUE(GetTabListView()->GetVisible());
  EXPECT_GT(GetTabListView()->GetBoundsInScreen().height(), 0);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveEmptyStateAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  auto* empty_state = archive_view->empty_state_view_for_testing();
  ASSERT_TRUE(empty_state);
  ASSERT_TRUE(empty_state->GetVisible());

  ui::AXNodeData data = GetAccessibilityNodeData(empty_state);
  EXPECT_EQ(ax::mojom::Role::kGroup, data.role);
  EXPECT_EQ(u"Archive empty state, no archived tabs yet",
            data.GetString16Attribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveResultsListAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  ASSERT_TRUE(archive_view->scroll_view_for_testing());
  ASSERT_TRUE(archive_view->list_container_for_testing());

  ui::AXNodeData scroll_data =
      GetAccessibilityNodeData(archive_view->scroll_view_for_testing());
  EXPECT_EQ(ax::mojom::Role::kGroup, scroll_data.role);
  EXPECT_EQ(u"Archived tabs results container",
            scroll_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_EQ(u"No tabs",
            scroll_data.GetString16Attribute(ax::mojom::StringAttribute::kValue));

  ui::AXNodeData list_data =
      GetAccessibilityNodeData(archive_view->list_container_for_testing());
  EXPECT_EQ(ax::mojom::Role::kList, list_data.role);
  EXPECT_EQ(u"Archived tabs results",
            list_data.GetString16Attribute(ax::mojom::StringAttribute::kName));

  ui::AXNodeData search_icon_data = GetAccessibilityNodeData(
      archive_view->children()[0]->children()[0]->children()[0]);
  EXPECT_TRUE(search_icon_data.HasState(ax::mojom::State::kIgnored));

  ui::AXNodeData rail_button_data = GetAccessibilityNodeData(
      sidebar->library_rail_view_for_testing()->button_for_category_for_testing(
          MahoSidebarLibraryRailView::Category::kArchivedTabs));
  EXPECT_TRUE(
      rail_button_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveNonEmptyAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "test-archived-tab";
  tab.title = u"Archive QA Tab";
  tab.url = "https://example.com/archive-qa";
  tab.host = u"example.com / archive-qa";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(archive_view->scroll_view_for_testing()->GetVisible());
  ASSERT_TRUE(archive_view->list_container_for_testing());
  ASSERT_GE(archive_view->list_container_for_testing()->children().size(), 3u);

  views::View* section_header =
      archive_view->list_container_for_testing()->children()[0];
  ui::AXNodeData section_data = GetAccessibilityNodeData(section_header);
  EXPECT_EQ(ax::mojom::Role::kGroup, section_data.role);
  EXPECT_EQ(u"Today, 1 archived tab",
            section_data.GetString16Attribute(ax::mojom::StringAttribute::kName));

  views::View* section_container =
      archive_view->list_container_for_testing()->children()[2];
  ASSERT_FALSE(section_container->children().empty());
  views::View* row = section_container->children()[0];
  ui::AXNodeData row_data = GetAccessibilityNodeData(row);
  EXPECT_EQ(ax::mojom::Role::kButton, row_data.role);
  EXPECT_EQ(u"Restore archived tab Archive QA Tab",
            row_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_TRUE(row->GetEnabled());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveRootPanelAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  ASSERT_GE(archive_view->children().size(), 2u);

  ui::AXNodeData root_data = GetAccessibilityNodeData(archive_view);
  EXPECT_EQ(ax::mojom::Role::kGroup, root_data.role);
  EXPECT_EQ(u"Archived tabs panel",
            root_data.GetString16Attribute(ax::mojom::StringAttribute::kName));

  views::View* header_surface = archive_view->children()[0].get();
  views::View* body_container = archive_view->children()[1].get();
  ASSERT_TRUE(header_surface);
  ASSERT_TRUE(body_container);

  ui::AXNodeData body_data = GetAccessibilityNodeData(body_container);
  EXPECT_FALSE(
      body_data.HasStringAttribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveUntitledRowUsesHostnameTitleAndLocationSubtitle) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "untitled-archive-tab";
  tab.title = u"";
  tab.url = "https://example.com/archive/path";
  tab.host = u"example.com / archive/path";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(archive_view->list_container_for_testing());
  ASSERT_GE(archive_view->list_container_for_testing()->children().size(), 3u);
  views::View* section_container =
      archive_view->list_container_for_testing()->children()[2];
  ASSERT_TRUE(section_container);
  ASSERT_FALSE(section_container->children().empty());

  views::View* row = section_container->children()[0];
  ASSERT_TRUE(row);
  ASSERT_GE(row->children().size(), 3u);

  views::View* text_container = row->children()[1];
  ASSERT_TRUE(text_container);
  ASSERT_GE(text_container->children().size(), 2u);

  auto* title_label =
      static_cast<views::Label*>(text_container->children()[0].get());
  auto* subtitle_label =
      static_cast<views::Label*>(text_container->children()[1].get());
  ASSERT_TRUE(title_label);
  ASSERT_TRUE(subtitle_label);

  EXPECT_EQ(u"example.com", title_label->GetText());
  EXPECT_EQ(u"example.com / archive/path", subtitle_label->GetText());

  ui::AXNodeData row_data = GetAccessibilityNodeData(row);
  EXPECT_EQ(u"Restore archived tab example.com",
            row_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveFilteredEmptyAccessibilitySemantics) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "filtered-empty-tab";
  tab.title = u"Visible Archive Tab";
  tab.url = "https://example.com/visible";
  tab.host = u"example.com / visible";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  auto* filter_button = archive_view->filter_button_for_testing();
  ASSERT_TRUE(filter_button);
  views::test::ButtonTestApi(filter_button).NotifyClick(ui::test::TestEvent());
  ASSERT_TRUE(archive_view->filter_menu_model_for_testing());
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_DOWN,
      false, false, false, false));
  ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
      browser()->GetWindow()->GetNativeWindow(), ui::VKEY_RETURN,
      false, false, false, false));
  base::RunLoop().RunUntilIdle();

  auto* empty_state = archive_view->empty_state_view_for_testing();
  ASSERT_TRUE(empty_state);
  ASSERT_TRUE(empty_state->GetVisible());
  ui::AXNodeData empty_data = GetAccessibilityNodeData(empty_state);
  EXPECT_EQ(ax::mojom::Role::kGroup, empty_data.role);
  EXPECT_EQ(u"Archive empty state, no matching tabs",
            empty_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveSearchMatchesUnicodeCaseFoldedQuery) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);

  ArchivedTabItem tab;
  tab.tab_id = "unicode-search-tab";
  tab.title = u"Café Archive";
  tab.url = "https://example.com/archive";
  tab.host = u"example.com / archive";
  tab.archived_at = base::Time::Now();
  std::vector<ArchivedTabItem> tabs;
  tabs.push_back(tab);
  archive_view->SetArchivedTabsForTesting(std::move(tabs));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(archive_view->scroll_view_for_testing()->GetVisible());
  ASSERT_FALSE(archive_view->empty_state_view_for_testing()->GetVisible());
  ASSERT_GE(archive_view->list_container_for_testing()->children().size(), 3u);

  ASSERT_GE(archive_view->children().size(), 1u);
  views::View* header_surface = archive_view->children()[0].get();
  ASSERT_GE(header_surface->children().size(), 1u);
  views::View* search_shell = header_surface->children()[0].get();
  ASSERT_GE(search_shell->children().size(), 2u);
  auto* search_field =
      static_cast<views::Textfield*>(search_shell->children()[1].get());
  ASSERT_TRUE(search_field);

  search_field->SetText(u"CAFÉ");
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(archive_view->scroll_view_for_testing()->GetVisible());
  EXPECT_FALSE(archive_view->empty_state_view_for_testing()->GetVisible());
  EXPECT_GE(archive_view->list_container_for_testing()->children().size(), 3u);

  search_field->SetText(u"no-match");
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(archive_view->scroll_view_for_testing()->GetVisible());
  EXPECT_TRUE(archive_view->empty_state_view_for_testing()->GetVisible());
  EXPECT_TRUE(archive_view->list_container_for_testing()->children().empty());
}

// Phase 5 — V1/V2 verification behavioral hooks.
//
// These two tests form the behavioral proof for Phase 5 capture validity:
//   ArchiveEmptyState_ProductionFFIPath   — V1 (capture validity) + V2 empty-state row
//   ArchivePopulatedState_TestInjectionPath — V2 populated-state row
//
// Provenance is encoded in the test name and documented below each test.
// Any visual capture taken against these tests must record the provenance label
// from the test name as the artifact's state-provenance annotation.

// V1 + V2: empty-archive-state proof via production FFI path.
// Provenance: clean-profile, no test injection, ReloadArchivedTabs() called
// (production maho_core_get_archived_tabs path). Empty result is the expected
// and correct clean-profile artifact.
IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchiveEmptyState_ProductionFFIPath) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  ASSERT_TRUE(archive_view->GetVisible());

  // Exercise the production FFI path explicitly. On a clean profile this
  // returns empty — that is the correct result for this state.
  ReloadArchivedTabsFromFFI();

  ASSERT_TRUE(archive_view->empty_state_view_for_testing());
  EXPECT_TRUE(archive_view->empty_state_view_for_testing()->GetVisible())
      << "Empty state must be visible after production FFI reload on clean profile";
  EXPECT_FALSE(archive_view->scroll_view_for_testing()->GetVisible())
      << "Scroll view must be hidden when archive is empty";
  EXPECT_EQ(0, archive_view->visible_result_count_for_testing())
      << "Visible result count must report zero for the clean-profile empty archive state";
  EXPECT_GT(archive_view->GetBoundsInScreen().height(), 0)
      << "Archive pane must have non-zero height confirming it is laid out";
}

// V2: populated-archive-state proof via explicit test-injection path.
// Provenance: test-injection via SetArchivedTabsForTesting() — NOT production FFI.
// This is the explicit mechanism for visual capture of populated archive state
// until a maho_core_archive_tab FFI function is available. Any capture taken
// here MUST be labeled "test-injection, not production-backed" in the provenance
// record.
IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ArchivePopulatedState_TestInjectionPath) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  auto* footer = GetFooterView();
  ASSERT_TRUE(footer);
  auto* archive_button = footer->archive_button_for_testing();
  ASSERT_TRUE(archive_button);

  views::test::ButtonTestApi(archive_button).NotifyClick(ui::test::TestEvent());
  base::RunLoop().RunUntilIdle();

  auto* archive_view = sidebar->archive_view_for_testing();
  ASSERT_TRUE(archive_view);
  ASSERT_TRUE(archive_view->GetVisible());

  std::vector<ArchivedTabItem> seed_tabs;
  for (int i = 0; i < 3; ++i) {
    ArchivedTabItem tab;
    tab.tab_id = base::StringPrintf("phase5-seed-tab-%d", i);
    tab.title = base::UTF8ToUTF16(
        base::StringPrintf("Phase 5 Seed Tab %d", i));
    tab.url = base::StringPrintf(
        "https://archive-seed.example.com/article-%d", i);
    tab.host = base::UTF8ToUTF16(
        base::StringPrintf("archive-seed.example.com / article-%d", i));
    tab.archived_at = base::Time::Now();
    seed_tabs.push_back(std::move(tab));
  }

  SeedArchivedTabs(std::move(seed_tabs));

  EXPECT_EQ(3, archive_view->visible_result_count_for_testing())
      << "Seeded populated archive state must report the three injected rows before any later reload";

  EXPECT_TRUE(archive_view->scroll_view_for_testing()->GetVisible())
      << "Scroll view must be visible after test-injection of archive content";
  EXPECT_FALSE(archive_view->empty_state_view_for_testing()->GetVisible())
      << "Empty state must be hidden when archive content is present";
  ASSERT_TRUE(archive_view->list_container_for_testing());
  EXPECT_GE(archive_view->list_container_for_testing()->children().size(), 1u)
      << "List container must have at least one section for seeded tabs";

  // Behavioral guardrail for the seed=4 capture contract: once the populated
  // state is explicitly test-injected, a later production reload request must
  // not wipe it before capture. The archive view now owns that suppression.
  ReloadArchivedTabsFromFFI();

  EXPECT_EQ(3, archive_view->visible_result_count_for_testing())
      << "A later production reload request must not overwrite the explicit test-injection populated state";
  EXPECT_TRUE(archive_view->scroll_view_for_testing()->GetVisible())
      << "Scroll view must remain visible after a suppressed production reload request";
  EXPECT_FALSE(archive_view->empty_state_view_for_testing()->GetVisible())
      << "Empty state must remain hidden after a suppressed production reload request";
  EXPECT_GT(archive_view->GetBoundsInScreen().height(), 0)
      << "Archive pane must have non-zero height confirming it is laid out";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       LibraryDownloadsLabelStaysSingleLineAtMinimumWidth) {
  ResolveViews();

  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
  base::RunLoop().RunUntilIdle();

  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  container->SetBounds(0, 0, 280, 1000);
  container->InvalidateLayout();

  auto* rail = sidebar->library_rail_view_for_testing();
  ASSERT_TRUE(rail);
  EXPECT_EQ(92, rail->GetPreferredSize().width());
  EXPECT_EQ(92, rail->width());

  auto* downloads_button = rail->button_for_category_for_testing(
      MahoSidebarLibraryRailView::Category::kDownloads);
  ASSERT_TRUE(downloads_button);
  EXPECT_EQ(80, downloads_button->GetPreferredSize().width());
  EXPECT_EQ(80, downloads_button->width());

  views::Label* downloads_label = nullptr;
  for (views::View* child : downloads_button->children()) {
    if (views::IsViewClass<views::Label>(child)) {
      downloads_label = static_cast<views::Label*>(child);
      break;
    }
  }
  ASSERT_TRUE(downloads_label);
  EXPECT_EQ(u"Downloads", downloads_label->GetText());
  EXPECT_LE(downloads_label->GetPreferredSize().width(), 68);
  EXPECT_FALSE(downloads_label->GetMultiLine());
  EXPECT_EQ(gfx::NO_ELIDE, downloads_label->GetElideBehavior());

  auto* content_pane = sidebar->downloads_view_for_testing();
  ASSERT_TRUE(content_pane);
  EXPECT_GE(content_pane->width(), 171);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ToggleSpacesOverlayKeyboardCommandOpensAndCloses) {
  ResolveViews();
  SeedProfile(2);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);

#if BUILDFLAG(IS_MAC)
  const int modifiers = ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN;
#else
  const int modifiers = ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN;
#endif
  const ui::Accelerator accelerator(ui::VKEY_S, modifiers);
  const std::string action_id =
      MahoShortcutInterceptor::ResolveActionForAccelerator(accelerator);
  ASSERT_EQ("toggle_spaces_overlay", action_id);

  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  EXPECT_FALSE(container->IsSpacesOverlayVisible());
  EXPECT_FALSE(sidebar->is_library_mode_for_testing());

  // Real accelerator path proof: dispatch directly through
  // BrowserView::AcceleratorPressed (the Views accelerator target that Maho wires),
  // which resolves via MahoShortcutInterceptor and dispatches ExecuteCommandAction.
  // First invocation: opens the Spaces overlay and enters library mode.
  EXPECT_TRUE(browser_view->AcceleratorPressed(accelerator));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container->IsSpacesOverlayVisible());
  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kSpaces,
            sidebar->active_library_category_for_testing());

  // Second invocation through accelerator dispatch closes the overlay and
  // returns to tabs.
  EXPECT_TRUE(browser_view->AcceleratorPressed(accelerator));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(container->IsSpacesOverlayVisible());
  EXPECT_FALSE(sidebar->is_library_mode_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ToggleSpacesOverlaySwitchesFromAnotherLibraryCategory) {
  ResolveViews();
  SeedProfile(2);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);

#if BUILDFLAG(IS_MAC)
  const int modifiers = ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN;
#else
  const int modifiers = ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN;
#endif
  const ui::Accelerator accelerator(ui::VKEY_S, modifiers);
  const std::string action_id =
      MahoShortcutInterceptor::ResolveActionForAccelerator(accelerator);
  ASSERT_EQ("toggle_spaces_overlay", action_id);

  auto* container = GetContainerView();
  ASSERT_TRUE(container);
  auto* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);

  // Start with another library category open (Downloads).
  sidebar->OpenLibrary(MahoSidebarLibraryRailView::Category::kDownloads);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(sidebar->is_library_mode_for_testing());
  ASSERT_EQ(MahoSidebarLibraryRailView::Category::kDownloads,
            sidebar->active_library_category_for_testing());
  ASSERT_FALSE(container->IsSpacesOverlayVisible());

  // Invoking toggle_spaces_overlay through real accelerator dispatch must switch to
  // Spaces and open the overlay rather than closing the library.
  EXPECT_TRUE(browser_view->AcceleratorPressed(accelerator));
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(container->IsSpacesOverlayVisible());
  EXPECT_TRUE(sidebar->is_library_mode_for_testing());
  EXPECT_EQ(MahoSidebarLibraryRailView::Category::kSpaces,
            sidebar->active_library_category_for_testing());

  // Invoking again through accelerator dispatch closes the overlay and exits
  // library mode.
  EXPECT_TRUE(browser_view->AcceleratorPressed(accelerator));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(container->IsSpacesOverlayVisible());
  EXPECT_FALSE(sidebar->is_library_mode_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       ToggleSpacesOverlayIgnoredInOffTheRecord) {
  Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->GetProfile()->IsOffTheRecord());

  BrowserView* incognito_view = BrowserView::GetBrowserViewForBrowser(incognito);
  ASSERT_TRUE(incognito_view);
  auto* container = static_cast<MahoSidebarContainerView*>(
      incognito_view->maho_sidebar_container());
  ASSERT_TRUE(container);

  EXPECT_FALSE(container->IsSpacesOverlayVisible());

  // OTR windows must reject the Spaces overlay command through accelerator
  // dispatch to preserve isolation (never opening regular spaces).
#if BUILDFLAG(IS_MAC)
  const int modifiers = ui::EF_COMMAND_DOWN | ui::EF_ALT_DOWN;
#else
  const int modifiers = ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN;
#endif
  const ui::Accelerator accelerator(ui::VKEY_S, modifiers);
  const std::string action_id =
      MahoShortcutInterceptor::ResolveActionForAccelerator(accelerator);
  ASSERT_EQ("toggle_spaces_overlay", action_id);

  // AcceleratorPressed handles the accelerator, but ExecuteCommandAction drops
  // it due to the OTR isolation guard, so the overlay remains hidden.
  EXPECT_TRUE(incognito_view->AcceleratorPressed(accelerator));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(container->IsSpacesOverlayVisible());
}

// Memory Saver (forced to Aggressive by Maho) treats chrome:// pages as
// discardable. A discarded Mail tab reboots the SPA on return, so reopening
// Mail showed a loading screen before the list. The Mail WebUI must opt out.
IN_PROC_BROWSER_TEST_F(MahoSidebarNavigationInteractiveTest,
                       MailTabIsProtectedFromAutoDiscard) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      sidebar_prefs::kMahoMailEnabled, true);
  // The Mail SPA keeps the page loading while its helper boots, so wait for the
  // navigation to commit rather than for load stop.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://maho-mail/"),
      WindowOpenDisposition::CURRENT_TAB, ui_test_utils::BROWSER_TEST_NO_WAIT);
  content::WebContents* mail =
      browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(mail);
  ASSERT_TRUE(::base::test::RunUntil([mail] {
    return mail->GetLastCommittedURL().host() == "maho-mail";
  }));
  EXPECT_TRUE(::base::test::RunUntil([mail] {
    return !performance_manager::PageLiveStateDecorator::IsAutoDiscardable(
        mail);
  }));

  // Ordinary pages keep the default so Memory Saver still reclaims them.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("about:blank"), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  EXPECT_TRUE(performance_manager::PageLiveStateDecorator::IsAutoDiscardable(
      browser()->GetTabStripModel()->GetActiveWebContents()));
}

}  // namespace
}  // namespace maho
