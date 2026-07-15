// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SYNC_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SYNC_UI_H_

#include <memory>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_sync/maho_sync.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoSyncPageHandler;

namespace content {
class WebUI;
}

class MahoSyncUI;

namespace content {
class BrowserContext;
}

class MahoSyncUIConfig : public content::DefaultWebUIConfig<MahoSyncUI> {
 public:
  MahoSyncUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoSyncUI : public ui::MojoWebUIController,
                   public maho_sync::mojom::PageHandlerFactory {
 public:
  explicit MahoSyncUI(content::WebUI* web_ui);
  MahoSyncUI(const MahoSyncUI&) = delete;
  MahoSyncUI& operator=(const MahoSyncUI&) = delete;
  ~MahoSyncUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_sync::mojom::PageHandlerFactory> receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  void CreatePageHandler(
      mojo::PendingRemote<maho_sync::mojom::Page> page,
      mojo::PendingReceiver<maho_sync::mojom::PageHandler> receiver) override;

  std::unique_ptr<MahoSyncPageHandler> page_handler_;
  mojo::Receiver<maho_sync::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SYNC_UI_H_
