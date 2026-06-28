// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_routines/maho_routines_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/maho_routines_resources.h"
#include "chrome/grit/maho_routines_resources_map.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

MahoRoutinesUIConfig::MahoRoutinesUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, maho::kMahoRoutinesHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoRoutinesUI)

MahoRoutinesUI::MahoRoutinesUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui, /*enable_chrome_send=*/false) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, maho::kMahoRoutinesHost);
  webui::SetupWebUIDataSource(source, kMahoRoutinesResources,
                               IDR_MAHO_ROUTINES_ROUTINES_HTML);
}

MahoRoutinesUI::~MahoRoutinesUI() = default;

void MahoRoutinesUI::BindInterface(
    mojo::PendingReceiver<maho_routines::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoRoutinesUI::CreatePageHandler(
    mojo::PendingRemote<maho_routines::mojom::Page> page,
    mojo::PendingReceiver<maho_routines::mojom::PageHandler> receiver) {
  page_handler_ = std::make_unique<MahoRoutinesPageHandler>(
      std::move(receiver), std::move(page));
}
