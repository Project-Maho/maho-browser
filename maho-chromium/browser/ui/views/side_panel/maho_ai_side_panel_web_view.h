// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_AI_SIDE_PANEL_WEB_VIEW_H_
#define MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_AI_SIDE_PANEL_WEB_VIEW_H_

#include "chrome/browser/ui/views/side_panel/side_panel_web_ui_view.h"
#include "content/public/browser/web_contents_delegate.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"
#include "ui/base/metadata/metadata_header_macros.h"

class Profile;
class SidePanelEntryScope;

namespace content {
class WebContents;
struct MediaStreamRequest;
}  // namespace content

// Side-panel host for chrome://maho-ai.
//
// `SidePanelWebUIView`'s default `RequestMediaAccessPermission` is an empty
// no-op, so `getUserMedia({audio:true})` from the AI panel (needed for voice
// input) is silently denied on this primary surface. This override forwards the
// request to Chrome's permission machinery, mirroring the Lens precedent at
// `chrome/browser/ui/lens/lens_overlay_side_panel_web_view.cc:94`.
class MahoAiSidePanelWebView : public SidePanelWebUIViewT<MahoAIUI> {
  using SidePanelWebUIViewT_MahoAIUI = SidePanelWebUIViewT<MahoAIUI>;
  METADATA_HEADER(MahoAiSidePanelWebView, SidePanelWebUIViewT_MahoAIUI)

 public:
  MahoAiSidePanelWebView(Profile* profile, SidePanelEntryScope& scope);
  MahoAiSidePanelWebView(const MahoAiSidePanelWebView&) = delete;
  MahoAiSidePanelWebView& operator=(const MahoAiSidePanelWebView&) = delete;
  ~MahoAiSidePanelWebView() override;

  // SidePanelWebUIViewT:
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override;
};

#endif  // MAHO_BROWSER_UI_VIEWS_SIDE_PANEL_MAHO_AI_SIDE_PANEL_WEB_VIEW_H_
