// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_UI_H_

#include <memory>

#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

class Browser;
class MahoInlineEditHandler;

class MahoInlineEditController {
 public:
  MahoInlineEditController(Browser* browser);
  MahoInlineEditController(const MahoInlineEditController&) = delete;
  MahoInlineEditController& operator=(const MahoInlineEditController&) = delete;
  ~MahoInlineEditController();

  void OnTabActivated();
  MahoInlineEditHandler* handler() { return handler_.get(); }

 private:
  std::unique_ptr<MahoInlineEditHandler> handler_;
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_INLINE_EDIT_MAHO_INLINE_EDIT_UI_H_
