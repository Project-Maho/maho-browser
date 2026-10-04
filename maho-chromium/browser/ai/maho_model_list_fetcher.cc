// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_model_list_fetcher.h"

#include <utility>

#include "base/base64.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "maho/browser/ai/maho_ai_runtime_router.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace maho::ai {

MahoModelListFetcher::PendingFetch::PendingFetch() = default;
MahoModelListFetcher::PendingFetch::PendingFetch(std::string pid, ModelsCallback cb)
    : provider_id(std::move(pid)), callback(std::move(cb)) {}
MahoModelListFetcher::PendingFetch::PendingFetch(PendingFetch&&) = default;
MahoModelListFetcher::PendingFetch& MahoModelListFetcher::PendingFetch::operator=(PendingFetch&&) = default;
MahoModelListFetcher::PendingFetch::~PendingFetch() = default;

MahoModelListFetcher::MahoModelListFetcher(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    base::RepeatingCallback<bool()> ai_gate)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      ai_gate_(std::move(ai_gate)) {
  if (g_browser_process && g_browser_process->os_crypt_async()) {
    g_browser_process->os_crypt_async()->GetInstance(
        base::BindOnce(&MahoModelListFetcher::OnOsCryptReady,
                       weak_factory_.GetWeakPtr()));
  }
}

MahoModelListFetcher::~MahoModelListFetcher() = default;

void MahoModelListFetcher::FetchModels(const std::string& provider_id,
                                       ModelsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ai_gate_ || !ai_gate_.Run()) {
    std::move(callback).Run({}, false);
    return;
  }
  base::Time fetched_at;
  auto cached_models = ReadCache(provider_id, &fetched_at);
  if (cached_models) {
    if (base::Time::Now() - fetched_at < base::Hours(1)) {
      std::move(callback).Run(*cached_models, true);
      return;
    }
    // Stale cache: return stale cache but refresh in background
    std::move(callback).Run(*cached_models, true);
    PerformFetch(provider_id, base::NullCallback());
    return;
  }

  PerformFetch(provider_id, std::move(callback));
}

void MahoModelListFetcher::RefreshModels(const std::string& provider_id,
                                         ModelsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ai_gate_ || !ai_gate_.Run()) {
    std::move(callback).Run({}, false);
    return;
  }
  PerformFetch(provider_id, std::move(callback));
}

void MahoModelListFetcher::SetRefreshedCallback(RefreshedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  refreshed_callback_ = std::move(callback);
}

void MahoModelListFetcher::PerformFetch(const std::string& provider_id,
                                        ModelsCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!url_loader_factory_) {
    std::move(callback).Run(GetHardcodedFallback(provider_id), false);
    return;
  }

  const bool needs_encryptor = (provider_id == "maho-managed" ||
                                provider_id == "openai" ||
                                provider_id == "anthropic");
  if (needs_encryptor && !local_encryptor_) {
    pending_fetches_.push_back(PendingFetch(provider_id, std::move(callback)));
    return;
  }

  std::string url;
  std::string auth_header_name;
  std::string auth_header_value;
  std::string anthropic_version;

  if (provider_id == "openai") {
    url = "https://api.openai.com/v1/models";
    std::string encrypted = prefs_->GetString(maho::ai_prefs::kByokOpenAIEncryptedB64);
    std::string key = DecryptKey(encrypted);
    if (key.empty()) {
      std::move(callback).Run(GetHardcodedFallback(provider_id), false);
      return;
    }
    auth_header_name = "Authorization";
    auth_header_value = "Bearer " + key;
  } else if (provider_id == "anthropic") {
    url = "https://api.anthropic.com/v1/models";
    std::string encrypted = prefs_->GetString(maho::ai_prefs::kByokAnthropicEncryptedB64);
    std::string key = DecryptKey(encrypted);
    if (key.empty()) {
      std::move(callback).Run(GetHardcodedFallback(provider_id), false);
      return;
    }
    auth_header_name = "x-api-key";
    auth_header_value = key;
    anthropic_version = "2025-06-01";
  } else if (provider_id == "openai-compatible") {
    std::string base_url = maho::ai::GetProviderBaseUrl(prefs_, "openai-compatible");
    if (base_url.empty()) {
      std::move(callback).Run(GetHardcodedFallback(provider_id), false);
      return;
    }
    if (base_url.back() == '/') {
      base_url.pop_back();
    }
    if (base_url.find("/v1") == std::string::npos) {
      url = base_url + "/v1/models";
    } else {
      url = base_url + "/models";
    }
    std::string encrypted = prefs_->GetString(maho::ai_prefs::kByokOpenAICompatibleEncryptedB64);
    std::string key = DecryptKey(encrypted);
    if (key.empty()) {
      key = prefs_->GetString(maho::ai_prefs::kApiKey);
    }
    if (!key.empty()) {
      auth_header_name = "Authorization";
      auth_header_value = "Bearer " + key;
    }
  } else if (provider_id == "local-server") {
    std::string base_url = maho::ai::GetProviderBaseUrl(prefs_, "local-server");
    if (base_url.empty()) {
      base_url = "http://localhost:11434";
    }
    if (base_url.back() == '/') {
      base_url.pop_back();
    }
    url = base_url + "/api/tags";
  } else if (provider_id == "maho-managed") {
    char* p = maho_core_managed_proxy_url();
    std::string proxy_url(p ? p : "https://proxy.maho.co/v1");
    if (p) maho_core_free_string(p);
    if (proxy_url.back() == '/') {
      proxy_url.pop_back();
    }
    url = proxy_url + "/models";

    std::string token = maho::auth::GetRelayAccessToken(prefs_, *local_encryptor_);
    if (token.empty()) {
      std::move(callback).Run(GetHardcodedFallback(provider_id), false);
      return;
    }
    auth_header_name = "Authorization";
    auth_header_value = "Bearer " + token;
  } else {
    std::move(callback).Run(GetHardcodedFallback(provider_id), false);
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = GURL(url);
  resource_request->method = "GET";
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  if (!auth_header_name.empty()) {
    resource_request->headers.SetHeader(auth_header_name, auth_header_value);
  }
  if (!anthropic_version.empty()) {
    resource_request->headers.SetHeader("anthropic-version", anthropic_version);
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("maho_model_list_fetcher", R"(
        semantics {
          sender: "Maho Model List Fetcher"
          description: "Fetches list of available models for a given AI provider."
          trigger: "User opens AI settings page or selects a provider."
          data: "Provider-specific API key or OAuth credentials."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "This feature cannot be disabled."
        }
      )");

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request), traffic_annotation);
  loader->SetTimeoutDuration(base::Seconds(10));

  auto* loader_ptr = loader.get();
  loader_ptr->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&MahoModelListFetcher::OnFetchComplete,
                     weak_factory_.GetWeakPtr(), provider_id,
                     std::move(callback), std::move(loader)));
}

void MahoModelListFetcher::OnFetchComplete(
    const std::string& provider_id,
    ModelsCallback callback,
    std::unique_ptr<network::SimpleURLLoader> loader,
    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ai_gate_ || !ai_gate_.Run()) {
    if (callback) {
      std::move(callback).Run({}, false);
    }
    return;
  }
  std::vector<std::string> models;
  bool success = false;

  int response_code = -1;
  if (loader->ResponseInfo() && loader->ResponseInfo()->headers) {
    response_code = loader->ResponseInfo()->headers->response_code();
  }

  if (response_body && response_code == 200) {
    if (provider_id == "local-server") {
      models = ParseOllamaTags(*response_body);
    } else {
      models = ParseOpenAICompatibleModels(*response_body);
    }
    success = true;
  }

  if (success && !models.empty()) {
    WriteCache(provider_id, models);
    if (refreshed_callback_) {
      refreshed_callback_.Run(provider_id, models);
    }
    if (callback) {
      std::move(callback).Run(models, false);
    }
  } else {
    std::vector<std::string> fallback = GetHardcodedFallback(provider_id);
    if (callback) {
      std::move(callback).Run(fallback, false);
    }
  }
}

std::optional<std::vector<std::string>> MahoModelListFetcher::ReadCache(
    const std::string& provider_id,
    base::Time* fetched_at) {
  if (!prefs_) return std::nullopt;
  const base::DictValue& cache = prefs_->GetDict(maho::ai_prefs::kModelCache);
  const base::DictValue* provider_entry = cache.FindDict(provider_id);
  if (!provider_entry) return std::nullopt;

  if (provider_id == "openai-compatible" || provider_id == "local-server") {
    const base::DictValue* src = provider_entry->FindDict("source");
    const std::string* cached_url = src ? src->FindString("base_url") : nullptr;
    std::string current_url = maho::ai::GetProviderBaseUrl(prefs_, provider_id);
    if (!current_url.empty() && current_url.back() == '/') {
      current_url.pop_back();
    }
    std::string norm_cached = (cached_url ? *cached_url : "");
    if (!norm_cached.empty() && norm_cached.back() == '/') {
      norm_cached.pop_back();
    }
    if (norm_cached != current_url) {
      return std::nullopt;
    }
  }

  const base::ListValue* models_list = provider_entry->FindList("models");
  std::optional<double> fetched_at_val = provider_entry->FindDouble("fetched_at");
  if (!models_list || !fetched_at_val) return std::nullopt;

  *fetched_at = base::Time::FromSecondsSinceUnixEpoch(*fetched_at_val);
  std::vector<std::string> models;
  for (const auto& item : *models_list) {
    if (item.is_string()) {
      models.push_back(item.GetString());
    }
  }
  return models;
}

void MahoModelListFetcher::WriteCache(const std::string& provider_id,
                                      const std::vector<std::string>& models) {
  if (!prefs_) return;
  ScopedDictPrefUpdate update(prefs_, maho::ai_prefs::kModelCache);
  base::DictValue provider_entry;
  base::ListValue models_list;
  for (const auto& m : models) {
    models_list.Append(m);
  }
  provider_entry.Set("models", std::move(models_list));
  provider_entry.Set("fetched_at", base::Time::Now().InSecondsFSinceUnixEpoch());
  if (provider_id == "openai-compatible" || provider_id == "local-server") {
    base::DictValue source_dict;
    std::string current_url = maho::ai::GetProviderBaseUrl(prefs_, provider_id);
    if (!current_url.empty() && current_url.back() == '/') {
      current_url.pop_back();
    }
    source_dict.Set("base_url", current_url);
    provider_entry.Set("source", std::move(source_dict));
  }
  provider_entry.Set("metadata", base::DictValue());
  update->Set(provider_id, std::move(provider_entry));
}

void MahoModelListFetcher::InvalidateCache(const std::string& provider_id) {
  if (!prefs_) return;
  ScopedDictPrefUpdate update(prefs_, maho::ai_prefs::kModelCache);
  update->Remove(provider_id);
}

std::vector<std::string> MahoModelListFetcher::GetHardcodedFallback(
    const std::string& provider_id) {
  if (provider_id == "openai") {
    return {"gpt-4o", "gpt-4o-mini", "gpt-4-turbo", "gpt-3.5-turbo"};
  } else if (provider_id == "anthropic") {
    return {"claude-sonnet-4-20250514", "claude-opus-4-20250514",
            "claude-3-5-haiku-20241022"};
  } else if (provider_id == "maho-managed") {
    return {"google/gemini-3-flash-lite:free"};
  }
  return {};
}

void MahoModelListFetcher::OnOsCryptReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  local_encryptor_ = std::move(encryptor);
  for (auto& pf : pending_fetches_) {
    PerformFetch(pf.provider_id, std::move(pf.callback));
  }
  pending_fetches_.clear();
}

std::string MahoModelListFetcher::DecryptKey(const std::string& encrypted_b64) {
  if (encrypted_b64.empty() || !local_encryptor_) return "";
  std::string encrypted_bytes;
  if (!base::Base64Decode(encrypted_b64, &encrypted_bytes)) {
    return "";
  }
  std::string plaintext;
  if (local_encryptor_->DecryptString(encrypted_bytes, &plaintext)) {
    return plaintext;
  }
  return "";
}

std::vector<std::string> MahoModelListFetcher::ParseOpenAICompatibleModels(
    const std::string& response_body) {
  std::vector<std::string> models;
  std::optional<base::Value> val = base::JSONReader::Read(response_body, base::JSON_PARSE_RFC);
  if (val && val->is_dict()) {
    const base::ListValue* data = val->GetDict().FindList("data");
    if (data) {
      for (const auto& item : *data) {
        if (item.is_dict()) {
          const std::string* id = item.GetDict().FindString("id");
          if (id) {
            models.push_back(*id);
          }
        }
      }
    }
  }
  return models;
}

std::vector<std::string> MahoModelListFetcher::ParseOllamaTags(
    const std::string& response_body) {
  std::vector<std::string> models;
  std::optional<base::Value> val = base::JSONReader::Read(response_body, base::JSON_PARSE_RFC);
  if (val && val->is_dict()) {
    const base::ListValue* list = val->GetDict().FindList("models");
    if (list) {
      for (const auto& item : *list) {
        if (item.is_dict()) {
          const std::string* name = item.GetDict().FindString("name");
          if (name) {
            models.push_back(*name);
          }
        }
      }
    }
  }
  return models;
}

}  // namespace maho::ai
