// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_boost/maho_boost_page_handler.h"

#include <utility>

MahoBoostPageHandler::MahoBoostPageHandler(
    mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_boost::mojom::Page> page)
    : receiver_(this, std::move(receiver)), page_(std::move(page)) {}

MahoBoostPageHandler::~MahoBoostPageHandler() = default;

void MahoBoostPageHandler::GetRulesForSite(
    const std::string& domain,
    GetRulesForSiteCallback callback) {
  auto rule = maho_boost::mojom::SiteRule::New();
  rule->id = "rule-1";
  rule->domain = domain;
  rule->css_rules = "";
  rule->js_enabled = true;
  rule->ads_blocked = false;
  std::move(callback).Run(std::move(rule));
}

void MahoBoostPageHandler::SetCssRules(const std::string& domain,
                                       const std::string& css,
                                       SetCssRulesCallback callback) {
  std::move(callback).Run(true);
}

void MahoBoostPageHandler::ToggleJs(const std::string& domain,
                                    bool enabled,
                                    ToggleJsCallback callback) {
  std::move(callback).Run(true);
}

void MahoBoostPageHandler::ToggleAds(const std::string& domain,
                                     bool blocked,
                                     ToggleAdsCallback callback) {
  std::move(callback).Run(true);
}
