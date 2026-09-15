// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_
#define MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "maho/browser/ai/maho_ai_runtime_adapter.h"

class Browser;
class PrefService;

namespace os_crypt_async {
class Encryptor;
}

namespace network {
class SharedURLLoaderFactory;
}

namespace maho::ai {

// Canonical task keys
inline constexpr char kTaskChat[] = "chat";
inline constexpr char kTaskTabTidy[] = "tab_tidy";
inline constexpr char kTaskInlineEdit[] = "inline_edit";
inline constexpr char kTaskPagePreview[] = "page_preview";
inline constexpr char kTaskTabTitle[] = "tab_title";
inline constexpr char kTaskDownloadTidy[] = "download_tidy";
inline constexpr char kTaskMemory[] = "memory";

// Canonical provider IDs
inline constexpr char kProviderMahoManaged[] = "maho-managed";
inline constexpr char kProviderOpenAI[] = "openai";
inline constexpr char kProviderAnthropic[] = "anthropic";
inline constexpr char kProviderOpenAICompatible[] = "openai-compatible";
inline constexpr char kProviderLocalServer[] = "local-server";

enum class MahoAiTask {
  kChat,
  kTabTidy,
  kInlineEdit,
  kPagePreview,
  kTabTitle,
  kDownloadTidy,
  kMemory,
};

struct MahoAiModelRoute {
  MahoAiModelRoute();
  MahoAiModelRoute(std::string provider_id,
                   std::string model_id,
                   std::string endpoint,
                   bool inherited_from_default = false);
  ~MahoAiModelRoute();
  MahoAiModelRoute(const MahoAiModelRoute&);
  MahoAiModelRoute& operator=(const MahoAiModelRoute&);
  MahoAiModelRoute(MahoAiModelRoute&&) noexcept;
  MahoAiModelRoute& operator=(MahoAiModelRoute&&) noexcept;

  std::string provider_id;
  std::string model_id;
  std::string endpoint;          // resolved, never persisted in task map
  bool inherited_from_default = false;
};

enum class MahoAiRouteError {
  kOk,
  kNoDefault,
  kProviderDisconnected,
  kModelMissing,
  kVisionUnsupported,
  kUnknownProvider,
};

struct MahoAiRouteResult {
  MahoAiRouteResult();
  MahoAiRouteResult(MahoAiRouteError error, MahoAiModelRoute route);
  ~MahoAiRouteResult();
  MahoAiRouteResult(const MahoAiRouteResult&);
  MahoAiRouteResult& operator=(const MahoAiRouteResult&);
  MahoAiRouteResult(MahoAiRouteResult&&) noexcept;
  MahoAiRouteResult& operator=(MahoAiRouteResult&&) noexcept;

  MahoAiRouteError error = MahoAiRouteError::kOk;
  MahoAiModelRoute route;

  bool is_ok() const { return error == MahoAiRouteError::kOk; }
};

std::string TaskToString(MahoAiTask task);
std::optional<MahoAiTask> StringToTask(const std::string& task_str);

bool IsValidProviderId(const std::string& provider_id);
bool IsProviderConnected(PrefService* prefs, const std::string& provider_id);

std::string GetProviderBaseUrl(PrefService* prefs, const std::string& provider_id);
void SetProviderBaseUrl(PrefService* prefs,
                        const std::string& provider_id,
                        const std::string& base_url);

std::string GetProviderLastModel(PrefService* prefs, const std::string& provider_id);
void SetProviderLastModel(PrefService* prefs,
                          const std::string& provider_id,
                          const std::string& model_id);

std::optional<std::pair<std::string, std::string>> GetTaskOverride(
    PrefService* prefs,
    MahoAiTask task);

void SetTaskOverride(PrefService* prefs,
                     MahoAiTask task,
                     const std::string& provider_id,
                     const std::string& model_id);

void ClearTaskOverride(PrefService* prefs, MahoAiTask task);

std::string ResolveProviderEndpoint(PrefService* prefs,
                                    const std::string& provider_id);

MahoAiRouteResult ResolveMahoAiModelRoute(PrefService* prefs, MahoAiTask task);

std::string DecryptProviderKey(
    PrefService* prefs,
    const std::string& provider_id,
    const os_crypt_async::Encryptor* encryptor);

void SyncMemoryAuthFromPrefs(
    PrefService* prefs,
    const os_crypt_async::Encryptor* encryptor);

}  // namespace maho::ai

// Owns the single ACP runtime adapter. Reads the `maho.ai.runtime_command`
// pref for the binary path. The adapter is constructed lazily on the first
// call to GetActiveAdapter().
class MahoAiRuntimeRouter {
 public:
  MahoAiRuntimeRouter(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      base::RepeatingCallback<bool()> ai_gate = base::RepeatingCallback<bool()>(),
      base::RepeatingCallback<Browser*()> bound_browser_resolver =
          base::RepeatingCallback<Browser*()>());
  ~MahoAiRuntimeRouter();

  MahoAiRuntimeRouter(const MahoAiRuntimeRouter&) = delete;
  MahoAiRuntimeRouter& operator=(const MahoAiRuntimeRouter&) = delete;

  MahoAiRuntimeAdapter* GetActiveAdapter();
  std::string GetActiveAdapterName() const;
  bool IsAvailable() const;

 private:
  void RebuildAdapterIfStale();

  SEQUENCE_CHECKER(sequence_checker_);
  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  base::RepeatingCallback<bool()> ai_gate_;
  base::RepeatingCallback<Browser*()> bound_browser_resolver_;

  std::unique_ptr<MahoAiRuntimeAdapter> active_adapter_;
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_RUNTIME_ROUTER_H_
