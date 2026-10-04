// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_UI_H_

#include "content/public/browser/webui_config.h"
#include "ui/webui/mojo_web_ui_controller.h"

namespace content {
class BrowserContext;
class WebUI;
}  // namespace content

class MahoRoutinesUI;

class MahoRoutinesUIConfig
    : public content::DefaultWebUIConfig<MahoRoutinesUI> {
 public:
  MahoRoutinesUIConfig();
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

// Compatibility-only controller. Routine management lives in the existing
// Maho AI compact panel; this WebUI serves only the legacy navigation handoff.
class MahoRoutinesUI : public ui::MojoWebUIController {
 public:
  explicit MahoRoutinesUI(content::WebUI* web_ui);
  MahoRoutinesUI(const MahoRoutinesUI&) = delete;
  MahoRoutinesUI& operator=(const MahoRoutinesUI&) = delete;
  ~MahoRoutinesUI() override;

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_ROUTINES_MAHO_ROUTINES_UI_H_
