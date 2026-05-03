// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_boost/maho_boost.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoBoostPageHandler;

namespace content {
class WebUI;
}

class MahoBoostUI;

class MahoBoostUIConfig : public content::DefaultWebUIConfig<MahoBoostUI> {
 public:
  MahoBoostUIConfig();
};

class MahoBoostUI : public ui::MojoWebUIController,
                    public maho_boost::mojom::PageHandlerFactory {
 public:
  explicit MahoBoostUI(content::WebUI* web_ui);
  MahoBoostUI(const MahoBoostUI&) = delete;
  MahoBoostUI& operator=(const MahoBoostUI&) = delete;
  ~MahoBoostUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_boost::mojom::PageHandlerFactory> receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_boost::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_boost::mojom::Page> page,
      mojo::PendingReceiver<maho_boost::mojom::PageHandler> receiver) override;

  std::unique_ptr<MahoBoostPageHandler> page_handler_;
  mojo::Receiver<maho_boost::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_BOOST_MAHO_BOOST_UI_H_
