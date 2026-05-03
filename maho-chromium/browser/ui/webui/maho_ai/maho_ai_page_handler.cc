// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include <optional>
#include <string>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "components/prefs/pref_registry_simple.h"
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
const char kMahoAiEndpointPref[] = "maho.ai.endpoint";
const char kMahoAiModelPref[] = "maho.ai.model";
const char kMahoAiProviderPref[] = "maho.ai.provider";
const char kMahoAiLocalServerEndpointPref[] = "maho.ai.localServer.endpoint";
const char kMahoAiLocalServerModelPref[] = "maho.ai.localServer.model";
const char kMahoAiCustomEndpointPref[] = "maho.ai.custom.endpoint";
const char kMahoAiCustomModelPref[] = "maho.ai.custom.model";
const char kMahoAiLocalServerProvider[] = "Local Server";
const char kMahoAiLegacyCustomProvider[] = "Custom";
const char kOpenAIEndpoint[] = "https://api.openai.com/v1/chat/completions";
const char kDefaultLocalServerEndpoint[] = "http://localhost:11434/v1";
const char kDefaultModel[] = "gpt-4o-mini";

constexpr size_t kMaxHistory = 200;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_ai_llm_request", R"(
      semantics {
        sender: "Maho AI Panel"
        description: "Sends chat messages to an OpenAI-compatible LLM endpoint."
        trigger: "User sends a message in the Maho AI side panel."
        data: "Chat message content and conversation history."
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

bool IsLocalServerProvider(const std::string& provider) {
  return provider == kMahoAiLocalServerProvider ||
         provider == kMahoAiLegacyCustomProvider;
}

#if defined(__APPLE__)
std::string GetMacOSAppDefaultsString(const char* key) {
  auto key_ref = base::SysUTF8ToCFStringRef(key);
  CFPropertyListRef value_ref =
      CFPreferencesCopyAppValue(key_ref.get(), CFSTR("com.maho.browser"));

  if (!value_ref) {
    return std::string();
  }

  if (CFGetTypeID(value_ref) != CFStringGetTypeID()) {
    CFRelease(value_ref);
    return std::string();
  }

  std::string value = base::SysCFStringRefToUTF8(
      static_cast<CFStringRef>(value_ref));
  CFRelease(value_ref);
  return value;
}
#else
std::string GetMacOSAppDefaultsString(const char* key) {
  return std::string();
}
#endif

struct LocalServerConfig {
  std::string endpoint;
  std::string model;
};

std::optional<LocalServerConfig> GetLocalServerConfig() {
  const std::string provider = GetMacOSAppDefaultsString(kMahoAiProviderPref);
  if (!IsLocalServerProvider(provider)) {
    return std::nullopt;
  }

  std::string endpoint =
      GetMacOSAppDefaultsString(kMahoAiLocalServerEndpointPref);
  if (endpoint.empty()) {
    endpoint = GetMacOSAppDefaultsString(kMahoAiCustomEndpointPref);
  }
  if (endpoint.empty()) {
    endpoint = kDefaultLocalServerEndpoint;
  }

  std::string model = GetMacOSAppDefaultsString(kMahoAiLocalServerModelPref);
  if (model.empty()) {
    model = GetMacOSAppDefaultsString(kMahoAiCustomModelPref);
  }
  if (model.empty()) {
    return std::nullopt;
  }

  return LocalServerConfig{.endpoint = std::move(endpoint),
                           .model = std::move(model)};
}

}  // namespace

MahoAIPageHandler::MahoAIPageHandler(
    mojo::PendingReceiver<maho_ai::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_ai::mojom::Page> page,
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)) {
  DCHECK(prefs_);
  DCHECK(url_loader_factory_);
}

MahoAIPageHandler::~MahoAIPageHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

// static
void MahoAIPageHandler::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kMahoAiApiKeyPref, std::string());
  registry->RegisterStringPref(kMahoAiEndpointPref, kOpenAIEndpoint);
  registry->RegisterStringPref(kMahoAiModelPref, kDefaultModel);
}

void MahoAIPageHandler::SetApiKey(const std::string& key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  prefs_->SetString(kMahoAiApiKeyPref, key);
}

void MahoAIPageHandler::GetApiKeyStatus(GetApiKeyStatusCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(!prefs_->GetString(kMahoAiApiKeyPref).empty() ||
                          GetLocalServerConfig().has_value());
}

void MahoAIPageHandler::ResetStreamingState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  weak_factory_.InvalidateWeakPtrs();

  if (url_loader_) {
    url_loader_.reset();
  }

  is_streaming_ = false;
  accumulated_response_.clear();
  sse_buffer_.clear();
  current_request_id_.clear();
}

void MahoAIPageHandler::clearHistory() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  chat_history_.clear();
  ResetStreamingState();
}

void MahoAIPageHandler::SendMessage(const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  double now = base::Time::Now().InSecondsFSinceUnixEpoch();

  auto user_msg = maho_ai::mojom::ChatMessage::New();
  user_msg->role = "user";
  user_msg->content = message;
  user_msg->timestamp = now;
  chat_history_.push_back(user_msg->Clone());

  while (chat_history_.size() > kMaxHistory) {
    chat_history_.erase(chat_history_.begin());
  }

  const std::optional<LocalServerConfig> local_server_config =
      GetLocalServerConfig();
  std::string api_key = prefs_->GetString(kMahoAiApiKeyPref);
  if (api_key.empty() && !local_server_config.has_value()) {
    auto error_msg = maho_ai::mojom::ChatMessage::New();
    error_msg->role = "assistant";
    error_msg->content =
        "No API key configured. Please set your API key in the settings below.";
    error_msg->timestamp = now + 0.001;
    chat_history_.push_back(error_msg->Clone());
    page_->OnStreamingComplete(std::move(error_msg));
    return;
  }

  GetPageContent(base::BindOnce(
    [](base::WeakPtr<MahoAIPageHandler> handler, std::string message,
       const std::string& title, const std::string& url,
       const std::string& page_content) {
        if (!handler) {
          return;
        }

        MahoAIPageHandler* handler_raw = handler.get();

        const bool include_page_context =
            page_content != "No active tab" &&
            page_content != "Cannot access page content" &&
            page_content != "Unable to extract page content" &&
            !page_content.empty();

        base::DictValue event = base::DictValue()
            .Set("kind", "chat_message")
            .Set("message", message)
            .Set("include_page_context", include_page_context);

        std::string event_json;
        base::JSONWriter::Write(event, &event_json);

        handler_raw->ResetStreamingState();
        handler_raw->is_streaming_ = true;

        base::ThreadPool::PostTaskAndReplyWithResult(
            FROM_HERE, {base::MayBlock()},
            base::BindOnce(&MahoAIPageHandler::HandleEventOnBackground,
                           event_json),
            base::BindOnce(&MahoAIPageHandler::OnRustEventResponse,
                           handler_raw->weak_factory_.GetWeakPtr(), message));
      },
      weak_factory_.GetWeakPtr(), message));
}

// static
std::string MahoAIPageHandler::HandleEventOnBackground(
    const std::string& event_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return "{}";
  }
  return maho::core::HandleEvent(core, event_json.c_str());
}

void MahoAIPageHandler::OnRustEventResponse(
    const std::string& user_message,
    const std::string& rust_response_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(ERROR) << "Maho AI raw engine response: " << rust_response_json;

  auto parsed = base::JSONReader::Read(rust_response_json,
                                       base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    auto error_msg = maho_ai::mojom::ChatMessage::New();
    error_msg->role = "assistant";
    error_msg->content = "Failed to process message through the AI engine.";
    error_msg->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
    chat_history_.push_back(error_msg->Clone());
    page_->OnStreamingComplete(std::move(error_msg));
    is_streaming_ = false;
    return;
  }

  const base::ListValue& updates = parsed->GetList();
  const base::DictValue* llm_request = nullptr;
  for (const auto& update : updates) {
    const base::DictValue* update_dict = update.GetIfDict();
    if (!update_dict) {
      continue;
    }

    const std::string* kind = update_dict->FindString("kind");
    if (kind && *kind == "request_llm_completion") {
      llm_request = update_dict;
      break;
    }
  }

  if (!llm_request) {
    auto error_msg = maho_ai::mojom::ChatMessage::New();
    error_msg->role = "assistant";
    error_msg->content = "Unexpected response from AI engine.";
    error_msg->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
    chat_history_.push_back(error_msg->Clone());
    page_->OnStreamingComplete(std::move(error_msg));
    is_streaming_ = false;
    return;
  }

  const std::string* request_id = llm_request->FindString("request_id");
  if (request_id) {
    current_request_id_ = *request_id;
  }

  const base::ListValue* prompt_messages = llm_request->FindList("prompt");
  std::optional<base::Value> parsed_prompt_storage;
  if (!prompt_messages) {
    const base::Value* prompt_value = llm_request->Find("prompt");
    if (prompt_value) {
      if (const auto* prompt_value_dict = prompt_value->GetIfDict()) {
        prompt_messages = prompt_value_dict->FindList("messages");
      } else if (const std::string* prompt_json = prompt_value->GetIfString()) {
        parsed_prompt_storage = base::JSONReader::Read(*prompt_json,
                                                       base::JSON_PARSE_RFC);
        if (parsed_prompt_storage) {
          if (const auto* prompt_list = parsed_prompt_storage->GetIfList()) {
            prompt_messages = prompt_list;
          } else if (const auto* parsed_prompt_dict =
                         parsed_prompt_storage->GetIfDict()) {
            prompt_messages = parsed_prompt_dict->FindList("messages");
          }
        }
      }
    }
  }
  if (!prompt_messages) {
    auto error_msg = maho_ai::mojom::ChatMessage::New();
    error_msg->role = "assistant";
    error_msg->content = "Invalid prompt from AI engine.";
    error_msg->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
    chat_history_.push_back(error_msg->Clone());
    page_->OnStreamingComplete(std::move(error_msg));
    is_streaming_ = false;
    return;
  }

  const std::optional<LocalServerConfig> local_server_config =
      GetLocalServerConfig();

  const std::string endpoint = prefs_->GetString(kMahoAiEndpointPref);
  const std::string model = prefs_->GetString(kMahoAiModelPref);
  const std::string resolved_endpoint =
      local_server_config.has_value() ? local_server_config->endpoint : endpoint;
  const std::string resolved_model =
      local_server_config.has_value() ? local_server_config->model : model;

  base::DictValue request_body = base::DictValue()
      .Set("model", resolved_model.empty() ? kDefaultModel : resolved_model)
      .Set("stream", true)
      .Set("messages", prompt_messages->Clone());

  std::string body_json;
  base::JSONWriter::Write(request_body, &body_json);

  std::string api_key = prefs_->GetString(kMahoAiApiKeyPref);

  const std::string endpoint_base =
      resolved_endpoint.empty() ? kOpenAIEndpoint : resolved_endpoint;
  GURL endpoint_url(endpoint_base);
  if (endpoint_url.is_valid() && !endpoint_base.empty()) {
    std::string request_url = endpoint_url.spec();
    if (!base::EndsWith(request_url, "/", base::CompareCase::SENSITIVE)) {
      request_url.push_back('/');
    }
    request_url.append("chat/completions");
    endpoint_url = GURL(request_url);
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = endpoint_url.is_valid() ? endpoint_url : GURL(kOpenAIEndpoint);
  resource_request->method = "POST";
  if (!api_key.empty()) {
    resource_request->headers.SetHeader("Authorization",
                                        "Bearer " + api_key);
  }
  resource_request->headers.SetHeader("Content-Type", "application/json");

  url_loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kTrafficAnnotation);
  url_loader_->AttachStringForUpload(body_json, "application/json");
  url_loader_->SetAllowHttpErrorResults(true);

  url_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&MahoAIPageHandler::OnLlmResponseDownloaded,
                     weak_factory_.GetWeakPtr()),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void MahoAIPageHandler::OnLlmResponseDownloaded(
    std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (body.has_value()) {
    sse_buffer_ = std::move(*body);
    std::string normalized_body;
    normalized_body.reserve(sse_buffer_.size());
    for (size_t i = 0; i < sse_buffer_.size(); ++i) {
      if (sse_buffer_[i] == '\r') {
        continue;
      }
      normalized_body.push_back(sse_buffer_[i]);
    }
    sse_buffer_ = std::move(normalized_body);
    if (!sse_buffer_.empty() &&
        !base::EndsWith(sse_buffer_, "\n\n", base::CompareCase::SENSITIVE)) {
      sse_buffer_ += "\n\n";
    }
    ProcessSSEBuffer();
  }

  if (is_streaming_) {
    FinalizeStreamingResponse();
  }

  url_loader_.reset();
}

void MahoAIPageHandler::OnDataReceived(std::string_view string_piece,
                                        base::OnceClosure resume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sse_buffer_.append(string_piece);
  ProcessSSEBuffer();
  std::move(resume).Run();
}

void MahoAIPageHandler::OnRetry(base::OnceClosure start_retry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sse_buffer_.clear();
  accumulated_response_.clear();
  std::move(start_retry).Run();
}

void MahoAIPageHandler::ProcessSSEBuffer() {
  // SSE events are delimited by double newlines.
  size_t pos;
  while ((pos = sse_buffer_.find("\n\n")) != std::string::npos) {
    std::string event_block = sse_buffer_.substr(0, pos);
    sse_buffer_.erase(0, pos + 2);

    for (const auto& line :
         base::SplitStringPiece(event_block, "\n",
                                base::KEEP_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      if (!base::StartsWith(line, "data:")) {
        continue;
      }

      std::string data = std::string(line.substr(5));
      size_t first_non_space = data.find_first_not_of(" \t");
      if (first_non_space != std::string::npos) {
        data = data.substr(first_non_space);
      } else {
        data = std::string();
      }
      if (data == "[DONE]") {
        FinalizeStreamingResponse();
        return;
      }

      auto json = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
      if (!json || !json->is_dict()) {
        continue;
      }

      const base::ListValue* choices = json->GetDict().FindList("choices");
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
        page_->OnStreamingResponse(*content);
      }
    }
  }
}

void MahoAIPageHandler::OnComplete(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!sse_buffer_.empty()) {
    sse_buffer_ += "\n\n";
    ProcessSSEBuffer();
  }

  if (is_streaming_) {
    FinalizeStreamingResponse();
  }

  url_loader_.reset();
}

void MahoAIPageHandler::FinalizeStreamingResponse() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  is_streaming_ = false;

  auto response = maho_ai::mojom::ChatMessage::New();
  response->role = "assistant";
  response->content = accumulated_response_.empty()
                          ? "No response received from the AI service."
                          : accumulated_response_;
  response->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  chat_history_.push_back(response->Clone());

  page_->OnStreamingComplete(std::move(response));

  if (!current_request_id_.empty() && !accumulated_response_.empty()) {
    SendLlmResultToRust(current_request_id_, accumulated_response_);
  }

  accumulated_response_.clear();
  sse_buffer_.clear();
  current_request_id_.clear();
}

void MahoAIPageHandler::SendLlmResultToRust(const std::string& request_id,
                                             const std::string& full_text) {
  base::DictValue event = base::DictValue()
      .Set("kind", "llm_result")
      .Set("request_id", request_id)
      .Set("result", full_text);

  std::string event_json;
  base::JSONWriter::Write(event, &event_json);

  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](std::string json) {
            MahoCore* core = maho::GetCore();
            if (core) {
              maho::core::HandleEvent(core, json.c_str());
            }
          },
          std::move(event_json)));
}

void MahoAIPageHandler::GetPageContent(GetPageContentCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  content::WebContents* active_wc =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!active_wc) {
    std::move(callback).Run("", "", "No active tab");
    return;
  }

  std::string title = base::UTF16ToUTF8(active_wc->GetTitle());
  std::string url = active_wc->GetVisibleURL().spec();

  content::RenderFrameHost* rfh = active_wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    std::move(callback).Run(title, url, "Cannot access page content");
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      u"(function() {"
      u"  if (!document.body) return '';"
      u"  var text = document.body.innerText || '';"
      u"  return text.substring(0, 50000);"
      u"})()",
      base::BindOnce(&MahoAIPageHandler::OnPageContentExtracted,
                     weak_factory_.GetWeakPtr(), title, url,
                     std::move(callback)),
      content::ISOLATED_WORLD_ID_CONTENT_END + 1);
}

void MahoAIPageHandler::OnPageContentExtracted(
    const std::string& title,
    const std::string& url,
    GetPageContentCallback callback,
    base::Value result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string text;
  if (result.is_string()) {
    text = result.GetString();
  }
  if (text.empty()) {
    text = "Unable to extract page content";
  }
  std::move(callback).Run(title, url, text);
}

void MahoAIPageHandler::GetChatHistory(GetChatHistoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::ChatMessagePtr> history;
  for (const auto& msg : chat_history_) {
    history.push_back(msg->Clone());
  }
  std::move(callback).Run(std::move(history));
}
