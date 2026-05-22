// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_NOTES_MAHO_NOTES_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_NOTES_MAHO_NOTES_UI_H_

#include <memory>
#include <string_view>

#include "chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "maho/browser/ui/webui/maho_notes/maho_notes.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"

class MahoNotesPageHandler;

namespace content {
class WebUI;
}

class MahoNotesUI;

class MahoNotesUIConfig : public DefaultTopChromeWebUIConfig<MahoNotesUI> {
 public:
  MahoNotesUIConfig();
};

class MahoNotesUI : public TopChromeWebUIController,
                    public maho_notes::mojom::PageHandlerFactory {
 public:
  explicit MahoNotesUI(content::WebUI* web_ui);
  MahoNotesUI(const MahoNotesUI&) = delete;
  MahoNotesUI& operator=(const MahoNotesUI&) = delete;
  ~MahoNotesUI() override;

  void BindInterface(
      mojo::PendingReceiver<maho_notes::mojom::PageHandlerFactory> receiver);

  static constexpr std::string_view GetWebUIName() {
    return "MahoNotes";
  }

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();

  // maho_notes::mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<maho_notes::mojom::Page> page,
      mojo::PendingReceiver<maho_notes::mojom::PageHandler> receiver) override;

  std::unique_ptr<MahoNotesPageHandler> page_handler_;
  mojo::Receiver<maho_notes::mojom::PageHandlerFactory>
      page_factory_receiver_{this};
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_NOTES_MAHO_NOTES_UI_H_
