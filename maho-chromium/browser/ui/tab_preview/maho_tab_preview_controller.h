// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TAB_PREVIEW_MAHO_TAB_PREVIEW_CONTROLLER_H_
#define MAHO_BROWSER_UI_TAB_PREVIEW_MAHO_TAB_PREVIEW_CONTROLLER_H_

#include "base/memory/raw_ptr.h"

class Browser;

class MahoTabPreviewController {
 public:
  explicit MahoTabPreviewController(Browser* browser);
  ~MahoTabPreviewController();

  void ShowPreview(int tab_index);
  void HidePreview();

 private:
  raw_ptr<Browser> browser_;
};

#endif  // MAHO_BROWSER_UI_TAB_PREVIEW_MAHO_TAB_PREVIEW_CONTROLLER_H_
