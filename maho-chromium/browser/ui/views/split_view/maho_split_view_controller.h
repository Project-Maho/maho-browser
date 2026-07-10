// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "url/gurl.h"

class Browser;

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
  explicit MahoSplitViewController(Browser* browser);
  ~MahoSplitViewController();

  MahoSplitViewController(const MahoSplitViewController&) = delete;
  MahoSplitViewController& operator=(const MahoSplitViewController&) = delete;

  static void RestoreForWindow(Browser* browser);

  void AddSplit();
  void AddSplitWithURL(const GURL& url);
  void RemoveSplit();
  void SwapSplit();
  void ToggleSplit();
  void ToggleSplitWithURL(const GURL& url);
  bool IsSplitActive() const;

 private:
  raw_ptr<Browser> browser_;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
