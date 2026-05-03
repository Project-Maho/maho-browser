// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/side_panel/maho_ai_side_panel_coordinator.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_content_proxy.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_scope.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/side_panel/side_panel_util.h"
#include "chrome/browser/ui/views/side_panel/side_panel_web_ui_view.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "url/gurl.h"

using SidePanelWebUIViewT_MahoAIUI = SidePanelWebUIViewT<MahoAIUI>;
BEGIN_TEMPLATE_METADATA(SidePanelWebUIViewT_MahoAIUI, SidePanelWebUIViewT)
END_METADATA

namespace {

std::unique_ptr<views::View> CreateMahoAiWebView(BrowserWindowInterface* browser,
                                                 Profile* profile,
                                                 SidePanelEntryScope& scope) {
  auto contents_wrapper =
      std::make_unique<WebUIContentsWrapperT<MahoAIUI>>(
          GURL(maho::kMahoAIURL), profile, /*title_string_id=*/0,
          /*esc_closes_ui=*/false,
          /*supports_draggable_regions=*/false);

  auto view = std::make_unique<SidePanelWebUIViewT_MahoAIUI>(
      scope, base::RepeatingClosure(), base::RepeatingClosure(),
      std::move(contents_wrapper));
  view->ShowUI();
  return view;
}

}  // namespace

namespace maho {

void RegisterMahoAiSidePanel(BrowserWindowInterface* browser,
                              SidePanelRegistry* global_registry) {
  global_registry->Register(std::make_unique<SidePanelEntry>(
      SidePanelEntry::Key(SidePanelEntry::Id::kMahoAiPanel),
      base::BindRepeating(&CreateMahoAiWebView, browser,
                          browser->GetProfile()),
      base::NullCallback()));
}

}  // namespace maho
