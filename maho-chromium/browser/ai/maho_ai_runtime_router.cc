// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_ai_runtime_router.h"

#include <utility>

#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "maho/browser/ai/maho_unified_agent_adapter.h"

namespace maho::ai {

MahoAiModelRoute::MahoAiModelRoute() = default;
MahoAiModelRoute::MahoAiModelRoute(std::string provider_id,
                                   std::string model_id,
                                   std::string endpoint,
                                   bool inherited_from_default)
    : provider_id(std::move(provider_id)),
      model_id(std::move(model_id)),
      endpoint(std::move(endpoint)),
      inherited_from_default(inherited_from_default) {}
MahoAiModelRoute::~MahoAiModelRoute() = default;
MahoAiModelRoute::MahoAiModelRoute(const MahoAiModelRoute&) = default;
MahoAiModelRoute& MahoAiModelRoute::operator=(const MahoAiModelRoute&) = default;
MahoAiModelRoute::MahoAiModelRoute(MahoAiModelRoute&&) noexcept = default;
MahoAiModelRoute& MahoAiModelRoute::operator=(MahoAiModelRoute&&) noexcept = default;

MahoAiRouteResult::MahoAiRouteResult() = default;
MahoAiRouteResult::MahoAiRouteResult(MahoAiRouteError error, MahoAiModelRoute route)
    : error(error), route(std::move(route)) {}
MahoAiRouteResult::~MahoAiRouteResult() = default;
MahoAiRouteResult::MahoAiRouteResult(const MahoAiRouteResult&) = default;
MahoAiRouteResult& MahoAiRouteResult::operator=(const MahoAiRouteResult&) = default;
MahoAiRouteResult::MahoAiRouteResult(MahoAiRouteResult&&) noexcept = default;
MahoAiRouteResult& MahoAiRouteResult::operator=(MahoAiRouteResult&&) noexcept = default;

std::string TaskToString(MahoAiTask task) {
  switch (task) {
    case MahoAiTask::kChat:
      return kTaskChat;
    case MahoAiTask::kTabTidy:
      return kTaskTabTidy;
    case MahoAiTask::kInlineEdit:
      return kTaskInlineEdit;
    case MahoAiTask::kPagePreview:
      return kTaskPagePreview;
    case MahoAiTask::kTabTitle:
      return kTaskTabTitle;
    case MahoAiTask::kDownloadTidy:
      return kTaskDownloadTidy;
    case MahoAiTask::kMemory:
      return kTaskMemory;
  }
}

std::optional<MahoAiTask> StringToTask(const std::string& task_str) {
  if (task_str == kTaskChat) {
    return MahoAiTask::kChat;
  }
  if (task_str == kTaskTabTidy) {
    return MahoAiTask::kTabTidy;
  }
  if (task_str == kTaskInlineEdit) {
    return MahoAiTask::kInlineEdit;
  }
  if (task_str == kTaskPagePreview) {
    return MahoAiTask::kPagePreview;
  }
  if (task_str == kTaskTabTitle) {
    return MahoAiTask::kTabTitle;
  }
  if (task_str == kTaskDownloadTidy) {
    return MahoAiTask::kDownloadTidy;
  }
  if (task_str == kTaskMemory) {
    return MahoAiTask::kMemory;
  }
  return std::nullopt;
}

bool IsValidProviderId(const std::string& provider_id) {
  return provider_id == kProviderMahoManaged ||
         provider_id == kProviderOpenAI ||
         provider_id == kProviderAnthropic ||
         provider_id == kProviderOpenAICompatible ||
         provider_id == kProviderLocalServer;
}

std::string GetProviderBaseUrl(PrefService* prefs, const std::string& provider_id) {
  if (!prefs) {
    return "";
  }
  const base::DictValue& configs = prefs->GetDict(ai_prefs::kProviderConfigs);
  const base::DictValue* entry = configs.FindDict(provider_id);
  if (entry) {
    const std::string* url = entry->FindString("base_url");
    if (url && !url->empty()) {
      return *url;
    }
  }

  // Fallback for legacy transition
  if (provider_id == kProviderOpenAICompatible) {
    if (prefs->GetString(ai_prefs::kProvider) == kProviderOpenAICompatible) {
      return prefs->GetString(ai_prefs::kBaseUrl);
    }
  } else if (provider_id == kProviderLocalServer) {
    if (prefs->GetString(ai_prefs::kProvider) == kProviderLocalServer) {
      return prefs->GetString(ai_prefs::kBaseUrl);
    }
    return "http://localhost:11434";
  }
  return "";
}

void SetProviderBaseUrl(PrefService* prefs,
                        const std::string& provider_id,
                        const std::string& base_url) {
  if (!prefs) {
    return;
  }
  ScopedDictPrefUpdate update(prefs, ai_prefs::kProviderConfigs);
  base::DictValue* entry = update->EnsureDict(provider_id);
  entry->Set("base_url", base_url);

  if (prefs->GetString(ai_prefs::kProvider) == provider_id) {
    prefs->SetString(ai_prefs::kBaseUrl, base_url);
  }
}

std::string GetProviderLastModel(PrefService* prefs, const std::string& provider_id) {
  if (!prefs) {
    return "";
  }
  const base::DictValue& configs = prefs->GetDict(ai_prefs::kProviderConfigs);
  const base::DictValue* entry = configs.FindDict(provider_id);
  if (entry) {
    const std::string* m = entry->FindString("last_model");
    if (!m) {
      m = entry->FindString("last_model_id");
    }
    if (m && !m->empty()) {
      return *m;
    }
  }
  if (prefs->GetString(ai_prefs::kProvider) == provider_id) {
    return prefs->GetString(ai_prefs::kModel);
  }
  return "";
}

void SetProviderLastModel(PrefService* prefs,
                          const std::string& provider_id,
                          const std::string& model_id) {
  if (!prefs) {
    return;
  }
  ScopedDictPrefUpdate update(prefs, ai_prefs::kProviderConfigs);
  base::DictValue* entry = update->EnsureDict(provider_id);
  entry->Set("last_model", model_id);
  entry->Set("last_model_id", model_id);
}

bool IsProviderConnected(PrefService* prefs, const std::string& provider_id) {
  if (!prefs) {
    return false;
  }
  if (provider_id == kProviderMahoManaged) {
    return maho::auth::HasValidRelaySession(prefs);
  }
  if (provider_id == kProviderOpenAI) {
    return !prefs->GetString(ai_prefs::kByokOpenAIEncryptedB64).empty() ||
           !prefs->GetString(ai_prefs::kOAuthOpenAIRefreshEncryptedB64).empty();
  }
  if (provider_id == kProviderAnthropic) {
    return !prefs->GetString(ai_prefs::kByokAnthropicEncryptedB64).empty() ||
           !prefs->GetString(ai_prefs::kOAuthAnthropicRefreshEncryptedB64).empty();
  }
  if (provider_id == kProviderOpenAICompatible) {
    return !GetProviderBaseUrl(prefs, kProviderOpenAICompatible).empty();
  }
  if (provider_id == kProviderLocalServer) {
    return !GetProviderBaseUrl(prefs, kProviderLocalServer).empty();
  }
  return false;
}

std::optional<std::pair<std::string, std::string>> GetTaskOverride(
    PrefService* prefs,
    MahoAiTask task) {
  if (!prefs || task == MahoAiTask::kChat) {
    return std::nullopt;
  }
  const base::DictValue& task_models = prefs->GetDict(ai_prefs::kTaskModels);
  const base::DictValue* entry = task_models.FindDict(TaskToString(task));
  if (!entry) {
    return std::nullopt;
  }
  const std::string* prov = entry->FindString("provider");
  if (!prov) {
    prov = entry->FindString("provider_id");
  }
  const std::string* mod = entry->FindString("model");
  if (!mod) {
    mod = entry->FindString("model_id");
  }
  if (!prov || !mod || prov->empty() || mod->empty()) {
    return std::nullopt;
  }
  return std::make_pair(*prov, *mod);
}

void SetTaskOverride(PrefService* prefs,
                     MahoAiTask task,
                     const std::string& provider_id,
                     const std::string& model_id) {
  if (!prefs || task == MahoAiTask::kChat) {
    return;
  }
  ScopedDictPrefUpdate update(prefs, ai_prefs::kTaskModels);
  base::DictValue entry;
  entry.Set("provider", provider_id);
  entry.Set("provider_id", provider_id);
  entry.Set("model", model_id);
  entry.Set("model_id", model_id);
  update->Set(TaskToString(task), std::move(entry));
}

void ClearTaskOverride(PrefService* prefs, MahoAiTask task) {
  if (!prefs || task == MahoAiTask::kChat) {
    return;
  }
  ScopedDictPrefUpdate update(prefs, ai_prefs::kTaskModels);
  update->Remove(TaskToString(task));
}

std::string ResolveProviderEndpoint(PrefService* prefs,
                                    const std::string& provider_id) {
  if (provider_id == kProviderMahoManaged) {
    char* p = maho_core_managed_proxy_url();
    std::string url(p ? p : "https://proxy.maho.co/v1");
    if (p) {
      maho_core_free_string(p);
    }
    if (!url.empty() && url.back() == '/') {
      url.pop_back();
    }
    return url;
  }
  if (provider_id == kProviderOpenAI) {
    return "https://api.openai.com/v1";
  }
  if (provider_id == kProviderAnthropic) {
    return "https://api.anthropic.com/v1";
  }
  if (provider_id == kProviderOpenAICompatible) {
    std::string url = GetProviderBaseUrl(prefs, kProviderOpenAICompatible);
    if (!url.empty() && url.back() == '/') {
      url.pop_back();
    }
    return url;
  }
  if (provider_id == kProviderLocalServer) {
    std::string url = GetProviderBaseUrl(prefs, kProviderLocalServer);
    if (url.empty()) {
      url = "http://localhost:11434";
    }
    if (!url.empty() && url.back() == '/') {
      url.pop_back();
    }
    return url;
  }
  return "";
}

MahoAiRouteResult ResolveMahoAiModelRoute(PrefService* prefs, MahoAiTask task) {
  if (!prefs) {
    return {MahoAiRouteError::kNoDefault, {}};
  }

  // 1. If override-capable task and an explicit override is configured:
  if (task != MahoAiTask::kChat) {
    auto override_opt = GetTaskOverride(prefs, task);
    if (override_opt) {
      const std::string& prov = override_opt->first;
      const std::string& mod = override_opt->second;

      if (!IsValidProviderId(prov)) {
        return {MahoAiRouteError::kUnknownProvider, {}};
      }
      if (!IsProviderConnected(prefs, prov)) {
        return {MahoAiRouteError::kProviderDisconnected, {}};
      }
      if (mod.empty()) {
        return {MahoAiRouteError::kModelMissing, {}};
      }
      std::string ep = ResolveProviderEndpoint(prefs, prov);
      return {MahoAiRouteError::kOk, {prov, mod, ep, /*inherited_from_default=*/false}};
    }
  }

  // 2. Resolve Default route:
  std::string default_provider = prefs->GetString(ai_prefs::kProvider);
  std::string default_model = prefs->GetString(ai_prefs::kModel);

  if (default_provider.empty()) {
    if (maho::auth::HasValidRelaySession(prefs)) {
      std::string mod = default_model.empty() ? "google/gemini-3-flash-lite:free" : default_model;
      std::string ep = ResolveProviderEndpoint(prefs, kProviderMahoManaged);
      return {MahoAiRouteError::kOk,
              {kProviderMahoManaged, mod, ep, /*inherited_from_default=*/true}};
    }
    return {MahoAiRouteError::kNoDefault, {}};
  }

  if (!IsValidProviderId(default_provider)) {
    return {MahoAiRouteError::kUnknownProvider, {}};
  }

  if (!IsProviderConnected(prefs, default_provider)) {
    return {MahoAiRouteError::kProviderDisconnected, {}};
  }

  if (default_model.empty()) {
    return {MahoAiRouteError::kModelMissing, {}};
  }

  std::string ep = ResolveProviderEndpoint(prefs, default_provider);
  bool inherited = (task != MahoAiTask::kChat);
  return {MahoAiRouteError::kOk, {default_provider, default_model, ep, inherited}};
}

namespace {

std::string DecryptKeyHelper(const std::string& encrypted_b64,
                             const os_crypt_async::Encryptor* encryptor) {
  if (encrypted_b64.empty() || !encryptor) {
    return "";
  }
  std::string encrypted_bytes;
  if (!base::Base64Decode(encrypted_b64, &encrypted_bytes)) {
    return "";
  }
  std::string plaintext;
  if (encryptor->DecryptString(encrypted_bytes, &plaintext)) {
    return plaintext;
  }
  return "";
}

}  // namespace

std::string DecryptProviderKey(PrefService* prefs,
                               const std::string& provider_id,
                               const os_crypt_async::Encryptor* encryptor) {
  if (!prefs) {
    return "";
  }
  if (provider_id == kProviderOpenAI) {
    std::string b64 = prefs->GetString(ai_prefs::kByokOpenAIEncryptedB64);
    return DecryptKeyHelper(b64, encryptor);
  }
  if (provider_id == kProviderAnthropic) {
    std::string b64 = prefs->GetString(ai_prefs::kByokAnthropicEncryptedB64);
    return DecryptKeyHelper(b64, encryptor);
  }
  if (provider_id == kProviderOpenAICompatible) {
    std::string b64 =
        prefs->GetString(ai_prefs::kByokOpenAICompatibleEncryptedB64);
    std::string decrypted = DecryptKeyHelper(b64, encryptor);
    if (!decrypted.empty()) {
      return decrypted;
    }
    return prefs->GetString(ai_prefs::kApiKey);
  }
  if (provider_id == kProviderMahoManaged) {
    if (encryptor) {
      return maho::auth::GetRelayAccessToken(prefs, *encryptor);
    }
    return "";
  }
  return "";
}

void SyncMemoryAuthFromPrefs(PrefService* prefs,
                             const os_crypt_async::Encryptor* encryptor) {
  MahoCore* core = maho::GetCore();
  if (!core || !prefs) {
    return;
  }

  MahoAiRouteResult route_res =
      ResolveMahoAiModelRoute(prefs, MahoAiTask::kMemory);
  if (!route_res.is_ok()) {
    base::DictValue event;
    event.Set("kind", "set_memory_auth");
    event.Set("auth", base::Value());
    std::string json;
    base::JSONWriter::Write(event, &json);
    maho::core::HandleEvent(core, json.c_str());
    return;
  }

  std::string api_key =
      DecryptProviderKey(prefs, route_res.route.provider_id, encryptor);

  base::DictValue event;
  event.Set("kind", "set_memory_auth");
  base::DictValue auth_dict;
  auth_dict.Set("provider", route_res.route.provider_id);
  auth_dict.Set("base_url", route_res.route.endpoint);
  auth_dict.Set("api_key", api_key);
  auth_dict.Set("model", route_res.route.model_id);
  event.Set("auth", std::move(auth_dict));

  std::string json;
  base::JSONWriter::Write(event, &json);
  maho::core::HandleEvent(core, json.c_str());
}

}  // namespace maho::ai

MahoAiRuntimeRouter::MahoAiRuntimeRouter(
    PrefService* prefs,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    base::RepeatingCallback<bool()> ai_gate,
    base::RepeatingCallback<Browser*()> bound_browser_resolver)
    : prefs_(prefs),
      url_loader_factory_(std::move(url_loader_factory)),
      ai_gate_(std::move(ai_gate)),
      bound_browser_resolver_(std::move(bound_browser_resolver)) {
  RebuildAdapterIfStale();
}

MahoAiRuntimeRouter::~MahoAiRuntimeRouter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

MahoAiRuntimeAdapter* MahoAiRuntimeRouter::GetActiveAdapter() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RebuildAdapterIfStale();
  return active_adapter_.get();
}

std::string MahoAiRuntimeRouter::GetActiveAdapterName() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_adapter_) {
    return "disabled";
  }
  return active_adapter_->GetAdapterName();
}

bool MahoAiRuntimeRouter::IsAvailable() const {
  return active_adapter_ && active_adapter_->IsAvailable();
}

void MahoAiRuntimeRouter::RebuildAdapterIfStale() {
  if (!ai_gate_ || !ai_gate_.Run()) {
    active_adapter_.reset();
    return;
  }
  if (active_adapter_) {
    return;
  }
  active_adapter_ = std::make_unique<MahoUnifiedAgentAdapter>(
      prefs_, url_loader_factory_, ai_gate_, bound_browser_resolver_);
}
