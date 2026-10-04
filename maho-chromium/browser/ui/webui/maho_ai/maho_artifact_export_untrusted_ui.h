// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_ARTIFACT_EXPORT_UNTRUSTED_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_ARTIFACT_EXPORT_UNTRUSTED_UI_H_

#include <memory>

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class WebUI;
class WebUIController;
}  // namespace content

// chrome-untrusted://maho-ai-artifact-export: capability-gated raw artifact
// bytes for drag-out (application/octet-stream). Attacker-influenced bytes never
// ride a trusted origin; access requires a WebContents-bound export token.
class MahoArtifactExportUntrustedUIConfig : public content::WebUIConfig {
 public:
  MahoArtifactExportUntrustedUIConfig();
  ~MahoArtifactExportUntrustedUIConfig() override;

  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoArtifactExportUntrustedUI : public content::WebUIController {
 public:
  explicit MahoArtifactExportUntrustedUI(content::WebUI* web_ui);
  MahoArtifactExportUntrustedUI(const MahoArtifactExportUntrustedUI&) = delete;
  MahoArtifactExportUntrustedUI& operator=(
      const MahoArtifactExportUntrustedUI&) = delete;
  ~MahoArtifactExportUntrustedUI() override;

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_ARTIFACT_EXPORT_UNTRUSTED_UI_H_
