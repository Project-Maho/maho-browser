// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_changelog/maho_changelog_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/maho_changelog_resources.h"
#include "chrome/grit/maho_changelog_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "maho/browser/ui/webui/maho_webui_private_boundary.h"
#include "maho/components/constants/webui_url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/webui_util.h"

MahoChangelogUIConfig::MahoChangelogUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         maho::kMahoChangelogHost) {}

bool MahoChangelogUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return MahoIsWebUIEnabled(browser_context);
}

WEB_UI_CONTROLLER_TYPE_IMPL(MahoChangelogUI)

MahoChangelogUI::MahoChangelogUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, maho::kMahoChangelogHost);
  webui::SetupWebUIDataSource(source, kMahoChangelogResources,
                              IDR_MAHO_CHANGELOG_INDEX_HTML);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default static-types;");
}

MahoChangelogUI::~MahoChangelogUI() = default;
