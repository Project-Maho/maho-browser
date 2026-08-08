// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/side_panel/maho_ai_side_panel_web_view.h"

#include <memory>
#include <utility>

#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_scope.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/web_contents.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/views/controls/webview/web_contents_set_background_color.h"
#include "url/gurl.h"

using SidePanelWebUIViewT_MahoAIUI = SidePanelWebUIViewT<MahoAIUI>;
BEGIN_TEMPLATE_METADATA(SidePanelWebUIViewT_MahoAIUI, SidePanelWebUIViewT)
END_METADATA

MahoAiSidePanelWebView::MahoAiSidePanelWebView(Profile* profile,
                                               SidePanelEntryScope& scope)
    : SidePanelWebUIViewT(
          scope,
          base::RepeatingClosure(),
          base::RepeatingClosure(),
          std::make_unique<WebUIContentsWrapperT<MahoAIUI>>(
              GURL(maho::kMahoAIURL),
              profile,
              /*task_manager_string_id=*/0,
              /*esc_closes_ui=*/false,
              /*supports_draggable_regions=*/false)) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  views::WebContentsSetBackgroundColor::CreateForWebContentsWithColor(
      web_contents(), SK_ColorTRANSPARENT);
  web_contents()->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
}

MahoAiSidePanelWebView::~MahoAiSidePanelWebView() = default;

void MahoAiSidePanelWebView::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  // Forward mic getUserMedia() prompts from chrome://maho-ai to Chrome's
  // permission machinery; the SidePanelWebUIView default is a no-op. Mirrors
  // LensOverlaySidePanelWebView (lens_overlay_side_panel_web_view.cc:94).
  MediaCaptureDevicesDispatcher::GetInstance()->ProcessMediaAccessRequest(
      web_contents, request, std::move(callback), /*extension=*/nullptr);
}

BEGIN_METADATA(MahoAiSidePanelWebView)
END_METADATA
