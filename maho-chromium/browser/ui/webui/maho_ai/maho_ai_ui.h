// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_UI_H_

#include <memory>
#include <string_view>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"

class MahoAIPageHandler;

namespace content {
class WebUI;
class BrowserContext;
}  // namespace content

class MahoAIUI;

class MahoAIUIConfig : public DefaultTopChromeWebUIConfig<MahoAIUI> {
 public:
  MahoAIUIConfig();
  bool ShouldAutoResizeHost() override;
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoAIUI : public TopChromeWebUIController,
                 public maho_ai::mojom::PageHandlerFactory {
 public:
  explicit MahoAIUI(content::WebUI* web_ui);
  MahoAIUI(const MahoAIUI&) = delete;
  MahoAIUI& operator=(const MahoAIUI&) = delete;
  ~MahoAIUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_ai::mojom::PageHandlerFactory> receiver);

  static constexpr std::string_view GetWebUIName() { return "MahoAi"; }

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  void WebUIPrimaryPageChanged(content::Page& page) override;
  void RedirectTabHostedNavigationToSidePanel();

  void CreatePageHandler(
      mojo::PendingRemote<maho_ai::mojom::Page> page,
      mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver) override;

  std::unique_ptr<MahoAIPageHandler> page_handler_;
  mojo::Receiver<maho_ai::mojom::PageHandlerFactory> page_factory_receiver_{
      this};
  base::WeakPtrFactory<MahoAIUI> weak_factory_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_AI_UI_H_
