#include "maho/browser/ai/maho_ai_llm_client.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "components/prefs/pref_service.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {

// Maximum accumulated size for full_text and arguments_json in a single
// streaming response.  Mirrors ConversationTask::kMaxAccumulatedBytes (H13).
constexpr size_t kMaxLlmResponseBytes = 2 * 1024 * 1024;  // 2 MB

const char kMahoAiApiKeyPref[] = "maho.ai.api_key";
const char kMahoAiEndpointPref[] = "maho.ai.endpoint";
const char kMahoAiModelPref[] = "maho.ai.model";
const char kOpenAIEndpoint[] = "https://api.openai.com/v1/chat/completions";
const char kMahoAiLocalServerEndpointPref[] = "maho.ai.localServer.endpoint";
const char kMahoAiLocalServerModelPref[] = "maho.ai.localServer.model";
const char kMahoAiCustomEndpointPref[] = "maho.ai.custom.endpoint";
const char kMahoAiCustomModelPref[] = "maho.ai.custom.model";
const char kMahoAiProviderPref[] = "maho.ai.provider";
const char kMahoAiLocalServerProvider[] = "Local Server";
const char kMahoAiLegacyCustomProvider[] = "Custom";
const char kDefaultLocalServerEndpoint[] = "http://localhost:11434/v1";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("maho_ai_llm_request", R"(
      semantics {
        sender: "Maho AI"
        description: "Sends chat requests to an OpenAI-compatible LLM endpoint."
        trigger: "User sends an AI request in Maho UI surfaces."
        data: "Chat prompt content and conversation history."
        destination: OTHER
        internal { contacts { email: "maho@example.com" } }
        user_data { type: OTHER }
        last_reviewed: "2026-05-04"
      }
      policy {
        cookies_allowed: NO
        setting: "User must configure an AI endpoint to enable this feature."
      }
    )");

struct LocalServerConfig {
  std::string endpoint;
  std::string model;
};

bool IsLocalServerProvider(const std::string& provider) {
  return provider == kMahoAiLocalServerProvider ||
         provider == kMahoAiLegacyCustomProvider;
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

std::string GetConfiguredApiKey(PrefService* prefs) {
  std::string api_key = prefs->GetString(kMahoAiApiKeyPref);
  if (!api_key.empty()) {
    return api_key;
  }
  return GetMacOSAppDefaultsString(kMahoAiApiKeyPref);
}

void AppendChatCompletionsPath(GURL* url) {
  if (!url->is_valid()) {
    return;
  }

  std::string request_url = url->spec();
  if (!base::EndsWith(request_url, "/", base::CompareCase::SENSITIVE)) {
    request_url.push_back('/');
  }
  request_url.append("chat/completions");
  *url = GURL(request_url);
}

std::string NormalizeSseChunk(std::string body) {
  std::string normalized_body;
  normalized_body.reserve(body.size());
  for (char ch : body) {
    if (ch != '\r') {
      normalized_body.push_back(ch);
    }
  }
  return normalized_body;
}

void ProcessSseBuffer(std::string* sse_buffer,
                      MahoAiLlmClient::CompletionResult* completion_result,
                      const MahoAiLlmClient::TokenCallback& on_token,
                      bool* overflowed) {
  // Cursor-based SSE parsing: advance cursor through the buffer without
  // erasing on every iteration.  A single erase(0, cursor) at the end reduces
  // total memmove cost from O(N²) to O(N) for an N-event stream.
  size_t cursor = 0;
  while (true) {
    size_t pos = sse_buffer->find("\n\n", cursor);
    if (pos == std::string::npos) {
      break;
    }
    std::string event_block(sse_buffer->substr(cursor, pos - cursor));
    cursor = pos + 2;

    for (const auto& line : base::SplitStringPiece(
             event_block, "\n", base::KEEP_WHITESPACE,
             base::SPLIT_WANT_NONEMPTY)) {
      if (!base::StartsWith(line, "data:")) {
        continue;
      }

      std::string data = std::string(line.substr(5));
      size_t first_non_space = data.find_first_not_of(" \t");
      if (first_non_space != std::string::npos) {
        data = data.substr(first_non_space);
      } else {
        data.clear();
      }

      if (data == "[DONE]") {
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

      if (const std::string* finish_reason =
              first_choice->FindString("finish_reason")) {
        completion_result->finish_reason = *finish_reason;
      }

      const base::DictValue* delta = first_choice->FindDict("delta");
      if (!delta) {
        continue;
      }

      if (const std::string* content = delta->FindString("content")) {
        if (!content->empty()) {
          if (completion_result->full_text.size() + content->size() >
              kMaxLlmResponseBytes) {
            *overflowed = true;
            return;
          }
          completion_result->full_text += *content;
          if (on_token) {
            on_token.Run(*content);
          }
        }
      }

      const base::ListValue* tool_calls = delta->FindList("tool_calls");
      if (!tool_calls) {
        continue;
      }

      for (size_t i = 0; i < tool_calls->size(); ++i) {
        const base::DictValue* tool_call = (*tool_calls)[i].GetIfDict();
        if (!tool_call) {
          continue;
        }

        const std::optional<int> index = tool_call->FindInt("index");
        const int call_index = index.value_or(static_cast<int>(i));
        const std::string* id = tool_call->FindString("id");
        const base::DictValue* function = tool_call->FindDict("function");
        if (!function) {
          continue;
        }
        const std::string* function_name = function->FindString("name");

        if (completion_result->tool_calls.size() <=
            static_cast<size_t>(call_index)) {
          completion_result->tool_calls.resize(
              static_cast<size_t>(call_index) + 1);
        }
        MahoAiLlmClient::ToolCall& parsed_call =
            completion_result->tool_calls[static_cast<size_t>(call_index)];
        parsed_call.index = call_index;
        if (id && parsed_call.id.empty()) {
          parsed_call.id = *id;
        }
        if (function_name && parsed_call.name.empty()) {
          parsed_call.name = *function_name;
        }
        if (const std::string* arguments = function->FindString("arguments")) {
          if (parsed_call.arguments_json.size() + arguments->size() >
              kMaxLlmResponseBytes) {
            *overflowed = true;
            return;
          }
          parsed_call.arguments_json += *arguments;
        }
      }
    }
  }
  if (cursor > 0) {
    sse_buffer->erase(0, cursor);
  }
}

}  // namespace

class MahoAiLlmClientStreamConsumer final
    : public network::SimpleURLLoaderStreamConsumer {
 public:
  explicit MahoAiLlmClientStreamConsumer(MahoAiLlmClient* client)
      : client_(client) {}

  MahoAiLlmClientStreamConsumer(const MahoAiLlmClientStreamConsumer&) = delete;
  MahoAiLlmClientStreamConsumer& operator=(
      const MahoAiLlmClientStreamConsumer&) = delete;

  void OnDataReceived(std::string_view string_piece,
                      base::OnceClosure resume) override {
    if (client_) {
      client_->OnResponseDataReceived(std::string(string_piece));
    }
    std::move(resume).Run();
  }

  void OnComplete(bool success) override {
    if (client_) {
      client_->OnResponseComplete(success);
    }
  }

  void OnRetry(base::OnceClosure start_retry) override {
    std::move(start_retry).Run();
  }

 private:
  raw_ptr<MahoAiLlmClient> client_;
};

namespace {

class OpenAICompatibleProviderTransport final
    : public MahoAiLlmClient::ProviderTransport {
 public:
  bool HasConfiguredCredential(PrefService* prefs) const override {
    return !GetConfiguredApiKey(prefs).empty() ||
           GetLocalServerConfig().has_value();
  }

  std::optional<std::string> ResolveEndpoint(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    const std::optional<LocalServerConfig> local_server_config =
        GetLocalServerConfig();
    const std::string endpoint_pref = prefs->GetString(kMahoAiEndpointPref);
    return request.endpoint.value_or(local_server_config.has_value()
                                         ? local_server_config->endpoint
                                         : endpoint_pref);
  }

  std::optional<std::string> ResolveModel(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    const std::optional<LocalServerConfig> local_server_config =
        GetLocalServerConfig();
    const std::string model_pref = prefs->GetString(kMahoAiModelPref);
    const std::string model = request.model.value_or(
        local_server_config.has_value() ? local_server_config->model
                                        : model_pref);
    if (model.empty()) {
      return std::nullopt;
    }
    return model;
  }

  MahoAiLlmClient::RequestPlan CreateRequestPlan(
      MahoAiLlmClient::CompletionRequest request,
      const std::string& endpoint,
      const std::string& model) const override {
    base::DictValue request_body;
    request_body.Set("model", model);
    request_body.Set("stream", true);
    request_body.Set("messages", std::move(request.prompt_messages));

    std::string body_json;
    base::JSONWriter::Write(request_body, &body_json);

    GURL endpoint_url(endpoint.empty() ? kOpenAIEndpoint : endpoint);
    AppendChatCompletionsPath(&endpoint_url);

    return MahoAiLlmClient::RequestPlan{
        .url = endpoint_url.is_valid() ? endpoint_url : GURL(kOpenAIEndpoint),
        .body_json = std::move(body_json)};
  }

  void ProcessResponseBody(
      std::string body,
      std::string* sse_buffer,
      MahoAiLlmClient::CompletionResult* completion_result,
      const MahoAiLlmClient::TokenCallback& on_token,
      bool* overflowed) const override {
    *sse_buffer += NormalizeSseChunk(std::move(body));
    ProcessSseBuffer(sse_buffer, completion_result, on_token, overflowed);
  }
};

class ProviderAdapterV2Transport final
    : public MahoAiLlmClient::ProviderTransport {
 public:
  bool HasConfiguredCredential(PrefService* prefs) const override {
    return !GetConfiguredApiKey(prefs).empty() ||
           GetLocalServerConfig().has_value();
  }

  std::optional<std::string> ResolveEndpoint(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    const std::optional<LocalServerConfig> local_server_config =
        GetLocalServerConfig();
    if (request.endpoint.has_value()) {
      return request.endpoint;
    }
    if (local_server_config.has_value()) {
      return local_server_config->endpoint;
    }
    const std::string endpoint_pref = prefs->GetString(kMahoAiEndpointPref);
    return endpoint_pref.empty() ? std::optional<std::string>()
                                 : std::optional<std::string>(endpoint_pref);
  }

  std::optional<std::string> ResolveModel(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    const std::optional<LocalServerConfig> local_server_config =
        GetLocalServerConfig();
    if (request.model.has_value()) {
      return request.model;
    }
    if (local_server_config.has_value()) {
      return local_server_config->model;
    }
    const std::string model_pref = prefs->GetString(kMahoAiModelPref);
    return model_pref.empty() ? std::optional<std::string>()
                              : std::optional<std::string>(model_pref);
  }

  MahoAiLlmClient::RequestPlan CreateRequestPlan(
      MahoAiLlmClient::CompletionRequest request,
      const std::string& endpoint,
      const std::string& model) const override {
    base::DictValue request_body;
    request_body.Set("model", model);
    request_body.Set("stream", true);
    request_body.Set("messages", std::move(request.prompt_messages));

    std::string body_json;
    base::JSONWriter::Write(request_body, &body_json);

    GURL endpoint_url(endpoint.empty() ? kOpenAIEndpoint : endpoint);
    AppendChatCompletionsPath(&endpoint_url);

    return MahoAiLlmClient::RequestPlan{
        .url = endpoint_url.is_valid() ? endpoint_url : GURL(kOpenAIEndpoint),
        .body_json = std::move(body_json)};
  }

  void ProcessResponseBody(
      std::string body,
      std::string* sse_buffer,
      MahoAiLlmClient::CompletionResult* completion_result,
      const MahoAiLlmClient::TokenCallback& on_token,
      bool* overflowed) const override {
    *sse_buffer += NormalizeSseChunk(std::move(body));
    ProcessSseBuffer(sse_buffer, completion_result, on_token, overflowed);
  }
};

}  // namespace

MahoAiLlmClient::MahoAiLlmClient(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    bool provider_adapter_v2_enabled)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      provider_adapter_v2_enabled_(provider_adapter_v2_enabled),
      provider_transport_(provider_adapter_v2_enabled_
                              ? std::unique_ptr<ProviderTransport>(std::make_unique<ProviderAdapterV2Transport>())
                              : std::unique_ptr<ProviderTransport>(std::make_unique<OpenAICompatibleProviderTransport>())) {}

MahoAiLlmClient::~MahoAiLlmClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MahoAiLlmClient::CompletionRequest::CompletionRequest() = default;
MahoAiLlmClient::CompletionRequest::~CompletionRequest() = default;
MahoAiLlmClient::CompletionRequest::CompletionRequest(CompletionRequest&&) = default;
MahoAiLlmClient::CompletionRequest&
MahoAiLlmClient::CompletionRequest::operator=(CompletionRequest&&) = default;

MahoAiLlmClient::ToolCall::ToolCall() = default;
MahoAiLlmClient::ToolCall::~ToolCall() = default;
MahoAiLlmClient::ToolCall::ToolCall(ToolCall&&) = default;
MahoAiLlmClient::ToolCall& MahoAiLlmClient::ToolCall::operator=(ToolCall&&) = default;

MahoAiLlmClient::CompletionResult::CompletionResult() = default;
MahoAiLlmClient::CompletionResult::~CompletionResult() = default;
MahoAiLlmClient::CompletionResult::CompletionResult(CompletionResult&&) = default;
MahoAiLlmClient::CompletionResult&
MahoAiLlmClient::CompletionResult::operator=(CompletionResult&&) = default;

bool MahoAiLlmClient::HasConfiguredCredential() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return provider_transport_->HasConfiguredCredential(prefs_);
}

void MahoAiLlmClient::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  stream_consumer_.reset();
  url_loader_.reset();
  sse_buffer_.clear();
  completion_result_ = CompletionResult();
  on_token_.Reset();
  on_complete_.Reset();
  on_error_.Reset();
}

void MahoAiLlmClient::StartCompletion(CompletionRequest request,
                                      TokenCallback on_token,
                                      CompleteCallback on_complete,
                                      ErrorCallback on_error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  Reset();
  on_token_ = std::move(on_token);
  on_complete_ = std::move(on_complete);
  on_error_ = std::move(on_error);

  const std::optional<std::string> endpoint =
      provider_transport_->ResolveEndpoint(request, prefs_);
  const std::optional<std::string> model =
      provider_transport_->ResolveModel(request, prefs_);
  if (!endpoint.has_value() || !model.has_value()) {
    FinishWithError(
        "No AI model is configured. Check your Local Server or AI settings.");
    return;
  }

  const RequestPlan request_plan =
      provider_transport_->CreateRequestPlan(std::move(request), *endpoint,
                                             *model);

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_plan.url;
  resource_request->method = "POST";
  const std::string api_key = GetConfiguredApiKey(prefs_);
  if (!api_key.empty()) {
    resource_request->headers.SetHeader("Authorization",
                                        "Bearer " + api_key);
  }
  resource_request->headers.SetHeader("Content-Type", "application/json");

  url_loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kTrafficAnnotation);
  url_loader_->AttachStringForUpload(request_plan.body_json,
                                    "application/json");
  url_loader_->SetAllowHttpErrorResults(true);
  stream_consumer_ = std::make_unique<MahoAiLlmClientStreamConsumer>(this);
  url_loader_->DownloadAsStream(url_loader_factory_.get(),
                                stream_consumer_.get());
}

void MahoAiLlmClient::OnResponseDataReceived(std::string body_chunk) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  bool overflowed = false;
  provider_transport_->ProcessResponseBody(std::move(body_chunk), &sse_buffer_,
                                           &completion_result_, on_token_,
                                           &overflowed);
  if (overflowed) {
    FinishWithError("LLM response exceeded 2 MB cap");
  }
}

void MahoAiLlmClient::OnResponseComplete(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!success && sse_buffer_.empty() && completion_result_.full_text.empty() &&
      completion_result_.tool_calls.empty()) {
    FinishWithError("No response received from the AI service.");
    return;
  }

  std::string remaining = std::move(sse_buffer_);
  if (!remaining.empty() &&
      !base::EndsWith(remaining, "\n\n", base::CompareCase::SENSITIVE)) {
    remaining += "\n\n";
  }
  bool overflowed = false;
  provider_transport_->ProcessResponseBody(std::move(remaining), &sse_buffer_,
                                           &completion_result_, on_token_,
                                           &overflowed);
  sse_buffer_.clear();
  if (overflowed) {
    FinishWithError("LLM response exceeded 2 MB cap");
    return;
  }
  FinishWithSuccess();
}

void MahoAiLlmClient::FinishWithSuccess() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (completion_result_.full_text.empty()) {
    completion_result_.full_text = "No response received from the AI service.";
  }
  if (on_complete_) {
    std::move(on_complete_).Run(std::move(completion_result_));
  }
  stream_consumer_.reset();
  url_loader_.reset();
}

void MahoAiLlmClient::FinishWithError(const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (on_error_) {
    std::move(on_error_).Run(error);
  }
  stream_consumer_.reset();
  url_loader_.reset();
}
