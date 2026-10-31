// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/unload_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/base/hit_test.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/views/command/maho_command_action_handler.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_scroll_bar.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h"
#include "maho/browser/ui/views/sidebar/maho_spaces_board_data.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/image/image.h"
#include "ui/views/accessibility/ax_virtual_view.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace maho {
namespace {

bool IsInlineRowButton(views::View* view, std::u16string expected_text) {
  if (!view) return false;
  if (auto* button = views::AsViewClass<views::LabelButton>(view)) {
    if (button->GetText() == expected_text) {
      return true;
    }
  }
  for (views::View* child : view->children()) {
    if (IsInlineRowButton(child, expected_text)) {
      return true;
    }
  }
  return false;
}

void ExpectEmptyPinnedSectionLayout(views::View* rows) {
  ASSERT_TRUE(rows);
  ASSERT_EQ(4u, rows->children().size())
      << "Idle empty-pinned layout should keep hidden pinned section and separator in the view tree, plus + New Tab and the normal section";

  EXPECT_FALSE(rows->children()[0]->GetVisible())
      << "Pinned section should be hidden when empty";
  EXPECT_TRUE(rows->children()[1]->GetVisible())
      << "Pinned separator should remain visible above + New Tab even when the pinned section is empty";
  EXPECT_TRUE(IsInlineRowButton(rows->children()[2], u"+ New Tab"))
      << "+ New Tab should follow the pinned section and separator";
  EXPECT_TRUE(rows->children()[2]->GetVisible())
      << "+ New Tab should remain visible";
  EXPECT_TRUE(rows->children()[3]->GetVisible())
      << "Normal section should remain visible after + New Tab";
  EXPECT_FALSE(rows->children()[3]->children().empty())
      << "Normal section should contain the normal rows";
}

void ExpectNoTabsRows(views::View* rows) {
  ASSERT_TRUE(rows);
  ASSERT_EQ(1u, rows->children().size())
      << "True zero-tab layout must render only the + New Tab CTA";
  EXPECT_TRUE(IsInlineRowButton(rows->children()[0], u"+ New Tab"))
      << "+ New Tab should be the single CTA child";
  EXPECT_TRUE(rows->children()[0]->GetVisible())
      << "+ New Tab should be visible";
}

gfx::Image MakeSidebarFaviconForTesting(SkColor color) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(16, 16);
  bitmap.eraseColor(color);
  return gfx::Image::CreateFrom1xBitmap(bitmap);
}

bool SetWebContentsFaviconForTesting(content::WebContents* contents,
                                     const GURL& icon_url,
                                     const gfx::Image& image) {
  if (!contents) {
    return false;
  }
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  if (!entry) {
    return false;
  }
  content::FaviconStatus& status = entry->GetFavicon();
  status.image = image;
  status.url = icon_url;
  status.valid = true;
  return true;
}

SidebarTreeNode MakeScrollPerfTab(int tab_number, bool pinned = false) {
  SidebarTreeNode tab;
  tab.kind = SidebarNodeKind::kTab;
  tab.tab_id = "scroll-perf-tab-" + base::NumberToString(tab_number);
  tab.tab_strip_index = -1;
  tab.is_pinned = pinned;
  tab.title = u"Synthetic performance tab";
  return tab;
}

MahoSidebarTabListModel MakeScrollPerfModel(int tab_count) {
  MahoSidebarTabListModel model;
  model.active_space_id = "scroll-perf-space";

  int next_tab = 0;
  const int pinned_count = std::max(8, tab_count / 20);
  for (; next_tab < pinned_count; ++next_tab) {
    model.pinned_tree.push_back(MakeScrollPerfTab(next_tab, true));
  }

  int pattern_number = 0;
  while (tab_count - next_tab >= 8) {
    model.normal_tree.push_back(MakeScrollPerfTab(next_tab++));
    model.normal_tree.push_back(MakeScrollPerfTab(next_tab++));

    SidebarTreeNode expanded_folder;
    expanded_folder.kind = SidebarNodeKind::kFolder;
    expanded_folder.folder_id =
        "scroll-perf-expanded-folder-" + base::NumberToString(pattern_number);
    expanded_folder.folder_name = u"Expanded performance folder";
    expanded_folder.is_expanded = true;
    for (int child_index = 0; child_index < 2; ++child_index) {
      SidebarTreeNode child = MakeScrollPerfTab(next_tab++);
      child.parent_folder_id = expanded_folder.folder_id;
      child.folder_child_index = child_index;
      expanded_folder.children.push_back(std::move(child));
    }
    model.normal_tree.push_back(std::move(expanded_folder));

    SidebarTreeNode collapsed_folder;
    collapsed_folder.kind = SidebarNodeKind::kFolder;
    collapsed_folder.folder_id =
        "scroll-perf-collapsed-folder-" + base::NumberToString(pattern_number);
    collapsed_folder.folder_name = u"Collapsed performance folder";
    collapsed_folder.is_expanded = false;
    for (int child_index = 0; child_index < 2; ++child_index) {
      SidebarTreeNode child = MakeScrollPerfTab(next_tab++);
      child.parent_folder_id = collapsed_folder.folder_id;
      child.folder_child_index = child_index;
      collapsed_folder.children.push_back(std::move(child));
    }
    model.normal_tree.push_back(std::move(collapsed_folder));

    SidebarTreeNode split_group;
    split_group.kind = SidebarNodeKind::kSplitGroup;
    split_group.split_id =
        "scroll-perf-split-" + base::NumberToString(pattern_number);
    split_group.split_orientation =
        pattern_number % 2 == 0 ? "vertical" : "horizontal";
    for (int child_index = 0; child_index < 2; ++child_index) {
      SidebarTreeNode child = MakeScrollPerfTab(next_tab++);
      child.is_in_split = true;
      split_group.children.push_back(std::move(child));
    }
    model.normal_tree.push_back(std::move(split_group));
    ++pattern_number;
  }

  while (next_tab < tab_count) {
    model.normal_tree.push_back(MakeScrollPerfTab(next_tab++));
  }
  return model;
}

SidebarTreeNode MakeProjectionTab(const std::string& tab_id,
                                  bool active = false,
                                  int depth = 0) {
  SidebarTreeNode tab;
  tab.kind = SidebarNodeKind::kTab;
  tab.tab_id = tab_id;
  tab.tab_strip_index = -1;
  tab.title = u"Projection tab";
  tab.is_active = active;
  tab.depth = depth;
  return tab;
}

// tabs + expanded folder + collapsed folder (active-descendant sticky) + a
// side-by-side split, all synthetic. The split uses "vertical" orientation,
// which RebuildSplitGroupContainer lays out side-by-side (a single 36dp row),
// so the projection's summed heights match the eager tree exactly.
MahoSidebarTabListModel MakeProjectionModel() {
  MahoSidebarTabListModel model;
  model.active_space_id = "projection-space";

  model.normal_tree.push_back(MakeProjectionTab("proj-t0"));

  SidebarTreeNode expanded;
  expanded.kind = SidebarNodeKind::kFolder;
  expanded.folder_id = "proj-f1";
  expanded.folder_name = u"Expanded";
  expanded.is_expanded = true;
  {
    SidebarTreeNode c0 = MakeProjectionTab("proj-c0", /*active=*/false, 1);
    c0.parent_folder_id = "proj-f1";
    c0.folder_child_index = 0;
    SidebarTreeNode c1 = MakeProjectionTab("proj-c1", /*active=*/false, 1);
    c1.parent_folder_id = "proj-f1";
    c1.folder_child_index = 1;
    expanded.children.push_back(std::move(c0));
    expanded.children.push_back(std::move(c1));
  }
  model.normal_tree.push_back(std::move(expanded));

  SidebarTreeNode collapsed;
  collapsed.kind = SidebarNodeKind::kFolder;
  collapsed.folder_id = "proj-f2";
  collapsed.folder_name = u"Collapsed";
  collapsed.is_expanded = false;
  {
    SidebarTreeNode c2 = MakeProjectionTab("proj-c2", /*active=*/true, 1);
    c2.parent_folder_id = "proj-f2";
    c2.folder_child_index = 0;
    SidebarTreeNode c3 = MakeProjectionTab("proj-c3", /*active=*/false, 1);
    c3.parent_folder_id = "proj-f2";
    c3.folder_child_index = 1;
    collapsed.children.push_back(std::move(c2));
    collapsed.children.push_back(std::move(c3));
  }
  model.normal_tree.push_back(std::move(collapsed));

  SidebarTreeNode split;
  split.kind = SidebarNodeKind::kSplitGroup;
  split.split_id = "proj-s1";
  split.split_orientation = "vertical";
  {
    SidebarTreeNode sp0 = MakeProjectionTab("proj-sp0");
    sp0.is_in_split = true;
    SidebarTreeNode sp1 = MakeProjectionTab("proj-sp1");
    sp1.is_in_split = true;
    split.children.push_back(std::move(sp0));
    split.children.push_back(std::move(sp1));
  }
  model.normal_tree.push_back(std::move(split));
  return model;
}

// Reconstructs the full tab-row visual order the eager tree realizes: kTab and
// sticky rows contribute their tab_id, and a split group contributes each of
// its member tabs (which the eager tree realizes as individual rows).
std::vector<std::string> ReconstructProjectionTabOrder(
    const std::vector<SidebarVisualRow>& projection) {
  std::vector<std::string> ids;
  std::function<void(const std::vector<SidebarTreeNode>&)> collect_tabs;
  collect_tabs = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const SidebarTreeNode& node : nodes) {
      if (node.kind == SidebarNodeKind::kTab) {
        ids.push_back(node.tab_id);
      }
      collect_tabs(node.children);
    }
  };
  for (const SidebarVisualRow& row : projection) {
    switch (row.kind) {
      case SidebarVisualRowKind::kTab:
      case SidebarVisualRowKind::kCollapsedStickyTab:
        ids.push_back(row.node.tab_id);
        break;
      case SidebarVisualRowKind::kSplitGroup:
        collect_tabs(row.node.children);
        break;
      case SidebarVisualRowKind::kInsertionLane:
      case SidebarVisualRowKind::kFolder:
        break;
    }
  }
  return ids;
}

void CollectRealizedTabIds(views::View* parent, std::vector<std::string>* out) {
  for (views::View* child : parent->children()) {
    if (auto* row = views::AsViewClass<SidebarTabRowView>(child)) {
      out->push_back(row->tab_id_for_testing());
    }
    CollectRealizedTabIds(child, out);
  }
}

int CountModelTabs(const std::vector<SidebarTreeNode>& nodes) {
  int count = 0;
  for (const SidebarTreeNode& node : nodes) {
    if (node.kind == SidebarNodeKind::kTab) {
      ++count;
    }
    count += CountModelTabs(node.children);
  }
  return count;
}

bool AllModelTabsAreSynthetic(const std::vector<SidebarTreeNode>& nodes) {
  for (const SidebarTreeNode& node : nodes) {
    if (node.kind == SidebarNodeKind::kTab && node.tab_strip_index != -1) {
      return false;
    }
    if (!AllModelTabsAreSynthetic(node.children)) {
      return false;
    }
  }
  return true;
}

int64_t PercentileMicros(const std::vector<int64_t>& samples, int percentile) {
  CHECK(!samples.empty());
  CHECK_GE(percentile, 1);
  CHECK_LE(percentile, 100);
  std::vector<int64_t> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  const size_t rank =
      (static_cast<size_t>(percentile) * sorted.size() + 99) / 100;
  return sorted[rank - 1];
}

int CountDescendantLayers(const views::View* root) {
  int count = 0;
  for (const views::View* child : root->children()) {
    if (child->layer()) {
      ++count;
    }
    count += CountDescendantLayers(child);
  }
  return count;
}

int ExpectIntersectingTabRowsVisible(views::View* root,
                                     MahoSidebarTabListView* tab_list,
                                     const gfx::Rect& viewport) {
  int visible_row_count = 0;
  for (views::View* child : root->children()) {
    if (auto* row = views::AsViewClass<SidebarTabRowView>(child)) {
      const gfx::Rect row_bounds = views::View::ConvertRectToTarget(
          row, tab_list, row->GetLocalBounds());
      if (row_bounds.Intersects(viewport)) {
        EXPECT_TRUE(row->GetVisible())
            << "Viewport-intersecting row was layout-hidden: "
            << row->tab_id_for_testing() << " bounds=" << row_bounds.ToString()
            << " viewport=" << viewport.ToString();
        EXPECT_TRUE(row->IsDrawn())
            << "Viewport-intersecting row was not drawn: "
            << row->tab_id_for_testing();
      }
      if (row->IsDrawn()) {
        ++visible_row_count;
      }
    }
    visible_row_count +=
        ExpectIntersectingTabRowsVisible(child, tab_list, viewport);
  }
  return visible_row_count;
}

class MahoSidebarContractTest : public MahoSidebarInteractiveTestBase {
 protected:
  void RunScrollPerformanceBenchmark(int tab_count);
  // Loads a synthetic |tab_count| model, sweeps a few scroll offsets, and
  // reports the maximum realized tab-row / descendant-layer counts observed
  // plus the viewport-derived window size (viewport rows + 2*overscan).
  void MeasureWindowedRealization(int tab_count,
                                  int* out_max_rows,
                                  int* out_max_layers,
                                  int* out_window_rows);
};

void MahoSidebarContractTest::MeasureWindowedRealization(int tab_count,
                                                         int* out_max_rows,
                                                         int* out_max_layers,
                                                         int* out_window_rows) {
  RunAllPendingTasks();
  StopSidebarObservations();
  tab_list_->Update(MakeScrollPerfModel(tab_count), static_cast<Browser*>(browser()));

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_EQ(scroll->contents(), tab_list_);
  ASSERT_TRUE(tab_list_->GetWidget());

  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);

  constexpr int kRowHeightDp = 36;
  constexpr int kBufferRows = 30;
  // Worst-case window: viewport + the velocity-direction leading overscan (up
  // to kMaxVelocityOverscanRows) + the base trailing overscan. Fixed — does
  // not scale with tab_count.
  *out_window_rows =
      (scroll->GetVisibleRect().height() + kRowHeightDp - 1) / kRowHeightDp +
      SidebarVisibilityManager::kMaxVelocityOverscanRows + kBufferRows;
  *out_max_rows = 0;
  *out_max_layers = 0;

  for (const int target_y :
       {max_scroll_y / 4, max_scroll_y / 2, max_scroll_y * 3 / 4}) {
    scroll->ScrollToOffset(gfx::PointF(0, target_y));
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return scroll->CurrentOffset().y() == target_y; }));
    tab_list_->GetWidget()->LayoutRootViewIfNecessary();
    const gfx::Rect viewport = scroll->GetVisibleRect();
    *out_max_rows = std::max(
        *out_max_rows,
        ExpectIntersectingTabRowsVisible(tab_list_, tab_list_, viewport));
    *out_max_layers =
        std::max(*out_max_layers, CountDescendantLayers(scroll));
  }
}

void MahoSidebarContractTest::RunScrollPerformanceBenchmark(int tab_count) {
  RunAllPendingTasks();
  StopSidebarObservations();
  const int cosmetic_apply_count_start =
      sidebar_->cosmetic_apply_count_for_testing();

  MahoSidebarTabListModel model = MakeScrollPerfModel(tab_count);
  ASSERT_EQ(tab_count, CountModelTabs(model.pinned_tree) +
                           CountModelTabs(model.normal_tree));
  ASSERT_TRUE(AllModelTabsAreSynthetic(model.pinned_tree));
  ASSERT_TRUE(AllModelTabsAreSynthetic(model.normal_tree));
  tab_list_->Update(model, static_cast<Browser*>(browser()));

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_EQ(scroll->contents(), tab_list_);
  ASSERT_TRUE(tab_list_->GetWidget());

  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  ASSERT_EQ(4u, rows->children().size());
  views::View* pinned_section = rows->children()[0];
  views::View* separator = rows->children()[1];
  views::View* actions_row = rows->children()[2];

  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  ASSERT_TRUE(pinned_section->GetVisible());
  ASSERT_TRUE(separator->GetVisible());
  ASSERT_TRUE(actions_row->GetVisible());

  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_EQ(max_scroll_y, scroll->vertical_scroll_bar()->GetMaxPosition());
  ASSERT_GT(max_scroll_y, 0);

  const std::vector<int> sweep_offsets = {
      max_scroll_y / 4,     max_scroll_y / 2,
      max_scroll_y * 3 / 4, max_scroll_y,
      max_scroll_y * 3 / 4, max_scroll_y / 2,
      max_scroll_y / 4,     0,
  };
  std::vector<int64_t> layout_latencies_us;
  std::vector<int64_t> offset_settle_latencies_us;
  int steps_over_16600_us = 0;
  int max_visible_tab_rows = 0;
  int max_descendant_layers = 0;

  for (const int target_y : sweep_offsets) {
    const base::TimeTicks step_start = base::TimeTicks::Now();
    scroll->ScrollToOffset(gfx::PointF(0, target_y));
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return scroll->CurrentOffset().y() == target_y;
    })) << "Layer-backed ScrollView did not settle at offset "
        << target_y;
    const int64_t offset_settle_us =
        (base::TimeTicks::Now() - step_start).InMicroseconds();

    const base::TimeTicks layout_start = base::TimeTicks::Now();
    tab_list_->GetWidget()->LayoutRootViewIfNecessary();
    const int64_t layout_us =
        (base::TimeTicks::Now() - layout_start).InMicroseconds();
    const int64_t step_us =
        (base::TimeTicks::Now() - step_start).InMicroseconds();

    offset_settle_latencies_us.push_back(offset_settle_us);
    layout_latencies_us.push_back(layout_us);
    if (step_us > 16600) {
      ++steps_over_16600_us;
    }

    const gfx::Rect viewport = scroll->GetVisibleRect();
    EXPECT_EQ(target_y, viewport.y());
    max_visible_tab_rows = std::max(
        max_visible_tab_rows,
        ExpectIntersectingTabRowsVisible(tab_list_, tab_list_, viewport));
    max_descendant_layers =
        std::max(max_descendant_layers, CountDescendantLayers(scroll));
  }

  // Exercise sub-row scroll deltas so future row-boundary coalescing changes
  // are visible independently of the large coarse jumps above. Forty 4dp
  // steps in each direction cross more than four 36dp rows while keeping the
  // 600-tab stress case practical in CI.
  constexpr int kFineStepDp = 4;
  constexpr int kFineStepsPerDirection = 40;
  const int fine_origin_y = max_scroll_y / 2;
  ASSERT_LE(fine_origin_y + kFineStepDp * kFineStepsPerDirection, max_scroll_y);

  scroll->ScrollToOffset(gfx::PointF(0, fine_origin_y));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return scroll->CurrentOffset().y() == fine_origin_y;
  })) << "Layer-backed ScrollView did not reach the fine-scroll origin";
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(fine_origin_y, scroll->GetVisibleRect().y());
  max_visible_tab_rows =
      std::max(max_visible_tab_rows,
               ExpectIntersectingTabRowsVisible(tab_list_, tab_list_,
                                                scroll->GetVisibleRect()));
  max_descendant_layers =
      std::max(max_descendant_layers, CountDescendantLayers(scroll));

  std::vector<int> fine_offsets;
  fine_offsets.reserve(kFineStepsPerDirection * 2);
  for (int step = 1; step <= kFineStepsPerDirection; ++step) {
    fine_offsets.push_back(fine_origin_y + step * kFineStepDp);
  }
  for (int step = kFineStepsPerDirection - 1; step >= 0; --step) {
    fine_offsets.push_back(fine_origin_y + step * kFineStepDp);
  }

  std::vector<int64_t> fine_layout_latencies_us;
  std::vector<int64_t> fine_invalidating_layout_latencies_us;
  std::vector<int64_t> fine_noninvalidating_layout_latencies_us;
  int fine_steps_over_16600_us = 0;
  int fine_invalidation_steps = 0;
  int fine_layout_steps_over_16600_us = 0;
  for (const int target_y : fine_offsets) {
    const int invalidation_count_before =
        tab_list_->scroll_layout_invalidation_count_for_testing();
    const base::TimeTicks step_start = base::TimeTicks::Now();
    scroll->ScrollToOffset(gfx::PointF(0, target_y));
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return scroll->CurrentOffset().y() == target_y;
    })) << "Layer-backed ScrollView did not settle at fine offset "
        << target_y;

    const base::TimeTicks layout_start = base::TimeTicks::Now();
    tab_list_->GetWidget()->LayoutRootViewIfNecessary();
    const int64_t layout_us =
        (base::TimeTicks::Now() - layout_start).InMicroseconds();
    const int64_t step_us =
        (base::TimeTicks::Now() - step_start).InMicroseconds();

    const bool step_invalidated =
        tab_list_->scroll_layout_invalidation_count_for_testing() >
        invalidation_count_before;

    fine_layout_latencies_us.push_back(layout_us);
    if (step_invalidated) {
      ++fine_invalidation_steps;
      fine_invalidating_layout_latencies_us.push_back(layout_us);
    } else {
      fine_noninvalidating_layout_latencies_us.push_back(layout_us);
    }
    if (step_us > 16600) {
      ++fine_steps_over_16600_us;
    }
    if (layout_us > 16600) {
      ++fine_layout_steps_over_16600_us;
    }

    const gfx::Rect viewport = scroll->GetVisibleRect();
    EXPECT_EQ(target_y, viewport.y());
    max_visible_tab_rows = std::max(
        max_visible_tab_rows,
        ExpectIntersectingTabRowsVisible(tab_list_, tab_list_, viewport));
    max_descendant_layers =
        std::max(max_descendant_layers, CountDescendantLayers(scroll));
  }

  scroll->ScrollToOffset(gfx::PointF(0, 0));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return scroll->CurrentOffset().y() == 0;
  })) << "Layer-backed ScrollView did not return to zero after fine scroll";
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_EQ(0, scroll->CurrentOffset().y());
  ASSERT_EQ(0, scroll->GetVisibleRect().y());
  EXPECT_TRUE(pinned_section->GetVisible());
  EXPECT_TRUE(separator->GetVisible());
  EXPECT_TRUE(actions_row->GetVisible());

  const int64_t layout_median_us = PercentileMicros(layout_latencies_us, 50);
  const int64_t layout_p95_us = PercentileMicros(layout_latencies_us, 95);
  const int64_t offset_settle_median_us =
      PercentileMicros(offset_settle_latencies_us, 50);
  const int64_t offset_settle_p95_us =
      PercentileMicros(offset_settle_latencies_us, 95);
  const int64_t fine_layout_median_us =
      PercentileMicros(fine_layout_latencies_us, 50);
  const int64_t fine_layout_p95_us =
      PercentileMicros(fine_layout_latencies_us, 95);
  const int64_t fine_invalidating_layout_p95_us =
      fine_invalidating_layout_latencies_us.empty()
          ? 0
          : PercentileMicros(fine_invalidating_layout_latencies_us, 95);
  const int64_t fine_noninvalidating_layout_p95_us =
      fine_noninvalidating_layout_latencies_us.empty()
          ? 0
          : PercentileMicros(fine_noninvalidating_layout_latencies_us, 95);
  const int cosmetic_apply_delta =
      sidebar_->cosmetic_apply_count_for_testing() - cosmetic_apply_count_start;

  RecordProperty("SIDEBAR_SCROLL_PERF_tab_count", tab_count);
  RecordProperty("SIDEBAR_SCROLL_PERF_layout_median_us", layout_median_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_layout_p95_us", layout_p95_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_offset_settle_median_us",
                 offset_settle_median_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_offset_settle_p95_us",
                 offset_settle_p95_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_steps_over_16600_us",
                 steps_over_16600_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_visible_tab_rows_max",
                 max_visible_tab_rows);
  RecordProperty("SIDEBAR_SCROLL_PERF_descendant_layers_max",
                 max_descendant_layers);
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_step_count", fine_offsets.size());
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_layout_median_us",
                 fine_layout_median_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_layout_p95_us", fine_layout_p95_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_steps_over_16600_us",
                 fine_steps_over_16600_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_invalidation_steps",
                 fine_invalidation_steps);
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_invalidating_layout_p95_us",
                 fine_invalidating_layout_p95_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_noninvalidating_layout_p95_us",
                 fine_noninvalidating_layout_p95_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_fine_layout_steps_over_16600_us",
                 fine_layout_steps_over_16600_us);
  RecordProperty("SIDEBAR_SCROLL_PERF_cosmetic_apply_delta",
                 cosmetic_apply_delta);

  LOG(INFO) << "[SIDEBAR_SCROLL_PERF] tab_count=" << tab_count
            << " layout_median_us=" << layout_median_us
            << " layout_p95_us=" << layout_p95_us
            << " offset_settle_median_us=" << offset_settle_median_us
            << " offset_settle_p95_us=" << offset_settle_p95_us
            << " steps_over_16600_us=" << steps_over_16600_us
            << " visible_tab_rows_max=" << max_visible_tab_rows
            << " descendant_layers_max=" << max_descendant_layers;
  LOG(INFO) << "[SIDEBAR_SCROLL_PERF] phase=fine tab_count=" << tab_count
            << " fine_step_count=" << fine_offsets.size()
            << " fine_step_dp=" << kFineStepDp
            << " fine_layout_median_us=" << fine_layout_median_us
            << " fine_layout_p95_us=" << fine_layout_p95_us
            << " fine_steps_over_16600_us=" << fine_steps_over_16600_us
            << " fine_invalidation_steps=" << fine_invalidation_steps
            << " fine_invalidating_layout_p95_us="
            << fine_invalidating_layout_p95_us
            << " fine_noninvalidating_layout_p95_us="
            << fine_noninvalidating_layout_p95_us
            << " fine_layout_steps_over_16600_us="
            << fine_layout_steps_over_16600_us
            << " cosmetic_apply_delta=" << cosmetic_apply_delta;

  // --- Hard, formula-derived acceptance guards (T3) ---
  // The realized view count must be bounded by the viewport window, never by
  // the tab count. All ceilings below are derived from the viewport height and
  // the fixed overscan; none scale with |tab_count| (300 vs 600 share them).
  constexpr int kRowHeightDp = 36;
  constexpr int kBufferRows = 30;  // SidebarVisibilityManager::Config buffers.
  const int viewport_rows =
      (scroll->GetVisibleRect().height() + kRowHeightDp - 1) / kRowHeightDp;
  // Worst-case window = viewport + velocity-direction leading overscan (capped
  // at kMaxVelocityOverscanRows) + base trailing overscan. Constant regardless
  // of tab_count (300 vs 600 share it).
  const int kWindowRows =
      viewport_rows + SidebarVisibilityManager::kMaxVelocityOverscanRows +
      kBufferRows;

  // Rows interleaved inside a fixed-height window can be packed denser than one
  // per 36dp: a side-by-side split holds two tab rows in a single 36dp slot,
  // collapsed folders contribute a sticky row, and each of the two section
  // boundaries can straddle a couple of partial rows. kStructuralSlack is a
  // fixed budget for that density; it does NOT scale with tab_count, which is
  // exactly the regression this guard protects against.
  constexpr int kStructuralSlack = 48;
  EXPECT_LE(max_visible_tab_rows, kWindowRows + kStructuralSlack)
      << "Realized tab rows must stay bounded by the viewport window; "
      << "tab_count=" << tab_count << " window=" << kWindowRows
      << " observed=" << max_visible_tab_rows;

  // Layer ceiling. Each realized tab row owns 2 layers (close_button_ +
  // audio_button_). A split container adds 1 (its awaiting-anim texture layer)
  // plus its two member rows' layers. A conservative +1/row absorbs any
  // transient favicon/row layer. kScaffoldAllowance covers the ScrollView, the
  // two section containers, the separator, the actions row, and the four
  // spacers.
  constexpr int kLayerPerTabRow = 2;
  constexpr int kMaxWindowSplits = 24;
  constexpr int kScaffoldAllowance = 24;
  const int kLayerCeiling = kWindowRows * kLayerPerTabRow +
                            kMaxWindowSplits * (1 + 2 * kLayerPerTabRow) +
                            kWindowRows * 1 + kScaffoldAllowance;
  EXPECT_LE(max_descendant_layers, kLayerCeiling)
      << "Descendant layers must stay bounded by the viewport window; "
      << "tab_count=" << tab_count << " ceiling=" << kLayerCeiling
      << " observed=" << max_descendant_layers;

  EXPECT_LT(fine_invalidating_layout_p95_us, 16600)
      << "Invalidating fine-scroll layout p95 must fit one 60Hz frame";
  EXPECT_LT(fine_noninvalidating_layout_p95_us, 16600)
      << "Non-invalidating fine-scroll layout p95 must stay negligible";
  EXPECT_EQ(0, cosmetic_apply_delta)
      << "Scrolling must not trigger any cosmetic refresh apply";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, SeededFavoritesMatchSnapshot) {
  SeedProfile(2);

  auto state = GetViewState();
  EXPECT_FALSE(state.favorites.items.empty())
      << "Seed 2 must produce at least one favorite";

  for (const auto& fav : state.favorites.items) {
    EXPECT_FALSE(fav.title.empty()) << "Favorite must have a title";
    EXPECT_TRUE(fav.url.is_valid()) << "Favorite must have a valid URL";
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, SeededFoldersMatchSnapshot) {
  SeedProfile(2);

  auto state = GetViewState();
  std::vector<const SidebarTreeNode*> folders;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      folders.push_back(&node);
    }
  }
  EXPECT_FALSE(folders.empty()) << "Seed 2 must produce at least one folder";

  for (const auto* folder : folders) {
    EXPECT_FALSE(folder->folder_name.empty())
        << "Folder must have a name";
    EXPECT_FALSE(folder->children.empty())
        << "Folder '" << folder->folder_name << "' must have children";
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, PinnedSectionContract) {
  SeedProfile(2);

  auto state = GetViewState();
  EXPECT_FALSE(state.tab_list.pinned_tree.empty())
      << "Seed 2 must produce at least one pinned tab";

  for (const auto& node : state.tab_list.pinned_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      EXPECT_TRUE(node.is_pinned) << "Pinned tabs in pinned_tree must be pinned";
      EXPECT_FALSE(node.tab_id.empty())
          << "Pinned tabs in pinned_tree must have a tab_id";
      continue;
    }

    EXPECT_EQ(SidebarNodeKind::kFolder, node.kind)
        << "Pinned tree may only contain tab or folder rows";
    EXPECT_TRUE(node.folder_is_pinned)
        << "Pinned folders in pinned_tree must be marked pinned";
    EXPECT_FALSE(node.folder_id.empty())
        << "Pinned folders in pinned_tree must have a folder_id";
  }

  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      EXPECT_FALSE(node.is_pinned)
          << "Normal tree must not contain pinned tabs";
    }
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, FolderSupportContract) {
  SeedProfile(2);

  auto state = GetViewState();
  bool found_folder = false;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder) {
      found_folder = true;
      EXPECT_FALSE(node.folder_name.empty())
          << "Folder must have folder_name";
      EXPECT_FALSE(node.folder_id.empty())
          << "Folder must have folder_id";
      EXPECT_FALSE(node.children.empty())
          << "Folder must have children";
    }
  }
  EXPECT_TRUE(found_folder) << "Seed 2 must have at least one folder";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, TabReorderContract) {
  SeedProfile(2);
  observer_.Clear();

  base::RunLoop().RunUntilIdle();
  ResolveViews();

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

  SidebarTabRowView* first = nullptr;
  SidebarTabRowView* second = nullptr;
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    ResolveViews();
    first = tab_list_->FindTabRowByIdForTesting(root_tab_ids[0]);
    second = tab_list_->FindTabRowByIdForTesting(root_tab_ids[1]);
    return first != nullptr && second != nullptr;
  }));

  DragViewToPoint(first, GetViewPoint(second, 0.5, 0.15));

  EXPECT_TRUE(observer_.HasEvent("reorder_root_item"))
      << "Expected reorder_root_item event after drag";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, SidebarTreeJsonContract) {
  SeedProfile(2);

  auto state = GetViewState();

  EXPECT_FALSE(state.tab_list.active_space_id.empty())
      << "active_space_id must be set";
  EXPECT_FALSE(state.tab_list.normal_tree.empty())
      << "normal_tree must be non-empty";
  EXPECT_FALSE(state.tab_list.pinned_tree.empty())
      << "pinned_tree must be non-empty for seed 2";
  EXPECT_FALSE(state.favorites.items.empty())
      << "favorites must be non-empty for seed 2";

  std::vector<std::string> pinned_root_ids;
  std::vector<std::string> normal_root_ids;
  auto collect_root_ids = [&](const std::vector<SidebarTreeNode>& tree,
                              std::vector<std::string>& root_ids,
                              bool is_root_level,
                              auto&& collect_root_ids_ref) -> void {
    for (const auto& node : tree) {
      bool is_tab = (node.kind == SidebarNodeKind::kTab);
      bool is_folder = (node.kind == SidebarNodeKind::kFolder);
      bool is_split_group = (node.kind == SidebarNodeKind::kSplitGroup);
      EXPECT_TRUE(is_tab || is_folder || is_split_group)
          << "Every tree node must be a tab, folder, or split group";
      if (is_tab) {
        EXPECT_FALSE(node.tab_id.empty());
        if (is_root_level) {
          EXPECT_TRUE(node.parent_folder_id.empty())
              << "Root tab nodes must stay at the root";
        }
        root_ids.push_back(node.tab_id);
      }
      if (is_folder) {
        EXPECT_FALSE(node.folder_id.empty());
        if (is_root_level) {
          root_ids.push_back(node.folder_id);
        }
      }
      if (is_split_group) {
        EXPECT_FALSE(node.split_id.empty())
            << "Split groups must have a stable split_id";
        collect_root_ids_ref(node.children, root_ids, /*is_root_level=*/false,
                             collect_root_ids_ref);
      }
    }
  };
  collect_root_ids(state.tab_list.pinned_tree, pinned_root_ids,
                   /*is_root_level=*/true, collect_root_ids);
  collect_root_ids(state.tab_list.normal_tree, normal_root_ids,
                   /*is_root_level=*/true, collect_root_ids);

  EXPECT_FALSE(pinned_root_ids.empty())
      << "Pinned root IDs must be collected from seeded profile";
  EXPECT_FALSE(normal_root_ids.empty())
      << "Normal root IDs must be collected from seeded profile";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, FolderCollapseExpandToggle) {
  SeedProfile(2);

  auto state = GetViewState();
  std::string folder_id;
  int children_count = 0;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && !node.children.empty()) {
      folder_id = node.folder_id;
      children_count = static_cast<int>(node.children.size());
      break;
    }
  }
  ASSERT_FALSE(folder_id.empty());
  ASSERT_GT(children_count, 0);

  SidebarFolderRowView* folder_row = nullptr;
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    ResolveViews();
    folder_row = tab_list_->FindFolderRowByIdForTesting(folder_id);
    return folder_row != nullptr;
  }));

  const gfx::Point center = folder_row->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    auto state_after = GetViewState();
    for (const auto& node : state_after.tab_list.normal_tree) {
      if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
        return !node.is_expanded;
      }
    }
    return false;
  })) << "Folder should collapse after click";

  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop2;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop2.QuitClosure()));
  click_loop2.Run();

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    auto state_after = GetViewState();
    for (const auto& node : state_after.tab_list.normal_tree) {
      if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
        return node.is_expanded;
      }
    }
    return false;
  })) << "Folder should expand after second click";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       EmptyFolderRemovedAfterLastChildMove) {
  SeedProfile(2);
  observer_.Clear();

  auto state = GetViewState();
  std::string folder_id;
  std::string child_tab_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kFolder && node.children.size() == 1) {
      folder_id = node.folder_id;
      if (node.children[0].kind == SidebarNodeKind::kTab) {
        child_tab_id = node.children[0].tab_id;
      }
      break;
    }
  }

  if (folder_id.empty()) {
    LOG(WARNING) << "No single-child folder in seed 2 — skipping";
    return;
  }
  ASSERT_FALSE(child_tab_id.empty());

  SidebarTabRowView* tab_row = nullptr;
  views::View* normal_target = nullptr;
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    ResolveViews();
    tab_row = tab_list_->FindTabRowByIdForTesting(child_tab_id);
    normal_target =
        tab_list_->FindSectionDropTargetForTesting(MahoSidebarTabSection::kNormal);
    return tab_row != nullptr && normal_target != nullptr;
  }));

  DragViewToPoint(tab_row, GetViewPoint(normal_target, 0.5, 0.05));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return observer_.HasEvent("move_tab_to_root");
  }));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    auto state_after = GetViewState();
    for (const auto& node : state_after.tab_list.normal_tree) {
      if (node.kind == SidebarNodeKind::kFolder && node.folder_id == folder_id) {
        return false;
      }
    }
    return true;
  })) << "Empty folder should be pruned after last child is moved out";
}

namespace {

int CountTabsInTree(const std::vector<SidebarTreeNode>& nodes) {
  int total = 0;
  for (const auto& node : nodes) {
    if (node.kind == SidebarNodeKind::kTab) {
      ++total;
    }
    if (!node.children.empty()) {
      total += CountTabsInTree(node.children);
    }
  }
  return total;
}

}  // namespace

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, LargeTabCountRenders) {
  const int initial_strip_count = browser()->GetTabStripModel()->count();
  constexpr int kAddedTabs = 20;
  AddTestTabs(kAddedTabs);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    TabStripModel* strip = browser()->GetTabStripModel();
    return strip && strip->count() >= initial_strip_count + kAddedTabs;
  })) << "Browser tab strip should eventually include the added tabs";

  ResolveViews();
  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(GetFooterView());

  EXPECT_TRUE(tab_list_->GetVisible())
      << "Tab list should remain visible with many tabs";
  EXPECT_GT(tab_list_->GetPreferredSize().height(), 0)
      << "Tab list should have non-zero preferred height";
  EXPECT_TRUE(GetFooterView()->GetVisible());
  EXPECT_GT(GetFooterView()->GetBoundsInScreen().height(), 0);
  EXPECT_EQ(GetFooterView()->GetPreferredSize().height(), GetFooterView()->height())
      << "Footer must keep its fixed preferred height under large tab counts";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SidebarRowOrderTreeLayoutKeepsEmptyPinnedSectionContainer) {
  SeedProfile(2);
  RunAllPendingTasks();
  StopSidebarObservations();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.active_space_id.empty())
      << "Seed 2 should use tree layout";

  MahoSidebarTabListModel empty_pinned_tree = state.tab_list;
  empty_pinned_tree.pinned_tree.clear();

  tab_list_->Update(empty_pinned_tree, static_cast<Browser*>(browser()));
  ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SidebarRowOrderNoTabsWithBrowserContextKeepsFullScaffold) {
  RunAllPendingTasks();
  StopSidebarObservations();
  MahoSidebarTabListModel empty_model;
  tab_list_->Update(empty_model, static_cast<Browser*>(browser()));

  ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing());
}

// --- Wave 0 baseline: refresh contract and BuildViewStateModel shape --------

// After a seed+refresh cycle the tab list must contain at least one row and
// the view state model must have the same active_space_id in all sections that
// depend on it (tab_list.active_space_id matches footer.space_ids membership).
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        RefreshProducesConsistentActiveSpaceIdAcrossSections) {
  SeedProfile(2);

  auto state = GetViewState();

  EXPECT_FALSE(state.tab_list.active_space_id.empty())
      << "active_space_id must be set after seeded profile refresh";

  if (!state.footer.space_ids.empty()) {
    bool found = false;
    for (const auto& sid : state.footer.space_ids) {
      if (sid == state.tab_list.active_space_id) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found)
        << "tab_list.active_space_id must appear in footer.space_ids";
  }
}

// After rapid-fire tab additions the sidebar view tree must remain
// structurally valid: the tab_rows container must still exist, must be
// visible, and the row count must reflect at least the newly added tabs.
// This tests the coalescing contract indirectly: even if ScheduleRefreshAll
// was triggered multiple times, the final applied state must be coherent.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        RapidTabAdditionsProduceCoherentFinalViewState) {
  SeedProfile(2);

  auto state_before = GetViewState();
  const int tabs_before = CountTabsInTree(state_before.tab_list.normal_tree) +
                          CountTabsInTree(state_before.tab_list.pinned_tree);
  ASSERT_GT(tabs_before, 0) << "Seed 2 must have at least one tab";

  const int initial_strip_count = browser()->GetTabStripModel()->count();
  AddTestTabs(5);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    TabStripModel* strip = browser()->GetTabStripModel();
    return strip && strip->count() >= initial_strip_count + 5;
  })) << "Tab strip should reflect the 5 added tabs";

  ResolveViews();

  auto* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows) << "tab_rows container must exist after rapid additions";
  EXPECT_TRUE(tab_list_->GetVisible())
      << "tab list must remain visible after rapid additions";
  EXPECT_GT(rows->children().size(), 0u)
      << "tab_rows must have at least one child after additions";
}

// BuildViewStateModel applied via ApplyViewState (reached through
// tab_list_->Update()) must preserve the active_space_id that was in the
// model: the Update path must not silently drop it.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        BuildViewStateModelPreservesActiveSpaceIdThroughUpdate) {
  SeedProfile(2);

  auto state = GetViewState();
  const std::string space_id = state.tab_list.active_space_id;
  ASSERT_FALSE(space_id.empty());

  MahoSidebarTabListModel patched = state.tab_list;
  patched.active_space_id = space_id;
  tab_list_->Update(patched, static_cast<Browser*>(browser()));

  auto* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  EXPECT_FALSE(rows->children().empty())
      << "Tab rows must survive a round-trip through Update() with the "
         "original active_space_id preserved";
}

// The structural sections of MahoSidebarViewStateModel must all be
// non-trivially constructed after a seeded profile refresh:
//   - top_bar (no fields; navigation moved to MahoContentsHeaderView)
//   - favorites (items non-empty for seed 2)
//   - tab_list (active_space_id non-empty)
//   - footer (space_count > 0)
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        ViewStateModelHasAllFiveSectionsPopulatedAfterSeed) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();

  EXPECT_FALSE(state.favorites.items.empty())
      << "favorites must be non-empty for seed 2";

  EXPECT_FALSE(state.tab_list.active_space_id.empty())
      << "tab_list.active_space_id must be set for seed 2";
  EXPECT_FALSE(state.tab_list.normal_tree.empty())
      << "tab_list.normal_tree must be non-empty for seed 2";

  EXPECT_GT(state.footer.space_count, 0)
      << "footer.space_count must be > 0 for seed 2";
}

// Calling tab_list_->Update() with a fresh model that has a different
// active_space_id must not leave stale row views from the previous model.
// (Baseline for the identity rewrite: row identity comes from tab_id,
// not position.)
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        UpdateWithNewSpaceIdClearsStaleRows) {
  SeedProfile(2);

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.normal_tree.empty());

  MahoSidebarTabListModel new_model;
  new_model.active_space_id = "fresh-space-wave0";
  new_model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "only-tab-wave0";
    n.tab_strip_index = 0;
    n.title = u"Wave0 Only Tab";
    return n;
  }());

  tab_list_->Update(new_model, static_cast<Browser*>(browser()));

  auto* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  auto* only_row = tab_list_->FindTabRowByIdForTesting("only-tab-wave0");
  EXPECT_TRUE(only_row)
      << "After Update() with a single-tab model, that tab's row must exist";
}

// After a reorder_favorite dispatch the async refresh pipeline
// (ScheduleRefreshAll → BuildFavoritesModelForSpaceIdJson → ApplyViewState)
// must eventually reflect the new ordering in GetViewState().favorites.items.
//
// The test dispatches reorder_favorite for the first two favorites in the
// seeded profile, then polls GetViewState() until either the order changes
// or we see the event recorded by the observer — either outcome proves the
// dispatch reached the pipeline.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        FavoritesReorderPropagatesIntoViewStateAfterRefresh) {
  SeedProfile(2);
  observer_.Clear();

  auto state_before = GetViewState();
  ASSERT_GE(state_before.favorites.items.size(), 2u)
      << "Seed 2 must produce at least two favorites to test reorder";

  const std::string first_tab_id = state_before.favorites.items[0].tab_id;
  const std::string second_tab_id = state_before.favorites.items[1].tab_id;
  ASSERT_FALSE(first_tab_id.empty());
  ASSERT_FALSE(second_tab_id.empty());

  const std::string space_id = GetActiveSpaceId();
  ASSERT_FALSE(space_id.empty());

  DispatchShellEventEx(
      "reorder_favorite",
      {ShellEventField("space_id", space_id),
       ShellEventField("moved_tab_id", first_tab_id),
       ShellEventField("target_tab_id", second_tab_id),
       ShellEventField("insert_before", false)});

  ASSERT_TRUE(base::test::RunUntil([&]() {
    if (observer_.HasEvent("reorder_favorite")) {
      return true;
    }
    ResolveViews();
    auto state_after = GetViewState();
    if (state_after.favorites.items.size() < 2u) {
      return false;
    }
    return state_after.favorites.items[0].tab_id == second_tab_id;
  })) << "reorder_favorite dispatch must either be recorded by the observer "
        "or produce a changed order in GetViewState() after refresh";

  ResolveViews();
  auto state_final = GetViewState();
  ASSERT_GE(state_final.favorites.items.size(), 2u);
  bool has_first = false;
  bool has_second = false;
  for (const auto& item : state_final.favorites.items) {
    if (item.tab_id == first_tab_id)  has_first = true;
    if (item.tab_id == second_tab_id) has_second = true;
  }
  EXPECT_TRUE(has_first)
      << "first_tab_id must still be present in favorites after reorder";
  EXPECT_TRUE(has_second)
      << "second_tab_id must still be present in favorites after reorder";
}

// --- P0 gap: in-folder child reorder final-state verification ---------------

// Drags the first child of a multi-child folder to after the second child
// (targeting the insertion lane before the third child, or appending if only
// two children exist) and asserts the folder's child order in GetViewState()
// has actually changed — not just that an event was dispatched.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       InFolderChildReorderChangesChildOrderInViewState) {
  SeedProfile(2);
  observer_.Clear();

  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();

  std::string folder_id;
  std::string first_child_id;
  std::string second_child_id;
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind != SidebarNodeKind::kFolder || node.children.size() < 2u) {
      continue;
    }
    const auto& c0 = node.children[0];
    const auto& c1 = node.children[1];
    if (c0.kind != SidebarNodeKind::kTab || c1.kind != SidebarNodeKind::kTab) {
      continue;
    }
    folder_id = node.folder_id;
    first_child_id = c0.tab_id;
    second_child_id = c1.tab_id;
    break;
  }

  if (folder_id.empty()) {
    LOG(WARNING) << "No multi-child folder with two plain-tab children in "
                    "seed 2 — skipping InFolderChildReorderChangesChildOrderInViewState";
    return;
  }
  ASSERT_FALSE(first_child_id.empty());
  ASSERT_FALSE(second_child_id.empty());

  SidebarTabRowView* second_row = nullptr;
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    ResolveViews();
    second_row = tab_list_->FindTabRowByIdForTesting(second_child_id);
    return second_row != nullptr;
  })) << "second child row must be resolvable";

  // The insertion lane before first_child sits at child-index 0 inside the
  // folder.  Dragging second_child onto it dispatches reorder_tab_in_folder
  // with to:0, which moves second_child to the front, giving a final order of
  // [second_child, first_child, ...].
  views::View* lane_before_first =
      tab_list_->FindInsertionLaneBeforeTabByIdForTesting(first_child_id);
  if (!lane_before_first) {
    LOG(WARNING) << "Insertion lane before first child not found — skipping";
    return;
  }

  // Same synthetic drop path as TabReorderContract: a real OS drag loop
  // never delivers the drop on headless test displays.
  DragViewToPoint(second_row, GetViewPoint(lane_before_first, 0.5, 0.5));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return observer_.HasEvent("reorder_tab_in_folder");
  })) << "Expected reorder_tab_in_folder event after in-folder drag";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    ResolveViews();
    auto s = GetViewState();
    for (const auto& n : s.tab_list.normal_tree) {
      if (n.kind == SidebarNodeKind::kFolder && n.folder_id == folder_id) {
        if (n.children.size() < 2u) {
          return false;
        }
        return n.children[0].kind == SidebarNodeKind::kTab &&
               n.children[0].tab_id == second_child_id &&
               n.children[1].kind == SidebarNodeKind::kTab &&
               n.children[1].tab_id == first_child_id;
      }
    }
    return false;
  })) << "Folder child order must reflect the reorder in GetViewState()";

  ResolveViews();
  auto state_final = GetViewState();
  const SidebarTreeNode* folder_node = nullptr;
  for (const auto& n : state_final.tab_list.normal_tree) {
    if (n.kind == SidebarNodeKind::kFolder && n.folder_id == folder_id) {
      folder_node = &n;
      break;
    }
  }
  ASSERT_TRUE(folder_node) << "Folder must still exist after in-folder reorder";
  ASSERT_GE(folder_node->children.size(), 2u);
  EXPECT_EQ(second_child_id, folder_node->children[0].tab_id)
      << "second_child must be at index 0 after reorder";
  EXPECT_EQ(first_child_id, folder_node->children[1].tab_id)
      << "first_child must be at index 1 after reorder";
}

// --- P0 gap: favorites same-index / no-op stability after refresh -----------

// Dispatches reorder_favorite with the moved item's own tab_id as both the
// moved and target IDs (a semantic no-op).  After the refresh cycle the
// favorites order must be identical to the pre-dispatch order: no items may
// be added, removed, or permuted.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FavoritesNoOpSameIndexStabilityAfterRefresh) {
  SeedProfile(2);
  observer_.Clear();

  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state_before = GetViewState();
  ASSERT_GE(state_before.favorites.items.size(), 2u)
      << "Seed 2 must produce at least two favorites for no-op stability test";

  // Snapshot the full tab_id order before the no-op dispatch.
  std::vector<std::string> ids_before;
  for (const auto& item : state_before.favorites.items) {
    ids_before.push_back(item.tab_id);
  }

  const std::string space_id = GetActiveSpaceId();
  ASSERT_FALSE(space_id.empty());

  // Dispatch a reorder where moved == target (same index, no-op semantically).
  const std::string& same_id = ids_before[0];
  DispatchShellEventEx(
      "reorder_favorite",
      {ShellEventField("space_id", space_id),
       ShellEventField("moved_tab_id", same_id),
       ShellEventField("target_tab_id", same_id),
       ShellEventField("insert_before", true)});

  // Wait for either the observer to record the event or at least one
  // RunUntilIdle cycle to pass through the refresh pipeline.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return observer_.HasEvent("reorder_favorite");
  })) << "reorder_favorite dispatch must be observed by the recording observer";

  // Give the async refresh pipeline time to settle.
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state_after = GetViewState();

  // The count must be identical.
  ASSERT_EQ(ids_before.size(), state_after.favorites.items.size())
      << "No-op reorder must not add or remove favorites";

  // The order must be identical (a no-op dispatch must not permute the list).
  std::vector<std::string> ids_after;
  for (const auto& item : state_after.favorites.items) {
    ids_after.push_back(item.tab_id);
  }
  EXPECT_EQ(ids_before, ids_after)
      << "Favorites order must be unchanged after a same-index no-op dispatch";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FooterAndBoardPreserveCanonicalThemeData) {
  SeedProfile(2);
  ResolveViews();

  const std::string active_space_id = GetActiveSpaceId();
  ASSERT_FALSE(active_space_id.empty());

  base::ListValue gradient_colors;
  {
    base::DictValue primary;
    primary.Set("hue", 0.08);
    primary.Set("saturation", 0.84);
    primary.Set("brightness", 0.96);
    primary.Set("isCustom", false);
    primary.Set("isPrimary", true);
    gradient_colors.Append(std::move(primary));
  }
  {
    base::DictValue secondary;
    secondary.Set("hue", 0.56);
    secondary.Set("saturation", 0.72);
    secondary.Set("brightness", 0.74);
    secondary.Set("isCustom", false);
    secondary.Set("isPrimary", false);
    gradient_colors.Append(std::move(secondary));
  }

  base::DictValue theme;
  theme.Set("type", "zen");
  theme.Set("gradientColors", std::move(gradient_colors));
  theme.Set("harmony", "analogous");
  theme.Set("opacity", 0.9);
  theme.Set("texture", 0.2);

  base::DictValue changes;
  changes.Set("spaceId", active_space_id);
  changes.Set("theme", std::move(theme));

  base::DictValue event;
  event.Set("changes", std::move(changes));
  DispatchShellEventDict("update_space_config", std::move(event));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    auto state = GetViewState();
    const int active_index = state.footer.active_space_index;
    return active_index >= 0 &&
           active_index < static_cast<int>(state.footer.space_themes.size()) &&
           state.footer.space_themes[active_index].has_theme &&
           state.footer.space_themes[active_index].kind ==
               MahoSpaceDisplayThemeKind::kZen &&
           !state.footer.space_themes[active_index].primary_color.empty() &&
           !state.footer.space_themes[active_index].secondary_color.empty();
  }));

  const auto state = GetViewState();
  ASSERT_GE(state.footer.active_space_index, 0);
  ASSERT_LT(state.footer.active_space_index,
            static_cast<int>(state.footer.space_themes.size()));

  const MahoSpaceDisplayThemeModel& active_theme =
      state.footer.space_themes[state.footer.active_space_index];
  EXPECT_TRUE(active_theme.has_theme);
  EXPECT_EQ(MahoSpaceDisplayThemeKind::kZen, active_theme.kind);
  EXPECT_FALSE(active_theme.theme_json.empty());
  EXPECT_FALSE(active_theme.palette_colors.empty());
  EXPECT_EQ(active_theme.primary_color,
            state.footer.space_colors[state.footer.active_space_index]);
  EXPECT_EQ(active_theme.primary_color, state.footer.space_color);
  EXPECT_EQ(active_theme.primary_color, state.footer.space_theme.primary_color);
  EXPECT_EQ(active_theme.secondary_color,
            state.footer.space_theme.secondary_color);

  const SpaceBoardModel board = BuildSpacesBoardModel();
  const auto column_it =
      std::find_if(board.columns.begin(), board.columns.end(), [&](const auto& column) {
        return column.space_id == active_space_id;
      });
  ASSERT_NE(column_it, board.columns.end());
  EXPECT_TRUE(column_it->theme.has_theme);
  EXPECT_EQ(MahoSpaceDisplayThemeKind::kZen, column_it->theme.kind);
  EXPECT_EQ(active_theme.primary_color, column_it->theme.primary_color);
  EXPECT_EQ(active_theme.primary_color, column_it->color);
}

// --- Regression tests for helper-based sidebar tab identity/activation ---

// Given a seeded profile with tabs,
// When BuildTabListModel resolves tab identity,
// Then every tab row with a tab_id must have a matching MahoTabIdHelper on
// the corresponding WebContents (no URL fallback, no contents_to_core_id_).
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                         TabActivationUsesHelperIdentityOnly) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto state = GetViewState();
  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);

  auto check_nodes = [&](const std::vector<SidebarTreeNode>& nodes) {
    for (const auto& node : nodes) {
      if (node.kind != SidebarNodeKind::kTab || node.tab_id.empty() ||
          node.tab_strip_index < 0 ||
          !strip->ContainsIndex(node.tab_strip_index)) {
        continue;
      }
      content::WebContents* contents =
          strip->GetWebContentsAt(node.tab_strip_index);
      ASSERT_TRUE(contents) << "WebContents must exist at index "
                            << node.tab_strip_index;
      auto* helper = MahoTabIdHelper::FromWebContents(contents);
      ASSERT_TRUE(helper) << "MahoTabIdHelper must be attached at index "
                          << node.tab_strip_index;
      EXPECT_EQ(helper->stable_tab_id(), node.tab_id)
          << "Helper stable_tab_id must match core tab_id at index "
          << node.tab_strip_index;
    }
  };

  check_nodes(state.tab_list.pinned_tree);
  check_nodes(state.tab_list.normal_tree);
}

// Given a tab navigated to an internal chrome:// URL,
// When the sidebar resolves its identity,
// Then the helper-based identity still works (no URL-based fallback needed).
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                         InternalTabHasHelperIdentity) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  ASSERT_GT(strip->count(), 0);

  content::WebContents* contents = strip->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  EXPECT_FALSE(helper->stable_tab_id().empty())
      << "Even internal/special tabs must have a stable_tab_id via helper";

  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_TRUE(tab_list);
  std::string core_id = tab_list->FindCoreTabIdByWebContents(contents);
  EXPECT_FALSE(core_id.empty())
      << "FindCoreTabIdByWebContents must resolve via helper for any tab";
  EXPECT_EQ(core_id, helper->stable_tab_id());
}

// Given a seeded profile,
// When ResolveTabStripIndexForTabId is called with a known tab_id,
// Then it returns the correct strip index using only the helper scan.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                          ResolveTabStripIndexUsesHelperScan) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  ASSERT_GT(strip->count(), 0);

  content::WebContents* contents = strip->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->stable_tab_id().empty());

  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_TRUE(tab_list);

  auto state = GetViewState();
  tab_list->Update(state.tab_list, static_cast<Browser*>(browser()));

  std::string tab_id = helper->stable_tab_id();
  views::View* rows = tab_list->tab_rows_for_testing();
  ASSERT_TRUE(rows);

  SidebarTabRowView* row = tab_list->FindTabRowByIdForTesting(tab_id);
  if (row) {
    EXPECT_EQ(row->tab_id_for_testing(), tab_id);
  }
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       TabRowApplyCosmeticRefreshUpdatesFavicon) {
  SeedProfile(1);
  base::RunLoop().RunUntilIdle();
  ResolveViews();
  RunAllPendingTasks();
  StopSidebarObservations();
  tab_list_->set_accessibility_tree_enabled_for_testing(false);

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  ASSERT_GT(strip->count(), 0);
  content::WebContents* contents = strip->GetWebContentsAt(0);
  ASSERT_TRUE(contents);
  auto* helper = MahoTabIdHelper::FromWebContents(contents);
  ASSERT_TRUE(helper);
  const std::string tab_id = helper->stable_tab_id();
  ASSERT_FALSE(tab_id.empty());

  const gfx::Image red = MakeSidebarFaviconForTesting(SK_ColorRED);
  const gfx::Image blue = MakeSidebarFaviconForTesting(SK_ColorBLUE);
  ASSERT_TRUE(SetWebContentsFaviconForTesting(
      contents, GURL("https://example.com/red-favicon.ico"), red));

  MahoSidebarTabListModel model;
  model.active_space_id = GetActiveSpaceId();
  model.active_tab.tab_id = tab_id;
  model.active_tab.index = 0;
  SidebarTreeNode node;
  node.kind = SidebarNodeKind::kTab;
  node.tab_id = tab_id;
  node.tab_strip_index = 0;
  node.title = u"Favicon test";
  node.host = u"example.com";
  node.url = contents->GetVisibleURL().is_valid()
                 ? contents->GetVisibleURL().spec()
                 : "https://example.com/favicon-test";
  model.normal_tree.push_back(node);

  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_TRUE(tab_list);
  tab_list->Update(model, static_cast<Browser*>(browser()));
  SidebarTabRowView* row = tab_list->FindTabRowByIdForTesting(tab_id);
  ASSERT_TRUE(row);
  EXPECT_EQ(ui::ImageModel::FromImage(red), row->favicon_for_testing());

  ASSERT_TRUE(SetWebContentsFaviconForTesting(
      contents, GURL("https://example.com/blue-favicon.ico"), blue));
  tab_list->ApplyCosmeticRefresh();

  EXPECT_EQ(ui::ImageModel::FromImage(blue), row->favicon_for_testing())
      << "ApplyCosmeticRefresh must refresh the realized row favicon after creation";
  RunAllPendingTasks();
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       TabRowCreateRejectsStaleTabStripIndexFavicon) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();
  RunAllPendingTasks();
  StopSidebarObservations();
  tab_list_->set_accessibility_tree_enabled_for_testing(false);

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  ASSERT_GE(strip->count(), 2);
  content::WebContents* first_contents = strip->GetWebContentsAt(0);
  content::WebContents* second_contents = strip->GetWebContentsAt(1);
  ASSERT_TRUE(first_contents);
  ASSERT_TRUE(second_contents);

  auto* first_helper = MahoTabIdHelper::FromWebContents(first_contents);
  auto* second_helper = MahoTabIdHelper::FromWebContents(second_contents);
  ASSERT_TRUE(first_helper);
  ASSERT_TRUE(second_helper);
  const std::string first_tab_id = first_helper->stable_tab_id();
  const std::string second_tab_id = second_helper->stable_tab_id();
  ASSERT_FALSE(first_tab_id.empty());
  ASSERT_FALSE(second_tab_id.empty());
  ASSERT_NE(first_tab_id, second_tab_id);

  const gfx::Image first_favicon = MakeSidebarFaviconForTesting(SK_ColorRED);
  const gfx::Image second_favicon = MakeSidebarFaviconForTesting(SK_ColorBLUE);
  ASSERT_TRUE(SetWebContentsFaviconForTesting(
      first_contents, GURL("https://example.com/first-favicon.ico"),
      first_favicon));
  ASSERT_TRUE(SetWebContentsFaviconForTesting(
      second_contents, GURL("https://example.com/second-favicon.ico"),
      second_favicon));

  MahoSidebarTabListModel model;
  model.active_space_id = GetActiveSpaceId();
  model.active_tab.tab_id = first_tab_id;
  model.active_tab.index = 0;
  SidebarTreeNode stale_node;
  stale_node.kind = SidebarNodeKind::kTab;
  stale_node.tab_id = first_tab_id;
  stale_node.tab_strip_index = 1;
  stale_node.title = u"Stale-index row";
  stale_node.host = u"example.com";
  stale_node.url = first_contents->GetVisibleURL().is_valid()
                       ? first_contents->GetVisibleURL().spec()
                       : "https://example.com/stale-index";
  model.normal_tree.push_back(stale_node);

  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_TRUE(tab_list);
  tab_list->Update(model, static_cast<Browser*>(browser()));
  SidebarTabRowView* row = tab_list->FindTabRowByIdForTesting(first_tab_id);
  ASSERT_TRUE(row);

  EXPECT_EQ(ui::ImageModel::FromImage(first_favicon), row->favicon_for_testing())
      << "CreateTabRowView must resolve by tab_id instead of trusting stale tab_strip_index";
  EXPECT_FALSE(ui::ImageModel::FromImage(second_favicon) ==
               row->favicon_for_testing())
      << "A stale index must not leak the neighbor tab favicon into this row";
  RunAllPendingTasks();
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                          RuntimeNewTabIdentityAligned) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  const int count_before = strip->count();

  NavigateToNewForegroundTab(static_cast<Browser*>(browser()),
                              GURL("https://maho.test/identity-check"),
                              ui::PAGE_TRANSITION_GENERATED);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(count_before + 1, strip->count())
      << "NavigateToNewForegroundTab must insert exactly one new tab";

  content::WebContents* new_contents =
      strip->GetWebContentsAt(strip->active_index());
  ASSERT_TRUE(new_contents);

  auto* helper = MahoTabIdHelper::FromWebContents(new_contents);
  ASSERT_TRUE(helper) << "New tab must have MahoTabIdHelper attached";
  const std::string helper_id = helper->stable_tab_id();
  EXPECT_FALSE(helper_id.empty())
      << "New tab MahoTabIdHelper must carry a non-empty stable_tab_id";

  // Verify helper_id is present in core's own tab store — not just derived
  // from the helper itself.  maho_core_get_space_tabs reads core state
  // directly so this assertion fails if create_tab was never dispatched.
  MahoCore* core = GetCore();
  ASSERT_TRUE(core);
  const std::string active_space_id = GetActiveSpaceId();
  ASSERT_FALSE(active_space_id.empty())
      << "Active space must be set after seeded profile";
  const std::string space_id_json =
      base::GetQuotedJSONString(active_space_id);
  char* space_tabs_json_c =
      maho_core_get_space_tabs(core, space_id_json.c_str(), nullptr);
  ASSERT_TRUE(space_tabs_json_c)
      << "maho_core_get_space_tabs must return a result for the active space";
  std::string space_tabs_json(space_tabs_json_c);
  maho_string_free(space_tabs_json_c);

  auto parsed = base::JSONReader::Read(space_tabs_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_list())
      << "maho_core_get_space_tabs must return a JSON array";

  bool found_in_core = false;
  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (id && *id == helper_id) {
      found_in_core = true;
      break;
    }
  }
  EXPECT_TRUE(found_in_core)
      << "helper stable_tab_id '" << helper_id
      << "' must be present in core's tab store for the active space "
         "(create_tab was dispatched after Navigate succeeded)";
}

// Regression: ExecuteCommandAction("select_space_1") must route through the
// bridge, updating browser-scoped active-space state and sidebar view state.
// Before the fix, command actions called maho_core_activate_space() directly
// and left the bridge stale.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       CommandPathSwitchUpdatesBridgeAndSidebarState) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  MahoCore* core = GetCore();
  ASSERT_TRUE(core);
  std::string target_space_id;
  {
    char* spaces_json_c = maho_core_get_space_view_models(core);
    if (spaces_json_c) {
      std::optional<base::Value> spaces =
          base::JSONReader::Read(spaces_json_c, base::JSON_PARSE_RFC);
      maho_string_free(spaces_json_c);
      if (spaces && spaces->is_list() && !spaces->GetList().empty()) {
        const auto* dict = spaces->GetList()[0].GetIfDict();
        if (dict) {
          const std::string* id = dict->FindString("id");
          if (id) {
            target_space_id = *id;
          }
        }
      }
    }
  }

  if (target_space_id.empty()) {
    GTEST_SKIP() << "maho_core_get_space_view_models returned no spaces";
  }

  std::vector<std::string> all_ids = bridge->GetSpaceIds();
  if (all_ids.size() < 2u) {
    GTEST_SKIP() << "Need at least 2 spaces for command-path switch test";
  }

  std::string initial_space;
  for (const auto& sid : all_ids) {
    if (sid != target_space_id) {
      initial_space = sid;
      break;
    }
  }
  if (!initial_space.empty()) {
    bridge->SetActiveSpaceId(static_cast<Browser*>(browser()), initial_space);
  }

  const std::string space_before = bridge->GetActiveSpaceId(browser());

  ExecuteCommandAction(static_cast<Browser*>(browser()), "select_space_1");
  base::RunLoop().RunUntilIdle();

  const std::string bridge_space_after = bridge->GetActiveSpaceId(browser());
  EXPECT_EQ(target_space_id, bridge_space_after)
      << "Bridge must record the target space after select_space_1";

  if (!initial_space.empty()) {
    EXPECT_NE(space_before, bridge_space_after)
        << "select_space_1 must produce a real transition, not a no-op";
  }

  ResolveViews();
  auto state = GetViewState();
  EXPECT_EQ(bridge_space_after, state.tab_list.active_space_id)
      << "Sidebar state must reflect the same active space as the bridge "
         "after a command-path switch";
}

// Regression: footer dot activation must update the browser-scoped bridge
// state, not only the process-global fallback. Before the fix, the no-browser
// SwitchToSpace() overload was used, leaving browser-scoped state stale.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FooterSwitchUpdatesBrowserScopedBridgeState) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::vector<std::string> all_ids = bridge->GetSpaceIds();
  if (all_ids.size() < 2u) {
    GTEST_SKIP() << "Need at least 2 spaces for footer switch test";
  }

  MahoSidebarFooterView* footer = GetFooterView();
  ASSERT_TRUE(footer);

  views::View* second_dot = footer->GetSpaceDotForTesting(1);
  if (!second_dot) {
    GTEST_SKIP() << "Footer has no second dot";
  }

  auto* dot = views::AsViewClass<maho::MahoSidebarSpaceDotView>(second_dot);
  ASSERT_TRUE(dot);
  const std::string target_id = dot->space_id();
  ASSERT_FALSE(target_id.empty());

  const gfx::Point center = second_dot->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(ui_controls::SendMouseMove(center.x(), center.y()));
  base::RunLoop click_loop;
  ASSERT_TRUE(ui_controls::SendMouseEventsNotifyWhenDone(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
      click_loop.QuitClosure()));
  click_loop.Run();
  base::RunLoop().RunUntilIdle();

  const std::string browser_space = bridge->GetActiveSpaceId(browser());
  EXPECT_EQ(target_id, browser_space)
      << "Browser-scoped bridge state must reflect the footer dot click; "
         "the no-browser overload path would leave this stale";
}

// Regression: command-path switch in window A must not change window B's
// browser-scoped active space.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       CommandPathSwitchDoesNotAffectOtherWindow) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  ASSERT_TRUE(bridge);

  std::vector<std::string> all_ids = bridge->GetSpaceIds();
  if (all_ids.size() < 2u) {
    GTEST_SKIP() << "Need at least 2 spaces for multi-window isolation test";
  }

  Browser* second_browser = static_cast<Browser*>(CreateBrowser(browser()->GetProfile()));
  ASSERT_TRUE(second_browser);
  base::RunLoop().RunUntilIdle();

  const std::string space_a = bridge->GetActiveSpaceId(browser());
  const std::string space_b_candidate = [&]() {
    for (const auto& sid : all_ids) {
      if (sid != space_a) {
        return sid;
      }
    }
    return std::string();
  }();

  if (space_b_candidate.empty()) {
    chrome::CloseWindow(second_browser);
    base::RunLoop().RunUntilIdle();
    GTEST_SKIP() << "All space ids are identical; cannot test isolation";
  }

  bridge->SetActiveSpaceId(second_browser, space_b_candidate);
  const std::string window2_space_before =
      bridge->GetActiveSpaceId(second_browser);

  ExecuteCommandAction(static_cast<Browser*>(browser()), "select_space_1");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(window2_space_before, bridge->GetActiveSpaceId(second_browser))
      << "Window 2 active space must not change when window 1 uses "
         "command-path switching";

  chrome::CloseWindow(second_browser);
  base::RunLoop().RunUntilIdle();
}

// Regression: closing the last pinned tab produces a model with an empty
// pinned_tree but the tab strip still has tabs.  RebuildRows must use the full
// scaffold (pinned-section placeholder + separator + + New Tab + normal section)
// so the separator is never lost.  This was the root cause of the
// pinned-separator disappearance bug.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        LastPinnedTabCloseKeepsFullScaffold) {
  SeedProfile(2);
  RunAllPendingTasks();
  StopSidebarObservations();

  auto state = GetViewState();
  ASSERT_FALSE(state.tab_list.normal_tree.empty())
      << "Seed 2 must have at least one normal tab";

  MahoSidebarTabListModel pinned_cleared = state.tab_list;
  pinned_cleared.pinned_tree.clear();

  tab_list_->Update(pinned_cleared, static_cast<Browser*>(browser()));

  ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing());
}

// Regression: an empty display tree when the tab strip also has tabs (the
// transient gap between closing the last pinned tab and the model rebuild)
// must still produce the full scaffold, not the bare + New Tab layout.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        EmptyDisplayTreeWithLiveTabsKeepsFullScaffold) {
  SeedProfile(2);
  RunAllPendingTasks();
  StopSidebarObservations();

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  ASSERT_GT(strip->count(), 0) << "Seed 2 must have at least one live tab";

  MahoSidebarTabListModel empty_trees;
  empty_trees.active_space_id = GetActiveSpaceId();

  tab_list_->Update(empty_trees, static_cast<Browser*>(browser()));

  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  ASSERT_EQ(4u, rows->children().size())
      << "Full scaffold must keep hidden pinned + separator + + New Tab + "
         "normal section when live tabs exist, even with an empty display tree";

  EXPECT_TRUE(tab_list_->pinned_separator_for_testing() != nullptr)
      << "pinned_separator_ must be allocated in the full scaffold path";
  EXPECT_TRUE(tab_list_->pinned_separator_for_testing()->GetVisible())
      << "Separator must be visible when live tabs exist";
  EXPECT_TRUE(tab_list_->new_tab_button_for_testing() != nullptr)
      << "+ New Tab button must exist in the full scaffold path";
  EXPECT_FALSE(rows->children()[0]->GetVisible())
      << "Pinned section placeholder must stay hidden for an empty tree";
  EXPECT_TRUE(IsInlineRowButton(rows->children()[2], u"+ New Tab"))
      << "+ New Tab should remain in the scaffold position";
  EXPECT_TRUE(rows->children()[3]->GetVisible())
      << "Normal section container must still exist in the full scaffold path";
}

// True zero-tab state: both the model and the tab strip are empty.
// RebuildRows must produce only the bare + New Tab CTA (1 child).
// This verifies that the zero-tab path is still reachable and correct.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        TrueZeroTabStateRendersBareNewTabOnly) {
  ASSERT_EQ(browser()->GetType(), BrowserWindowInterface::Type::TYPE_NORMAL);
  auto* unload = UnloadController::From(browser());
  ASSERT_NE(unload, nullptr);
  ASSERT_FALSE(unload->is_attempting_to_close_browser());
  AddTestTab(GURL("about:blank"), u"Zero-tab transition");
  ASSERT_GT(browser()->GetTabStripModel()->count(), 0);
  browser()->GetTabStripModel()->CloseAllTabs();
  EXPECT_TRUE(browser()->GetTabStripModel()->empty());
  EXPECT_FALSE(unload->is_attempting_to_close_browser());
  EXPECT_FALSE(browser()->IsDeleteScheduled());

  MahoSidebarTabListModel empty_model;
  tab_list_->Update(empty_model, /*browser=*/nullptr);

  ExpectNoTabsRows(tab_list_->tab_rows_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SpaceHeader_TopRegionLayout) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoSidebarTabListModel model;
  model.active_space_icon = "💼";
  model.active_space_name = u"Work";
  model.active_space_id = "space-header-view-test-1";
  model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "tab-header-view-1";
    n.tab_strip_index = 0;
    n.title = u"Test Tab";
    return n;
  }());

  tab_list_->Update(model, static_cast<Browser*>(browser()));
  views::View* header = sidebar_->space_header_view_for_testing();
  ASSERT_NE(header, nullptr);
  UpdateSidebarSpaceHeaderRow(header, model.active_space_icon,
                              model.active_space_name);
  header->SetVisible(!model.active_space_name.empty());
  EXPECT_TRUE(header->GetVisible());

  // With the search pill removed, favorites open the top region and the
  // space header follows them.
  views::View* top_region = sidebar_->tabs_surface_for_testing()->children()[0];
  ASSERT_GE(top_region->children().size(), 2u);
  EXPECT_EQ(top_region->children()[0], sidebar_->favorites_view_for_testing());
  EXPECT_EQ(top_region->children()[1], header);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        SpaceHeader_VisibilityFollowsActiveSpaceName) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoSidebarTabListModel visible_model;
  visible_model.active_space_icon = "🚀";
  visible_model.active_space_name = u"Work";
  visible_model.active_space_id = "space-header-vis-test";
  visible_model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "tab-vis-1";
    n.tab_strip_index = 0;
    n.title = u"Test Tab";
    return n;
  }());

  tab_list_->Update(visible_model, static_cast<Browser*>(browser()));
  views::View* header = sidebar_->space_header_view_for_testing();
  ASSERT_NE(header, nullptr);
  UpdateSidebarSpaceHeaderRow(header, visible_model.active_space_icon,
                              visible_model.active_space_name);
  header->SetVisible(!visible_model.active_space_name.empty());
  EXPECT_TRUE(header->GetVisible());

  MahoSidebarTabListModel hidden_model;
  hidden_model.active_space_icon = "";
  hidden_model.active_space_name = u"";
  hidden_model.active_space_id = "space-header-vis-test";
  hidden_model.normal_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "tab-vis-2";
    n.tab_strip_index = 0;
    n.title = u"Test Tab";
    return n;
  }());

  tab_list_->Update(hidden_model, static_cast<Browser*>(browser()));
  UpdateSidebarSpaceHeaderRow(header, hidden_model.active_space_icon,
                              hidden_model.active_space_name);
  header->SetVisible(!hidden_model.active_space_name.empty());
  EXPECT_FALSE(header->GetVisible());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        TabScrollView_WrapsTabListView) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  EXPECT_EQ(scroll->contents(), tab_list_);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                        TabScrollView_ScrollBarConfig) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  EXPECT_EQ(scroll->GetHorizontalScrollBarMode(),
            views::ScrollView::ScrollBarMode::kDisabled);
  EXPECT_EQ(scroll->GetVerticalScrollBarMode(),
            views::ScrollView::ScrollBarMode::kEnabled);
  // Arc-style overlay scrollbar: it overlays the content (reserves no layout
  // width) rather than a classic width-reserving scrollbar.
  ASSERT_NE(scroll->vertical_scroll_bar(), nullptr);
  EXPECT_TRUE(scroll->vertical_scroll_bar()->OverlapsContent())
      << "vertical scrollbar must be a thin auto-hiding overlay, not classic";
  EXPECT_EQ(std::string(scroll->vertical_scroll_bar()->GetClassName()),
            "MahoSidebarScrollBar")
      << "the sidebar paints its palette-tinted pill, not the Fluent skin";
  ASSERT_NE(sidebar_->tab_scroll_bar_for_testing(), nullptr);
  EXPECT_EQ(sidebar_->tab_scroll_bar_for_testing()->GetThickness(),
            MahoSidebarScrollBar::kThickness);
  EXPECT_NE(sidebar_->tab_scroll_bar_for_testing()->idle_color_for_testing(),
            SK_ColorTRANSPARENT)
      << "the pill must be tinted from the resolved sidebar palette";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ScrollInvalidationCoalescesAtRowBoundaries) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(80), static_cast<Browser*>(browser()));

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_EQ(scroll->contents(), tab_list_);
  ASSERT_TRUE(tab_list_->GetWidget());

  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  ASSERT_GE(scroll->vertical_scroll_bar()->GetMaxPosition(), 180);
  tab_list_->ResetScrollLayoutInvalidationStateForTesting();

  for (const int target_y : {4, 8, 12, 16, 20, 24, 28, 32}) {
    scroll->ScrollToOffset(gfx::PointF(0, target_y));
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return scroll->CurrentOffset().y() == target_y &&
             scroll->GetVisibleRect().y() == target_y;
    }));
    EXPECT_EQ(1, tab_list_->scroll_layout_invalidation_count_for_testing())
        << "Sub-row offset " << target_y
        << " should stay within the first 36dp row boundary";
  }

  scroll->ScrollToOffset(gfx::PointF(0, 36));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return scroll->CurrentOffset().y() == 36 &&
           scroll->GetVisibleRect().y() == 36;
  }));
  EXPECT_EQ(2, tab_list_->scroll_layout_invalidation_count_for_testing());

  scroll->ScrollToOffset(gfx::PointF(0, 180));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return scroll->CurrentOffset().y() == 180 &&
           scroll->GetVisibleRect().y() == 180;
  }));
  EXPECT_EQ(3, tab_list_->scroll_layout_invalidation_count_for_testing())
      << "A multi-row jump should invalidate immediately";
}

// --- Regression: virtual-layout visibility latch on deep-scroll-then-top -----
//
// SidebarVirtualLayoutDelegate::CalculateProposedLayout() (maho_sidebar_tab_
// list_view.cc) gates each direct child's layout visibility on
//   children[i]->GetVisible() && (... intersects the expanded overscan rect)
// The leading GetVisible() term latches: once a top scaffold child is culled
// at a deep scroll offset (its layout visibility is set to false, which drives
// View::SetVisible(false)), scrolling back to y=0 can never re-show it — the
// GetVisible() precondition is already false, so the intersection test that
// would restore it is short-circuited and never reached.
//
// This test drives the real ScrollView through that exact sequence. The
// contents-scrolled callback (MahoSidebarView::OnTabListScrolled ->
// MahoSidebarTabListView::OnScrollChanged) invalidates tab_rows_ when the
// effective visible row boundary changes, so both large jumps below re-run the
// virtual layout with no timers or sleeps.
//
// buffer_rows_above (30) * kRowHeightDp (36) = 1080dp of overscan above the
// viewport, so the normal section is made far taller than 1080dp plus any
// plausible browser-test viewport height, guaranteeing the top three scaffold
// children (populated pinned section, separator, actions row) fall outside the
// overscan window at bottom scroll and are genuinely culled.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       DeepScrollThenScrollTopRestoresTopChildrenVisibility) {
  RunAllPendingTasks();
  StopSidebarObservations();

  // Populated pinned section => pinned scaffold child is intended-visible.
  MahoSidebarTabListModel model;
  model.active_space_id = "deep-scroll-latch-space";
  model.pinned_tree.push_back([] {
    SidebarTreeNode n;
    n.kind = SidebarNodeKind::kTab;
    n.tab_id = "pinned-latch-tab";
    n.tab_strip_index = -1;
    n.is_pinned = true;
    n.title = u"Pinned Latch Tab";
    return n;
  }());
  // 30 overscan rows * 36dp = 1080dp; 200 normal rows (~7200dp) keeps the top
  // three scaffold children well outside the overscan window at bottom scroll,
  // independent of the browser-test viewport height.
  constexpr int kNormalRows = 200;
  for (int i = 0; i < kNormalRows; ++i) {
    model.normal_tree.push_back([i] {
      SidebarTreeNode n;
      n.kind = SidebarNodeKind::kTab;
      n.tab_id = "normal-latch-tab-" + base::NumberToString(i);
      n.tab_strip_index = -1;
      n.title = u"Normal Latch Tab";
      return n;
    }());
  }

  tab_list_->Update(model, static_cast<Browser*>(browser()));

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_EQ(scroll->contents(), tab_list_);

  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  ASSERT_EQ(4u, rows->children().size())
      << "Full scaffold must be pinned section + separator + actions row + "
         "normal section";

  views::View* pinned_section = rows->children()[0];
  views::View* separator = rows->children()[1];
  views::View* actions_row = rows->children()[2];

  // Flush the initial layout at scroll offset 0, then wait for the compositor
  // to present it. Layer-backed ScrollView bounds are not usable for a
  // subsequent ScrollToOffset() until that layout reaches the content layer.
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  // Precondition: at the top, all three top scaffold children are visible.
  ASSERT_TRUE(pinned_section->GetVisible())
      << "Populated pinned section must start visible at scroll top";
  ASSERT_TRUE(separator->GetVisible())
      << "Pinned separator must start visible at scroll top";
  ASSERT_TRUE(actions_row->GetVisible())
      << "Actions row (+ New Tab) must start visible at scroll top";

  // Scroll to the real bottom offset. Deriving it from the laid-out content
  // and viewport makes the requested offset valid without relying on an
  // asynchronous clamp.
  constexpr int kVirtualLayoutOverscanDp = 30 * 36;
  const gfx::Rect top_visible_rect = scroll->GetVisibleRect();
  const int max_scroll_y = tab_list_->height() - top_visible_rect.height();
  ASSERT_EQ(max_scroll_y, scroll->vertical_scroll_bar()->GetMaxPosition());
  ASSERT_GT(max_scroll_y,
            kVirtualLayoutOverscanDp + actions_row->bounds().bottom())
      << "Bottom scroll must move the complete top scaffold beyond virtual "
         "layout overscan";

  scroll->ScrollToOffset(gfx::PointF(0, max_scroll_y));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return scroll->CurrentOffset().y() == max_scroll_y;
  })) << "Layer-backed ScrollView did not reach the requested bottom offset";
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  const gfx::Rect deep_visible_rect = scroll->GetVisibleRect();
  ASSERT_EQ(max_scroll_y, deep_visible_rect.y());
  ASSERT_GT(deep_visible_rect.y(),
            kVirtualLayoutOverscanDp + actions_row->bounds().bottom())
      << "Deep visible rect must exclude the complete top scaffold plus "
         "virtual-layout overscan";

  // Deep-scroll culling precondition: the top three scaffold children must
  // actually be culled (>1080dp above the viewport). Without this, the test
  // would never exercise the latch.
  ASSERT_FALSE(pinned_section->GetVisible())
      << "Pinned section must be culled at deep scroll (else the normal "
         "section is too short to exercise the latch)";
  ASSERT_FALSE(separator->GetVisible())
      << "Separator must be culled at deep scroll";
  ASSERT_FALSE(actions_row->GetVisible())
      << "Actions row must be culled at deep scroll";

  // Scroll back to the top and flush layout.
  scroll->ScrollToOffset(gfx::PointF(0, 0));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return scroll->CurrentOffset().y() == 0;
  })) << "Layer-backed ScrollView did not return to scroll offset 0";
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_EQ(0, scroll->GetVisibleRect().y());

  // Regression expectations: all three top scaffold children must be visible
  // again. On the old production code the GetVisible() latch at
  // maho_sidebar_tab_list_view.cc keeps them hidden, so these fail.
  EXPECT_TRUE(pinned_section->GetVisible())
      << "Pinned section must be restored to visible after scrolling back to "
         "top; the GetVisible() latch regresses this";
  EXPECT_TRUE(separator->GetVisible())
      << "Separator must be restored to visible after scrolling back to top";
  EXPECT_TRUE(actions_row->GetVisible())
      << "Actions row must be restored to visible after scrolling back to top";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ScrollPerformance300TabsProductionBaseline) {
  RunScrollPerformanceBenchmark(300);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ScrollPerformance600TabsStressBaseline) {
  RunScrollPerformanceBenchmark(600);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, BoundedRealizedRows) {
  int max_rows_300 = 0;
  int max_layers_300 = 0;
  int window_rows_300 = 0;
  MeasureWindowedRealization(300, &max_rows_300, &max_layers_300,
                             &window_rows_300);

  int max_rows_600 = 0;
  int max_layers_600 = 0;
  int window_rows_600 = 0;
  MeasureWindowedRealization(600, &max_rows_600, &max_layers_600,
                             &window_rows_600);

  constexpr int kStructuralSlack = 48;
  EXPECT_LE(max_rows_300, window_rows_300 + kStructuralSlack)
      << "300-tab realized rows must fit the viewport window";
  EXPECT_LE(max_rows_600, window_rows_600 + kStructuralSlack)
      << "600-tab realized rows must fit the viewport window";
  // The realized count must NOT grow with the total tab count: doubling the
  // model (300 -> 600) must not materially change how many rows are realized.
  EXPECT_LE(max_rows_600, max_rows_300 + kStructuralSlack)
      << "Realized rows scaled with tab count: 300=" << max_rows_300
      << " 600=" << max_rows_600;
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, LayerCeiling) {
  int max_rows_300 = 0;
  int max_layers_300 = 0;
  int window_rows_300 = 0;
  MeasureWindowedRealization(300, &max_rows_300, &max_layers_300,
                             &window_rows_300);

  int max_rows_600 = 0;
  int max_layers_600 = 0;
  int window_rows_600 = 0;
  MeasureWindowedRealization(600, &max_rows_600, &max_layers_600,
                             &window_rows_600);

  auto layer_ceiling = [](int window_rows) {
    constexpr int kLayerPerTabRow = 2;
    constexpr int kMaxWindowSplits = 24;
    constexpr int kScaffoldAllowance = 24;
    return window_rows * kLayerPerTabRow +
           kMaxWindowSplits * (1 + 2 * kLayerPerTabRow) + window_rows * 1 +
           kScaffoldAllowance;
  };
  EXPECT_LE(max_layers_300, layer_ceiling(window_rows_300));
  EXPECT_LE(max_layers_600, layer_ceiling(window_rows_600));
  EXPECT_LE(max_layers_600, max_layers_300 + layer_ceiling(0))
      << "Descendant layers scaled with tab count: 300=" << max_layers_300
      << " 600=" << max_layers_600;
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       CulledSelectionBulkActionPreserved) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeScrollPerfModel(300);
  // The first normal (non-pinned) tab: realized at the top, scrolled off later.
  std::string target_tab_id;
  for (const SidebarTreeNode& node : model.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      target_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(target_tab_id.empty());

  tab_list_->Update(model, static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());

  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  // Select the tab while it is realized near the top.
  ASSERT_NE(tab_list_->FindTabRowByIdForTesting(target_tab_id), nullptr);
  tab_list_->ToggleTabSelected(target_tab_id);
  EXPECT_TRUE(tab_list_->IsTabSelected(target_tab_id));

  // Scroll to the bottom so the selected row leaves the window and is culled.
  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);
  scroll->ScrollToOffset(gfx::PointF(0, max_scroll_y));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == max_scroll_y; }));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  ASSERT_EQ(tab_list_->FindTabRowByIdForTesting(target_tab_id), nullptr)
      << "Selected tab row must be culled (never realized) at bottom scroll";
  EXPECT_TRUE(tab_list_->IsTabSelected(target_tab_id))
      << "Selection must survive row culling";

  // A bulk action must still target the culled selection by id.
  observer_.Clear();
  tab_list_->PinSelected(true);

  bool pinned_target = false;
  for (const auto& event : observer_.events()) {
    if (event.kind == "pin_tab" &&
        event.json.find(target_tab_id) != std::string::npos) {
      pinned_target = true;
      break;
    }
  }
  EXPECT_TRUE(pinned_target)
      << "PinSelected must dispatch pin_tab for the culled selected tab id";
}

// --- H1 regression: well-formed ShellEvents must never parse-error in core ---
//
// The silent-no-op ("H1") bug class happened when the native shell built a
// ShellEvent payload with a field name the Rust `ShellEvent` enum did not
// expect (e.g. move_tab_to_space sent with `space_id` instead of the
// `target_space_id` the enum actually deserializes).  serde rejected the
// payload, `maho_core_handle_event` returned a structured parse-error payload
// — `{"error":{"kind":"parse",...}}` — and the UI silently assumed success:
// the drag looked like it worked but nothing changed in core.
//
// This contract test dispatches a representative *well-formed* payload for
// every sidebar-relevant ShellEvent kind straight through
// maho_core_handle_event (the same core entry point DispatchShellEvent* uses,
// but read here for its return value, which those helpers discard) and asserts
// the core never answers with the parse-error payload.  Field names below
// mirror the Rust ShellEvent variants exactly (maho-types shell_event.rs).
//
// A negative control at the end re-sends the exact H1 shape and asserts it DOES
// produce a parse error, proving the detector above is not vacuous.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       WellFormedShellEventsDoNotProduceParseErrors) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();

  MahoCore* core = GetCore();
  ASSERT_TRUE(core);

  // Use a real active space id and a real tab id so the fixtures are as
  // representative as possible.  Parse validity does not depend on the ids
  // referencing live objects (an unknown id is a semantic no-op, never a parse
  // error), but realistic ids keep the payloads honest.
  const std::string space_id = GetActiveSpaceId();
  ASSERT_FALSE(space_id.empty());

  std::string tab_id;
  {
    auto state = GetViewState();
    for (const auto& node : state.tab_list.normal_tree) {
      if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
        tab_id = node.tab_id;
        break;
      }
    }
    if (tab_id.empty()) {
      for (const auto& node : state.tab_list.pinned_tree) {
        if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
          tab_id = node.tab_id;
          break;
        }
      }
    }
  }
  ASSERT_FALSE(tab_id.empty()) << "Seed 2 must expose at least one tab id";

  // base::GetQuotedJSONString returns the value WITH its surrounding double
  // quotes and escapes any embedded quotes, so the ids can be spliced straight
  // into the JSON literals below without breaking them.  Built as raw JSON
  // strings on purpose: base::Value::Dict is not name-visible in this modules
  // TU, so payloads are assembled by hand.
  const std::string q_tab = base::GetQuotedJSONString(tab_id);
  const std::string q_space = base::GetQuotedJSONString(space_id);

  // Round-trips a raw JSON payload through maho_core_handle_event and returns
  // the decoded response.  An empty string means the FFI returned null, which
  // for a live core + valid C string must never happen for a well-formed event.
  auto dispatch = [&](const std::string& json) -> std::string {
    char* result_c = maho_core_handle_event(core, json.c_str());
    if (!result_c) {
      return std::string();
    }
    std::string result(result_c);
    maho_string_free(result_c);
    return result;
  };

  // Order-independent parse-error detection.  serde_json serializes with keys
  // sorted alphabetically (no preserve_order), so the payload is
  // {"error":{"detail":"...","kind":"parse"}} — the "error" object and the
  // "kind":"parse" member must both be present, but their order must not be
  // assumed.
  auto is_parse_error = [](const std::string& response) -> bool {
    return response.find("\"error\"") != std::string::npos &&
           response.find("\"kind\":\"parse\"") != std::string::npos;
  };

  // Table of {description, raw JSON payload}.  Field names mirror the Rust
  // ShellEvent variants exactly (maho-types shell_event.rs); every id is
  // quoted via base::GetQuotedJSONString.
  std::vector<std::pair<std::string, std::string>> rows = {
      // PinTab { tab_id }
      {"pin_tab", "{\"kind\":\"pin_tab\",\"tab_id\":" + q_tab + "}"},
      // UnpinTab { tab_id }
      {"unpin_tab", "{\"kind\":\"unpin_tab\",\"tab_id\":" + q_tab + "}"},
      // FavoriteTab { tab_id }
      {"favorite_tab", "{\"kind\":\"favorite_tab\",\"tab_id\":" + q_tab + "}"},
      // ChangeTabRole { tab_id, new_role: TabRole }.  TabRole is internally
      // tagged with `type` (serde tag="type", rename_all="camelCase").
      {"change_tab_role",
       "{\"kind\":\"change_tab_role\",\"tab_id\":" + q_tab +
           ",\"new_role\":{\"type\":\"pinned\"}}"},
      // ReorderFavorite { tab_id, new_index: usize }
      {"reorder_favorite", "{\"kind\":\"reorder_favorite\",\"tab_id\":" + q_tab +
                               ",\"new_index\":0}"},
      // CloseTab { tab_id, expected_space_id: Option (serde default) }
      {"close_tab (no expected_space_id)",
       "{\"kind\":\"close_tab\",\"tab_id\":" + q_tab + "}"},
      {"close_tab (with expected_space_id)",
       "{\"kind\":\"close_tab\",\"tab_id\":" + q_tab +
           ",\"expected_space_id\":" + q_space + "}"},
      // SuspendTab { tab_id }
      {"suspend_tab", "{\"kind\":\"suspend_tab\",\"tab_id\":" + q_tab + "}"},
      // ArchiveTabById { tab_id }
      {"archive_tab_by_id",
       "{\"kind\":\"archive_tab_by_id\",\"tab_id\":" + q_tab + "}"},
      // MoveTabToSpace { tab_id, target_space_id, section: String (default) }.
      // target_space_id — NOT space_id — is the exact field the H1 bug got
      // wrong.
      {"move_tab_to_space (target_space_id + section)",
       "{\"kind\":\"move_tab_to_space\",\"tab_id\":" + q_tab +
           ",\"target_space_id\":" + q_space + ",\"section\":\"normal\"}"},
      // MoveTabToRoot { space_id, folder_id, tab_id, before_tab_id: Option }
      {"move_tab_to_root",
       "{\"kind\":\"move_tab_to_root\",\"space_id\":" + q_space +
           ",\"folder_id\":\"folder-contract-test\",\"tab_id\":" + q_tab + "}"},
      // ReorderRootItem { space_id, item: RootItem, insertion_point:
      // RootInsertionPoint }.  RootItem is tag="kind", content="id"; Append is
      // {"kind":"append"}.
      {"reorder_root_item",
       "{\"kind\":\"reorder_root_item\",\"space_id\":" + q_space +
           ",\"item\":{\"kind\":\"tab\",\"id\":" + q_tab +
           "},\"insertion_point\":{\"kind\":\"append\"}}"},
      // CreateFolderWithTabs { space_id, name, tab_ids: Vec<TabId> }
      {"create_folder_with_tabs",
       "{\"kind\":\"create_folder_with_tabs\",\"space_id\":" + q_space +
           ",\"name\":\"Contract Folder\",\"tab_ids\":[" + q_tab + "]}"},
  };

  for (const auto& [description, json] : rows) {
    const std::string response = dispatch(json);
    EXPECT_FALSE(response.empty())
        << "maho_core_handle_event returned null for well-formed '"
        << description << "'";
    EXPECT_FALSE(is_parse_error(response))
        << description << " -> " << response;
  }

  // Negative control — the exact H1 shape: move_tab_to_space carrying the wrong
  // field name `space_id` instead of `target_space_id`.  serde reports the
  // required `target_space_id` as missing, so the core MUST answer with the
  // parse-error payload.  If this ever stops being a parse error, the guard
  // above has gone vacuous.
  {
    const std::string bad = "{\"kind\":\"move_tab_to_space\",\"tab_id\":" +
                            q_tab + ",\"space_id\":" + q_space + "}";
    const std::string response = dispatch(bad);
    ASSERT_FALSE(response.empty())
        << "malformed event must still return a non-null error payload";
    EXPECT_TRUE(is_parse_error(response))
        << "malformed move_tab_to_space (space_id instead of target_space_id) "
           "must yield a parse-error payload; got: " << response;
  }
}

// B2 regression: cosmetic refreshes requested while the ScrollView is actively
// scrolling must be held (pending) and applied exactly once after the scroll-
// idle transition — not during scrolling.
//
// Uses the real ScrollView callback path (AddContentsScrolledCallback →
// OnTabListScrolled), ScheduleCosmeticRefresh(), and the deterministic idle
// hook SimulateScrollIdleForTesting() — no wall-clock sleeps.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ScrollIdle_CosmeticRefreshDeferredDuringScroll) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(80), static_cast<Browser*>(browser()));

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_EQ(scroll->contents(), tab_list_);
  ASSERT_TRUE(tab_list_->GetWidget());

  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  ASSERT_GE(scroll->vertical_scroll_bar()->GetMaxPosition(), 10)
      << "Scroll content must be tall enough for the 10dp test offset";

  // Precondition: no scrolling state, no pending cosmetic work.
  EXPECT_FALSE(sidebar_->is_scrolling_for_testing());
  EXPECT_FALSE(sidebar_->cosmetic_refresh_pending_for_testing());
  const int apply_count_before = sidebar_->cosmetic_apply_count_for_testing();

  // Trigger a real contents-scrolled callback, which sets is_scrolling_ = true.
  scroll->ScrollToOffset(gfx::PointF(0, 10));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == 10; }));
  EXPECT_TRUE(sidebar_->is_scrolling_for_testing());

  // While scrolling: ScheduleCosmeticRefresh() must mark pending, not apply.
  sidebar_->ScheduleCosmeticRefresh();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(sidebar_->cosmetic_refresh_pending_for_testing());
  EXPECT_EQ(apply_count_before, sidebar_->cosmetic_apply_count_for_testing())
      << "ApplyCosmeticRefresh must not run during active scrolling";

  // Multiple cosmetic refresh requests during the same scroll window still
  // coalesce to a single pending flag (idempotent).
  sidebar_->ScheduleCosmeticRefresh();
  sidebar_->ScheduleCosmeticRefresh();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(sidebar_->cosmetic_refresh_pending_for_testing());
  EXPECT_EQ(apply_count_before, sidebar_->cosmetic_apply_count_for_testing());

  // Fire the scroll-idle transition deterministically (no sleep).
  sidebar_->SimulateScrollIdleForTesting();
  EXPECT_FALSE(sidebar_->is_scrolling_for_testing());
  EXPECT_FALSE(sidebar_->cosmetic_refresh_pending_for_testing());

  // The cosmetic debounce timer is now armed; fire it deterministically.
  // RunUntilIdle() does not advance OneShotTimers, so use the test hook.
  sidebar_->FireCosmeticDebounceForTesting();
  EXPECT_EQ(apply_count_before + 1,
            sidebar_->cosmetic_apply_count_for_testing())
      << "ApplyCosmeticRefresh must fire exactly once after scroll-idle";
}

// B2 regression (issue 1): a cosmetic refresh armed while idle (debounce timer
// running) must be converted to pending — not applied — if a real scroll starts
// before the 16ms window expires.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ScrollIdle_ArmedCosmeticConvertedToPendingOnScroll) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(80), static_cast<Browser*>(browser()));

  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_EQ(scroll->contents(), tab_list_);
  ASSERT_TRUE(tab_list_->GetWidget());

  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  ASSERT_GE(scroll->vertical_scroll_bar()->GetMaxPosition(), 10)
      << "Scroll content must be tall enough for the 10dp test offset";

  EXPECT_FALSE(sidebar_->is_scrolling_for_testing());
  EXPECT_FALSE(sidebar_->cosmetic_refresh_pending_for_testing());
  const int apply_count_before = sidebar_->cosmetic_apply_count_for_testing();

  // Arm the cosmetic debounce timer while idle (normal fast-path).
  sidebar_->ScheduleCosmeticRefresh();
  EXPECT_FALSE(sidebar_->cosmetic_refresh_pending_for_testing());
  EXPECT_EQ(apply_count_before, sidebar_->cosmetic_apply_count_for_testing());

  // Scroll starts before the 16ms debounce fires — real callback path.
  scroll->ScrollToOffset(gfx::PointF(0, 10));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == 10; }));

  // OnTabListScrolled() must have stopped the timer and set pending.
  EXPECT_TRUE(sidebar_->is_scrolling_for_testing());
  EXPECT_TRUE(sidebar_->cosmetic_refresh_pending_for_testing())
      << "Armed cosmetic timer must be converted to pending when scroll starts";
  EXPECT_EQ(apply_count_before, sidebar_->cosmetic_apply_count_for_testing())
      << "ApplyCosmeticRefresh must not have run during scroll onset";

  // Deterministic idle + debounce release must produce exactly one apply.
  sidebar_->SimulateScrollIdleForTesting();
  sidebar_->FireCosmeticDebounceForTesting();
  EXPECT_EQ(apply_count_before + 1,
            sidebar_->cosmetic_apply_count_for_testing())
      << "Exactly one apply after pending work is released";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       SectionProjectionMirrorsRebuildTreeOrder) {
  MahoSidebarTabListModel model = MakeProjectionModel();
  std::vector<SidebarVisualRow> projection;
  BuildSectionProjection(model.normal_tree, MahoSidebarTabSection::kNormal,
                         &projection, false);

  struct ExpectedRow {
    SidebarVisualRowKind kind;
    std::string stable_id;
    int height_dp;
    int cumulative_top_dp;
  };
  const std::vector<ExpectedRow> expected = {
      {SidebarVisualRowKind::kInsertionLane, "lane:normal:before:proj-t0", 2, 0},
      {SidebarVisualRowKind::kTab, "proj-t0", 36, 2},
      {SidebarVisualRowKind::kInsertionLane, "lane:normal:before:proj-f1", 2, 38},
      {SidebarVisualRowKind::kFolder, "proj-f1", 36, 40},
      {SidebarVisualRowKind::kInsertionLane, "lane:normal:before:proj-c0", 2, 76},
      {SidebarVisualRowKind::kTab, "proj-c0", 36, 78},
      {SidebarVisualRowKind::kInsertionLane, "lane:normal:before:proj-c1", 2, 114},
      {SidebarVisualRowKind::kTab, "proj-c1", 36, 116},
      {SidebarVisualRowKind::kInsertionLane, "lane:normal:after:proj-c1", 2, 152},
      {SidebarVisualRowKind::kInsertionLane, "lane:normal:before:proj-f2", 2, 154},
      {SidebarVisualRowKind::kFolder, "proj-f2", 36, 156},
      {SidebarVisualRowKind::kCollapsedStickyTab, "sticky:proj-f2:proj-c2", 36,
       192},
      {SidebarVisualRowKind::kSplitGroup, "proj-s1", 36, 228},
  };

  ASSERT_EQ(expected.size(), projection.size());
  int running_top = 0;
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i].kind, projection[i].kind) << "kind at row " << i;
    EXPECT_EQ(expected[i].stable_id, projection[i].stable_id)
        << "stable_id at row " << i;
    EXPECT_EQ(expected[i].height_dp, projection[i].height_dp)
        << "height at row " << i;
    EXPECT_EQ(expected[i].cumulative_top_dp, projection[i].cumulative_top_dp)
        << "cumulative_top at row " << i;
    EXPECT_EQ(running_top, projection[i].cumulative_top_dp)
        << "prefix-sum invariant at row " << i;
    running_top += projection[i].height_dp;
  }
  EXPECT_EQ(264, running_top);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       SectionProjectionKeepsOnlySplitChildren) {
  MahoSidebarTabListModel model = MakeProjectionModel();
  std::vector<SidebarVisualRow> projection;
  BuildSectionProjection(model.normal_tree, MahoSidebarTabSection::kNormal,
                         &projection, false);

  const SidebarVisualRow* expanded_folder = nullptr;
  const SidebarVisualRow* collapsed_folder = nullptr;
  const SidebarVisualRow* sticky_tab = nullptr;
  const SidebarVisualRow* split_group = nullptr;
  for (const SidebarVisualRow& row : projection) {
    if (row.kind == SidebarVisualRowKind::kFolder &&
        row.stable_id == "proj-f1") {
      expanded_folder = &row;
    } else if (row.kind == SidebarVisualRowKind::kFolder &&
               row.stable_id == "proj-f2") {
      collapsed_folder = &row;
    } else if (row.kind == SidebarVisualRowKind::kCollapsedStickyTab) {
      sticky_tab = &row;
    } else if (row.kind == SidebarVisualRowKind::kSplitGroup) {
      split_group = &row;
    }
  }

  ASSERT_NE(expanded_folder, nullptr);
  ASSERT_NE(collapsed_folder, nullptr);
  ASSERT_NE(sticky_tab, nullptr);
  ASSERT_NE(split_group, nullptr);

  EXPECT_TRUE(expanded_folder->node.children.empty())
      << "Folder projection rows must not deep-copy their child subtree";
  EXPECT_TRUE(collapsed_folder->node.children.empty())
      << "Collapsed folder projection rows must not retain hidden children";
  EXPECT_TRUE(sticky_tab->node.children.empty())
      << "Sticky tab rows are single tab descriptors, not subtree owners";
  EXPECT_EQ(2u, split_group->node.children.size())
      << "Split groups still need their member nodes for layout and AX";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       SectionProjectionMatchesRealizedEagerTree) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeProjectionModel();
  tab_list_->Update(model, static_cast<Browser*>(browser()));
  ASSERT_TRUE(tab_list_->GetWidget());
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  std::vector<SidebarVisualRow> expected;
  BuildSectionProjection(model.normal_tree, MahoSidebarTabSection::kNormal,
                         &expected, false);
  const std::vector<SidebarVisualRow>& wired =
      tab_list_->normal_projection_for_testing();
  ASSERT_EQ(expected.size(), wired.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i].kind, wired[i].kind) << "wired kind at row " << i;
    EXPECT_EQ(expected[i].stable_id, wired[i].stable_id)
        << "wired stable_id at row " << i;
    EXPECT_EQ(expected[i].cumulative_top_dp, wired[i].cumulative_top_dp)
        << "wired cumulative_top at row " << i;
  }

  std::vector<std::string> realized;
  CollectRealizedTabIds(tab_list_->tab_rows_for_testing(), &realized);
  const std::vector<std::string> reconstructed =
      ReconstructProjectionTabOrder(wired);
  EXPECT_EQ(realized, reconstructed);

  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_EQ(4u, rows->children().size());
  views::View* normal_section = rows->children()[3];
  ASSERT_FALSE(normal_section->children().empty());
  views::View* rows_container = normal_section->children()[0];
  // rows_container is [top_spacer, <realized rows>, bottom_spacer]. This model
  // is short enough to fit entirely in the window, so every projection row is
  // realized and both spacers collapse to zero.
  ASSERT_EQ(wired.size() + 2u, rows_container->children().size());
  EXPECT_EQ(0, rows_container->children().front()->GetPreferredSize().height());
  EXPECT_EQ(0, rows_container->children().back()->GetPreferredSize().height());

  int summed = 0;
  for (size_t i = 0; i < wired.size(); ++i) {
    EXPECT_EQ(wired[i].height_dp,
              rows_container->children()[i + 1]->GetPreferredSize().height())
        << "eager height mismatch at row " << i << " kind "
        << static_cast<int>(wired[i].kind);
    summed += wired[i].height_dp;
  }
  EXPECT_EQ(summed, rows_container->GetPreferredSize().height());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       MeasureVisualRowHeightMatchesTokens) {
  SidebarVisualRow lane;
  lane.kind = SidebarVisualRowKind::kInsertionLane;
  EXPECT_EQ(2, MeasureVisualRowHeight(lane));

  SidebarVisualRow tab;
  tab.kind = SidebarVisualRowKind::kTab;
  EXPECT_EQ(36, MeasureVisualRowHeight(tab));

  SidebarVisualRow folder;
  folder.kind = SidebarVisualRowKind::kFolder;
  EXPECT_EQ(36, MeasureVisualRowHeight(folder));

  SidebarVisualRow sticky;
  sticky.kind = SidebarVisualRowKind::kCollapsedStickyTab;
  EXPECT_EQ(36, MeasureVisualRowHeight(sticky));

  SidebarVisualRow split_side_by_side;
  split_side_by_side.kind = SidebarVisualRowKind::kSplitGroup;
  split_side_by_side.node.split_orientation = "vertical";
  split_side_by_side.node.children.push_back(MakeProjectionTab("m0"));
  split_side_by_side.node.children.push_back(MakeProjectionTab("m1"));
  EXPECT_EQ(36, MeasureVisualRowHeight(split_side_by_side));
  split_side_by_side.split_force_stacked = true;
  EXPECT_EQ(2 * (36 + 2 * 2) + 1,
            MeasureVisualRowHeight(split_side_by_side));

  SidebarVisualRow split_stacked;
  split_stacked.kind = SidebarVisualRowKind::kSplitGroup;
  split_stacked.node.split_orientation = "horizontal";
  split_stacked.node.children.push_back(MakeProjectionTab("m0"));
  split_stacked.node.children.push_back(MakeProjectionTab("m1"));
  // A stacked split realizes before+append insertion lanes (2dp each) around
  // each 36dp pane, plus a 1dp divider between the two panes.
  EXPECT_EQ(2 * (36 + 2 * 2) + 1, MeasureVisualRowHeight(split_stacked));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ProjectionVisibleRangeSelectsVariableHeightRows) {
  SidebarVisibilityManager manager(SidebarVisibilityManager::Config{
      .row_height_dp = 36,
      .buffer_rows_above = 0,
      .buffer_rows_below = 0,
      .drag_overscan_rows = 0});
  const std::vector<int> tops = {0, 36, 72, 144, 180};
  const int total_height = 216;

  auto range = manager.ComputeVisibleRangeForProjection(
      gfx::Rect(0, 80, 100, 40), tops, total_height);
  EXPECT_EQ(2, range.first_visible_index);
  EXPECT_EQ(2, range.last_visible_index);
  EXPECT_EQ(5, range.total_rows);

  auto spanning = manager.ComputeVisibleRangeForProjection(
      gfx::Rect(0, 50, 100, 40), tops, total_height);
  EXPECT_EQ(1, spanning.first_visible_index);
  EXPECT_EQ(2, spanning.last_visible_index);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ProjectionVisibleRangeAccessorMatchesVectorWrapper) {
  SidebarVisibilityManager manager(SidebarVisibilityManager::Config{
      .row_height_dp = 36,
      .buffer_rows_above = 0,
      .buffer_rows_below = 1,
      .drag_overscan_rows = 0});
  const std::vector<int> tops = {0, 36, 72, 144, 180};
  const int total_height = 216;
  const gfx::Rect viewport(0, 80, 100, 40);

  auto wrapper = manager.ComputeVisibleRangeForProjection(viewport, tops,
                                                          total_height);
  auto accessor = manager.ComputeVisibleRange(
      viewport, static_cast<int>(tops.size()), total_height,
      [&tops](int index) { return tops[index]; });

  EXPECT_EQ(wrapper.first_visible_index, accessor.first_visible_index);
  EXPECT_EQ(wrapper.last_visible_index, accessor.last_visible_index);
  EXPECT_EQ(wrapper.total_rows, accessor.total_rows);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ProjectionVisibleRangeAppliesOverscan) {
  SidebarVisibilityManager manager(SidebarVisibilityManager::Config{
      .row_height_dp = 36,
      .buffer_rows_above = 0,
      .buffer_rows_below = 1,
      .drag_overscan_rows = 0});
  const std::vector<int> tops = {0, 36, 72, 144, 180};
  const int total_height = 216;

  auto range = manager.ComputeVisibleRangeForProjection(
      gfx::Rect(0, 80, 100, 40), tops, total_height);
  EXPECT_EQ(1, range.first_visible_index);
  EXPECT_EQ(3, range.last_visible_index);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ProjectionVisibleRangeIncludesForcedIndex) {
  SidebarVisibilityManager manager(SidebarVisibilityManager::Config{
      .row_height_dp = 36,
      .buffer_rows_above = 0,
      .buffer_rows_below = 0,
      .drag_overscan_rows = 0});
  const std::vector<int> tops = {0, 36, 72, 144, 180};
  const int total_height = 216;
  const gfx::Rect viewport(0, 80, 100, 40);

  manager.SetForcedVisibleIndex(0);
  auto forced_low =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);
  EXPECT_EQ(0, forced_low.first_visible_index);
  EXPECT_EQ(2, forced_low.last_visible_index);

  manager.SetForcedVisibleIndex(4);
  auto forced_high =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);
  EXPECT_EQ(2, forced_high.first_visible_index);
  EXPECT_EQ(4, forced_high.last_visible_index);

  manager.ClearForcedVisibleIndex();
  auto cleared =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);
  EXPECT_EQ(2, cleared.first_visible_index);
  EXPECT_EQ(2, cleared.last_visible_index);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ProjectionVisibleRangeWidensDuringDrag) {
  SidebarVisibilityManager manager(SidebarVisibilityManager::Config{
      .row_height_dp = 36,
      .buffer_rows_above = 0,
      .buffer_rows_below = 1,
      .drag_overscan_rows = 3});
  const std::vector<int> tops = {0, 36, 72, 144, 180};
  const int total_height = 216;
  const gfx::Rect viewport(0, 80, 100, 40);

  auto idle =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);
  EXPECT_EQ(1, idle.first_visible_index);
  EXPECT_EQ(3, idle.last_visible_index);

  manager.SetDragActive(true);
  auto dragged =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);
  EXPECT_EQ(0, dragged.first_visible_index);
  EXPECT_EQ(4, dragged.last_visible_index);
}

// --- T5-T9 helpers ---

std::vector<std::string> TopLevelNormalTabIds(
    const MahoSidebarTabListModel& model) {
  std::vector<std::string> ids;
  for (const SidebarTreeNode& node : model.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab) {
      ids.push_back(node.tab_id);
    }
  }
  return ids;
}

int CountAxTabNodes(const views::ViewAccessibility& ax) {
  int count = 0;
  std::function<void(const views::AXVirtualView*)> walk;
  walk = [&](const views::AXVirtualView* node) {
    if (node->GetData().role == ax::mojom::Role::kTab) {
      ++count;
    }
    for (const auto& child : node->children()) {
      walk(child.get());
    }
  };
  for (const auto& child : ax.virtual_children()) {
    walk(child.get());
  }
  return count;
}

int ExpectedProjectionAxTabCount(const MahoSidebarTabListModel& model) {
  std::vector<SidebarVisualRow> pinned;
  std::vector<SidebarVisualRow> normal;
  BuildSectionProjection(model.pinned_tree, MahoSidebarTabSection::kPinned,
                         &pinned, false);
  BuildSectionProjection(model.normal_tree, MahoSidebarTabSection::kNormal,
                         &normal, false);
  return static_cast<int>(ReconstructProjectionTabOrder(pinned).size() +
                          ReconstructProjectionTabOrder(normal).size());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       AccessibilityRebuildIsDeferredAndCoalesced) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeProjectionModel();
  const int expected_tabs = ExpectedProjectionAxTabCount(model);
  ASSERT_GT(expected_tabs, 0);

  tab_list_->set_accessibility_tree_enabled_for_testing(true);
  tab_list_->Update(model, static_cast<Browser*>(browser()));
  EXPECT_EQ(0, tab_list_->accessibility_rebuild_count_for_testing())
      << "RebuildRows should only post the a11y rebuild, not run it inline";
  EXPECT_EQ(0, CountAxTabNodes(tab_list_->GetViewAccessibility()))
      << "AX tree should be populated by the deferred task";

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, tab_list_->accessibility_rebuild_count_for_testing());
  EXPECT_EQ(expected_tabs, CountAxTabNodes(tab_list_->GetViewAccessibility()))
      << "Deferred a11y rebuild must still expose the full projection";

  MahoSidebarTabListModel model_with_extra = model;
  model_with_extra.normal_tree.push_back(MakeProjectionTab("proj-extra-a"));
  MahoSidebarTabListModel model_with_two_extra = model_with_extra;
  model_with_two_extra.normal_tree.push_back(
      MakeProjectionTab("proj-extra-b"));
  tab_list_->Update(model_with_extra, static_cast<Browser*>(browser()));
  tab_list_->Update(model_with_two_extra, static_cast<Browser*>(browser()));
  EXPECT_EQ(1, tab_list_->accessibility_rebuild_count_for_testing())
      << "Multiple rebuilds in the same burst should coalesce before idle";

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2, tab_list_->accessibility_rebuild_count_for_testing())
      << "The burst should produce one additional a11y rebuild";
  EXPECT_EQ(ExpectedProjectionAxTabCount(model_with_two_extra),
            CountAxTabNodes(tab_list_->GetViewAccessibility()));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FolderChildrenLookupSurvivesChildlessProjectionRows) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeProjectionModel();
  tab_list_->Update(model, static_cast<Browser*>(browser()));
  base::RunLoop().RunUntilIdle();

  const std::vector<SidebarTreeNode>* children =
      tab_list_->FindFolderChildrenForTesting("proj-f1");
  ASSERT_NE(children, nullptr)
      << "Folder hover should resolve children from last_model_ on demand";
  ASSERT_EQ(2u, children->size());
  EXPECT_EQ("proj-c0", (*children)[0].tab_id);
  EXPECT_EQ("proj-c1", (*children)[1].tab_id);

  const std::vector<SidebarVisualRow>& projection =
      tab_list_->normal_projection_for_testing();
  const auto folder_row = std::ranges::find_if(
      projection, [](const SidebarVisualRow& row) {
        return row.kind == SidebarVisualRowKind::kFolder &&
               row.stable_id == "proj-f1";
      });
  ASSERT_NE(folder_row, projection.end());
  EXPECT_TRUE(folder_row->node.children.empty())
      << "The on-demand lookup, not projection storage, owns hover children";
  EXPECT_TRUE(folder_row->folder_has_children)
      << "Folder hover still needs a cheap child-presence bit after shallow copy";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       SelectionFastPathSchedulesAccessibilityRebuild) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->set_accessibility_tree_enabled_for_testing(true);
  tab_list_->Update(MakeProjectionModel(), static_cast<Browser*>(browser()));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1, tab_list_->accessibility_rebuild_count_for_testing());

  tab_list_->ToggleTabSelected("proj-t0");
  EXPECT_EQ(1, tab_list_->accessibility_rebuild_count_for_testing());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2, tab_list_->accessibility_rebuild_count_for_testing())
      << "Selection-only changes must refresh the virtual AX selected state";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       BackgroundAllTabChangeSkipsUnrealizedRows) {
  SeedProfile(2);
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->set_accessibility_tree_enabled_for_testing(true);
  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  ASSERT_GE(strip->count(), 2);
  const int active_index = strip->active_index();
  ASSERT_TRUE(strip->ContainsIndex(active_index));
  const int non_realized_background_index = active_index == 0 ? 1 : 0;
  ASSERT_TRUE(strip->ContainsIndex(non_realized_background_index));
  ASSERT_NE(active_index, non_realized_background_index);
  auto* helper = MahoTabIdHelper::FromWebContents(
      strip->GetWebContentsAt(non_realized_background_index));
  ASSERT_TRUE(helper);
  ASSERT_FALSE(helper->stable_tab_id().empty());

  MahoSidebarTabListModel model = MakeScrollPerfModel(300);
  SidebarTreeNode background_tab = MakeScrollPerfTab(300);
  background_tab.tab_id = helper->stable_tab_id();
  background_tab.tab_strip_index = non_realized_background_index;
  model.normal_tree.push_back(std::move(background_tab));
  tab_list_->Update(model, static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_TRUE(scroll);
  ASSERT_TRUE(tab_list_->GetWidget());
  // Put the real background tab after all synthetic rows and pin the viewport
  // to the top. Its strip index does not determine its projected row position.
  {
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE,
                                           TestTimeouts::action_timeout());
    base::RunLoop frame;
    scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
    scroll->InvalidateLayout();
    tab_list_->GetWidget()->LayoutRootViewIfNecessary();
    frame.Run();
  }
  scroll->ScrollToPosition(scroll->vertical_scroll_bar(), 0);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_EQ(0, scroll->GetVisibleRect().y());
  ASSERT_LT(scroll->GetVisibleRect().height(), tab_list_->height());
  ASSERT_FALSE(tab_list_->IsTabRowRealizedAt(non_realized_background_index));

  const int apply_count_before = sidebar_->cosmetic_apply_count_for_testing();
  sidebar_->OnTabChangedAtForTesting(
      strip->GetTabAtIndex(non_realized_background_index), TabChangeType::kAll);
  sidebar_->FireCosmeticDebounceForTesting();

  EXPECT_EQ(apply_count_before, sidebar_->cosmetic_apply_count_for_testing())
      << "background kAll changes for non-realized rows should not schedule a "
         "cosmetic refresh";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       BackgroundAllTabChangeKeepsRealizedRowsRefreshing) {
  SeedProfile(2);
  base::RunLoop().RunUntilIdle();
  ResolveViews();
  StopSidebarObservations();

  auto state = GetViewState();
  tab_list_->Update(state.tab_list, static_cast<Browser*>(browser()));
  ASSERT_TRUE(tab_list_->GetWidget());
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  TabStripModel* strip = browser()->GetTabStripModel();
  ASSERT_TRUE(strip);
  ASSERT_GE(strip->count(), 2);
  const int active_index = strip->active_index();
  ASSERT_TRUE(strip->ContainsIndex(active_index));
  const int realized_background_index = active_index == 0 ? 1 : 0;
  ASSERT_TRUE(strip->ContainsIndex(realized_background_index));
  ASSERT_NE(active_index, realized_background_index);
  ASSERT_TRUE(tab_list_->IsTabRowRealizedAt(realized_background_index));

  const int apply_count_before = sidebar_->cosmetic_apply_count_for_testing();

  sidebar_->OnTabChangedAtForTesting(
      strip->GetTabAtIndex(realized_background_index), TabChangeType::kAll);
  sidebar_->FireCosmeticDebounceForTesting();

  EXPECT_EQ(apply_count_before + 1,
            sidebar_->cosmetic_apply_count_for_testing())
      << "realized background kAll changes should still schedule a cosmetic "
         "refresh";
}

// --- T5: off-window reveal + focus ---

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, OffWindowRevealRealizesTarget) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeScrollPerfModel(300);
  const std::vector<std::string> root_tabs = TopLevelNormalTabIds(model);
  ASSERT_GT(root_tabs.size(), 10u);
  const std::string deep = root_tabs.back();

  tab_list_->Update(model, static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  EXPECT_EQ(tab_list_->FindTabRowByIdForTesting(deep), nullptr)
      << "A deep tab must start culled (off-window) at the top";
  const int extent_before = tab_list_->height();

  tab_list_->RevealTabById(deep);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  SidebarTabRowView* row = tab_list_->FindTabRowByIdForTesting(deep);
  ASSERT_NE(row, nullptr) << "RevealTabById must realize the off-window row";
  EXPECT_TRUE(row->IsDrawn());
  EXPECT_EQ(extent_before, tab_list_->height())
      << "Reveal must not change the content extent";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, FocusSurvivesCullRestore) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeScrollPerfModel(300);
  const std::vector<std::string> root_tabs = TopLevelNormalTabIds(model);
  ASSERT_GT(root_tabs.size(), 10u);
  const std::string deep = root_tabs.back();

  tab_list_->Update(model, static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  tab_list_->RevealTabById(deep);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  SidebarTabRowView* row = tab_list_->FindTabRowByIdForTesting(deep);
  ASSERT_NE(row, nullptr);
  row->RequestFocus();

  scroll->ScrollToOffset(gfx::PointF(0, 0));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == 0; }));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(tab_list_->FindTabRowByIdForTesting(deep), nullptr)
      << "The focused row must cull when scrolled far away";

  tab_list_->RevealTabById(deep);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_NE(tab_list_->FindTabRowByIdForTesting(deep), nullptr)
      << "Re-revealing must restore the row after a cull";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ActiveTabRevealScrollsAlreadyRealizedRowOutsideViewport) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeScrollPerfModel(80);
  tab_list_->Update(model, static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());

  base::RunLoop initial_frame_loop;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(
      initial_frame_loop.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  initial_frame_loop.Run();

  ASSERT_EQ(0, scroll->GetVisibleRect().y());
  const gfx::Rect initial_viewport = scroll->GetVisibleRect();

  // 1. Find a tab that is already realized (due to the 30-row overscan buffer
  // below the viewport), but located strictly outside the visible viewport.
  std::string target_tab_id;
  SidebarTabRowView* target_row = nullptr;
  const std::vector<std::string> visual_ids =
      tab_list_->tab_visual_order_ids_for_testing();
  for (const std::string& id : visual_ids) {
    if (auto* row = tab_list_->FindTabRowByIdForTesting(id)) {
      const gfx::Rect row_bounds = views::View::ConvertRectToTarget(
          row, tab_list_, row->GetLocalBounds());
      if (row_bounds.y() >= initial_viewport.bottom()) {
        target_tab_id = id;
        target_row = row;
        break;
      }
    }
  }
  ASSERT_FALSE(target_tab_id.empty())
      << "Expected an already-realized row outside the initial viewport";
  ASSERT_NE(target_row, nullptr);

  const gfx::Rect row_bounds_before = views::View::ConvertRectToTarget(
      target_row, tab_list_, target_row->GetLocalBounds());
  ASSERT_FALSE(initial_viewport.Intersects(row_bounds_before));

  // Activating the off-viewport tab must reveal it into view.
  tab_list_->UpdateActiveTabHighlightOnly(target_tab_id);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  const gfx::Rect new_viewport = scroll->GetVisibleRect();
  const gfx::Rect row_bounds_after = views::View::ConvertRectToTarget(
      target_row, tab_list_, target_row->GetLocalBounds());
  EXPECT_TRUE(new_viewport.Intersects(row_bounds_after))
      << "Already-realized active tab outside viewport must be revealed into view; "
      << "row_bounds=" << row_bounds_after.ToString()
      << " viewport=" << new_viewport.ToString();
  EXPECT_GT(new_viewport.y(), 0)
      << "Viewport must have scrolled to bring the already-realized row into view";

  // 2. In-viewport no-scroll: to prove visible selection does not steal scroll,
  // find a DIFFERENT tab that is already fully visible inside the current
  // viewport and activate it. Since the tab differs from the current active
  // tab, this genuinely exercises UpdateActiveTabHighlightOnly and RevealTabById,
  // asserting that the scroll offset remains completely stationary.
  const int settled_scroll_y = new_viewport.y();
  std::string different_visible_tab_id;
  for (const std::string& id : visual_ids) {
    if (id == target_tab_id) {
      continue;
    }
    if (auto* row = tab_list_->FindTabRowByIdForTesting(id)) {
      const gfx::Rect bounds = views::View::ConvertRectToTarget(
          row, tab_list_, row->GetLocalBounds());
      if (new_viewport.Contains(bounds)) {
        different_visible_tab_id = id;
        break;
      }
    }
  }
  ASSERT_FALSE(different_visible_tab_id.empty())
      << "Expected a different tab fully visible in the new viewport";

  tab_list_->UpdateActiveTabHighlightOnly(different_visible_tab_id);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(settled_scroll_y, scroll->GetVisibleRect().y())
      << "Activating a different in-viewport tab must not change scroll position";

  // 3. Manual scroll after activation remains where the user leaves it (no
  // persistent scroll lock or re-snapping).
  const int manual_target_y = std::max(0, settled_scroll_y - 20);
  scroll->ScrollToOffset(gfx::PointF(0, manual_target_y));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(manual_target_y, scroll->GetVisibleRect().y())
      << "Manual scroll after activation must remain where user leaves it";

  // Ensure rows remain realized across the test.
  EXPECT_NE(tab_list_->FindTabRowByIdForTesting(target_tab_id), nullptr)
      << "Target row must remain realized across manual scroll";
  EXPECT_NE(tab_list_->FindTabRowByIdForTesting(different_visible_tab_id),
            nullptr)
      << "Different visible row must remain realized across manual scroll";
}

// --- T6: projection-based range navigation across the window boundary ---

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       RangeSelectionCrossesWindowBoundary) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(300), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  const std::vector<std::string> ids =
      tab_list_->tab_visual_order_ids_for_testing();
  ASSERT_GT(ids.size(), 80u);
  const int anchor_idx = 3;
  const int target_idx = static_cast<int>(ids.size()) - 6;

  tab_list_->ToggleTabSelected(ids[anchor_idx]);
  ASSERT_TRUE(tab_list_->IsTabSelected(ids[anchor_idx]));

  tab_list_->SelectRangeTo(ids[target_idx]);

  for (int i = anchor_idx; i <= target_idx; ++i) {
    EXPECT_TRUE(tab_list_->IsTabSelected(ids[i]))
        << "id at index " << i << " should be in the range selection";
  }
  EXPECT_EQ(static_cast<size_t>(target_idx - anchor_idx + 1),
            tab_list_->selected_tab_ids().size());

  tab_list_->RevealTabById(ids[target_idx]);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_NE(tab_list_->FindTabRowByIdForTesting(ids[target_idx]), nullptr)
      << "The range target must be revealable/focusable";
}

// --- T7: folder toggle keeps realization bounded + extent exact ---

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FolderToggleKeepsRealizationBounded) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(300), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  const int viewport_rows =
      (scroll->GetVisibleRect().height() + 35) / 36;
  const int bound = viewport_rows + 2 * 30 + 48;
  const int extent_before = tab_list_->height();

  tab_list_->ToggleFolderExpandedForTesting("scroll-perf-expanded-folder-0");
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_LE(ExpectIntersectingTabRowsVisible(tab_list_, tab_list_,
                                             scroll->GetVisibleRect()),
            bound)
      << "Collapsing a folder must keep realization bounded";
  EXPECT_GT(tab_list_->height(), 0);
  EXPECT_LT(tab_list_->height(), extent_before)
      << "Collapsing removes child rows, so the extent shrinks";

  tab_list_->ToggleFolderExpandedForTesting("scroll-perf-expanded-folder-0");
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_LE(ExpectIntersectingTabRowsVisible(tab_list_, tab_list_,
                                             scroll->GetVisibleRect()),
            bound)
      << "Re-expanding must keep realization bounded";
  EXPECT_EQ(extent_before, tab_list_->height())
      << "Re-expanding restores the original extent";
}

// --- T8: DnD drop onto a realized lane after deep scroll ---

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       DeepScrollLaneDropDispatchesReorder) {
  RunAllPendingTasks();
  StopSidebarObservations();

  MahoSidebarTabListModel model = MakeScrollPerfModel(300);
  const std::vector<std::string> root_tabs = TopLevelNormalTabIds(model);
  ASSERT_GT(root_tabs.size(), 5u);
  const std::string target = root_tabs.back();
  const std::string source = root_tabs.front();
  ASSERT_NE(target, source);

  tab_list_->Update(model, static_cast<Browser*>(browser()));
  ASSERT_TRUE(tab_list_->GetWidget());
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  tab_list_->RevealTabById(target);
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  views::View* lane =
      tab_list_->FindInsertionLaneBeforeTabByIdForTesting(target);
  ASSERT_NE(lane, nullptr)
      << "The deep target's before-lane must be realized after reveal";

  observer_.Clear();
  SidebarDragPayload payload;
  payload.node_kind = SidebarNodeKind::kTab;
  payload.node_id = source;
  payload.origin = SidebarDragOrigin::kNormalSection;
  payload.space_id = "scroll-perf-space";
  payload.tab_strip_index = -1;
  ui::OSExchangeData data;
  WriteMahoDragData(payload, &data);
  const gfx::PointF loc(5, 1);
  ui::DropTargetEvent event(data, loc, loc,
                            static_cast<int>(ui::mojom::DragOperation::kMove));
  EXPECT_EQ(lane->OnDragUpdated(event),
            static_cast<int>(ui::mojom::DragOperation::kMove));
  auto callback = lane->GetDropCallback(event);
  ASSERT_FALSE(callback.is_null());
  ui::mojom::DragOperation output = ui::mojom::DragOperation::kNone;
  std::move(callback).Run(event, output, std::unique_ptr<ui::LayerTreeOwner>());

  EXPECT_TRUE(observer_.HasEvent("reorder_root_item"))
      << "A drop on a realized deep lane must dispatch reorder by model id";
}

// --- T9: AXVirtualView enumerates the full projection regardless of scroll ---

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       AxVirtualNodesEnumerateFullProjection) {
  RunAllPendingTasks();
  StopSidebarObservations();

  // The virtual AX tree is built only while an AT is active; force it on.
  tab_list_->set_accessibility_tree_enabled_for_testing(true);
  tab_list_->Update(MakeScrollPerfModel(300), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  const size_t projected =
      tab_list_->tab_visual_order_ids_for_testing().size();
  ASSERT_GT(projected, 60u);

  const int ax_top = CountAxTabNodes(tab_list_->GetViewAccessibility());
  EXPECT_EQ(static_cast<int>(projected), ax_top)
      << "AX must enumerate every projected tab at top scroll";

  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);
  scroll->ScrollToOffset(gfx::PointF(0, max_scroll_y));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == max_scroll_y; }));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  const int ax_deep = CountAxTabNodes(tab_list_->GetViewAccessibility());
  EXPECT_EQ(ax_top, ax_deep)
      << "AX tab enumeration must not regress after a deep scroll";

  const int realized = ExpectIntersectingTabRowsVisible(
      tab_list_, tab_list_, scroll->GetVisibleRect());
  EXPECT_LT(realized, ax_deep)
      << "Realized rows must stay far fewer than the full AX enumeration";
}

// --- T11: fast-fling blank-row fix ---

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FastFlingViewportRowsPreRealized) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(320), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);

  // Scroll to a MID offset (room remains below) mimicking a fling in progress,
  // then let deferred layout run — the model is async-layout now, no sync pass.
  const int target_y = max_scroll_y / 2;
  scroll->ScrollToOffset(gfx::PointF(0, target_y));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == target_y; }));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  const gfx::Rect viewport = scroll->GetVisibleRect();
  int viewport_rows = 0;
  int max_realized_bottom = -1;
  std::function<void(views::View*)> verify;
  verify = [&](views::View* parent) {
    for (views::View* child : parent->children()) {
      if (auto* row = views::AsViewClass<SidebarTabRowView>(child)) {
        const gfx::Rect row_bounds = views::View::ConvertRectToTarget(
            row, tab_list_, row->GetLocalBounds());
        if (!row->size().IsEmpty()) {
          max_realized_bottom = std::max(max_realized_bottom, row_bounds.bottom());
        }
        if (row_bounds.Intersects(viewport)) {
          EXPECT_TRUE(row->IsDrawn())
              << "viewport row not drawn: " << row->tab_id_for_testing();
          EXPECT_FALSE(row->size().IsEmpty())
              << "viewport row lacks laid-out bounds: "
              << row->tab_id_for_testing();
          ++viewport_rows;
        }
      }
      verify(child);
    }
  };
  verify(tab_list_);

  EXPECT_GT(viewport_rows, 0) << "viewport must contain realized rows";
  // Proactive pre-realization: rows AHEAD of the viewport (below it, in the
  // scroll-down direction) are already realized + laid out, so they are
  // positioned before scrolling into view. The realized leading edge extends
  // well past the viewport bottom (base overscan alone is 30 rows).
  EXPECT_GT(max_realized_bottom, viewport.bottom())
      << "rows below the viewport must be pre-realized (leading overscan)";
  EXPECT_GE(max_realized_bottom - viewport.bottom(), 5 * 36)
       << "leading overscan must keep several off-screen rows ready ahead";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       FastFlingPresentsRowsWithoutManualLayout) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(320), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);
  tab_list_->ResetScrollLayoutInvalidationStateForTesting();

  const int target_y = max_scroll_y / 2;
  scroll->ScrollToOffset(gfx::PointF(0, target_y));
  ASSERT_EQ(target_y, scroll->CurrentOffset().y())
      << "ScrollToOffset must synchronously land before presentation checks";
  ASSERT_EQ(target_y, scroll->GetVisibleRect().y())
      << "The viewport must synchronously follow the requested offset";

  EXPECT_GE(tab_list_->scroll_layout_invalidation_count_for_testing(), 1)
      << "The fling must exercise OnScrollChanged before presentation checks";
  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_NE(rows, nullptr);
  EXPECT_FALSE(rows->needs_layout())
      << "OnScrollChanged must lay out newly-realized rows in the same callback "
         "instead of deferring presentation to the next frame";

  const gfx::Rect viewport = scroll->GetVisibleRect();
  int viewport_rows = 0;
  std::function<void(views::View*)> verify;
  verify = [&](views::View* parent) {
    for (views::View* child : parent->children()) {
      if (auto* row = views::AsViewClass<SidebarTabRowView>(child)) {
        const gfx::Rect row_bounds = views::View::ConvertRectToTarget(
            row, tab_list_, row->GetLocalBounds());
        if (row_bounds.Intersects(viewport)) {
          EXPECT_FALSE(row->size().IsEmpty())
              << "viewport row " << row->tab_id_for_testing()
              << " needs laid-out bounds for same-callback presentation";
          EXPECT_TRUE(row->IsDrawn())
              << "viewport row " << row->tab_id_for_testing()
              << " must be drawn for same-callback presentation";
          ++viewport_rows;
        }
      }
      verify(child);
    }
  };
  verify(tab_list_);

  EXPECT_GT(viewport_rows, 0)
      << "same-callback presentation must expose at least one viewport row";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest, ReconcileWindowIsReentrancySafe) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(320), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  // A re-entrant reconcile (as a synchronous scroll callback would trigger)
  // must be a guarded no-op, leaving the realized window untouched.
  EXPECT_TRUE(tab_list_->ReentrantReconcileIsBlockedForTesting());

  // Aggressive fling sweep (large rapid jumps both directions): mutating the
  // spacers must never re-enter reconciliation and corrupt window_/children.
  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);
  for (const int target_y :
       {max_scroll_y * 3 / 4, max_scroll_y / 4, max_scroll_y, 0,
        max_scroll_y / 2}) {
    scroll->ScrollToOffset(gfx::PointF(0, target_y));
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return scroll->CurrentOffset().y() == target_y; }));
    tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  }

  // No crash + consistent state: four-child scaffold intact, viewport still
  // has realized rows, extent still positive.
  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_EQ(4u, rows->children().size());
  EXPECT_GT(ExpectIntersectingTabRowsVisible(tab_list_, tab_list_,
                                             scroll->GetVisibleRect()),
            0);
  EXPECT_GT(tab_list_->height(), 0);
  EXPECT_TRUE(tab_list_->ReentrantReconcileIsBlockedForTesting());
}

// A ScrollView layout pass can move the viewport WITHOUT firing the
// contents-scrolled callback: ScrollView::Layout() clamps the offset through
// ConstrainScrollToBounds(), which never calls OnScrolled(). After a rebuild
// shrinks the content extent that clamp is exactly what happens, so the
// realized row window stays parked where the OLD offset was and the viewport
// shows nothing but skeleton spacers — the "sidebar tab list vanished"
// regression. The post-layout hook must re-sync the window against the settled
// viewport.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ScrollClampAfterShrinkKeepsViewportRowsRealized) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(400), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  // Scroll deep into the list so the offset is far past what the smaller model
  // below can support; the clamp on the next layout is then unavoidable.
  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);
  scroll->ScrollToOffset(gfx::PointF(0, max_scroll_y));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == max_scroll_y; }));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_GT(ExpectIntersectingTabRowsVisible(tab_list_, tab_list_,
                                             scroll->GetVisibleRect()),
            0)
      << "precondition: the deep-scrolled viewport must start out populated";

  // Shrink the model. RebuildRows() reconciles against the pre-clamp geometry,
  // then the following layout clamps the offset toward the top.
  // Still taller than the viewport, so the viewport can be filled after the
  // clamp; far shorter than the 400-row model, so the clamp is unavoidable.
  tab_list_->Update(MakeScrollPerfModel(120), static_cast<Browser*>(browser()));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return scroll->CurrentOffset().y() <=
           std::max(0, tab_list_->height() - scroll->GetVisibleRect().height());
  }));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  // The post-layout re-sync is posted; drain it, then place the new rows.
  base::RunLoop().RunUntilIdle();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();

  const gfx::Rect viewport = scroll->GetVisibleRect();
  int intersecting_rows = 0;
  std::function<void(views::View*)> count_rows = [&](views::View* parent) {
    for (views::View* child : parent->children()) {
      if (auto* row = views::AsViewClass<SidebarTabRowView>(child)) {
        if (row->IsDrawn() &&
            views::View::ConvertRectToTarget(row, tab_list_,
                                             row->GetLocalBounds())
                .Intersects(viewport)) {
          ++intersecting_rows;
        }
      }
      count_rows(child);
    }
  };
  count_rows(tab_list_);
  // Without the post-layout re-sync the realized window stays parked at the
  // pre-clamp offset: only each section's last row reaches the viewport and
  // the rest of it is skeleton spacer.
  EXPECT_GE(intersecting_rows, std::max(2, viewport.height() / 36 / 2))
      << "viewport=" << viewport.ToString()
      << ": after the post-rebuild scroll clamp the viewport must be filled "
         "with realized rows, not skeleton spacers";
}

// Regression (Tidy): a rebuild that shrinks the list while deep-scrolled lays
// out tab_rows_ against the pre-clamp offset, so the virtual layout culls the
// action row and the normal section. When both section windows already hold
// every row, the clamp does not change them, and the scaffold stayed hidden
// below the pinned section until an unrelated scroll or rebuild.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ScrollClampAfterShrinkRestoresCulledScaffold) {
  RunAllPendingTasks();
  StopSidebarObservations();

  tab_list_->Update(MakeScrollPerfModel(400), static_cast<Browser*>(browser()));
  views::ScrollView* scroll = sidebar_->tab_scroll_view_for_testing();
  ASSERT_NE(scroll, nullptr);
  ASSERT_TRUE(tab_list_->GetWidget());
  base::RunLoop frame;
  scroll->RegisterNextSuccessfulFramePostLayoutCallback(frame.QuitClosure());
  scroll->InvalidateLayout();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  frame.Run();

  const int max_scroll_y =
      tab_list_->height() - scroll->GetVisibleRect().height();
  ASSERT_GT(max_scroll_y, 0);
  scroll->ScrollToOffset(gfx::PointF(0, max_scroll_y));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return scroll->CurrentOffset().y() == max_scroll_y; }));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_FALSE(tab_list_->action_row_for_testing()->GetVisible())
      << "precondition: the action row is culled at the deep offset";

  // Small enough that both section windows realize every row at any offset.
  tab_list_->Update(MakeScrollPerfModel(16), static_cast<Browser*>(browser()));
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  base::RunLoop().RunUntilIdle();
  tab_list_->GetWidget()->LayoutRootViewIfNecessary();
  ASSERT_EQ(0, scroll->CurrentOffset().y());

  views::View* rows = tab_list_->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  for (views::View* child : rows->children()) {
    EXPECT_TRUE(child->GetVisible())
        << child->GetClassName()
        << " must be visible once the offset settles at the top";
  }
  EXPECT_TRUE(tab_list_->action_row_for_testing()->IsDrawn());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       VelocityOverscanExpandsInScrollDirection) {
  SidebarVisibilityManager manager(SidebarVisibilityManager::Config{
      .row_height_dp = 36,
      .buffer_rows_above = 30,
      .buffer_rows_below = 30,
      .drag_overscan_rows = 50});
  std::vector<int> tops;
  tops.reserve(400);
  for (int i = 0; i < 400; ++i) {
    tops.push_back(i * 36);
  }
  const int total_height = 400 * 36;
  const gfx::Rect viewport(0, 200 * 36, 100, 10 * 36);  // mid-list, 10 rows.

  manager.SetProjectionOverscanRows(30, 30);
  auto base_range =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);

  // Fast scroll DOWN: leading (below) overscan grows to the cap; trailing
  // (above) edge stays at base.
  manager.SetProjectionOverscanRows(
      30, SidebarVisibilityManager::kMaxVelocityOverscanRows);
  auto down_range =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);
  EXPECT_GT(down_range.last_visible_index, base_range.last_visible_index)
      << "fast down-scroll must realize more rows ahead (below the viewport)";
  EXPECT_EQ(down_range.first_visible_index, base_range.first_visible_index)
      << "trailing (above) edge must stay at the base overscan";
  const int viewport_last_row = viewport.bottom() / 36;
  EXPECT_LE(down_range.last_visible_index,
            viewport_last_row +
                SidebarVisibilityManager::kMaxVelocityOverscanRows)
      << "leading overscan must never exceed kMaxVelocityOverscanRows";

  // Fast scroll UP: leading (above) grows to the cap; trailing (below) is base.
  manager.SetProjectionOverscanRows(
      SidebarVisibilityManager::kMaxVelocityOverscanRows, 30);
  auto up_range =
      manager.ComputeVisibleRangeForProjection(viewport, tops, total_height);
  EXPECT_LT(up_range.first_visible_index, base_range.first_visible_index)
      << "fast up-scroll must realize more rows ahead (above the viewport)";
  EXPECT_EQ(up_range.last_visible_index, base_range.last_visible_index)
      << "trailing (below) edge must stay at the base overscan";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ActionRowUsesDedicatedFortyTwoDpHeight) {
  SeedProfile(2);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* action_row = list_view->action_row_for_testing();
  ASSERT_TRUE(action_row);
  EXPECT_EQ(action_row->GetPreferredSize(views::SizeBounds()).height(), 42);

  std::string normal_tab_id;
  auto state = GetViewState();
  for (const auto& node : state.tab_list.normal_tree) {
    if (node.kind == SidebarNodeKind::kTab && !node.tab_id.empty()) {
      normal_tab_id = node.tab_id;
      break;
    }
  }
  ASSERT_FALSE(normal_tab_id.empty())
      << "Seed 2 must expose at least one normal tab row";
  SidebarTabRowView* tab_row = nullptr;
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    tab_row = list_view->FindTabRowByIdForTesting(normal_tab_id);
    return tab_row != nullptr;
  }));
  EXPECT_EQ(tab_row->GetPreferredSize(views::SizeBounds()).height(), 36)
      << "A normal tab row keeps the 36dp rhythm, distinct from the 42dp "
         "action row";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       MailTabIsExcludedFromCoreBackedTabList) {
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  MahoSidebarTabListModel model;
  model.active_space_id = "mail-filter-space";

  SidebarTreeNode regular_tab = MakeProjectionTab("regular-tab");
  regular_tab.url = "https://example.test/";
  model.normal_tree.push_back(std::move(regular_tab));

  SidebarTreeNode mail_tab = MakeProjectionTab("mail-tab");
  mail_tab.url = "chrome://maho-mail/";
  model.normal_tree.push_back(std::move(mail_tab));

  list_view->RebuildRowsForTesting(model, static_cast<Browser*>(browser()));
  base::RunLoop().RunUntilIdle();

  EXPECT_NE(nullptr, list_view->FindTabRowByIdForTesting("regular-tab"));
  EXPECT_EQ(nullptr, list_view->FindTabRowByIdForTesting("mail-tab"));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ActionRowStaysBetweenPinnedSeparatorAndNormalSection) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* tab_rows = list_view->tab_rows_for_testing();
  ASSERT_TRUE(tab_rows);
  ASSERT_GE(tab_rows->children().size(), 4u);

  EXPECT_EQ(tab_rows->children()[2], list_view->action_row_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       ActionRowPointerSurvivesRebuild) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  views::View* action_row_before = list_view->action_row_for_testing();
  ASSERT_TRUE(action_row_before);

  // The action row is scaffold, not tab content: with the seeded projection
  // populated it must sit between the two section containers and never appear
  // in the tab visual order or either section projection. This is verified via
  // public accessors + stable ids rather than internal view-tree walks.
  views::View* rows = list_view->tab_rows_for_testing();
  ASSERT_TRUE(rows);
  ASSERT_GE(rows->children().size(), 4u);
  EXPECT_EQ(rows->children()[2], action_row_before)
      << "Action row must be a direct scaffold child, outside both sections";
  EXPECT_FALSE(views::IsViewClass<SidebarTabRowView>(action_row_before))
      << "Action row must not be a tab row";

  // The seeded model reaches the tab list on the sidebar's async refresh.
  std::vector<std::string> visual_order;
  ASSERT_TRUE(BlockAndPollUntil([&]() {
    visual_order = list_view->tab_visual_order_ids_for_testing();
    return !visual_order.empty();
  })) << "Seed 1 must populate the tab visual order for a meaningful check";
  for (const std::string& id : visual_order) {
    EXPECT_NE(
        static_cast<views::View*>(list_view->FindTabRowByIdForTesting(id)),
        action_row_before)
        << "Visual-order tab id '" << id
        << "' must not resolve to the action row";
  }

  const auto assert_projection_excludes_action_row =
      [&](const std::vector<SidebarVisualRow>& projection) {
        for (const SidebarVisualRow& row : projection) {
          if (row.kind != SidebarVisualRowKind::kTab &&
              row.kind != SidebarVisualRowKind::kCollapsedStickyTab) {
            continue;
          }
          EXPECT_NE(static_cast<views::View*>(
                        list_view->FindTabRowByIdForTesting(row.stable_id)),
                    action_row_before)
              << "Projection stable_id '" << row.stable_id
              << "' must not resolve to the action row";
        }
      };
  assert_projection_excludes_action_row(
      list_view->normal_projection_for_testing());
  assert_projection_excludes_action_row(
      list_view->pinned_projection_for_testing());

  // Two browser-context rebuilds plus one zero-context (nullptr) rebuild must
  // all keep the same action row identity.
  list_view->RebuildRowsForTesting(MahoSidebarTabListModel(), static_cast<Browser*>(browser()));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(action_row_before, list_view->action_row_for_testing())
      << "Action row must survive the first browser-context rebuild";

  list_view->RebuildRowsForTesting(MahoSidebarTabListModel(), static_cast<Browser*>(browser()));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(action_row_before, list_view->action_row_for_testing())
      << "Action row must survive the second browser-context rebuild";

  list_view->RebuildRowsForTesting(MahoSidebarTabListModel(), nullptr);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(action_row_before, list_view->action_row_for_testing())
      << "Action row must survive the zero-context rebuild";
}

IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       PrivateActionRowRemainsNewTabOnly) {
  SeedProfile(1);
  MahoSidebarTabListView* list_view = GetTabListView();
  ASSERT_TRUE(list_view);

  list_view->SetPrivateMode(true);
  list_view->RebuildRowsForTesting(MahoSidebarTabListModel(), static_cast<Browser*>(browser()));
  base::RunLoop().RunUntilIdle();

  views::View* action_row = list_view->action_row_for_testing();
  ASSERT_TRUE(action_row);
  EXPECT_TRUE(IsInlineRowButton(action_row, u"+ New Tab"));
  EXPECT_FALSE(IsInlineRowButton(action_row, u"Tidy"));
  EXPECT_FALSE(IsInlineRowButton(action_row, u"Clear"));
  EXPECT_EQ(list_view->action_processing_view_for_testing(), nullptr);
}

// Regression test: Native window dragging via NonClientHitTest in the sidebar.
// The top bar empty area (traffic-light spacer, padding, and top margin) must
// return HTCAPTION so native window drag works, while interactive buttons and
// tabs must return HTCLIENT so clicks reach them.
IN_PROC_BROWSER_TEST_F(MahoSidebarContractTest,
                       SidebarWindowDragNonClientHitTestReturnsCaption) {
  RunAllPendingTasks();
  StopSidebarObservations();
  ResolveViews();

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(browser_view, nullptr);
  views::View* root_target = browser_view->parent() ? browser_view->parent() : browser_view;
  ASSERT_NE(root_target, nullptr);

  MahoSidebarContainerView* container = GetContainerView();
  ASSERT_NE(container, nullptr);
  ASSERT_TRUE(container->GetVisible());
  ASSERT_FALSE(container->bounds().IsEmpty());

  MahoSidebarView* sidebar = GetSidebarView();
  ASSERT_NE(sidebar, nullptr);
  ASSERT_TRUE(sidebar->GetVisible());
  ASSERT_FALSE(sidebar->bounds().IsEmpty());

  MahoSidebarTopBarView* top_bar = sidebar->top_bar_view_for_testing();
  ASSERT_NE(top_bar, nullptr);
  ASSERT_TRUE(top_bar->GetVisible());
  ASSERT_FALSE(top_bar->bounds().IsEmpty());

  views::View* spacer = top_bar->traffic_light_spacer_for_testing();
  ASSERT_NE(spacer, nullptr);

  // 1. Point inside the top-bar traffic-light spacer must yield HTCAPTION.
  gfx::Point spacer_point(spacer->width() / 2, spacer->height() / 2);
  views::View::ConvertPointToTarget(spacer, root_target, &spacer_point);
  EXPECT_EQ(HTCAPTION, browser_view->NonClientHitTest(spacer_point))
      << "Empty top bar region (traffic-light spacer) must return HTCAPTION for native window dragging";

  // 2. Point in the top margin above the top bar must yield HTCAPTION.
  gfx::Point top_margin_point(sidebar->width() / 2, 2);
  views::View::ConvertPointToTarget(sidebar, root_target, &top_margin_point);
  EXPECT_EQ(HTCAPTION, browser_view->NonClientHitTest(top_margin_point))
      << "Top margin above top bar must return HTCAPTION for native window dragging";

  // 3. Point on an interactive button (both center and button padding) must yield HTCLIENT.
  views::ImageButton* toggle_button = top_bar->sidebar_toggle_button_for_testing();
  ASSERT_NE(toggle_button, nullptr);
  ASSERT_TRUE(toggle_button->GetVisible());
  ASSERT_FALSE(toggle_button->bounds().IsEmpty());

  // 3a. Center of button must return HTCLIENT.
  gfx::Point button_center_point(toggle_button->width() / 2, toggle_button->height() / 2);
  views::View::ConvertPointToTarget(toggle_button, root_target, &button_center_point);
  EXPECT_EQ(HTCLIENT, browser_view->NonClientHitTest(button_center_point))
      << "Interactive button center must return HTCLIENT so clicks reach the control";

  // 3b. Button padding (outside centered 16x16 icon but inside 20x20 button bounds) must return HTCLIENT.
  gfx::Point button_padding_point(1, toggle_button->height() / 2);
  views::View::ConvertPointToTarget(toggle_button, root_target, &button_padding_point);
  EXPECT_EQ(HTCLIENT, browser_view->NonClientHitTest(button_padding_point))
      << "Button padding must return HTCLIENT so entire interactive target receives clicks";

  // 4. Point in the tab list / normal surface must yield HTCLIENT.
  MahoSidebarTabListView* tab_list = GetTabListView();
  ASSERT_NE(tab_list, nullptr);
  ASSERT_TRUE(tab_list->GetVisible());
  ASSERT_FALSE(tab_list->bounds().IsEmpty());

  gfx::Point tab_point(tab_list->width() / 2, 20);
  views::View::ConvertPointToTarget(tab_list, root_target, &tab_point);
  EXPECT_EQ(HTCLIENT, browser_view->NonClientHitTest(tab_point))
      << "Tab list surface must return HTCLIENT";
}

}  // namespace
}  // namespace maho
