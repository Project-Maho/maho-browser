// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/fileicon_source.h"  // nogncheck
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/grit/maho_ai_resources.h"
#include "chrome/grit/maho_ai_resources_map.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/page.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_pending_surface.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

MahoAIUIConfig::MahoAIUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme, maho::kMahoAIHost) {
}

bool MahoAIUIConfig::ShouldAutoResizeHost() {
  return false;
}

bool MahoAIUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoAIUI)

MahoAIUI::MahoAIUI(content::WebUI* web_ui) : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoAIHost);
  PrefService* prefs = Profile::FromWebUI(web_ui)->GetPrefs();
  source->AddBoolean("isMahoAiThinMode",
                     prefs->GetBoolean(maho::ai_prefs::kUiThinModeEnabled));
  webui::SetupWebUIDataSource(source, kMahoAiResources,
                              IDR_MAHO_AI_MAHO_AI_HTML);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default static-types;");
  // LemonSqueezy overlay checkout (P1-A): allow loading lemon.js from LS CDN,
  // embedding the LS checkout iframe, and connecting to the LS API for signed
  // checkout URL creation.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome://resources 'self' https://app.lemonsqueezy.com;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc,
      "frame-src https://maho.lemonsqueezy.com https://lemonsqueezy.com "
      "chrome-untrusted://maho-ai-artifact-preview;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src 'self' https://api.lemonsqueezy.com;");
  // Render real OS (Finder) file-type icons for artifacts. FileIconSource
  // resolves the icon from the extension alone, so the artifact card passes
  // only the display name (no real filesystem path is exposed).
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src chrome://resources chrome://theme chrome://fileicon 'self' "
      "data: blob:;");
  content::URLDataSource::Add(Profile::FromWebUI(web_ui),
                              std::make_unique<FileIconSource>());
}

MahoAIUI::~MahoAIUI() = default;

void MahoAIUI::WebUIPrimaryPageChanged(content::Page& page) {
  TopChromeWebUIController::WebUIPrimaryPageChanged(page);
  RedirectTabHostedNavigationToSidePanel();
}

void MahoAIUI::RedirectTabHostedNavigationToSidePanel() {
  if (embedder()) {
    return;
  }

  content::WebContents* const web_contents = web_ui()->GetWebContents();
  if (!web_contents) {
    return;
  }

  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          web_contents));
  if (!browser) {
    return;
  }

  if ((browser->GetType() == BrowserWindowInterface::TYPE_POPUP)) {
    return;
  }

  auto* side_panel_ui = browser->GetFeatures().side_panel_ui();
  if (!side_panel_ui) {
    return;
  }

  std::string view;
  if (net::GetValueForKeyInQuery(web_contents->GetVisibleURL(), "view",
                                 &view) &&
      view == "routines") {
    maho::ai::MahoAiPendingSurface::Request(
        Profile::FromWebUI(web_ui())->GetPrefs(),
        maho_ai::mojom::CompactSurface::kRoutines);
  }

  side_panel_ui->Show(SidePanelEntryId::kMahoAiPanel);

  const int tab_index =
      browser->GetTabStripModel()->GetIndexOfWebContents(web_contents);
  if (tab_index == TabStripModel::kNoTab) {
    return;
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoAIUI> weak_self, Browser* browser,
             content::WebContents* web_contents, int expected_index) {
            if (!weak_self || !browser || !browser->GetTabStripModel()) {
              return;
            }

            const int current_index =
                browser->GetTabStripModel()->GetIndexOfWebContents(web_contents);
            if (current_index != expected_index ||
                current_index == TabStripModel::kNoTab) {
              return;
            }

            browser->GetTabStripModel()->CloseWebContentsAt(
                current_index, TabCloseTypes::CLOSE_USER_GESTURE);
          },
          weak_factory_.GetWeakPtr(), browser, web_contents, tab_index));
}

void MahoAIUI::BindInterface(
    mojo::PendingReceiver<maho_ai::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoAIUI::CreatePageHandler(
    mojo::PendingRemote<maho_ai::mojom::Page> page,
    mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver) {
  content::WebContents* host = web_ui()->GetWebContents();
  BrowserWindowInterface* browser_window =
      host ? webui::GetBrowserWindowInterface(host) : nullptr;
  Browser* browser = static_cast<Browser*>(browser_window);

  Profile* profile = Profile::FromWebUI(web_ui());

  // R-9: fail closed. Require the exact owning browser window whose profile
  // agrees with the host, build a kAI token, and deny every non-regular
  // context with no handler/data source/pref consumption and a dropped pipe.
  if (!browser || !profile || browser_window->GetProfile() != profile) {
    receiver.reset();
    page.reset();
    return;
  }

  // The AI panel is hosted in the side panel, whose WebContents is not a tab.
  // Build the R-9 token against the browser window only (no web_contents), so
  // Revalidate classifies by profile and does not require tab-strip membership
  // (which a side-panel WebContents never has). This mirrors how
  // RegisterMahoAiSidePanel builds its token and still denies every non-regular
  // (OTR/Guest/system/DevTools) context.
  auto token = std::make_unique<MahoPrivateContextToken>(
      browser_window, /*web_contents=*/nullptr);
  if (!token->Revalidate(MahoPrivateCapability::kAI)) {
    receiver.reset();
    page.reset();
    return;
  }

  auto url_loader_factory = profile->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();

  page_handler_ = std::make_unique<MahoAIPageHandler>(
      std::move(receiver), std::move(page), std::move(token), browser, host,
      profile->GetPrefs(), std::move(url_loader_factory));

  if (embedder()) {
    embedder()->ShowUI();
  }
}
