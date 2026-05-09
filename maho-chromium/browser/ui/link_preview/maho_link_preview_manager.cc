// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/link_preview/maho_link_preview_manager.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/link_preview/link_hover_script_string.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

const char kMahoAiApiKeyPref[] = "maho.ai.api_key";
const char kOpenAIEndpoint[] = "https://api.openai.com/v1/chat/completions";
constexpr int kLinkPreviewWorldId = content::ISOLATED_WORLD_ID_CONTENT_END + 3;
constexpr int kMaxResponseBytes = 64 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_link_preview", R"(
      semantics {
        sender: "Maho Link Preview"
        description: "Sends a link URL to an OpenAI-compatible endpoint for summarization."
        trigger: "User hovers over a link for 500ms."
        data: "Link URL and anchor text."
        destination: OTHER
        internal { contacts { email: "maho@example.com" } }
        user_data { type: OTHER }
        last_reviewed: "2026-04-27"
      }
      policy {
        cookies_allowed: NO
        setting: "User must set an API key to enable this feature."
      }
    )");

}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(MahoLinkPreviewManager);

MahoLinkPreviewManager::MahoLinkPreviewManager(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<MahoLinkPreviewManager>(*web_contents) {
  auto* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  prefs_ = profile->GetPrefs();
  url_loader_factory_ = profile->GetURLLoaderFactory();
}

MahoLinkPreviewManager::~MahoLinkPreviewManager() {
  CloseBubble();
}

void MahoLinkPreviewManager::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted()) {
    return;
  }
  CloseBubble();
  InjectHoverScript();
}

void MahoLinkPreviewManager::OnDidAddMessageToConsole(
    content::RenderFrameHost* source_frame,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id,
    const std::optional<std::u16string>& untrusted_stack_trace) {
  std::string msg = base::UTF16ToUTF8(message);
  static constexpr char kPrefix[] = "MAHO_LINK_PREVIEW:";
  if (!base::StartsWith(msg, kPrefix)) {
    return;
  }

  auto parsed =
      base::JSONReader::Read(msg.substr(strlen(kPrefix)), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }

  const base::DictValue& dict = parsed->GetDict();
  const std::string* type = dict.FindString("type");
  if (!type) {
    return;
  }

  if (*type == "hover") {
    const std::string* url = dict.FindString("url");
    const std::string* link_text = dict.FindString("text");
    const base::DictValue* rect_dict = dict.FindDict("rect");
    if (!url || !link_text || !rect_dict) {
      return;
    }
    gfx::Rect rect(rect_dict->FindInt("x").value_or(0),
                    rect_dict->FindInt("y").value_or(0),
                    rect_dict->FindInt("width").value_or(200),
                    rect_dict->FindInt("height").value_or(20));

    // Convert page-relative rect to screen coordinates.
    gfx::Rect container = web_contents()->GetContainerBounds();
    rect.Offset(container.x(), container.y());

    HandleLinkHover(*url, *link_text, rect);
  } else if (*type == "dismiss") {
    HandleLinkDismiss();
  }
}

void MahoLinkPreviewManager::InjectHoverScript() {
  content::RenderFrameHost* rfh = web_contents()->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    return;
  }
  rfh->ExecuteJavaScriptInIsolatedWorld(
      std::u16string(maho::kMahoLinkHoverScript), base::NullCallback(),
      kLinkPreviewWorldId);
}

void MahoLinkPreviewManager::HandleLinkHover(const std::string& url,
                                              const std::string& text,
                                              const gfx::Rect& rect) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  generation_++;
  pending_generation_ = generation_;
  pending_anchor_rect_ = rect;

  CloseBubble();
  ShowBubble(u"Loading preview\u2026", rect);

  std::string api_key = prefs_->GetString(kMahoAiApiKeyPref);
  if (api_key.empty()) {
    ShowBubble(u"No API key configured for link previews.", rect);
    return;
  }

  std::string prompt = base::StrCat(
      {"Summarize this link in 1-2 short sentences. Link URL: ", url,
       " Link text: ", text});

  base::DictValue event = base::DictValue().Set(
      "ChatMessage",
      base::DictValue()
          .Set("message", prompt)
          .Set("include_page_context", false));

  std::string event_json;
  base::JSONWriter::Write(event, &event_json);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&MahoLinkPreviewManager::HandleEventOnBackground,
                     event_json),
      base::BindOnce(&MahoLinkPreviewManager::OnRustEventResponse,
                     weak_factory_.GetWeakPtr(), pending_generation_));
}

void MahoLinkPreviewManager::HandleLinkDismiss() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  pending_generation_ = 0;
  url_loader_.reset();
  CloseBubble();
}

// static
std::string MahoLinkPreviewManager::HandleEventOnBackground(
    const std::string& event_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return "{}";
  }
  return maho::core::HandleEvent(core, event_json.c_str());
}

void MahoLinkPreviewManager::OnRustEventResponse(
    uint64_t generation,
    const std::string& rust_response_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (generation != pending_generation_) {
    return;
  }

  auto parsed =
      base::JSONReader::Read(rust_response_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    ShowBubble(u"Failed to process link preview.", pending_anchor_rect_);
    return;
  }

  const base::DictValue* llm_request = nullptr;

  if (parsed->is_dict()) {
    llm_request = parsed->GetDict().FindDict("RequestLlmCompletion");
    if (!llm_request) {
      llm_request = parsed->GetDict().FindDict("request_llm_completion");
    }
    if (!llm_request) {
      const std::string* kind = parsed->GetDict().FindString("kind");
      if (kind && *kind == "request_llm_completion") {
        llm_request = &parsed->GetDict();
      }
    }
  } else if (parsed->is_list()) {
    for (const auto& item : parsed->GetList()) {
      const base::DictValue* d = item.GetIfDict();
      if (!d) continue;
      const std::string* kind = d->FindString("kind");
      if (kind && *kind == "request_llm_completion") {
        llm_request = d;
        break;
      }
    }
  }

  if (!llm_request) {
    ShowBubble(u"Unexpected response from AI engine.", pending_anchor_rect_);
    return;
  }

  auto prompt_messages = maho::ai::ExtractPromptMessages(*llm_request);
  if (!prompt_messages) {
    ShowBubble(u"Invalid prompt from AI engine.", pending_anchor_rect_);
    return;
  }

  base::DictValue request_body = base::DictValue()
      .Set("model", "gpt-4o-mini")
      .Set("stream", false)
      .Set("messages", base::Value(std::move(*prompt_messages)));

  std::string body_json;
  base::JSONWriter::Write(request_body, &body_json);

  std::string api_key = prefs_->GetString(kMahoAiApiKeyPref);
  MakeLlmRequest(generation, body_json, api_key);
}

void MahoLinkPreviewManager::MakeLlmRequest(uint64_t generation,
                                              const std::string& body_json,
                                              const std::string& api_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = GURL(kOpenAIEndpoint);
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Authorization", "Bearer " + api_key);
  resource_request->headers.SetHeader("Content-Type", "application/json");

  url_loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                                  kTrafficAnnotation);
  url_loader_->AttachStringForUpload(body_json, "application/json");
  url_loader_->SetAllowHttpErrorResults(true);

  url_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&MahoLinkPreviewManager::OnLlmResponse,
                     weak_factory_.GetWeakPtr(), generation),
      kMaxResponseBytes);
}

void MahoLinkPreviewManager::OnLlmResponse(
    uint64_t generation,
    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  url_loader_.reset();

  if (generation != pending_generation_) {
    return;
  }

  if (!response_body || response_body->empty()) {
    ShowBubble(u"No response from AI service.", pending_anchor_rect_);
    return;
  }

  auto parsed =
      base::JSONReader::Read(*response_body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    ShowBubble(u"Failed to parse AI response.", pending_anchor_rect_);
    return;
  }

  const base::ListValue* choices = parsed->GetDict().FindList("choices");
  if (!choices || choices->empty()) {
    ShowBubble(u"Empty AI response.", pending_anchor_rect_);
    return;
  }

  const base::DictValue* first_choice = (*choices)[0].GetIfDict();
  if (!first_choice) {
    ShowBubble(u"Invalid AI response format.", pending_anchor_rect_);
    return;
  }

  const base::DictValue* msg = first_choice->FindDict("message");
  if (!msg) {
    ShowBubble(u"Missing message in AI response.", pending_anchor_rect_);
    return;
  }

  const std::string* content = msg->FindString("content");
  if (!content || content->empty()) {
    ShowBubble(u"Empty summary from AI.", pending_anchor_rect_);
    return;
  }

  ShowBubble(base::UTF8ToUTF16(*content), pending_anchor_rect_);
}

void MahoLinkPreviewManager::ShowBubble(const std::u16string& text,
                                         const gfx::Rect& anchor_rect) {
  CloseBubble();

  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      nullptr, views::BubbleBorder::TOP_LEFT);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets(8));
  delegate->SetAnchorRect(anchor_rect);

  auto contents = std::make_unique<views::View>();
  contents->SetLayoutManager(std::make_unique<views::FillLayout>());
  auto label = std::make_unique<views::Label>(text);
  label->SetMultiLine(true);
  label->SetMaximumWidth(360);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  contents->AddChildView(std::move(label));
  delegate->SetContentsView(std::move(contents));

  bubble_widget_ = views::BubbleDialogDelegate::CreateBubble(
      std::move(delegate));
  if (bubble_widget_) {
    bubble_widget_->Show();
  }
}

void MahoLinkPreviewManager::CloseBubble() {
  if (bubble_widget_) {
    bubble_widget_->Close();
    bubble_widget_ = nullptr;
  }
}
