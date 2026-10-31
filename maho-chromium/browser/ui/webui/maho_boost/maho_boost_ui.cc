// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_boost/maho_boost_ui.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/maho_boost_resources.h"
#include "chrome/grit/maho_boost_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller_bridge.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

MahoBoostUIConfig::MahoBoostUIConfig()
    : WebUIConfig(content::kChromeUIUntrustedScheme,
                  maho::kMahoBoostUntrustedHost) {}

MahoBoostUIConfig::~MahoBoostUIConfig() = default;

std::unique_ptr<content::WebUIController>
MahoBoostUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                         const GURL& url) {
  return std::make_unique<MahoBoostUI>(web_ui);
}

bool MahoBoostUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoBoostUI)

MahoBoostUI::MahoBoostUI(content::WebUI* web_ui)
    : ui::MojoWebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), maho::kMahoBoostUntrustedURL);
  webui::SetupWebUIDataSource(source, kMahoBoostResources,
                              IDR_MAHO_BOOST_BOOST_HTML);

  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      "script-src chrome-untrusted://resources 'self';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::StyleSrc,
      "style-src 'self' 'unsafe-inline';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      base::StrCat({webui::kDefaultTrustedTypesPolicies, " default;"}));

  const GURL& visible_url = web_ui->GetWebContents()->GetVisibleURL();
  const GURL& url = visible_url.is_valid()
                        ? visible_url
                        : web_ui->GetWebContents()->GetLastCommittedURL();
  std::string domain_param;
  if (net::GetValueForKeyInQuery(url, "domain", &domain_param)) {
    domain_ = domain_param;
  }

  std::string controller_token;
  if (net::GetValueForKeyInQuery(url, "controller", &controller_token)) {
    controller_ =
        maho::ConsumePendingBoostControllerForUI(controller_token);
  }
}

MahoBoostUI::~MahoBoostUI() = default;

void MahoBoostUI::SetController(
    base::WeakPtr<maho::MahoBoostWindowController> controller) {
  controller_ = std::move(controller);
}

void MahoBoostUI::BindInterface(
    mojo::PendingReceiver<maho_boost::mojom::PageHandlerFactory> receiver) {
  page_factory_receiver_.reset();
  page_factory_receiver_.Bind(std::move(receiver));
}

void MahoBoostUI::CreatePageHandler(
    mojo::PendingRemote<maho_boost::mojom::PageObserver> page,
    mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver) {
  content::WebContents* host = web_ui()->GetWebContents();
  if (!controller_) {
    std::string controller_token;
    if (net::GetValueForKeyInQuery(host->GetVisibleURL(), "controller",
                                   &controller_token)) {
      controller_ =
          maho::ConsumePendingBoostControllerForUI(controller_token);
    }
  }
  content::WebContents* target = maho::GetTargetTabForBoost(controller_);
  if (!controller_ || !target ||
      !target->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return;
  }
  BrowserWindowInterface* browser_window = webui::GetBrowserWindowInterface(host);
  if (!browser_window) {
    browser_window = webui::GetBrowserWindowInterface(target);
  }

  if (!browser_window || Profile::FromBrowserContext(host->GetBrowserContext()) != browser_window->GetProfile() ||
      !MahoIsWebUIEnabled(host->GetBrowserContext())) {
    return;
  }

  if (domain_.empty()) {
    const GURL& url = web_ui()->GetWebContents()->GetVisibleURL();
    std::string domain_param;
    if (net::GetValueForKeyInQuery(url, "domain", &domain_param)) {
      domain_ = domain_param;
    }
  }

  domain_ = target->GetLastCommittedURL().host();

  page_handler_ = std::make_unique<MahoBoostPageHandler>(
      std::move(receiver), std::move(page), domain_, controller_,
      browser_window, host, target);

  if (controller_ && page_handler_) {
    maho::SetBoostEditorKilledCallback(
        controller_,
        base::BindRepeating(&MahoBoostPageHandler::FireEditorKilled,
                            page_handler_->GetWeakPtr()));
  }
}
