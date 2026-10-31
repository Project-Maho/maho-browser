#include "maho/browser/ai/maho_ai_llm_client.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/prefs/pref_service.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "chrome/browser/browser_process.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_settings/maho_settings_page_handler.h"
#include "maho/third_party/maho/maho_ffi.h"

namespace {

constexpr size_t kMaxLlmResponseBytes = 2 * 1024 * 1024;  // 2 MB
// Upper bound on the tool-call slots a single response may address. The SSE
// `index` field is model-controlled, so it also bounds the vector this parser
// will grow on demand.
constexpr size_t kMaxLlmToolCallsPerResponse = 256;

const char kMahoAiApiKeyPref[] = "maho.ai.api_key";
const char kMahoAiEndpointPref[] = "maho.ai.base_url";
const char kMahoAiModelPref[] = "maho.ai.model";
const char kMahoAiProviderPref[] = "maho.ai.provider";
const char kOpenAIEndpoint[] = "https://api.openai.com/v1/chat/completions";
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

std::string GetConfiguredApiKey(PrefService* prefs) {
  return prefs->GetString(kMahoAiApiKeyPref);
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
        sse_buffer->erase(0, cursor);
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
        // `index` arrives from the model stream and has no lower bound. A
        // negative value casts to a huge size_t whose +1 wraps to 0, so the
        // resize below would CLEAR the vector and the subscript would write at
        // SIZE_MAX. Reject out-of-range indices instead.
        if (call_index < 0 ||
            static_cast<size_t>(call_index) >= kMaxLlmToolCallsPerResponse) {
          continue;
        }
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

void ProcessAnthropicSseBuffer(
    std::string* sse_buffer,
    MahoAiLlmClient::CompletionResult* completion_result,
    const MahoAiLlmClient::TokenCallback& on_token,
    bool* overflowed) {
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
        sse_buffer->erase(0, cursor);
        return;
      }

      auto json = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
      if (!json || !json->is_dict()) {
        continue;
      }

      const std::string* type = json->GetDict().FindString("type");
      if (type && *type == "message_stop") {
        completion_result->finish_reason = "stop";
      }

      const base::DictValue* delta = json->GetDict().FindDict("delta");
      if (delta) {
        const std::string* text = delta->FindString("text");
        if (text && !text->empty()) {
          if (completion_result->full_text.size() + text->size() >
              kMaxLlmResponseBytes) {
            *overflowed = true;
            return;
          }
          completion_result->full_text += *text;
          if (on_token) {
            on_token.Run(*text);
          }
        }
        if (const std::string* stop_reason = delta->FindString("stop_reason")) {
          completion_result->finish_reason = *stop_reason;
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

void MahoAiLlmClient::ProviderTransport::ApplyHeaders(
    network::ResourceRequest* resource_request,
    const std::string& credential) const {
  if (!credential.empty()) {
    resource_request->headers.SetHeader("Authorization",
                                        "Bearer " + credential);
  }
  resource_request->headers.SetHeader("Content-Type", "application/json");
}

namespace {

class OpenAICompatibleProviderTransport final
    : public MahoAiLlmClient::ProviderTransport {
 public:
  explicit OpenAICompatibleProviderTransport(std::string provider_id = "")
      : provider_id_(std::move(provider_id)) {}

  bool HasConfiguredCredential(PrefService* prefs) const override {
    if (!provider_id_.empty()) {
      return maho::ai::IsProviderConnected(prefs, provider_id_);
    }
    return !GetConfiguredApiKey(prefs).empty() ||
           !prefs->GetString(kMahoAiEndpointPref).empty() ||
           prefs->GetString(kMahoAiProviderPref) == "local-server";
  }

  std::string GetCredential(PrefService* prefs, const os_crypt_async::Encryptor* encryptor) const override {
    if (!provider_id_.empty()) {
      return maho::ai::DecryptProviderKey(prefs, provider_id_, encryptor);
    }
    return GetConfiguredApiKey(prefs);
  }

  std::optional<std::string> ResolveEndpoint(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.endpoint.has_value()) {
      return request.endpoint;
    }
    if (!provider_id_.empty()) {
      std::string ep = maho::ai::ResolveProviderEndpoint(prefs, provider_id_);
      if (!ep.empty()) return ep;
    }
    const std::string endpoint = prefs->GetString(kMahoAiEndpointPref);
    if (!endpoint.empty()) {
      return endpoint;
    }
    if (prefs->GetString(kMahoAiProviderPref) == "local-server" ||
        provider_id_ == "local-server") {
      return std::string(kDefaultLocalServerEndpoint);
    }
    return std::nullopt;
  }

  std::optional<std::string> ResolveModel(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.model.has_value()) {
      return request.model;
    }
    if (!provider_id_.empty()) {
      std::string last = maho::ai::GetProviderLastModel(prefs, provider_id_);
      if (!last.empty()) return last;
    }
    const std::string model = prefs->GetString(kMahoAiModelPref);
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
        // Fail closed on a malformed user-configured endpoint: falling back to
        // kOpenAIEndpoint would ship the user's prompts and BYOK key to OpenAI
        // without telling them. The caller rejects an invalid URL.
        .url = endpoint_url,
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

 private:
  std::string provider_id_;
};

class AnthropicProviderTransport final
    : public MahoAiLlmClient::ProviderTransport {
 public:
  bool HasConfiguredCredential(PrefService* prefs) const override {
    return maho::ai::IsProviderConnected(prefs, maho::ai::kProviderAnthropic);
  }

  std::string GetCredential(PrefService* prefs,
                            const os_crypt_async::Encryptor* encryptor) const override {
    return maho::ai::DecryptProviderKey(prefs, maho::ai::kProviderAnthropic, encryptor);
  }

  std::optional<std::string> ResolveEndpoint(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.endpoint.has_value()) {
      return request.endpoint;
    }
    return std::string("https://api.anthropic.com/v1");
  }

  std::optional<std::string> ResolveModel(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.model.has_value()) {
      return request.model;
    }
    std::string last =
        maho::ai::GetProviderLastModel(prefs, maho::ai::kProviderAnthropic);
    if (!last.empty()) {
      return last;
    }
    const std::string model = prefs->GetString(kMahoAiModelPref);
    if (!model.empty()) {
      return model;
    }
    return std::string("claude-3-5-sonnet-20241022");
  }

  MahoAiLlmClient::RequestPlan CreateRequestPlan(
      MahoAiLlmClient::CompletionRequest request,
      const std::string& endpoint,
      const std::string& model) const override {
    base::DictValue request_body;
    request_body.Set("model", model);
    request_body.Set("stream", true);
    request_body.Set("max_tokens", 4096);

    base::ListValue anthropic_messages;
    std::string system_prompt;
    for (const auto& item : request.prompt_messages) {
      const base::DictValue* msg = item.GetIfDict();
      if (!msg) continue;
      const std::string* role = msg->FindString("role");
      const std::string* content = msg->FindString("content");
      if (role && *role == "system") {
        if (content) {
          if (!system_prompt.empty()) system_prompt += "\n\n";
          system_prompt += *content;
        }
      } else if (role && content) {
        base::DictValue m;
        m.Set("role", *role);
        m.Set("content", *content);
        anthropic_messages.Append(std::move(m));
      }
    }
    if (!system_prompt.empty()) {
      request_body.Set("system", system_prompt);
    }
    request_body.Set("messages", std::move(anthropic_messages));

    std::string body_json;
    base::JSONWriter::Write(request_body, &body_json);

    GURL endpoint_url(endpoint);
    std::string path = endpoint_url.spec();
    if (!base::EndsWith(path, "/", base::CompareCase::SENSITIVE)) {
      path.push_back('/');
    }
    path.append("messages");
    endpoint_url = GURL(path);

    return MahoAiLlmClient::RequestPlan{
        .url = endpoint_url,
        .body_json = std::move(body_json)};
  }

  void ApplyHeaders(network::ResourceRequest* resource_request,
                    const std::string& credential) const override {
    if (!credential.empty()) {
      resource_request->headers.SetHeader("x-api-key", credential);
    }
    resource_request->headers.SetHeader("anthropic-version", "2023-06-01");
    resource_request->headers.SetHeader("Content-Type", "application/json");
  }

  void ProcessResponseBody(
      std::string body,
      std::string* sse_buffer,
      MahoAiLlmClient::CompletionResult* completion_result,
      const MahoAiLlmClient::TokenCallback& on_token,
      bool* overflowed) const override {
    *sse_buffer += NormalizeSseChunk(std::move(body));
    ProcessAnthropicSseBuffer(sse_buffer, completion_result, on_token, overflowed);
  }
};

class ProviderAdapterV2Transport final
    : public MahoAiLlmClient::ProviderTransport {
 public:
  bool HasConfiguredCredential(PrefService* prefs) const override {
    return !GetConfiguredApiKey(prefs).empty() ||
           !prefs->GetString(kMahoAiEndpointPref).empty() ||
           prefs->GetString(kMahoAiProviderPref) == "local-server";
  }

  std::string GetCredential(PrefService* prefs, const os_crypt_async::Encryptor*) const override {
    return GetConfiguredApiKey(prefs);
  }

  std::optional<std::string> ResolveEndpoint(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.endpoint.has_value()) {
      return request.endpoint;
    }
    const std::string endpoint = prefs->GetString(kMahoAiEndpointPref);
    if (!endpoint.empty()) {
      return endpoint;
    }
    if (prefs->GetString(kMahoAiProviderPref) == "local-server") {
      return std::string(kDefaultLocalServerEndpoint);
    }
    return std::nullopt;
  }

  std::optional<std::string> ResolveModel(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.model.has_value()) {
      return request.model;
    }
    const std::string model = prefs->GetString(kMahoAiModelPref);
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
        // Fail closed on a malformed user-configured endpoint: falling back to
        // kOpenAIEndpoint would ship the user's prompts and BYOK key to OpenAI
        // without telling them. The caller rejects an invalid URL.
        .url = endpoint_url,
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

class MahoManagedProviderTransport final
    : public MahoAiLlmClient::ProviderTransport {
 public:
  bool HasConfiguredCredential(PrefService* prefs) const override {
    return maho::auth::HasValidRelaySession(prefs);
  }

  std::string GetCredential(PrefService* prefs, const os_crypt_async::Encryptor* encryptor) const override {
    if (encryptor) {
      return maho::auth::GetRelayAccessToken(prefs, *encryptor);
    }
    return "";
  }

  std::optional<std::string> ResolveEndpoint(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.endpoint.has_value()) {
      return request.endpoint;
    }
    char* p = maho_core_managed_proxy_url();
    if (!p) {
      return std::nullopt;
    }
    std::string proxy_url(p);
    maho_core_free_string(p);
    return proxy_url;
  }

  std::optional<std::string> ResolveModel(
      const MahoAiLlmClient::CompletionRequest& request,
      PrefService* prefs) const override {
    if (request.model.has_value()) {
      return request.model;
    }
    const std::string model = prefs->GetString(kMahoAiModelPref);
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

    GURL endpoint_url(endpoint);
    AppendChatCompletionsPath(&endpoint_url);

    return MahoAiLlmClient::RequestPlan{
        .url = endpoint_url,
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

MahoAiLlmClient::PendingRequest::PendingRequest() = default;
MahoAiLlmClient::PendingRequest::PendingRequest(CompletionRequest r, TokenCallback t, CompleteCallback c, ErrorCallback e)
    : request(std::move(r)), on_token(std::move(t)), on_complete(std::move(c)), on_error(std::move(e)) {}
MahoAiLlmClient::PendingRequest::PendingRequest(PendingRequest&&) = default;
MahoAiLlmClient::PendingRequest& MahoAiLlmClient::PendingRequest::operator=(PendingRequest&&) = default;
MahoAiLlmClient::PendingRequest::~PendingRequest() = default;

MahoAiLlmClient::MahoAiLlmClient(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    const os_crypt_async::Encryptor* encryptor,
    bool provider_adapter_v2_enabled,
    base::RepeatingCallback<bool()> ai_gate)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      provider_adapter_v2_enabled_(provider_adapter_v2_enabled),
      encryptor_(encryptor),
      ai_gate_(std::move(ai_gate)) {
  const std::string provider = prefs_->GetString(kMahoAiProviderPref);
  if (provider == "maho-managed") {
    provider_transport_ = std::make_unique<MahoManagedProviderTransport>();
  } else if (provider_adapter_v2_enabled_) {
    provider_transport_ = std::make_unique<ProviderAdapterV2Transport>();
  } else {
    provider_transport_ = std::make_unique<OpenAICompatibleProviderTransport>();
  }

  if (!encryptor_) {
    if (g_browser_process && g_browser_process->os_crypt_async()) {
      g_browser_process->os_crypt_async()->GetInstance(
          base::BindOnce(&MahoAiLlmClient::OnOsCryptReady,
                         oscrypt_weak_factory_.GetWeakPtr()));
    }
  }
}

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

bool MahoAiLlmClient::HasConfiguredCredential(
    const std::optional<std::string>& provider_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string target_provider = provider_id.value_or(
      prefs_->GetString(kMahoAiProviderPref));
  auto transport = CreateTransportForProvider(target_provider);
  return transport ? transport->HasConfiguredCredential(prefs_)
                   : (provider_transport_ && provider_transport_->HasConfiguredCredential(prefs_));
}

std::unique_ptr<MahoAiLlmClient::ProviderTransport>
MahoAiLlmClient::CreateTransportForProvider(
    const std::string& provider_id) const {
  if (provider_id == "maho-managed") {
    return std::make_unique<MahoManagedProviderTransport>();
  }
  if (provider_id == "anthropic") {
    return std::make_unique<AnthropicProviderTransport>();
  }
  if (provider_id == "openai" || provider_id == "openai-compatible" ||
      provider_id == "local-server") {
    return std::make_unique<OpenAICompatibleProviderTransport>(provider_id);
  }
  if (provider_adapter_v2_enabled_) {
    return std::make_unique<ProviderAdapterV2Transport>();
  }
  return std::make_unique<OpenAICompatibleProviderTransport>();
}

void MahoAiLlmClient::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  stream_consumer_.reset();
  url_loader_.reset();
  active_transport_.reset();
  sse_buffer_.clear();
  completion_result_ = CompletionResult();
  on_token_.Reset();
  on_complete_.Reset();
  on_error_.Reset();
  is_retry_ = false;
  pending_request_.reset();
}

void MahoAiLlmClient::StartCompletion(CompletionRequest request,
                                      TokenCallback on_token,
                                      CompleteCallback on_complete,
                                      ErrorCallback on_error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  Reset();

  active_request_.provider_id = request.provider_id;
  active_request_.endpoint = request.endpoint;
  active_request_.model = request.model;
  active_request_.prompt_messages = request.prompt_messages.Clone();

  on_token_ = std::move(on_token);
  on_complete_ = std::move(on_complete);
  on_error_ = std::move(on_error);

  if (!ai_gate_ || !ai_gate_.Run()) {
    FinishWithError("AI is not available in this context");
    return;
  }

  std::string provider_id = request.provider_id.value_or(
      prefs_->GetString(kMahoAiProviderPref));
  active_transport_ = CreateTransportForProvider(provider_id);
  if (!active_transport_) {
    active_transport_ = CreateTransportForProvider("");
  }

  if (active_transport_ &&
      active_transport_->HasConfiguredCredential(prefs_) &&
      !GetEncryptor()) {
    pending_request_ = PendingRequest{
        std::move(request), on_token_,
        std::move(on_complete_), std::move(on_error_)};
    return;
  }

  PerformStartCompletion(std::move(request));
}

void MahoAiLlmClient::PerformStartCompletion(CompletionRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!ai_gate_ || !ai_gate_.Run()) {
    FinishWithError("AI is not available in this context");
    return;
  }

  if (!active_transport_) {
    std::string provider_id = request.provider_id.value_or(
        prefs_->GetString(kMahoAiProviderPref));
    active_transport_ = CreateTransportForProvider(provider_id);
  }
  ProviderTransport* transport = active_transport_ ? active_transport_.get()
                                                   : provider_transport_.get();
  if (!transport) {
    FinishWithError("No AI provider transport available");
    return;
  }

  const std::optional<std::string> endpoint =
      transport->ResolveEndpoint(request, prefs_);
  const std::optional<std::string> model =
      transport->ResolveModel(request, prefs_);
  if (!endpoint.has_value() || !model.has_value()) {
    FinishWithError(
        "No AI model is configured. Check your Local Server or AI settings.");
    return;
  }

  const RequestPlan request_plan =
      transport->CreateRequestPlan(std::move(request), *endpoint,
                                   *model);

  // A user-configured endpoint that does not parse must abort the request.
  // Silently substituting a default provider would leak the prompt and the
  // Authorization header to a host the user never chose.
  if (!request_plan.url.is_valid()) {
    FinishWithError(
        "The configured AI endpoint is not a valid URL. Check your Local "
        "Server or AI settings.");
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_plan.url;
  resource_request->method = "POST";
  const std::string api_key = transport->GetCredential(prefs_, GetEncryptor());
  transport->ApplyHeaders(resource_request.get(), api_key);

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
  ProviderTransport* transport = active_transport_ ? active_transport_.get()
                                                   : provider_transport_.get();
  if (transport) {
    transport->ProcessResponseBody(std::move(body_chunk), &sse_buffer_,
                                   &completion_result_, on_token_,
                                   &overflowed);
  }
  if (overflowed) {
    FinishWithError("LLM response exceeded 2 MB cap");
  }
}

void MahoAiLlmClient::OnOsCryptReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  local_encryptor_ = std::move(encryptor);
  if (pending_request_) {
    PendingRequest req = std::move(*pending_request_);
    pending_request_.reset();
    StartCompletion(std::move(req.request), std::move(req.on_token),
                    std::move(req.on_complete), std::move(req.on_error));
  }
}

const os_crypt_async::Encryptor* MahoAiLlmClient::GetEncryptor() const {
  if (encryptor_) return encryptor_;
  if (local_encryptor_) return &*local_encryptor_;
  return nullptr;
}

void MahoAiLlmClient::OnResponseComplete(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int response_code = -1;
  if (url_loader_ && url_loader_->ResponseInfo() && url_loader_->ResponseInfo()->headers) {
    response_code = url_loader_->ResponseInfo()->headers->response_code();
  }

  if (response_code == 401 && !is_retry_ && prefs_ && url_loader_factory_) {
    if (!ai_gate_ || !ai_gate_.Run()) {
      FinishWithError("AI is not available in this context");
      return;
    }
    const os_crypt_async::Encryptor* encryptor = GetEncryptor();
    if (!encryptor) {
      FinishWithError("Unauthorized: Decryption helper not ready.");
      return;
    }
    std::string refresh_token = maho::auth::GetRelayRefreshToken(prefs_, *encryptor);
    if (refresh_token.empty()) {
      FinishWithError("Unauthorized: Session expired. Please sign in again.");
      return;
    }
    is_retry_ = true;
    maho::auth::RefreshAccessToken(
        refresh_token, url_loader_factory_,
        base::BindOnce(&MahoAiLlmClient::OnRefreshComplete,
                       weak_factory_.GetWeakPtr()));
    return;
  }

  if (!success && sse_buffer_.empty() && completion_result_.full_text.empty() &&
      completion_result_.tool_calls.empty()) {
    if (response_code == 401) {
      FinishWithError("Unauthorized: Session expired. Please sign in again.");
    } else {
      FinishWithError("No response received from the AI service.");
    }
    return;
  }

  std::string remaining = std::move(sse_buffer_);
  if (!remaining.empty() &&
      !base::EndsWith(remaining, "\n\n", base::CompareCase::SENSITIVE)) {
    remaining += "\n\n";
  }
  bool overflowed = false;
  auto& effective_transport =
      active_transport_ ? active_transport_ : provider_transport_;
  effective_transport->ProcessResponseBody(std::move(remaining), &sse_buffer_,
                                           &completion_result_, on_token_,
                                           &overflowed);
  sse_buffer_.clear();
  if (overflowed) {
    FinishWithError("LLM response exceeded 2 MB cap");
    return;
  }
  FinishWithSuccess();
}

void MahoAiLlmClient::OnRefreshComplete(
    bool success,
    int http_status,
    std::optional<maho::auth::RefreshedTokens> tokens) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!success || !tokens.has_value()) {
    FinishWithError("Unauthorized: Session expired. Please sign in again.");
    return;
  }
  // The relay invalidated the refresh token it just consumed; persist the
  // rotated pair or the retry reuses the stale access token and the profile
  // keeps a dead refresh token that signs the user out on the next refresh.
  const os_crypt_async::Encryptor* encryptor = GetEncryptor();
  if (!encryptor ||
      !maho::auth::StoreRefreshedRelayTokens(prefs_, *encryptor, *tokens)) {
    FinishWithError("Unauthorized: Failed to store refreshed session.");
    return;
  }

  stream_consumer_.reset();
  url_loader_.reset();

  CompletionRequest request;
  request.endpoint = active_request_.endpoint;
  request.model = active_request_.model;
  request.provider_id = active_request_.provider_id;
  request.prompt_messages = active_request_.prompt_messages.Clone();

  PerformStartCompletion(std::move(request));
}

void MahoAiLlmClient::FinishWithSuccess() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (completion_result_.full_text.empty() &&
      completion_result_.tool_calls.empty()) {
    FinishWithError("No response received from the AI service.");
    return;
  }
  stream_consumer_.reset();
  url_loader_.reset();
  if (on_complete_) {
    std::move(on_complete_).Run(std::move(completion_result_));
  }
}

void MahoAiLlmClient::FinishWithError(const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  stream_consumer_.reset();
  url_loader_.reset();
  if (on_error_) {
    std::move(on_error_).Run(error);
  }
}
