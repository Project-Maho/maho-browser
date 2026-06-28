// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_AI_LLM_CLIENT_H_
#define MAHO_BROWSER_AI_MAHO_AI_LLM_CLIENT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "url/gurl.h"

class PrefService;

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

class MahoAiLlmClientStreamConsumer;

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
  using ErrorCallback = base::OnceCallback<void(const std::string& error)>;

  struct ToolCall {
    ToolCall();
    ~ToolCall();
    ToolCall(ToolCall&&);
    ToolCall& operator=(ToolCall&&);

    int index = -1;
    std::string id;
    std::string name;
    std::string arguments_json;
  };

  struct CompletionResult {
    CompletionResult();
    ~CompletionResult();
    CompletionResult(CompletionResult&&);
    CompletionResult& operator=(CompletionResult&&);

    std::string full_text;
    std::vector<ToolCall> tool_calls;
    std::optional<std::string> finish_reason;
  };

  using CompleteCallback =
      base::OnceCallback<void(CompletionResult result)>;

  struct RequestPlan {
    GURL url;
    std::string body_json;
  };

  class ProviderTransport {
   public:
    virtual ~ProviderTransport() = default;

    virtual bool HasConfiguredCredential(PrefService* prefs) const = 0;
    virtual std::optional<std::string> ResolveEndpoint(
        const CompletionRequest& request,
        PrefService* prefs) const = 0;
    virtual std::optional<std::string> ResolveModel(
        const CompletionRequest& request,
        PrefService* prefs) const = 0;
    virtual RequestPlan CreateRequestPlan(CompletionRequest request,
                                          const std::string& endpoint,
                                          const std::string& model) const = 0;
    virtual void ProcessResponseBody(
        std::string body,
        std::string* sse_buffer,
        CompletionResult* completion_result,
        const TokenCallback& on_token,
        bool* overflowed) const = 0;
  };

  MahoAiLlmClient(
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      bool provider_adapter_v2_enabled);
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
  friend class MahoAiLlmClientStreamConsumer;

  void OnResponseDataReceived(std::string body_chunk);
  void OnResponseComplete(bool success);
  void FinishWithSuccess();
  void FinishWithError(const std::string& error);

  SEQUENCE_CHECKER(sequence_checker_);

  friend class MahoAiLlmClientStreamConsumer;

  raw_ptr<PrefService> prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> url_loader_;
  std::unique_ptr<MahoAiLlmClientStreamConsumer> stream_consumer_;
  bool provider_adapter_v2_enabled_ = false;
  std::unique_ptr<ProviderTransport> provider_transport_;
  std::string sse_buffer_;
  CompletionResult completion_result_;
  TokenCallback on_token_;
  CompleteCallback on_complete_;
  ErrorCallback on_error_;

  base::WeakPtrFactory<MahoAiLlmClient> weak_factory_{this};
};

#endif  // MAHO_BROWSER_AI_MAHO_AI_LLM_CLIENT_H_
