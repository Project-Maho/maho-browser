// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_full_page_capture_client.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/timer/timer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "chrome/browser/image_editor/screenshot_flow.h"
#include "chrome/browser/ui/sharing_hub/screenshot/screenshot_captured_bubble_controller.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"

namespace maho {

// static
void MahoFullPageCaptureClient::TriggerCapture(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return;
  }

  scoped_refptr<content::DevToolsAgentHost> agent_host =
      content::DevToolsAgentHost::GetOrCreateFor(web_contents);
  if (!agent_host) {
    return;
  }

  auto* client = new MahoFullPageCaptureClient();
  client->agent_host_ = agent_host;

  if (!agent_host->AttachClient(client)) {
    delete client;
    return;
  }

  constexpr base::TimeDelta kCaptureWatchdog = base::Seconds(30);
  client->watchdog_timer_.Start(
      FROM_HERE, kCaptureWatchdog,
      base::BindOnce(&MahoFullPageCaptureClient::OnWatchdogTimeout,
                     base::Unretained(client)));

  base::DictValue params;
  params.Set("format", "png");
  params.Set("captureBeyondViewport", true);
  params.Set("fromSurface", true);

  base::DictValue message;
  message.Set("id", kCaptureCommandId);
  message.Set("method", "Page.captureScreenshot");
  message.Set("params", std::move(params));

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(message)), &json);
  agent_host->DispatchProtocolMessage(client, base::as_byte_span(json));
}

MahoFullPageCaptureClient::MahoFullPageCaptureClient() = default;

MahoFullPageCaptureClient::~MahoFullPageCaptureClient() {
  watchdog_timer_.Stop();
}

void MahoFullPageCaptureClient::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  if (finished_) {
    return;
  }

  std::string_view message_sv(reinterpret_cast<const char*>(message.data()),
                               message.size());
  std::optional<base::Value> value =
      base::JSONReader::Read(message_sv, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    Finish();
    return;
  }

  const base::DictValue& dict = value->GetDict();
  std::optional<int> id = dict.FindInt("id");
  if (!id || *id != kCaptureCommandId) {
    return;
  }

  const base::DictValue* result = dict.FindDict("result");
  if (!result) {
    Finish();
    return;
  }

  const std::string* data = result->FindString("data");
  if (!data || data->empty()) {
    Finish();
    return;
  }

  std::optional<std::vector<uint8_t>> png_bytes = base::Base64Decode(*data);
  if (!png_bytes || png_bytes->empty()) {
    Finish();
    return;
  }

  gfx::Image image = gfx::Image::CreateFrom1xPNGBytes(*png_bytes);
  if (image.IsEmpty()) {
    Finish();
    return;
  }

  content::WebContents* web_contents = agent_host->GetWebContents();
  if (web_contents) {
    sharing_hub::ScreenshotCapturedBubbleController* controller =
        sharing_hub::ScreenshotCapturedBubbleController::Get(web_contents);
    if (controller) {
      image_editor::ScreenshotCaptureResult capture_result;
      capture_result.result_code =
          image_editor::ScreenshotCaptureResultCode::SUCCESS;
      capture_result.image = std::move(image);
      capture_result.screen_bounds = gfx::Rect();
      controller->ShowBubble(capture_result);
    }
  }

  Finish();
}

void MahoFullPageCaptureClient::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {
  watchdog_timer_.Stop();
  agent_host_ = nullptr;
  finished_ = true;
  delete this;
}

void MahoFullPageCaptureClient::Finish() {
  if (finished_) {
    return;
  }
  finished_ = true;
  watchdog_timer_.Stop();
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_ = nullptr;
  }
  delete this;
}

void MahoFullPageCaptureClient::OnWatchdogTimeout() {
  Finish();
}

}  // namespace maho
