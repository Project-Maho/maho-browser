// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_routines_ui.h"

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_pending_surface.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "maho/components/constants/webui_url_constants.h"

MahoRoutinesUIConfig::MahoRoutinesUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoRoutinesHost) {}

bool MahoRoutinesUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoRoutinesUI)

MahoRoutinesUI::MahoRoutinesUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui, /*enable_chrome_send=*/false) {
  Profile* profile = Profile::FromWebUI(web_ui);
  // The handoff below runs from this constructor, so the navigation has to
  // reach commit. A chrome:// load goes through the WebUI URL loader, which
  // fails with ERR_INVALID_URL when no WebUIDataSource is registered for the
  // host (content/browser/webui/web_ui_url_loader_factory.cc); that failure
  // precedes commit, so without a data source this controller is never
  // constructed and the redirect silently never happens. Serve a throwaway
  // document: the handoff closes this tab on the next task regardless.
  if (profile) {
    content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
        profile, maho::kMahoRoutinesHost);
    source->SetRequestFilter(
        base::BindRepeating([](const std::string&) { return true; }),
        base::BindRepeating([](const std::string&,
                               content::WebUIDataSource::GotDataCallback
                                   callback) {
          std::move(callback).Run(
              base::MakeRefCounted<base::RefCountedString>(std::string(
                  "<!doctype html><meta charset=\"utf-8\">"
                  "<title>Maho Routines</title>")));
        }));
  }
  content::WebContents* host = web_ui->GetWebContents();
  Browser* browser =
      host ? static_cast<Browser*>(
                 GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(host))
           : nullptr;
  if (!profile || !browser || (browser->GetType() == BrowserWindowInterface::TYPE_POPUP) ||
      !MahoIsWebUIEnabled(profile)) {
    return;
  }
  maho::ai::MahoAiPendingSurface::Request(
      profile->GetPrefs(), maho_ai::mojom::CompactSurface::kRoutines);
  auto* side_panel_ui = browser->GetFeatures().side_panel_ui();
  if (!side_panel_ui) {
    return;
  }
  side_panel_ui->Show(SidePanelEntryId::kMahoAiPanel);
  base::WeakPtr<content::WebContents> weak_host = host->GetWeakPtr();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> web_contents) {
            if (!web_contents) {
              return;
            }
            Browser* browser =
                static_cast<Browser*>(GlobalBrowserCollection::GetInstance()
                                          ->FindBrowserWithTab(web_contents.get()));
            if (!browser) {
              return;
            }
            const int index = browser->GetTabStripModel()->GetIndexOfWebContents(
                web_contents.get());
            if (index != TabStripModel::kNoTab) {
              browser->GetTabStripModel()->CloseWebContentsAt(
                  index, TabCloseTypes::CLOSE_USER_GESTURE);
            }
          },
          weak_host));
}

MahoRoutinesUI::~MahoRoutinesUI() = default;
