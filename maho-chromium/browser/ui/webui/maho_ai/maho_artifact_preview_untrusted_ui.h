// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_ARTIFACT_PREVIEW_UNTRUSTED_UI_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_ARTIFACT_PREVIEW_UNTRUSTED_UI_H_

#include <memory>

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class WebUI;
class WebUIController;
}  // namespace content

// chrome-untrusted://maho-ai-artifact-preview: capability-gated sanitized-HTML
// preview of an agent artifact for the chrome://maho-ai sandboxed iframe.
// Untrusted bytes never ride a trusted origin; scripts are CSP-disabled and the
// frame is embeddable only by chrome://maho-ai.
class MahoArtifactPreviewUntrustedUIConfig : public content::WebUIConfig {
 public:
  MahoArtifactPreviewUntrustedUIConfig();
  ~MahoArtifactPreviewUntrustedUIConfig() override;

  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class MahoArtifactPreviewUntrustedUI : public content::WebUIController {
 public:
  explicit MahoArtifactPreviewUntrustedUI(content::WebUI* web_ui);
  MahoArtifactPreviewUntrustedUI(const MahoArtifactPreviewUntrustedUI&) =
      delete;
  MahoArtifactPreviewUntrustedUI& operator=(
      const MahoArtifactPreviewUntrustedUI&) = delete;
  ~MahoArtifactPreviewUntrustedUI() override;

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_AI_MAHO_ARTIFACT_PREVIEW_UNTRUSTED_UI_H_
