#ifndef MAHO_BROWSER_AI_MAHO_CHAT_ORCHESTRATION_SERVICE_H_
#define MAHO_BROWSER_AI_MAHO_CHAT_ORCHESTRATION_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/values.h"
#include "maho/browser/ai/maho_ai_llm_client.h"
#include "maho/browser/ai/maho_ai_page_context_extractor.h"
#include "maho/browser/ai/maho_conversation_task.h"
#include "maho/browser/ai/maho_tool_execution_graph.h"
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

class Browser;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace maho_ai::mojom {
enum class ChatIntent;
}  // namespace maho_ai::mojom

struct ChatSession;
struct MahoChatConfig;

class MahoChatOrchestrationService {
 public:
  enum class RequestMode {
    kGeneralChat,
    kPageQuestion,
    kPageTransformation,
    kBrowserAction,
    kToolAssistedTask,
  };

  struct RequestClassification {
    RequestMode request_mode = RequestMode::kGeneralChat;
    bool requires_page_context = false;
  };

  using TokenCallback = base::RepeatingCallback<void(const std::string&)>;
  using CompleteCallback = base::OnceCallback<void(const std::string& full_text)>;
  using ErrorCallback = base::OnceCallback<void(const std::string& error)>;

  MahoChatOrchestrationService(
      Browser* browser,
      PrefService* prefs,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  MahoChatOrchestrationService(const MahoChatOrchestrationService&) = delete;
  MahoChatOrchestrationService& operator=(
      const MahoChatOrchestrationService&) = delete;
  ~MahoChatOrchestrationService();

  bool HasConfiguredCredential() const;
  void Reset();

  void HandleMessage(const std::string& message,
                     maho_ai::mojom::ChatIntent chat_intent,
                     bool force_page_context,
                     TokenCallback on_token,
                     CompleteCallback on_complete,
                     ErrorCallback on_error);

  static RequestClassification ClassifyRequest(
      const std::string& message,
      maho_ai::mojom::ChatIntent chat_intent);
  static bool RequestModeRequiresPageContext(RequestMode mode);
  static std::string GetRequestModeName(RequestMode mode);
  static std::string GetIntentName(maho_ai::mojom::ChatIntent intent);
  static std::string GetPageContextFailureMessage(
      MahoAiPageContextExtractor::PageContextResult::Status status);

 private:
  using PageContextResult = MahoAiPageContextExtractor::PageContextResult;

  struct ConversationRoundState {
    ConversationRoundState();
    ConversationRoundState(const ConversationRoundState&) = delete;
    ConversationRoundState& operator=(const ConversationRoundState&) = delete;
    ConversationRoundState(ConversationRoundState&&);
    ConversationRoundState& operator=(ConversationRoundState&&);
    ~ConversationRoundState();

    std::string message;
    maho_ai::mojom::ChatIntent chat_intent;
    RequestClassification classification;
    PageContextResult page_context;
    TokenCallback on_token;
    CompleteCallback on_complete;
    ErrorCallback on_error;
    base::ListValue prompt_messages;
    std::vector<MahoAiLlmClient::ToolCall> pending_tool_calls;
    std::string assistant_text;
    size_t round_index = 0;
    size_t pending_tool_index = 0;

    // --- Phase 4.1.5: structured state machine + tool execution ---

    // Per-turn conversation lifecycle with bounded 2 MB accumulator (H13).
    std::unique_ptr<maho::ConversationTask> task;

    // Tool executor with deterministic member lifetime (C2 closed).
    std::unique_ptr<maho::ToolExecutionGraph> tool_graph;
  };

  struct ToolInvocation {
    std::string id;
    std::string name;
    std::string arguments_json;
  };

  static constexpr int kMaxToolRounds = 4;

  void OnPageContextReady(const std::string& message,
                          maho_ai::mojom::ChatIntent chat_intent,
                          RequestClassification classification,
                          TokenCallback on_token,
                          CompleteCallback on_complete,
                          ErrorCallback on_error,
                          PageContextResult page_context);

  static std::string ShapeRustEvent(const std::string& message,
                                    maho_ai::mojom::ChatIntent chat_intent,
                                    const RequestClassification& classification,
                                    const PageContextResult& page_context,
                                    bool structured_context_v2_enabled,
                                    bool browser_tools_v1_enabled);
  static std::string HandleEventOnBackground(const std::string& event_json);

  void OnRustEventResponse(const std::string& message,
                           maho_ai::mojom::ChatIntent chat_intent,
                           RequestClassification classification,
                           PageContextResult page_context,
                           TokenCallback on_token,
                           CompleteCallback on_complete,
                           ErrorCallback on_error,
                           const std::string& rust_response_json);

  void StartConversationRound(
      std::shared_ptr<ConversationRoundState> state);
  void OnLlmRoundComplete(std::shared_ptr<ConversationRoundState> state,
                          MahoAiLlmClient::CompletionResult result);
  void ExecuteNextToolInvocation(std::shared_ptr<ConversationRoundState> state);
  void ExecuteToolInvocation(std::shared_ptr<ConversationRoundState> state,
                             const ToolInvocation& tool_invocation);
  void OnToolInvocationComplete(std::shared_ptr<ConversationRoundState> state,
                                ToolInvocation tool_invocation,
                                base::DictValue result);
  void AppendAssistantToolCallMessage(
      base::ListValue* prompt_messages,
      const std::string& assistant_text,
      const std::vector<MahoAiLlmClient::ToolCall>& tool_calls);
  void AppendToolResultMessage(base::ListValue* prompt_messages,
                               const ToolInvocation& tool_invocation,
                               const base::DictValue& result);
  static bool ResponseRequestsTools(
      const MahoAiLlmClient::CompletionResult& result);
  static std::string SerializeJsonValue(const base::DictValue& value);

  void OnLlmToken(const TokenCallback& on_token,
                  std::shared_ptr<ConversationRoundState> state,
                  const std::string& token);
  void FinalizeResponse(CompleteCallback on_complete,
                        const std::string& full_text);
  void SendLlmResultToRust(const std::string& request_id,
                           const std::string& full_text);

  SEQUENCE_CHECKER(sequence_checker_);

  raw_ptr<Browser> browser_;
  raw_ptr<PrefService> prefs_;
  MahoAiLlmClient llm_client_;
  MahoAiPageContextExtractor page_context_extractor_;

  bool structured_context_v2_enabled_ = false;
  bool browser_tools_v1_enabled_ = true;
  bool chat_orchestrator_v2_enabled_ = false;

  std::string current_request_id_;
  bool received_llm_content_ = false;

  base::WeakPtrFactory<MahoChatOrchestrationService> weak_factory_{this};

#if defined(MAHO_RUST_AI)
 public:
  struct FfiUserData {
    FfiUserData();
    ~FfiUserData();
    base::WeakPtr<MahoChatOrchestrationService> weak_service;
    scoped_refptr<base::SequencedTaskRunner> task_runner;
  };

 private:
  void OnRustToken(const std::string& token);
  void OnRustComplete(const std::string& full_text,
                      const std::string& tool_calls_json);
  void OnRustError(const std::string& error);

  static void OnRustTokenCallback(void* user_data, const char* token);
  static void OnRustCompleteCallback(void* user_data,
                                     const char* full_text,
                                     const char* tool_calls_json);
  static void OnRustErrorCallback(void* user_data, const char* error);

  ChatSession* rust_session_ = nullptr;
  std::unique_ptr<FfiUserData> ffi_user_data_;
  std::shared_ptr<ConversationRoundState> active_round_state_;
#endif
};

#endif  // MAHO_BROWSER_AI_MAHO_CHAT_ORCHESTRATION_SERVICE_H_
