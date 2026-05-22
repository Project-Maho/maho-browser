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
#include "maho/browser/ui/webui/maho_ai/maho_ai.mojom.h"

class Browser;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace maho_ai::mojom {
enum class ChatIntent;
}  // namespace maho_ai::mojom

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

  void OnLlmToken(const TokenCallback& on_token, const std::string& token);
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
};

#endif  // MAHO_BROWSER_AI_MAHO_CHAT_ORCHESTRATION_SERVICE_H_
