// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_

#include <cstddef>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "url/gurl.h"

class Browser;

// Privacy boundary: kPersistent mirrors split membership to MahoCore; primary
// Incognito uses kPrivateLocal, which must never dispatch create_split /
// clear_split_view to maho_core_handle_event (VerificationContract 4).
enum class MahoSplitPersistence {
  kPersistent,
  kPrivateLocal,
};

// Landing side for a sidebar tab-on-tab split drag. This is preview-only until
// the drop callback commits the split.
enum class MahoSplitDropSide {
  kLeft,
  kRight,
};

// Manages split-view tab layout for a single browser window.
//
// Lifecycle: BrowserView owns one persistent instance created lazily on first
// use via BrowserView::GetOrCreateMahoSplitViewController(). Callers that only
// need a non-mutating query without forcing creation may call
// BrowserView::GetMahoSplitViewController(), which returns nullptr when no
// split has been requested yet.
//
// Design constraint: this class must NOT include or depend on BrowserView
// headers (that would create a circular dependency). BrowserView creates and
// owns instances of this class; the class itself only knows about Browser.
class MahoSplitViewController {
 public:
  explicit MahoSplitViewController(
      Browser* browser,
      MahoSplitPersistence persistence = MahoSplitPersistence::kPersistent);
  ~MahoSplitViewController();

  MahoSplitViewController(const MahoSplitViewController&) = delete;
  MahoSplitViewController& operator=(const MahoSplitViewController&) = delete;

  // Only kPersistent may mirror split membership to MahoCore; kPrivateLocal
  // must not (primary Incognito). Shared by production and unit tests.
  static bool ShouldDispatchPersistentSplitEvent(MahoSplitPersistence mode);

  // Shared by sidebar hover and drop handling so the preview always describes
  // the side that will be committed. These helpers are pure and never mutate
  // split state.
  static MahoSplitDropSide ResolveDropSide(int x, int width);
  static bool IsSplitDropEligible(bool is_multi_drag,
                                  bool source_is_target,
                                  bool source_already_split,
                                  bool target_already_split);

  static void RestoreForWindow(Browser* browser);

  void AddSplit();
  void AddSplitWithURL(const GURL& url);
  // Splits the existing tab at |clicked_index| against the active pivot tab;
  // falls back to AddSplit() (blank partner) when it is the active tab, and is
  // a no-op if either the clicked or active tab is already split.
  void AddSplitForExistingTab(int clicked_index);
  bool AddViewportSplitForExistingTab(
      int source_index,
      split_tabs::SplitTabLayout layout,
      bool source_before_target = false);
  bool AddViewportSplitForExistingTab(
      int source_index,
      size_t target_pane_index,
      split_tabs::SplitTabLayout layout,
      bool source_before_target = false);
  void RemoveSplit();
  void RemoveSplitForTab(int tab_strip_index);
  void SwapSplit();
  void ToggleSplit();
  void ToggleSplitWithURL(const GURL& url);
  bool ResizeSplit(double delta);
  bool IsSplitActive() const;
  std::optional<split_tabs::SplitTabLayout> GetSplitOrientation() const;
  bool SetSplitOrientation(split_tabs::SplitTabLayout orientation);
  std::optional<split_tabs::SplitTabLayout> ToggleSplitOrientation();

 private:
  raw_ptr<Browser> browser_;
  MahoSplitPersistence persistence_;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
