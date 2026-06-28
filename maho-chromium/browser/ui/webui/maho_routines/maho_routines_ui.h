// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_routines/maho_routines.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoRoutinesPageHandler;

namespace content {
class WebUI;
}

class MahoRoutinesUI;

class MahoRoutinesUIConfig
    : public content::DefaultWebUIConfig<MahoRoutinesUI> {
 public:
  MahoRoutinesUIConfig();
};

class MahoRoutinesUI : public ui::MojoWebUIController,
                       public maho_routines::mojom::PageHandlerFactory {
 public:
  explicit MahoRoutinesUI(content::WebUI* web_ui);
  MahoRoutinesUI(const MahoRoutinesUI&) = delete;
  MahoRoutinesUI& operator=(const MahoRoutinesUI&) = delete;
  ~MahoRoutinesUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_routines::mojom::PageHandlerFactory>
          receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  void CreatePageHandler(
      mojo::PendingRemote<maho_routines::mojom::Page> page,
      mojo::PendingReceiver<maho_routines::mojom::PageHandler> receiver)
      override;

  std::unique_ptr<MahoRoutinesPageHandler> page_handler_;
  mojo::Receiver<maho_routines::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_UI_H_
