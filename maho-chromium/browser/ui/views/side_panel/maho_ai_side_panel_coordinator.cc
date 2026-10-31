// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/side_panel/maho_ai_side_panel_coordinator.h"

#include <memory>
#include <utility>

#include "base/logging.h"

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_scope.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/views/side_panel/maho_ai_side_panel_web_view.h"
#include "ui/views/view.h"

namespace {

std::unique_ptr<views::View> CreateMahoAiWebView(BrowserWindowInterface* browser,
                                                 Profile* profile,
                                                 SidePanelEntryScope& scope) {
  LOG(INFO) << "CreateMahoAiWebView: browser=" << browser << " profile=" << profile;
  auto view = std::make_unique<MahoAiSidePanelWebView>(profile, scope);
  // Let MahoAIUI signal readiness through embedder()->ShowUI() once its
  // page handler is actually bound.
  return view;
}

}  // namespace

namespace maho {

void RegisterMahoAiSidePanel(BrowserWindowInterface* browser,
                              SidePanelRegistry* global_registry) {
  // R-9: never register the AI side-panel entry in a non-regular (OTR/Guest/
  // system/DevTools) context. kAI is denied for every non-regular class.
  MahoPrivateContextToken token(browser, /*web_contents=*/nullptr);
  bool allowed = token.Revalidate(MahoPrivateCapability::kAI);
  LOG(INFO) << "RegisterMahoAiSidePanel: browser=" << browser
            << " allowed=" << allowed;
  if (!allowed) {
    return;
  }

  LOG(INFO) << "RegisterMahoAiSidePanel: Registering kMahoAiPanel";
  auto entry = std::make_unique<SidePanelEntry>(
      SidePanelEntry::Key(SidePanelEntry::Id::kMahoAiPanel),
      base::BindRepeating(&CreateMahoAiWebView, browser,
                          browser->GetProfile()),
      base::NullCallback());
  entry->set_should_show_header(false);
  global_registry->Register(std::move(entry));
}

}  // namespace maho
