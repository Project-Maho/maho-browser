#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "maho_chat_orchestration_service.h"

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "components/prefs/pref_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_browser_tool_executor.h"
#include "maho/browser/ai/maho_browser_tool_registry.h"
#include "maho/browser/ai/maho_extract_prompt_messages.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/third_party/maho/maho_bridge.h"

namespace {

const char kNoActiveTabMessage[] =
    "There is no active page to analyze right now.";
const char kCannotAccessPageMessage[] =
    "Maho AI couldn't access the current page content. Try again once the page finishes loading.";
const char kExtractionFailedMessage[] =
    "Maho AI couldn't extract readable content from the current page.";

}  // namespace

MahoChatOrchestrationService::MahoChatOrchestrationService(
    Browser* browser,
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : browser_(browser),
      prefs_(prefs),
      llm_client_(prefs, std::move(url_loader_factory),
                  prefs->GetBoolean(maho::ai_prefs::kProviderAdapterV2Enabled)),
      page_context_extractor_(browser) {
  DCHECK(prefs_);
  structured_context_v2_enabled_ =
      prefs_->GetBoolean(maho::ai_prefs::kStructuredContextV2Enabled);
  browser_tools_v1_enabled_ =
      prefs_->GetBoolean(maho::ai_prefs::kBrowserToolsV1Enabled);
  chat_orchestrator_v2_enabled_ =
      prefs_->GetBoolean(maho::ai_prefs::kChatOrchestratorV2Enabled);
}

MahoChatOrchestrationService::ConversationRoundState::ConversationRoundState()
    : chat_intent(maho_ai::mojom::ChatIntent::kFreeform) {}

MahoChatOrchestrationService::ConversationRoundState::ConversationRoundState(
    ConversationRoundState&&) = default;

MahoChatOrchestrationService::ConversationRoundState&
MahoChatOrchestrationService::ConversationRoundState::operator=(
    ConversationRoundState&&) = default;

MahoChatOrchestrationService::ConversationRoundState::~ConversationRoundState() = default;

MahoChatOrchestrationService::~MahoChatOrchestrationService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool MahoChatOrchestrationService::HasConfiguredCredential() const {
  return llm_client_.HasConfiguredCredential();
}

void MahoChatOrchestrationService::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  llm_client_.Reset();
  received_llm_content_ = false;
  current_request_id_.clear();
}

void MahoChatOrchestrationService::HandleMessage(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    TokenCallback on_token,
    CompleteCallback on_complete,
    ErrorCallback on_error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  RequestClassification classification = ClassifyRequest(message, chat_intent);
  if (!chat_orchestrator_v2_enabled_ &&
      (classification.request_mode == RequestMode::kBrowserAction ||
       classification.request_mode == RequestMode::kToolAssistedTask)) {
    classification.request_mode = classification.requires_page_context
                                     ? RequestMode::kPageQuestion
                                     : RequestMode::kGeneralChat;
  }

  Reset();
  page_context_extractor_.GetPageContext(base::BindOnce(
      &MahoChatOrchestrationService::OnPageContextReady,
      weak_factory_.GetWeakPtr(), message, chat_intent, classification,
      std::move(on_token), std::move(on_complete), std::move(on_error)));
}

void MahoChatOrchestrationService::OnPageContextReady(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    RequestClassification classification,
    TokenCallback on_token,
    CompleteCallback on_complete,
    ErrorCallback on_error,
    PageContextResult page_context) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (RequestModeRequiresPageContext(classification.request_mode) &&
      !page_context.HasUsableContent()) {
    std::move(on_error).Run(
        GetPageContextFailureMessage(page_context.extraction_status));
    return;
  }

  std::string event_json =
      ShapeRustEvent(message, chat_intent, classification, page_context,
                     structured_context_v2_enabled_, browser_tools_v1_enabled_);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&MahoChatOrchestrationService::HandleEventOnBackground,
                     event_json),
      base::BindOnce(&MahoChatOrchestrationService::OnRustEventResponse,
                     weak_factory_.GetWeakPtr(), message, chat_intent,
                     classification, std::move(page_context),
                     std::move(on_token), std::move(on_complete),
                     std::move(on_error)));
}

// static
std::string MahoChatOrchestrationService::ShapeRustEvent(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    const RequestClassification& classification,
    const PageContextResult& page_context,
    bool structured_context_v2_enabled,
    bool browser_tools_v1_enabled) {
  bool include_context = classification.requires_page_context &&
                         page_context.HasUsableContent();

  base::DictValue event = base::DictValue()
                              .Set("kind", "chat_message")
                              .Set("message", message)
                              .Set("include_page_context", include_context)
                              .Set("page_context",
                                   include_context
                                       ? base::Value(std::string("Title: ") +
                                                     page_context.title +
                                                     "\nURL: " + page_context.url +
                                                     "\nContent:\n" +
                                                     page_context.GetLegacyText())
                                       : base::Value())
                              .Set("page_context_status",
                                   page_context.GetExtractionStatusString())
                              .Set("intent", GetIntentName(chat_intent))
                              .Set("request_mode",
                                   GetRequestModeName(classification.request_mode));

  if (structured_context_v2_enabled) {
    event.Set("page_context_details", page_context.ToValue());
    event.Set("structured_context_v2", page_context.ToValue());
  }

  if (browser_tools_v1_enabled) {
    event.Set("browser_tools",
              MahoBrowserToolRegistry::SerializePhase1ToolSchemas(
                  browser_tools_v1_enabled));
  }

  std::string event_json;
  base::JSONWriter::Write(event, &event_json);
  return event_json;
}

// static
std::string MahoChatOrchestrationService::HandleEventOnBackground(
    const std::string& event_json) {
  MahoCore* core = maho::GetCore();
  if (!core) {
    return "{}";
  }
  return maho::core::HandleEvent(core, event_json.c_str());
}

void MahoChatOrchestrationService::OnRustEventResponse(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    RequestClassification classification,
    PageContextResult page_context,
    TokenCallback on_token,
    CompleteCallback on_complete,
    ErrorCallback on_error,
    const std::string& rust_response_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(ERROR) << "Maho AI raw engine response: " << rust_response_json;

  auto parsed = base::JSONReader::Read(rust_response_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    std::move(on_error).Run("Failed to process message through the AI engine.");
    return;
  }

  base::DictValue* llm_request = nullptr;

  if (parsed->is_list()) {
    for (auto& update : parsed->GetList()) {
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
  } else if (parsed->is_dict()) {
    base::DictValue* nested =
        parsed->GetDict().FindDict("request_llm_completion");
    if (!nested) {
      nested = parsed->GetDict().FindDict("RequestLlmCompletion");
    }
    if (nested) {
      llm_request = nested;
    } else {
      const std::string* kind = parsed->GetDict().FindString("kind");
      if (kind && *kind == "request_llm_completion") {
        llm_request = &parsed->GetDict();
      }
    }
  }

  if (!llm_request) {
    std::move(on_error).Run("Unexpected response from AI engine.");
    return;
  }

  const std::string* request_id = llm_request->FindString("request_id");
  if (request_id) {
    current_request_id_ = *request_id;
  }

  auto prompt_messages = maho::ai::ExtractPromptMessages(*llm_request);
  if (!prompt_messages) {
    std::move(on_error).Run("Invalid prompt from AI engine.");
    return;
  }

  auto state = std::make_shared<ConversationRoundState>();
  state->message = message;
  state->chat_intent = chat_intent;
  state->classification = classification;
  state->page_context = std::move(page_context);
  state->on_token = std::move(on_token);
  state->on_complete = std::move(on_complete);
  state->on_error = std::move(on_error);
  state->prompt_messages = std::move(*prompt_messages);

  StartConversationRound(std::move(state));
}

void MahoChatOrchestrationService::StartConversationRound(
    std::shared_ptr<ConversationRoundState> state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!state) {
    return;
  }
  if (state->round_index >= kMaxToolRounds) {
    std::move(state->on_error).Run(
        "The browser tool loop reached its limit before producing a final answer.");
    return;
  }

  state->round_index++;
  received_llm_content_ = false;

  MahoAiLlmClient::CompletionRequest request;
  request.prompt_messages = state->prompt_messages.Clone();
  llm_client_.StartCompletion(
      std::move(request),
      base::BindRepeating(&MahoChatOrchestrationService::OnLlmToken,
                          weak_factory_.GetWeakPtr(), state->on_token),
      base::BindOnce(&MahoChatOrchestrationService::OnLlmRoundComplete,
                     weak_factory_.GetWeakPtr(), state),
      base::BindOnce([](std::shared_ptr<ConversationRoundState> state,
                        const std::string& error) {
        if (state) {
          std::move(state->on_error).Run(error);
        }
      }, state));
}

void MahoChatOrchestrationService::OnLlmRoundComplete(
    std::shared_ptr<ConversationRoundState> state,
    MahoAiLlmClient::CompletionResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!state) {
    return;
  }

  if (!ResponseRequestsTools(result)) {
    if (received_llm_content_ && !current_request_id_.empty()) {
      SendLlmResultToRust(current_request_id_, result.full_text);
    }
    received_llm_content_ = false;
    current_request_id_.clear();
    std::move(state->on_complete).Run(result.full_text);
    return;
  }

  if (!browser_tools_v1_enabled_) {
    std::move(state->on_error).Run("Browser tools are disabled.");
    return;
  }

  state->assistant_text = result.full_text;
  state->pending_tool_calls = std::move(result.tool_calls);
  state->pending_tool_index = 0;
  AppendAssistantToolCallMessage(&state->prompt_messages, state->assistant_text,
                                 state->pending_tool_calls);
  ExecuteNextToolInvocation(std::move(state));
}

void MahoChatOrchestrationService::ExecuteNextToolInvocation(
    std::shared_ptr<ConversationRoundState> state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!state) {
    return;
  }
  if (state->pending_tool_index >= state->pending_tool_calls.size()) {
    if (received_llm_content_ && !current_request_id_.empty()) {
      SendLlmResultToRust(current_request_id_, state->assistant_text);
    }
    received_llm_content_ = false;
    current_request_id_.clear();
    std::move(state->on_complete).Run(state->assistant_text);
    return;
  }

  const auto& tool_call =
      state->pending_tool_calls[state->pending_tool_index++];
  ToolInvocation invocation;
  invocation.id = tool_call.id;
  invocation.name = tool_call.name;
  invocation.arguments_json = tool_call.arguments_json;
  ExecuteToolInvocation(std::move(state), invocation);
}

void MahoChatOrchestrationService::ExecuteToolInvocation(
    std::shared_ptr<ConversationRoundState> state,
    const ToolInvocation& tool_invocation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!state) {
    return;
  }

  base::DictValue arguments;
  if (!tool_invocation.arguments_json.empty()) {
    auto parsed = base::JSONReader::Read(tool_invocation.arguments_json,
                                         base::JSON_PARSE_RFC);
    if (parsed && parsed->is_dict()) {
      arguments = parsed->GetDict().Clone();
    }
  }

  MahoBrowserToolExecutor executor(browser_, browser_tools_v1_enabled_);
  executor.Execute(
      tool_invocation.name, arguments,
      base::BindOnce(&MahoChatOrchestrationService::OnToolInvocationComplete,
                     weak_factory_.GetWeakPtr(), state, tool_invocation));
}

void MahoChatOrchestrationService::OnToolInvocationComplete(
    std::shared_ptr<ConversationRoundState> state,
    ToolInvocation tool_invocation,
    base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!state) {
    return;
  }

  AppendToolResultMessage(&state->prompt_messages, tool_invocation, result);

  if (state->pending_tool_index < state->pending_tool_calls.size()) {
    ExecuteNextToolInvocation(std::move(state));
    return;
  }

  if (state->round_index >= kMaxToolRounds) {
    std::move(state->on_error).Run(
        "The browser tool loop reached its limit before producing a final answer.");
    return;
  }

  StartConversationRound(std::move(state));
}

void MahoChatOrchestrationService::AppendAssistantToolCallMessage(
    base::ListValue* prompt_messages,
    const std::string& assistant_text,
    const std::vector<MahoAiLlmClient::ToolCall>& tool_calls) {
  if (!prompt_messages) {
    return;
  }

  base::DictValue assistant_message;
  assistant_message.Set("role", "assistant");
  assistant_message.Set("content", assistant_text);

  base::ListValue serialized_calls;
  for (const auto& tool_call : tool_calls) {
    base::DictValue call;
    if (tool_call.index >= 0) {
      call.Set("index", tool_call.index);
    }
    if (!tool_call.id.empty()) {
      call.Set("id", tool_call.id);
    }
    call.Set("type", "function");
    base::DictValue function;
    function.Set("name", tool_call.name);
    function.Set("arguments", tool_call.arguments_json);
    call.Set("function", std::move(function));
    serialized_calls.Append(std::move(call));
  }
  assistant_message.Set("tool_calls", std::move(serialized_calls));
  prompt_messages->Append(std::move(assistant_message));
}

void MahoChatOrchestrationService::AppendToolResultMessage(
    base::ListValue* prompt_messages,
    const ToolInvocation& tool_invocation,
    const base::DictValue& result) {
  if (!prompt_messages) {
    return;
  }

  base::DictValue tool_message;
  tool_message.Set("role", "tool");
  if (!tool_invocation.id.empty()) {
    tool_message.Set("tool_call_id", tool_invocation.id);
  }
  tool_message.Set("name", tool_invocation.name);
  tool_message.Set("content", SerializeJsonValue(result));
  prompt_messages->Append(std::move(tool_message));
}

bool MahoChatOrchestrationService::ResponseRequestsTools(
    const MahoAiLlmClient::CompletionResult& result) {
  return !result.tool_calls.empty();
}

std::string MahoChatOrchestrationService::SerializeJsonValue(
    const base::DictValue& value) {
  std::string output;
  base::JSONWriter::Write(value, &output);
  return output;
}

void MahoChatOrchestrationService::OnLlmToken(const TokenCallback& on_token,
                                              const std::string& token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  received_llm_content_ = true;
  on_token.Run(token);
}

void MahoChatOrchestrationService::FinalizeResponse(
    CompleteCallback on_complete,
    const std::string& full_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (received_llm_content_ && !current_request_id_.empty()) {
    SendLlmResultToRust(current_request_id_, full_text);
  }

  received_llm_content_ = false;
  current_request_id_.clear();
  std::move(on_complete).Run(full_text);
}

void MahoChatOrchestrationService::SendLlmResultToRust(
    const std::string& request_id,
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

// ---------- Static classification helpers ----------

// static
MahoChatOrchestrationService::RequestClassification
MahoChatOrchestrationService::ClassifyRequest(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent) {
  RequestClassification classification;

  switch (chat_intent) {
    case maho_ai::mojom::ChatIntent::kAnalyzeCurrentPage:
    case maho_ai::mojom::ChatIntent::kExplainCurrentPage:
    case maho_ai::mojom::ChatIntent::kSummarizeCurrentPage:
      classification.request_mode = RequestMode::kPageQuestion;
      classification.requires_page_context = true;
      return classification;
    case maho_ai::mojom::ChatIntent::kFreeform:
      break;
  }

  std::string lowered = base::ToLowerASCII(message);
  const bool mentions_page =
      lowered.find("page") != std::string::npos ||
      lowered.find("this page") != std::string::npos ||
      lowered.find("current page") != std::string::npos ||
      lowered.find("\xec\x9d\xb4 \xed\x8e\x98\xec\x9d\xb4\xec\xa7\x80") !=
          std::string::npos ||
      lowered.find(
          "\xed\x98\x84\xec\x9e\xac "
          "\xed\x8e\x98\xec\x9d\xb4\xec\xa7\x80") != std::string::npos;
  const bool mentions_browser_action =
      lowered.find("click") != std::string::npos ||
      lowered.find("open") != std::string::npos ||
      lowered.find("navigate") != std::string::npos ||
      lowered.find("scroll") != std::string::npos ||
      lowered.find("type") != std::string::npos ||
      lowered.find("enter") != std::string::npos ||
      lowered.find("browser") != std::string::npos ||
      lowered.find("tab") != std::string::npos;
  const bool mentions_transformation =
      lowered.find("rewrite") != std::string::npos ||
      lowered.find("edit") != std::string::npos ||
      lowered.find("change") != std::string::npos ||
      lowered.find("transform") != std::string::npos ||
      lowered.find("modify") != std::string::npos ||
      lowered.find("replace") != std::string::npos;
  const bool mentions_tool_assisted_task =
      lowered.find("tool") != std::string::npos ||
      lowered.find("automate") != std::string::npos ||
      lowered.find("workflow") != std::string::npos ||
      lowered.find("task") != std::string::npos;

  if (mentions_browser_action) {
    classification.request_mode = RequestMode::kBrowserAction;
    classification.requires_page_context = mentions_page;
    return classification;
  }
  if (mentions_transformation) {
    classification.request_mode = RequestMode::kPageTransformation;
    classification.requires_page_context = mentions_page;
    return classification;
  }
  if (mentions_tool_assisted_task) {
    classification.request_mode = RequestMode::kToolAssistedTask;
    classification.requires_page_context = mentions_page;
    return classification;
  }
  if (mentions_page) {
    classification.request_mode = RequestMode::kPageQuestion;
    classification.requires_page_context = true;
    return classification;
  }

  classification.request_mode = RequestMode::kGeneralChat;
  classification.requires_page_context = false;
  return classification;
}

// static
bool MahoChatOrchestrationService::RequestModeRequiresPageContext(
    RequestMode mode) {
  switch (mode) {
    case RequestMode::kGeneralChat:
      return false;
    case RequestMode::kPageQuestion:
    case RequestMode::kPageTransformation:
    case RequestMode::kBrowserAction:
    case RequestMode::kToolAssistedTask:
      return true;
  }
  return false;
}

// static
std::string MahoChatOrchestrationService::GetRequestModeName(
    RequestMode mode) {
  switch (mode) {
    case RequestMode::kGeneralChat:
      return "general_chat";
    case RequestMode::kPageQuestion:
      return "page_question";
    case RequestMode::kPageTransformation:
      return "page_transformation";
    case RequestMode::kBrowserAction:
      return "browser_action";
    case RequestMode::kToolAssistedTask:
      return "tool_assisted_task";
  }
  return "general_chat";
}

// static
std::string MahoChatOrchestrationService::GetIntentName(
    maho_ai::mojom::ChatIntent intent) {
  switch (intent) {
    case maho_ai::mojom::ChatIntent::kAnalyzeCurrentPage:
      return "analyze_current_page";
    case maho_ai::mojom::ChatIntent::kExplainCurrentPage:
      return "explain_current_page";
    case maho_ai::mojom::ChatIntent::kSummarizeCurrentPage:
      return "summarize_current_page";
    case maho_ai::mojom::ChatIntent::kFreeform:
      return "freeform";
  }
  return "freeform";
}

// static
std::string MahoChatOrchestrationService::GetPageContextFailureMessage(
    MahoAiPageContextExtractor::PageContextResult::Status status) {
  switch (status) {
    case MahoAiPageContextExtractor::PageContextResult::Status::kNoActiveTab:
      return kNoActiveTabMessage;
    case MahoAiPageContextExtractor::PageContextResult::Status::kCannotAccess:
      return kCannotAccessPageMessage;
    case MahoAiPageContextExtractor::PageContextResult::Status::kExtractionFailed:
      return kExtractionFailedMessage;
    case MahoAiPageContextExtractor::PageContextResult::Status::kSuccess:
    case MahoAiPageContextExtractor::PageContextResult::Status::kPartial:
      return std::string();
  }
  return kExtractionFailedMessage;
}
