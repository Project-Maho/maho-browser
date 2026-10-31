// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.h"

#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "content/public/test/browser_test.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"
#include "ui/events/test/test_event.h"

namespace maho {
namespace {

constexpr base::TimeDelta kTabPreviewWait = base::Milliseconds(350);

void WaitForPotentialPreviewTimer() {
  base::RunLoop run_loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), kTabPreviewWait);
  run_loop.Run();
}

class MahoSidebarTabPreviewInteractiveTest
    : public MahoSidebarInteractiveTestBase {};

void HoverRow(SidebarTabRowView* row) {
  ASSERT_TRUE(row);
  const gfx::Point center = row->GetBoundsInScreen().CenterPoint();
  ui::MouseEvent enter_event(ui::EventType::kMouseEntered,
                             gfx::PointF(center), gfx::PointF(center),
                             base::TimeTicks::Now(), 0, 0);
  row->OnMouseEntered(enter_event);
}

void ExitRow(SidebarTabRowView* row) {
  ASSERT_TRUE(row);
  const gfx::Point origin = row->GetBoundsInScreen().origin();
  ui::MouseEvent exit_event(ui::EventType::kMouseExited, gfx::PointF(origin),
                            gfx::PointF(origin), base::TimeTicks::Now(), 0, 0);
  row->OnMouseExited(exit_event);
}

IN_PROC_BROWSER_TEST_F(MahoSidebarTabPreviewInteractiveTest,
                       HoverNonActiveTabShowsPreviewBubble) {
  AddTestTabs(2);
  ResolveViews();
  // Drain all pending sidebar update tasks so the row hierarchy is stable
  // before we grab a raw pointer and hover. Without this, in-flight
  // BuildTabListModel callbacks can call RebuildRows() while RunUntil() pumps
  // the loop, destroying the anchor view and cancelling the preview timer.
  base::RunLoop().RunUntilIdle();

  GetTabListView()->SetTabPreviewShowDelayForTesting(base::TimeDelta());

  auto* row = GetTabListView()->FindFirstInactiveTabRowForTesting();
  ASSERT_TRUE(row);
  ASSERT_GE(row->live_tab_index_for_testing(), 0);

  HoverRow(row);

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return GetTabListView()->IsTabPreviewShowingForTesting();
  }));

  EXPECT_NE(nullptr, GetTabListView()->tab_preview_widget_for_testing());
  EXPECT_FALSE(GetTabListView()->tab_preview_title_for_testing().empty());
  EXPECT_EQ(base::UTF8ToUTF16(browser()->GetTabStripModel()
                                  ->GetWebContentsAt(row->live_tab_index_for_testing())
                                  ->GetVisibleURL()
                                  .spec()),
            GetTabListView()->tab_preview_url_for_testing());
}

IN_PROC_BROWSER_TEST_F(MahoSidebarTabPreviewInteractiveTest,
                       MovingOffRowDismissesPreviewBubble) {
  AddTestTabs(2);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  GetTabListView()->SetTabPreviewShowDelayForTesting(base::TimeDelta());

  auto* row = GetTabListView()->FindFirstInactiveTabRowForTesting();
  ASSERT_TRUE(row);

  HoverRow(row);
  ASSERT_TRUE(base::test::RunUntil([this]() {
    return GetTabListView()->IsTabPreviewShowingForTesting();
  }));

  ExitRow(row);

  ASSERT_TRUE(base::test::RunUntil([this]() {
    return !GetTabListView()->IsTabPreviewShowingForTesting();
  }));
}

IN_PROC_BROWSER_TEST_F(MahoSidebarTabPreviewInteractiveTest,
                       HoverActiveTabDoesNotShowPreviewBubble) {
  AddTestTabs(2);
  ResolveViews();
  base::RunLoop().RunUntilIdle();

  // Switch to tab 0 (the initial NTP) whose stable_tab_id is guaranteed to be
  // populated; test-added tabs may not be registered yet.
  browser()->GetTabStripModel()->ActivateTabAt(0);
  base::RunLoop().RunUntilIdle();

  GetTabListView()->SetTabPreviewShowDelayForTesting(base::TimeDelta());

  auto* active_row = GetTabListView()->FindActiveTabRowForTesting();
  ASSERT_TRUE(active_row);

  HoverRow(active_row);
  WaitForPotentialPreviewTimer();

  EXPECT_FALSE(GetTabListView()->IsTabPreviewShowingForTesting());
  EXPECT_EQ(nullptr, GetTabListView()->tab_preview_widget_for_testing());
}

}  // namespace
}  // namespace maho
