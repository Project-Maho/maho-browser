// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_INTERACTIVE_TEST_BASE_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_INTERACTIVE_TEST_BASE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "base/timer/timer.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_archive_view.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "ui/gfx/geometry/point.h"
#include "url/gurl.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "base/task/single_thread_task_runner.h"

namespace base {
class CommandLine;
}

namespace views {
class View;
}

namespace maho {

class MahoSidebarContainerView;
class MahoSidebarView;
class MahoSidebarTabListView;
class MahoSidebarFavoritesGridView;
class MahoSidebarFooterView;

// Records shell events dispatched during tests for assertion.
class RecordingShellEventObserver : public ShellEventObserver {
 public:
  struct Event {
    std::string kind;
    std::string json;
  };

  RecordingShellEventObserver();
  ~RecordingShellEventObserver() override;

  void OnShellEventDispatched(const std::string& kind,
                              const std::string& event_json) override;

  const std::vector<Event>& events() const { return events_; }
  void Clear() { events_.clear(); }
  bool HasEvent(const std::string& kind) const;

 private:
  std::vector<Event> events_;
};

// Base class for Maho sidebar interactive browser tests.
class MahoSidebarInteractiveTestBase : public InteractiveBrowserTest {
 public:
  MahoSidebarInteractiveTestBase();
  ~MahoSidebarInteractiveTestBase() override;

  void SetUpCommandLine(base::CommandLine* command_line) override;
  void PreRunTestOnMainThread() override;
  void SetUpOnMainThread() override;
  void TearDownOnMainThread() override;

 protected:
  // View resolution (same pattern as existing browser test).
  void ResolveViews();
  MahoSidebarView* GetSidebarView();
  MahoSidebarTabListView* GetTabListView();
  MahoSidebarFavoritesGridView* GetFavoritesView();
  MahoSidebarFooterView* GetFooterView();
  MahoSidebarContainerView* GetContainerView();

  // State helpers.
  MahoSidebarViewStateModel GetViewState();
  std::string GetActiveSpaceId();

  // Tab manipulation helpers.
  void AddTestTab(const GURL& url, const std::u16string& title);
  void AddTestTabs(int count);

  // Seed helpers (navigates to chrome://maho-test?seed=N).
  void SeedProfile(int seed_number);
  void SeedSidebarPrefs(bool layout_enabled, bool panel_expanded);
  void StopSidebarObservations();
  void RunAllPendingTasks();

  // Archive state helpers — explicit provenance distinction.
  //
  // SeedArchivedTabs: test-injection path. Bypasses production FFI.
  // Use for behavioral tests where archive content is required but production
  // FFI backing is not the claim under test. Label captures from this path
  // as "test-injection" in any provenance record.
  //
  // ReloadArchivedTabsFromFFI: production FFI path. Calls ReloadArchivedTabs()
  // on the archive view, which calls maho_core_get_archived_tabs. Use this when
  // the claim under test is that the production loading path is wired correctly.
  // On a clean profile this returns empty — that is the expected result and is
  // a valid empty-archive-state capture artifact.
  void SeedArchivedTabs(std::vector<ArchivedTabItem> tabs);
  void ReloadArchivedTabsFromFFI();

  void ExpectSidebarLayoutEnabled(bool enabled);
  void ExpectSidebarPanelExpanded(bool expanded);

  void ShowContextMenuOnView(views::View* view);

  // DnD helpers (real mouse simulation).
  void DragViewToView(views::View* source, views::View* target);
  void DragViewToPoint(views::View* source, const gfx::Point& screen_point);
  gfx::Point GetViewPoint(views::View* view,
                          double x_fraction,
                          double y_fraction);

  // Assertion helpers.
  void ExpectSidebarVisible(bool visible);
  void ExpectTabCount(int expected);
  void ExpectActiveTabIndex(int expected);

  // Shell event observer.
  RecordingShellEventObserver observer_;

  // Resolved views.
  raw_ptr<MahoSidebarContainerView> container_ = nullptr;
  raw_ptr<MahoSidebarView> sidebar_ = nullptr;
  raw_ptr<MahoSidebarTabListView> tab_list_ = nullptr;
  raw_ptr<MahoSidebarFavoritesGridView> favorites_ = nullptr;
};

template <typename C>
bool BlockAndPollUntil(C condition, base::TimeDelta timeout = base::Seconds(10)) {
  base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
  while (base::TimeTicks::Now() < deadline) {
    if (condition()) {
      return true;
    }
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(10));
    run_loop.Run();
  }
  return condition();
}

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_INTERACTIVE_TEST_BASE_H_
