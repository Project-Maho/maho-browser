// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_sync/maho_sync_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_sync/maho_sync_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

MahoSyncUIConfig::MahoSyncUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoSyncHost) {}

bool MahoSyncUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoSyncUI)

MahoSyncUI::MahoSyncUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoSyncHost);
}

MahoSyncUI::~MahoSyncUI() = default;

void MahoSyncUI::BindInterface(
    mojo::PendingReceiver<maho_sync::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoSyncUI::CreatePageHandler(
    mojo::PendingRemote<maho_sync::mojom::Page> page,
    mojo::PendingReceiver<maho_sync::mojom::PageHandler> receiver) {
  auto* profile = Profile::FromWebUI(web_ui());
  page_handler_ = std::make_unique<MahoSyncPageHandler>(
      std::move(receiver), std::move(page), profile);
}
