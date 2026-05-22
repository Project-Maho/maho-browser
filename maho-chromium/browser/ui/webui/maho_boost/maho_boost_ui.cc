// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_boost/maho_boost_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/maho_boost_resources.h"
#include "chrome/grit/maho_boost_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

MahoBoostUIConfig::MahoBoostUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                  maho::kMahoBoostHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoBoostUI)

MahoBoostUI::MahoBoostUI(content::WebUI* web_ui)
    : TopChromeWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoBoostHost);
  webui::SetupWebUIDataSource(source, kMahoBoostResources,
                              IDR_MAHO_BOOST_BOOST_HTML);
}

MahoBoostUI::~MahoBoostUI() = default;

void MahoBoostUI::BindInterface(
    mojo::PendingReceiver<maho_boost::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoBoostUI::CreatePageHandler(
    mojo::PendingRemote<maho_boost::mojom::Page> page,
    mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver) {
  page_handler_ = std::make_unique<MahoBoostPageHandler>(
      std::move(receiver), std::move(page));
}
