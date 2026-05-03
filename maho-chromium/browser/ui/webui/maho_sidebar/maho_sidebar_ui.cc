// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_sidebar/maho_sidebar_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"  // nogncheck
#include "chrome/grit/maho_sidebar_resources.h"
#include "chrome/grit/maho_sidebar_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_sidebar/maho_sidebar_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

MahoSidebarUIConfig::MahoSidebarUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         maho::kMahoSidebarHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoSidebarUI)

MahoSidebarUI::MahoSidebarUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoSidebarHost);
  webui::SetupWebUIDataSource(source, kMahoSidebarResources,
                              IDR_MAHO_SIDEBAR_SIDEBAR_HTML);
}

MahoSidebarUI::~MahoSidebarUI() = default;

void MahoSidebarUI::BindInterface(
    mojo::PendingReceiver<maho_sidebar::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoSidebarUI::CreatePageHandler(
    mojo::PendingRemote<maho_sidebar::mojom::Page> page,
    mojo::PendingReceiver<maho_sidebar::mojom::PageHandler> receiver) {
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  if (!browser) {
    auto* profile = Profile::FromWebUI(web_ui());
    if (profile) {
      browser = chrome::FindBrowserWithProfile(profile);
    }
  }
  if (!browser) {
    return;
  }
  page_handler_ = std::make_unique<MahoSidebarPageHandler>(
      std::move(receiver), std::move(page), browser);
}
