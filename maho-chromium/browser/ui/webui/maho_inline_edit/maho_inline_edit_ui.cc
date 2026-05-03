// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit_ui.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit_handler.h"

MahoInlineEditController::MahoInlineEditController(Browser* browser) {
  Profile* profile = browser->profile();
  auto url_loader_factory =
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess();

  handler_ = std::make_unique<MahoInlineEditHandler>(
      browser, profile->GetPrefs(), std::move(url_loader_factory));
}

MahoInlineEditController::~MahoInlineEditController() = default;

void MahoInlineEditController::OnTabActivated() {
  if (handler_) {
    handler_->InjectScriptIntoActiveTab();
  }
}
