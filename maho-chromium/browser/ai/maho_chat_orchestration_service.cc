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
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include "base/strings/sys_string_conversions.h"
#endif
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

#if defined(MAHO_RUST_AI)
struct LocalServerConfig {
  std::string endpoint;
  std::string model;
};

bool IsLocalServerProvider(const std::string& provider) {
  return provider == "Local Server" || provider == "Custom";
}

std::string GetMacOSAppDefaultsString(const char* key) {
#if defined(__APPLE__)
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
  std::string value =
      base::SysCFStringRefToUTF8(static_cast<CFStringRef>(value_ref));
  CFRelease(value_ref);
  return value;
#else
  return std::string();
#endif
}

std::optional<LocalServerConfig> GetLocalServerConfig() {
  const std::string provider = GetMacOSAppDefaultsString("maho.ai.provider");
  if (!IsLocalServerProvider(provider)) {
    return std::nullopt;
  }

  std::string endpoint =
      GetMacOSAppDefaultsString("maho.ai.localServer.endpoint");
  if (endpoint.empty()) {
    endpoint = GetMacOSAppDefaultsString("maho.ai.custom.endpoint");
  }
  if (endpoint.empty()) {
    endpoint = "http://localhost:11434/v1";
  }

  std::string model = GetMacOSAppDefaultsString("maho.ai.localServer.model");
  if (model.empty()) {
    model = GetMacOSAppDefaultsString("maho.ai.custom.model");
  }
  if (model.empty()) {
    return std::nullopt;
  }

  return LocalServerConfig{.endpoint = std::move(endpoint),
                           .model = std::move(model)};
}

std::string GetConfiguredApiKey(PrefService* prefs) {
  std::string api_key = prefs->GetString("maho.ai.api_key");
  if (!api_key.empty()) {
    return api_key;
  }
  return GetMacOSAppDefaultsString("maho.ai.api_key");
}

std::string ResolveEndpoint(PrefService* prefs) {
  const std::optional<LocalServerConfig> local_server_config =
      GetLocalServerConfig();
  std::string endpoint = local_server_config.has_value()
                             ? local_server_config->endpoint
                             : prefs->GetString("maho.ai.endpoint");
  if (endpoint.empty()) {
    endpoint = "https://api.openai.com/v1";
  }
  if (!base::EndsWith(endpoint, "chat/completions",
                      base::CompareCase::INSENSITIVE_ASCII)) {
    if (!base::EndsWith(endpoint, "/", base::CompareCase::SENSITIVE)) {
      endpoint.push_back('/');
    }
    endpoint.append("chat/completions");
  }
  return endpoint;
}

std::string ResolveModel(PrefService* prefs) {
  const std::optional<LocalServerConfig> local_server_config =
      GetLocalServerConfig();
  std::string model = local_server_config.has_value()
                          ? local_server_config->model
                          : prefs->GetString("maho.ai.model");
  if (model.empty()) {
    model = "gpt-4o-mini";
  }
  return model;
}
#endif

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
#if defined(MAHO_RUST_AI)
  if (rust_session_) {
    maho_core_chat_set_event_sink(rust_session_, nullptr, nullptr, nullptr, nullptr);
    maho_core_chat_cancel(rust_session_);
    maho_core_chat_session_free(rust_session_);
    rust_session_ = nullptr;
  }
#endif
}

bool MahoChatOrchestrationService::HasConfiguredCredential() const {
  return llm_client_.HasConfiguredCredential();
}

void MahoChatOrchestrationService::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
#if defined(MAHO_RUST_AI)
  if (rust_session_) {
    maho_core_chat_cancel(rust_session_);
  }
  active_round_state_.reset();
#endif
  llm_client_.Reset();
  received_llm_content_ = false;
  current_request_id_.clear();
}

void MahoChatOrchestrationService::HandleMessage(
    const std::string& message,
    maho_ai::mojom::ChatIntent chat_intent,
    bool force_page_context,
    TokenCallback on_token,
    CompleteCallback on_complete,
    ErrorCallback on_error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

#if defined(MAHO_RUST_AI)
  if (!rust_session_) {
    MahoChatConfig config;
    std::string api_key = GetConfiguredApiKey(prefs_);
    std::string endpoint = ResolveEndpoint(prefs_);
    std::string model = ResolveModel(prefs_);
    std::string system_instruction = "";

    config.api_key = api_key.c_str();
    config.endpoint = endpoint.c_str();
    config.model = model.c_str();
    config.system_instruction = system_instruction.c_str();

    rust_session_ = maho_core_chat_session_new(&config);
    if (!rust_session_) {
      std::move(on_error).Run("Failed to create Rust chat session.");
      return;
    }

    if (browser_tools_v1_enabled_) {
      for (const auto& tool : MahoBrowserToolRegistry::GetPhase1ToolSchemas()) {
        std::string schema_json;
        base::JSONWriter::Write(tool.input_schema, &schema_json);
        maho_core_chat_register_tool(rust_session_, tool.name.c_str(),
                                     tool.description.c_str(), schema_json.c_str());
      }
    }

    ffi_user_data_ = std::make_unique<FfiUserData>();
    ffi_user_data_->weak_service = weak_factory_.GetWeakPtr();
    ffi_user_data_->task_runner = base::SequencedTaskRunner::GetCurrentDefault();

    maho_core_chat_set_event_sink(
        rust_session_,
        &MahoChatOrchestrationService::OnRustTokenCallback,
        &MahoChatOrchestrationService::OnRustCompleteCallback,
        &MahoChatOrchestrationService::OnRustErrorCallback,
        ffi_user_data_.get());
  }

  RequestClassification classification = ClassifyRequest(message, chat_intent);
  if (force_page_context) {
    classification.requires_page_context = true;
  }
  if (!chat_orchestrator_v2_enabled_ &&
      (classification.request_mode == RequestMode::kBrowserAction ||
       classification.request_mode == RequestMode::kToolAssistedTask)) {
    classification.request_mode = classification.requires_page_context
                                     ? RequestMode::kPageQuestion
                                     : RequestMode::kGeneralChat;
  }

  weak_factory_.InvalidateWeakPtrs();
  ffi_user_data_->weak_service = weak_factory_.GetWeakPtr();

  auto state = std::make_shared<ConversationRoundState>();
  state->message = message;
  state->chat_intent = chat_intent;
  state->classification = classification;
  state->on_token = std::move(on_token);
  state->on_complete = std::move(on_complete);
  state->on_error = std::move(on_error);

  active_round_state_ = state;

  page_context_extractor_.GetPageContext(base::BindOnce(
      &MahoChatOrchestrationService::OnPageContextReady,
      weak_factory_.GetWeakPtr(), message, chat_intent, classification,
      base::NullCallback(), base::NullCallback(), base::NullCallback()));
#else
  RequestClassification classification = ClassifyRequest(message, chat_intent);
  if (force_page_context) {
    classification.requires_page_context = true;
  }
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
#endif
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

#if defined(MAHO_RUST_AI)
  if (!active_round_state_) {
    return;
  }
  if (RequestModeRequiresPageContext(classification.request_mode) &&
      !page_context.HasUsableContent()) {
    std::move(active_round_state_->on_error).Run(
        GetPageContextFailureMessage(page_context.extraction_status));
    active_round_state_.reset();
    return;
  }

  active_round_state_->page_context = std::move(page_context);

  std::string final_message = message;
  bool include_context = classification.requires_page_context &&
                         active_round_state_->page_context.HasUsableContent();
  if (include_context) {
    final_message = std::string("Title: ") + active_round_state_->page_context.title +
                    "\nURL: " + active_round_state_->page_context.url +
                    "\nContent:\n" + active_round_state_->page_context.GetLegacyText() +
                    "\n\nUser Question:\n" + message;
  }

  if (!maho_core_chat_send_user_turn(rust_session_, final_message.c_str())) {
    std::move(active_round_state_->on_error).Run("Failed to send user turn.");
    active_round_state_.reset();
  }
#else
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
#endif
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

#if defined(MAHO_RUST_AI)
  std::string result_json = SerializeJsonValue(result);
  bool is_last_tool =
      (state->pending_tool_index >= state->pending_tool_calls.size());
  if (!maho_core_chat_send_tool_result(rust_session_,
                                       tool_invocation.id.c_str(),
                                       tool_invocation.name.c_str(),
                                       result_json.c_str(), is_last_tool)) {
    std::move(state->on_error).Run("Failed to send tool result to Rust.");
    active_round_state_.reset();
    return;
  }

  if (!is_last_tool) {
    ExecuteNextToolInvocation(std::move(state));
  }
#else
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
#endif
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

#if defined(MAHO_RUST_AI)
MahoChatOrchestrationService::FfiUserData::FfiUserData() = default;
MahoChatOrchestrationService::FfiUserData::~FfiUserData() = default;

// static
void MahoChatOrchestrationService::OnRustTokenCallback(void* user_data, const char* token) {
  auto* ud = static_cast<FfiUserData*>(user_data);
  std::string token_str(token);
  ud->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoChatOrchestrationService::OnRustToken,
                     ud->weak_service, std::move(token_str)));
}

// static
void MahoChatOrchestrationService::OnRustCompleteCallback(
    void* user_data,
    const char* full_text,
    const char* tool_calls_json) {
  auto* ud = static_cast<FfiUserData*>(user_data);
  std::string full_text_str(full_text);
  std::string tool_calls_json_str(tool_calls_json);
  ud->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoChatOrchestrationService::OnRustComplete,
                     ud->weak_service, std::move(full_text_str),
                     std::move(tool_calls_json_str)));
}

// static
void MahoChatOrchestrationService::OnRustErrorCallback(void* user_data, const char* error) {
  auto* ud = static_cast<FfiUserData*>(user_data);
  std::string error_str(error);
  ud->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&MahoChatOrchestrationService::OnRustError,
                     ud->weak_service, std::move(error_str)));
}

void MahoChatOrchestrationService::OnRustToken(const std::string& token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_round_state_) {
    return;
  }
  active_round_state_->on_token.Run(token);
}

void MahoChatOrchestrationService::OnRustComplete(
    const std::string& full_text,
    const std::string& tool_calls_json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_round_state_) {
    return;
  }

  maho_core_chat_append_assistant_message(rust_session_, full_text.c_str(),
                                           tool_calls_json.c_str());

  if (tool_calls_json.empty()) {
    std::move(active_round_state_->on_complete).Run(full_text);
    active_round_state_.reset();
    return;
  }

  auto parsed = base::JSONReader::Read(tool_calls_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    std::move(active_round_state_->on_error).Run(
        "Failed to parse tool calls from Rust.");
    active_round_state_.reset();
    return;
  }

  active_round_state_->assistant_text = full_text;
  active_round_state_->pending_tool_calls.clear();
  active_round_state_->pending_tool_index = 0;

  for (const auto& item : parsed->GetList()) {
    const base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    MahoAiLlmClient::ToolCall tc;
    if (const std::string* id = dict->FindString("id")) {
      tc.id = *id;
    }
    if (const std::string* name = dict->FindString("name")) {
      tc.name = *name;
    }
    if (const std::string* args = dict->FindString("arguments_json")) {
      tc.arguments_json = *args;
    }
    active_round_state_->pending_tool_calls.push_back(std::move(tc));
  }

  ExecuteNextToolInvocation(active_round_state_);
}

void MahoChatOrchestrationService::OnRustError(const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_round_state_) {
    return;
  }
  std::move(active_round_state_->on_error).Run(error);
  active_round_state_.reset();
}
#endif

