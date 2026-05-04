// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_llm_client.h"

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

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

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

bool IsLocalServerProvider(const std::string& provider) {
  return provider == kMahoAiLocalServerProvider ||
         provider == kMahoAiLegacyCustomProvider;
}

}  // namespace

MahoAiLlmClient::MahoAiLlmClient(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : prefs_(prefs), url_loader_factory_(std::move(url_loader_factory)) {}

MahoAiLlmClient::~MahoAiLlmClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MahoAiLlmClient::CompletionRequest::CompletionRequest() = default;
MahoAiLlmClient::CompletionRequest::~CompletionRequest() = default;
MahoAiLlmClient::CompletionRequest::CompletionRequest(CompletionRequest&&) = default;
MahoAiLlmClient::CompletionRequest& MahoAiLlmClient::CompletionRequest::operator=(CompletionRequest&&) = default;

bool MahoAiLlmClient::HasConfiguredCredential() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !GetConfiguredApiKey().empty() || GetLocalServerConfig().has_value();
}

void MahoAiLlmClient::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  url_loader_.reset();
  sse_buffer_.clear();
  accumulated_response_.clear();
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

  std::optional<ResolvedConfig> config = ResolveConfig(request);
  if (!config.has_value()) {
    FinishWithError(
        "No AI model is configured. Check your Local Server or AI settings.");
    return;
  }

  base::DictValue request_body;
  request_body.Set("model", config->model);
  request_body.Set("stream", true);
  request_body.Set("messages", std::move(request.prompt_messages));

  std::string body_json;
  base::JSONWriter::Write(request_body, &body_json);

  GURL endpoint_url(config->endpoint);
  if (endpoint_url.is_valid() && !config->endpoint.empty()) {
    std::string request_url = endpoint_url.spec();
    if (!base::EndsWith(request_url, "/", base::CompareCase::SENSITIVE)) {
      request_url.push_back('/');
    }
    request_url.append("chat/completions");
    endpoint_url = GURL(request_url);
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url =
      endpoint_url.is_valid() ? endpoint_url : GURL(kOpenAIEndpoint);
  resource_request->method = "POST";
  if (!config->api_key.empty()) {
    resource_request->headers.SetHeader("Authorization",
                                        "Bearer " + config->api_key);
  }
  resource_request->headers.SetHeader("Content-Type", "application/json");

  url_loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kTrafficAnnotation);
  url_loader_->AttachStringForUpload(body_json, "application/json");
  url_loader_->SetAllowHttpErrorResults(true);
  url_loader_->DownloadToString(
      url_loader_factory_.get(),
      base::BindOnce(&MahoAiLlmClient::OnResponseDownloaded,
                     weak_factory_.GetWeakPtr()),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void MahoAiLlmClient::OnResponseDownloaded(std::optional<std::string> body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!body.has_value()) {
    FinishWithError("No response received from the AI service.");
    return;
  }

  sse_buffer_ = std::move(*body);
  std::string normalized_body;
  normalized_body.reserve(sse_buffer_.size());
  for (char ch : sse_buffer_) {
    if (ch != '\r') {
      normalized_body.push_back(ch);
    }
  }
  sse_buffer_ = std::move(normalized_body);
  if (!sse_buffer_.empty() &&
      !base::EndsWith(sse_buffer_, "\n\n", base::CompareCase::SENSITIVE)) {
    sse_buffer_ += "\n\n";
  }

  ProcessSSEBuffer();
  FinishWithSuccess();
}

void MahoAiLlmClient::ProcessSSEBuffer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_t pos;
  while ((pos = sse_buffer_.find("\n\n")) != std::string::npos) {
    std::string event_block = sse_buffer_.substr(0, pos);
    sse_buffer_.erase(0, pos + 2);

    for (const auto& line :
         base::SplitStringPiece(event_block, "\n", base::KEEP_WHITESPACE,
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

      const base::DictValue* delta = first_choice->FindDict("delta");
      if (!delta) {
        continue;
      }

      const std::string* content = delta->FindString("content");
      if (content && !content->empty()) {
        accumulated_response_ += *content;
        if (on_token_) {
          on_token_.Run(*content);
        }
      }
    }
  }
}

void MahoAiLlmClient::FinishWithSuccess() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string result = accumulated_response_.empty()
                           ? "No response received from the AI service."
                           : accumulated_response_;
  if (on_complete_) {
    std::move(on_complete_).Run(result);
  }
  url_loader_.reset();
}

void MahoAiLlmClient::FinishWithError(const std::string& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (on_error_) {
    std::move(on_error_).Run(error);
  }
  url_loader_.reset();
}

std::optional<MahoAiLlmClient::ResolvedConfig> MahoAiLlmClient::ResolveConfig(
    const CompletionRequest& request) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::optional<LocalServerConfig> local_server_config =
      GetLocalServerConfig();
  const std::string endpoint_pref = prefs_->GetString(kMahoAiEndpointPref);
  const std::string model_pref = prefs_->GetString(kMahoAiModelPref);
  const std::string endpoint = request.endpoint.value_or(
      local_server_config.has_value() ? local_server_config->endpoint
                                      : endpoint_pref);
  const std::string model = request.model.value_or(
      local_server_config.has_value() ? local_server_config->model : model_pref);

  if (model.empty()) {
    return std::nullopt;
  }

  ResolvedConfig config;
  config.endpoint = endpoint.empty() ? kOpenAIEndpoint : endpoint;
  config.model = model.empty() ? kDefaultModel : model;
  config.api_key = GetConfiguredApiKey();
  return config;
}

std::string MahoAiLlmClient::GetConfiguredApiKey() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::string api_key = prefs_->GetString(kMahoAiApiKeyPref);
  if (!api_key.empty()) {
    return api_key;
  }
  return GetMacOSAppDefaultsString(kMahoAiApiKeyPref);
}

std::optional<MahoAiLlmClient::LocalServerConfig>
MahoAiLlmClient::GetLocalServerConfig() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

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

// static
std::string MahoAiLlmClient::GetMacOSAppDefaultsString(const char* key) {
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
