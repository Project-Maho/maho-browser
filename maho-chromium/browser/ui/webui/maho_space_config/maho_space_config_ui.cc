#include "maho/browser/ui/webui/maho_space_config/maho_space_config_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/grit/maho_space_config_resources.h"
#include "chrome/grit/maho_space_config_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_space_config/maho_space_config_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "ui/webui/webui_util.h"

namespace {

maho_space_config::mojom::InitialFocus ParseFocus(const std::string& value) {
  if (value == "icon") {
    return maho_space_config::mojom::InitialFocus::kIcon;
  }
  if (value == "color") {
    return maho_space_config::mojom::InitialFocus::kColor;
  }
  if (value == "profile") {
    return maho_space_config::mojom::InitialFocus::kProfile;
  }
  return maho_space_config::mojom::InitialFocus::kName;
}

}  // namespace

MahoSpaceConfigUIConfig::MahoSpaceConfigUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         maho::kMahoSpaceConfigHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoSpaceConfigUI)

MahoSpaceConfigUI::MahoSpaceConfigUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoSpaceConfigHost);
  webui::SetupWebUIDataSource(source, kMahoSpaceConfigResources,
                              IDR_MAHO_SPACE_CONFIG_SPACE_CONFIG_HTML);

  const GURL& url = web_ui->GetWebContents()->GetLastCommittedURL();
  std::string space_id_param;
  if (net::GetValueForKeyInQuery(url, "space_id", &space_id_param)) {
    space_id_ = space_id_param;
  }
  std::string focus_param;
  if (net::GetValueForKeyInQuery(url, "focus", &focus_param)) {
    initial_focus_ = ParseFocus(focus_param);
  }
}

MahoSpaceConfigUI::~MahoSpaceConfigUI() = default;

void MahoSpaceConfigUI::SetSpaceId(const std::string& space_id) {
  space_id_ = space_id;
}

void MahoSpaceConfigUI::SetInitialFocus(
    maho_space_config::mojom::InitialFocus focus) {
  initial_focus_ = focus;
}

void MahoSpaceConfigUI::BindInterface(
    mojo::PendingReceiver<maho_space_config::mojom::PageHandlerFactory>
        receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoSpaceConfigUI::CreatePageHandler(
    mojo::PendingRemote<maho_space_config::mojom::Page> page,
    mojo::PendingReceiver<maho_space_config::mojom::PageHandler> receiver) {
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

  if (space_id_.empty()) {
    const GURL& url = web_ui()->GetWebContents()->GetVisibleURL();
    std::string space_id_param;
    if (net::GetValueForKeyInQuery(url, "space_id", &space_id_param)) {
      space_id_ = space_id_param;
    }
    std::string focus_param;
    if (net::GetValueForKeyInQuery(url, "focus", &focus_param)) {
      initial_focus_ = ParseFocus(focus_param);
    }
  }

  page_handler_ = std::make_unique<MahoSpaceConfigPageHandler>(
      std::move(receiver), std::move(page), browser,
      web_ui()->GetWebContents(), space_id_, initial_focus_);
}
