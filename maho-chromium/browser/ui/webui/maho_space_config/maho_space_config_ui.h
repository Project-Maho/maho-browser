#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CONFIG_MAHO_SPACE_CONFIG_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CONFIG_MAHO_SPACE_CONFIG_UI_H_

#include <memory>
#include <string>

#include "content/public/browser/webui_config.h"
#include "maho/browser/ui/webui/maho_space_config/maho_space_config.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/webui/mojo_web_ui_controller.h"

class MahoSpaceConfigPageHandler;

namespace content {
class WebUI;
}

class MahoSpaceConfigUI;

namespace content {
class BrowserContext;
}

class MahoSpaceConfigUIConfig
    : public content::DefaultWebUIConfig<MahoSpaceConfigUI> {
 public:
  MahoSpaceConfigUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoSpaceConfigUI : public ui::MojoWebUIController,
                          public maho_space_config::mojom::PageHandlerFactory {
 public:
  explicit MahoSpaceConfigUI(content::WebUI* web_ui);
  MahoSpaceConfigUI(const MahoSpaceConfigUI&) = delete;
  MahoSpaceConfigUI& operator=(const MahoSpaceConfigUI&) = delete;
  ~MahoSpaceConfigUI() override;

  void SetSpaceId(const std::string& space_id);
  void SetInitialFocus(maho_space_config::mojom::InitialFocus focus);

  void BindInterface(
      mojo::PendingReceiver<maho_space_config::mojom::PageHandlerFactory>
          receiver);

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  void CreatePageHandler(
      mojo::PendingRemote<maho_space_config::mojom::Page> page,
      mojo::PendingReceiver<maho_space_config::mojom::PageHandler> receiver)
      override;

  std::string space_id_;
  maho_space_config::mojom::InitialFocus initial_focus_ =
      maho_space_config::mojom::InitialFocus::kName;
  std::unique_ptr<MahoSpaceConfigPageHandler> page_handler_;
  mojo::Receiver<maho_space_config::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SPACE_CONFIG_MAHO_SPACE_CONFIG_UI_H_
