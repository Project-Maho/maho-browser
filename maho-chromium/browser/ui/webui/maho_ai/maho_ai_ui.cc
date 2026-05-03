// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/grit/maho_ai_resources.h"
#include "chrome/grit/maho_ai_resources_map.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/page.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

MahoAIUIConfig::MahoAIUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                   maho::kMahoAIHost) {}

bool MahoAIUIConfig::ShouldAutoResizeHost() {
  return false;
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoAIUI)

MahoAIUI::MahoAIUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoAIHost);
  webui::SetupWebUIDataSource(source, kMahoAiResources,
                              IDR_MAHO_AI_MAHO_AI_HTML);
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

  Browser* browser = chrome::FindBrowserWithTab(web_contents);
  if (!browser) {
    return;
  }

  auto* side_panel_ui =
      browser->GetFeatures().side_panel_ui();
  if (!side_panel_ui) {
    return;
  }

  side_panel_ui->Show(SidePanelEntryId::kMahoAiPanel);

  const int tab_index =
      browser->tab_strip_model()->GetIndexOfWebContents(web_contents);
  if (tab_index == TabStripModel::kNoTab) {
    return;
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<MahoAIUI> weak_self, Browser* browser,
             content::WebContents* web_contents, int expected_index) {
            if (!weak_self || !browser || !browser->tab_strip_model()) {
              return;
            }

            const int current_index =
                browser->tab_strip_model()->GetIndexOfWebContents(web_contents);
            if (current_index != expected_index ||
                current_index == TabStripModel::kNoTab) {
              return;
            }

            browser->tab_strip_model()->CloseWebContentsAt(
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
  Browser* browser = nullptr;
  if (auto* browser_window_interface =
          webui::GetBrowserWindowInterface(web_ui()->GetWebContents())) {
    browser = browser_window_interface->GetBrowserForMigrationOnly();
  }
  if (!browser) {
    browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  }
  if (!browser) {
    auto* profile = Profile::FromWebUI(web_ui());
    if (profile) {
      browser = chrome::FindBrowserWithProfile(profile);
    }
  }
  if (!browser) {
    return;
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  auto url_loader_factory =
      profile->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess();

  page_handler_ = std::make_unique<MahoAIPageHandler>(
      std::move(receiver), std::move(page), browser, profile->GetPrefs(),
      std::move(url_loader_factory));

  if (embedder()) {
    embedder()->ShowUI();
  }

}
