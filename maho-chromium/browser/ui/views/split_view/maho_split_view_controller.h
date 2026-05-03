// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "url/gurl.h"

class Browser;

namespace content {
class WebContents;
}

// High-level controller for split view operations in Maho.
// Wraps Chromium's MultiContentsView to provide add/remove split pane
// functionality, callable from BrowserCommandController.
class MahoSplitViewController {
 public:
  explicit MahoSplitViewController(Browser* browser);
  ~MahoSplitViewController();

  MahoSplitViewController(const MahoSplitViewController&) = delete;
  MahoSplitViewController& operator=(const MahoSplitViewController&) = delete;

  // Add a split view with about:blank as the secondary pane.
  void AddSplit();

  // Add a split view navigating the secondary pane to |url|.
  void AddSplitWithURL(const GURL& url);

  // Remove split view, closing the secondary WebContents.
  void RemoveSplit();

  // Returns true if split view is currently active.
  bool IsSplitActive() const;

 private:
  // Creates a new WebContents for the secondary pane.
  std::unique_ptr<content::WebContents> CreateSecondaryWebContents();

  raw_ptr<Browser> browser_;
  // Owned secondary WebContents while split is active.
  std::unique_ptr<content::WebContents> secondary_web_contents_;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
