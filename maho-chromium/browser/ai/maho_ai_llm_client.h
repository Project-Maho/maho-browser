// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_LLM_CLIENT_H_
#define MAHO_BROWSER_AI_MAHO_AI_LLM_CLIENT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"

class PrefService;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

class MahoAiLlmClient {
 public:
  struct CompletionRequest {
    CompletionRequest();
    ~CompletionRequest();
    CompletionRequest(CompletionRequest&&);
    CompletionRequest& operator=(CompletionRequest&&);

    std::optional<std::string> endpoint;
    std::optional<std::string> model;
    base::ListValue prompt_messages;
  };

  using TokenCallback = base::RepeatingCallback<void(const std::string&)>;
  using CompleteCallback =
      base::OnceCallback<void(const std::string& full_text)>;
  using ErrorCallback = base::OnceCallback<void(const std::string& error)>;

  MahoAiLlmClient(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  MahoAiLlmClient(const MahoAiLlmClient&) = delete;
  MahoAiLlmClient& operator=(const MahoAiLlmClient&) = delete;
  ~MahoAiLlmClient();

  static void RegisterProfilePrefs(PrefService* prefs) = delete;

  bool HasConfiguredCredential() const;
  void Reset();
  void StartCompletion(CompletionRequest request,
                       TokenCallback on_token,
                       CompleteCallback on_complete,
                       ErrorCallback on_error);

 private:
  struct ResolvedConfig {
    std::string endpoint;
    std::string model;
    std::string api_key;
  };

  struct LocalServerConfig {
    std::string endpoint;
    std::string model;
  };

  void OnResponseDownloaded(std::optional<std::string> body);
  void ProcessSSEBuffer();
  void FinishWithSuccess();
  void FinishWithError(const std::string& error);

  std::optional<ResolvedConfig> ResolveConfig(
      const CompletionRequest& request) const;
  std::string GetConfiguredApiKey() const;
  std::optional<LocalServerConfig> GetLocalServerConfig() const;
  static std::string GetMacOSAppDefaultsString(const char* key);

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  std::string sse_buffer_;
  std::string accumulated_response_;
  TokenCallback on_token_;
  CompleteCallback on_complete_;
  ErrorCallback on_error_;

  base::WeakPtrFactory<MahoAiLlmClient> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_LLM_CLIENT_H_
