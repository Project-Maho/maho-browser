// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_command/maho_command_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"  // nogncheck
#include "chrome/grit/maho_command_resources.h"
#include "chrome/grit/maho_command_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_command/maho_command_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

MahoCommandUIConfig::MahoCommandUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoCommandHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoCommandUI)

MahoCommandUI::MahoCommandUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoCommandHost);
  webui::SetupWebUIDataSource(source, kMahoCommandResources,
                              IDR_MAHO_COMMAND_COMMAND_HTML);
}

MahoCommandUI::~MahoCommandUI() = default;

void MahoCommandUI::BindInterface(
    mojo::PendingReceiver<maho_command::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoCommandUI::CreatePageHandler(
    mojo::PendingRemote<maho_command::mojom::Page> page,
    mojo::PendingReceiver<maho_command::mojom::PageHandler> receiver) {
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  if (!browser) {
    return;
  }
  page_handler_ = std::make_unique<MahoCommandPageHandler>(
      std::move(receiver), std::move(page), browser);
}
