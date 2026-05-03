// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_EASEL_MAHO_EASEL_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_EASEL_MAHO_EASEL_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_easel/maho_easel.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoEaselPageHandler;

namespace content {
class WebUI;
}

class MahoEaselUI;

class MahoEaselUIConfig : public content::DefaultWebUIConfig<MahoEaselUI> {
 public:
  MahoEaselUIConfig();
};

class MahoEaselUI : public ui::MojoWebUIController,
                    public maho_easel::mojom::PageHandlerFactory {
 public:
  explicit MahoEaselUI(content::WebUI* web_ui);
  MahoEaselUI(const MahoEaselUI&) = delete;
  MahoEaselUI& operator=(const MahoEaselUI&) = delete;
  ~MahoEaselUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_easel::mojom::PageHandlerFactory> receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_easel::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_easel::mojom::Page> page,
      mojo::PendingReceiver<maho_easel::mojom::PageHandler> receiver) override;

  std::unique_ptr<MahoEaselPageHandler> page_handler_;
  mojo::Receiver<maho_easel::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_EASEL_MAHO_EASEL_UI_H_
