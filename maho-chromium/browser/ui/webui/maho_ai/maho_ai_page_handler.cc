// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/webui/maho_ai/maho_ai_page_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
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

namespace {

const char kMahoAiApiKeyPref[] = "maho.ai.api_key";
const char kMahoAiEndpointPref[] = "maho.ai.endpoint";
const char kMahoAiModelPref[] = "maho.ai.model";
const char kOpenAIEndpoint[] = "https://api.openai.com/v1/chat/completions";
const char kDefaultModel[] = "gpt-4o-mini";
constexpr size_t kMaxHistory = 200;

const char kNoActiveTabMessage[] =
    "There is no active page to analyze right now.";
const char kCannotAccessPageMessage[] =
    "Maho AI couldn't access the current page content. Try again once the page finishes loading.";
const char kExtractionFailedMessage[] =
    "Maho AI couldn't extract readable content from the current page.";

bool IsPageRequiredIntent(maho_ai::mojom::ChatIntent chat_intent) {
  switch (chat_intent) {
    case maho_ai::mojom::ChatIntent::kAnalyzeCurrentPage:
    case maho_ai::mojom::ChatIntent::kExplainCurrentPage:
    case maho_ai::mojom::ChatIntent::kSummarizeCurrentPage:
      return true;
    case maho_ai::mojom::ChatIntent::kFreeform:
      return false;
  }
}

std::string GetPageContextFailureMessage(
    MahoAIPageHandler::PageContextResult::Status status) {
  switch (status) {
    case MahoAIPageHandler::PageContextResult::Status::kNoActiveTab:
      return kNoActiveTabMessage;
    case MahoAIPageHandler::PageContextResult::Status::kCannotAccess:
      return kCannotAccessPageMessage;
    case MahoAIPageHandler::PageContextResult::Status::kExtractionFailed:
      return kExtractionFailedMessage;
    case MahoAIPageHandler::PageContextResult::Status::kSuccess:
      return std::string();
  }
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
      llm_client_(prefs, std::move(url_loader_factory)) {
  DCHECK(prefs_);
}

MahoAIPageHandler::~MahoAIPageHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MahoAIPageHandler::PageContextResult::PageContextResult() = default;
MahoAIPageHandler::PageContextResult::~PageContextResult() = default;
MahoAIPageHandler::PageContextResult::PageContextResult(PageContextResult&&) = default;
MahoAIPageHandler::PageContextResult& MahoAIPageHandler::PageContextResult::operator=(PageContextResult&&) = default;

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
  std::move(callback).Run(llm_client_.HasConfiguredCredential());
}

void MahoAIPageHandler::ResetStreamingState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  weak_factory_.InvalidateWeakPtrs();
  llm_client_.Reset();
  received_llm_content_ = false;
  current_request_id_.clear();
}

void MahoAIPageHandler::clearHistory() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  chat_history_.clear();
  ResetStreamingState();
}

void MahoAIPageHandler::SendMessage(maho_ai::mojom::ChatIntent chat_intent,
                                    const std::string& message) {
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

  if (!llm_client_.HasConfiguredCredential()) {
    auto error_msg = maho_ai::mojom::ChatMessage::New();
    error_msg->role = "assistant";
    error_msg->content =
        "No API key configured. Please set your API key in the settings below.";
    error_msg->timestamp = now + 0.001;
    chat_history_.push_back(error_msg->Clone());
    page_->OnStreamingComplete(std::move(error_msg));
    return;
  }

  GetPageContext(base::BindOnce(
      [](base::WeakPtr<MahoAIPageHandler> handler,
         maho_ai::mojom::ChatIntent chat_intent, std::string message,
         double now, PageContextResult page_context) {
        if (!handler) {
          return;
        }

        MahoAIPageHandler* handler_raw = handler.get();

        if (IsPageRequiredIntent(chat_intent) &&
            page_context.status != PageContextResult::Status::kSuccess) {
          auto error_msg = maho_ai::mojom::ChatMessage::New();
          error_msg->role = "assistant";
          error_msg->content = GetPageContextFailureMessage(page_context.status);
          error_msg->timestamp = now + 0.001;
          handler_raw->chat_history_.push_back(error_msg->Clone());
          handler_raw->page_->OnStreamingComplete(std::move(error_msg));
          return;
        }

        base::DictValue event = base::DictValue()
            .Set("kind", "chat_message")
            .Set("message", message)
            .Set("include_page_context",
                 page_context.status == PageContextResult::Status::kSuccess)
            .Set(
                "intent",
                chat_intent == maho_ai::mojom::ChatIntent::kAnalyzeCurrentPage
                    ? "analyze_current_page"
                : chat_intent ==
                          maho_ai::mojom::ChatIntent::kExplainCurrentPage
                    ? "explain_current_page"
                : chat_intent ==
                          maho_ai::mojom::ChatIntent::kSummarizeCurrentPage
                    ? "summarize_current_page"
                    : "freeform");

        std::string event_json;
        base::JSONWriter::Write(event, &event_json);

        handler_raw->ResetStreamingState();

        base::ThreadPool::PostTaskAndReplyWithResult(
            FROM_HERE, {base::MayBlock()},
            base::BindOnce(&MahoAIPageHandler::HandleEventOnBackground,
                           event_json),
            base::BindOnce(&MahoAIPageHandler::OnRustEventResponse,
                           handler_raw->weak_factory_.GetWeakPtr(), message));
      },
      weak_factory_.GetWeakPtr(), chat_intent, message, now));
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

  auto parsed = base::JSONReader::Read(rust_response_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    HandleLlmError("Failed to process message through the AI engine.");
    return;
  }

  base::ListValue& updates = parsed->GetList();
  base::DictValue* llm_request = nullptr;
  for (auto& update : updates) {
    base::DictValue* update_dict = update.GetIfDict();
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
    HandleLlmError("Unexpected response from AI engine.");
    return;
  }

  const std::string* request_id = llm_request->FindString("request_id");
  if (request_id) {
    current_request_id_ = *request_id;
  }

  base::ListValue* prompt_messages = llm_request->FindList("prompt");
  std::optional<base::Value> parsed_prompt_storage;
  if (!prompt_messages) {
    base::Value* prompt_value = llm_request->Find("prompt");
    if (prompt_value) {
      if (auto* prompt_value_dict = prompt_value->GetIfDict()) {
        prompt_messages = prompt_value_dict->FindList("messages");
      } else if (const std::string* prompt_json = prompt_value->GetIfString()) {
        parsed_prompt_storage = base::JSONReader::Read(*prompt_json,
                                                       base::JSON_PARSE_RFC);
        if (parsed_prompt_storage) {
          if (auto* prompt_list = parsed_prompt_storage->GetIfList()) {
            prompt_messages = prompt_list;
          } else if (auto* parsed_prompt_dict =
                         parsed_prompt_storage->GetIfDict()) {
            prompt_messages = parsed_prompt_dict->FindList("messages");
          }
        }
      }
    }
  }
  if (!prompt_messages) {
    HandleLlmError("Invalid prompt from AI engine.");
    return;
  }

  MahoAiLlmClient::CompletionRequest request;
  request.prompt_messages = std::move(*prompt_messages);
  received_llm_content_ = false;

  llm_client_.StartCompletion(
      std::move(request),
      base::BindRepeating(&MahoAIPageHandler::OnLlmToken,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&MahoAIPageHandler::FinalizeStreamingResponse,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&MahoAIPageHandler::HandleLlmError,
                     weak_factory_.GetWeakPtr()));
}

void MahoAIPageHandler::OnLlmToken(const std::string& token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  received_llm_content_ = true;
  page_->OnStreamingResponse(token);
}

void MahoAIPageHandler::FinalizeStreamingResponse(
    const std::string& full_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto response = maho_ai::mojom::ChatMessage::New();
  response->role = "assistant";
  response->content = full_text;
  response->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  chat_history_.push_back(response->Clone());

  page_->OnStreamingComplete(std::move(response));

  if (received_llm_content_ && !current_request_id_.empty()) {
    SendLlmResultToRust(current_request_id_, full_text);
  }

  received_llm_content_ = false;
  current_request_id_.clear();
}

void MahoAIPageHandler::HandleLlmError(const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto error_msg = maho_ai::mojom::ChatMessage::New();
  error_msg->role = "assistant";
  error_msg->content = error;
  error_msg->timestamp = base::Time::Now().InSecondsFSinceUnixEpoch();
  chat_history_.push_back(error_msg->Clone());
  page_->OnStreamingComplete(std::move(error_msg));

  received_llm_content_ = false;
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

void MahoAIPageHandler::GetPageContext(PageContextCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  content::WebContents* active_wc =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!active_wc) {
    PageContextResult result;
    result.status = PageContextResult::Status::kNoActiveTab;
    std::move(callback).Run(std::move(result));
    return;
  }

  std::string title = base::UTF16ToUTF8(active_wc->GetTitle());
  std::string url = active_wc->GetVisibleURL().spec();

  content::RenderFrameHost* rfh = active_wc->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    PageContextResult result;
    result.status = PageContextResult::Status::kCannotAccess;
    result.title = title;
    result.url = url;
    std::move(callback).Run(std::move(result));
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      u"(function() {"
      u"  if (!document.body) return '';"
      u"  var text = document.body.innerText || '';"
      u"  return text.substring(0, 50000);"
      u"})()",
      base::BindOnce(&MahoAIPageHandler::OnPageContextExtracted,
                     weak_factory_.GetWeakPtr(), title, url,
                     std::move(callback)),
      content::ISOLATED_WORLD_ID_CONTENT_END + 1);
}

void MahoAIPageHandler::OnPageContextExtracted(
    const std::string& title,
    const std::string& url,
    PageContextCallback callback,
    base::Value result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  PageContextResult page_context;
  page_context.title = title;
  page_context.url = url;

  if (result.is_string()) {
    page_context.text = result.GetString();
  }
  if (page_context.text.empty()) {
    page_context.status = PageContextResult::Status::kExtractionFailed;
  } else {
    page_context.status = PageContextResult::Status::kSuccess;
  }
  std::move(callback).Run(std::move(page_context));
}

void MahoAIPageHandler::GetPageContent(GetPageContentCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  GetPageContext(base::BindOnce(
      [](GetPageContentCallback callback, PageContextResult page_context) {
        std::string text_content;
        switch (page_context.status) {
          case PageContextResult::Status::kSuccess:
            text_content = page_context.text;
            break;
          case PageContextResult::Status::kNoActiveTab:
            text_content = "No active tab";
            break;
          case PageContextResult::Status::kCannotAccess:
            text_content = "Cannot access page content";
            break;
          case PageContextResult::Status::kExtractionFailed:
            text_content = "Unable to extract page content";
            break;
        }
        std::move(callback).Run(page_context.title, page_context.url,
                                text_content);
      },
      std::move(callback)));
}

void MahoAIPageHandler::GetChatHistory(GetChatHistoryCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<maho_ai::mojom::ChatMessagePtr> history;
  for (const auto& msg : chat_history_) {
    history.push_back(msg->Clone());
  }
  std::move(callback).Run(std::move(history));
}
