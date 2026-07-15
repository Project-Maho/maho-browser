// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_EXTENSIONS_API_MAHO_SIDE_PANEL_API_H_
#define MAHO_BROWSER_EXTENSIONS_API_MAHO_SIDE_PANEL_API_H_

#include "extensions/browser/extension_function.h"

namespace extensions {

// chrome.maho.sidePanel.setLayout(layout)
class MahoSidePanelSetLayoutFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("maho.sidePanel.setLayout",
                             MAHO_SIDEPANEL_SETLAYOUT)

  MahoSidePanelSetLayoutFunction();

 protected:
  ~MahoSidePanelSetLayoutFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // MAHO_BROWSER_EXTENSIONS_API_MAHO_SIDE_PANEL_API_H_
