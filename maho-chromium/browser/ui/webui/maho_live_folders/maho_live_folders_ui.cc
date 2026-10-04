// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"

MahoLiveFoldersUIConfig::MahoLiveFoldersUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         maho::kMahoLiveFoldersHost) {}

bool MahoLiveFoldersUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoLiveFoldersUI)

MahoLiveFoldersUI::MahoLiveFoldersUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoLiveFoldersHost);
}

MahoLiveFoldersUI::~MahoLiveFoldersUI() = default;

void MahoLiveFoldersUI::BindInterface(
    mojo::PendingReceiver<maho_live_folders::mojom::PageHandlerFactory>
        receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoLiveFoldersUI::CreatePageHandler(
    mojo::PendingRemote<maho_live_folders::mojom::Page> page,
    mojo::PendingReceiver<maho_live_folders::mojom::PageHandler> receiver) {
  auto* profile = Profile::FromWebUI(web_ui());
  page_handler_ = std::make_unique<MahoLiveFolderPageHandler>(
      std::move(receiver), std::move(page), profile);
}
