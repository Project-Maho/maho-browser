// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_EXTENSIONS_API_MAHO_SPLIT_VIEW_API_H_
#define MAHO_BROWSER_EXTENSIONS_API_MAHO_SPLIT_VIEW_API_H_

#include "extensions/browser/extension_function.h"

namespace extensions {

// chrome.maho.splitView.create([url])
class MahoSplitViewCreateFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("maho.splitView.create", MAHO_SPLITVIEW_CREATE)

  MahoSplitViewCreateFunction();

 protected:
  ~MahoSplitViewCreateFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;
};

// chrome.maho.splitView.close()
class MahoSplitViewCloseFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("maho.splitView.close", MAHO_SPLITVIEW_CLOSE)

  MahoSplitViewCloseFunction();

 protected:
  ~MahoSplitViewCloseFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;
};

// chrome.maho.splitView.query()
class MahoSplitViewQueryFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("maho.splitView.query", MAHO_SPLITVIEW_QUERY)

  MahoSplitViewQueryFunction();

 protected:
  ~MahoSplitViewQueryFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // MAHO_BROWSER_EXTENSIONS_API_MAHO_SPLIT_VIEW_API_H_
