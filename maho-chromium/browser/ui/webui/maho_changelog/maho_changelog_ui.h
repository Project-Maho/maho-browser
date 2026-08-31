// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_CHANGELOG_MAHO_CHANGELOG_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_CHANGELOG_MAHO_CHANGELOG_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace content {
class BrowserContext;
class WebUI;
}  // namespace content

class MahoChangelogUI;

class MahoChangelogUIConfig : public content::DefaultWebUIConfig<MahoChangelogUI> {
 public:
  MahoChangelogUIConfig();
  ~MahoChangelogUIConfig() override = default;

  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoChangelogUI : public content::WebUIController {
 public:
  explicit MahoChangelogUI(content::WebUI* web_ui);

  MahoChangelogUI(const MahoChangelogUI&) = delete;
  MahoChangelogUI& operator=(const MahoChangelogUI&) = delete;

  ~MahoChangelogUI() override;

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_CHANGELOG_MAHO_CHANGELOG_UI_H_
