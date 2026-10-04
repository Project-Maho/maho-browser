// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_FULL_PAGE_CAPTURE_CLIENT_H_
#define MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_FULL_PAGE_CAPTURE_CLIENT_H_

#include "base/memory/scoped_refptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"

namespace content {
class WebContents;
}  // namespace content

namespace maho {

// Issues a single Page.captureScreenshot (captureBeyondViewport=true,
// fromSurface=true) and routes the result through the existing screenshot
// bubble. Self-deletes after the CDP round-trip completes or the agent closes.
class MahoFullPageCaptureClient : public content::DevToolsAgentHostClient {
 public:
  static void TriggerCapture(content::WebContents* web_contents);

  MahoFullPageCaptureClient(const MahoFullPageCaptureClient&) = delete;
  MahoFullPageCaptureClient& operator=(const MahoFullPageCaptureClient&) =
      delete;

 private:
  MahoFullPageCaptureClient();
  ~MahoFullPageCaptureClient() override;

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;

  void Finish();

  void OnWatchdogTimeout();

  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  bool finished_ = false;
  base::OneShotTimer watchdog_timer_;
  static constexpr int kCaptureCommandId = 1;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_LOCATION_BAR_MAHO_FULL_PAGE_CAPTURE_CLIENT_H_
