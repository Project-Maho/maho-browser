#include "maho/browser/ui/webui/maho_space_manage/maho_space_manage_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/grit/maho_space_manage_resources.h"
#include "chrome/grit/maho_space_manage_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_space_manage/maho_space_manage_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/webui/webui_util.h"

MahoSpaceManageUIConfig::MahoSpaceManageUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         maho::kMahoSpaceManageHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoSpaceManageUI)

MahoSpaceManageUI::MahoSpaceManageUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoSpaceManageHost);
  webui::SetupWebUIDataSource(source, kMahoSpaceManageResources,
                              IDR_MAHO_SPACE_MANAGE_SPACE_MANAGE_HTML);
}

MahoSpaceManageUI::~MahoSpaceManageUI() = default;

void MahoSpaceManageUI::BindInterface(
    mojo::PendingReceiver<maho_space_manage::mojom::PageHandlerFactory>
        receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoSpaceManageUI::CreatePageHandler(
    mojo::PendingRemote<maho_space_manage::mojom::Page> page,
    mojo::PendingReceiver<maho_space_manage::mojom::PageHandler> receiver) {
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
  page_handler_ = std::make_unique<MahoSpaceManagePageHandler>(
      std::move(receiver), std::move(page), browser,
      web_ui()->GetWebContents());
}
