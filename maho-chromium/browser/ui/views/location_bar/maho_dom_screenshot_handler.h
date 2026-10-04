// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_HANDLER_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_HANDLER_H_

#include <string>

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace views {
class Widget;
}  // namespace views

namespace maho {

// Drives Maho's DOM-aware selected-area screenshot flow:
//   1. Injects an overlay script into an isolated world.
//   2. The user hovers to select an element (or drags a rect).
//   3. JS writes the chosen rect to a JS variable; C++ polls for it.
//   4. On receipt the rect is captured immediately via CDP.
//   5. The decoded image is shown in a Maho post-capture bubble anchored
//      near the selected region with Copy and Download actions.
// Self-deletes once the result bubble is dismissed, capture fails, or the
// WebContents is closed/navigated away.
class MahoDomScreenshotHandler
    : public content::WebContentsObserver,
      public content::DevToolsAgentHostClient {
 public:
  static void TriggerCapture(content::WebContents* web_contents);

  MahoDomScreenshotHandler(const MahoDomScreenshotHandler&) = delete;
  MahoDomScreenshotHandler& operator=(const MahoDomScreenshotHandler&) = delete;

 private:
  explicit MahoDomScreenshotHandler(content::WebContents* web_contents);
  ~MahoDomScreenshotHandler() override;

  void InjectOverlay();
  void PollForResult();
  void OnPollResult(base::Value result);

  bool AttachDevToolsClient();
  void CaptureRect(const gfx::Rect& rect_css_px);

  void ShowResultBubble(const gfx::Rect& anchor_rect_screen);
  void OnCopyClicked();
  void OnSaveClicked();
  void OnDownloadClicked();
  void OnSendToClicked();
  void OnRetakeClicked();
  void OnEditClicked();
  void OnSendViaIMessageClicked();
  void OnSaveToLibraryClicked();
  void OnBubbleDismissed();

  void Finish();

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void RenderFrameDeleted(
      content::RenderFrameHost* render_frame_host) override;
  void WebContentsDestroyed() override;

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;

  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  std::string session_token_;
  bool finished_ = false;
  // True between CaptureRect() dispatch and DispatchProtocolMessage() receipt;
  // navigation during this window triggers a clean teardown.
  bool capture_in_flight_ = false;
  int poll_count_ = 0;

  // Screen-space anchor saved at capture time to position the result bubble.
  std::optional<gfx::Rect> capture_anchor_screen_;

  // Decoded image kept alive for the duration of the result bubble.
  gfx::Image captured_image_;

  // Non-owning pointer to the result bubble widget; used only to close it
  // programmatically on navigation or teardown.
  raw_ptr<views::Widget> result_bubble_widget_ = nullptr;

  static constexpr int kCaptureCommandId = 1;
  static constexpr int kMaxPollAttempts = 600;  // 60 s at 100 ms intervals

  base::WeakPtrFactory<MahoDomScreenshotHandler> weak_factory_{this};
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_DOM_SCREENSHOT_HANDLER_H_
