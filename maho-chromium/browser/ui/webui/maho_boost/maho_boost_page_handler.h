// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_PAGE_HANDLER_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_PAGE_HANDLER_H_

#include <string>

#include "maho/browser/ui/webui/maho_boost/maho_boost.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

class MahoBoostPageHandler : public maho_boost::mojom::PageHandler {
 public:
  MahoBoostPageHandler(
      mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver,
      mojo::PendingRemote<maho_boost::mojom::Page> page);
  MahoBoostPageHandler(const MahoBoostPageHandler&) = delete;
  MahoBoostPageHandler& operator=(const MahoBoostPageHandler&) = delete;
  ~MahoBoostPageHandler() override;

  void GetRulesForSite(const std::string& domain,
                       GetRulesForSiteCallback callback) override;
  void SetCssRules(const std::string& domain,
                   const std::string& css,
                   SetCssRulesCallback callback) override;
  void ToggleJs(const std::string& domain,
                bool enabled,
                ToggleJsCallback callback) override;
  void ToggleAds(const std::string& domain,
                 bool blocked,
                 ToggleAdsCallback callback) override;

 private:
  mojo::Receiver<maho_boost::mojom::PageHandler> receiver_;
  mojo::Remote<maho_boost::mojom::Page> page_;
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_PAGE_HANDLER_H_
