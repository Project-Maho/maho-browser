// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_handler.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/base64.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/share/share_attempt.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_request_utils.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

#include "maho/browser/ui/views/location_bar/maho_dom_screenshot_script_string.h"

constexpr int kIsolatedWorldIdForDomScreenshot =
    content::ISOLATED_WORLD_ID_CONTENT_END + 5;

constexpr base::TimeDelta kPollInterval = base::Milliseconds(100);

std::u16string FilenameForURL(const GURL& url) {
  if (!url.has_host() || url.HostIsIPAddress()) {
    return u"maho_screenshot.png";
  }
  return base::ASCIIToUTF16(
      base::StrCat({"maho_screenshot_", url.GetHost(), ".png"}));
}

}  // namespace

namespace maho {

// static
void MahoDomScreenshotHandler::TriggerCapture(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return;
  }
  auto* handler = new MahoDomScreenshotHandler(web_contents);
  handler->InjectOverlay();
}

MahoDomScreenshotHandler::MahoDomScreenshotHandler(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {}

MahoDomScreenshotHandler::~MahoDomScreenshotHandler() = default;

void MahoDomScreenshotHandler::InjectOverlay() {
  content::WebContents* wc = web_contents();
  if (!wc) {
    delete this;
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    delete this;
    return;
  }

  static int s_session_counter = 0;
  session_token_ = std::to_string(++s_session_counter);

  std::string combined =
      "window.__mahoDomScreenshotResultData = null;"
      "window.__mahoDomScreenshotToken = '" +
      session_token_ + "';" +
      std::string(kDomScreenshotScript);
  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(combined),
      base::DoNothing(),
      kIsolatedWorldIdForDomScreenshot);

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&MahoDomScreenshotHandler::PollForResult,
                     weak_factory_.GetWeakPtr()),
      kPollInterval);
}

void MahoDomScreenshotHandler::PollForResult() {
  if (finished_) {
    return;
  }

  if (++poll_count_ > kMaxPollAttempts) {
    Finish();
    return;
  }

  content::WebContents* wc = web_contents();
  if (!wc) {
    Finish();
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    Finish();
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(
          "(function(){"
          "var d=window.__mahoDomScreenshotResultData;"
          "if(d&&d.session==='" + session_token_ + "'){"
          "window.__mahoDomScreenshotResultData=null;"
          "return JSON.stringify(d);"
          "}"
          "return null;"
          "})()"),
      base::BindOnce(&MahoDomScreenshotHandler::OnPollResult,
                     weak_factory_.GetWeakPtr()),
      kIsolatedWorldIdForDomScreenshot);
}

void MahoDomScreenshotHandler::OnPollResult(base::Value result) {
  if (finished_) {
    return;
  }

  if (!result.is_string() || result.GetString().empty() ||
      result.GetString() == "null") {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&MahoDomScreenshotHandler::PollForResult,
                       weak_factory_.GetWeakPtr()),
        kPollInterval);
    return;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(result.GetString(), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    Finish();
    return;
  }

  const base::DictValue& dict = parsed->GetDict();

  std::optional<bool> cancelled = dict.FindBool("cancelled");
  if (cancelled && *cancelled) {
    Finish();
    return;
  }

  std::optional<double> x = dict.FindDouble("x");
  std::optional<double> y = dict.FindDouble("y");
  std::optional<double> w = dict.FindDouble("width");
  std::optional<double> h = dict.FindDouble("height");

  if (!x || !y || !w || !h || *w <= 0 || *h <= 0) {
    Finish();
    return;
  }

  const gfx::Rect rect_css_px(static_cast<int>(*x), static_cast<int>(*y),
                               static_cast<int>(*w), static_cast<int>(*h));

  content::WebContents* wc = web_contents();
  if (!wc || wc->IsBeingDestroyed()) {
    Finish();
    return;
  }

  const gfx::Rect container_bounds = wc->GetContainerBounds();
  gfx::Rect anchor_rect_screen = rect_css_px;
  anchor_rect_screen.Offset(container_bounds.x(), container_bounds.y());
  capture_anchor_screen_ = anchor_rect_screen;

  if (!AttachDevToolsClient()) {
    Finish();
    return;
  }

  CaptureRect(rect_css_px);
}

bool MahoDomScreenshotHandler::AttachDevToolsClient() {
  if (agent_host_) {
    return true;
  }

  content::WebContents* wc = web_contents();
  if (!wc) {
    return false;
  }

  scoped_refptr<content::DevToolsAgentHost> agent_host =
      content::DevToolsAgentHost::GetOrCreateFor(wc);
  if (!agent_host) {
    return false;
  }

  if (!agent_host->AttachClient(this)) {
    return false;
  }
  agent_host_ = agent_host;
  return true;
}

void MahoDomScreenshotHandler::CaptureRect(const gfx::Rect& rect_css_px) {
  base::DictValue clip;
  clip.Set("x", static_cast<double>(rect_css_px.x()));
  clip.Set("y", static_cast<double>(rect_css_px.y()));
  clip.Set("width", static_cast<double>(rect_css_px.width()));
  clip.Set("height", static_cast<double>(rect_css_px.height()));
  clip.Set("scale", 1.0);

  base::DictValue params;
  params.Set("format", "png");
  params.Set("clip", std::move(clip));
  params.Set("fromSurface", true);

  base::DictValue message;
  message.Set("id", kCaptureCommandId);
  message.Set("method", "Page.captureScreenshot");
  message.Set("params", std::move(params));

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(message)), &json);
  capture_in_flight_ = true;
  agent_host_->DispatchProtocolMessage(this, base::as_byte_span(json));
}

void MahoDomScreenshotHandler::DispatchProtocolMessage(
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

  capture_in_flight_ = false;

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

  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_ = nullptr;
  }

  if (!capture_anchor_screen_) {
    Finish();
    return;
  }

  captured_image_ = std::move(image);
  ShowResultBubble(*capture_anchor_screen_);
}

void MahoDomScreenshotHandler::ShowResultBubble(
    const gfx::Rect& anchor_rect_screen) {
  content::WebContents* wc = web_contents();
  if (!wc || wc->IsBeingDestroyed() || finished_) {
    Finish();
    return;
  }

  gfx::NativeView native_view = wc->GetNativeView();
  if (!native_view) {
    Finish();
    return;
  }

  MahoDomScreenshotActionMenu menu;
  menu.header_label = u"Send To...";
  menu.header_icon = &maho_lucide_icons::kShareIcon;
  menu.header_activate = base::BindOnce(
      &MahoDomScreenshotHandler::OnSendToClicked, weak_factory_.GetWeakPtr());

  menu.rows.push_back({
      u"Copy", &maho_lucide_icons::kCopyIcon, true, false,
      base::BindOnce(&MahoDomScreenshotHandler::OnCopyClicked,
                     weak_factory_.GetWeakPtr()),
  });
  menu.rows.push_back({
      u"Save...", &maho_lucide_icons::kArrowDownToLineIcon, true, false,
      base::BindOnce(&MahoDomScreenshotHandler::OnSaveClicked,
                     weak_factory_.GetWeakPtr()),
  });
  menu.rows.push_back({
      u"Retake", &maho_lucide_icons::kRotateCwIcon, true, false,
      base::BindOnce(&MahoDomScreenshotHandler::OnRetakeClicked,
                     weak_factory_.GetWeakPtr()),
  });

  constexpr bool edit_enabled = true;
  menu.rows.push_back({
      u"Edit", &maho_lucide_icons::kPencilLineIcon, edit_enabled, false,
      edit_enabled
          ? base::BindOnce(&MahoDomScreenshotHandler::OnEditClicked,
                           weak_factory_.GetWeakPtr())
          : base::OnceClosure(),
  });

#if BUILDFLAG(IS_MAC)
  constexpr bool imessage_enabled = true;
#else
  const bool imessage_enabled = false;
#endif
  menu.rows.push_back({
      u"Send via iMessage...", &maho_lucide_icons::kMessageSquareIcon, imessage_enabled, false,
      imessage_enabled
          ? base::BindOnce(
                &MahoDomScreenshotHandler::OnSendViaIMessageClicked,
                weak_factory_.GetWeakPtr())
          : base::OnceClosure(),
  });

  constexpr bool save_to_library_enabled = true;
  menu.rows.push_back({
      u"Save to Library", &maho_lucide_icons::kArrowDownToLineIcon, save_to_library_enabled,
      false,
      save_to_library_enabled
          ? base::BindOnce(&MahoDomScreenshotHandler::OnSaveToLibraryClicked,
                           weak_factory_.GetWeakPtr())
          : base::OnceClosure(),
  });

  menu.dismiss_callback = base::BindOnce(
      &MahoDomScreenshotHandler::OnBubbleDismissed,
      weak_factory_.GetWeakPtr());

  result_bubble_widget_ = MahoDomScreenshotActionDialogView::Show(
      native_view, anchor_rect_screen, std::move(menu));
  if (!result_bubble_widget_) {
    Finish();
  }
}

void MahoDomScreenshotHandler::OnCopyClicked() {
  const SkBitmap bitmap = captured_image_.AsBitmap();
  if (!bitmap.isNull()) {
    {
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteImage(bitmap);
    }
  }
  Finish();
}

void MahoDomScreenshotHandler::OnSaveClicked() {
  OnDownloadClicked();
}

void MahoDomScreenshotHandler::OnDownloadClicked() {
  content::WebContents* wc = web_contents();
  if (!wc) {
    Finish();
    return;
  }

  const SkBitmap bitmap = captured_image_.AsBitmap();
  if (bitmap.isNull()) {
    Finish();
    return;
  }

  const GURL data_url = GURL(webui::GetBitmapDataUrl(bitmap));
  content::DownloadManager* download_manager =
      wc->GetBrowserContext()->GetDownloadManager();
  if (!download_manager) {
    Finish();
    return;
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_screenshot_save", R"(
      semantics {
        sender: "Maho Browser Screenshots"
        description:
          "The user captured a selected area of the current page. This saves "
          "the generated image via a data URL to disk on the local client."
        trigger: "User clicks 'Download' in the Maho post-capture bubble."
        data: "A capture of a selected portion of the current webpage."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting: "No user-visible setting."
        policy_exception_justification: "Not implemented."
      })");

  std::unique_ptr<download::DownloadUrlParameters> params =
      content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(
          wc, data_url, traffic_annotation);
  params->set_suggested_name(FilenameForURL(wc->GetLastCommittedURL()));
  download_manager->DownloadUrl(std::move(params));

  Finish();
}

void MahoDomScreenshotHandler::OnBubbleDismissed() {
  result_bubble_widget_ = nullptr;
  Finish();
}

void MahoDomScreenshotHandler::OnSendToClicked() {
  content::WebContents* wc = web_contents();
  if (!wc) {
    Finish();
    return;
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(wc);
  if (!browser) {
    Finish();
    return;
  }
  sharing_hub::SharingHubBubbleController* controller =
      sharing_hub::SharingHubBubbleController::CreateOrGetFromWebContents(wc);
  if (controller) {
    controller->ShowBubble(share::ShareAttempt(wc));
  }
  Finish();
}

void MahoDomScreenshotHandler::OnRetakeClicked() {
  content::WebContents* wc = web_contents();
  if (wc) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&MahoDomScreenshotHandler::TriggerCapture, wc));
  }
  Finish();
}

void MahoDomScreenshotHandler::OnEditClicked() {
  Finish();
}

void MahoDomScreenshotHandler::OnSendViaIMessageClicked() {
  Finish();
}

void MahoDomScreenshotHandler::OnSaveToLibraryClicked() {
  OnDownloadClicked();
}

void MahoDomScreenshotHandler::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {
  Observe(nullptr);
  agent_host_ = nullptr;
  finished_ = true;
  if (result_bubble_widget_ && !result_bubble_widget_->IsClosed()) {
    result_bubble_widget_->CloseNow();
  }
  result_bubble_widget_ = nullptr;
  delete this;
}

void MahoDomScreenshotHandler::Finish() {
  if (finished_) {
    return;
  }
  Observe(nullptr);
  finished_ = true;
  capture_anchor_screen_.reset();
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_ = nullptr;
  }
  if (result_bubble_widget_ && !result_bubble_widget_->IsClosed()) {
    result_bubble_widget_->CloseNow();
  }
  result_bubble_widget_ = nullptr;
  delete this;
}

void MahoDomScreenshotHandler::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle || !navigation_handle->HasCommitted() ||
      !navigation_handle->IsInPrimaryMainFrame()) {
    return;
  }
  Finish();
}

void MahoDomScreenshotHandler::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  content::WebContents* wc = web_contents();
  if (!wc || render_frame_host == wc->GetPrimaryMainFrame()) {
    Finish();
  }
}

void MahoDomScreenshotHandler::WebContentsDestroyed() {
  Finish();
}

}  // namespace maho
