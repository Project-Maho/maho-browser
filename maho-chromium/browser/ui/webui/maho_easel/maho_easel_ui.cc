// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_easel/maho_easel_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/maho_easel_resources.h"
#include "chrome/grit/maho_easel_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_easel/maho_easel_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

MahoEaselUIConfig::MahoEaselUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                  maho::kMahoEaselHost) {}

bool MahoEaselUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoEaselUI)

MahoEaselUI::MahoEaselUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoEaselHost);
  webui::SetupWebUIDataSource(source, kMahoEaselResources,
                              IDR_MAHO_EASEL_EASEL_HTML);
}

MahoEaselUI::~MahoEaselUI() = default;

void MahoEaselUI::BindInterface(
    mojo::PendingReceiver<maho_easel::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoEaselUI::CreatePageHandler(
    mojo::PendingRemote<maho_easel::mojom::Page> page,
    mojo::PendingReceiver<maho_easel::mojom::PageHandler> receiver) {
  content::WebContents* host = web_ui()->GetWebContents();
  BrowserWindowInterface* browser_window = webui::GetBrowserWindowInterface(host);
  if (!browser_window || Profile::FromBrowserContext(host->GetBrowserContext()) != browser_window->GetProfile() ||
      !MahoIsWebUIEnabled(host->GetBrowserContext())) {
    return;
  }
  page_handler_ = std::make_unique<MahoEaselPageHandler>(
      std::move(receiver), std::move(page), browser_window, host);
}
