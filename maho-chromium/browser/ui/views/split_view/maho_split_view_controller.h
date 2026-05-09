// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
#define MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "url/gurl.h"

class Browser;

class MahoSplitViewController {
 public:
  explicit MahoSplitViewController(Browser* browser);
  ~MahoSplitViewController();

  MahoSplitViewController(const MahoSplitViewController&) = delete;
  MahoSplitViewController& operator=(const MahoSplitViewController&) = delete;

  void AddSplit();
  void AddSplitWithURL(const GURL& url);
  void RemoveSplit();
  void ToggleSplit();
  void ToggleSplitWithURL(const GURL& url);
  bool IsSplitActive() const;

 private:
  raw_ptr<Browser> browser_;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SPLIT_VIEW_MAHO_SPLIT_VIEW_CONTROLLER_H_
