// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_inline_edit/maho_inline_edit_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace {

const char kMahoAiApiKeyPref[] = "maho.ai.api_key";
const char kOpenAIEndpoint[] = "https://api.openai.com/v1/chat/completions";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_inline_edit_request", R"(
      semantics {
        sender: "Maho Inline Edit"
        description: "Sends text editing requests to an OpenAI-compatible LLM."
        trigger: "User selects text in an editable field and picks an edit action."
        data: "Selected text and editing instruction."
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

// Embedded JS injection script — loaded at compile time.
#include "maho/browser/ui/webui/maho_inline_edit/inline_edit_script_string.h"

}  // namespace

MahoInlineEditHandler::MahoInlineEditHandler(
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : browser_(browser),
      prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)) {
  DCHECK(prefs_);
  DCHECK(url_loader_factory_);
}

MahoInlineEditHandler::~MahoInlineEditHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

content::WebContents* MahoInlineEditHandler::GetActiveWebContents() {
  if (!browser_) {
    return nullptr;
  }
  return browser_->tab_strip_model()->GetActiveWebContents();
}

void MahoInlineEditHandler::ExecuteJsInActiveTab(
    const std::u16string& script) {
  content::WebContents* wc = GetActiveWebContents();
  if (!wc) {
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    return;
  }
  rfh->ExecuteJavaScriptInIsolatedWorld(
      script, base::NullCallback(),
      content::ISOLATED_WORLD_ID_CONTENT_END + 2);
}

void MahoInlineEditHandler::InjectScriptIntoActiveTab() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ExecuteJsInActiveTab(base::UTF8ToUTF16(std::string(kInlineEditScript)));
}

void MahoInlineEditHandler::GetSelectedText(GetSelectedTextCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  content::WebContents* wc = GetActiveWebContents();
  if (!wc) {
    std::move(callback).Run("");
    return;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    std::move(callback).Run("");
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      u"window.__mahoGetSelectedText ? window.__mahoGetSelectedText() : ''",
      base::BindOnce(
          [](GetSelectedTextCallback cb, base::Value result) {
            std::string text;
            if (result.is_string()) {
              text = result.GetString();
            }
            std::move(cb).Run(text);
          },
          std::move(callback)),
      content::ISOLATED_WORLD_ID_CONTENT_END + 2);
}

void MahoInlineEditHandler::RequestEdit(const std::string& text,
                                        const std::string& instruction,
                                        RequestEditCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string api_key = prefs_->GetString(kMahoAiApiKeyPref);
  if (api_key.empty()) {
    std::move(callback).Run("No API key configured.");
    return;
  }

  base::DictValue event = base::DictValue().Set(
      "ChatMessage",
      base::DictValue()
          .Set("message", instruction)
          .Set("include_page_context", false));

  std::string event_json;
  base::JSONWriter::Write(event, &event_json);

  is_streaming_ = true;
  accumulated_response_.clear();
  sse_buffer_.clear();

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&MahoInlineEditHandler::HandleEventOnBackground,
                     event_json),
      base::BindOnce(&MahoInlineEditHandler::OnRustEventResponse,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

// static
std::string MahoInlineEditHandler::HandleEventOnBackground(
    const std::string& event_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return "{}";
  }
  return maho::core::HandleEvent(core, event_json.c_str());
}

void MahoInlineEditHandler::OnRustEventResponse(
    RequestEditCallback callback,
    const std::string& rust_response_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto parsed =
      base::JSONReader::Read(rust_response_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    is_streaming_ = false;
    std::move(callback).Run("Failed to process edit request.");
    return;
  }

  const base::DictValue& root = parsed->GetDict();
  const base::DictValue* llm_request = root.FindDict("RequestLlmCompletion");
  if (!llm_request) {
    is_streaming_ = false;
    std::move(callback).Run("Unexpected response from AI engine.");
    return;
  }

  const base::ListValue* prompt_messages = llm_request->FindList("prompt");
  if (!prompt_messages) {
    is_streaming_ = false;
    std::move(callback).Run("Invalid prompt from AI engine.");
    return;
  }

  std::string api_key = prefs_->GetString(kMahoAiApiKeyPref);
  StartLlmRequest(api_key, *prompt_messages, std::move(callback));
}

void MahoInlineEditHandler::StartLlmRequest(
    const std::string& api_key,
    const base::ListValue& prompt_messages,
    RequestEditCallback callback) {
  pending_callback_ = std::move(callback);

  base::DictValue request_body = base::DictValue()
      .Set("model", "gpt-4o-mini")
      .Set("stream", true)
      .Set("messages", prompt_messages.Clone());

  std::string body_json;
  base::JSONWriter::Write(request_body, &body_json);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = GURL(kOpenAIEndpoint);
  resource_request->method = "POST";
  resource_request->headers.SetHeader("Authorization", "Bearer " + api_key);
  resource_request->headers.SetHeader("Content-Type", "application/json");

  url_loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kTrafficAnnotation);
  url_loader_->AttachStringForUpload(body_json, "application/json");
  url_loader_->SetAllowHttpErrorResults(true);
  url_loader_->DownloadAsStream(url_loader_factory_.get(), this);
}

void MahoInlineEditHandler::OnDataReceived(std::string_view string_piece,
                                           base::OnceClosure resume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sse_buffer_.append(string_piece);
  ProcessSSEBuffer();
  std::move(resume).Run();
}

void MahoInlineEditHandler::OnRetry(base::OnceClosure start_retry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sse_buffer_.clear();
  accumulated_response_.clear();
  std::move(start_retry).Run();
}

void MahoInlineEditHandler::ProcessSSEBuffer() {
  size_t pos;
  while ((pos = sse_buffer_.find("\n\n")) != std::string::npos) {
    std::string event_block = sse_buffer_.substr(0, pos);
    sse_buffer_.erase(0, pos + 2);

    for (const auto& line : base::SplitStringPiece(
             event_block, "\n", base::KEEP_WHITESPACE,
             base::SPLIT_WANT_NONEMPTY)) {
      if (!base::StartsWith(line, "data: ")) {
        continue;
      }

      auto data = line.substr(6);
      if (data == "[DONE]") {
        FinalizeStreaming();
        return;
      }

      auto json = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
      if (!json || !json->is_dict()) {
        continue;
      }

      const base::ListValue* choices =
          json->GetDict().FindList("choices");
      if (!choices || choices->empty()) {
        continue;
      }

      const base::DictValue* first_choice = (*choices)[0].GetIfDict();
      if (!first_choice) {
        continue;
      }

      const base::DictValue* delta = first_choice->FindDict("delta");
      if (!delta) {
        continue;
      }

      const std::string* content = delta->FindString("content");
      if (content && !content->empty()) {
        accumulated_response_ += *content;
      }
    }
  }
}

void MahoInlineEditHandler::OnComplete(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!sse_buffer_.empty()) {
    sse_buffer_ += "\n\n";
    ProcessSSEBuffer();
  }

  if (is_streaming_) {
    FinalizeStreaming();
  }

  url_loader_.reset();
}

void MahoInlineEditHandler::FinalizeStreaming() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  is_streaming_ = false;

  std::string result = accumulated_response_.empty()
                           ? "No response received from the AI service."
                           : accumulated_response_;

  if (pending_callback_) {
    std::move(pending_callback_).Run(result);
  }

  std::string escaped = result;
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\\", "\\\\");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "'", "\\'");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\n", "\\n");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\r", "\\r");

  ExecuteJsInActiveTab(base::UTF8ToUTF16(
      "window.__mahoInlineEditResult && window.__mahoInlineEditResult('" +
      escaped + "')"));

  accumulated_response_.clear();
  sse_buffer_.clear();
}

void MahoInlineEditHandler::ApplyEdit(const std::string& edited_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string escaped = edited_text;
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\\", "\\\\");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "'", "\\'");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\n", "\\n");
  base::ReplaceSubstringsAfterOffset(&escaped, 0, "\r", "\\r");

  ExecuteJsInActiveTab(base::UTF8ToUTF16(
      "window.__mahoReplaceSelection && window.__mahoReplaceSelection('" +
      escaped + "')"));
}
