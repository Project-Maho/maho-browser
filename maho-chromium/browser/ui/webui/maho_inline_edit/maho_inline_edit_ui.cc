// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit_ui.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit_handler.h"

MahoInlineEditController::MahoInlineEditController(Browser* browser) {
  Profile* profile = browser->GetProfile();
  auto url_loader_factory =
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess();

  content::WebContents* active_wc =
      browser->GetTabStripModel()
          ? browser->GetTabStripModel()->GetActiveWebContents()
          : nullptr;
  auto token =
      std::make_unique<MahoPrivateContextToken>(browser, active_wc);

  handler_ = std::make_unique<MahoInlineEditHandler>(
      std::move(token), browser, profile->GetPrefs(),
      std::move(url_loader_factory));
}

MahoInlineEditController::~MahoInlineEditController() = default;

void MahoInlineEditController::OnTabActivated() {
  if (handler_) {
    handler_->InjectScriptIntoActiveTab();
  }
}
