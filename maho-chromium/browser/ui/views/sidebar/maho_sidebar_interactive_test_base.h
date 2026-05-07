// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_INTERACTIVE_TEST_BASE_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_INTERACTIVE_TEST_BASE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_state_models.h"
#include "url/gurl.h"

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
  RecordingShellEventObserver() = default;
  ~RecordingShellEventObserver() override = default;

  void OnShellEventDispatched(const std::string& kind,
                              const std::string& event_json) override {
    events_.push_back({kind, event_json});
  }

  struct Event {
    std::string kind;
    std::string json;
  };

  const std::vector<Event>& events() const { return events_; }

  void Clear() { events_.clear(); }

  bool HasEvent(const std::string& kind) const {
    for (const auto& e : events_) {
      if (e.kind == kind)
        return true;
    }
    return false;
  }

 private:
  std::vector<Event> events_;
};

// Base class for Maho sidebar interactive browser tests.
class MahoSidebarInteractiveTestBase : public InteractiveBrowserTest {
 public:
  MahoSidebarInteractiveTestBase();
  ~MahoSidebarInteractiveTestBase() override;

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

  void ExpectSidebarLayoutEnabled(bool enabled);
  void ExpectSidebarPanelExpanded(bool expanded);

  // DnD helpers (real mouse simulation).
  void DragViewToView(views::View* source, views::View* target);
  void DragViewToPoint(views::View* source, const gfx::Point& screen_point);

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

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_INTERACTIVE_TEST_BASE_H_
